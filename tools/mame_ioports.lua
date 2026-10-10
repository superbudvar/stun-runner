-- Reads the input ports the MC68010 actually sees, so our fake_ports defaults
-- can be transcribed instead of guessed.  Two guesses (IN0 bit 5 and the
-- a80000 aux-start bit) decided whether the ROM ran the memory-test
-- diagnostics or the game, and both were wrong.
--
--   STUNRUN_IOPORT_DUMP=1 ./mame-ref/mame stunrun -rompath stun-runner/roms \
--       -autoboot_script stun-runner/tools/mame_ioports.lua -video none \
--       -sound none -seconds_to_run 60 -nosleep -skip_gameinfo

local function pcall_safe(f, d)
    local ok, v = pcall(f)
    if not ok then return d end
    return v
end

local ports = {
    "mainpcb:IN0", "mainpcb:SW1", "mainpcb:a80000",
    "mainpcb:8BADC.0", "mainpcb:12BADC.0", "mainpcb:12BADC.1",
}

local function dump(tag)
    for _, name in ipairs(ports) do
        local p = pcall_safe(function() return manager.machine.ioport(name) end, nil)
        if p then
            local v = pcall_safe(function() return p.value end, 0)
            local dflt = pcall_safe(function() return p.defvalue end, -1)
            local reset = pcall_safe(function() return p.resetvalue end, -1)
            print(string.format("%s %-20s value=0x%04X defvalue=0x%04X resetvalue=0x%04X",
                                tag, name, v or 0, dflt or 0, reset or 0))
        else
            print(string.format("%s %-20s <absent>", tag, name))
        end
    end
end

print("== frame 0 (power on) ==")
dump("t0")

local frame = 0
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 2 then
        print("== frame 2 ==")
        dump("t2")
    end
    -- Sample a few more frames so any value that settles late is visible.
    if frame == 5 or frame == 45 then
        print(string.format("== frame %d ==", frame))
        dump("t" .. frame)
    end
    if frame >= 60 then
        manager.machine:exit()
    end
end)