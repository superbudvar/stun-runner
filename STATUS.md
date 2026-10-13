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

Reproduction target: **initial ADSP replacement slice only; gameplay reproduction not started**

Native target: **deterministic replay/checkpoint scaffolding and evidence-backed slices; game implementation not started**

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

### Parallel thread — GSP execution (`stun-runner-gsvp`)

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
- **`gsvp.3` host harness — open.** `native/gsp_rtl/gsp_board.c` is the
  toolkit-free C99 board model (memory map, host port, reset/clock), testable
  without Verilator. The Verilated driver is wired into the shell frame loop.
- **`gsvp.4` first milestone — open, and its headline number is weaker than it
  looks.** Feeding the real 68010 upload (`gsp_bus_events.bin`, 901,354 events)
  through the board model gives byte-identical VRAM — but read the 4 MiB caveat
  above before quoting it. What is *not* yet shown is the TMS34010 core
  executing the uploaded display list. Scanout deliberately stays in the native
  rasteriser, which already reproduces the MAME frame byte-for-byte from
  correct VRAM, palette and registers.
- **`gsvp.5` upload boundary — done (`stun-runner-8p4.2`).** The GSP program is
  now pinned to an upload and a release rather than inferred from a frame-600
  dump. `tools/capture-gsp-program-upload` records the ordered 68010 host-port
  stream and images the share either side of the boundary. Two independent runs
  agree on everything, including byte-identical VRAM images.
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
- **Still open from that work:** the pinned oracle binary is not stock MAME.
  `/mnt/build/mame-ref` carries local observation-only hooks in
  `harddriv_m.cpp`, `harddriv_v.cpp` and `tms34010.cpp`, and this repository does
  not yet record them. See the open question below.

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

- **The GSP VRAM share is 4 MiB and a host data write FILLS the 16-byte cell.
  Both FIXED; host-write parity is now MEASURED against MAME.**
  - *Size.* `GSP_BOARD_VRAM_BYTES` (`native/gsp_rtl/gsp_board.h:29`) and
    `MUSASHI_GSP_VRAM_BYTES` (`native/musashi_cpu.h:67`) are both `0x400000`
    since 2026-10-12 (`cf8587e`, `bd5b873`), masking with `0x3FFFFF`.
    `tools/mame-gsp-mirror-probe`: the old mask sent 745,384 of 843,688
    captured `0xFFxxxxxx` HSTDAT writes (88.4%) to a different cell. Section 19.
  - *Store.* A host `HSTDAT` stores **16 bytes**, the low byte eight times then
    the high byte eight times -- not a packed word. Measured at frame 390 with
    the GSP in reset (`gsp PC=0`), through the real host port:
    `HSTADRL=0xFF801000, HSTDAT=0x1234` reads back
    `34 34 34 34 34 34 34 34 12 12 12 12 12 12 12 12`. `fcf1eab` had
    "corrected" `native/musashi_cpu.c` to packed on a misreading of
    `m_program.write_word`; commit `d91ae6a` propagated that misreading into the
    board model and has been **reverted**. Section 27, `BUGS.md` SR-20.
  - **Section 15.4's byte-identical VRAM match (`0 / 524,288`) is RETRACTED.**
    Both models carried the same wrong constant *and* the same wrong store, so
    they agreed by construction. It proved host-write dispatch, nothing else.
  - **Host-write parity IS now measured**, on the frame-390 window (GSP in
    reset, so the share holds only host writes): replaying the captured stream
    gives 70,960 of 4,194,304 bytes differing (98.31%), against 748,098
    (82.16%) for a packed model. The residual is `0xff` fill in the ranges the
    2bpp expander writes, which the arbitration harness does not model.
  - **Not yet established:** neither model is compared against MAME at a frame
    where the GSP has *run*. The frame-600 oracle is 87.5% uniform GSP
    framebuffer data (0x82, 0xb8, 0xaa...) that no host-port replay can produce,
    and the 2bpp path is still unmodelled in the arbitration harness. Section
    18.2 stands and is independent: the capture ends with the GSP halted 99.61%
    of its window.
  - `tools/check-gsp-replay` also never had a pass branch -- it ended in an
    unconditional `return 1`, so it could not report success. Fixed; it returns
    0 on byte-identity. Section 27.5.
  - **Reproducing the arbitration needs a specific window.** MAME's share is
    entirely zero at frame 360 and the GSP is running by frame 420, so "an
    early frame" is not early enough; the window has to be found by checking
    `gsp PC` at the capture point. Frame 390 works.

- **The frame-600 VRAM fixture is REPLACED and now has a checker.** The old file
  was 1048576 bytes, began with the MC68010 reset SP/PC (`ffffc000 00000216`),
  had no provenance, and agreed with a fresh pinned capture on 0.18% of bytes —
  it was main RAM under a VRAM name, and nothing failed because nothing read it.
  It is superseded by the 4194304-byte capture from
  `tools/mame-gsp-vram-dump` (sha256 `35c9262190d8ec85...`), with provenance in
  `fixtures/oracle/frame_600/metadata.json`.
  `tools/verify-oracle-fixture` checks size, the reset-vector tell, sha256
  against the recorded provenance, and the all-zero capture case; five public
  tests pin it, three of them negative controls. Sections 19.5 and 27.
  - The replacement is **not** yet usable as a parity gate. It contains GSP-core
    writes as well as host-port writes, so a whole-array diff against a
    host-port-only model cannot decide anything. See the open item above.
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
  ADSP program image at frame 600 (2,718 nonzero words); the same local image
  runs through the M2 loader and reaches PC `0x275D` at frame 142. Frame 136 is
  retained as an all-zero negative boundary. This is reference-image transport,
  not yet reconstructed ADSP behavior. An immediate frame-1 installation also
  reaches PC `0x27A2` at frame 142, so the next replacement task is isolating
  the original upload/control path rather than proving image placement again.

## Last verified checkpoint

`title-attract`, captured at frame 600 / 9.96648 emulated seconds. Numeric state is recorded in `reference/checkpoints/title/state.json`; the screenshot remains local and is identified by hash in `reference/checkpoints/title/metadata.json`.
