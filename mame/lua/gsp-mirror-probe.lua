-- Measure the GSP VRAM decode: which addresses are mapped, and what offset
-- inside the share each one reaches.
--
-- The map declares (harddriv.cpp:628)
--
--     map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram")
--
-- and that window is 0xff800000 to 0xffbfffff inclusive, which is 0x400000
-- bytes -- 4 MiB.  Mirrored through mask bit 22, so bit 22 picks the copy and
-- the region itself occupies address bits 0-21.  That predicts
--
--     mapped iff (addr & 0x7f800000) == 0x7f800000   i.e. 0xff800000-0xffffffff
--     offset      =  addr & 0x003fffff
--
-- Both native/ VRAM paths instead assume a 512 KiB share and compute
-- `(addr - 0xff800000) & 0x7ffff`, which agrees only below 0xffa00000 and
-- folds every 2 MiB stride onto offset 0 above it.  492,544 writes in the
-- captured bus stream land above 0xffa00000, so this is worth measuring
-- rather than arguing about.  See docs/mc68010-boot-findings.md section 19.
--
-- Three measurements, no reliance on Lua share introspection (this MAME's
-- binding exposes no `space:share` and no `machine.memory.shares`):
--
--   1. share size    -- write a marker at base+off, read it back at base+off,
--                        over a doubling sequence of offsets.  The largest
--                        offset that round-trips bounds the share from below
--                        and the first that does not bounds it from above.
--   2. mapped set    -- eight probe addresses 2 MiB apart across the top
--                        256 MiB.  A write that changes nothing anywhere is an
--                        address MAME does not map.
--   3. offset(a)     -- write a unique marker at address a, then scan the
--                        mapped range for that marker.  A baseline read comes
--                        first, because an unmapped read returns the floating
--                        bus (0xffff here) rather than zero, so "reads zero"
--                        means nothing without it.
--
-- This writes program memory and leaves the machine in a state not worth
-- screenshotting.  It answers one question and exits.  Not a test harness.

local machine = manager.machine

local frame = tonumber(os.getenv("STUNRUN_MIRROR_FRAME") or "600")

local base = 0xff800000        -- declared window start
local step = 0x200000         -- 2 MiB probe stride
local count = 8               -- covers 0xff000000-0xffffffff
local share_bytes = 0

local function probe_addr(k)
    return 0xff000000 + k * step
end

local function fmt(v)
    return string.format("0x%04X", v & 0xffff)
end

local function read(space, a)
    local ok, v = pcall(function() return space:read_u16(a) end)
    if not ok then return nil end
    return v & 0xffff
end

local function write(space, a, v)
    return (pcall(function() space:write_u16(a, v) end))
end

-- 1. Bound the share size.
--
-- A plain round-trip at base+off cannot tell storage from a mirror: write a
-- marker at base+off, read it back at base+off, and a base+off that aliases
-- offset 0 passes exactly the same test as a real offset.  base+0x400000 is
-- the bit-22 mirror of offset 0, so it round-trips perfectly whether or not
-- offset 0x400000 exists.  So separate the two with a second address.  Park a
-- sentinel at offset 0, write a candidate marker at base+off, then read
-- offset 0: if it now holds the candidate, base+off was never a distinct
-- offset and the share ended here.
local function measure_share(space)
    local sentinel, cand = 0xd000, 0xe000
    local off = 0x10000
    while off <= 0x1000000 do
        write(space, base, sentinel)
        local landed = write(space, base + off, cand)
        if not landed or read(space, base + off) ~= cand then
            return off - 0x10000, off, "not_stored"
        end
        if read(space, base) == cand then
            return off, off, "aliases_offset_0"
        end
        off = off * 2
    end
    return off, nil, "no_alias_found"
end

local function main()
    local gsp = machine.devices[':mainpcb:gsp']
    local space = gsp.spaces['program']
    local pc = gsp.state['PC'].value

    -- share_bytes is the measured share size: the offset at which an address stops
    -- being distinct storage and starts aliasing offset 0.  The declared window
    -- 0xff800000-0xffbfffff is 0x400000 bytes, so 0x400000 is the predicted
    -- answer and the sentinel probe is what checks it.
    local share_bytes, first_bad, why = measure_share(space)

    print(string.format("M1_MIRROR base=%08X step=%X count=%d frame=%d pc=%08X",
          base, step, count, frame, pc))
    print(string.format("M1_MIRROR share_name=gsp_vram share_bytes=%d "
          .. "share_first_bad=%s share_why=%s",
          share_bytes,
          first_bad and string.format("%X", first_bad) or "none", why))

    -- 2. Baseline: what each probe address reads before we touch anything.
    for k = 0, count - 1 do
        local a = probe_addr(k)
        print(string.format("M1_MIRROR phase=baseline k=%d addr=%08X value=%s",
              k, a, tostring(read(space, a))))
    end

    -- 2/3. Marker per probe address, then a full scan for where it landed.
    for k = 0, count - 1 do
        local a = probe_addr(k)
        local m = 0xa000 + k
        local wrote = write(space, a, m)
        print(string.format("M1_MIRROR phase=write k=%d addr=%08X value=%s ok=%d",
              k, a, fmt(m), wrote and 1 or 0))
        -- Read back at the write address and at every other probe address,
        -- which is what names the alias sets cheaply.
        for j = 0, count - 1 do
            local b = probe_addr(j)
            print(string.format("M1_MIRROR phase=read wrote=%d k=%d addr=%08X value=%s",
                  k, j, b, tostring(read(space, b))))
        end
        -- Then find where in the share it actually landed.  Two-level scan: a
        -- coarse sweep first, then refine only the bucket holding the marker.
        -- Scanning all 256 MiB at 16-byte granularity is 8.4M reads per probe
        -- and far too slow in Lua; the coarse sweep does not depend on any
        -- predicted offset, so the answer is still measured, not assumed.
        local coarse = 0x10000
        local buckets = {}
        for off = 0xff800000, 0xffffffff, coarse do
            if read(space, off) == m then
                buckets[#buckets + 1] = off
            end
        end
        local found = {}
        for _, bucket in ipairs(buckets) do
            for off = bucket, bucket + coarse - 16, 16 do
                if read(space, off) == m then
                    found[#found + 1] = off
                end
            end
        end
        table.sort(found)
        print(string.format("M1_MIRROR phase=offset k=%d addr=%08X value=%s hits=%d first=%s",
              k, a, fmt(m), #found,
              #found > 0 and string.format("%08X", found[1]) or "none"))
        for i, off in ipairs(found) do
            print(string.format("M1_MIRROR phase=offset_at k=%d addr=%08X index=%d off=%08X",
                  k, a, i, off))
        end
    end

    machine:exit()
end

local seen = 0
emu.register_frame_done(function()
    seen = seen + 1
    if seen == frame then
        main()
    end
end)