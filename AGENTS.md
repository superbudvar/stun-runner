# Agent Entry Point

Evidence-driven reconstruction of Atari Games' **S.T.U.N. Runner**. The original
game under pinned MAME is the behavioral oracle; every claim must trace back to a
recorded observation.

**Read `STATUS.md` first.** It owns the current milestone, the immediate objective,
the working processor inventory, and the open blockers. This file is only the
map; it does not track progress.

## Documents, and when you need them

Do not bulk-load these. Read `STATUS.md` plus the contract your role needs.

| File | Owns |
|---|---|
| `STATUS.md` | current milestone, objective, blockers, inventory |
| `PROCESS.md` | Investigator / Implementer / Verifier roles, experimental method |
| `PROJECT.md` | target goals and architecture |
| `DRIVER_MINING.md` | mandatory Step 0 driver-mining process |
| `MAME_HARNESS.md` | harness contract: selectors, waits, fixtures, checkpoints |
| `docs/mame-machine-reference.md` | what the pinned MAME tree models: chips, address maps, wiring |
| `TESTING.md` · `LCOV.md` · `PREFLIGHT.md` · `MILESTONES.md` | test strategy, coverage, readiness gates |
| `EVIDENCE.md` | required provenance fields for any captured artifact |
| `BUGS.md` | defects in the **1989 game** and in **MAME** (never ours) |
| `docs/sources.md` | the citation register (`S1`…`S9`) and the rule for using it |
| `CREDITS.md` | who made the original game, from the ROM's own credits table |
| `QUESTIONS.md` | unresolved cross-agent requests — use it for handoffs |
| `docs/mc68010-boot-findings.md` | running log of readings, inferences, and **our own refutations** |
| `analysis/` | per-topic interpretation and captures; `analysis/driver-mining/` is Step 0 |

One role per substantial task (see `PROCESS.md`). Do not silently cross the
ownership boundaries; hand off through `QUESTIONS.md`.

---

## Commands

```bash
./bootstrap          # tools/env-check + the ROM-free public suite
./test               # public suite: python3 -m unittest discover -s tests  (362 tests, ~85 s idle)
./build.sh && ./run.sh          # build + launch the live native shell (SDL/OpenGL windows)
STUNRUN_LIVE_VIDEO=0 STUNRUN_FRAMES=600 ./run.sh     # bounded headless native run

cmake -S native -B build/native && cmake --build build/native -j"$(nproc)"
ctest --test-dir build/native                        # C slice tests (CTest)

STUNRUN_ROMPATH=roms ./oracle-test                   # ROM + MAME oracle checks; SKIPs when unset
tools/env-check                                       # what is installed, one line per tool
```

`oracle-test` assumes `mame` is on `PATH`; without it the run fails inside
`tools/check-roms` before it does anything:

```bash
PATH=/mnt/build/mame-ref:$PATH STUNRUN_ROMPATH=roms ./oracle-test
```

Single public test:

```bash
python3 -m unittest discover -s tests -p 'test_public_tools.py' -v
python3 -m unittest discover -s tests -p 'test_checkpoint_golden.py' -k test_c_emitter
```

`STUNRUN_*` variables are the native shell's configuration surface — `./run.sh -h`
lists them. Any interaction you expect to repeat belongs in `tools/` or the
harness, not in a shell incantation you will have to remember.

---

## Traps

Each of these has already cost real time here.

1. **The Musashi objects are not in the submodule.** `native/CMakeLists.txt` links
   `third_party/musashi/{m68kcpu,m68kdasm,softfloat/softfloat}.o` as prebuilt files,
   and that submodule tracks no `.o`. A fresh checkout fails to link the shell and
   every musashi-linked test. Build them once:
   ```bash
   make -C third_party/musashi m68kcpu.o m68kdasm.o softfloat/softfloat.o
   ```
2. **`mame` is not on `PATH`.** Tools default to a bare `mame` and fail
   confusingly — `oracle-test` dies inside `tools/check-roms` with
   `exec: mame: cannot execute`. Either export the directory
   (`PATH=/mnt/build/mame-ref:$PATH`) or pass the binary as the tools' second
   argument: `tools/check-roms roms /mnt/build/mame-ref/mame`,
   `tools/mame-ram-write-trace roms OUT --mame /mnt/build/mame-ref/mame`.
   `/mnt/build/mame-oracle/mame` is the frozen oracle binary (sha256 recorded in
   `analysis/mame-local-patches.json`); `mame/system-baseline.json` still names
   `/usr/lib/mame/mame`, which no longer exists — trust the patches inventory.

    **There are three MAME binaries, and "the oracle" is not one of their
    names.** Call them by what they are:

    | name | path | sha256 | use it for |
    |---|---|---|---|
    | **`md12-fixed`** (DEFAULT) | `/mnt/build/mame-ref/mame` | `55956f2b…` | **all new work.** Upstream MAME with MD-12 fixed, plus every observation hook, including `STUNRUN_GSP_IOBREPORT`. |
    | `stock-behaviour` | `/mnt/build/mame-oracle/mame` | `75b09ee4…` | **only** re-reproducing a capture recorded under it. Predates the MD-12 fix and lacks every hook added after 2026-10-09. |
    | *unrecorded* | — | `fcb54790…` | produced committed captures and is in neither recorded slot. See `stun-runner-w55`. |

    Why `md12-fixed` is the default, measured: **MD-12 never fires on the paths
    we run** — 600 frames power-on and 600 frames of service mode produced zero
    out-of-bounds GSP register writes (findings §86.2). The fix therefore changes
    nothing we have recorded, and `md12-fixed` is simply the more capable build.
    The oracle's defect is real but inert here. **Never compare a capture across
    binaries**; record the sha256 with the capture.
3. **The pinned MAME tree is patched, and a guard enforces it.**
   `/mnt/build/mame-ref` is a git tree on branch `stunrun-observe` carrying
   observation hooks. `tests/test_mame_local_patches.py` diffs it against
   `analysis/mame-local-patches.json` and fails on any unrecorded source change.
   If it fails, the MAME tree was edited outside this session — record the patch
   (`tag`, `function`, `env`, `observation_only`, `purpose`, `files`) or revert the
   edit. Do not "fix" the test. A behavioural edit smuggled in among observation
   hooks invalidates every measurement in `reference/`.
4. **Scripted MAME input is nondeterministic.** `--input none` is deterministic;
   any `--input` schedule alternates between arms run-to-run. Run a command
   **five times** before claiming it reproduces or fails — one re-run has produced
   confident wrong answers in both directions here (`docs/mc68010-boot-findings.md`
   §29). Frame numbers from an `--input` run are mode-specific; never fold them into
   a mode-independent expectation. And `result.json`'s `canonical_log_sha256`
   covers the output path, so it is only comparable within one directory.
5. **`./build.sh` configures `Release`.** That defines `NDEBUG` and compiles out the
   `assert()`-based self-checks, so the suite passes vacuously. For tests use
   `-DCMAKE_BUILD_TYPE=Debug` (what the Python suite uses) or no build type at all.
6. **`build/` stays out of git.** CMake caches an absolute source path, so a
   committed tree breaks every build the moment the repo moves. Configure fresh.
7. **`.gitmodules` has no entry for `third_party/ymfm`**, so `git submodule status`
   errors even though the tree is present. The build still works; that error is not
   a broken checkout.
8. **Commercial ROMs are local and gitignored** (`roms/stunrun/`, plus `*.zip`,
   `stunrun-flat.bin`, `nvram/`, `snap/`, `capture/`, `cfg/*.cfg`). Never commit
   them. `tools/check-roms roms <mame>` validates the set against
   `roms/stunrun.manifest.json`.

9. **A captured RAM buffer can be commercial ROM.** Trap 8 is about files you
   know are ROM. It does not catch a memory capture that turns out to hold ROM
   bytes: main RAM `0xFF9584`/`0xFF9884` at frame 1200 is words 0-359
   bit-identical to ROM `0x044930`-`0x044B2E`, and in the gameplay state
   384/384 verbatim. It reads like a regression fixture and would have been
   committed as one. Before committing any captured buffer, read the ROM side
   with the emulator and compare — do not assume "it was in RAM" means "it is
   not ROM". `tools/check-roms` will not catch this. Findings §74.

10. **`/mnt/build/mame-ref` is a SHALLOW clone — one commit. History queries
    lie.** `git log -S "<string>"`, `git blame`, anything reaching further back
    than `HEAD` returns **nothing**, and nothing looks like proof: "the string was
    never added or removed in any commit" is what a search returns whether the
    string is ours or upstream's. It nearly produced a confident wrong answer
    about MD-12 today. To tell a local edit from an upstream one, use
    `git diff HEAD -- <file>` against the pinned base `e4c1c2b5` — that is the
    comparison that works. (`git rev-list --count HEAD` returns `1`; say so in
    the findings whenever a history query is untrustworthy.)

11. **Reconcile any share sweep against `nonzero_words` in the capture's
    `result.json`.** This is the cheapest guard in the repository and it is not
    automated everywhere. The 512 KiB physical GSP share is **dense** — 262,144
    words at 2 bytes — and a 2-byte sweep of the frame-404 image returns exactly
    the **81,899** nonzero words the metadata records. Two nearby historical
    numbers get confused here, so state which you mean: **10,539** nonzero words
    sampled at 16-byte intervals by the OLD `disasm-gsp` reader, and **10,989**
    16-byte-aligned cells containing any nonzero byte. Same image, both correct,
    different questions. The bug was a unit mismatch: the tool changed input from
    a 4 MiB cell image to a 512 KiB dense share but left its cell reader unchanged.
    `WORD_STRIDE = 16` is still correct as MAME length units per word; the fix
    expands dense input to cells internally and reports dense byte offsets
    (`stun-runner-a9q`). Its tests now separate ISA decoding from address-fold
    tests against MAME's own expression. When a tool's output is a count, a total,
    or a "N instructions" claim, check the count against the capture's own
    metadata before repeating it.

12. **MAME state is per-RUN, and `tools/mame_launch.py` is the only place that
    is decided.** MAME writes `stunrun.cfg` at exit including current input-port
    (DIP) state, and `-noreadconfig` does not stop the next run reading it —
    that file is read and written through `options().cfg_directory()` and
    nothing else (`src/emu/config.cpp`), so the two `-no*config` flags do not
    control it at all. `tools/mame-memory-snapshot` used to leak input-port
    state between runs: `service_probe` presses "Service Mode" on
    `:mainpcb:IN0`, which silently forced that DIP off in every later run —
    main RAM stayed all-`0xFF`, the exit status was still 0, and the tool
    reported success. A whole input-mode sweep was corrupted this way and looked
    like clean null results. Fixed by giving each run its own
    `-cfg_directory` (`MameCfgIsolationTests`, findings §74).

    The same defect was still live in **20 of the 27 launchers** when measured
    at `a9a9776`: 12 Python tools carried a byte-identical
    `"-cfg_directory", "/tmp/stunrun-mame-cfg", "-nonvram_save"` line and 3
    shell tools the same sequence in shell quoting; 22 named the literal at
    all; 8 also shared `/tmp/stunrun-state-dir`; `-statename` appeared in only
    10. Separately **18** passed `-nonvram_save` with no `-nvram_directory`
    anywhere: in `src/emu/machine.cpp` that flag gates only the *write*
    (`if (options().nvram_save()) nvram_save();`) while the read is
    unconditional and served from `options().nvram_directory()`, so those runs
    inherited `~/.mame/nvram/stunrun/` and produced a capture of exactly the
    right size holding stale or zeroed bytes.

    **Every MAME argv now comes from `mame_launch()`**: per-run cfg, nvram and
    state directories made with `tempfile.mkdtemp` and removed in a `finally`,
    on the exception path too — `/tmp` is ~92% full and disk-full has already
    produced exit code 120 with *empty* output files. Do not hand-roll an argv
    and do not hardcode a shared directory. `MameCfgIsolationTests` discovers
    the launchers itself instead of from a list, and its controls break copies
    of real tools to show it can fail; a guard that only looks where it already
    looked is the same failure as the bug. If you add a capture tool, re-run a
    known-good capture after any run that touches an input port.

---

## Layout

```text
native/        native port + the deterministic shell (shell.c) and its C slices
reproduction/  processor-faithful slices, one dir per CPU:
               adsp/ gsp/ maincpu/ sound/     <- compiled into BOTH targets
third_party/   musashi (submodule), ymfm (gitlink), stb, tms34010_sv (vendored RTL)
tools/         ~80 committed analysis tools, one question each; covered by tests/
tests/         ROM-free Python unittest suite (the public suite)
mame/ + mame_harness/   MAME launchers, Lua taps, process reaping
reference/     raw captured evidence — never edited
analysis/      interpretation, per topic
docs/          findings write-ups, incl. the mc68010 boot log
BUGS.md        defects in the original game and in MAME
```

**Single-canonical-implementation rule:** a transcribed processor routine lives
once, in `reproduction/<cpu>/`, and `native/CMakeLists.txt` compiles those same
sources into both the shell and the standalone slice test. Do not fork a copy into
`native/`. (`native/` also holds earlier slices plus the shell/render/checkpoint
boundary — leave those where they are.)

Ported functions are `stunrun_<subsystem>_<name>()` in C. Ghidra symbols
(`s_<addr>_<subsystem>_<name>`, project `stunrunner` under gitignored
`ghidra_stunrunner/`) keep the ROM address in the name, so a C slice and its
disassembly line up in either direction.

---

## Rules that outrank convenience

**Evidence.** Never edit anything under `reference/` to match a hypothesis — it is
raw evidence. Conclusions go in `analysis/` and `docs/`. When you capture
something, record the provenance `EVIDENCE.md` demands: ROM manifest, MAME build,
machine-map revision, harness identity, start condition, scripts, trace flags,
captured state.

**Cite what you assert.** Any external claim carries a source number from
`docs/sources.md`; anything you measured carries `[ROM]` and the tool that
reproduces it. Prefer the primary source: for what the game *says* the ROM wins,
for what the board *is* the SP-338 manual wins, for what the emulator *does* the
pinned MAME source wins. Mark inference at the site where you make it. A claim
you cannot source is `UNKNOWN` — and a wrong citation is worse than none,
because it launders a guess into something that looks verified. This applies to
biography too: writing a plausible one-line summary of a real person's career is
the easiest way to put an error into a permanent file.

**Bounded waits.** Every wait needs a bound; a timeout must produce a diagnostic
that says what it was waiting for. Python runners go through
`mame_harness/process.py` so MAME and its descendants are reaped. Headless is the
default; interactive runs must pass `-window`, never fullscreen.

**Two records per investigation, written during it, not after.**

- A surprise in the **original ROM or in MAME** → `BUGS.md` (separate ROM-code /
  ROM-data / hardware-abuse / MAME sections, every field filled including
  `Confidence`).
- **Our own** misunderstanding → `docs/mc68010-boot-findings.md`, as a refutation.
  The test: did the original code or the emulator do the surprising thing, or did
  we?

**A read disassembly is not a deliverable.** Each routine becomes
`reproduction/<cpu>/<name>.{h,c}` + `<name>_test.c`, registered in
`native/CMakeLists.txt`, asserting **observable output** — the FIFO word stream, the
register writes, the resulting memory — not "it ran". Write the expectations from
the disassembly, not from your own implementation's output. Where the reading is
ambiguous, keep the unambiguous part faithful, implement the ambiguous part as a
documented choice, and say so at the decision site and in the findings doc.

**Never redefine an oracle expectation to make an implementation pass.** Resolve the
contradiction against the original, or record it as `UNKNOWN`.

**Delegate one routine per subagent.** Twenty routines in one context means the
early ones decay into a summary that stops separating confirmed from inferred. A
subagent owns `reproduction/<cpu>/<name>.{h,c}`, `<name>_test.c`, and one appended
findings section — **not** `native/CMakeLists.txt`; the parent registers the target.
It must hand back the verbatim transcription with address range and source chip,
confirmed vs inferred stated separately, ambiguities spelled out rather than
silently resolved, what its test asserts and why, and its own check command and
result. Before integrating: read the transcription yourself, and confirm the test
**fails** when the implementation is broken. This repo has shipped three tests that
passed for the wrong reason.

**Promote throwaway scripts into `tools/`.** A script that answered a question you
intend to keep is the start of a tool, not the end of one — `/tmp` is not evidence,
and an investigation has already been lost to `git checkout -- .`. Match the
existing shape: `tools/<verb>-<noun>`, shebang, docstring saying what question it
answers *and what it gets wrong*, `argparse`, `--json` with a `stunrun-*-v1` schema,
and a case in `tests/test_public_tools.py` that you have seen fail against a
deliberately broken tool. A tool that reads a raw binary dump must reject a
wrong-sized input loudly — those files are unversioned struct dumps, and a reader
built against a different revision invents plausible data instead of failing.

## Stop and hand off rather than guess

Driver-mined facts and runtime inventory disagree; evidence is missing or
contradictory; provenance mismatches; replay is not deterministic; a selector cannot
be justified from evidence; a wait cannot be bounded; an encoder cannot be
independently validated. An explicit `UNKNOWN` or `ASSUMPTION CONFLICT` is a valid
result.

---

## Git policy

Local commits are expected at logical opportunities — a slice whose own test passes
and leaves the suite green, or a finding written down. Each is self-contained and
says what changed and what was verified. Never commit ROMs, emulator state, build
trees, or a red suite; leave it uncommitted and say so at handoff. `git push` and
`bd dolt push` need explicit authority. (`CLAUDE.md`'s beads block predates this
and is more restrictive; this section is current.)

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

<!-- bv-agent-instructions-v7 -->

---

## Beads Workflow Integration

This project uses a Beads tracker—either the Go `bd` CLI or the Rust `br` CLI—for issue tracking, plus [beads_viewer](https://github.com/Dicklesworthstone/beads_viewer) (`bv`) for graph-aware triage. Issues are stored in `.beads/`. `bv` auto-discovers supported JSONL exports, including `.beads/issues.jsonl` and legacy `.beads/beads.jsonl`.

**Choose the tracker CLI from this repository's instructions and configuration.** Use `bd` commands in a Go Beads workspace and `br` commands in a beads_rust workspace. Do not run both trackers against the same workspace or infer the tracker solely from the JSONL filename.

### Using bv as an AI sidecar

bv is a graph-aware triage engine for Beads projects. Instead of parsing .beads/issues.jsonl / .beads/beads.jsonl directly or hallucinating graph traversal, use robot flags for deterministic, dependency-aware outputs with precomputed metrics (PageRank, betweenness, critical path, cycles, HITS, eigenvector, k-core).

**Scope boundary:** bv handles *what to work on* (triage, priority, planning). The selected tracker CLI (`bd` or `br`) handles creating, claiming, modifying, and closing beads.

**CRITICAL: Use ONLY --robot-* flags. Bare bv launches an interactive TUI that blocks your session.**

#### The Workflow: Start With Triage

**`bv --robot-triage` is your single entry point.** Its `triage` object contains:
- `quick_ref`: at-a-glance counts + top 3 picks
- `recommendations`: ranked actionable items with scores, reasons, unblock info
- `quick_wins`: low-effort high-impact items
- `blockers_to_clear`: items that unblock the most downstream work
- `project_health`: status/type/priority distributions, graph metrics
- `commands`: copy-paste shell commands for next steps

```bash
bv --robot-triage        # THE MEGA-COMMAND: start here
bv --robot-next          # Minimal: just the single top pick + claim command

# TOON output (--format toon): a compact tabular encoding. Measured on this
# repository it is 7% smaller than JSON for --robot-graph but 9-15% LARGER for
# nested payloads (--robot-triage, --robot-plan, --robot-insights,
# --robot-label-health); use --stats to see both sizes before adopting it.
# TOON encoding shells out to the tru binary. With no encoder installed,
# --format toon prints a fallback warning, emits JSON with output_format "json",
# and --stats prints no sizes at all.
bv --robot-graph --format toon
bv --robot-triage --format toon --stats
```

Recommendations can include blocked or assigned work; `triage.quick_ref.top_picks` reflects snapshot readiness. A suggested action records its original local ID, working directory, and tracker route. Use that route rather than a namespaced display ID or an unrelated current directory. Inspect current tracker state before execution: analysis does not reserve work or guarantee that a later claim succeeds.

#### Other bv Commands

| Command | Returns |
|---------|---------|
| `--robot-plan` | Parallel execution tracks with unblocks lists |
| `--robot-priority` | Priority misalignment detection with confidence |
| `--robot-insights` | Full metrics: PageRank, betweenness, HITS, eigenvector, critical path, cycles, k-core |
| `--robot-alerts` | Stale issues, blocking cascades, priority mismatches |
| `--robot-suggest` | Hygiene: duplicates, missing deps, label suggestions, cycle breaks |
| `--robot-diff --diff-since <ref>` | Changes since ref: new/closed/modified issues |
| `--robot-graph [--graph-format=json\|dot\|mermaid]` | Dependency graph export |

Robot analysis commands default to JSON; `--format toon` selects TOON, and `--robot-help` defaults to text. In JSON mode, `--graph-format=dot` or `mermaid` puts diagram text in the `graph` field (`bv --robot-graph --graph-format=dot | jq -r .graph`).

#### Scoping & Filtering

```bash
bv --robot-plan --label backend              # Scope to label's subgraph
bv --robot-insights --as-of HEAD~30          # Historical point-in-time
bv --recipe actionable --robot-plan          # Pre-filter: ready to work (no blockers)
bv --recipe high-impact --robot-triage       # Pre-filter: top PageRank scores
```

### Tracker Commands for Issue Management

Use exactly one command family, matching the tracker configured for the repository.

#### Rust beads_rust (`br`)

Use `br` 0.6.0 or newer when executing saved claim commands. It rechecks
deferred status and future `defer_until` values when the claim runs, so a
recommendation captured before a deferral cannot bypass it. This requirement
applies to executing tracker claims.

```bash
br ready --json                       # Show issues ready to work (no blockers)
br list --status=open --json          # All open issues
br show <id> --json                   # Full issue details with dependencies
br create --title="..." --type=task --priority=2 --json
br update <id> --claim --json         # Claim for the current actor and start work
br close <id> --reason="Completed" --json
br close <id1> <id2> --reason="Completed" --json
br sync --flush-only                  # Export DB to JSONL after Beads mutations
```

#### Go Beads (`bd`)

```bash
bd ready --json                       # Show issues ready to work
bd show <id> --json                   # Full issue details
bd create "..." -t task -p 2 --json
bd update <id> --claim --json         # Atomically claim work
bd close <id> --json
bd dep add <issue> <depends-on>
bd export -o .beads/issues.jsonl        # Refresh the compatibility export read by bv
```

### Workflow Pattern

1. **Triage**: Run `bv --robot-triage` to find the highest-impact actionable work
2. **Verify**: Check the selected tracker's `show`/`ready` output before claiming
3. **Claim**: Use `br update <id> --claim --json` or `bd update <id> --claim --json`
4. **Work**: Implement the task
5. **Complete**: Use the selected tracker's `close` command
6. **Refresh for bv**: Run `br sync --flush-only` or the `bd export` command above so the JSONL export is current

### Key Concepts

- **Dependencies**: Issues can block other issues. `br ready --json` and `bd ready --json` show unblocked work.
- **Priority**: P0=critical, P1=high, P2=medium, P3=low, P4=backlog (use numbers 0-4, not words)
- **Types**: task, bug, feature, epic, chore, docs, question
- **Blocking**: Use `br dep add <issue> <depends-on>` or `bd dep add <issue> <depends-on>` to add dependencies

### Git Policy

Tracker commands do not grant permission to commit or push application code. Follow this repository's own git and tracker instructions before staging, committing, syncing, or pushing. If the repository says "commit only when asked," that rule overrides any generic workflow advice.

<!-- end-bv-agent-instructions -->
