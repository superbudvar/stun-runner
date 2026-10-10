#!/usr/bin/env python3
"""Render candidate STUN Runner frames straight out of a GSP VRAM dump.

The multisync scanline formula is MAME's, from harddriv_v.cpp:

    vram_base = &m_gsp_vram[(rowaddr << 10) & m_vram_mask]      # 2 KiB row
    coloridx  = coladdr++ & 0x7ff
    color     = (coloridx & 1) ? vram_base[coloridx >> 1] >> 8
                                : vram_base[coloridx >> 1]
    dest[x]   = palette_bank * 256 + color

We do not have the GSP's DPYADR (MAME keeps the register file host-side, not in
VRAM, so Lua cannot read it -- see BUGS.md MD-04), so this sweeps candidate
row bases and writes a PNG for each.  A candidate that decodes to a coherent
picture rather than noise identifies the real display geometry.
"""
import struct
import sys

W, H = 512, 228
STRIDE = 2048          # multisync row = 1024 uint16 (harddriv_v.cpp:439)
VISIBLE = 1024         # 512 px * 2 bytes/px


def load_palette(d):
    lo = open(d + "/gsp_pal_lo.bin", "rb").read()
    hi = open(d + "/gsp_pal_hi.bin", "rb").read()
    n = min(len(lo), len(hi)) // 2
    return [(lo[2 * i] << 8 | lo[2 * i + 1], hi[2 * i] << 8 | hi[2 * i + 1])
            for i in range(n)]


def render(vram, pal, bank, row_base):
    out = bytearray()
    for y in range(H):
        # rowaddr is a WORD index: &m_gsp_vram[(rowaddr << 10) & m_vram_mask]
        off = row_base * 2 + y * STRIDE
        row = bytearray(b"\x00\x00\x00" * W)
        ci = 0
        for x in range(W):
            w = struct.unpack_from("<H", vram, off + ci)[0] if off + ci + 2 <= len(vram) else 0
            ci = (ci + 1) & 0x7FF
            col = (w >> 8) if (ci & 1) else (w & 0xFF)
            pen = bank * 256 + col
            r, g = (0, 0)
            b = 0
            if pen < len(pal):
                r = (pal[pen][0] >> 8) & 0xFF
                g = pal[pen][0] & 0xFF
                b = pal[pen][1] & 0xFF
            row[3 * x] = r
            row[3 * x + 1] = g
            row[3 * x + 2] = b
        out += row
    return bytes(out)


def write_png(path, rgb, w, h):
    import zlib
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    dump = sys.argv[1] if len(sys.argv) > 1 else "/tmp/or600c"
    bases = [int(b, 16) for b in sys.argv[2].split(",")] if len(sys.argv) > 2 else [
        0x0D0000, 0x0E0000, 0x1A4000, 0x1C0000, 0x1D0000, 0x340000]
    vram = open(dump + "/gsp_vram.bin", "rb").read()
    pal = load_palette(dump)
    for b in bases:
        for bank in range(8):
            rgb = render(vram, pal, bank, b)
            nz = sum(1 for i in range(0, len(rgb), 3) if rgb[i] or rgb[i + 1] or rgb[i + 2])
            pct = nz / (W * H) * 100
            print(f"row_base={b:#08x} bank={bank} nonblack={pct:6.2f}%")
            write_png(f"/tmp/frame_{b:08x}_{bank}.png", rgb, W, H)


main()