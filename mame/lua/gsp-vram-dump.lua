-- Dump the STUN Runner GSP VRAM backing share as consecutive 16-bit words.
--
-- Why this exists alongside tools/mame-gsp-state-snapshot: that tool SAMPLES
-- the share.  Its vram_high_sum32 is a stride-0x4000 sweep (see
-- sample_range(0xff800000, 0x800000, 0x4000) in gsp-state-snapshot.lua), which
-- is the right shape for a checksum and the wrong shape for an image.  A
-- stride sampler cannot answer "are these 512 KiB identical", so tools/mame-
-- memory-snapshot was tried for a full capture and returned 262,144 zeros over
-- the same range where the sampler reported vram_high_sum32 = 8,490,604.
--
-- TMS34010 program addresses are BIT addresses (addrshift=3). One physical
-- 16-bit word is therefore 0x10 address units, not 2. The old `+ index * 2`
-- loop walked overlapping bit-address windows, wrote a 4 MiB file, and called
-- it the physical share. The direct MAME memory_share hook (harddriv_v.cpp)
-- shows the actual backing is 524288 bytes; the corrected loop below uses the
-- bit-address stride. See docs/mc68010-boot-findings.md section 34.
--
-- The physical share is 512 KiB = 262144 uint16 words. The mapped GSP bit
-- address range is 0x400000 units; 0x400000 / 8 = 0x80000 physical bytes.
-- The direct C++ memory_share capture is the authority; this Lua path is a
-- reproducible cross-check now that it walks in correct word units.
--
-- The nonzero-word count is reported as data, not as a success check. A real
-- early boot can leave the share all-zero after a hardware clear; zero is valid
-- content and must not be rewritten as an observer failure.
--
-- Environment:
--   STUNRUN_GSP_VRAM_OUT    output path (required)
--   STUNRUN_GSP_VRAM_FRAME  frame to capture at (default 600)
--   STUNRUN_GSP_VRAM_WORDS  words to capture (default 262144 = 512 KiB share)
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
    -- emu.register_prestart never fires on this build: MAME registers it as a
    -- one_time MACHINE_NOTIFY_RESET notifier (luaengine.cpp:862) which fires
    -- before -autoboot_script is loaded. Applied here at the first frame
    -- instead. That is later than a true prestart, so any input that must be
    -- in effect before frame 0 needs a different answer, not this one.
    -- See docs/mc68010-boot-findings.md section 31.3.
    local pr_start_applied = false
    emu.register_frame_done(function()
        if pr_start_applied then return end
        pr_start_applied = true
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
        local value = space:read_u16(base + index * 0x10)
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

    -- Palette banks, same capture moment as the VRAM above.
    --
    -- gsp_render.py reads gsp_pal_lo.bin / gsp_pal_hi.bin from the dumpdir and
    -- builds the pens with harddriv_v.cpp:326 gsp_palette_change()
    -- (red=(lo>>8)&0xff, green=lo&0xff, blue=hi&0xff).  Without these a
    -- dumpdir cannot be rendered, so every VRAM-only capture had to be paired
    -- with a capture from a different tool at a possibly different frame --
    -- which is not a comparison, it is a coincidence.  See findings section 81.
    --
    -- Written only when STUNRUN_GSP_DUMP_PALETTE is set, so an existing
    -- invocation that expects one output file still gets one.
    if os.getenv('STUNRUN_GSP_DUMP_PALETTE') then
        -- The palette is NOT in VRAM.  The 68010 writes it through the GSP
        -- palette windows at 0xF5000000 (red+green) and 0xF5800000 (blue);
        -- reading it out of the VRAM space yields 1024 all-zero pens and the
        -- render comes out uniformly black, which looks like "empty VRAM"
        -- rather than "wrong address space".  See findings section 81.
        --
        -- Stride and window, from harddriv_v.cpp:
        --   hdgsp_paletteram_lo_r(offs_t offset)  -- a BYTE offset
        --       offset = m_gfx_palettebank * 0x100 + (offset & 0xff);
        --       return m_gsp_paletteram_lo[offset];
        -- So 256 consecutive 16-bit entries, not a 4-byte stride, and only the
        -- first 256 are reachable without a palette bank.  gsp_render.py's
        -- build_palette wants red=(lo>>8), green=lo&0xff, blue=hi&0xff over the
        -- whole array, so dump the whole 1024-entry backing array with the
        -- handler's own indexing: bank 0 for entries 0-255 and bank 1 for
        -- 256-511 is NOT how this ROM works, so read the backing store.
        --
        -- Read through the MAINCPU space, which is where those windows are
        -- mapped (hd68k_gsp_io_r/w).
        local cpu_space = assert(machine.devices[':mainpcb:maincpu'].spaces['program'],
                                 'no maincpu program space')
        -- Read the backing store, not the window.  harddriv.cpp:399-400 declares
        --   m_gsp_paletteram_lo(*this, "gsp_palram_lo", 1024*2, LITTLE)
        --   m_gsp_paletteram_hi(*this, "gsp_palram_hi", 1024*2, LITTLE)
        -- i.e. 1024 little-endian 16-bit pens each.  The 68010 window at
        -- 0xf5000000-0xf5000fff goes through hdgsp_paletteram_lo_r, which does
        --   offset = m_gfx_palettebank * 0x100 + (offset & 0xff)
        --   return m_gsp_paletteram_lo[offset];
        -- so the window can only ever reach 256 entries at whatever bank the
        -- driver currently holds, and reading it for pen N>255 is not possible
        -- without driving the bank register.  gsp_render.py wants all 1024.
        -- The share name carries the MACHINE TAG.  harddriv.cpp:399-400 declares
        --   m_gsp_paletteram_lo(*this, "gsp_palram_lo", 1024*2, LITTLE)
        -- on the mainpcb device, so the Lua key is ':mainpcb:gsp_palram_lo',
        -- not ':gsp_palram_lo'.  Enumerating machine.memory.shares on the
        -- pinned build returns exactly:
        --   :mainpcb:adsp_data :mainpcb:adsp_pgm_memory
        --   :mainpcb:gsp_control_hi :mainpcb:gsp_control_lo
        --   :mainpcb:gsp_palram_hi :mainpcb:gsp_palram_lo :mainpcb:gsp_vram
        -- The untagged key finds nothing, which is what made this tap print
        -- "M1_GSP_PALETTE unavailable shares" and sent findings section 81 to
        -- conclude the palette needed a C++ observer.  It does not.  Verified
        -- 2026-10-18: with the tagged key the dump below reproduces
        -- tools/mame-gsp-state-snapshot's service capture.  See section 101.
        local lo_share = machine.memory.shares[':mainpcb:gsp_palram_lo']
                          or machine.memory.shares[':gsp_palram_lo']
        local hi_share = machine.memory.shares[':mainpcb:gsp_palram_hi']
                          or machine.memory.shares[':gsp_palram_hi']
        if lo_share and hi_share then
            local PENS = 1024
            local function dump_pal(path, share)
                local pf = assert(io.open(path, 'wb'), 'cannot open ' .. path)
                local c, p = {}, 0
                for i = 0, PENS - 1 do
                    local v = share:read_u16(i * 2)
                    c[#c + 1] = string.char(v & 0xff, (v >> 8) & 0xff)
                    p = p + 1
                    if p == 4096 then pf:write(table.concat(c)); c = {}; p = 0 end
                end
                if p > 0 then pf:write(table.concat(c)) end
                pf:close()
            end
            dump_pal(out_path:gsub('gsp_vram%.bin$', 'gsp_pal_lo.bin'), lo_share)
            dump_pal(out_path:gsub('gsp_vram%.bin$', 'gsp_pal_hi.bin'), hi_share)
        else
            -- Real limitation, now narrower than it was.  Only if the shares
            -- are genuinely absent from machine.memory.shares does this fire:
            -- the 0xf5000000 window can then only ever reach 256 entries at
            -- whatever bank m_gfx_palettebank currently holds, so dumping
            -- through the window instead would produce a plausible-looking
            -- but WRONG palette, which is worse than not dumping one.
            -- Before the machine-tag fix above this branch fired on every run.
            print('M1_GSP_PALETTE unavailable shares -- VRAM dumped without palette')
            local _unused = cpu_space
        end
    end

    print(string.format('M1_GSP_VRAM frame=%d base=%08X words=%d nonzero=%d out=%s pc=%08X',
                        frame, base, words, nonzero, out_path, gsp.state['PC'].value))
    machine:exit()
end)
