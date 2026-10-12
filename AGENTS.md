# Agent Entry Point

This file is the mandatory entry point for agent sessions.

Do not begin broad disassembly, implementation, or verification from raw ROMs alone. First establish the machine, evidence, and automation baseline.

## Always read first

1. `AGENTS.md` — this entry point
2. `STATUS.md` — current milestone and immediate objective
3. `PROCESS.md` — role boundaries and experimental method
4. `PROJECT.md` — target-specific goals and architecture
5. `QUESTIONS.md` — unresolved cross-agent requests
6. `BUGS.md` — defects in the **original** game and in MAME, as opposed to ours

Then read the contracts relevant to the task:

- `DRIVER_MINING.md` — mandatory Step 0 MAME driver-mining process
- `REQUIREMENTS.md` — hardware, ROM, compiler/assembler, and emulator requirements
- `EVIDENCE.md` — listings, traces, snapshots, and checkpoint provenance
- `MAME_INSTRUMENTATION.md` — stock MAME Lua/debugger instrumentation
- `MAME_HARNESS.md` — Playwright-style MAME automation/test-driver contract
- `TESTING.md` — unit/integration/E2E/regression/CI strategy
- `LCOV.md` — native and original-code coverage
- `PREFLIGHT.md` — readiness gate, cost controls, stop conditions, concurrency
- `MILESTONES.md` — observable completion criteria

Do not repeatedly load every long document when a task only needs a subset. Preserve context and credits by following the role/task-specific contracts.

## Role selection

Each substantial task operates as exactly one role:

- **Investigator** — evidence generation, Step 0 driver mining, static listings, tracing, experiments, interconnect discovery, semantic understanding, candidate selectors, telemetry definitions, evidence-backed fixtures.
- **Implementer** — reproduction/native implementation, processor toolchain validation, implementation-level unit tests.
- **Verifier** — MAME harness, deterministic replay, checkpoints, assertions, fixtures, failure artifacts, differential/integration/E2E tests, golden fixtures, coverage, mismatch classification.

Do not silently cross ownership boundaries. Use `QUESTIONS.md` for handoffs.

---

# Mandatory Step 0 — Mine the MAME driver

Before broad static or dynamic reverse engineering of a target, follow `DRIVER_MINING.md`.

The Investigator must produce or refresh:

```text
analysis/driver-mining/<target>.md
analysis/driver-mining/<target>.machine-map.yaml
```

The mining pass must identify, with provenance:

- the target's actual machine configuration, not just generic board-family capabilities;
- active CPUs/DSPs and optional processors that are not instantiated;
- target init path and dynamically installed handlers;
- CPU/DSP address maps and shared/device windows;
- ROM regions, interleave/load rules, and hashes available from MAME;
- input/ADC/service/coin landmarks;
- interrupt and inter-device wiring;
- meaningful MAME handler names as candidate symbols/selectors;
- contradictions against project assumptions;
- bounded runtime experiments derived from those puzzle pieces.

MAME names describe the emulator model; they do not prove game semantics.

The S.T.U.N. Runner worked example is:

```text
analysis/driver-mining/stunrun.md
analysis/driver-mining/stunrun.machine-map.yaml
```

Step 0 should happen for future targets before asking agents to stare at an unannotated disassembly.

---

# Current priority

The repository is at **M0 — Reproducible machine, harness, and evidence baseline**.

Do not begin broad decompilation, semantic rewriting, or a broad native port until all of the following exist:

- Step 0 driver-mining output refreshed against the pinned MAME revision;
- a pinned S.T.U.N. Runner ROM set/revision;
- a checked-in MAME-derived ROM manifest with expected CRC32/SHA-1 values but no ROM contents;
- a pinned MAME version/commit and launch recipe;
- runtime CPU/device-tag inventory reconciled with the Step 0 machine map;
- a static listing recipe for every **actually active programmable processor**;
- at least one bounded deterministic trace recipe;
- a canonical reset/title checkpoint definition;
- a tested code-emission plan for every active programmable CPU/DSP that must be reconstructed;
- public tests that do not require commercial ROMs;
- a stable Playwright-style MAME harness capable of deterministic input, bounded waits, assertions, fixture/checkpoint creation, and failure artifacts;
- a first trace-to-LCOV report from original code.

Family-level optional devices must not become toolchain requirements until the target's pinned MAME config/runtime inventory shows they are active.

---

# Evidence rule

`reference/` contains raw reference evidence, not editable interpretation.

Never modify a raw listing, trace, memory dump, screenshot, or canonical metadata file to match a hypothesis. Put annotations and conclusions under `analysis/`, `asm/`, and `symbols/`.

Large regenerable artifacts may remain local when their generation scripts, metadata, hashes, and representative evidence are committed.

---

# Harness rule

Treat the original game under pinned MAME as the behavioral oracle and interact with it through the stable harness described in `MAME_HARNESS.md` whenever practical.

If an emulator interaction will be repeated, turn it into a harness capability, selector, fixture, experiment, or assertion instead of leaving it as a manual agent ritual.

The harness is mandatory for repeatable tests and investigations: use the
existing scripted runners rather than launching ad-hoc MAME commands. Extend
`MAME_HARNESS.md`, `mame/`, `mame_harness/`, or `tools/` when a new test needs
an input mode, selector, checkpoint, assertion, trace, snapshot, or failure
artifact. Python MAME runners must use `mame_harness/process.py` so MAME and
its descendants are reaped on timeout, interruption, or failure. Interactive
runs must use explicit windowed mode (`-window`), not fullscreen; headless
runs remain the default for automated captures.

All waits must be bounded. Timeouts must produce useful diagnostics.

---

# Naming conventions

## Ghidra function names

When creating or renaming functions in the Ghidra MCP (`maincpu.bin` program), use:

```
s_<addr>_<subsystem>_<friendly_name>
```

Examples:

| Ghidra name | ROM addr | Subsystem | Friendly name |
|---|---|---|---|
| `s_000025E8_mc68010_text_draw` | `$25E8` | mc68010_text | main text draw loop |
| `s_0000452A_mc68010_text_write_glyphs` | `$452A` | mc68010_text | glyph write via host port |
| `s_00004A48_mc68010_text_poll_gsp` | `$4A48` | mc68010_text | poll GSP ready (delay loop) |
| `s_0000196_mc68010_text_selector_dispatch` | `$196` | mc68010_text | selector dispatch table |
| `s_00004F82_mc68010_gsp_vram_test_main` | `$4F82` | mc68010 | BAD GSP VRAM diagnostic entry |
| `s_000003F8_gsp_boot_reset_main` | `$3F8` | gsp_boot | GSP reset orchestrator |
| `s_00000370_gsp_boot_delay_400` | `$370` | gsp_boot | 400-cycle delay |
| `s_0000037C_gsp_boot_load_palette` | `$37C` | gsp_boot | load palette from RAM table |
| `s_0000034A_gsp_boot_write_fifo_records` | `$34A` | gsp_boot | write (address,word) pairs to FIFO |
| `s_000007C2_gsp_boot_write_gsp_table` | `$7C2` | gsp_boot | write GSP control table |
| `s_0000048E_gsp_boot_init_fifo_write` | `$48E` | gsp_boot | init FIFO + write incrementing words |
| `s_00000570_gsp_boot_composite_fill` | `$570` | gsp_boot | composite: clear + palette + fill |
| `s_00000636_gsp_boot_fill_vram_FFFF_rect` | `$636` | gsp_boot | fill VRAM rect with 0xFFFF |
| `s_0000067A_gsp_boot_fill_vram_color` | `$67A` | gsp_boot | fill VRAM rect with colour word |
| `s_000022EC_mc68010_road_write_fifo_words` | `$22EC` | mc68010_road | write 8 words to FIFO (road poly) |
| `s_000024B8_mc68010_road_draw_quad` | `$24B8` | mc68010_road | draw 4 road polys |
| `s_000053F2_gsp_boot_ready_sequence` | `$53F2` | gsp_boot | GSP boot: clear regs + delay + set mode |

The ROM address is always part of the name so it is instantly visible in both Ghidra's function list and in any C slice that references it. Subsystem rough categories: `mc68010`, `mc68010_text`, `mc68010_road`, `gsp_boot`, `mc68010_diag`.

Rename via Ghidra MCP:

```
rename_function(old_name="FUN_000025E8", new_name="s_000025E8_mc68010_text_draw", strict_mode="off")
```

## Native C slice names

Ported C slices use `stunrun_<subsystem>_<friendly_name>` (no address in the C name — the address lives in the file header comment and the Ghidra symbol):

```
native/gsp_boot.c       → stunrun_gsp_boot_*()
native/mc68010_text_writer.c → stunrun_mc68010_text_draw()
native/gsp_2bpp_glyph.c → stunrun_gsp_2bpp_emit_row()
```

Each slice gets `native/<name>.{h,c}` + `native/<name>_test.c`, registered in `native/CMakeLists.txt`.

## Symbols directory

XREF CSV files under `symbols/` use the Ghidra name (with address) as the canonical identifier, not a C symbol name.  The `s_` prefix in Ghidra maps to `stunrun_` in C (the C prefix is longer to avoid namespace collisions with system headers).

---

# Investigator instructions

Before broad semantic annotation:

1. complete/reconcile Step 0 driver mining;
2. verify exact debugger-visible device tags through the running MAME harness;
3. generate static listings for each active executable processor space;
4. apply provenance-aware hardware landmarks from the machine map;
5. search XREFs to known device/input/shared-memory ranges;
6. execute bounded experiments derived from those landmarks;
7. promote semantic claims only when evidence supports them.

Prioritize `analysis/interconnect.md`: shared RAM, command queues, interrupts, graphics submission, DSP communication, and sound commands.

When a useful observation procedure will recur, contribute reusable harness/configuration capability rather than a one-off debugger ritual.

---

# Reverse-engineering findings protocol (mandatory)

Two records must be written **during** an investigation, not reconstructed afterwards. An investigation that ends in a fix but leaves no record has not finished.

## 1. Original-code defects go in `BUGS.md`

`BUGS.md` records facts about the **1989 game** and about the **MAME driver**. It
does not record mistakes we made. If you find any of the following, add an entry
before you close the investigation:

- a ROM routine that does something surprising, off-by-one, or self-referential;
- ROM data that contradicts the obvious reading (asymmetric write order, skewed ramp);
- hardware the ROM addresses in a way the board does not honour;
- a MAME driver defect or gap that shaped how we could observe the machine.

Use the template in that file and fill every field, including `Confidence`. Keep
ROM-code, ROM-data, hardware-abuse and curiosity in their own sections, and keep MAME
defects in the separate MAME section — they behave differently and upstreaming only
applies to the latter.

**Keep our own misunderstandings out of it.** "We misread the map and thought RAM was
512 KiB" belongs in `../docs/mc68010-boot-findings.md`, which is our running log of
wrong turns and their refutations. The line between the two is: does the original code
or the emulator do the surprising thing, or did we?

## 2. Assembly findings go in `../docs/mc68010-boot-findings.md`

Every disassembly read during debugging is conversion material. The C++ port replaces
routines, so the disassembly must outlive the debugging session. When you read a
routine, append it with:

- the ROM address range and the ROM file it came from;
- the transcription in mnemonic form, verbatim;
- the semantics you inferred, and **which parts are inference**;
- anything ambiguous, with the alternative readings spelled out;
- the C slice that implements it, once one exists.

## 3. Transcribe into a tested C slice

A read disassembly is not a deliverable. Each routine above should have a
`native/<name>.{h,c}` + `native/<name>_test.c` pair, registered as a CTest in
`native/CMakeLists.txt`, asserting the **observable output** — the FIFO word stream,
the marker writes, the resulting memory — rather than just "it ran".

When the disassembly is ambiguous, **keep the unambiguous part faithful and implement
the ambiguous part as a documented choice**. Say so in a comment at the decision site
and in the findings doc. Do not silently pick one reading.

The two existing examples are the pattern: `native/gsp_boot.{h,c}` (19 boot routines,
asserting the exact GSP FIFO word sequence) and `native/ram_march.{h,c}` (five RAM-march
variants, asserting watchdog cadence, marker words and fault injection).

## Delegate one routine per subagent

**When a routine needs to be analysed or ported, spawn a subagent to do it
rather than doing it in the main session.** One routine per subagent.

The reason is the failure mode this repository has actually hit. Reading a
disassembly is slow, and doing twenty of them in one context means the early
ones get compressed into a summary that no longer distinguishes *confirmed*
from *inferred*. By the time the reader reaches routine twenty, the standard
is whatever the last confident sentence implied. Splitting the work gives each
routine a fresh context that has to do the analysis properly, and it lets the
lanes run in parallel because their write scopes do not overlap.

### What a subagent owns

Exactly this, and nothing else:

```text
native/<name>.h
native/<name>.c
native/<name>_test.c
one appended section of ../docs/mc68010-boot-findings.md
```

**`native/CMakeLists.txt` is shared and the subagent does not touch it.** The
parent registers the test. This is the same discipline the dual-GSP lanes used
(`analysis/dual-gsp-comparison-plan.md`: the lanes "must not change shared
top-level CMake or existing CPU/backend interfaces"), and it is what makes
parallel lanes safe rather than merely fast.

If two routines genuinely belong in one slice, that is one subagent's scope,
not two subagents' — overlapping files are a merge conflict waiting to
happen, and the second agent's test will silently stop testing the first
agent's code.

### What a subagent must return

Not just files. Each subagent hands back:

1. the verbatim transcription, with the ROM address range and source chip;
2. **which parts are inference and which are confirmed**, stated separately;
3. any ambiguity, with the alternative readings spelled out rather than one
   chosen silently;
4. the observable output its test asserts, and why that is the right thing to
   assert rather than a weaker proxy;
5. its own check facts — the exact command it ran and the actual result;
6. anything it found that belongs in `BUGS.md` (a surprise in the original or
   in MAME), as a finding rather than a silent fix.

### What the parent owes before integrating

A subagent's success is not a semantic pass. Before its work is committed:

- read the transcription against the disassembly yourself;
- confirm the test asserts observable output and that it **fails** when the
  implementation is broken. A test that cannot fail is not a check — this
  repository has already shipped three that passed for the wrong reason (see
  `docs/mc68010-boot-findings.md` sections 15.5 and 15.6);
- run the suite yourself, not just the agent's report of it;
- resolve any contradiction the agent flagged against the oracle before
  changing an expected value. **Never** redefine an oracle expectation to
  make an implementation pass.

If two subagents disagree about the same routine, that disagreement is the
result. Resolve it against the original, and record the refuted reading in the
findings doc rather than deleting it.

---

# Implementer instructions

Do not treat successful disassembly as proof of a usable reproduction toolchain.

For each active programmable CPU/DSP that needs replacement code, prove:

```text
known instruction/source
→ expected bytes
→ fixed placement
→ MAME execution or independent validation
```

Do not build toolchains for optional processors that Step 0/runtime inventory shows are absent.

When semantics are uncertain, preserve literal mechanism rather than inventing intent.

Use established fixtures and the shared replay/checkpoint schema. Do not redefine oracle expectations to make implementation pass.

When implementing a routine, if the original code does something that looks like a bug,
**reproduce it** and add an entry to `BUGS.md` citing the slice. Do not improve it.

---

# Verifier instructions

Canonical evidence must identify:

- ROM manifest;
- MAME build/version;
- Step 0 machine-map revision;
- harness/experiment identity;
- start condition;
- debugger/input scripts;
- trace flags;
- captured state.

Own the harness architecture described in `MAME_HARNESS.md`, including deterministic input, bounded waits, fixtures/save states, normalized checkpoints, failure telemetry, differential testing, regression preservation, and original-code LCOV generation.

The Verifier checks the runtime machine inventory against the mined driver map so stale assumptions fail early.

Do not silently modify production code or oracle expectations to make verification pass.

---

# Stop conditions

Stop and create a handoff/investigation request rather than guessing when:

- driver-mined facts and runtime inventory materially disagree;
- required evidence is missing or contradictory;
- ROM/MAME provenance mismatches;
- replay is not deterministic;
- a selector cannot be justified from evidence;
- a wait cannot be bounded reliably;
- an assembler/encoder cannot be independently validated;
- implementation conflicts with a trusted oracle fixture;
- changing an expected result would require convenience rather than new evidence.

An explicit `UNKNOWN` or `ASSUMPTION CONFLICT` is a valid and useful result.

---

# Session hygiene

Leave persistent evidence, commands, tests, selectors, fixtures, harness capabilities, driver-mining updates, and concise project-state changes in the repository.

Do not rely on hidden agent context or previous chat history for facts another agent will need.

## Promote analysis scripts into `tools/`

**A throwaway script that answers a question is the start of a tool, not the end
of one.** When a script has produced a finding you intend to keep, promote it
into `tools/` in the same commit that records the finding.

The reason is loss, not tidiness. This repository has already lost a working
investigation to `git checkout -- .` and `git clean -fd`, and the scripts that
had produced it went with it. `/tmp` is not evidence and is not durable;
`tools/` is committed, reviewable, and covered by `tests/test_public_tools.py`.

Rules:

- **Name it `tools/<verb>-<noun>`.** Shebang, a module docstring that says what
  question it answers *and what it gets wrong*, `argparse`, and `--json`
  emitting a `stunrun-*-v1` schema. Match the existing tools.
- **Document the trap it exists to avoid.** The value is not the output, it is
  not making the same wrong assumption twice. `tools/build-maincpu-image` leads
  with the `ROM_LOAD16_BYTE`/`ROM_SKIP(1)` interleave, because assembling that
  image by copying files to byte offsets produces an image that *decodes without
  error into meaningless instructions*. `tools/analyze-gsp-hstctl` leads with
  the halt bit, because most writes to that register are HLT-preserving
  read-modify-writes and counting them as halt transitions is wrong.
- **A tool that reads a raw binary must refuse a wrong-sized input loudly.**
  These files are unversioned dumps of C structs; a reader built against a
  different struct revision does not fail, it invents plausible data. Guard on
  both the stride and the dense record sequence, and say which struct layout it
  expects and where that layout is defined.
- **Register a public test, and prove it can fail.** Run the test against a
  deliberately broken tool before committing; a check that passes both ways is
  not a check.

Tools currently covering this ground: `tools/build-maincpu-image` (assemble the
68010 ROM the shell executes), `tools/analyze-gsp-hstctl` (who halts and
releases the GSP, and for how much of a capture),
`tools/check-gsp-replay` (replay a captured stream through the board model and
the Verilated core), `tools/analyze-musashi-gsp-writes` (group the *decoded*
host-write log by caller PC).

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:46cd31e7 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Agents **commit locally at logical opportunities** (see "Committing at logical opportunities" below). Do not `git push`, and do not run Dolt remote sync, unless explicitly asked. At handoff, report commits, changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same local-commit git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may also close beads, run quality gates, `git push`, and `bd dolt push` as part of session close. A current "do not push" instruction still wins.

## Committing at logical opportunities

Local commits are expected, not something to ask permission for. A long
investigation left uncommitted is one crash, one `/tmp` wipe, or one context
loss away from being lost, and this repository accumulates evidence that
cannot be regenerated cheaply.

Commit when a unit of work is **verifiable and coherent** — not at every
save, and not only at session end. Concretely, commit at:

- a slice that passes its own test and leaves the suite green;
- a recorded finding, refutation, or evidence capture, once written down;
- a vendored dependency landing with its licence and provenance;
- before starting a materially different lane of work.

A commit must be **self-contained**: it builds or tests on its own, and its
message says what changed and what was verified. Do not commit a half-finished
slice "to save it" — that just moves the breakage to someone else's checkout.

Each logical opportunity is a separate commit. Squashing unrelated slices into
one commit destroys the history that makes the evidence auditable, and this
project's whole method depends on being able to see what was known when.

Never commit:

- commercial ROM contents, NVRAM, snapshots, or other local emulator state;
- generated build trees (`build/`) — see "Keep the build tree out of git";
- a failing test suite. If a change cannot be made green, leave it uncommitted
  and say so plainly at handoff.

### How to commit

```bash
git add <specific paths>       # never `git add -A`; review each path
git commit                     # message states what changed and what was verified
git status                     # confirm what was left out, and say why
```

Remote sync (`git push`, `bd dolt push`) is still gated by the active profile
above. Local commit authority is not.

## Keep the build tree out of git

`build/` is gitignored and must stay that way. It is a local artifact, not
source, and a committed tree is worse than no tree at all:

- **CMake caches an absolute source path.** `CMakeCache.txt` records the
  directory it was configured from. If the repository is moved, renamed, or
  checked out anywhere else, a committed cache makes `cmake --build build`
  fail outright with *"The current CMakeCache.txt directory ... is different
  than the directory ... where CMakeCache.txt was created"*, and the error
  message does not obviously point at the real cause. This repository has
  already been renamed once; the stale `build/` left behind broke every build
  until it was deleted.
- The tree is large, regenerable, and per-machine.

Configure into a fresh directory instead, and prefer a path outside the repo
when experimenting:

```bash
cmake -S native -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

If a committed build tree is ever found in history, delete it rather than
repairing it, and confirm `.gitignore` still lists `build/` before committing.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Commit at logical opportunities** — commit coherent, self-contained,
   green slices as they complete, per "Committing at logical opportunities"
   above. Do not leave a verified slice uncommitted.
5. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: local commits are expected; push is not.
   # Report status and the exact push command; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   bd dolt push
   git push
   git status
   ```
6. **Hand off** - Summarize commits made, changes, validation, issue status, and
   any blocked sync/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Local commits need no per-commit permission under this repository's policy.
- `git push` and `bd dolt push` still need authority from the active profile or
  the current user request.
- Never commit a red suite. Leave it uncommitted and report it instead.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->

<!-- BEGIN BEADS CODEX SETUP: generated by bd setup codex -->
## Beads Issue Tracker

Use Beads (`bd`) for durable task tracking in repositories that include it. Use the `beads` skill at `.agents/skills/beads/SKILL.md` (project install) or `~/.agents/skills/beads/SKILL.md` (global install) for Beads workflow guidance, then use the `bd` CLI for issue operations.

### Quick Reference

```bash
bd ready                # Find available work
bd show <id>            # View issue details
bd update <id> --claim  # Claim work
bd close <id>           # Complete work
bd prime                # Refresh Beads context
```

### Rules

- Use `bd` for all task tracking; do not create markdown TODO lists.
- Run `bd prime` when Beads context is missing or stale. Codex 0.129.0+ can load Beads context automatically through native hooks; use `/hooks` to inspect or toggle them.
- Keep persistent project memory in Beads via `bd remember`; do not create ad hoc memory files.

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md for details and anti-patterns.
<!-- END BEADS CODEX SETUP -->
