# STUN Runner static ROM inventory

## Capture and validation

Captured the loaded MAME memory regions (before gameplay) with
`mame/lua/dump-rom-regions.lua`. The script reads each byte from
`machine.memory.regions`, writes one raw binary per region, verifies the output
length, and reports each original MAME region tag and size. These bytes are
static region images, not CPU-visible addresses or runtime RAM.

Reproduction (from the repository root; ROM set is local and is not committed):

```sh
mkdir -p /tmp/stunrun-rom-regions
STUNRUN_ROM_REGION_DIR=/tmp/stunrun-rom-regions \
  /path/to/mame -rompath /path/to/rom-parent stunrun \
  -video none -sound none -skip_gameinfo -nothrottle -seconds_to_run 2 \
  -autoboot_script stun-runner/mame/lua/dump-rom-regions.lua
```

Evidence for this capture: MAME reports `0.289 (unknown)`; the committed ROM
manifest identifies target `stunrun`, S.T.U.N. Runner (rev 6), and MAME
`0.289 (mame0289-dirty)`. `tools/check-roms . /mnt/build/mame-ref/mame`
reported `romset stunrun is good`. Independently, every ROM file in
`stun-runner/roms/stunrun.manifest.json` was byte-compared against its loaded
MAME region at the manifest offset, applying stride 2 to the `mainpcb:maincpu`
and `mainpcb:user1` byte-lane loads and stride 1 to contiguous regions. All 25
manifest entries matched. The MAME build string differs from the manifest's
recorded build suffix; retain that provenance distinction when citing this
capture. The executable used here is `/mnt/build/mame-ref/mame`, SHA-256
`75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`.

Raw ROM dumps remain local because the ROM data is copyrighted. Captured
region SHA-1 hashes:

| MAME region tag | Size | SHA-1 | Manifest coverage / initial interpretation |
|---|---:|---|---|
| `:mainpcb:maincpu` | 0x100000 | `75b721654691d5589c3a1d2df72340b657aa6917` | Twelve 64 KiB interleaved lanes verified. `0xC0000–0xFFFFF` is 0x40000 zero bytes not populated by the rev 6 ROM definition; this establishes an unpopulated region tail, not globally unused CPU addresses. |
| `:mainpcb:jsa:cpu` | 0x10000 | `c26b8ddbcb011416e6ab695980d2cf37e672e973` | One 64 KiB ROM verified. |
| `:mainpcb:user1` | 0x60000 | `b18db322ab8202f36c3fa30c21981a290d87507e` | Six 64 KiB interleaved lanes verified; driver mining identifies it as the ADSP object ROM. |
| `:mainpcb:jsa:oki1` | 0x40000 | `79a67775709a61c8dfbc8d007bd6d38b8ba67911` | Four contiguous 64 KiB sample ROMs verified. |
| `:mainpcb:200e` | 0x800 | `605db3fdbaff4ba13729371ad0c4fbab3889378e` | One EEPROM image verified. |
| `:mainpcb:210e` | 0x800 | `605db3fdbaff4ba13729371ad0c4fbab3889378e` | Byte-identical to `:mainpcb:200e`; separate MAME region, same contents. |

## Initial static-content scan

`strings -a` / printable-run scans on the reconstructed MAME regions did not
reveal obvious human-readable game text. The long printable runs in the main
CPU and `user1` regions look like incidental byte sequences, not identified
strings. This is only a first pass: absence of obvious ASCII does not rule out
packed text, alternate encodings, or structured data.

The `mainpcb:user1` data should be treated as binary object/geometry data, not
as a texture or palette bank based on its label alone. The current scan has not
decoded its record format or established individual asset boundaries. Palette
and rendered pixel data are runtime GSP state and are not among these static
ROM regions.

## Geometry and asset claim check

The supplied description is a useful hypothesis list, not a ROM map. Current
evidence supports a narrower picture:

| Claim | Assessment | Evidence / limit |
|---|---|---|
| Main processor is a 68000 | **Correct family, wrong target part number.** The target is an MC68010. | `PROJECT.md:51-58`; MAME config instantiates `M68010` at `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:1463`. The ROM comment's generic “68000 code” label is not the exact CPU type. |
| A math processor performs polygon/geometry work | **Broadly supported, terminology needs care.** The active math/geometry processor is the ADSP-2100 on the ADSP II board; MAME describes the ADSP board family as doing polygon transforms, lighting, and slope computations. No separate “Mathbox” processor appears in the target processor inventory. | `PROJECT.md:51-60`; `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:61-72,1598`; `analysis/driver-mining/stunrun.machine-map.yaml:17-43`. “Mathbox” may be informal shorthand, not evidence of another chip. |
| Geometry is in raw XYZ vertex lists, polygon index tables, and bounding volumes | **Unconfirmed.** `mainpcb:user1` is explicitly named “ADSP object ROM,” and the active ADSP uses `/MP` and `/SIMBUF`; those facts do not define the record schema. | `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:2709-2715`; `/mnt/build/mame-ref/src/mame/atari/harddriv_m.cpp:842-897`; `analysis/interconnect.md:51-66,141-149`. Exact structures, units, boundaries, and collision data remain unknown. |
| All geometry resides in the main program EPROMs | **Overgeneralized.** Main CPU ROM contains confirmed road-table data, while MAME separately loads 384 KiB of ADSP object ROM in `mainpcb:user1`. | `analysis/track-storage.md:10-31,33-79,101-127`; `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:2691-2715`. The road table is 384 16-bit words selected from observed ROM bases; its words are not decoded into XYZ or segment semantics. |
| Flat-shaded/no-texture models, a full-360° tunnel, and 2D sprite overlays | **Not established by this inventory.** No dedicated texture, sprite, or palette ROM region appears in the rev-6 region list. This does not prove those rendering techniques are absent: the GSP has runtime VRAM/palette state, and the static capture does not describe every drawing path. | Captured region inventory above; `analysis/static-rom-inventory.md:53-57`; target driver config at `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:1463-1483`. |

## MC68010 references into geometry-related data

The main CPU does reference non-code regions of its program ROM that feed the
road/GSP path. The strongest confirmed chain is:

```text
MC68010 selector code (0x024A14–0x024B40)
  -> ROM pointer table at 0x047406 (23 state-indexed longwords)
  -> road-table slots at 0x044630, 0x044930, 0x044C30, 0x044F30, 0x045230, 0x045530
  -> copy routines at 0x029760 / 0x02976E / 0x0297F4
  -> base RAM 0xFF9584–0xFF9883 (384 halfwords)
  -> transfer routine at 0x02248E
  -> GSP FIFO 0xC0000C
```

The pointer table and loader path are documented in
`analysis/track-storage.md:33-93`. A replay trace confirms contiguous `+2`
source reads through the table-loader PCs, including complete 384-word reads
from `0x044630` and later reads from `0x044930`; provenance and exact observed
frames are in
`reference/experiments/stunrun/m5-track-rom-read-sequence.metadata.json`.
Three complete transfer bursts at PC `0x02248E` read the base buffer; the
observed bus/lane behavior yielded 192 FIFO writes for 384 halfword source
reads. This establishes a road-data submission path, **not** the meaning of
the table words as XYZ vertices, curvature, elevation, polygons, or segments.
The twin buffer at `0xFF9884–0xFF9B83` is consumed by a separate animation/
transform path, not this observed FIFO drain; see `analysis/road-upload-path.md`.

A second, less specific set of MC68010 ROM references occurs in the
renderer-adjacent code window `0x02B000–0x02BC00`: the bounded trace reports
non-code reads at `0x0475FC`, `0x0475FE`, `0x047600`, `0x0473CA`, and `0x047604`.
These are renderer/table candidates, not decoded assets or proven geometry
formats. The trace also includes opcode fetches; its non-code summary and
limitations are recorded in
`reference/experiments/stunrun/geometry-rom-read-trace.metadata.json`.

There is also a distinct object-record drawing candidate in RAM: the
`0xFFDD86` record path reaches primitive emitter `0x02F470` through
`0x03D012` → `0x03E51C` → `0x03E3FC`. In the recorded late-drive run,
`0x02F470` was instead called from `0x02B7E8`; the object-record branch did not
execute. Treat the static caller chain as a candidate, not a runtime-proven
source of those rendered primitives. Details and trace provenance are in
`analysis/object-record-renderer-path.md:7-18,58-96`.

**Additional runtime correlation (2026-10-17):** a separate copy routine at
`$02E226` loads a longword from offset `+$26` in each of 23 observed runtime
records, and the subsequent reads at `$02E220/$02E22A/$02E230/$02E25A/$02E264`
match 13 unique targets in the maincpu ROM byte-for-byte across five repeated
`late_drive` runs. This confirms those sampled record fields are consumed as
maincpu-ROM descriptor pointers, but does not connect them to the
`$FFDD86`-to-emitter path, identify any entry as a vehicle, or prove global
table boundaries. The 23,210 same-run GSP FIFO writes have multiple writer
PCs and are not attributed to these entries. Full provenance and limitations:
`analysis/object-pointer-deref-correlation.md` and
`reference/experiments/stunrun/m5-object-pointer-deref-correlation/metadata.json`.

These are MC68010-visible program-ROM references. The separate `mainpcb:user1`
ADSP object ROM is read through the ADSP `/MP` and `/SIMBUF` interface; it is
not a main-CPU ROM overlay or a direct reference from the MC68010 address
range. Its record/asset boundaries remain unknown (see
`analysis/static-rom-inventory.md:110-128`).

The supplied `slapstic.cpp` pointer is not the useful source for this target's
geometry path: that file documents Atari's bank-switching/security chip, not
the geometry pipeline (`/mnt/build/mame-ref/src/mame/atari/slapstic.cpp:15-20`).
Start with the target-specific `harddriv.cpp`/`harddriv_m.cpp` driver and the
captured runtime interfaces instead. The suggestion to use
Ninja Ripper or a generic memory scanner to obtain OBJ meshes has not been
verified and is not needed for the current ROM-level investigation.

## Zero-run candidates (not “unused” findings)

A byte scan of the captured, reconstructed `mainpcb:user1` region found these
long zero runs:

| Region-relative range (end exclusive) | Length | Observation |
|---|---:|---|
| `0x1F1F1–0x20002` | `0x0E11` | Zero bytes cross the `0x20000` lane-pair boundary. |
| `0x3FE82–0x40001` | `0x017F` | Zero bytes cross the `0x40000` lane-pair boundary. |
| `0x49B02–0x60000` | `0x164FE` | Zero through the end of the 0x60000-byte region. |

The main CPU region also has the previously recorded `0xC0000–0x100000`
unpopulated tail. These are **fill/unpopulated candidates only**: none is
classified as unused by the game. The object-ROM region is loaded from six
interleaved EPROM files, so zero-valued bytes in its loaded image must not be
confused with absent ROM loads. Promotion to “unused” requires an address/read
trace over the relevant execution modes and, for `user1`, a trace correlating
ADSP `/MP`, `/SIMCLK`, and `/SIMBUF` values to region offsets.

## Provenance caveat for runtime traces

The static ROM capture described above used `/mnt/build/mame-ref/mame` with
SHA-256 `75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`.
The current `analysis/driver-mining/stunrun.machine-map.yaml:4-9` records a
different pinned executable SHA-256 (`fcb54790e1837ed653020dc25a4e51d9ba7288d789cf556299d5d4d2cf2f3bc1`).
Existing ADSP trace metadata records the MAME version string but not that
binary hash. Keep those runtime traces distinct from this capture until their
executable provenance is reconciled; the static region bytes themselves were
independently checked against all 25 rev-6 manifest loads.

### Object-ROM access path (driver evidence)

`/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:2691-2715` defines the rev 6
`mainpcb:user1` region as `ROM_REGION16_BE(0x60000, ..., "384k for ADSP object
ROM")`; the six manifest loads fill it completely as three 128 KiB even/odd
lane pairs. In the pinned driver implementation,
`/mnt/build/mame-ref/src/mame/atari/harddriv_m.cpp:842-897` implements the
ADSP special-register path: `/SIMBUF` reads `m_sim_memory` and increments the
SIM address (returning `0xff` out of range), while `/MP` sets the base to
`0x10000 * data`. This is MAME-CONFIRMED modeled access, not proof of which
object records the original title path selects. Existing project evidence in
`analysis/interconnect.md:141-149` identifies `user1` as this separate object
ROM—not ADSP executable program storage—and leaves runtime ownership/bank use
open.

This gives a concrete next reference-tracing seam: correlate ADSP `/MP`,
`/SIMCLK`, and `/SIMBUF` accesses with selected bank/address and the MAME
object-ROM bytes. Do not interpret arbitrary reads as asset boundaries until
that selector/read sequence is captured.

## Frame-600 runtime memory snapshot

Captured MAME runtime memory at frame 600 with the existing
`tools/mame-memory-snapshot` and `tools/mame-adsp-program-dump` runners. The
frame-600 screen capture at `/tmp/opencode/stunrun-frame600-screen.png` shows
the tunnel/road 3D scene. These are runtime snapshots, not new static ROM
regions; nonzero memory alone does not identify geometry records.

| Space / range | Observation at frame 600 | Local evidence |
|---|---|---|
| ADSP data RAM, `:mainpcb:adsp` data `0x0000–0x1FFF`, 16-bit | 982 of 8,192 words nonzero. Only the mapped RAM range was read; the ADSP special-I/O range was not read because `/SIMBUF` reads have side effects. | `/tmp/opencode/stunrun-frame600-adsp-data/snapshot-600.json`; snapshot-file SHA-256 `87b3232b75bcfb2754a8fbed6b692b0e9742ab6bab6f9e8c275a8751e8cfe075`. |
| ADSP program RAM, `:mainpcb:adsp` program `0x0000–0x1FFF`, 32-bit | 4,356 of 8,192 words nonzero. This is the uploaded ADSP program image, not proof that these words are geometry assets. | `/tmp/opencode/stunrun-frame600-adsp-program/adsp2100.bin`; image SHA-256 `b8c2d2594d609a110a26a47aa2cad11f8db3783643a5e3fb49b1897660051c02`. |
| GSP shared VRAM, `:mainpcb:gsp` program `0xFF800000–0xFFBFFFFF`, byte reads | 2,563,776 of 4,194,304 bytes nonzero. This confirms populated runtime GSP shared memory during the rendered scene, but its contents have not been semantically decoded. | `/tmp/opencode/stunrun-frame600-gsp-vram-shared/snapshot-600.json`; snapshot-file SHA-256 `a68674e7b8567dee7054701adf1ffe7bc62bfc2db93ff367e0ef7e89f901ed36`. |
| GSP aperture, `:mainpcb:gsp` program `0x02000000–0x020FFFFF`, byte reads | The byte-view snapshot returned zero for the whole range. Do not infer that this address range is unused: the MAME map routes it through a 2-bpp handler, while the backing shared RAM is separately visible at `0xFF800000`. | `/tmp/opencode/stunrun-frame600-gsp-vram/snapshot-600.json`; snapshot-file SHA-256 `295988fc3e0be18e40ddb7fefee916cd1a45b2879b2a1318fb18e2fb80644bcb`. |

Capture provenance: all frame-600 snapshots used `/mnt/build/mame-ref/mame`,
SHA-256 `75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`,
reporting `0.289 (unknown)`. They ran with `-nonvram_save` and loaded the
current host NVRAM (SHA-256 `520cf5315b9c0c6727fa0de26c967180f1f207bbfa60feda7852f189bbf93077`
for `mainpcb_200e`; `0fe1c786431209f4c8e7762e469678d5856a5018cb7a83e440a17df3176d8f50`
for `mainpcb_210e`). The local binary hash differs from the pinned hash noted
above; earlier isolated-NVRAM ADSP snapshots also reported a different
nonzero program-word count (2,718 vs. 4,356 here). Keep the captures
provenance-separated rather than attributing the count difference to one
cause. ROM-derived/runtime binary contents remain outside the repository.

### Address-space distinction

There is no direct overlay of the `mainpcb:user1` object-ROM bytes into the
68010 ROM address range. MAME maps the 68010 ROM at `0x000000–0x0FFFFF` and
installs separate ADSP RAM/buffer apertures at `0x800000–0x807FFF`,
`0x808000–0x80BFFF`, and `0x810000–0x813FFF` (`harddriv.cpp:602,4818-4836`).
The ADSP reads the object ROM through its special `/SIMBUF` interface;
`/MP` selects a `0x10000 * data` base into `mainpcb:user1`
(`/mnt/build/mame-ref/src/mame/atari/harddriv_m.cpp:842-897`). The GSP VRAM
addresses are likewise in the GSP's own program space, not the 68010's ROM
space (`/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:619-628`). These
processor-specific windows are real memory interfaces, but they are not
overlays at the static asset ROM offsets being investigated.

## Next mapping work

- Classify byte-lane layout and candidate records within `mainpcb:user1`; trace
  the main-CPU source reads/copy loop and the ADSP program/data interpretation
  before assigning object names or boundaries.
- Search all populated main-CPU and sound-program regions for structured text,
  tables, repeated fill patterns, and references; separate code from data using
  runtime read evidence and ROM-load boundaries.
- Distinguish the confirmed unpopulated `maincpu` region tail from any claim
  that addresses or ranges are unused by the running game. Such a claim needs
  address-map and execution/read evidence.
