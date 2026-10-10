-- STUN Runner oracle: dump exactly the regions the native port reproduces.
-- Addresses verified against MAME 0.289 + the STUNRUN-DPY/BASE probe:
--   GSP program space (== gsp_vram share) base 0xff800000, 0x80000 bytes (512 KiB)
--   palette lo 0xf5000000 / hi 0xf5800000, 0x800 bytes each (1024 entries)
--   68010 work RAM 0xff8000, 0x8000 bytes (32 KiB)
-- NOTE: the byte tables below are 1-based on purpose. Filling them from index 0
-- makes `#t` report one less than the device size, which silently truncated
-- every dump by a byte and shifted it by one.
local TARGET = tonumber(os.getenv("STUNRUN_CHECKPOINT_FRAME") or "600")
local DIR    = os.getenv("STUNRUN_CHECKPOINT_OUTPUT_DIR") or "/tmp/oracle"
local frame  = 0
local function hexs(v) return string.format("%06x", v) end
local function wr(name, bytes)
  -- f:write() here does NOT reliably accept a long numeric vararg list: a
  -- chunked table.unpack write produced a 4271-byte gsp_pal_lo.bin for a
  -- 2048-byte device, which silently corrupted every palette reading taken
  -- from these dumps. Build the chunk as a string with string.char instead.
  local n = #bytes
  local f = assert(io.open(DIR .. "/" .. name, "wb"))
  local CH = 2048
  local i = 0
  while i < n do
    local j = math.min(i + CH - 1, n - 1)
    f:write(string.char(table.unpack(bytes, i + 1, j + 1)))
    i = j + 1
  end
  f:close()
  -- Verify: a short or long file means the write path is still wrong.
  local got = io.open(DIR .. "/" .. name, "rb"):seek("end")
  if got ~= n then
    error(string.format("%s: wrote %d of %d bytes", name, got, n))
  end
end
emu.register_frame_done(function()
  frame = frame + 1
  if frame < TARGET then return end
  local m = manager.machine
  local main = m.devices[":mainpcb:maincpu"]
  local gsp  = m.devices[":mainpcb:gsp"]
  local jsa  = m.devices[":mainpcb:jsa:cpu"]
  local adsp = m.devices[":mainpcb:adsp"] or m.devices[":adsp"]

  -- 68010 work RAM
  local ram = {}
  local ms = main.spaces["program"]
  for i = 0, 0x7fff do ram[i + 1] = ms:read_u8(0xff8000 + i) end
  wr("main_ram.bin", ram)

  -- GSP VRAM share via program space, 512 KiB
  local vr = {}
  local gs = gsp.spaces["program"]
  for i = 0, 0x7ffff do vr[i + 1] = gs:read_u8(0xff800000 + i) end
  wr("gsp_vram.bin", vr)

  -- palette
  local plo, phi = {}, {}
  for i = 0, 0x7ff do plo[i + 1] = gs:read_u8(0xf5000000 + i) end
  for i = 0, 0x7ff do phi[i + 1] = gs:read_u8(0xf5800000 + i) end
  wr("gsp_pal_lo.bin", plo); wr("gsp_pal_hi.bin", phi)

  -- 2bpp expander window
  local v2 = {}
  for i = 0, 0xfffff do v2[i + 1] = gs:read_u8(0x02000000 + i) end
  wr("gsp_vram_2bpp.bin", v2)

  local f = assert(io.open(DIR .. "/frame_" .. hexs(frame) .. "_regs.txt", "w"))
  f:write(string.format("frame %d\n", frame))
  for _, d in ipairs({main, gsp, jsa, adsp}) do
    if d then
      local st = d.state
      local parts = {}
      for _, sym in ipairs({"PC", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
                            "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "SR", "SP"}) do
        local e = st[sym]
        if e then parts[#parts + 1] = sym .. "=" .. string.format("%08x", e.value) end
      end
      f:write(d.name .. " tag=" .. d.tag .. " " .. table.concat(parts, " ") .. "\n")
    end
  end
  f:close()
  m:exit()
end)
