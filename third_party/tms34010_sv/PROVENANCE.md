# Vendored TMS34010 SystemVerilog core

This subtree is third-party code, vendored verbatim. It is **not** project
source and must not be edited to make our implementation pass.

## Upstream

| Field | Value |
|---|---|
| Repository | `https://github.com/birdybro/TMS34010_sv` |
| Commit | `94a258e80a07ceb4303ce0b99818df832e96007f` |
| Commit date | 2026-07-30 06:58:16 -0600 |
| Commit author | Kevin Coleman |
| Commit subject | `Record Task 0174 completion commit` |
| Licence | MIT — see `LICENSE`, copied verbatim |

## What is vendored

`rtl/` in full (33 `.sv` modules, 13,937 lines) plus `LICENSE`.
`manifest.json` records a SHA-256 for every vendored file so drift is
detectable; `tools/check-gsp-rtl` verifies it.

## What is deliberately NOT vendored

| Excluded | Why |
|---|---|
| `sim/tb/*` (100+ SystemVerilog testbenches) | Upstream regression infrastructure, not needed to build or drive the core. Re-running them belongs to upstream, not to us. |
| `sim/vectors/*` | Upstream's accepted MAME/TI result corpora. Useful as upstream evidence, but it is upstream's claim about *their* core, not a check of our integration. |
| `sim/models/*` | Behavioural memory models written for their testbenches. We implement the STUN Runner memory map ourselves in `native/gsp_rtl/`. |
| `third_party/TMS34010_Info` | An upstream git submodule carrying TI documentation. **Not required to build**: lint and the 137-case replay corpus both pass with it absent. Verified again on the vendored copy. |
| `fpga/` directory contents | Vendored for tree fidelity, but **not instantiated** by our host harness. They are Cyclone V board wrappers (PLLs, pin drivers, the FPGA top level). Our harness instantiates `tms34010_system`. |

## Verification actually performed

Run in this repository against **this vendored copy**, not against the
upstream checkout. Tool: Verilator 5.052 (`2026-09-05 rev v5.052`).

1. **Lint clean.** `verilator --lint-only -Wno-MULTITOP <rtl in package-first
   order>` exits 0 and reports 34 modules. This is upstream's own `lint.sh`
   invocation; note that adding `-Wall` is *stricter than upstream* and fails
   with 38 warnings (unused parameters such as `DPYTAP_HI`), so `-Wall` is not
   the bar here.

2. **Upstream replay corpus.** Before vendoring, in the upstream checkout,
   `scripts/sim.sh tb_mame_graphics_replay` reported
   `TEST_RESULT: PASS (137 checked-in pinned-MAME graphics replays)`. That
   corpus exercises **graphics instruction semantics at core level**. It is
   upstream's evidence about the core; it is not evidence that our
   integration is correct.

3. **Throughput, measured here.** A 20M-cycle spike measured ~1.73M
   cycles/second single-threaded. `verilator --threads 4` measured 11.0 s
   versus 11.57 s single-threaded on the same spike — about 5%, within noise.
   The TMS34010 is one sequential state machine and offers no parallelism to
   extract, so threading is not a lever.

   At STUN Runner's 50 MHz GSP clock, 600 frames is 498M cycles: roughly
   5 minutes at the measured best case and realistically 10–25 minutes with
   real code and a real memory map. **Offline oracle verification: fine.
   Interactive: not viable.**

## Why this core

STUN Runner's GSP has **no ROM region at all**. In
`src/mame/atari/harddriv.cpp` the `mainpcb:user1` region is the *ADSP* object
ROM; there is no GSP ROM. The 68010 uploads the GSP program into VRAM at boot.

That is usually disqualifying for a CPU core, but not here:
`rtl/tms34010_pkg.sv:110` defines `RESET_PC = '0` as an **overridable
parameter**, and `rtl/core/tms34010_pc.sv:61` resets the PC to it and then
fetches from the external bus. The core therefore boots ROM-less from a VRAM
image the CPU supplies, which is exactly this board's arrangement.

## Scope boundary

The core's job is narrow: **execute the GSP program so VRAM, palette and
display registers become correct.** Scanout stays in the existing native
rasteriser `stunrun_gsp_video_render`, which already reproduces the MAME
frame byte-for-byte from correct state. The core's video path is not required
for our acceptance.

## Relationship to the toolkit-free seam

Verilator-generated C++ is ordinary toolkit-free C++. This is a CPU core, in
the same category as the Musashi 68010 core already linked into the native
target. It sits **below** the seam and holds no game state and no policy.

## Removal boundary

Nothing here is required for the 50 native CTests to pass. If the GSP RTL
provider is retired or becomes unmaintained, delete this whole subtree plus
`native/gsp_rtl/`; the rest of the project is unaffected. This is not the
temporary vendored-source case described for `stun-runner-a10.2` in
`analysis/dual-gsp-comparison-plan.md` — that one was noncommercial research
code, whereas this is permissively licensed and may ship.