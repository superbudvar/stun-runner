-- High-resolution maincpu PC trace, for locating where our emulator's boot
-- diverges from MAME's.  tools/mame_pclog.lua samples once per frame, which is
-- far too coarse during the 230 400-iteration VRAM fill loops.
--
-- emu.register_periodic(period, func, param) calls func every `period` seconds
-- of EMULATED time, so a 1 ms period gives ~1000 samples per emulated second,
-- i.e. ~60 samples per 60 Hz frame.
--
-- Env:
--   STUNRUN_PCLOG2=<path>   where to write "sec frame 0xPC" lines
--   STUNRUN_PCLOG2_MS       period in ms (default 1)
--   STUNRUN_PCLOG2_FRAME    stop after this frame (default 0 = no limit)

local out_path = os.getenv("STUNRUN_PCLOG2") or "/tmp/stunrunner_oracle/pc2.txt"
local ms = tonumber(os.getenv("STUNRUN_PCLOG2_MS") or "1")
local last_frame = tonumber(os.getenv("STUNRUN_PCLOG2_FRAME") or "0")

local m = manager.machine
local cpu = m.devices[":mainpcb:maincpu"]
local sp = cpu.spaces["program"]
if sp == nil then error("no program space") end

local fh = io.open(out_path, "w")
if fh == nil then error("cannot open " .. out_path) end

local frame = 0
local samples = 0

-- device_state_entry is UPPERCASE and needs :value() on some builds; the
-- earlier scripts proved plain property access works for "PC".
local function pc_now()
    local ok, v = pcall(function() return cpu.state["PC"]:value() end)
    if ok and type(v) == "number" then return v end
    local ok2, w = pcall(function() return cpu.state["PC"] end)
    if ok2 and type(w) == "number" then return w end
    return 0
end

local n = 0
emu.register_periodic(ms / 1000.0, function()
    local pc = pc_now()
    samples = samples + 1
    if samples <= 25 then
        fh:write(string.format("PERIODIC-FIRES %d frame=%u pc=0x%08X\n", samples, frame, pc))
        fh:flush()
    end
    n = n + 1
end)

emu.register_frame_done(function()
    if frame < 25 then fh:write(string.format("FRAMEDONE %u\n", frame + 1)) end
    frame = frame + 1
    if last_frame > 0 and frame >= last_frame then
        fh:write(string.format("# %d frames, %d samples\n", frame, samples))
        fh:close()
        manager.machine:exit()
    end
end)