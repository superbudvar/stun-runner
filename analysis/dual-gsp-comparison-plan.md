# Dual GSP native comparison plan

> **Scope update (2026-10-12):** The initial proposal below for comparing a
> transcripted GSP with an embedded MAME-derived GSP was superseded for the
> current renderer slice by the user's direction to compare the CPU GSP
> reference scanout with an independent OpenGL scanout in two windows. The GL
> shader consumes raw VRAM/palette textures and per-line address state; it does
> not consume CPU-rendered RGB. The original dual-GSP proposal remains here as
> historical context, not as the acceptance contract for this scanout slice.

## Accepted intent

The native app is to keep two independent GSP implementations alive in one
process. The same ordered MC68010 host-port writes feed both implementations;
each owns its own GSP state, VRAM, palette and output buffer, and each output is
presented in a separate native window. The original MAME application remains a
separate behavioral oracle and is not one of the two windows. Preserve the
existing transcripted implementation; any MAME-derived source is temporary and
must have a clear removal boundary.

This is not a dual-monitor setup, two instances of the full MAME game, or a
proposal to guess GSP geometry semantics. The driver-mined machine map confirms
the active TMS34010 is `:mainpcb:gsp` (`analysis/driver-mining/stunrun.machine-map.yaml:23-27`).

## Existing boundary and open feasibility question

`native/gsp_display.h` exposes a scanout/rasterization path over caller-owned
VRAM, palettes and display registers; it is not a TMS34010 execution core.
`native/musashi_cpu.c` owns one native GSP memory model and captures effective
HSTDAT words as `{addr,data,pc}` in `musashi_gsp_write_t`. A new raw observer,
`musashi_cpu_set_gsp_host_write_observer`, captures actual MC68010 stores with
absolute address, merged word, lane mask, access width, PC and sequence. Its
synthetic Musashi test passes for word/byte/long stores and verifies a raw
offset beyond the first 16 bytes of MAME's mapped window. The observer preserves
the bus address so each provider can apply its own decode; it does not settle the
MAME-vs-native register-index ambiguity. The actual MAME TMS34010 device may depend on MAME's
emulator framework and cannot be assumed to be a standalone library or to
provide a `display_writer` API. The MAME-core lane must verify the real device
API and dependencies before copying or claiming integration.

## Primary specification and candidate cores

The project already has a substantial working extraction of the TI TMS34010
Graphics System Processor User's Guide: `docs/tms34010-reference.md`. It covers
instruction encodings/semantics, bit-addressed memory, host registers, graphics
operations and display address generation, so it is sufficient to guide a
manual-based implementation rather than treating MAME source as the only
possible source of behavior. It is not a board map: STUN Runner's GSP map,
reset/load sequence, and host-port wiring still come from the driver and
runtime evidence. OCR-sensitive details must stay marked uncertain and be
checked against the original manual pages/MAME; the working reference itself
identifies suspect opcode-summary rows, unreadable DPYCTL.DUDATE values, and
conflicting DPYADR LAD prose (`docs/tms34010-reference.md:21-44`). The source
PDF and OCR text are local at `/tmp/tms34010ug.pdf` and `/tmp/ug.txt`; neither is
committed.

Online candidate, not adopted: `birdybro/TMS34010_sv` at inspected HEAD
`94a258e80a07ceb4303ce0b99818df832e96007f` is a synthesizable SystemVerilog
TMS34010 core with MIT license. Its README and `docs/mame_graphics_reference.md`
describe a large spec-derived regression and an exact pinned-MAME differential.
We independently ran `scripts/lint.sh` and `scripts/sim.sh
tb_mame_graphics_replay` with local Verilator 5.052: lint passed with zero
diagnostics across 34 RTL modules, and all 137 checked-in MAME graphics replays
passed. This verifies the selected offline corpus, not the entire 159-bench
suite or a live current-MAME differential. It is **not MAME source**, and using
it as a provider instead of the requested MAME-derived core would change the
agreed comparison; do not substitute it silently. The local clone is
`/tmp/opencode/tms34010-sv-research`; its README's reported coverage is not yet a
full independent pass. Gentoo `eix`/`equery` found no installed Verilator; the
available `sci-electronics/verilator-5.052-r1::guru` ebuild fails because its
`PYTHON_COMPAT` includes unsupported `python3_15`, while 5.050 is `~amd64`-masked.
At the user's suggestion, official Verilator 5.052 was built and installed
locally (source tag commit
`ea338be98e1e838d3518809ce8899f85a009963c`), without system package or
repository changes.

> **Verilator prefix resolved 2026-10-11.** That install originally lived at
> `/tmp/opencode/verilator-install`, which does not survive. It has moved to
> `$HOME/.local/share/verilator-5.052`, and `native/gsp_rtl/CMakeLists.txt`
> now finds it through `find_program` HINTS, so no environment setup is needed.
> Verified with `VERILATOR`, `VERILATOR_ROOT` unset and a bare
> `PATH=/usr/bin:/bin`: configure, full build and 53/53 CTests all pass.
> This closes the open half of `stun-runner-gsvp.2`; the optional-target
> half was already done.

> **SUPERSEDED 2026-10-11 by explicit user decision.** The paragraph above
> recorded this candidate as "not adopted" and warned against substituting it
> silently, because using it instead of a MAME-derived core changes what the
> dual-window comparison means. The user has now chosen it knowingly, so the
> caveat no longer binds.
>
> Concretely: `stun-runner-a10.2`, the MAME C++ adapter, is **retired rather
> than pursued**. Its blocked conclusion still stands — current MAME's
> `tms34010_device` derives from `cpu_device` + `device_video_interface` and is
> not embeddable as a standalone library — but the lane is no longer the path
> forward. Forward work moved to `stun-runner-gsvp` and its children.
>
> The core is vendored at the pinned commit under `third_party/tms34010_sv/`,
> with `PROVENANCE.md` recording the upstream, the commit, and the verification
> actually performed rather than the verification claimed.
>
> **Why this core fits STUN Runner specifically.** The board's GSP has *no ROM*
> — `mainpcb:user1` is the ADSP object ROM — which is normally disqualifying for
> a CPU core. It is not: `tms34010_pkg.sv:110` makes `RESET_PC` an overridable
> parameter and the PC loads from `RESET_VECTOR_ADDR` (`0xFFFF_FFE0`) through
> the memory bus, inside the mirrored VRAM the 68010 fills. The core therefore
> boots ROM-less, which is exactly this board's arrangement. Verified: it takes
> its level-0 vector from VRAM and reaches a seeded entry in 10 cycles with
> zero unmapped accesses.
>
> **Two claims refuted while integrating it**, recorded in
> `docs/mc68010-boot-findings.md` section 14 rather than here because neither is
> readable off the RTL headers: the core address is a **byte** address (no
> `>> 3`, despite `addr_shift 3` and a PC that advances by 16), and HCS must be
> **asserted** at reset or the core never fetches its vector at all.

The temporary older MAME4ALL TMS34010 source is a more direct MAME-derived
provider candidate: it exposes CPU reset/execute/context and host-port calls,
but still uses the older MAME global memory, timer, CPU-context and screen
interfaces. Its README includes restrictive noncommercial redistribution
terms; any experiment stays temporary and keeps its notices. Current MAME's
`libemu.a` exists, but the current TMS34010 device remains framework-coupled.
The isolated feasibility probe now compiles this old core behind small
MAME-service shims and tests actual bounded TMS34010 instruction execution and
the public host-port API. It asserts register/PC/memory effects and TI-guide
host-address alignment, HSTDATA read/write, and `+16` bit-address increment
(`/tmp/opencode/wt-gsp-mame/native/mame_gsp/legacy_core_probe/README.md`, TI source
`docs/tms34010-reference.md` §§3.1, 2.2 and 5.2). Parent independently
reconfigured/built/tested that probe: CTest `1/1` passed. This is genuine
instruction execution, unlike `native/mame_gsp/host_bus.*`. Its manual-backed
`PIXT A0,*A2` case (`0xF802`, PSIZE=16, A0=`0xBEEF`, A2=`0x1000`) writes exactly
`0xBEEF` at backing byte address `0x200`, leaves adjacent guard word `0xCAFE`
unchanged, and preserves operands; the TI reference maps logical bit address
`0x1000` to byte address `0x200` (`docs/tms34010-reference.md:1311-1317,
1487-1513,724-736`). Parent independently rebuilt it and CTest passed `1/1`.
This proves one linear pixel write, not a rendered frame or STUN Runner scene.
Board memory mapping, ROM loading/reset-vector behavior, MC68010 host-port
integration, interrupts, display timing and actual scanout are still absent.
Do not claim equivalence with the current MAME TMS34010 device merely because
both descend from MAME.

The first useful comparison milestone is therefore deliberately bounded:

1. Capture MC68010 writes over MAME's mapped `$C00000-$C03FFF` host window as
   ordered raw bus events `{address, merged_data, active_lane_mask, width, PC,
   sequence}`. Preserve the original absolute address because the MAME handler
   transforms its offset with `(offset / 2) ^ 1`, while the native model uses a
   different interpretation; traffic must decide the mapping rather than the
   capture layer. A 32-bit CPU store is two ordered 16-bit events; an 8-bit
   store carries its lane mask and merged register value. Deliver each event
    synchronously after the native register-write side effect, without routing
    it back into the MC68010. The existing
    `gsp_host_writes.bin` records only effective HSTDAT destination/data/PC
    words; it is not a complete host-port stream.
    The native bus map now matches MAME's `$C00000-$C03FFF` aperture and does
    not wrap higher addresses through an 8-bit offset: the synthetic test keeps
    `$C00020` as an observed invalid-register write and verifies `$C04000` is
    outside the device map with no host-state effect. After this correction,
    fresh native build succeeded and all 50 CTests passed.
2. Feed the identical event sequence to two separately allocated GSP states.
3. Render the two resulting buffers through two distinct native windows.
4. Compare each state/output against a matching, harness-captured MAME oracle
   artifact. Keep unverified GSP execution/geometry behavior marked UNKNOWN.

### Startup/control event

The host-port observer preserves writes throughout MAME's `$C00000-$C03FFF`
mapped host window. The same ordered event stream now includes MAME's board
NWR function-6 `/GSPRES` control, decoded from the word offset in
`$604000-$607FFF`: bit 3 selects the line value, low three bits select function
6. `native/musashi_cpu.c` tracks reset-release state and emits
`MUSASHI_GSP_BUS_RESET_CONTROL`; its synthetic test asserts that `$60400C`
asserts reset and `$60401C` releases it. The handler is grounded in MAME
`harddriv_m.cpp:466-495` and summarized in
`analysis/geometry-acceleration-boundary.md`. Other NWR functions remain
unimplemented. This only captures the control event; the GSP providers still
need to consume it and actually reset/run their independent cores. Do not claim
game execution merely because host or reset events are being forwarded.

If the TMS34010 core cannot be isolated without importing most of MAME, record
the exact dependency blocker and stop short of a fake adapter. No MAME run or
physical display experiment is part of the implementation lanes.

## Beads and parallel implementation packets

Parent feature: `stun-runner-a10` (dual GSP comparison windows).

| Bead | Scope | Worktree | Runtime session |
|---|---|---|---|
| `stun-runner-a10.2` | Temporary MAME TMS34010 core adapter under `native/mame_gsp/` | `/tmp/opencode/wt-gsp-mame` | `ses_ed7e0336effev7sLcw7iiMxv3Z` |
| `stun-runner-a10.3` | Isolated transcripted GSP state/model under `native/gsp_transcribed/` | `/tmp/opencode/wt-gsp-transcribed` | `ses_ed7e0335cffeA92Lv6kuJqBU4S` |
| `stun-runner-a10.1` | Two-window SDL presenter under `backend/dual_window/` | `/tmp/opencode/wt-gsp-window` | `ses_ed7e032a5ffe1vXkyrfs8gJ1l2` |
| `stun-runner-a10.4` | Parent-owned wiring and MAME differential acceptance after the three lanes | primary worktree | not started |

The three implementation lanes have disjoint source roots and must not change
shared top-level CMake or existing CPU/backend interfaces. Parent integration
will reconcile their APIs and add shared build registration only after review.

Beads initially routed this checkout as a contributor to
`/home/budvar/.beads-planning` while the active project's embedded store was
empty. The current root is now `maintainer`; the 33 issue records from the
preserved outer-workspace export and the five `stun-runner-a10*` lane records
were imported into the current project database without modifying the source
stores. `stun-runner-a10.4` is claimed, and `bd sync --json` completed with
`{"attempts":1,"pulled":true,"pushed":true,"rows_corrected":0,"status":"ok"}`.
The planning repository still retains its source copies. Worktree separation is
used for the parallel source changes, but is not an OS sandbox; the declared
write scopes still require review before integration.

## First live OpenGL preview

The existing native shell was run for 600 frames with the real ROM set,
`STUNRUN_MC68010=1`, `STUNRUN_LIVE_VIDEO=1`, and
`STUNRUN_VIDEO_BACKEND=sdl_opengl`, using `mame_harness/process.py` with a
private Xvfb display. It selected `sdl_opengl_gl21_compat` and reported
`backend=active video=sdl_opengl`; no physical display was used. The captured
renderer framebuffer is
`/tmp/opencode/native-gl-frame600-fixed-xvfb/frame_0600.png` (SHA-256
`345ed1cf24738197a9b1e1cfcf79e6659b19965b0b9ad328edef4a2a6fb0bb71`), and the
full shell log is `/tmp/opencode/native-gl-frame600-fixed-xvfb/native-shell.log`
(SHA-256 `bd7d9b91c7dd899e0616fc5c47bb137742393f9a04d1498c5d4ad727de750c8f`).
The shell reached frame 600 with `install_ready=1`, `upload=complete`, and
software frame hash `0x7D611DC5`; the image contains sparse horizontal lines,
not a reconstructed track. This confirms the existing indexed/live GSP share
can be presented through the OpenGL backend, but the frame PNG is the renderer's
CPU framebuffer (not GPU readback), and this is not dual-provider or MAME frame
equivalence. The TMS34010 GSP still does not execute the uploaded display list.
