-- Does a write tap on the GSP program space see what the host port writes?
--
-- Section 33.8 measured 492,544 writes to this space over 600 frames and NOT ONE
-- nonzero value, which cannot be right: docs/mc68010-boot-findings.md section 31
-- shows the 68010 issuing `move.w D0,$c0000c.l` with data 0x00DF at $2666 and
-- 0xDFDF/0xDF00 at $2674 on frames 45-47. Section 33.9 flagged that contradiction
-- and refused to let 33.8 be cited until it was settled. This settles it.
--
-- The question is only about the observer, not about the machine, so this prints
-- rather than summarising: every write in a narrow frame window, with address,
-- value and mask, so a 0x00DF either appears or does not. Summaries are exactly
-- what made the contradiction possible in the first place.
--
-- It also separates a third possibility from the other two. MD-09 records the
-- service-mode GSP VRAM diagnostic at ROM $004F82 reading back 0xFFFF. That runs
-- under IN0 bit 5 and therefore not at all in `--input none`, so the absence of
-- diagnostic writes here says nothing about MD-09 -- and the presence of text
-- writes here says everything about whether the tap is blind.
local machine = manager.machine
local gsp = assert(machine.devices[':mainpcb:gsp'], 'no :mainpcb:gsp')
local sp = assert(gsp.spaces['program'], 'no GSP program space')

local BASE = 0xff800000
local lo = tonumber(os.getenv('STUNRUN_TAPWINDOW_LO') or '44')
local hi = tonumber(os.getenv('STUNRUN_TAPWINDOW_HI') or '48')
local printed = 0
local limit = tonumber(os.getenv('STUNRUN_TAPWINDOW_LIMIT') or '60')
local seen = 0
local nonzero = 0

sp:install_write_tap(BASE, BASE + 0x3fffff, 'tapwindow', function(offset, value, mask)
    seen = seen + 1
    if value ~= 0 then
        nonzero = nonzero + 1
        if printed < limit then
            printed = printed + 1
            print(string.format('TAPW NONZERO off=0x%06X value=0x%04X mask=0x%04X frame=%d',
                offset & 0x3fffff, value, mask, frame))
        end
    end
end)

local frame = 0
emu.register_frame_done(function()
    frame = frame + 1
    if frame < lo or frame > hi then return end
    if frame == lo then
        print(string.format('TAPW window frames %d..%d, limit %d', lo, hi, limit))
    end
    print(string.format('TAPW frame=%d writes=%d nonzero=%d printed=%d', frame, seen, nonzero, printed))
end)