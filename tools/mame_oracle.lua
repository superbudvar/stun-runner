-- mame_oracle.lua: MAME oracle harness for STUN Runner RE
-- Usage: mame stunrun -skip_gameinfo -rompath /path/to/roms -autoboot script.lua
--
-- Captures memory/VRAM state at specific frames and writes binary files
-- that our native harness can load as oracles.

local TARGET_FRAMES = {1, 369, 411, 412, 447, 600}
local OUTPUT_DIR = os.getenv("MAME_ORACLE_DIR") or "/tmp/stunrunner_oracle"
local VERBOSE = os.getenv("MAME_ORACLE_VERBOSE") == "1"

-- Ensure output directory exists
local function mkdir(path)
    os.execute("mkdir -p " .. path)
end

local function write_u8(filename, data)
    local f = io.open(filename, "wb")
    if not f then return false end
    for _, v in ipairs(data) do
        f:write(string.char(v))
    end
    f:close()
    return true
end

local function dump_maincpu_ram()
    -- Main MC68010 RAM: 0x000000-0x00ffff (64KB)
    -- Accessed via maincpu program space
    local cpu = manager.machine.devices[":mainpcb:maincpu"]
    if not cpu then
        if VERBOSE then print("[oracle] maincpu not found") end
        return false
    end
    local space = cpu.spaces["program"]
    if not space then
        if VERBOSE then print("[oracle] program space not found") end
        return false
    end

    local RAM_START = 0x000000
    local RAM_SIZE = 0x10000
    local filename = OUTPUT_DIR .. "/maincpu_ram.bin"
    local f = io.open(filename, "wb")
    if not f then return false end

    for i = 0, RAM_SIZE - 1 do
        local v = space:read_u8(RAM_START + i)
        f:write(string.char(v))
    end
    f:close()

    if VERBOSE then
        print(string.format("[oracle] MC68010 RAM: %d bytes -> %s", RAM_SIZE, filename))
    end
    return true
end

local function dump_gsp_vram()
    -- TMS34010 GSP VRAM: maincpu space at 0x02000000-0x020fffff (1MB)
    local cpu = manager.machine.devices[":mainpcb:maincpu"]
    if not cpu then
        if VERBOSE then print("[oracle] maincpu not found") end
        return false
    end
    local space = cpu.spaces["program"]
    if not space then
        if VERBOSE then print("[oracle] program space not found") end
        return false
    end

    local VRAM_START = 0x02000000
    local VRAM_SIZE = 0x100000
    local filename = OUTPUT_DIR .. "/gsp_vram.bin"
    local f = io.open(filename, "wb")
    if not f then return false end

    for addr = VRAM_START, VRAM_START + VRAM_SIZE - 1 do
        local v = space:read_u8(addr)
        f:write(string.char(v))
    end
    f:close()

    if VERBOSE then
        print(string.format("[oracle] GSP VRAM: %d bytes -> %s", VRAM_SIZE, filename))
    end
    return true
end

local function dump_gsp_palette()
    -- GSP palette RAM is TWO separate memory regions:
    --   0xF5000000 = gsp_paletteram_lo (1024 entries × 2 bytes LE = 2048 bytes)
    --   0xF5800000 = gsp_paletteram_hi (1024 entries × 2 bytes LE = 2048 bytes)
    -- Each palette entry is selected by (palette_bank * 0x100 + index).
    -- Format per entry: lo[offset] = R<<8 | G (LE uint16), hi[offset] = B<<8 | 0 (LE uint16)
    --
    -- MAME's gsp_palette_change():
    --   int red   = (lo[offset] >> 8) & 0xff;   -- upper byte of lo
    --   int green = lo[offset] & 0xff;            -- lower byte of lo
    --   int blue  = hi[offset] & 0xff;           -- lower byte of hi (upper byte ignored)
    --
    -- Total: 2048 + 2048 = 4096 bytes
    local cpu = manager.machine.devices[":mainpcb:maincpu"]
    if not cpu then return false end
    local space = cpu.spaces["program"]
    if not space then return false end

    local PAL_LO_START = 0xF5000000
    local PAL_HI_START = 0xF5800000
    local PAL_REGION_SIZE = 2048
    local filename = OUTPUT_DIR .. "/gsp_palette.bin"
    local f = io.open(filename, "wb")
    if not f then return false end

    -- Write lo bank (2048 bytes)
    for i = 0, PAL_REGION_SIZE - 1 do
        f:write(string.char(space:read_u8(PAL_LO_START + i)))
    end
    -- Write hi bank (2048 bytes)
    for i = 0, PAL_REGION_SIZE - 1 do
        f:write(string.char(space:read_u8(PAL_HI_START + i)))
    end
    f:close()

    if VERBOSE then
        print(string.format("[oracle] GSP palette: %d bytes -> %s", PAL_REGION_SIZE * 2, filename))
    end
    return true
end

local function dump_adsp_memory()
    -- ADSP-2100 data memory window: 0x808000-0x80bfff (16KB)
    -- ADSP-2100 program memory: 0x800000-0x807fff (32KB)
    local cpu = manager.machine.devices[":mainpcb:maincpu"]
    if not cpu then return false end
    local space = cpu.spaces["program"]
    if not space then return false end

    local regions = {
        {name = "adsp_prog", start = 0x800000, size = 0x08000},
        {name = "adsp_data", start = 0x808000, size = 0x04000},
    }
    for _, r in ipairs(regions) do
        local filename = OUTPUT_DIR .. "/" .. r.name .. ".bin"
        local f = io.open(filename, "wb")
        if f then
            for i = 0, r.size - 1 do
                local v = space:read_u8(r.start + i)
                f:write(string.char(v))
            end
            f:close()
            if VERBOSE then
                print(string.format("[oracle] %s: %d bytes -> %s", r.name, r.size, filename))
            end
        end
    end
    return true
end

local function dump_jsa_memory()
    -- JSA II 6502 memory region — accessed via JSA cpu program space
    -- The JSA CPU device tag is :mainpcb:jsa:cpu
    local jsa = manager.machine.devices[":mainpcb:jsa:cpu"]
    if not jsa then
        if VERBOSE then print("[oracle] JSA CPU not found") end
        return false
    end
    local space = jsa.spaces["program"]
    if not space then
        if VERBOSE then print("[oracle] JSA program space not found") end
        return false
    end

    -- JSA 6502 ROM: 0x10000-0x1ffff (64KB)
    local JSA_START = 0x10000
    local JSA_SIZE = 0x10000
    local filename = OUTPUT_DIR .. "/jsa_rom.bin"
    local f = io.open(filename, "wb")
    if not f then return false end

    for i = 0, JSA_SIZE - 1 do
        local v = space:read_u8(JSA_START + i)
        f:write(string.char(v))
    end
    f:close()

    if VERBOSE then
        print(string.format("[oracle] JSA ROM: %d bytes -> %s", JSA_SIZE, filename))
    end
    return true
end

local function capture_screenshot(frame)
    -- Try to capture a screenshot via the screen device
    -- First try :mainpcb:screen
    local scr = manager.machine.screens[":mainpcb:screen"]
    if scr then
        local sc_path = string.format("%s/frame_%03d_screen.png", OUTPUT_DIR, frame)
        scr:snapshot(sc_path)
        print("[oracle] screenshot -> " .. sc_path)
        return
    end
    -- Fallback: enumerate all screens
    for tag, screen in pairs(manager.machine.screens) do
        print(string.format("[oracle] found screen: %s", tag))
    end
end

mkdir(OUTPUT_DIR)

-- Frame counter
local frame = 0

-- Use emu.register_frame_done (MAME 0.289 compatible)
emu.register_frame_done(function(event)
    frame = frame + 1

    -- Check if this frame is in our target list
    local is_target = false
    for _, f in ipairs(TARGET_FRAMES) do
        if frame == f then
            is_target = true
            break
        end
    end

    if is_target or VERBOSE then
        print(string.format("[oracle] frame=%d", frame))
    end

    if is_target then
        local prefix = string.format("frame_%03d", frame)
        local ok = true

        -- Dump GSP VRAM
        ok = dump_gsp_vram() and ok
        os.rename(OUTPUT_DIR .. "/gsp_vram.bin",
                  OUTPUT_DIR .. "/" .. prefix .. "_gsp_vram.bin")

        -- Dump GSP palette
        ok = dump_gsp_palette() and ok
        os.rename(OUTPUT_DIR .. "/gsp_palette.bin",
                  OUTPUT_DIR .. "/" .. prefix .. "_gsp_palette.bin")

        -- Dump main CPU RAM
        ok = dump_maincpu_ram() and ok
        os.rename(OUTPUT_DIR .. "/maincpu_ram.bin",
                  OUTPUT_DIR .. "/" .. prefix .. "_maincpu_ram.bin")

        -- Dump ADSP memory
        dump_adsp_memory()
        os.rename(OUTPUT_DIR .. "/adsp_prog.bin",
                  OUTPUT_DIR .. "/" .. prefix .. "_adsp_prog.bin")
        os.rename(OUTPUT_DIR .. "/adsp_data.bin",
                  OUTPUT_DIR .. "/" .. prefix .. "_adsp_data.bin")

        -- Dump JSA ROM
        dump_jsa_memory()
        os.rename(OUTPUT_DIR .. "/jsa_rom.bin",
                  OUTPUT_DIR .. "/" .. prefix .. "_jsa_rom.bin")

        -- Capture screenshot
        capture_screenshot(frame)

        print(string.format("[oracle] frame=%d captured (ok=%s)", frame, tostring(ok)))

        -- Exit MAME after last target frame
        if frame >= TARGET_FRAMES[#TARGET_FRAMES] then
            print("[oracle] all frames captured — exiting MAME")
            manager.machine:exit()
        end
    end
end)

print("[oracle] MAME oracle harness loaded")
print("[oracle] target frames: " .. table.concat(TARGET_FRAMES, ","))
print("[oracle] output dir: " .. OUTPUT_DIR)
print(string.format("[oracle] will capture %d frames", #TARGET_FRAMES))
