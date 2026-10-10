-- mame_oracle_run.lua: headless oracle capture for STUN Runner
-- Usage: cat this_file | mame stunrun -skip_gameinfo -console -rompath ...
-- Or:    mame stunrun -debugcmdfile oracle_commands.txt

local TARGET_FRAMES = {1, 369, 411, 412, 447, 600}
local OUTPUT_DIR = os.getenv("MAME_ORACLE_DIR") or "/tmp/stunrunner_oracle"
local VERBOSE = os.getenv("MAME_ORACLE_VERBOSE") == "1"

local frame = 0
local done = false

local function mkdir(path)
    os.execute("mkdir -p " .. path)
end

local function write_file(filename, content)
    local f = io.open(filename, "wb")
    if not f then return false end
    f:write(content)
    f:close()
    return true
end

mkdir(OUTPUT_DIR)

-- Count frames by hooking the emu.frame_iterator (MAME 0.236+)
-- Alternatively, use the debugger to step and count
local machine = manager.machine

print("[oracle] Starting headless capture")
print("[oracle] target frames: " .. table.concat(TARGET_FRAMES, ","))
print("[oracle] output dir: " .. OUTPUT_DIR)

-- Check maincpu
local maincpu = machine.devices[":maincpu"]
if maincpu then
    print("[oracle] maincpu found: " .. maincpu:name())
else
    print("[oracle] ERROR: maincpu not found")
end

-- Check for GSP
local gsp = machine.devices[":gsp"]
if gsp then
    print("[oracle] GSP found: " .. gsp:name())
else
    print("[oracle] GSP device not found, searching...")
    for k, v in pairs(machine.devices) do
        if string.find(k, "gsp") or string.find(v:name() or "", "34010") then
            print("  device: " .. k .. " = " .. v:name())
        end
    end
end

-- Check memory regions
print("[oracle] Memory regions:")
for k, v in pairs(machine.memory) do
    print("  " .. k .. " -> " .. tostring(v))
end

-- Take a snapshot of current state
local function capture_frame(framenum)
    local prefix = string.format(OUTPUT_DIR .. "/frame_%03d", framenum)

    -- Capture maincpu RAM (0x000000-0x00ffff = 64KB)
    local maincpu = machine.devices[":maincpu"]
    if maincpu and maincpu.spaces and maincpu.spaces["program"] then
        local space = maincpu.spaces["program"]
        local data = ""
        for addr = 0, 0xffff do
            local b = space:read_u8(addr)
            data = data .. string.char(b)
        end
        write_file(prefix .. "_maincpu_ram.bin", data)
        print(string.format("[oracle] frame=%d: maincpu RAM captured", framenum))
    end

    -- Capture GSP VRAM if accessible
    -- GSP VRAM is typically mapped at 0x02000000 in maincpu space
    if maincpu and maincpu.spaces and maincpu.spaces["program"] then
        local space = maincpu.spaces["program"]
        local data = ""
        for addr = 0x02000000, 0x020fffff do
            local b = space:read_u8(addr)
            data = data .. string.char(b)
        end
        write_file(prefix .. "_gsp_vram.bin", data)
        print(string.format("[oracle] frame=%d: GSP VRAM captured", framenum))

        -- GSP palette at 0xF5000000
        data = ""
        for addr = 0xF5000000, 0xF50003ff do
            local b = space:read_u8(addr)
            data = data .. string.char(b)
        end
        write_file(prefix .. "_gsp_palette.bin", data)
        print(string.format("[oracle] frame=%d: GSP palette captured", framenum))
    end

    print(string.format("[oracle] frame=%d: ALL CAPTURED", framenum))
end

-- Register frame hook using the frameemitter callback
local frame_count = 0
machine:frameemitter():subscribe(function()
    frame_count = frame_count + 1
    local f = frame_count

    for _, target in ipairs(TARGET_FRAMES) do
        if f == target then
            print(string.format("[oracle] >>> CAPTURING FRAME %d <<<", f))
            capture_frame(f)
        end
    end

    if f >= 600 then
        print("[oracle] reached frame 600, exiting")
        done = true
        -- Force MAME to exit
        os.exit(0)
    end
end)

print("[oracle] Hooks registered, waiting for frames...")
