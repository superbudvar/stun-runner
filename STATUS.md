# Current Status

## Current milestone

**M5 — First visible output**

## State

M0: **complete** — reproducible machine, harness, evidence, coverage, and emission baseline committed

M1: **complete** — evidence-backed machine/interconnect contract and bounded
input-dependent renderer boundary committed

M2: **complete** — fixed-origin ADSP replacement image is built, placed, and
executed in the pinned MAME runtime with repeatable read-back and PC advance

M3: **complete** — source-defined ADSP reset/setup slice installs before the
original upload, reaches a repeatable bounded loop, and matches the
synchronized non-ADSP checkpoint boundary

M4: **complete** — deterministic native timing/input/rendering boundary, replay,
checkpoint, and logging scaffolding verified

Original in MAME: **ROM-validated and runtime-inventoried; title checkpoint established**

Step 0 driver mining: **worked example retained; runtime tags reconciled against system MAME 0.289**

Reproduction target: **initial ADSP replacement slice only; main-CPU gameplay behavior has not been reproduced end-to-end**

Native target: **deterministic replay/checkpoint scaffolding and evidence-backed slices; a C translation now covers the 68010 state-dispatch table, but state handlers and integrated gameplay behavior remain unported**

Verification harness: **bounded replay/checkpoint/trace path implemented; M1 machine facts are promoted into selectors and evidence fixtures**

## Immediate objective — M5 independent scanout comparison

Bring up the CPU GSP scanout reference and independent OpenGL shader scanout in
two separate windows over the same live VRAM/palette/register state. The shader
must consume raw board state rather than CPU-rendered RGB. Preserve bounded
headless behavior and validate GPU readback against deterministic CPU scanout
fixtures before treating the comparison as accepted output.

### Current M5 progress

The dual-window scanout slice is implemented. With `STUNRUN_LIVE_VIDEO=1` and
the SDL/OpenGL backend, the shell creates **GSP Scanout (CPU Reference)** and
**OpenGL GSP Scanout** windows. The GPU shader reads raw packed VRAM and palette
textures; the CPU supplies line-address state, not rendered RGB. The GL parity
test covers three register cases and currently reports zero differing bytes.
The ROM-backed shell opens both windows and reaches its bounded terminal frame.

This does not claim that the TMS34010 RTL emits pixels or that the GSP has
executed the game display list. The RTL output path remains distinct work; the
comparison is between two scanout implementations over the shell's available
board state.

**Superseded for the live path.** `./run.sh` now presents the Verilated core's
own output: 84,337 nonzero words, `core-set=[DPYSTRT DPYCTL]`,
`lit=122880/122880 colours=225`, and captured PNGs show the game's screen (a
logo band and text rows), stable across frames 500/700/900. Findings §39.
Judging this by `hash=` alone is not possible — every wrong-origin defect here
produced a stable hash; use `lit=`/`colours=`. Still open in §39.6: colour
equality against MAME, a sheared image, and the unresolved cycle offset.

The CPU-free oracle replay is **not** a matching screen: at frame 450 the
renderer differs from MAME on 99.72% of pixels. MAME frame 240/480 remain the
same title image, while the replay consumes an incomplete display-register
shadow (`core-set=DPYSTRT` only) and its physical VRAM is 47.70% different from
MAME at the checkpoint. Findings §46 records the comparison. Do not treat the
replay's visible/225-colour output as screen parity.

**Oracle-driven app replay.** `./run.sh --gsp-replay <capture-dir>` now stages
the pre-release upload into the GSP board without a live 68010, then runs the
Verilated core from the captured HLT-release boundary while applying the
oracle's post-release host transactions in sequence. The 701,877-row capture
is split into 631,200 staged pre-release host writes and 70,672 post-release
host writes; replay timing is frame-ordered because the capture has no CPU
cycle stamps. The shared renderer shows output at frame 420 (225 colours); the
frame-450 image is `/tmp/opencode/boundary-full.png.frame-0450.png`. RTL emitted
one HINT/INTOUT assert and clear. This is not MAME parity: the oracle capture
still lacks GSP host-read responses and MAME's INTOUT edge timeline. Findings
§42. Truncated-prefix control is rejected against `result.json`'s stored count.

### Parallel thread — GSP execution (`stun-runner-gsvp`)

**ADSP output semantics established for the observed ordinary renderer path
(2026-10-17).** Fresh same-run evidence joins ADSP SOM output through banked
SOM, unchanged 68010 ferry words and GSP reads to actual pixel writes. Records
carry colour/style, left/right X endpoints, starting Y and signed 16.16 edge
increments with inclusive vertical runs; a separate offset list orders the
records. Predictions match 18,720 primary horizontal-fill groups across two
windows, including endpoints, write addresses and first-word coverage masks.
See `analysis/adsp-output-semantics.md` and findings §90. This does not claim
all alternate workers, object identities or native gameplay parity. It also
refutes the old prefix codec's word-2-as-DX interpretation; the codec fix and
live-prefix regression are now in place (`stun-runner-3vb` closed).

**ADSP `$04B2` C slice (2026-10-17).** `reproduction/adsp/som_producer.c` now
transcribes the captured two-pass tuple transform: it resolves three `SIMCLK`
addresses through the `/MP=1` object-ROM word window, reproduces all 54
pass-1/pass-2 ring writes, and composes the derived rings with the existing SOM
scan/walk to match all nine 16-word records. The source DM table and selected
tuples are now produced from bounded captured DM/SIMBUF/PM operands; the record
schedule, control word and selector starts remain measured entry inputs.
Alternate branches, `$04EB-$04FD` wrapper state, and live gameplay scheduling
remain open. See findings §92-95 and `reproduction/adsp/README.md`.

**ADSP source-table C slice (2026-10-17).** `reproduction/adsp/source_producer.c`
now derives the 57 observed `$015C/$0171` writes in `$0147-$0173` from the
same-run DM/SIMBUF/PM operands. The composed source-selector test constructs
that table before selecting tuples and walking SOM records. The transaction
artifact retains two repeated same-seed captures with 171 PM read events each.
The record schedule, control word, selector starts, and entry data for the
upstream dispatch remain measured inputs; E8 MR2/caller-loop flags, alternate
branches, `$04EB-$04FD` wrapper state and live gameplay scheduling remain
unmodeled. The single-transaction SOM/GINT envelope is now composed below.
See findings §93 and
`reproduction/adsp/README.md`.

**ADSP ring clamp slice (2026-10-18).** `reproduction/adsp/som_clamp.c` now
transcribes `$04FE-$050D` and is composed between the `$04B2` ring transform and
the SOM walk. The frame-551 oracle path leaves all 54 ring words unchanged;
synthetic boundary vectors cover the disassembled saturation branches, which
remain unobserved at runtime. The preceding `$04EB-$04FD` caller logic and
alternate `$0663` mode are still open. See findings §94.

**ADSP transaction envelope (2026-10-18).** The C path now composes the complete
two-zero-prologue plus nine-record SOMLATCH stream with opposite-bank addresses,
then derives GINT `$0045` from the final record's walk `AX0`, not a completion
table. Per-record final `AX0` values match same-run PC `$05D4` snapshots; the
command request bit at PC `$0046` remains an explicit captured entry input. The
ROM-free `./run.sh --adsp-som` diagnostic view now uses this full derivation
path. This is still not live gameplay/ADSP scheduling, and non-captured caller
modes remain unmodeled. See findings §95 and `adsp-som-view-slice-c`.

**Fixture checker cross-checks the oracle capture (2026-10-18).**
`tools/check-adsp-som-fixture` no longer relies only on identities internal to
the transcription: it verifies the committed capture's 146-word stream against
its own recorded digest and requires the nine fixture records to appear at that
stream's offsets, so a uniformly wrong record can no longer pass. A negative
control corrupts a single side value. See findings §96.

Distinct from M5. The GSP has no ROM: `mainpcb:user1` is the ADSP object ROM,
and the 68010 programs the GSP through VRAM. Until a GSP executes, the display
list is never rasterised and the 68010 spins at `$022766` waiting on a queue
nothing fills.

- **`gsvp.1` vendoring — done.** MIT-licensed `birdybro/TMS34010_sv` pinned at
  `94a258e80a07ceb4303ce0b99818df832e96007f` under `third_party/tms34010_sv/`,
  33 modules / 13,937 lines, with `PROVENANCE.md` and a SHA-256 manifest.
- **`gsvp.2` toolchain — done.** Verilator 5.052 at
  `$HOME/.local/share/verilator-5.052`; the core is an *optional* CMake target
  that skips cleanly when Verilator is absent, so the ROM-free build gains no
  simulator dependency.
- **`gsvp.3` host harness — DONE.** `native/gsp_rtl/gsp_board.c` is the
  toolkit-free C99 board model (memory map, host port, reset/clock), testable
  without Verilator, and it now consumes the corrected units.
  `GSP_BOARD_VRAM_BYTES` is `0x80000` (512 KiB), and every VRAM path converts
  the GSP **bit** address through one function,
  `((addr - 0xFF800000) >> 3) & 0x7FFFF`. The host HSTDAT store is a packed
  16-bit word at a 2-byte physical offset, not a replicated 16-byte cell; the
  `& 0xfffffff0` is a 16-bit-*word* alignment. The 2bpp window offset,
  its colour source (`ctrl_lo[1]`), its endianness, and the palette index and
  word order were fixed with it — each had absorbed the same unit error.
  Checked three ways: unit tests pinned against the ROM's own
  `$FF896040 -> $12C08` text address (negative control: removing the `>> 3`
  gives 11 failures); `tools/check-gsp-replay` byte-identical to the native
  model at 524,288 bytes, service mode frame 48, `RESULT PASS`; and the native
  dump reads `df df df df df df 00 00 ...` at `$12C08`, byte-identical to
  MAME's direct share capture, with the byte-addressed `$096040` empty.
  Findings section 35.
  - The mapped **address** range is unchanged (0xff800000-0xffffffff, mirrored
    through bit 22). It is the share behind it that is 512 KiB, and that is
    what 4 MiB got wrong.
  - Not yet shown: agreement with the native model plus a MAME-anchored text
    cell is a chain, not a whole-share comparison. The 2bpp expander's
    big-endian store previously cancelled its own big-endian read, so a
    differential check cannot see an error both sides share.
- **`gsvp.4` first milestone — DONE, and the core keeps going past it.** The
  Verilated TMS34010 core executes the uploaded display list and reproduces
  **MAME's first output byte for byte**. The GSP is frozen by HSTCTL bit 15 for
  the whole capture, so the release frame edge is the one window in which only
  the GSP can change the share; both sides agree on it (MAME frame 405 / PC
  `$0002C1C4` / HSTCTL `$7800`; native record 899809 / PC `$0002C1CC` / HSTCTL
  `$7800`).
  Replaying the native stream to its own release and running 200,000 GSP cycles:
  PC `$00000000 -> $FFF59A70`, 24 distinct low-16 values, 11,948 board writes.
  Over the window the pre/post-release pair itself defines (bytes 446118-448912,
  1397 words): **core matches post-release 1397/1397, and changed exactly the
  1295 words MAME changed — none more, none fewer.** Findings sections 36, 37.
  - **It converges.** Sweeping the cycle budget, the core reaches 83,153 nonzero
    words against MAME's 83,194 — **99.95% of the content MAME has written** —
    with the PC range widening to `$FFF59DD0` and distinct low-16 going 24 -> 542.
    A program running, not a loop. 200,000 cycles is about one MAME frame after
    release, which is why whole-share agreement is highest at that budget.
  - **The 2,388-word whole-share residual is a trajectory offset, not model
    error**, and is constant from 20k to 300k cycles. 2,023 of it is the core
    having zeroed a region MAME still holds at its `0xFFFF` boot fill, and 365 is
    a ramp (`0505 0a0a 0f0f 1414 ...`) in `$060000-$07FFFF`. MAME clears that
    region itself by frame 600 (only 117 of the 2,023 are still nonzero), so the
    core is ahead on the same path, not wrong. Weaker claim than section 36, and
    recorded as such.
  - **The oracle needed re-capturing first:** the recorded boundary images were
    4194304 bytes of program ADDRESS, 8x the share. Corrected they give **1295
    changed words in 22 ranges**, and `10360 / 8 = 1295` exactly — the old
    address-space figure and the new physical one agreeing on an 8x factor is
    the strongest evidence in the file for the `>> 3`.
  - **Still not shown:** whole-share parity at a *stated frame* (needs the 68010
    cycle at which MAME reaches each frame paired with an equivalent bound on
    the core's run), and display-list *semantics* — the output so far is uniform
    fill, a clear, and a ramp. Section 18.2 is untouched.
  - `tools/check-gsp-first-output` derives its window from the images rather than
    assuming it, checks the SET of changed words so a halted core cannot pass,
    and refuses a 4194304-byte input by name.
- **`gsvp.5` upload boundary — bus boundary done (`stun-runner-8p4.2`); old VRAM
  images superseded.** The GSP program is
  now pinned to an upload and a release rather than inferred from a frame-600
  dump. `tools/capture-gsp-program-upload` records the ordered 68010 host-port
  stream and images the GSP program-space address window either side of the
  boundary. The old 4 MiB images were not physical-share captures; do not use
  them as VRAM oracle data. The 512 KiB direct share capture is a separate
  frame-600 observation, not a replacement boundary capture.
  - The halt that holds the GSP during the upload is **HSTCTL bit 15**, not
    `/GSPRES`. `/GSPRES` goes released once at power-on *before any host write*
    and is toggled again at frames 362 and 396 while the GSP stays halted.
  - `$0002FE68`/`$0002FE6E` write 6,091 words each across frames 396–405 into
    `$FFF40000`–`$FFFFFFE0`, after which `$0002C1C4` clears HLT in frame 405 at
    HSTCTL `$7800`. **Every** sampled post-release GSP PC
    (`$FFF42E50`, `$FFF40F90`, `$FFF41060`, `$FFF414C0`) lies inside that
    uploaded range.
  - Diffing the two boundary images isolates the GSP's **first output**:
    10,360 words in 22 contiguous ranges inside `$FFB67530`–`$FFB6CC80`, every
    range previously zero and filled with one repeated constant word, nothing
    overwritten. That is uniform-fill behaviour, not the upload.
  - The `--boot` native replay remains weaker evidence than this: it replays a
    stream that mixes upload and runtime submissions, whereas this capture
    separates them by construction.
  - Open: `$00069A` issues 224,522 host writes to a composed destination of
    `$00000000`, which is not a mapped cell of the share; and it is not yet
    established whether the two program writers interleave as halves of one
    32-bit fill. See `analysis/gsp-program-upload-boundary.md`.
- **Correction, 2026-10-13:** the two Lua-tap behaviours first filed as MAME
  driver defects (MD-10, MD-11) are **withdrawn** — they are documented
  behaviour and misuse respectively, not defects, and `BUGS.md` says its entries
  get reported upstream, so false ones are a liability. MAME documents
  `passthrough:reinstall()` as the remedy for a handler dropped by a handler
  change, and one call restores the tap (measured 29,287 events before the read,
  139,120 after). The behaviour is still worth guarding against because the
  removal is silent. Refutation: `docs/mc68010-boot-findings.md` §28.
- **Closed 2026-10-17:** the pinned oracle binary is not stock MAME.
  `/mnt/build/mame-ref` carries local observation-only hooks in
  `harddriv_m.cpp`, `harddriv_v.cpp` and `tms34010.cpp`. **They are recorded**,
  in `analysis/mame-local-patches.json` (8 patches across those three files plus
  `tms34010.h` and a genie version stamp), and
  `tests/test_mame_local_patches.py` fails the suite on any unrecorded change.
  The entry that used to say this repository "does not yet record them" was
  stale.
- **But the binary inventory is incomplete.** `reference/` metadata names **two**
  MAME executables, and one of them is in neither recorded slot:
  `75b09ee4…` (the oracle, ×9) and `fcb54790e1837ed6…` (`executable_sha256` in
  `adsp-program-snapshot.metadata.json`, ×2, **unrecorded**). The recorded
  observer `55956f2b…` appears in no committed metadata at all. So at least one
  committed measurement came from a build this repository cannot name. See
  `stun-runner-w55`.

### M4 completion

The native shell emits deterministic fixed-step timing, a generic replay input
latch/hash, a blank 320×240 RGB framebuffer hash, transport logging, and a
one-line `stunrun-checkpoint/v1` document. The checkpoint is explicitly a
`native-shell-transport-model` artifact: its ADSP image-region summary and
install selector are derived from the shell model, while unimplemented CPU
registers remain zero. Rendering remains blank by design until M5 evidence
supplies actual visible output. Full evidence is in
`analysis/m4-native-shell.md`.

`tools/mame-replay` now supports `--load-state`: it stages a saved checkpoint
in an isolated MAME slot, uses a long bounded allowance for states whose
emulated time is already advanced, and records relative experiment frames in
the result. A live installed-MAME fork from the frame-2400 `late_drive` state
applied the common `coin_start` schedule through relative frame 600; provenance
is in `reference/experiments/stunrun/replay-save-state-fork.metadata.json`.
Two 1,800-frame relative steering forks from that same state now produce
different exact-frame screenshots with `--nothrottle`; the schedules and
hashes are recorded in
`reference/experiments/stunrun/saved-state-render-differential.metadata.json`.
The reusable `stunrun-known-regions/v1` mask and
`tools/mask-memory-snapshot` were then applied to full work-RAM snapshots from
center/left forks of the same state. The frame-600 differential contains 118
changed cells; one is already explained and 117 remain as 39 literal residue
ranges. The strongest cluster is the known `0xFFDD86–0xFFDD91`
object/animation record, so no new craft-status semantic is promoted. See
`reference/experiments/stunrun/saved-state-ram-mask-differential.metadata.json`.

### M3 completion evidence

Two fresh C-emitter/MAME runs reproduce the fixed `init-state` image
(`ddbe84919c7120f970fc5e635979ec5b4fd1fd55da8a991f9a5076e9f9668fe1`), install
at frame 1, settle at frame 61 with ADSP `PC=0x0050`, report zero program
readback mismatches, and reproduce `DM($0955)=0x1242`, `DM($0956)=0x124E`,
`DM($0959)=0x7FFF`, and `DM($095A)=0xFFFF`. The two result JSON hashes are
identical. The synchronized comparison in
`reference/experiments/stunrun/m3-adsp-c-init-state-frame61.metadata.json`
matches main CPU, GSP, and sound CPU registers against the canonical boundary;
the loaded ADSP program/PC difference is intentional and documented.

## Historical M3 objective

Use the M2 image loader and the frozen M1 contract to move the replacement slice
to the earliest reproducible initialization boundary:

1. Identify the earliest bounded point at which the replacement ADSP image can be installed without relying on the original upload.
2. Preserve the verified ADSP reset/control sequence and establish a stable replacement execution loop.
3. Compare the resulting machine boundary against the canonical title-path checkpoint.
4. Preserve the M1 selectors, provenance, and unresolved questions; do not reinterpret the oracle to fit the replacement.

The first useful output is not decompiled C. It is a repeatable laboratory plus a box of provenance-labeled puzzle pieces.

### M3 progress

The replacement loader now honors the MAME-confirmed ADSP reset entry at
program word `0x0004` (the prior `0x0000` probe executed the reset-vector
`RTI` word). A five-word NOP control advances repeatably from `0x0004` to
`0x0E30`; the ROM-derived frame-600 image is installed with complete read-back
verification and advances repeatably to `0x004F` after the bounded settle
interval. These results establish image transport and a stable execution
loop, but not behavioral equivalence or a source-produced initialization slice.
The captured image has since been replaced with a minimal independently
encoded ADSP initialization routine. The first source-defined
prefix now preserves the observed calls to `0x0780` and `0x0834`, returns from
both stubs, and settles at PC `0x0004` after two fresh MAME runs. A second
literal setup slice reproduces the observed PM/DM initialization range and
the `DM($0955–$095A)` landmarks with zero image readback mismatches. Two fresh
snapshot runs now
bound the original upload precisely: program RAM is empty through frame 407,
begins populating at frame 408, and is complete by frame 411. See
`reference/experiments/stunrun/m3-adsp-upload-boundary.metadata.json`,
`reference/experiments/stunrun/m3-adsp-init-prefix.metadata.json`, and
`reference/experiments/stunrun/m3-adsp-init-state.metadata.json`.

The source-defined init/control/upload slices are now also emitted by a
dependency-free C99 implementation shared by the reproduction and native
targets. A deterministic native shell parses the common experiment schema,
dispatches replay events, and checks the verified title-path contracts; its
12 CTest targets and 55 ROM-free repository tests pass. This is integration
scaffolding and a source-emission proof. The C-produced `init-state` image has
also passed the existing MAME replacement loader with zero readback
mismatches, reset entry `0x0004`, bounded PC advance to `0x0050`, and all four
observed DM landmarks; the runtime result is recorded in
`reference/experiments/stunrun/m3-adsp-c-init-state-runtime.metadata.json`.
At frame 3, the replacement and original match in GSP, sound CPU, main SR/SP,
and the original ADSP reset PC before the replacement image runs; the main PC
has a two-byte observed divergence and the loaded ADSP program necessarily
differs from the original empty frame-3 program. This still does not establish
full behavioral equivalence to the original initialization checkpoint.
The synchronized frame-61 comparison now has a dedicated provenance record in
`reference/experiments/stunrun/m3-adsp-c-init-state-frame61.metadata.json`:
main CPU, GSP, and sound CPU registers match exactly while the intentional
ADSP replacement difference is isolated.

The first evidence-backed main-CPU state slices are also present: the live
score/object-hit accumulator and the persistent ten-entry high-score table
decoder. Their C self-checks use the frozen score and NVRAM findings without
claiming unresolved score promotion, name padding, or counter-wrap semantics.
The ADSP-buffer to GSP-FIFO block framing is now also represented as a literal
C slice: the observed length/terminator formula, transfer shape, and 1200-frame
summary are covered without assigning payload semantics. With these slices
integrated, the native build passes 12 CTest targets and the ROM-free
repository suite passes 55 tests.

## Working processor inventory

Current Step 0 mining indicates the active programmable processors are:

- Motorola 68010 main CPU
- TMS34010 GSP
- ADSP-2100 geometry/math DSP
- 6502-class JSA II sound CPU

Other important modeled devices include:

- TMS34012 pixel processor / expander
- YM2151
- OKI6295

The previously assumed second TMS34010 MSP is currently **excluded** because the mined target config calls `multisync_nomsp(config)`. Runtime inventory from the pinned build must confirm this.

## Step 0 puzzle pieces already identified

From the worked driver-mining example:

```text
0x60c000              main digital input landmark
0x800000–0x807fff     68010-visible ADSP program window
0x808000–0x80bfff     68010-visible ADSP data window
0xb80000 family       documented ADC/input landmarks
```

These are hardware-interface labels, not yet gameplay semantics.

## M1 handoff

### Investigator

Use the committed M0 artifacts as inputs, then establish:

- exact target shortname/revision;
- exact MAME build/version/commit;
- ROM manifest;
- runtime CPU/device inventory;
- static listings;
- XREFs to driver-mined address landmarks;
- bounded boot/title experiment;
- `analysis/interconnect.md` observations;
- first experiments from `analysis/driver-mining/stunrun.md`.

### Implementer

Avoid broad game implementation until M1 identifies a verified slice and its machine contract.

Prove minimal replacement-code generation independently for the active targets:

- 68010
- TMS34010 GSP
- ADSP-2100
- 6502

Do not build an MSP toolchain unless pinned runtime inventory demonstrates an MSP is active.

### Verifier

Extend the M0 harness with targeted memory/interrupt telemetry and the normalized checkpoint schema from IRQ-0003.

The first oracle pipeline should support deterministic launch/input, bounded waits, checkpoint capture, deliberate failure artifacts, bounded tracing, and original-code LCOV generation.

## Current blockers

- **MD-14 (JR/JA executor form mismatch) is withdrawn.** Re-reading the form
  helpers showed the earlier report conflated `j_xx_0` and `j_xx_8`: the former
  uses the word-relative extension when its low nibble is zero; the latter uses
  the long absolute target. Both match the ISA and MAME disassembler. The
  bounded fetch trace did find two fetched words of each form, but neither is
  mis-executed. Findings §88 and `BUGS.md` MD-14 retain the refutation; do not
  treat this as a blocker or report it upstream. The separate odd condition-code
  decoder coverage question remains unverified and is not the same claim.
- **`tools/disasm-gsp` still has `WORD_STRIDE = 16`, wrong for the dense 512 KiB
  share** (`stun-runner-a9q`, P1). §84 fixed the share *size* and missed the
  *stride*, so the tool reads one word in eight while every test stayed green —
  `tests/test_disasm_gsp.py` encodes the same assumption in its `share()`
  helper. Sanity check that would have caught it: a share sweep's nonzero-word
  count must equal the `nonzero_words` in the capture's `result.json`.
- **Physical GSP VRAM is now established as 512 KiB.** SP-338 Page 10 says
  `VRAM Array (16) 64K x 4`; MAME's direct `m_gsp_vram` pointer dump reports
  524,288 bytes / 262,144 words. TMS34010 addresses are bit addresses
  (`addrshift=3`): HSTADR `$FF896040` maps to physical share offset `$12C08`.
  The corrected `tools/mame-gsp-vram-dump` reads one word per `0x10` address
  units and was compared byte-for-byte with the direct MAME share hook. See
  findings §34 and `reference/experiments/stunrun/gsp-share-reader-crosscheck-service/`.
- **The native Musashi host-write model now uses the physical 512 KiB share** and
  packed 16-bit HSTDAT stores. The 128 KiB ROM-bank interleave was also fixed;
  frame 48 now loads 394 text descriptors, matches selector `$47`, and stores
  its first text cell byte-for-byte at physical `$12C08`. The old 4 MiB/16-byte
  claims, Lua zero-read conclusion, and disabled text/upload tests were our
  mistakes and are retracted in findings §34.
- **Palette bank decode corrected in the shared GSP board model.** MAME selects
  palette banks from control-high write-address bits, not the stored control
  word. Regression coverage passes; frame-600 raw palette-plane agreement rose
  to 75–77%, but exact color/frame parity remains open (findings §69).
- **Frame-48 service-text comparison is not yet a stable acceptance check.**
  The 68010 is still in the `$25E8` character loop at native frame 48 (19/30
  characters emitted); its frame-49 internal framebuffer, cropped to MAME's
  512×229 visible area, is pixel-identical to one MAME frame-48 prompt capture.
  The diagnostic `STUNRUN_FORCE_FRAMEBUF` row-to-DPYADR encoding is fixed and
  regression-tested (it had put the physical row number directly into DPYADR
  instead of DPYADR[15:4]). However, five fresh `--input service` oracle runs
  alternate between prompt and black at frame 48, so do not infer a glyph-byte-
  order bug or claim stable parity from the matching run. A deterministic
  startup service oracle is still needed (findings §70).
- **CLOSED: the Verilated `native/gsp_rtl/gsp_board.c` model now consumes the
  512 KiB bit-addressed backing** (gsvp.3, 2026-10-14). It was the last model
  carrying the unit error; see the gsvp.3 entry above and findings section 35
  for the conversion, the six regions fixed with it, and the three levels it is
  checked at. The remaining gap is not the model: it is that the check chain
  runs through a differential between two of our own models with one
  MAME-anchored cell, and gsvp.4 — the core executing the uploaded display
  list — is what actually produces pixels.
- **The frame-600 VRAM fixture is now the physical 512 KiB share.** The 4 MiB
  program-address-space sample has been removed from
  `fixtures/oracle/frame_600/frame_600_gsp_vram.bin` and replaced with MAME's
  direct `m_gsp_vram` pointer dump (524,288 bytes, sha256 `04cebed17bccd9c2...`,
  5058 nonzero). `tools/verify-oracle-fixture` now pins **512 KiB**, not 4 MiB:
  the program space is `datawidth 16, addrshift 3`, so one address unit is one
  bit and a 4 MiB address span is 512 KiB of physical bytes. Its negative
  controls cover the 1 MiB main-RAM file, a 4 MiB sample, a tampered blob, an
  all-zero capture, and a missing metadata record. Provenance and the
  bit-to-byte conversion are in `fixtures/oracle/frame_600/metadata.json`;
  source capture in
  `reference/experiments/stunrun/gsp-direct-share-frame600-service/`. The
  other 4 MiB captures under `reference/` stay as historical address-space
  traces and are still not VRAM goldens.
- M0 is complete; the installed package's unavailable upstream source commit remains documented in `analysis/m0-audit.md`.
- Installed MAME reports `0.289 (mame0289-dirty)` and exposes no source commit; its binary hash is recorded in `mame/system-baseline.json`.
- Static-listing generation recipe works for all four active programmable processors; generated listings remain local and are hash-recorded in `reference/listings/stunrun.metadata.json`.
- The native smoke coverage pipeline now emits standard LCOV through GCC/gcov; npm `lcov` is available, while `genhtml` is unavailable, so HTML rendering is not part of this baseline.
- Deterministic replay schema, bounded frame selector, and machine-readable failure artifacts exist; semantic title/gameplay selectors remain future work.
- M1 has established the active ADSP program writer (`0x02D35C`), its
  RAM-resident 17-record/2,728-word upload stream, the ADSP-buffer→GSP FIFO
  block contract, a frame-363 title-path FIFO submission landmark, source-mapped
  control/reset/bank handlers, and the title-path JSA command-byte pairing.
  Remaining questions are upload-stream semantics, post-gameplay traffic, the
  first input-dependent GSP submission, and a synchronized semantic gameplay
  selector. A bounded AD Stick X/Button 1 replay now produces a repeatable
  input-dependent rendered delta at frame 1800, a normalized checkpoint, a
  late GSP differential, and a first main-CPU input-path divergence. A later
  default-DIP coin/start schedule now reaches a repeatable roadway/control
  boundary with a second normalized checkpoint, but semantic selector meaning
  is still unproven. The late-applied SW1-off experiment is a repeatable loading / blank
  negative control; applying the same DIP bank before reset now yields a
  repeatable exact-frame rendered-scene checkpoint, but the identical no-input
  render means it is not yet a gameplay selector. A bounded post-start GSP trace
  now identifies two additional drive-only writers to `@F4000000` (`0xFFF45330`
  and `0xFFF479E0`), narrowing the renderer path while preserving the semantic
  selector caveat.

The M5 track-storage slice is now evidence-backed through the native boundary:
observed 384-word ROM slots share 286 words, settled RAM snapshots match the
selected slot through word 359, and the remaining runtime update is explained
by raw-base/scaled-twin copy loops plus full-buffer byte subtract/add passes.
The literal copy/arithmetic behavior is covered by
`road-buffer-math-slice-c`; the native shell still consumes a pre-transform
fixture and does not claim a native gameplay producer. Human-facing detail is
in `analysis/track-storage.md`.

## Completed baseline slices

- Canonical working set: `stunrun`, S.T.U.N. Runner (rev 6), 25 ROM files; MAME `-verifyroms` passes.
- MAME-derived manifest: `roms/stunrun.manifest.json`.
- Repeatable runtime inventory: `tools/mame-inventory`.
- Runtime inventory evidence: `reference/inventory/stunrun.json`.
- Machine-map reconciliation: `reference/inventory/stunrun.reconciliation.json` (all four active processors present; no unexpected programmable device).
- Reproducible MAME launchers use `-noreadconfig -nowriteconfig`, an isolated
  config directory, and `-nonvram_save`; pinned local NVRAM hashes are recorded
  in `mame/system-baseline.json`, so persisted user DIP/input state cannot
  contaminate evidence.
- Deterministic replay: `tools/mame-replay` runs `experiments/stunrun/boot_to_title.json` and `coin_start.json`; repeated boot runs produced identical screenshot hashes.
- Bounded debugger traces: `tools/mame-trace` and `mame/lua/trace.lua`; six-frame trace metadata is under `reference/traces/stunrun/`.
- Trace-to-LCOV prototype: `tools/trace-to-lcov`; first standard `.info` report is hash-recorded under `reference/coverage/original/`.
- Minimal code-emission proof: `tools/emit-proof`; all four active processors decode emitted NOP fixtures through MAME.
- M2 first image slice: `tools/build-replacement-image` emits a fixed-origin ADSP
  NOP image, and `tools/mame-replacement-image` installs it at frame 140 after
  the original upload, releases the MAME-modeled ADSP reset/`/BR`/`/HALT`
  controls, and observes a repeatable PC advance to `0x0E2C` at frame 142.
  Evidence: `reference/experiments/stunrun/m2-adsp-replacement-image.metadata.json`.
- M3 investigation: `tools/mame-adsp-program-dump` captures a repeatable full
  ADSP program image at frame 600 (2,718 nonzero words). Fresh captures on both
  `md12-fixed` and `stock-behaviour` are byte-identical (image sha256
  `821f3478ef4de4deb67c2b492fc726aaa73351d240b037e46b9f0d23014cecc4`).
  A separate local 4,356-word / PC `0x060E` image exists (sha256
  `b8c2d2594d609a110a26a47aa2cad11f8db3783643a5e3fb49b1897660051c02`), but
  its MAME binary and effective startup state were not recorded. The differing
  capture condition remains unresolved; neither image is retired (findings
  §85.3, bead `stun-runner-w55`).
  The 2,718-word image runs through the M2 loader and reaches PC `0x275D` at
  frame 142. Frame 136 is
  retained as an all-zero negative boundary. This is reference-image transport,
  not yet reconstructed ADSP behavior. An immediate frame-1 installation also
  reaches PC `0x27A2` at frame 142, so the next replacement task is isolating
  the original upload/control path rather than proving image placement again.

## Last verified checkpoint

`title-attract`, captured at frame 600 / 9.96648 emulated seconds. Numeric state is recorded in `reference/checkpoints/title/state.json`; the screenshot remains local and is identified by hash in `reference/checkpoints/title/metadata.json`.
