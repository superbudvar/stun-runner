-- MAME oracle for STUN Runner: dump the regions our native shell captures, so
-- the two implementations can be diffed at the same frame.
--
-- Usage (from the repo root):
--   ./mame-ref/mame stunrun -rompath stun-runner/roms \
--        -autoboot_script stun-runner/tools/mame_oracle2.lua \
--        -video none -sound none -seconds_to_run 300 -nosleep -skip_gameinfo
--
-- Env: STUNRUN_CHECKPOINT_FRAME (default 600), STUNRUN_CHECKPOINT_OUTPUT_DIR
-- (default /tmp/stunrunner_oracle).
--
-- Region addresses come from the multisync board maps in
-- mame-ref/src/mame/atari/harddriv.cpp: multisync_68k_map at :599-616 and
-- multisync_gsp_map at :618-629.
--
-- NOTE: device state symbols are UPPERCASE ("PC", not "pc"), device spaces are
-- reached through device.spaces["program"], and a Lua method reference needs
-- its parentheses -- all three cost a debugging round each.

local out_dir = os.getenv("STUNRUN_CHECKPOINT_OUTPUT_DIR") or "/tmp/stunrunner_oracle"
local frame = tonumber(os.getenv("STUNRUN_CHECKPOINT_FRAME") or "600")
local frames = { 1, 369, 411, 412, 447, frame }

os.execute("mkdir -p '" .. out_dir .. "'")

local devs = manager.machine.devices
local maincpu = devs[":mainpcb:maincpu"]
local gsp = devs[":mainpcb:gsp"]
local mainspace = maincpu.spaces["program"]
local gspspace = gsp.spaces["program"]

-- The 68010 has a 24-bit bus; the GSP has a full 32-bit one.  Masking both to
-- 24 bits silently turned every GSP address above 0xffffff into address 0,
-- which reads back as unmap_value_high (0xff) and made the whole 4 MiB VRAM
-- dump look like one solid run of 0xff.
local function write_binary(name, space, base, size, mask)
    mask = mask or 0xffffffff
    local fh = io.open(out_dir .. "/" .. name, "wb")
    if fh == nil then return false end
    for i = 0, size - 1 do
        fh:write(string.char(space:read_u8((base + i) & mask)))
    end
    fh:close()
    return true
end

-- State symbols are UPPERCASE, and not every register exists on every device
-- (the 68010 has no MSP/VBR until the stack pointer is set up), so a missing
-- symbol must print as "-" rather than abort the whole dump.
local function reg(device, symbol)
    local ok, value = pcall(function() return device.state[symbol].value end)
    if not ok then return "-" end
    return string.format("%08x", value)
end

local function dump(tag)
    -- 68010 scratch RAM: multisync_68k_map has map(0xff8000, 0xffffff).ram().
    -- multisync_68k_map: map(0xff8000, 0xffffff).ram() is 0x8000 bytes.
    -- Dumping more wraps past 0xffffff and reads ROM, not RAM.
    write_binary("main_ram.bin", mainspace, 0xff8000, 0x8000, 0xffffff)

    -- GSP-side memory: 2bpp area, both palette halves, and the 512 KiB VRAM
    -- share mirrored down from 0xff800000.
    write_binary("gsp_vram_2bpp.bin", gspspace, 0x02000000, 0x100000)
    write_binary("gsp_pal_lo.bin", gspspace, 0xf5000000, 0x800)
    write_binary("gsp_pal_hi.bin", gspspace, 0xf5800000, 0x800)
    write_binary("gsp_vram.bin", gspspace, 0xff800000, 0x400000)

    local fh = io.open(out_dir .. "/frame_" .. tag .. "_regs.txt", "w")
    fh:write("maincpu\n")
    for _, n in ipairs({ "PC", "SR", "USP", "SSP", "MSP", "VBR" }) do
        fh:write(string.format("%-4s %s\n", n, reg(maincpu, n)))
    end
    for i = 0, 7 do
        fh:write(string.format("D%-3d %s\n", i, reg(maincpu, "D" .. i)))
        fh:write(string.format("A%-3d %s\n", i, reg(maincpu, "A" .. i)))
    end
    fh:write("gsp\n")
    fh:write(string.format("PC   %s\n", reg(gsp, "PC")))
    fh:write(string.format("ST   %s\n", reg(gsp, "ST")))
    fh:write(string.format("SP   %s\n", reg(gsp, "SP")))
    for i = 0, 15 do
        fh:write(string.format("A%-3d %s\n", i, reg(gsp, "A" .. i)))
    end
    for i = 0, 14 do
        fh:write(string.format("B%-3d %s\n", i, reg(gsp, "B" .. i)))
    end
    fh:close()
end

local done = {}
local count = 0

emu.register_frame_done(function()
    count = count + 1
    for _, f in ipairs(frames) do
        local tag = tostring(f)
        if count == f and not done[tag] then
            done[tag] = true
            dump(tag)
        end
    end
    if count >= frame then
        manager.machine:exit()
    end
end)