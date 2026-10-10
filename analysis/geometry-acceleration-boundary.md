# Geometry acceleration boundary

## Current working model

The target is an MC68010 + ADSP-2100 + TMS34010 system. The working model is:

```text
MC68010: game state and submission protocol
ADSP-2100: object-ROM/serial input and numeric geometry work (candidate)
TMS34010: command consumption and rasterization
```

This is a hypothesis, not a decoded contract. The MAME driver comment gives
the ADSP polygon/lighting/slope responsibilities, while the project trace
evidence establishes only transport edges and not field semantics.

## What the MAME source adds

The pinned driver is materially useful here, even though it cannot provide
game-level meanings by itself:

- `harddriv.cpp:1859-1866` builds S.T.U.N. Runner as `multisync_nomsp` plus
  the ADSP board; the optional MSP is not part of this target.
- `harddriv.cpp:1483-1492` configures the TMS34010 GSP at 5 MHz with
  `set_halt_on_reset(true)`.
- `harddriv.cpp:570-580` maps GSP program space `$FF800000-$FFFFFFFF` as
  shared RAM. The STUN Runner ROM definition has no separate GSP program ROM
  region; its static executable regions are the MC68010 ROM and the ADSP
  object ROM (`harddriv.cpp:2691-2715`). This suggests GSP code is loaded or
  initialized into its RAM address space rather than being a missing ROM set.
- `harddriv_m.cpp:492-495` releases GSP reset when the MC68010 writes the
  `/GSPRES` function in `$604000-$607FFF`. This is the next decisive trace
  point.
- `harddriv.cpp:5082-5097` installs STUN Runner GSP speedups and identifies
  `$FFF41070` as a GSP speedup PC. That is a better runtime landmark than
  assuming `$FFF48C20` is reached in every boot path.

Therefore the next implementation target is not “write a TMS34010 from
scratch.” It is to reproduce the **load-and-release contract**: capture the
MC68010 writes that populate GSP RAM, capture the `$604000-$607FFF` reset
write, then observe the loaded GSP program. Only after that should we decide
whether the OpenGL seam replaces the GSP parser, rasterizer, or both.

## Existing evidence to preserve

- `analysis/interconnect.md:43-58` observes MC68010 ADSP-buffer blocks copied
  to the GSP FIFO at `$C0000C`, and active ADSP `SIMBUF`, `SIMCLK`, `SOMLATCH`,
  `GINT`, and `MP` special I/O.
- `analysis/interconnect.md:76-85` observes 106 length-prefixed ADSP-buffer
  blocks, each terminated by `$FFFF`, with exactly the declared count of FIFO
  writes. This is the strongest current MC68010→ADSP-buffer→GSP boundary.
- `analysis/interconnect.md:119-133` records the ADSP program upload and its
  ROM-resident source stream.
- `analysis/display-list-callers.md` inventories direct `$225E4` callers. That
  interpreter is not yet proven to be the 3D path; its streams may be text,
  setup commands, or other display-list assets.

## Bounded harness probe

The existing scripted `tools/mame-gsp-boundary` and
`tools/mame-display-list-trace` were run through the pinned MAME executable
`/mnt/build/mame-ref/mame` using ROMPATH `roms`, with the repository harness
process wrapper and bounded timeouts. The 600-frame no-input boundary probe
and 1200-frame drive probe both completed with return code 0 but recorded no
FIFO writes; the corresponding display-list probes recorded zero `$225E4`
calls. Results are under `/tmp/opencode/geometry-boundary-600`,
`/tmp/opencode/geometry-boundary-drive-1200`,
`/tmp/opencode/geometry-dl-600`, and
`/tmp/opencode/geometry-dl-drive-1200`.

This contradicts earlier captures that observed post-initialization activity
around frames 122–132. It is an instrumentation/state-selection discrepancy,
not evidence that the game has no geometry submission. The result is therefore
`UNKNOWN`; do not use these empty captures to choose an OpenGL replacement.
The next probe must reconcile the harness selector/input/start condition with
the earlier canonical trace before semantic promotion.

The follow-up debugger trace exposed an instrumentation defect: the current
`tools/mame-trace` invocation passes `-debugger none` while asking the Lua
script to issue debugger `trace` commands. That produces a clean bounded run
but an empty trace. A trial with `-debugger console` blocked in the interactive
debugger and was terminated; no debugger mode is being promoted until the
harness has a non-interactive bounded configuration. This is a harness issue,
not game evidence.

The corrected boundary selector was also exercised with `input=sw_off_none`.
That produced the expected reset-time host-port fill (`$C0000C`, PC `$54AC`)
and command writes at frame 46 in the 180-frame run, while the 600-frame run
recorded no later FIFO activity. GSP end-of-frame PC samples were `$000000`
at frames 1, 60, 120, 122, 132, 136, 180, and 600. These are useful selector
diagnostics, but they do not prove that the GSP is semantically idle: the
driver configures it halt-on-reset and the sampled state may be post-halt.
The captures are `/tmp/opencode/geometry-boundary-sw-180` and
`/tmp/opencode/geometry-boundary-sw-600`.

A direct main-CPU write-window trace answered the reset question. The
600-frame no-input run captured eight writes in `$604000-$607FFF`, at frames
170, 305, 439, and 573, with addresses `$604000`, `$604002`, `$604010`, and
`$604012`, from PCs `$5DF0`, `$5DF6`, `$5DFE`, and `$5E04`. Under the MAME
handler's `offset &= 7` decode, these exercise functions 0 and 1 (CR2/CR1),
not function 6 (`/GSPRES`). No GSP reset-release write was observed. The
artifact is `/tmp/opencode/gsp-reset-window-600/result.json`; its canonical
log SHA-256 is
`18f6de783fc24c93c936a92ea8727050cfd5f500af0628b85f1b5c47a68c8521`.
This explains the sampled halted GSP and is the current highest-value lead:
the normal no-input path is not releasing the GSP in this run.

The harness was then extended with the reproducible `service_f2` input mode
(press `:mainpcb:IN0` / `Service Mode` at frame 60, release at frame 120).
That path does release GSP reset. The trace
`/tmp/opencode/gsp-reset-service-f2-600/result.json` records `$60400C` and
`$60401C` writes at frame 66 from PCs `$003FC` and `$00406`, the boot entry
already represented by `native/gsp_boot.c`. Later service diagnostics repeat
the same release sequence at frame 153 and 454, and exercise additional
control functions `$604008`, `$60400A`, `$604018`, and `$60401A` at frame 404.
The trace contains 16 writes total and has canonical log SHA-256
`743adbb2c254c229362333fc89e9a5337eae99d1480e3a2da14630986235bb41`.

The corresponding GSP state capture
`/tmp/opencode/gsp-service-state/result.json` still reports PC `$000000` and
ST `$00000010` at frames 66, 153, 404, and 600. This means the reset-release
write is now proven, but it does **not** yet prove sustained GSP execution;
the service path appears to release and then halt or reset the GSP. The next
experiment is to capture the GSP RAM/load writes and the first nonzero GSP PC
immediately after frame 66, rather than relying only on end-of-frame samples.

An early-frame service capture narrowed that further: frames 60–80 and 120/153
all report GSP PC `$000000`, ST `$00000010`; frame 67 briefly changes the
display address to zero, but does not expose a nonzero GSP PC. The capture
result is `/tmp/opencode/gsp-service-early/result.json`, with canonical log
SHA-256 `749d98aabc5fa643762860bd0c6ee22e1ef71e5942058bad70e48c007f929a77`.
Thus the `/GSPRES` write is a real control-path landmark, but the current
service run still does not provide sustained TMS34010 execution evidence.

The post-release host trace also confirms that the assembly-to-C conversion
is being used as the evidence model rather than replaced by guesses. During
frames 60–70, the MC68010 executes the already converted `$067A` routine
(`native/gsp_boot.c:445-466`): it selects GSP main VRAM `$FF800000` through
`$C00002`, then emits the zero fill through `$C0000C`. The trace is dominated
by PC `$069A`, the loop body, and contains 19,951 data writes before the
bounded 20,000-event cap. Separate host-port writes at `$C00002/$C00004` show
the address latch being programmed. This is initialization/VRAM clear, not
polygon rasterisation, but it verifies that the decoded routine can be
attributed in the original path.

## Acceleration replacement boundary (not yet frozen)

The likely replacement seam is after decoding the ADSP-buffer/GSP-FIFO record
format and before TMS34010 rasterization. A software/OpenGL renderer should
consume a validated intermediate record stream, not bypass the ADSP or assume
that `$225E4` output is geometry. Required evidence before freezing that seam:

1. correlate one FIFO block with its ADSP serial/object-ROM source;
2. identify the GSP consumer and record fields;
3. establish where transform, depth ordering, lighting, and clipping occur;
4. replay one oracle block through a software decoder and compare visible output.

## 2026-10-10 display-list capture check

The existing `tools/mame-display-list-trace` harness was run against the
pinned rev-6 set with `--input drive`, frames 850–1200, and a 2,048-byte raw
window per candidate call. It exited successfully but captured zero calls and
zero FIFO words. The canonical MAME log SHA-256 is
`28993b8f23594bffddee7c7d8cdf6730ea64a3f930497b2170cc11ed146676c6`.

This is not evidence that the display-list path is absent: the same address
range executes in the native late-drive model, while this MAME selector did
not reach it. Keep the result as an instrumentation/input-path limitation and
do not promote it into a geometry conclusion. The GL seam therefore remains
blocked on a correlated oracle FIFO capture or a validated GSP consumer
record format.
