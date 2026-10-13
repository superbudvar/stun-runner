# Stun Runner program-ROM text strings

## Question this answers

Do the ROMs carry game text, and do we have it extracted?

**Yes, and yes.** 199 game-text strings, verbatim, with offsets, in
`analysis/rom-text-strings.txt`, regenerated and verified by
`analysis/extract_rom_strings.py`.

| Class | Count | Record format |
|---|---:|---|
| In-game text | 143 | `0x00`-terminated, space-separated |
| Service / self-test | 56 | `0xFF`, length byte, then that many bytes; `*`-separated words |
| Flagged (see below) | 3 | in-game, string head ambiguous |

## The thing that makes this non-obvious

The MC68010 program ROM files on disk are **not** readable as text, and
`strings` over them yields output that looks word-shaped but has every letter in
the wrong place:

```
0x3713  RS TR UTN        0x37ad  EIL NIER  Wl ol
0x371d  NET% ON          0x37cd  OMNCTOS   DnDenie
```

This is **not** an encoding and **not** obfuscation. Those files are the twelve
64 KiB *lanes* of an interleaved image. The CPU weaves each **pair** together at
stride 2 — one supplies the even byte of every 16-bit word, the other the odd
byte — so any single lane holds half of each character, spaces and punctuation
included.

Reassembling from `roms/stunrun.manifest.json` reproduces the region SHA-1
already recorded independently in `analysis/static-rom-inventory.md`:

```
75b721654691d5589c3a1d2df72340b657aa6917   :mainpcb:maincpu, 0x100000 bytes
```

`extract_rom_strings.py` rebuilds that region, verifies the SHA-1, and **exits
non-zero** rather than emit a dump from an unverified image (checked by perturbing
a manifest offset). `roms/stunrun/stunrun_maincpu.bin` is the same image already
assembled — byte-identical to `region[0:0xC0000]`; the `0x40000` byte tail from
`0xC0000` is unpopulated zeros.

## Offsets

Region base is **CPU address `0x000000`**, so **region offset == CPU address**.

| Address | Len | Lane ROM(s) supplying the bytes | Text |
|---|---:|---|---|
| `0x0066BD` | 16 | `136070-2102.200r,136070-2101.210r` | `VRAM*VERIFY*TEST` |
| `0x0067F1` | 9 | `136070-2102.200r,136070-2101.210r` | `TEST*MENU` |
| `0x020ED1` | 15 | `136070-6104.200s,136070-6103.210s` | `GAME DIFFICULTY` |
| `0x020F25` | 36 | `136070-6104.200s,136070-6103.210s` | `CONTINUE IS ONE COIN LESS THAN START` |
| `0x04398F` | 34 | `136070-6106.200t,136070-6105.210t` | `Hold a Coin Switch and press START` |
| `0x046B62` | 13 | `136070-6106.200t,136070-6105.210t` | `INSERT 1 COIN` |
| `0x046B8E` | 26 | `136070-6106.200t,136070-6105.210t` | `Copyright 1989 ATARI GAMES` |
| `0x046BBA` | 9 | `136070-6106.200t,136070-6105.210t` | `GAME OVER` |
| `0x046F3A` | 34 | `136070-6106.200t,136070-6105.210t` | `THE ANCIENT ONE:     Rick Moncrief` |
| `0x0474F4` | 52 | `136070-6106.200t,136070-6105.210t` | `Sorry STUN Runner, though you have the potential, it` |
| `0x04A9B4` | 13 | `136070-6106.200t,136070-6105.210t` | `ADDRESS ERROR` |
| `0x05BDF9` | 10 | `136070-6106.200t,136070-6105.210t` | `Coin Mode:` |
| `0x05BFA8` | 9 | `136070-6106.200t,136070-6105.210t` | `Free Play` |

Full table with per-string lane attribution is in the `.txt`.

`0x020ED1` also appears under *flagged strings* below — it is a valid string, but
68010 code sits in front of it in the same chunk, so the scanner reports it
separately.

### Converting a region address back to a lane file

```
file_offset_in_lane = (address - lane_base) / 2
```

with `lane_base` from the lane map (e.g. `0x040000` for `136070-6106.200t`). The
even bytes of the pair live in the file at the lower-named lane, the odd bytes in
the other — which is why neither file alone is readable.

### Lane map

| Byte offset | ROM |
|---|---|
| `0x000000` | `136070-2102.200r` |
| `0x000001` | `136070-2101.210r` |
| `0x020000` | `136070-6104.200s` |
| `0x020001` | `136070-6103.210s` |
| `0x040000` | `136070-6106.200t` |
| `0x040001` | `136070-6105.210t` |
| `0x060000` | `136070-2108.200u` |
| `0x060001` | `136070-2107.210u` |
| `0x080000` | `136070-2110.200v` |
| `0x080001` | `136070-2109.210v` |
| `0x0A0000` | `136070-2112.200w` |
| `0x0A0001` | `136070-2111.210w` |

## Two record formats

- **In-game** strings are `0x00`-terminated and densely packed
  (`INSERT %d COINS\0INSERT 1 COIN\0INSERT %d COINS\0…`).
- **Service / self-test** strings are `0xFF` + one length byte + exactly that many
  text bytes, and are *not* NUL-terminated:

  ```
  0x0066B0  ff 09 41 44 53 50 2a 54 45 53 54            ADSP*TEST
  0x0066BD  ff 10 56 52 41 4d 2a ...                     VRAM*VERIFY*TEST
  ```

  All 96 records found this way have a length byte that exactly matches the text
  that follows, which is the evidence for the format rather than an assumption.

  A NUL-only scan finds **zero** service strings — that is the trap here.

### Confirmed against the running game

| ROM | Screenshot |
|---|---|
| `0x046B62  INSERT 1 COIN` | title screen `INSERT 1 COIN` |
| `0x017010  Copyright 1989 ATARI GAMES` | title screen credit line |
| `0x046BBA  GAME OVER` | in-game |
| `0x05BDF9  Coin Mode:` … `0x05BFA8  Free Play` | service menu |

## The flagged strings

NUL-terminated strings whose printable run does **not** begin at the NUL boundary,
because 68010 code in front renders as printable ASCII. Left verbatim and listed
separately rather than trimmed by a guess.

| Address | Reported | Raw prefix (last 8) | Reading |
|---|---|---|---|
| `0x017010` | `Copyright 1989 ATARI GAMES` | `02 bb 5a 30 5f ee 3d a8` | head correct — the identical string at `0x046B8E` and `0x046F5E` is clean |
| `0x020ED1` | `GAME DIFFICULTY` | `ff d0 88 90 89 4e 75 fd` | head correct — identical string at `0x021025` is clean |
| `0x04397A` | `N^NuJust press START` | `58 8f 4c ee 1c fc ff cc` | **actually `Just press START` at `0x04397E`**; prefix ends `ff cc` (Scc), `4e 5e` (MOVEC), `4e 75` (RTS) |

Only the third is actually wrong. `4e 75` is `RTS`, and its two bytes spell `Nu` —
that is the one mechanism found where 68010 code masquerades as string text. The
other two are flagged conservatively because their prefix is long enough to look
like code; the strings themselves begin where reported.

The script flags any run with ≥3 bytes of non-printable data in front of it inside
the same chunk. A 1–2 byte prefix is treated as a tag or length field and the
string start is taken as unambiguous.

## Refuted: the earlier reading of this data

An earlier pass concluded the text was obfuscated and recorded a list of
eliminated encodings (XOR, bit permutation, Caesar, monoalphabetic substitution,
columnar transposition, per-index arithmetic). **Wrong, and wrong in one specific
way: every hypothesis was tested against half the data.** Each transform was
applied to a single 64 KiB lane, where the letter stream is interleaved with bytes
belonging to a different ROM. A substitution search over that can only converge to
noise — exactly the `+0.36` logprob/letter that was observed.

Also refuted:

- *"The program ROMs are not mapped at `0x000000`; load base unknown."* Base is
  `0x000000`. `prog-hi0.s20` starts `00 00 00 12 00 29 00 29 …` because it is one
  lane — those are not the bytes of a reset vector.
- *"`stunrun/mc68010_rom.bin` is mislabeled."* Not established either way. It is
  0xB0000 bytes, starts like `gsp_rom.bin`, and is not the `maincpu` region, but
  nothing here shows it is wrong about anything.
- *"146 strings"*, from a naive printable-run scan that merged code bytes into
  string heads and missed the `0xFF` record format entirely. An intermediate
  structured scan then found 139; the current figure is **199** (143 in-game +
  56 service), plus 3 flagged.

The transferable lesson: **before hypothesising an encoding, prove the image is
the image the CPU sees.** A verified region SHA-1 is a one-line check that settles
it, and should come before any cryptanalysis.

## Reproducing

```sh
cd ~/projects/decomp/stun-runner
python3 analysis/extract_rom_strings.py > analysis/rom-text-strings.txt
```

The ROM set is local and deliberately uncommitted. On a fresh clone, use:

```sh
python3 analysis/extract_rom_strings.py --manifest-only
```

which needs no ROM data and prints the lane map, region geometry and expected
SHA-1. Place the 12 ROM files in `roms/stunrun/` to extract strings.

Confidence: **KNOWN** for every offset, length and string byte above — the region
was assembled from manifest-verified ROM files and matched against the SHA-1
recorded independently in `analysis/static-rom-inventory.md`. **UNKNOWN** remains:
what draws the `0xFF` service records and what consumes the in-game strings, and
whether any string is reachable only on hardware revisions other than the rev 6 set
present here.
