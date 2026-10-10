#!/usr/bin/env python3
"""Rasterise a STUN Runner frame straight out of a GSP VRAM dump.

This is the acceptance harness for the display path: if this reproduces MAME's
own frame-600 output, then our understanding of where the picture lives and how
the TMS34010 addresses it is correct, and the only remaining work is executing
the GSP microcode that draws it.

Everything here is transcribed from MAME 0.289, not guessed:

  * `scanline_multisync()`            mame-ref/src/mame/atari/harddriv_v.cpp:436
  * `get_display_params()`            mame-ref/src/devices/cpu/tms34010/tms34010.cpp:1160
  * `gsp_palette_change()`             mame-ref/src/mame/atari/harddriv_v.cpp:326

Geometry constants come from the STUNRUN-DPY / BASE probe added to
scanline_multisync() (see docs/mc68010-boot-findings.md); the probe printed

    BASE off=61440 (0x00f000) byte=0x01e000 vram_words=262144 mask=0x003ffff

which pins the two facts that were previously wrong: the gsp_vram share is
512 KiB (not 4 MiB), and a row base is `rowaddr << 10` words masked to 0x3ffff.

Usage:
    gsp_render.py <dumpdir> <out.png> [--frame N]
"""

import argparse
import os
import struct
import sys
import zlib

# --- screen geometry (harddriv.cpp stunrun_board_device_state::device_add_mconfig) ---
H_TOTAL = 317 * 2      # 634
H_BLANK = 256 * 2      # 512 visible pixels
V_TOTAL = 262
V_VISIBLE = 228
VBLANK = 0

# --- display parameters at frame 600, from the STUNRUN-DPY/BASE probe ---
DPYSTRT = 0xFC3C
DPYTAP = 0x0000
DPYCTL = 0xF004
HEBLNK = 0x37          # * 2 = 110
HSBLNK = 0x137         # * 2 = 622
VEBLNK = 19
VSBLNK = 259
PIXPERCLOCK = 2
FINESCROLL = 7
PALBANK = 0

DPYCTL_ORG = 0x0400        # 0 => DPYADR is one's-complemented
DPYCTL_DUDATE = 0x03FC
VRAM_WORDS = 0x40000       # 512 KiB / 2
VRAM_MASK = VRAM_WORDS - 1


def get_display_params(dpyadr, dpystrt=DPYSTRT, dpytap=DPYTAP, dpyctl=DPYCTL):
    """Port of tms340x0_device::get_display_params()."""
    if not (dpyctl & DPYCTL_ORG):
        dpyadr ^= 0xfffc
    return {
        "rowaddr": dpyadr >> 4,
        "coladdr": ((dpyadr & 0x007c) << 4) | (dpytap & 0x3fff),
        "yoffset": (dpystrt - dpyadr) & 3,
        "dudate": dpyctl & DPYCTL_DUDATE,
    }


def rasterise(vram, palette, nframes=None, dpystrt=DPYSTRT, dpyctl=DPYCTL,
              dputap=DPYTAP, heblnk=HEBLNK * PIXPERCLOCK,
              hsblnk=HSBLNK * PIXPERCLOCK, veblnk=VEBLNK, vsblnk=VSBLNK,
              finescroll=FINESCROLL, palbank=PALBANK):
    """Port of harddriv_state::scanline_multisync() over the whole visible area."""
    fb = bytearray(H_TOTAL * V_TOTAL * 2)
    dpyadr = dpystrt
    dpyctl_dudate = dpyctl & DPYCTL_DUDATE
    for v in range(V_TOTAL):
        p = get_display_params(dpyadr, dpystrt, dputap, dpyctl)
        if dpyctl & 0x8000:                      # DPYCTL.NIL = display enabled
            base = (p["rowaddr"] << 10) & VRAM_MASK
            col = (p["yoffset"] << 9) + ((p["coladdr"] & 0xFF) << 3) - 7 \
                + (finescroll & 0x07)
            row = v * H_TOTAL * 2
            for x in range(heblnk, hsblnk):
                ci = col & 0x7FF
                col += 1
                word = vram[base + (ci >> 1)]
                colour = (word >> 8) if (ci & 1) else (word & 0xFF)
                idx = palbank * 256 + colour
                fb[row + x * 2] = idx & 0xFF
                fb[row + x * 2 + 1] = (idx >> 8) & 0xFF
        # DPYADR advance, harddriv_v.cpp update_display_params(). It is gated on
        # the visible window: get_display_params() only reloads from DPYSTRT and
        # only steps once per line while veblnk <= vcount < vsblnk, so DPYADR is
        # frozen through vblank. Advancing unconditionally shears the whole
        # picture by (VBLNK + VTOTAL - VSBLNK) rows.
        if veblnk <= v < vsblnk:
            if (dpyadr & 3) == 0:
                dpyadr = ((dpyadr & 0xFFFC) - (dpyctl_dudate & 0xFFFC)) | (dpystrt & 3)
            else:
                dpyadr = (dpyadr & 0xFFFC) | ((dpyadr - 1) & 3)
    return fb


def build_palette(lo, hi, pens=1024):
    """gsp_palette_change(): red=(lo>>8)&0xff, green=lo&0xff, blue=hi&0xff.

    The dumped files are `pens` consecutive little-endian uint16 entries each
    (the whole 0x100..-byte share), NOT two interleaved halves.
    """
    out = bytearray(pens * 3)
    for i in range(pens):
        if i * 2 + 1 < len(lo):
            l = lo[i * 2] | (lo[i * 2 + 1] << 8)
        else:
            l = 0
        if i * 2 + 1 < len(hi):
            h = hi[i * 2] | (hi[i * 2 + 1] << 8)
        else:
            h = 0
        out[i * 3 + 0] = (l >> 8) & 0xFF
        out[i * 3 + 1] = l & 0xFF
        out[i * 3 + 2] = h & 0xFF
    return out


def read_u16_file(path, count):
    """Return `count` little-endian uint16 words.

    This must return WORDS, not bytes: rasterise() indexes vram with word
    offsets ((rowaddr << 10) & VRAM_MASK), and returning the raw byte string
    silently shifts every row by half.
    """
    import array
    data = open(path, "rb").read()
    a = array.array("H")
    a.frombytes(data[: (len(data) & ~1)])
    if sys.byteorder == "big":
        a.byteswap()
    if len(a) < count:
        a.extend([0] * (count - len(a)))
    return a


def crop_to_visible(fb):
    """The interesting picture is x in [heblnk, hsblnk), y in [veblnk, vsblnk)."""
    x0, x1 = HEBLNK * PIXPERCLOCK, HSBLNK * PIXPERCLOCK
    y0, y1 = VEBLNK, VSBLNK
    w, h = x1 - x0, y1 - y0
    out = bytearray(w * h * 2)
    for y in range(h):
        src = ((y0 + y) * H_TOTAL + x0) * 2
        out[y * w * 2:(y + 1) * w * 2] = fb[src:src + w * 2]
    return out, w, h


def to_rgb(fb, w, h, palette):
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        idx = fb[i * 2] | (fb[i * 2 + 1] << 8)
        r, g, b = palette[(idx % 256) * 3], palette[(idx % 256) * 3 + 1], palette[(idx % 256) * 3 + 2]
        rgb[i * 3] = r
        rgb[i * 3 + 1] = g
        rgb[i * 3 + 2] = b
    return rgb


def write_png(path, rgb, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def histogram(rgb):
    from collections import Counter
    c = Counter()
    for i in range(len(rgb) // 3):
        c[(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2])] += 1
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("out")
    ap.add_argument("--frame", type=int, default=600)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    vram = read_u16_file(os.path.join(a.dumpdir, "gsp_vram.bin"), VRAM_WORDS)
    lo = open(os.path.join(a.dumpdir, "gsp_pal_lo.bin"), "rb").read()
    hi = open(os.path.join(a.dumpdir, "gsp_pal_hi.bin"), "rb").read()
    palette = build_palette(lo, hi)

    fb = rasterise(vram, palette)
    cropped, w, h = crop_to_visible(fb)
    rgb = to_rgb(cropped, w, h, palette)
    write_png(a.out, rgb, w, h)

    nz = sum(1 for x in cropped if x)
    hist = histogram(rgb)
    print(f"rendered {a.out}  {w}x{h}")
    print(f"  framebuffer nonzero bytes : {nz}/{len(cropped)}")
    print(f"  distinct colours          : {len(hist)}")
    top = hist.most_common(10)
    for (r, g, b), c in top:
        print(f"    RGB({r:3d},{g:3d},{b:3d})  {c*100.0/(w*h):6.2f}%")
    if a.verbose:
        # acceptance reference: MAME's own frame 600 has 47 colours with
        # RGB(177,46,36) dominant at 21.71%
        mame = {(177, 46, 36): 21.71, (182, 151, 120): 16.03, (151, 40, 32): 5.60,
                (138, 37, 30): 5.34, (0, 0, 0): 5.33, (114, 75, 42): 4.88,
                (74, 51, 30): 4.24, (94, 63, 36): 3.87}
        print("  vs MAME frame 600:")
        ok = True
        for col, pct in mame.items():
            got = hist.get(col, 0) * 100.0 / (w * h)
            flag = "ok " if abs(got - pct) < 1.0 else "MISS"
            if flag == "MISS":
                ok = False
            print(f"    {flag} RGB{col}  mame {pct:5.2f}%  ours {got:5.2f}%")
        print("  RESULT:", "MATCH" if ok and len(hist) else "MISMATCH")


if __name__ == "__main__":
    sys.exit(main())