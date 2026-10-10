#!/usr/bin/env python3
"""
mame_oracle.py — MAME oracle harness for STUN Runner

Spawns MAME with a Lua script that:
  1. Runs the STUN Runner driver for N frames
  2. Dumps GSP VRAM (1 MB) to vram.bin
  3. Dumps GSP palette-low (512 bytes) to palette_low.bin
  4. Dumps GSP palette-high (256 bytes) to palette_high.bin
  5. Exits MAME cleanly

Usage:
  python3 mame_oracle.py --frames 600 --mame /path/to/mame --roms /path/to/roms --outdir /path/to/output
"""

import argparse
import os
import subprocess
import sys
import tempfile

from process import run_mame

LUA_SCRIPT = """
-- mame_oracle.lua: drive MAME as a frame-accurate oracle for STUN Runner
local FRAMES = 600
local OUTDIR = "/tmp/stunrun_oracle"

-- Allow overrides from environment (passed by Python harness)
local env_frames = os.getenv("MAME_ORACLE_FRAMES")
local env_outdir = os.getenv("MAME_ORACLE_OUTDIR")
if env_frames then FRAMES = tonumber(env_frames) end
if env_outdir then OUTDIR = env_outdir end

local mach = manager:machine()

-- List all devices for debugging
print("oracle: available devices:")
for k,v in pairs(mach.devices) do
    print("  " .. k)
end

-- STUN Runner has: maincpu (68010), gsp (TMS34010), msp, jsa (6502), etc.
-- The GSP device manages VRAM and palette.
-- We access GSP memory via the device's memory window.
local gsp_dev = mach.devices[":gsp"]
if gsp_dev == nil then
    print("oracle: ERROR: :gsp device not found")
    os.exit(1)
end

local cpu_dev = mach.devices[":maincpu"]
if cpu_dev == nil then
    print("oracle: ERROR: :maincpu device not found")
    os.exit(1)
end

-- Dump a memory region by readblock(offset, size) on a space
local function dump_space(space, offset, size, path)
    local f = io.open(path, "wb")
    if f == nil then
        print("oracle: cannot open " .. path .. " for write")
        return false
    end
    -- Use offset 0 within the space; size is in bytes
    local ok, data = pcall(function()
        return space:readblock(offset, size)
    end)
    if not ok then
        print("oracle: readblock failed at offset=" .. string.format("0x%08x", offset) .. " size=" .. size .. ": " .. tostring(data))
        f:close()
        return false
    end
    if data == nil or #data == 0 then
        print("oracle: readblock returned empty at offset=" .. string.format("0x%08x", offset) .. " size=" .. size)
        f:close()
        return false
    end
    f:write(data)
    f:close()
    print("oracle: dumped " .. #data .. " bytes to " .. path)
    return true
end

-- Step 1: Reset and run FRAMES
print("oracle: resetting maincpu...")
local gsp_space = gsp_dev.spaces["program"]
local cpu_space = cpu_dev.spaces["program"]

if gsp_space == nil then
    print("oracle: ERROR: gsp program space not found")
    os.exit(1)
end

-- The TMS34010 GSP memory map (from harddriv.cpp):
--   0x000000-0x1FFFFF : VRAM (2 MB)
--   0x200000-0x203FFF : GSP registers
--   0xF5000000+        : external palette RAM (host CPU access)
--
-- For STUN Runner specifically:
--   VRAM at 0x000000-0x0FFFFF (1 MB visible in scanline mode)
--   Palette at 0xF5000000-0xF50002FF (512 bytes = 256 colors × 2 bytes)
--
-- The GSP device's program space maps VRAM at 0x000000-0x1FFFFF
-- and the palette is accessed by the host CPU via a separate region.

-- Run FRAMES
print("oracle: running " .. FRAMES .. " frames...")
for i = 1, FRAMES do
    mach:step()
    if i % 100 == 0 then
        print("oracle: frame " .. i)
    end
end
print("oracle: reached frame " .. FRAMES)

-- Step 2: Dump GSP VRAM (1 MB = 0x100000 bytes starting at offset 0)
local VRAM_SIZE = 0x100000  -- 1 MB
local ok = true
ok = dump_space(gsp_space, 0x00000, VRAM_SIZE, OUTDIR .. "/vram.bin") and ok

-- Step 3: Dump palette
-- The palette is at 0xF5000000 in the host address space.
-- In MAME, we can access it via the system's address space.
-- Try to find the palette region via the system's memory map.
local mem = mach.mem
-- List available address spaces
print("oracle: memory spaces available:")
for name, sp in pairs(mem) do
    print("  " .. name)
end

-- For the palette, try to read from the host CPU address space
-- palette_low:  256 × 16-bit = 512 bytes at offset 0
-- palette_high: 256 × 8-bit  = 256 bytes at offset 512
-- The palette is mapped at 0xF5000000 (host address, accessible via maincpu space)
local PALETTE_LOW_SIZE  = 512   -- 256 colors × 2 bytes (R+G as 16-bit)
local PALETTE_HIGH_SIZE = 256   -- 256 colors × 1 byte (B)
local PALETTE_ADDR = 0xF5000000

-- Try reading from maincpu program space at the palette address
local cpu_space_ref = cpu_dev.spaces["program"]
if cpu_space_ref then
    local pal_low_ok, pal_low_data = pcall(function()
        return cpu_space_ref:readblock(PALETTE_ADDR, PALETTE_LOW_SIZE)
    end)
    if pal_low_ok and pal_low_data and #pal_low_data == PALETTE_LOW_SIZE then
        local f = io.open(OUTDIR .. "/palette_low.bin", "wb")
        f:write(pal_low_data)
        f:close()
        print("oracle: dumped palette_low " .. #pal_low_data .. " bytes")
    else
        print("oracle: WARNING: could not read palette_low: " .. tostring(pal_low_data))
    end

    local pal_high_ok, pal_high_data = pcall(function()
        return cpu_space_ref:readblock(PALETTE_ADDR + PALETTE_LOW_SIZE, PALETTE_HIGH_SIZE)
    end)
    if pal_high_ok and pal_high_data and #pal_high_data == PALETTE_HIGH_SIZE then
        local f = io.open(OUTDIR .. "/palette_high.bin", "wb")
        f:write(pal_high_data)
        f:close()
        print("oracle: dumped palette_high " .. #pal_high_data .. " bytes")
    else
        print("oracle: WARNING: could not read palette_high: " .. tostring(pal_high_data))
    end
else
    print("oracle: WARNING: maincpu program space not available for palette read")
end

print("oracle: done, exiting")
os.exit(0)
"""

def build_lua_script(frames: int, outdir: str) -> str:
    return LUA_SCRIPT.format(frames=frames, outdir=outdir)

def run_oracle(mame_path: str, roms_dir: str, outdir: str, frames: int = 600,
               timeout: int = 300) -> bool:
    """
    Run MAME as oracle for STUN Runner, capturing GSP state at frame N.

    Returns True if all dumps were created successfully.
    """
    os.makedirs(outdir, exist_ok=True)

    lua_script = build_lua_script(frames, outdir)

    # Write Lua script to a temp file (MAME -luac expects compiled or plain .lua)
    with tempfile.NamedTemporaryFile(mode='w', suffix='.lua',
                                      delete=False) as f:
        f.write(lua_script)
        lua_path = f.name

    try:
        # MAME command:
        #   -nodisp     : no display (headless)
        #   -monitor    : dummy monitor (not needed for headless)
        #   -throttle   : throttle to real time (disable for oracle)
        #   -script     : run lua script file
        cmd = [
            mame_path,
            "stunrun",
            "-config", "none",           # no ini
            "-once",                      # stop after attract/coin
            "-waitnone",                  # no wait for input
            "-rompath", roms_dir,
            "-throttle",                  # throttle (keep frames deterministic)
            "-script", lua_path,
            # MAME passes args to lua via this env-var approach, but we bake them
            # into the script above instead.
        ]

        # Also pass frame count and outdir via environment
        env = os.environ.copy()
        env["MAME_ORACLE_FRAMES"] = str(frames)
        env["MAME_ORACLE_OUTDIR"] = outdir

        print(f"oracle: spawning MAME with {frames} frames...")
        print(f"oracle: rompath={roms_dir}")
        print(f"oracle: outdir={outdir}")
        print(f"oracle: lua script:\n{lua_script[:200]}...")

        result = run_mame(
            cmd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )

        print(f"oracle: MAME exit code = {result.returncode}")
        if result.stdout:
            print("stdout:", result.stdout[:2000])
        if result.stderr:
            print("stderr:", result.stderr[:2000])

    except subprocess.TimeoutExpired:
        print(f"oracle: TIMEOUT after {timeout}s", file=sys.stderr)
        return False
    except FileNotFoundError:
        print(f"oracle: MAME not found at {mame_path}", file=sys.stderr)
        return False
    finally:
        os.unlink(lua_path)

    # Verify output files exist and have non-zero size
    vram_path     = os.path.join(outdir, "vram.bin")
    pal_low_path  = os.path.join(outdir, "palette_low.bin")
    pal_high_path = os.path.join(outdir, "palette_high.bin")

    for path in [vram_path, pal_low_path, pal_high_path]:
        if not os.path.exists(path):
            print(f"oracle: ERROR: {path} not created", file=sys.stderr)
            return False
        size = os.path.getsize(path)
        if size == 0:
            print(f"oracle: ERROR: {path} is empty", file=sys.stderr)
            return False
        print(f"oracle: {path} = {size} bytes OK")

    print("oracle: all dumps verified")
    return True

def main():
    parser = argparse.ArgumentParser(description="MAME oracle for STUN Runner")
    parser.add_argument("--mame", default="./mame",
                        help="Path to MAME binary (default: ./mame)")
    parser.add_argument("--roms", default="./roms",
                        help="MAME ROM search path containing stunrun/ (default: ./roms)")
    parser.add_argument("--outdir", default="./oracle_dumps",
                        help="Output directory for dumps (default: ./oracle_dumps)")
    parser.add_argument("--frames", type=int, default=600,
                        help="Number of frames to run (default: 600)")
    parser.add_argument("--timeout", type=int, default=300,
                        help="Timeout in seconds (default: 300)")

    args = parser.parse_args()

    # Default mame path relative to this script's location
    if not os.path.isabs(args.mame) and args.mame == "./mame":
        script_dir = os.path.dirname(os.path.abspath(__file__))
        default_mame = os.path.join(os.path.dirname(script_dir), "..", "mame-ref", "mame")
        if os.path.exists(default_mame):
            args.mame = default_mame
            print(f"oracle: auto-detected MAME at {args.mame}")

    success = run_oracle(
        mame_path=args.mame,
        roms_dir=args.roms,
        outdir=args.outdir,
        frames=args.frames,
        timeout=args.timeout,
    )

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
