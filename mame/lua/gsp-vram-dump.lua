-- Dump the STUN Runner GSP VRAM share as a full consecutive image at one frame.
--
-- Why this exists alongside tools/mame-gsp-state-snapshot: that tool SAMPLES
-- the share.  Its vram_high_sum32 is a stride-0x4000 sweep (see
-- sample_range(0xff800000, 0x800000, 0x4000) in gsp-state-snapshot.lua), which
-- is the right shape for a checksum and the wrong shape for an image.  A
-- stride sampler cannot answer "are these 512 KiB identical", so tools/mame-
-- memory-snapshot was tried for a full capture and returned 262,144 zeros over
-- the same range where the sampler reported vram_high_sum32 = 8,490,604.
--
-- So this reads every word consecutively instead.  The output is byte-for-byte
-- comparable against a native board-model replay: same address range, same
-- little-endian word order that native/gsp_rtl/gsp_board.{c,h} stores.
--
-- The share is 512 KiB, not 4 MiB (docs/mc68010-boot-findings.md 12.1:
-- m_vram_mask = 0x3FFFF), and it is read at the start of the declared 4 MiB
-- host window, 0xff800000.
--
-- The nonzero-word count is reported because a misconfigured read is silent:
-- it yields a correctly sized file full of zeros that looks like a valid
-- capture.  A zero count here means the dump is worthless, so the wrapper
-- treats it as a failure rather than an artifact.
--
-- Environment:
--   STUNRUN_GSP_VRAM_OUT    output path (required)
--   STUNRUN_GSP_VRAM_FRAME  frame to capture at (default 600)
--   STUNRUN_GSP_VRAM_WORDS  words to capture (default 262144 = the whole share)
--   STUNRUN_GSP_VRAM_INPUT  input mode; same names and timing as
--                           gsp-state-snapshot.lua, so a dump and a state
--                           snapshot can be taken of the same machine state

local machine = manager.machine
local gsp = assert(machine.devices[':mainpcb:gsp'], 'no :mainpcb:gsp')
local space = assert(gsp.spaces['program'], 'no GSP program space')

local out_path = assert(os.getenv('STUNRUN_GSP_VRAM_OUT'), 'STUNRUN_GSP_VRAM_OUT is required')
local target = tonumber(os.getenv('STUNRUN_GSP_VRAM_FRAME') or '600')
local words = tonumber(os.getenv('STUNRUN_GSP_VRAM_WORDS') or '262144')
local base = 0xff800000
local input_mode = os.getenv('STUNRUN_GSP_VRAM_INPUT') or 'none'

local sw_off_prefix = {
    {port = ':mainpcb:SW1', field = 'SW1:1', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:2', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:3', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:4', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:5', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:6', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:7', action = 'set', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:8', action = 'set', value = 1}
}
local events = (input_mode == 'service' or input_mode == 'service_probe') and {
    {frame = 60, port = ':mainpcb:IN0', field = 'Service Mode', action = 'press'},
    {frame = 120, port = ':mainpcb:IN0', field = 'Service Mode', action = 'release'}
} or input_mode == 'late_drive' and {
    {frame = 650, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press'},
    {frame = 680, port = ':mainpcb:IN0', field = 'Coin 1', action = 'release'},
    {frame = 750, port = ':mainpcb:a80000', field = '1 Player Start', action = 'press'},
    {frame = 780, port = ':mainpcb:a80000', field = '1 Player Start', action = 'release'},
    {frame = 900, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 220},
    {frame = 900, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'press'},
    {frame = 1500, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 128},
    {frame = 1500, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'release'}
} or input_mode == 'late' and {
    {frame = 650, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press'},
    {frame = 680, port = ':mainpcb:IN0', field = 'Coin 1', action = 'release'},
    {frame = 750, port = ':mainpcb:a80000', field = '1 Player Start', action = 'press'},
    {frame = 780, port = ':mainpcb:a80000', field = '1 Player Start', action = 'release'}
} or input_mode == 'drive' and {
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', action = 'set', value = 0},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', action = 'set', value = 1},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', action = 'set', value = 0},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', action = 'set', value = 1},
    {frame = 600, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'set', value = 0},
    {frame = 600, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 220},
    {frame = 1200, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'set', value = 1},
    {frame = 1200, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 128}
} or input_mode == 'fork_hold_left' and {
    {frame = 2, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 0},
    {frame = 2, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'release'},
    {frame = 2, port = ':mainpcb:a80000', field = 'P1 Button 2', action = 'release'}
} or input_mode == 'fork_hold_right' and {
    {frame = 2, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 255},
    {frame = 2, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'release'},
    {frame = 2, port = ':mainpcb:a80000', field = 'P1 Button 2', action = 'release'}
} or input_mode == 'fork_center' and {
    {frame = 2, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 128},
    {frame = 2, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'release'},
    {frame = 2, port = ':mainpcb:a80000', field = 'P1 Button 2', action = 'release'},
    {frame = 1800, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 128}
} or input_mode == 'sw_off_prestart' and {
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press'},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', action = 'release'},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', action = 'press'},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', action = 'release'}
} or {}
local next_event = 1

local function apply_event(event)
    local port = assert(machine.ioport.ports[event.port], 'unknown input port: ' .. event.port)
    local field = assert(port.fields[event.field], 'unknown input field: ' .. event.port .. '/' .. event.field)
    if event.action == 'press' then
        field:set_value(1)
    elseif event.action == 'release' then
        field:set_value(0)
    else
        field:set_value(event.value)
    end
end

if input_mode == 'sw_off_prestart' or input_mode == 'drive' or input_mode == 'late' or input_mode == 'late_drive' then
    emu.register_prestart(function()
        for _, event in ipairs(sw_off_prefix) do apply_event(event) end
    end)
end

local frame = 0
local done = false

emu.register_frame_done(function()
    while next_event <= #events and events[next_event].frame <= frame do
        apply_event(events[next_event])
        next_event = next_event + 1
    end
    frame = frame + 1
    if frame < target or done then return end
    done = true

    local file = assert(io.open(out_path, 'wb'), 'cannot open ' .. out_path)
    local nonzero = 0
    local chunk = {}
    local pending = 0
    for index = 0, words - 1 do
        local value = space:read_u16(base + index * 2)
        if value ~= 0 then nonzero = nonzero + 1 end
        chunk[#chunk + 1] = string.char(value & 0xff, (value >> 8) & 0xff)
        pending = pending + 1
        if pending == 4096 then
            file:write(table.concat(chunk))
            chunk = {}
            pending = 0
        end
    end
    if pending > 0 then file:write(table.concat(chunk)) end
    file:close()

    print(string.format('M1_GSP_VRAM frame=%d base=%08X words=%d nonzero=%d out=%s pc=%08X',
                        frame, base, words, nonzero, out_path, gsp.state['PC'].value))
    machine:exit()
end)