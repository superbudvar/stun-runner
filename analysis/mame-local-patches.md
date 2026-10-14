# MAME local modifications

The pinned oracle at `/mnt/build/mame-ref/mame` is **not stock MAME**. It carries
seven observation hooks across four files, plus one behavioural fix, none of
which this repository records in machine-readable form. This file is that
record.

## Why the distinction matters

`AGENTS.md` treats the game under pinned MAME as the behavioural oracle. It is
worth being precise about which binary that means, because there are now two:

| | path | sha256 | role |
|---|---|---|---|
| **Oracle** | `/mnt/build/mame-oracle/mame` | `75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06` | frozen. Never rebuilt. Measurements recorded in `reference/experiments/` were taken against this. |
| **Observer** | `/mnt/build/mame-ref/mame` | rebuilds | `stunrun-observe` branch. Carries the same hooks plus `STUNRUN-FETCH`. Used only to produce new observations. |

The distinction is not cosmetic. Every hook below is env-gated and disabled by
default, but *disabled* is a behavioural claim that has to be tested rather than
assumed — see "Verifying the observer matches the oracle".

## The seven observation hooks

All of these are observation-only: each reads state and appends to a file or
stderr, and none alters emulation. Every one is gated on an environment variable
and is a no-op when it is unset.

### 1. `STUNRUN-DEBUG` — GSP register dump

`src/devices/cpu/tms34010/tms34010.cpp`, in `tms340x0_device::execute_run()`.

Dumps `DPYADR DPYSTRT DPYTAP DPYCTL HEBLNK HSBLNK VCOUNT HSTCTL` and the GSP PC
on frames 1–3 and every 600th thereafter, to stderr.

**Why it exists:** the display register file is host-side (`m_regs[]`), so Lua
cannot read it (`BUGS.md` MD-04); and with `-video none` MAME never calls the
scanline callback, so the `harddriv_v.cpp` dump cannot fire either.
`execute_run()` always runs, so this is the only reliable site.

### 2. `STUNRUN-DUMP` / `STUNRUN-DPY` — VRAM and display-parameter dump

`src/mame/atari/harddriv_v.cpp`. Env `STUNRUN_DUMP_DIR=<path>`.

### 3. `STUNRUN-HOSTPROBE` — host-interface register mapping

`src/mame/atari/harddriv_m.cpp`. Env `STUNRUN_GSP_HOSTPROBE=<path>`.

Records both the raw byte offset and the post-transform register index for every
68010 store into the TMS34010 host interface. It exists because the mapping is
genuinely ambiguous in the source: `hd68k_gsp_io_w` derives the register from
`offset = (offset / 2) ^ 1`, and whether `offset` is a raw byte offset or an
already-halved word index is not settled by reading `hd68k_gsp_io_w` alone. Our
port chose the word-index reading; if that is wrong, every HSTCTL and HSTDAT
access decodes to the wrong register. Letting the ROM's own traffic settle it is
the point of the hook.

### 4. `STUNRUN-OPLOG` — executed-opcode profiler

`src/devices/cpu/tms34010/tms34010.{h,cpp}`, via `stunrun_oplog(uint16_t op)`.

Env `STUNRUN_GSP_OPLOG_OUT`, `_EVERY`, `_FROM`, `_TO`, `_DETAIL`.

Answers "which TMS34010 ops does this game use, and in what proportion" — the
question that decides whether the CPU core can be skipped and only the graphics
ops reimplemented.

### 5. `STUNRUN-FETCH` — instruction-fetch trace *(added 2026-10-14)*

`src/devices/cpu/tms34010/tms34010.{h,cpp}`, via
`stunrun_fetch(uint32_t pc_bits, uint16_t op)`, called from
`tms34010_device::ROPCODE()`.

Env `STUNRUN_GSP_FETCH_OUT`, `_SKIP`, `_MAX`.

Output, one executed instruction per line:
`<index> <time_ticks> <pc_bits> <pc_byteaddr> <opcode>`

**Why it exists:** everything measured so far describes what the 68010 *wrote* and
where the GSP's PC *pointed*. It never established that the GSP fetches
instructions at all. The Lua execute tap is unavailable on this build
(`gsp_execute_tap_status: unavailable`), and
`analysis/gsp-program-upload-boundary.md` shows the share contains no instruction
stream. `ROPCODE()` is the single choke point every executed instruction passes
through, so this is the only place a fetch can be observed from.

**Two things about this device that will mislead anyone reusing the trace:**

- `m_pc` is a **bit** address. `ROPCODE()` advances it by `2 << 3`, and MAME's own
  `34010dsm.cpp` advances its read position by 16 units per word for the same
  reason. Both the bit address and `pc >> 3` are logged because callers want
  different ones.
- The core has no screen and therefore no frame number. `time_ticks` is in
  machine timebase ticks; the harness converts to a frame index using the
  measured frame period.

### 6. `3rdparty/genie/src/host/version.h` — build stamp

The version string. On the frozen oracle it reads `(Local Build)`; on
`stunrun-observe` it reads the commit. Any recorded result carrying a MAME
version string must say which binary produced it.

## Verifying the observer matches the oracle

The oracle's integrity is recorded as a sha256 in this repository. The risk is
not that someone edits the oracle — it is that someone rebuilds it and the
recorded sha silently stops describing the binary that produced the evidence.

`tools/check-mame-oracle` performs that check. It fails loudly if the frozen
binary's hash has moved.

## Machine-readable inventory

`analysis/mame-local-patches.json` lists every hook, the file and function it
lives in, its environment variables, and whether it is observation-only. A test
diffs it against `git diff` in `/mnt/build/mame-ref`, so a hook added to the
source tree without being recorded here fails rather than passing unnoticed.

It holds **two** lists, and the split matters:

- `patches` — observation-only, and the test requires every entry to say so.
- `behaviour_changes` — edits that alter emulation. Each must state
  `observation_only: false`, give a reason, and cite the `BUGS.md` finding it
  answers.

The split is not bookkeeping. `STUNRUN-IOB` and the MD-12 fix touch the same two
files, so a *behavioural* change can ride inside a file already listed for an
unrelated observation hook and never show up in a file-level diff. Filing it as
an observation hook would have been the easy way to make the suite green and the
wrong one; `behaviour_changes` exists so the honest option is also the easy one.

## The one behavioural change: MD-12

`src/devices/cpu/tms34010/34010ops.h` and `tms34010.h`, in
`tms340x0_device::execute_run()`.

`internal_regs_map` maps `map(0xc0000000, 0xc00001ff)`, so the TMS34010's I/O
offsets run to `0x1ff`. The backing store was `uint16_t m_IOregs[64]` and
`IOREG(reg)` was `m_IOregs[reg]` with no mask. S.T.U.N. Runner's GSP program
stores at offsets `0x110`–`0x160` — 208 entries past the end of the array — and
those fetches were traced executing, so this is not a theoretical overrun.

The fix grows the array to `0x200` entries and masks `IOREG()` to `0x1ff`.
Access below `0x40` is byte-for-byte unchanged, and every register MAME's `REG_`
enum and the Atari driver actually use is below `0x40`, so nothing the model
depends on moves.

**This changes emulation behaviour**, which is why it is not in `patches`. Any
measurement that depends on GSP internal register state at an offset of `0x40` or
above is suspect until this is settled either way. See `BUGS.md` MD-12 for the
full finding, including the unresolved question of whether the register file
should be 128 four-byte-spaced registers rather than 512 bytes at two-byte
spacing.

## What has not been upstreamed

`STUNRUN-HOSTPROBE` exists because of a real ambiguity in `hd68k_gsp_io_w`'s
offset handling, and `STUNRUN-FETCH` exists because the TMS34010 offers no way to
trace instruction fetch from the debugger. Both would be reasonable upstream
pull requests. Neither has been submitted: this investigation has not yet
established which of the two offset readings is correct, and upstreaming a probe
built on an unsettled premise costs more credibility than it buys.