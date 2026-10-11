-- One-off investigation probe: settle the TMS34010 address unit.
--
-- Question: the GSP PC reported by MAME's debugger is the raw device
-- register (tms34010.cpp:726 `state_add(TMS34010_PC, "PC", m_pc)`), and
-- m_pc advances by `2 << 3` per 16-bit instruction (tms34010.cpp:164).  The
-- program space is declared with addr_shift = 3 (tms34010.cpp:67), and
-- emumem.h:2078 defines addr2byte(address) = address >> addr_shift.
--
-- So the device address is a BIT address and byte = addr >> 3.  But the
-- GSP VRAM is mapped at BYTE 0xff800000 (multisync_gsp_map), and
-- 0xff800000 << 3 does not fit in 32 bits.  This probe measures which
-- reading is right, instead of arguing about it.
--
-- It reads the GSP PC, then for each candidate byte address prints the
-- 16-bit value MAME actually returns.  A candidate in the VRAM returns
-- program data; an unmapped candidate returns 0.
--
-- Not a test harness: this is a bounded investigation probe, run once.

local machine = manager.machine
local frame = 0
local done = false

local function dump(tag)
    local gsp = machine.devices[':mainpcb:gsp']
    local space = gsp.spaces['program']
    local pc = gsp.state['PC'].value
    print(string.format(
        'PROBE %s pc=0x%08X pc>>3=0x%08X', tag, pc, pc >> 3))

    local candidates = {
        {'pc-as-byte',     pc},
        {'pc-shift3',      pc >> 3},
        {'pc-shift3-mask', (pc >> 3) & 0x01ffffff},
        {'pc-shift4',      pc >> 4},
        {'vram-base',      0xff800000},
        {'vram-base-shr3', 0xff800000 >> 3},
    }
    for _, c in ipairs(candidates) do
        local a = c[2]
        local ok, v = pcall(function() return space:read_u16(a) end)
        print(string.format('PROBE   %-16s addr=0x%08X value=%s',
              c[1], a, ok and string.format('0x%04X', v) or 'ERR'))
    end
end

emu.register_frame_done(function()
    frame = frame + 1
    if frame == 200 and not done then
        done = true
        dump('frame200')
    end
    if frame == 600 then
        dump('frame600')
        machine:exit()
    end
end)