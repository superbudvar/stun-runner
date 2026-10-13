#!/usr/bin/env python3
"""Extract the Stun Runner game text from the MC68010 CPU-visible address space.

Run from the repository root:

    python3 analysis/extract_rom_strings.py > analysis/rom-text-strings.txt

Why the ROM files cannot simply be grepped
-----------------------------------------
The MC68010 program ROM files on disk are the twelve 64 KiB *lanes* of an
interleaved image.  The CPU sees each pair of files woven together at stride 2:
one supplies the even byte of every 16-bit word, the other the odd byte.  Any
single lane therefore holds half of each character -- including half of each
space.  Running `strings` over a lane yields printable ASCII that looks
word-shaped but has every letter displaced, which reads convincingly like an
obfuscated encoding and is not one.

So this script reassembles the region from roms/stunrun.manifest.json and checks
it against the region SHA-1 recorded independently in
analysis/static-rom-inventory.md before extracting anything.  On a mismatch it
exits non-zero rather than emitting a dump from an unverified image.

Offsets
-------
The region is mapped at CPU address 0x000000, so a region offset *is* the CPU
address, and file offset in a lane ROM is (address - lane_base) / 2 + 0.

String conventions
------------------
Two record formats share this ROM, and the script handles both:

  Game strings are NUL-terminated and densely packed.  For each NUL-delimited
  chunk the longest printable run is taken as the string.  Where that run does
  not begin at the chunk boundary the bytes in front are 68010 code whose ASCII
  rendering is itself printable (`4e 75` = RTS renders as "Nu", `ff cc` = Scc).
  Such rows are reported under AMBIGUOUS STARTS with their raw prefix bytes
  rather than being silently trimmed by a guess.

  Service/self-test strings are length-prefixed: the byte 0xFF, then a length
  byte, then exactly that many printable bytes.  They are not NUL-terminated,
  which is why a NUL-only scan finds none of them.  Every length in this region
  is self-consistent with the text that follows it, which is the evidence for
  the format.

Usage
-----
  --manifest-only   Do not require the ROM data.  Print the lane map, region
                    geometry and the expected SHA-1, then stop.  This is what
                    works on a fresh clone, where the ROM set is local and
                    deliberately uncommitted.
"""

import argparse
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "roms" / "stunrun.manifest.json"
ROMDIR = ROOT / "roms" / "stunrun"
PREBUILT = ROMDIR / "stunrun_maincpu.bin"

REGION = "mainpcb:maincpu"
REGION_SIZE = 0x100000  # populated through 0xC0000; the remainder is zero
LANE_STRIDE = 2

# SHA-1 of the full 0x100000 region, per analysis/static-rom-inventory.md.
EXPECTED_SHA1 = "75b721654691d5589c3a1d2df72340b657aa6917"

MIN_RUN = 5

ENGLISH = set("""
the and for are you not all can has was one two three four five with this that from
game games start starts press pressed pressing coin coins credit credits insert player
players score scores high low level levels mode music sound sounds demo attract option
options menu language english japanese settings default difficulty track tracks select
time lives extra bonus item items power speed road truck car home name names new test
tested turbo kid dip sw volume free play played continue over ready warning caution title
arcade stage zone mission control enter exit copyright rights reserved all year by
programmed designed display vram ram buffer address addresses data error errors which
value values normal hard easy count counts month day phone code exit remember continued
figure out nothing atari stuns runner star stars red green blue path hold hold touch
drive triggers laser lasers boost pads mag cycles construction crazy letter letters
setting settings original restore save choose choose current beat prove made big done
demonstration censor soundstest zram adsp msp gsp jsa
""".split())


def read_manifest():
    return json.loads(MANIFEST.read_text())


def lane_table(manifest):
    entries = sorted(
        (r for r in manifest["roms"] if r["region"] == REGION),
        key=lambda r: r["offset"],
    )
    if not entries:
        raise SystemExit(f"no {REGION} entries in {MANIFEST}")
    return entries


def print_lane_map(out, lanes):
    out.write("Lane map -- each pair is woven together at stride 2\n")
    out.write("-" * 78 + "\n")
    for entry in lanes:
        out.write(f"  region 0x{entry['offset']:06X}  {entry['filename']}\n")


def build_region(lanes):
    buf = bytearray(REGION_SIZE)
    for entry in lanes:
        path = ROMDIR / entry["filename"]
        if not path.exists():
            return None, entry["filename"]
        data = path.read_bytes()
        if len(data) != entry["size"]:
            raise SystemExit(f"{entry['filename']}: size {len(data)} != {entry['size']}")
        digest = hashlib.sha1(data).hexdigest()
        if digest != entry["sha1"]:
            raise SystemExit(f"{entry['filename']}: sha1 {digest} != manifest {entry['sha1']}")
        base = entry["offset"]
        for i, byte in enumerate(data):
            buf[base + LANE_STRIDE * i] = byte
    return bytes(buf), None


def lanes_for(lanes, addr, length):
    """Names of the lane ROMs that supply the bytes of [addr, addr+length)."""
    out = []
    for entry in lanes:
        base, size = entry["offset"], entry["size"]
        end = base + LANE_STRIDE * size
        if addr < end and base < addr + length:
            out.append(entry["filename"])
    return out


def nul_chunks(data):
    """(start, end) spans between NUL bytes -- the candidate string extents."""
    spans, start = [], 0
    for match in re.finditer(rb"\x00", data):
        spans.append((start, match.start()))
        start = match.end()
    spans.append((start, len(data)))
    return spans


def english_hits(text):
    return sum(1 for w in re.findall(r"[A-Za-z]{2,}", text) if w.lower() in ENGLISH)


# A NUL-chunk prefix of only one or two bytes is normally a tag or length field
# and the string start is still unambiguous; three or more is almost certainly
# 68010 code, so those rows get flagged for review.
PREFIX_SUSPECT_BYTES = 3


def scan_nul_terminated(data, lanes):
    """Game strings: NUL-terminated, densely packed."""
    clean, ambiguous = [], []
    for start, end in nul_chunks(data):
        chunk = data[start:end]
        cut = len(chunk)
        while cut > 0 and 0x20 <= chunk[cut - 1] <= 0x7E:
            cut -= 1
        run = chunk[cut:]
        if len(run) < MIN_RUN:
            continue
        text = run.decode("ascii")
        if english_hits(text) < 2:
            continue
        addr = start + cut
        row = {
            "addr": addr,
            "len": len(run),
            "text": text,
            "lanes": lanes_for(lanes, addr, len(run)),
        }
        if cut >= PREFIX_SUSPECT_BYTES:
            row["prefix"] = chunk[:cut]
            ambiguous.append(row)
        else:
            clean.append(row)
    return clean, ambiguous


def scan_ff_prefixed(data, lanes):
    """Service strings: 0xFF, length byte, then that many printable bytes."""
    found, seen = [], set()
    i = 0
    while i < len(data) - 1:
        if data[i] == 0xFF:
            n = data[i + 1]
            body = data[i + 2 : i + 2 + n]
            if 5 <= n <= 80 and len(body) == n and all(0x20 <= b <= 0x7E for b in body):
                text = body.decode("ascii")
                if "*" in text and english_hits(text) >= 2 and i not in seen:
                    seen.add(i)
                    found.append(
                        {
                            "addr": i,
                            "len": n,
                            "text": text,
                            "lanes": lanes_for(lanes, i, n + 2),
                            "body": i + 2,
                        }
                    )
                    i += 2 + n
                    continue
        i += 1
    return found


def scan(data, lanes):
    """Return (clean, ambiguous, service)."""
    clean, ambiguous = scan_nul_terminated(data, lanes)
    service = scan_ff_prefixed(data, lanes)
    taken = {r["addr"] for r in clean} | {r["addr"] for r in ambiguous}
    service = [r for r in service if r["body"] not in taken]
    return clean, ambiguous, service


def classify(text):
    return "service" if "*" in text and " " not in text else "game"


def emit_rows(out, rows):
    for row in rows:
        lane = ",".join(row["lanes"]) or "-"
        out.write(
            f"  0x{row['addr']:06X}  {row['len']:>3}B  {lane:<29}  {row['text']}\n"
        )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--manifest-only",
        action="store_true",
        help="print lane map and geometry without requiring the ROM data",
    )
    args = ap.parse_args()

    manifest = read_manifest()
    lanes = lane_table(manifest)
    out = sys.stdout

    out.write("Stun Runner MC68010 game text\n")
    out.write("=" * 78 + "\n")
    out.write(f"Source image : assembled from roms/stunrun.manifest.json ({REGION})\n")
    out.write(f"Region       : 0x{REGION_SIZE:X} bytes, CPU base 0x000000 (offset == address)\n")
    out.write(f"Region SHA-1 : {EXPECTED_SHA1}  (expected; verified after assembly)\n")
    out.write(f"Lane weave   : stride {LANE_STRIDE}, {len(lanes)} files\n")
    out.write("Equivalent   : roms/stunrun/stunrun_maincpu.bin (= region[0:0xC0000])\n")
    out.write("Strings      : verbatim; '*' is the service-screen word separator\n")
    out.write("=" * 78 + "\n\n")

    print_lane_map(out, lanes)

    if args.manifest_only:
        out.write("\n--manifest-only: ROM data not required and not present.\n")
        out.write("To extract, place the 12 ROM files in roms/stunrun/ (names above).\n")
        out.write("The ROM set is local and deliberately uncommitted; see\n")
        out.write("analysis/static-rom-inventory.md for provenance.\n")
        return

    region, missing = build_region(lanes)
    if region is None:
        raise SystemExit(
            f"missing ROM {missing}\n"
            f"The ROM set is local and not committed. Use --manifest-only for a\n"
            f"lane/geometry report, or restore roms/stunrun/ to extract strings."
        )
    digest = hashlib.sha1(region).hexdigest()
    if digest != EXPECTED_SHA1:
        raise SystemExit(
            f"assembled region sha1 {digest} != expected {EXPECTED_SHA1}; refusing to "
            f"extract from an unverified image"
        )
    out.write(f"\nRegion SHA-1 : {digest}  [VERIFIED against static-rom-inventory.md]\n")

    clean, ambiguous, service = scan(region, lanes)

    game = sorted((r for r in clean if classify(r["text"]) == "game"),
                  key=lambda r: r["addr"])
    svc_inline = [r for r in clean if classify(r["text"]) == "service"]
    service = sorted(service + svc_inline, key=lambda r: r["addr"])

    out.write(f"\nGAME STRINGS -- in-game text, NUL-terminated, space-separated: "
              f"{len(game)}\n")
    out.write("-" * 78 + "\n")
    emit_rows(out, game)

    out.write(f"\nSERVICE STRINGS -- service/self-test screen, 0xFF+length records,\n"
              f"asterisk-separated words: {len(service)}\n")
    out.write("-" * 78 + "\n")
    emit_rows(out, service)

    out.write(
        f"\nAMBIGUOUS STARTS -- {len(ambiguous)} NUL-terminated string(s) preceded by\n"
        f"{PREFIX_SUSPECT_BYTES}+ bytes of 68010 code inside the same chunk; the ASCII\n"
        f"rendering of that code bleeds into the string head. Left verbatim.\n"
    )
    out.write("-" * 78 + "\n")
    for row in ambiguous:
        lane = ",".join(row["lanes"]) or "-"
        out.write(f"  0x{row['addr']:06X}  {row['len']:>3}B  {lane:<29}  text={row['text']!r}\n")
        out.write(f"             raw prefix: {row['prefix'][-8:].hex(' ')}\n")

    out.write(f"\nTOTAL: {len(game)} game + {len(service)} service "
              f"+ {len(ambiguous)} flagged\n")


if __name__ == "__main__":
    main()
