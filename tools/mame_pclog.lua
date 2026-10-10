-- Per-frame MC68010 PC series from MAME, for comparing against our shell's
-- STUNRUN_PCLOG output.  Written every frame to a single file so the two
-- timelines can be diffed directly instead of eyeballed.
--
--   STUNRUN_PCLOG=<path> ./mame-ref/mame stunrun -rompath stun-runner/roms \
--       -autoboot_script stun-runner/tools/mame_pclog.lua -video none \
--       -sound none -seconds_to_run 300 -nosleep -skip_gameinfo

local path = os.getenv("STUNRUN_PCLOG") or "/tmp/mame_pc.txt"
local fh = assert(io.open(path, "w"))
local cpu = manager.machine.devices[":mainpcb:maincpu"]
local frame = 0

emu.register_frame_done(function()
    frame = frame + 1
    fh:write(string.format("%d 0x%08X\n", frame, cpu.state["PC"].value))
    if frame >= 150 then
        fh:close()
        manager.machine:exit()
    end
end)