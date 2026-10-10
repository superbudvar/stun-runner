#!/usr/bin/env python3
"""
Convert MAME oracle dumps (gsp_vram.bin + gsp_palette.bin) to the harness
fixture format (vram.bin + palette_low.bin + palette_high.bin).

Harness format (from gsp_video.c):
  palette_low.bin  = 256 × 2 bytes LE uint16_t  (palette_low[i] = R<<8 | G_low)
  palette_high.bin = 256 × 1 byte                (B)
  vram.bin         = raw bytes (stunrun_gsp_video_set_vram_bytes reads LE uint16_t pairs)

MAME gsp_palette.bin: 4096 bytes total
  Bytes 0-2047:   gsp_paletteram_lo  (1024 entries × 2 bytes LE uint16_t)
  Bytes 2048-4095: gsp_paletteram_hi (1024 entries × 2 bytes LE uint16_t)

  Per entry (index i):
    lo[i] = LE uint16 at byte[i*2]   = R<<8 | G
    hi[i] = LE uint16 at byte[2048 + i*2] = B<<8 | 0
    → R = lo[i] >> 8,  G = lo[i] & 0xFF,  B = hi[i] & 0xFF

Harness reads: R = palette_low>>8, G = palette_low&0xFF, B = palette_high
So palette_low[i]  = lo[i]   (LE uint16 = R<<8 | G)
And palette_high[i] = hi[i] & 0xFF  (lower byte of hi entry)

MAME gsp_vram.bin: 1MB of raw bytes (MAME memory:lookup_region returns bytes in address order).
GSP is big-endian, so byte[n*2]=MSB, byte[n*2+1]=LSB.
Harness stunrun_gsp_video_set_vram_bytes reads LE uint16_t: byte[n*2]|byte[n*2+1]<<8.
→ MAME bytes as-is are correct (GSP big-endian == LE when read as uint16 pair).
"""

import struct, sys, os, shutil

def convert_frame(src_dir, frame_num, out_dir=None):
    if out_dir is None:
        out_dir = src_dir
    os.makedirs(out_dir, exist_ok=True)

    # mame_oracle.lua writes files as frame_NNN_gsp_palette.bin etc in src_dir directly
    palette_path = os.path.join(src_dir, f'frame_{frame_num:03d}_gsp_palette.bin')
    vram_path    = os.path.join(src_dir, f'frame_{frame_num:03d}_gsp_vram.bin')

    # Palette: 4096 bytes (2048 lo + 2048 hi, each LE uint16_t[1024])
    #   → palette_low (LE uint16_t × 256) + palette_high (byte × 256)
    if os.path.exists(palette_path):
        with open(palette_path, 'rb') as f:
            data = f.read()
        assert len(data) == 4096, f"palette: expected 4096, got {len(data)}"

        lo_bank = data[0:2048]    # gsp_paletteram_lo
        hi_bank = data[2048:4096] # gsp_paletteram_hi

        low_bytes  = bytearray(512)
        high_bytes = bytearray(256)
        for i in range(256):
            lo_val = lo_bank[i*2] | (lo_bank[i*2+1] << 8)
            hi_val = hi_bank[i*2] | (hi_bank[i*2+1] << 8)
            struct.pack_into('<H', low_bytes,  i * 2, lo_val)
            high_bytes[i] = hi_val & 0xFF

        with open(os.path.join(out_dir, 'palette_low.bin'),  'wb') as f: f.write(low_bytes)
        with open(os.path.join(out_dir, 'palette_high.bin'), 'wb') as f: f.write(high_bytes)

    # VRAM: MAME gsp_vram.bin is already raw bytes in address order (big-endian 16-bit)
    # Harness reads as LE uint16_t, so MAME big-endian bytes → LE pair is correct
    if os.path.exists(vram_path):
        shutil.copy(vram_path, os.path.join(out_dir, 'vram.bin'))

    return os.path.exists(palette_path) or os.path.exists(vram_path)

if __name__ == '__main__':
    src_dir = sys.argv[1] if len(sys.argv) > 1 else '/tmp/stunrunner_oracle'
    frames = [1, 369, 411, 412, 447, 600]
    if len(sys.argv) > 2:
        frames = [int(sys.argv[2])]

    for frame in frames:
        ok = convert_frame(src_dir, frame, out_dir=src_dir)
        if ok:
            print(f"frame {frame:03d}: OK")
        else:
            print(f"frame {frame:03d}: no oracle files found")
