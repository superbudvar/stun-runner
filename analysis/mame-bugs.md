# MAME bugs found while decompiling S.T.U.N. Runner

Review-and-report index for the defects and gaps we hit in MAME 0.289. The full
findings live in `BUGS.md` under **MAME driver**; this file exists because that
section mixes MAME bugs with ROM and hardware findings, and because a bug you
cannot find again is a bug you will not report.

**None of these have been sent upstream.** Several are close; none is ready.
The blockers are named per entry, because "we found a MAME bug" is not the same
as "we can file a useful one", and a report built on an unsettled premise costs
more credibility than the finding is worth.

## Inventory

| ID | Summary | Kind | Confidence | Upstreamable? |
|---|---|---|---|---|
| MD-01 | `m:snapshot()` writes zero files | driver gap | confirmed | yes — smallest reproducer here |
| MD-02 | `emu.register_periodic` never fires | driver defect | confirmed | yes |
| MD-03 | gdbstub has no 68010 register description | driver gap | confirmed | yes |
| MD-04 | GSP display registers unreadable from Lua | driver gap | confirmed | yes — by design, needs framing |
| MD-05 | **missing — no entry, no withdrawal note** | — | — | bookkeeping gap |
| MD-06 | Lua tables filled from index 0 silently truncate | driver defect | confirmed | yes |
| MD-07 | `sizeof()` on a `memory_share_creator` is not the share size | driver defect | confirmed | yes |
| MD-08 | `scanline_multisync()` does not run headless | driver defect | confirmed | yes — related to MD-04 |
| MD-09 | Service mode "BAD GSP VRAM" | driver defect **or** ROM behaviour | **unresolved** | **no — do not report yet** |
| MD-10 | *withdrawn 2026-10-13 — documented behaviour, not a defect* | — | — | no |
| MD-11 | *withdrawn 2026-10-13 — misuse of a documented API, not a defect* | — | — | no |
| MD-12 | GSP register writes run off the end of `m_IOregs` | driver defect (memory safety) | indexing confirmed; **inert for this game** (§86.2) | **no — needs a sanitiser run first** |
| MD-13 | *withdrawn 2026-10-17 — the part has no repeat instructions; our error* | — | — | no |
| MD-14 | TMS34010 JR/JA: executor and MAME's own disassembler select different forms | driver defect (ISA divergence) | **disagreement confirmed by source**; impact unbounded | **yes — strongest candidate now**, after the fetched question |

## What each needs before it can be sent

**MD-13 — withdrawn 2026-10-17, do not send.** This one was the closest to ready
and that made it the most dangerous entry in the file: it had a concrete patch
sized, a published encoding, and "easy approval, real value to anyone reading
GSP code" written against it. All of that was resting on our having invented the
instructions. SPVU001A Appendix A lists every instruction the TMS34010 has, and
there is no `RS`/`RS1`/`RS2`/`RI`/`RPT` — `RS` and `RD` are operand symbols
("source register"/"destination register" in the chart's own legend). MAME's
`case 0x9800` renders `MOVE *Rs+,*Rd+,F`, which is what A-14 says that encoding
*is*, and `34010tbl.hxx:471` dispatches the same range to `move0_ni_ni_a`.

Had this been sent, it would have been a confident bug report asking MAME to add
mnemonics for instructions the silicon does not have, backed by a patch that
would have decoded real `MOVE`s as something fictional. Two false reports cost
more credibility than the silence they replace.

The general lesson, and the reason it is recorded at this length: **a missing
mnemonic is not a defect until the primary source says the opcode should produce
a different one.** Checking that the ISA contains an instruction before filing
that it decodes wrongly is a cheap step, and skipping it is what put a
"trivial patch, easy approval" row in this table for months.

The refutation, with the sweep and the `$0x9920` accounting, is in
`docs/mc68010-boot-findings.md` §83 and `BUGS.md` MD-13.

**MD-01, MD-02, MD-03, MD-06, MD-07 — confirmed, need reducers.** Each has a
measured symptom but was found while working around the oracle, not while
reducing a test case. A report wants: driver, ROM set, exact command line, and
observed-vs-expected. MD-06 and MD-07 are the most likely to be accepted as-is,
because both are self-contained Lua-side API problems with a one-line reproducer.

**MD-04 and MD-08 — need framing, not more evidence.** Both are the same
underlying fact: the GSP's display registers live in a host-side array that Lua
and the headless scanline path cannot reach. Reporting them as two separate bugs
would get one closed as "by design". They should go in as one report about the
GSP display register being unreachable outside `execute_run`, with MD-08's
`get_display_params()` contrast as the evidence that a path already exists.

**MD-09 — do not report.** It is unresolved whether "BAD GSP VRAM" is MAME or the
1989 game. `BUGS.md` records that the MC68010 diagnostic at `$004F82` reads back
16 words that all return `$FFFF`, and that the instruction encoding at `$004F96`
is non-canonical in a way the MAME disassembler papers over. Until that is
settled, a report would be asserting one of two contradictory things.

**MD-12 — the one worth doing properly.** See below.

## MD-12 needs a sanitiser run

`internal_regs_map` exposes `map(0xc0000000, 0xc00001ff)` — 512 bytes of offsets —
while the backing store is `uint16_t m_IOregs[64]` and `IOREG(reg)` is
`m_IOregs[reg]` with no mask. > **CORRECTED 2026-10-17 — this game does NOT do that.** The claim that "the
> real S.T.U.N. Runner GSP program writes at offsets `0x90`–`0x160`, so it indexes
> 208 entries past the end" is **disproven**. MAME ships the `STUNRUN_GSP_IOBREPORT`
> triage hook, which `fopen`s a report lazily inside the `if (offset >= 64)` arm.
> Run on `md12-fixed`, which **contains** the hook: 600 frames power-on and 600
> frames of service mode produced **no report file**, so no write at offset ≥ `0x40`
> ever happened (findings §86.2). Findings §52.2 reached the same conclusion but
> **vacuously** — it ran the frozen oracle, which contains zero occurrences of the
> hook string, so the instrument was absent.
>
> The overflow is real in the unfixed array; it does not occur on this game's
> paths. **The sanitiser run below is therefore no longer on the critical path
> for S.T.U.N. Runner** — it would demonstrate a latent defect in the array, not
> anything that affects us. Prioritise **MD-14** instead, which is a live ISA
> divergence with 161 candidate sites on the frame-404 image.

The array's declared extent is 64 entries while `internal_regs_map` exposes
`0x200` bytes of offsets, so any program that did write above `0x40` would
index up to 208 entries past the end.

The out-of-bounds indexing is confirmed by reading the source. What it *corrupts*
is not, and ASAN will probably not tell us: the overflow lands inside the
`tms34010_device` object, so it is an intra-object overflow rather than a heap
one, and stock ASAN does not instrument C++ member arrays for that. It would need
`-fsanitize=address` together with either reordering the member to be last or a
deliberate probe.

The cheaper and more direct route is to print where `m_IOregs[0x110]` actually
lands:

```c
fprintf(stderr, "IOREG %d -> %p (m_IOregs %p, delta %ld)\n",
        offset, &m_IOregs[offset], (void*)m_IOregs,
        (long)((char*)&m_IOregs[offset] - (char*)m_IOregs));
```

for each of the offsets the program writes. That names the clobbered members
directly instead of inferring them from the class layout, and it costs one
incremental rebuild instead of a full sanitised one.

**Do not send this upstream yet.** There is a second, unresolved question that has
to be answered first: the TMS34010's I/O space is 4-byte spaced over `0x200`
bytes, which is 128 registers, while MAME stores 64 `uint16_t` and names 32 in its
`REG_` enum. MAME's register file may be half the size the hardware map implies
even before the overflow. A patch that only grows the array could be papering over
a different intent, and the MAME maintainers will ask exactly that.

## Withdrawn, and why that matters

MD-10 and MD-11 were filed and then withdrawn. Both turned out to be documented
behaviour (a pass-through tap being dropped by a handler change) and misuse of a
documented API (Lua callbacks are not coroutines, so no per-event allocation or
file I/O). They are recorded with the refutation inline rather than deleted, so
the next agent does not re-file them.

That is the argument for this file existing. The failure mode here is not "we
missed a bug" — it is "we reported four things upstream and two were wrong",
which is how a project stops being taken seriously.

## Local-only, not upstreamable

Recorded in `BUGS.md` but deliberately not numbered: moving the MAME source tree
invalidates `build/projects/**/ocore_sdl.make`, which bakes in an absolute
`-include` path from configure time, so any rebuild dies in `osd_sdl`. That is
build-system behaviour, not a defect worth an issue.

The six local observation hooks in the MAME tree are catalogued in
`analysis/mame-local-patches.md` with a test that fails if an unrecorded one
appears in `git diff`. They are not MAME bugs; they are ours, and the guard exists
so the oracle cannot silently stop being stock.