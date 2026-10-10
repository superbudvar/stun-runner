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

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   bd dolt push
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
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
