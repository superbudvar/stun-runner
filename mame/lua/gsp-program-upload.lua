-- Capture the 68010 -> GSP program-upload boundary from a power-on run.
--
-- What this answers, and what it deliberately does NOT answer
-- -----------------------------------------------------------
-- The board's TMS34010 has no program ROM: `mainpcb:user1` is the ADSP object
-- ROM, and the MC68010 uploads the GSP program into shared VRAM through the
-- host port while the GSP is held halted, then releases it. This script records
-- that transaction stream so the uploaded image can be separated from every
-- later runtime submission, and reports the exact frame of the halt release so
-- VRAM can be imaged on both sides of it in separate runs.
--
-- Three things here are measurements and must not be read as more:
--
--   * `destination` on an HSTDAT event is DERIVED, not observed. It composes
--     HSTADRH:HSTADRL the way the driver composes them -- including the
--     `addr & 0xfffffff0` store mask and the HSTCTL 0x0800 post-increment
--     (BUGS.md SR-20) -- so it is the address MAME would store at. The stored
--     words are the `data` field, which IS observed.
--   * `a0`/`a1`/`a2` on an HSTDAT event are CANDIDATE source pointers, sampled at
--     the store. No 68010 read tap is installed, so which of those registers
--     (if any) the copy loop actually reads is not established here.
--   * Nothing here proves the uploaded image is executable code. Whether the GSP
--     executes inside a written range is a separate question, answered where the
--     post-release PC samples land inside VRAM the upload wrote.
--
-- VRAM images are NOT taken here, for a documented reason.  MAME's Lua
-- reference states that a pass-through handler "may be removed due to other
-- changes to handlers in the address space" and that `passthrough:reinstall()`
-- "may be necessary" (docs/source/luascript/ref-mem.rst:197-200).  Reading the
-- shared VRAM is such a change.  The removal is SILENT -- no error, clean exit,
-- and a script that counts its own events sees zero and concludes the board did
-- nothing.  Measured here: the identical script records 1000 events without a
-- VRAM read and 0 events with one, and `handler:reinstall()` restores it
-- (29,287 events before the read, 139,120 after).
--
-- So the split is not a crash workaround: it is the supported way to run a tap
-- this hot.  Taking a 4 MiB image per frame across the upload window is also
-- why it was not simply done inline -- that did not finish 600 frames in four
-- minutes.  The wrapper runs this script for the event stream and the boundary
-- frame, then takes the two images with the proven tools/mame-gsp-vram-dump.
--
-- Why the event log is a preallocated ring buffer
-- ------------------------------------------------
-- A 60-frame run issues roughly 500,000 host-window writes. These callbacks are
-- NOT coroutines (ref-mem.rst), they fire once per memory access, so anything
-- per-event is a performance hazard by construction. Measured here on MAME
-- 0.289:
--
--   retaining one Lua table per event  -> returncode -11 (segfault, no Lua error)
--   calling file:write per event        -> returncode -11, output truncated
--                                           mid-line at event 339
--   growing a flat table from the tap   -> callback silently stops after 1493
--   assigning into a preallocated ring  -> 65536 events captured, 449508 counted
--                                           as dropped, clean exit
--
-- That is not an emulator defect and is not filed as one; it is why this script
-- preallocates `buf` at load and formats its output after the run. Overflow is
-- counted, never silently dropped.
--
-- Why tap_install_count is reported
-- ----------------------------------
-- It is the evidence that the tap set is installed exactly once per run and was
-- never silently dropped or reinstalled behind the caller's back.
--
-- Environment:
--   STUNRUN_GSP_UPLOAD_OUT         output directory (required)
--   STUNRUN_GSP_UPLOAD_FRAMES      bounded run length in frames (default 600)
--   STUNRUN_GSP_UPLOAD_CAP         ring capacity in events (default 800000;
--                                12 numbers each, so ~700k events cost well over
--                                100 MB of Lua memory -- and the ring keeps the
--                                FIRST cap events, so a boundary beyond it is
--                                reported as truncated rather than hidden)

local output = assert(os.getenv('STUNRUN_GSP_UPLOAD_OUT'),
    'STUNRUN_GSP_UPLOAD_OUT is required')
local limit = tonumber(os.getenv('STUNRUN_GSP_UPLOAD_FRAMES') or '600')
local cap = tonumber(os.getenv('STUNRUN_GSP_UPLOAD_CAP') or '800000')

local machine = manager.machine

local WIDTH = 12
local buf = {}
for index = 1, cap * WIDTH do buf[index] = 0 end
local stored = 0
local dropped = 0
local seq = 0

local frame = 0
local taps_installed = false
local hstadr_hi, hstadr_lo, hstctl = 0, 0, 0
-- Effective HSTADR, kept exactly as the driver keeps it.
--
-- The destination column must NOT be read off the latches directly. MAME's
-- tms34010 host_w stores at `addr & 0xfffffff0` and then, when HSTCTL bit
-- 0x0800 (INCW) is set, adds 0x10 back into both latches (BUGS.md SR-20 and
-- docs/tms34010-reference.md). A long upload therefore writes a CONTIGUOUS run
-- of addresses while the latch still shows the base of the run: reading the
-- latch reports one repeated address instead of the range actually written, and
-- a "which range did this writer fill" question gets a useless answer. So the
-- latches are composed here exactly as the driver composes them, and the
-- increment is applied after each HSTDAT.
local hstadr = 0
local released = false
local hstdat_writes = 0
local captured = false
local release_seq = 0
local release_frame = -1
local dest_lo, dest_hi = nil, nil
local samples = {}
local tap_install_count = 0

-- Device, space and register handles resolve inside install_taps(), which runs
-- from the first frame-done callback -- NOT at script load. Touching device
-- state before the machine has started also segfaults this build. Every other
-- script in mame/lua/ resolves handles the same way. install_taps() is
-- idempotent and is re-called after each VRAM image, because taking one stops
-- the taps.
local cpu, gsp, space, gsp_space, pc, a0, a1, a2, gsp_pc

-- MAME hands a write tap the FULL address in the space, not an offset relative
-- to the tap's base: the FIFO tap in display-list-trace.lua logs `addr=00C0000C`
-- for a tap installed at 0xC0000C. Comparing against range-relative values
-- records nothing while the run still reports success.
local HSTADRH_ADDR = 0xc00002
local HSTADRL_ADDR = 0xc00004
local HSTCTL_ADDR = 0xc00008
local HSTDAT_ADDR = 0xc0000c

local KIND_HOST = 1
local KIND_HSTADRH = 2
local KIND_HSTADRL = 3
local KIND_HSTCTL = 4
local KIND_HSTDAT = 5
local KIND_GSPRES = 6

-- One shared emitter for both windows, so `seq` is a single ordering across
-- host-port stores and /GSPRES transitions. A 32-bit CPU store appears as two
-- ordered 16-bit events, exactly as the native bus observer reports it.
-- Allocation-free by construction: no table is created and no table grows.
local function emit(kind, address, data, mask, derived_destination)
    seq = seq + 1
    if stored >= cap then
        dropped = dropped + 1
        return
    end
    local b = stored * WIDTH
    buf[b + 1] = seq
    buf[b + 2] = frame
    buf[b + 3] = kind
    buf[b + 4] = address
    buf[b + 5] = data
    buf[b + 6] = mask
    buf[b + 7] = derived_destination or 0
    buf[b + 8] = pc.value
    buf[b + 9] = a0.value
    buf[b + 10] = a1.value
    buf[b + 11] = a2.value
    -- Global emulated machine time, rounded to ns. At 48 MHz one GSP cycle is
    -- 20.8 ns; double->ns rounding is much finer than that and keeps the log
    -- deterministic. This is sampled in the same callback as the write, not
    -- inferred later from frame/sequence (which loses the CPU/GSP interleave).
    buf[b + 12] = math.floor(machine.time:as_double() * 1000000000 + 0.5)
    stored = stored + 1
    if kind == KIND_HSTDAT and derived_destination ~= nil then
        if dest_lo == nil or derived_destination < dest_lo then
            dest_lo = derived_destination
        end
        if dest_hi == nil or derived_destination > dest_hi then
            dest_hi = derived_destination
        end
    end
end

local function install_taps()
    cpu = assert(machine.devices[':mainpcb:maincpu'], 'no :mainpcb:maincpu')
    gsp = assert(machine.devices[':mainpcb:gsp'], 'no :mainpcb:gsp')
    space = assert(cpu.spaces['program'], 'no MC68010 program space')
    gsp_space = assert(gsp.spaces['program'], 'no GSP program space')
    pc = cpu.state['CURPC'] or cpu.state['PC']
    a0, a1, a2 = cpu.state['A0'], cpu.state['A1'], cpu.state['A2']
    gsp_pc = gsp.state['CURPC'] or gsp.state['PC']
    tap_install_count = tap_install_count + 1

    space:install_write_tap(0xc00000, 0xc0000f, 'stunrun_gsp_upload_host',
        function(address, data, mask)
            if address == HSTADRH_ADDR then
                hstadr_hi = data
                hstadr = (data << 16) | (hstadr & 0xffff)
                emit(KIND_HSTADRH, address, data, mask, nil)
            elseif address == HSTADRL_ADDR then
                hstadr_lo = data
                hstadr = (hstadr & 0xffff0000) | data
                emit(KIND_HSTADRL, address, data, mask, nil)
            elseif address == HSTCTL_ADDR then
                local was_halted = (hstctl & 0x8000) ~= 0
                local halts = (data & 0x8000) ~= 0
                hstctl = data
                emit(KIND_HSTCTL, address, data, mask,
                    was_halted and not halts and 2 or (halts and 1 or 0))
                -- The halt this board uses to hold the GSP during the upload is
                -- HSTCTL bit 15 (HLT), not /GSPRES: /GSPRES is released once at
                -- power-on, before the fill. Capture on the first HLT release
                -- that FOLLOWS host data writes, so the image is the upload and
                -- not the empty pre-upload reset release.
                if was_halted and not halts and hstdat_writes > 0 and not captured then
                    captured = true
                    release_seq = seq
                    release_frame = frame
                    print(string.format(
                        'M1_GSP_UPLOAD_RELEASE frame=%d seq=%d pc=%08X hstctl=%04X hstdat=%d',
                        frame, seq, pc.value, hstctl, hstdat_writes))
                end
            elseif address == HSTDAT_ADDR then
                hstdat_writes = hstdat_writes + 1
                local effective = hstadr & 0xfffffff0
                emit(KIND_HSTDAT, address, data, mask, effective)
                -- The driver writes back the incremented latch, so a following
                -- HSTADRH/HSTADRL store composes from the moved pointer.
                if (hstctl & 0x0800) ~= 0 then hstadr = hstadr + 0x10 end
            end
        end)

    -- MAME's board NWR handler ignores the written data and decodes the word
    -- index instead: bits 0-2 pick the function, bit 3 picks the line value,
    -- and function 6 drives the active-low /GSPRES line.
    space:install_write_tap(0x604000, 0x607fff, 'stunrun_gsp_upload_reset',
        function(address, data, mask)
            local word_index = (address - 0x604000) >> 1
            if (word_index & 7) ~= 6 then return end
            local assert_release = ((word_index >> 3) & 1) == 0
            emit(KIND_GSPRES, address, data, mask, assert_release and 1 or 0)
            if assert_release == released then return end
            released = assert_release
            print(string.format(
                'M1_GSP_UPLOAD_RESETLINE frame=%d seq=%d pc=%08X line=%s hstdat=%d',
                frame, seq, pc.value, assert_release and 'assert' or 'release',
                hstdat_writes))
        end)
end

local function write_event_log()
    local names = {'HOST', 'HSTADRH', 'HSTADRL', 'HSTCTL', 'HSTDAT', 'GSPRES'}
    local file = assert(io.open(output .. '/gsp-upload-events.txt', 'w'),
        'cannot open events file')
    for index = 0, stored - 1 do
        local b = index * WIDTH
        file:write(string.format('%d %d %s %08X %08X %08X %08X %08X %08X %08X %08X %.0f\n',
            buf[b + 1], buf[b + 2], names[buf[b + 3]], buf[b + 4], buf[b + 5],
            buf[b + 6], buf[b + 7], buf[b + 8], buf[b + 9], buf[b + 10],
            buf[b + 11], buf[b + 12]))
    end
    file:close()
end

-- Derived here rather than in the tap: a table keyed per writer PC would have
-- to grow inside the callback, which is exactly what crashes this build.
local function report_writer_pcs()
    local stats = {}
    local order = {}
    for index = 0, stored - 1 do
        local b = index * WIDTH
        local key = buf[b + 3] .. ':' .. buf[b + 8]
        local stat = stats[key]
        if stat == nil then
            stat = {kind = buf[b + 3], pc = buf[b + 8], count = 0,
                    first_frame = buf[b + 2], last_frame = buf[b + 2],
                    data_first = buf[b + 5], dest_first = buf[b + 7],
                    data_last = buf[b + 5], dest_last = buf[b + 7]}
            stats[key] = stat
            order[#order + 1] = key
        end
        stat.count = stat.count + 1
        stat.last_frame = buf[b + 2]
        stat.data_last = buf[b + 5]
        stat.dest_last = buf[b + 7]
    end
    table.sort(order)
    local names = {'HOST', 'HSTADRH', 'HSTADRL', 'HSTCTL', 'HSTDAT', 'GSPRES'}
    for _, key in ipairs(order) do
        local stat = stats[key]
        print(string.format(
            'M1_GSP_UPLOAD_PC kind=%s pc=%08X count=%d first_frame=%d last_frame=%d data_first=%08X dest_first=%08X data_last=%08X dest_last=%08X',
            names[stat.kind], stat.pc, stat.count, stat.first_frame,
            stat.last_frame, stat.data_first, stat.dest_first, stat.data_last,
            stat.dest_last))
    end
end

emu.register_frame_done(function()
    frame = frame + 1
    if not taps_installed then
        taps_installed = true
        install_taps()
        print(string.format('M1_GSP_UPLOAD_TAPS frame=%d seq=%d', frame, seq))
    end

    if captured and frame <= release_frame + 12 then
        samples[#samples + 1] = {
            frame = frame, pc = gsp_pc.value,
            st = (gsp.state['ST'] and gsp.state['ST'].value) or 0
        }
    end
    if frame < limit then return end

    -- The ring keeps the FIRST `cap` events, so a boundary beyond that window
    -- is absent from the log even though the counters found it. Say so instead
    -- of handing back a log that quietly stops before the interesting part.
    if release_seq > stored then
        print(string.format(
            'M1_GSP_UPLOAD_TRUNCATED release_seq=%d stored=%d', release_seq, stored))
    end

    write_event_log()
    for _, row in ipairs(samples) do
        print(string.format('M1_GSP_UPLOAD_SAMPLE frame=%d pc=%08X st=%08X',
            row.frame, row.pc, row.st))
    end
    report_writer_pcs()
    print(string.format(
        'M1_GSP_UPLOAD_DONE frames=%d seq=%d stored=%d dropped=%d release_seq=%d release_frame=%d hstdat_writes=%d dest_lo=%08X dest_hi=%08X tap_installs=%d time_ns=%.0f',
        frame, seq, stored, dropped, release_seq, release_frame, hstdat_writes,
        dest_lo or 0, dest_hi or 0, tap_install_count,
        math.floor(machine.time:as_double() * 1000000000 + 0.5)))
    machine:exit()
end)
