-- mame_frame.lua — capture one frame of stunrun two ways at the same instant:
--   * the raw GSP VRAM / palette / main RAM, for offline analysis
--   * the rendered frame MAME itself produced, as ground truth
--
-- Needs a real video driver for the rendered half (DISPLAY=:0 works); with
-- -video none there is no render target and snapshot() produces nothing.
--
-- Env:
--   STUNRUN_FRAME_DUMP_DIR   output directory (required)
--   STUNRUN_FRAME_AT         frame number, default 600
--
-- Usage:
--   DISPLAY=:0 mame stunrun -rompath roms -autoboot_script tools/mame_frame.lua \
--       -snapshot_directory <dir> -video opengl -sound none -nosleep

local frame = tonumber(os.getenv("STUNRUN_FRAME_AT") or "600")
local dir = os.getenv("STUNRUN_FRAME_DUMP_DIR")
if not dir then
  print("mame_frame: STUNRUN_FRAME_DUMP_DIR not set")
  emu.exit()
  return
end

-- space:read_u8(addr, len) does not return a buffer in MAME 0.289's Lua
-- binding; dump one byte at a time, as tools/mame_oracle2.lua does.
local function dump(name, space, base, size, mask)
  mask = mask or 0xffffffff
  local fh = io.open(dir .. "/" .. name, "wb")
  if fh == nil then return end
  for i = 0, size - 1 do
    fh:write(string.char(space:read_u8((base + i) & mask)))
  end
  fh:close()
end

local function put(name, text)
  local fh = io.open(dir .. "/" .. name, "w")
  if fh then fh:write(text) fh:close() end
end

local seen = 0
emu.register_frame_done(function()
  seen = seen + 1
  if seen < frame then return end

  local m = manager.machine
  local maincpu = m.devices[":mainpcb:maincpu"]
  local gsp = m.devices[":mainpcb:gsp"]
  local mainsp = maincpu.spaces["program"]
  local gspsp = gsp.spaces["program"]

  -- GSP VRAM is the shared region at 0xff800000, mirrored every 0x400000.
  -- maincpu is a 24-bit space, so it must be read from the GSP's own space.
  dump("gsp_vram.bin",      gspsp, 0xff800000, 0x400000)
  dump("gsp_vram_2bpp.bin", gspsp, 0x02000000, 0x100000)
  dump("gsp_pal_lo.bin",    gspsp, 0xf5000000, 0x800)
  dump("gsp_pal_hi.bin",    gspsp, 0xf5800000, 0x800)
  dump("gsp_ctrl_lo.bin",   gspsp, 0xf4000000, 0x100)
  dump("gsp_ctrl_hi.bin",   gspsp, 0xf4800000, 0x100)
  -- multisync_68k_map: map(0xff8000, 0xffffff).ram() is 0x8000 bytes.
  dump("main_ram.bin",      mainsp, 0xff8000,   0x8000, 0xffffff)

  local regs = {}
  for _, sym in ipairs({"PC", "SR", "SP", "SSP", "USP", "VBR", "NUSP", "D0", "D1",
                        "A0", "A1", "A2", "A7"}) do
    -- .value is the numeric field of the device_state_entry.  Without it,
    -- string.format gets a userdata and throws
    -- "bad argument #2 to 'format' (number expected, got sol.device_state_entry*)".
    local ok, v = pcall(function() return maincpu.state[sym].value end)
    regs[#regs + 1] = string.format("maincpu %s = %s", sym,
                                   (ok and type(v) == "number") and string.format("0x%08X", v)
                                   or "-")
  end
  for _, sym in ipairs({"PC", "SP", "ST", "A0", "A1", "B0", "B14"}) do
    local ok, v = pcall(function() return gsp.state[sym] end)
    regs[#regs + 1] = string.format("gsp %s = %s", sym,
                                   (ok and v ~= nil) and string.format("0x%08X", v)
                                   or "-")
  end
  put("frame_regs.txt", table.concat(regs, "\n") .. "\n")

  -- Rendered ground truth.
  local ok, err = pcall(function() m:snapshot() end)
  print(string.format("mame_frame: frame %d dumped to %s (snapshot ok=%s %s)",
                      seen, dir, tostring(ok), tostring(err)))
  m:exit()
end)