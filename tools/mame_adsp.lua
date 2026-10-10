-- mame_adsp.lua — dump the ADSP-2100 board as the 68010 sees it.
--
-- The 68010's main loop (ROM 0x1032-0x10a6) polls three words in ADSP program
-- RAM -- $8036, $8046, $8056 -- and while they are clear it only re-syncs to
-- vblank and never emits a display list.  Those words are written by the ADSP
-- CPU, so this script records the whole window over time to show when and how
-- they change, plus the data/buffer windows the 68010 talks through.
--
-- Env:
--   STUNRUN_ADSP_OUT        output directory (required)
--   STUNRUN_ADSP_FRAMES     comma-separated frames, default 150,200,300,600
--
-- Usage:
--   mame stunrun -rompath roms -autoboot_script tools/mame_adsp.lua \
--       -video none -sound none -nosleep

local dir = os.getenv("STUNRUN_ADSP_OUT")
if not dir then
  print("mame_adsp: STUNRUN_ADSP_OUT not set")
  emu.exit()
  return
end

local want = {}
for f in string.gmatch(os.getenv("STUNRUN_ADSP_FRAMES") or "150,200,300,600",
                       "[^,]+") do
  want[tonumber(f)] = true
end
local last = 0
for f in pairs(want) do if f > last then last = f end end

-- space:read_u8(addr, len) does not return a buffer in MAME 0.289's Lua
-- binding (it yields a 1-byte string), so dump one byte at a time as
-- tools/mame_oracle2.lua does.
local function dump(name, space, base, size, mask)
  mask = mask or 0xffffffff
  local fh = io.open(dir .. "/" .. name, "wb")
  if fh == nil then return end
  for i = 0, size - 1 do
    fh:write(string.char(space:read_u8((base + i) & mask)))
  end
  fh:close()
end

local seen = 0
emu.register_frame_done(function()
  seen = seen + 1
  if not want[seen] then
    if seen < last then return end
    manager.machine:exit()
    return
  end

  local m = manager.machine
  local mainsp = m.devices[":mainpcb:maincpu"].spaces["program"]
  local tag = string.format("frame_%03d", seen)
  dump(tag .. "_adsp_pgm.bin",  mainsp, 0x800000, 0x8000, 0xffffff)
  dump(tag .. "_adsp_data.bin", mainsp, 0x808000, 0x4000, 0xffffff)
  dump(tag .. "_adsp_buf.bin",  mainsp, 0x810000, 0x4000, 0xffffff)
  dump(tag .. "_adsp_ctrl.bin", mainsp, 0x818000, 0x0080, 0xffffff)

  -- The three words the main loop polls, plus the boot handshake words.
  local probe = {}
  for _, a in ipairs({0x801e, 0x8036, 0x8046, 0x8056, 0x8090, 0x8092}) do
    probe[#probe + 1] = string.format("$%04x = 0x%04x", a, mainsp:read_u16(a))
  end
  local pf = io.open(dir .. "/" .. string.format("frame_%03d_adsp_probe.txt", seen), "w")
  pf:write(table.concat(probe, "\n") .. "\n")

  local adsp = m.devices[":mainpcb:adsp"]
  local ar = {}
  for _, sym in ipairs({"PC", "SP", "ST", "AX0", "AY0", "LC0", "LC1", "LC2",
                        "PX", "PY", "PX1", "TX0", "TY0"}) do
    -- device_state_entry needs :value() before it formats as a number
    local ok, v = pcall(function() return adsp.state[sym] end)
    ar[#ar + 1] = string.format("adsp %s = %s", sym,
      (ok and type(v) == "number") and string.format("0x%04X", v) or "-")
  end
  local rf = io.open(dir .. "/" .. string.format("frame_%03d_adsp_regs.txt", seen), "w")
  rf:write(table.concat(ar, "\n") .. "\n")

  print(string.format("mame_adsp: frame %d  %s", seen,
                      table.concat(probe, "  ")))
  if seen >= last then m:exit() end
end)