-- How far up the GSP share does the board actually write, and with what?
--
-- SP-338 Page 10's block diagram reads "VRAM Array (16) 64K x 4", which is
-- 512 KiB of silicon. MAME installs a 4 MiB share (harddriv.cpp:628). If MAME is
-- too big, everything above 512 KiB should alias back down, and a difference is
-- only observable if the ROM writes something NONZERO above 0x80000.
--
-- docs/mc68010-boot-findings.md section 33.6 records that question. This is the
-- measurement that answers it, and it is deliberately narrow: a write tap on the
-- GSP program space, counting writes per 128 KiB page and tracking the highest
-- offset that carries a nonzero value.
--
-- The zero-fill at $548E is the reason this is not trivially zero. It walks
-- 0x384000 bytes from share offset 0, so it necessarily touches pages 2 and 3 --
-- but it carries 0x0000, which is indistinguishable from fresh RAM. Only a
-- nonzero above 512 KiB makes the size question bite.
local machine = manager.machine
local gsp = assert(machine.devices[':mainpcb:gsp'], 'no :mainpcb:gsp')
local sp = assert(gsp.spaces['program'], 'no GSP program space')

local max_any, max_nonzero = -1, -1
local writes, nonzero, above_nonzero = 0, 0, 0
local per_page = {}

local BASE = 0xff800000
sp:install_write_tap(BASE, BASE + 0x3fffff, 'reach', function(offset, value, mask)
    -- offset is absolute in this space; the share offset is the low 22 bits.
    local byte = offset & 0x3fffff
    writes = writes + 1
    if value ~= 0 then
        nonzero = nonzero + 1
        if byte > max_nonzero then max_nonzero = byte end
        if byte >= 0x80000 then above_nonzero = above_nonzero + 1 end
    end
    if byte > max_any then max_any = byte end
    local page = byte >> 17
    per_page[page] = (per_page[page] or 0) + 1
end)

local frame = 0
emu.register_frame_done(function()
    frame = frame + 1
    if frame ~= tonumber(os.getenv('STUNRUN_REACH_FRAME') or '600') then return end
    print(string.format('REACH frame=%d writes=%d nonzero_writes=%d', frame, writes, nonzero))
    print(string.format(
        'REACH max_offset_any=0x%06X max_offset_NONZERO=0x%06X nonzero_at_or_above_512KiB=%d',
        max_any, max_nonzero, above_nonzero))
    local keys = {}
    for k in pairs(per_page) do keys[#keys + 1] = k end
    table.sort(keys)
    for _, p in ipairs(keys) do
        print(string.format('REACH page %d 0x%06X-0x%06X writes=%d%s',
            p, p << 17, (p << 17) + 0x1FFFF, per_page[p],
            (p << 17) >= 0x80000 and '  <-- ABOVE 512 KiB' or ''))
    end
end)