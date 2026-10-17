# Open questions index — the cross-referenced queue

**What this is.** A single place to answer: *what is outstanding, and is the
answer already somewhere in this repository?* It exists because this project kept
losing time to an answer that was already present — in the MAME tree, in a
previous finding, in a closed bead — with nothing connecting the question to it.

**How an entry leaves.** Every entry has one of three exits, and it is not
allowed to just sit here:

| exit | when | where it goes |
|---|---|---|
| **drain** | the answer exists and is acted on | deleted; the action lands in the findings or the code |
| **graduate** | it becomes a real finding about the game | moved into `docs/mc68010-boot-findings.md` or `analysis/`, then deleted here |
| **promote** | it turns out to be a standing fact, not a question | moved into a reference doc (`docs/tms34010-reference.md`, `docs/mame-machine-reference.md`) |

Nothing is restated here as authority. Where an answer lives, the row points at
it (`findings §85.4`, `harddriv.cpp:628`) rather than copying it, because a third
copy of a fact is how a retracted claim stayed alive across three files
(`BUGS.md` MD-13; recorded in findings §83.4).

**The rule that makes it drain.** An entry must carry an **owner or next action**
(a bead id, or a named next check) and a **reconciled-on date**. An entry with
neither an action nor a candidate question does not belong in this file: it gets
promoted to a reference doc or deleted. An entry nobody has touched in several
sessions is visibly stale rather than quietly assumed current.

**How it was built, and its known limits.** Two read-only passes over disjoint
source sets, merged. Section A read the findings log, `STATUS.md` and the seven
highest-UNKNOWN `analysis/` notes — **the other 40 notes were sampled by marker
count and not read**, so this index is not exhaustive. Section B read
`QUESTIONS.md`, `BUGS.md` and the bead tracker. Every `ANSWER ALREADY EXISTS` and
"premise may now be wrong" row was then **independently re-verified before
merging**, and three of them did not survive; see the corrections section. No MAME
run was performed by either pass — every "the source says" row is a grep, a read
or arithmetic.

**Reconciled on:** 2026-10-17.

---

## Corrections applied during verification

Four claims were re-checked by hand before merging. Three did not survive. They
are recorded here rather than deleted, because the *shape* of the failure is the
useful part: **every one of them came from comparing two representations of the
same thing**, which is the same trap as MD-13.

**1. MD-12 is NOT "already fixed" — it is fixed only in the newer binary.**
Reading `tms34010.h:353` (`uint16_t m_IOregs[0x200]`, commented "Was 64") and
`34010ops.h:40` (`IOREG(reg) = (m_IOregs[(reg) & 0x1ff])`) suggests the defect is
gone. It is gone *from `/mnt/build/mame-ref`*. The frozen oracle
(`/mnt/build/mame-oracle/mame`, Oct 9) that produced most of `reference/` still
carries `m_IOregs[64]` and an unmasked `IOREG`. `analysis/mame-local-patches.json`
says so explicitly: the observer "is NOT a behavioural substitute for the
oracle". **MD-12 stays a live memory-safety defect and stays reportable.** Had
the first reading stood, we would have downgraded a real bug because we read the
wrong machine.

**2. There is no "MAME disassembler vs executor" divergence on JR condition
`0x0001`.** The disassembler prints `P` (`34010dsm.cpp:137`); the executor
computes `j_xx_0(!N_FLAG() && !Z_FLAG())` (`34010ops.hxx:1657`). Those are the
*same condition* — SPVU001A defines Positive on the 34010 as N=0 and Z=0. A
mnemonic and its own expansion were compared as if they were rival claims. This
would have become a false upstream bug report, which is the specific harm
`BUGS.md` exists to avoid.

**3. MD-09's "non-canonical `movea.l`" claim is retracted.**
`0x2279 & 0xf1ff == 0x2079` exactly — canonical `movea.l #<long>`, An=1, full
32-bit immediate. Musashi agrees on its own mask (`0x2279 & 0xf1c0 == 0x2040`,
`m68kdasm.c:3473`) and routes `0x2279` to MOVEA, not `move_16`. `BUGS.md` MD-09
has been corrected in place; the encoding is not a candidate explanation for the
"BAD GSP VRAM" symptom.

**4. The recorded MAME binary inventory is incomplete.** `reference/` metadata
names two MAME *executables*: the oracle `75b09ee4…` (×9) and
`fcb54790e1837ed6…` (`executable_sha256` in `adsp-program-snapshot.metadata.json`,
×2) — **the second is in neither recorded slot.** The recorded observer
`55956f2b…` appears in no committed metadata at all. So at least one committed
measurement came from a build this repository cannot name. `STATUS.md` corrected;
tracked as `stun-runner-w55`. (A loose grep also surfaces `mame_log_sha256` and
`native_binary_sha256` values; those are log and native-build hashes, not extra
MAME binaries — the count above is the deduplicated executable set.)

---

## Section A — the findings log, `STATUS.md`, and the analysis notes

## The rows

### F1 — MD-12 is already fixed in the pinned tree; the bug row and the triage hook both still describe the unfixed state

- **Question** — "**GSP register writes run off the end of `m_IOregs`**", status **unresolved**, "indexing confirmed; impact not", next step "**no — needs a sanitiser run first**" (`analysis/mame-bugs.md:28`); mechanism: "`internal_regs_map` exposes `map(0xc0000000, 0xc00001ff)` — 512 bytes of offsets — while the backing store is `uint16_t m_IOregs[64]` and `IOREG(reg)` is `m_IOregs[reg]` with no mask. The real S.T.U.N. Runner GSP program writes at offsets `0x90`–`0x160`, so it indexes 208 entries past the end." (`analysis/mame-bugs.md:80-83`)
- **Where the answer may already be** — three places, all read in this pass:
  1. `tms34010.h:350-353` — the declaration is now `uint16_t m_IOregs[0x200];` under a comment that reads "Indexed by IOREG() with a 0x1ff mask, covering the whole range internal_regs_map exposes. **Was 64**, which the GSP program overruns -- see the macro and BUGS.md MD-12."
  2. `34010ops.h:40` — `#define IOREG(reg) (m_IOregs[(reg) & 0x1ff])`. Both halves of the bug row's mechanism are false as written.
  3. `analysis/mame-local-patches.md:131-152` — "**The one behavioural change: MD-12** … The fix grows the array to `0x200` entries and masks `IOREG()` to `0x1ff`. Access below `0x40` is byte-for-byte unchanged … **This changes emulation behaviour** … Any measurement that depends on GSP internal register state at an offset of `0x40` or above is suspect until this is settled either way." Recorded in `analysis/mame-local-patches.json` under tag `MD-12-IOBOUNDS` with `observation_only: false`.
- **Status** — `ANSWER ALREADY EXISTS`
- **Bead** — none
- **Three stale artefacts this leaves behind, all pointing the same way:**
  - `analysis/mame-bugs.md:80-83` still states the unfixed 64-entry mechanism and still queues a sanitiser run for it.
  - The `STUNRUN-IOB` triage hook's own comment (`tms34010.cpp:1580-1586`) and the `purpose` field in `analysis/mame-local-patches.json` both say "m_IOregs holds only 64 entries and `IOREG()` indexes it unmasked". In the current tree it holds `0x200` and `IOREG()` is masked. The inventory faithfully records stale text as a record of what was written; the *tree* refutes it.
  - findings **§52.2** (`:9750-9777`) concluded from a null `STUNRUN_GSP_IOBREPORT` run that the `0x90`–`0x160` premise was "**stale or a misreading**". `analysis/mame-local-patches.md:138-140` says the opposite — "S.T.U.N. Runner's GSP program stores at offsets `0x110`–`0x160` … and **those fetches were traced executing**". See F3; this is the same contradiction, and it is why F3 is ranked first.
- **What MD-12 still legitimately asks** — `analysis/mame-local-patches.md:150-152` records the surviving question: whether the register file should be **128 four-byte-spaced registers rather than 512 bytes at two-byte spacing**. That is F3's hardware question, restated from the fix side, and it is not answered by the tree.
- **Verified how** — `sed -n '350,356p' src/devices/cpu/tms34010/tms34010.h`; `grep -n "define IOREG" src/devices/cpu/tms34010/34010ops.h`; `python3 -c` over `analysis/mame-local-patches.json`; `sed -n '131,152p' analysis/mame-local-patches.md`.

### F2 — STATUS.md says the oracle's local patches are unrecorded; the tree records and guards them

- **Question** — "**Still open from that work:** the pinned oracle binary is not stock MAME. `/mnt/build/mame-ref` carries local observation-only hooks in `harddriv_m.cpp`, `harddriv_v.cpp` and `tms34010.cpp`, and **this repository does not yet record them**. See the open question below." (`STATUS.md:192-195`)
- **Where the answer may already be** — `analysis/mame-local-patches.json`: eight patch records (`STUNRUN-DEBUG`, `STUNRUN-DUMP`, `STUNRUN-DPY`, `STUNRUN-HOSTPROBE`, `STUNRUN-OPLOG`, `STUNRUN-FETCH`, `VERSION`, `STUNRUN-IOB`), each with `observation_only: true` and an explicit file list covering all three files STATUS.md names. Plus `analysis/mame-local-patches.md` and the guard `tests/test_mame_local_patches.py`.
- **Status** — `ANSWER ALREADY EXISTS`.
- **Bead** — none
- **Verified how** — `python3 -m unittest tests.test_mame_local_patches` → **9 tests OK**, including `test_every_modified_source_file_is_recorded` ("A modified file the inventory never mentions") and `test_every_stunrun_tag_in_the_source_is_recorded`. Those two passing is the claim: a modified or unrecorded source file fails them.
- **Carried further than STATUS.md** — the inventory also separates two binaries the STATUS text does not mention: `observer_has_behavioural_changes: true` vs `oracle_has_behavioural_changes: false`, with the note that a measurement taken with `/mnt/build/mame-ref/mame` is *not* comparable to anything in `reference/` captured with the frozen oracle. That distinction is load-bearing for every §-number that quotes a `GSPREG` line.

### F3 — the TMS34010 I/O register-numbering question: two measurements in the tree do not reconcile

- **Question** — "**Not established:** which of the two address rules the real TMS34010 uses" — the vendored RTL's `idx = addr[8:4]` versus MAME's `offset = address - 0xC0000000`, a factor of 16 on the address and nothing on the register (findings **§52.4**, `docs/mc68010-boot-findings.md:9803-9821`; same question at **§47.6**, `:9190-9220` and **§47.10**, `:9332-9336`).
- **Where the answer may already be** — four places, in descending order of promise:
  1. **S9 (SPVU001A)**, the TMS34010 User's Guide's I/O register-file address table. This is the only real authority. Per `docs/sources.md:37` it is **not held locally** — cite by section, and do not imply a local copy.
  2. `third_party/tms34010_sv/rtl/core/tms34010_core.sv:4231` (`mem_addr = imm32;`) and `:3407` (`io_access_addr_q <= mem_addr;`) — see the contradiction below.
  3. `rtl/tms34010_pkg.sv:367-398` against `tms34010.h:63-96` — read in this pass: **the two implementations agree on the entire register ordering and numbering** (`IO_IDX_HSTCTLL = 5'h0F` = `REG_HSTCTLL = 15`, and so on through `REFCNT = 0x1F`). Only the address→index rule differs. So the disagreement is 16× on one expression, not two different register files.
  4. `third_party/tms34010_sv/PROVENANCE.md:31` — upstream's `third_party/TMS34010_Info` submodule, "carrying TI documentation", was **deliberately not vendored**. If the answer is in the RTL author's own notes, that is where they were.
- **Status** — `candidate, unverified`
- **Bead** — none (Bugs.md candidate, unfiled, deliberately — `BUGS.md` entries get reported upstream)
- **Contradiction, and the reason this is the highest-value row in the file.** I read `tms34010_core.sv:4231` and `:3407` in this pass. The bus address the core presents for an I/O access is the **instruction's 32-bit immediate**, latched unmodified. Therefore §47.6's address list (`0x000000 … 0x000160`, stride `0x10`) is a fact about the *uploaded GSP program's encoded immediates* — ROM data, not an artefact of our decode. Under MAME's rule (`tms34010.cpp:37`, the `.rw()` handler) an encoded `$C0000160` write arrives at an offset well above `0x40`, and §52.2's instrument is gated on exactly that: the `STUNRUN_GSP_IOBREPORT` file is `fopen`ed inside `if (offset >= 64)` (`tms34010.cpp:1596-1615`, recorded in the tree at `analysis/mame-local-patches.json` as tag `STUNRUN-IOB`). **No file was created.** But `analysis/mame-local-patches.md:138-140` asserts the program "stores at offsets `0x110`–`0x160` … and those fetches were traced executing". So the tree contains **two opposite claims about whether the GSP ever writes at a MAME-offset of `0x40` or above**, and F1 shows why the answer changed underneath both: MD-12's fix made the two MAME binaries behave differently, and the two claims were made against different binaries (see F28). I could not resolve which is right, and I am not going to guess: I re-ran neither, and the runs have different windows (§47.6 is an RTL-core run, §52.2 a 30 s oracle run).

### F4 — §52.4's "not filed" and §47.10's "should not be filed until…" are now stale

- **Question** — §47.10: the register-index BUGS.md candidate "should not [be filed] until `STUNRUN_GSP_IOBREPORT` has been run against the same traffic" (`:9335`). §52.4: "**Not filed:** the §47.6 BUGS.md candidate. The premise … is now contradicted by direct measurement, so there is nothing left to report upstream for that specific claim" (`:9810-9812`).
- **Where the answer may already be** — the condition §47.10 sets is met by §52.2's measurement; what remains is bookkeeping in `analysis/mame-bugs.md`, whose table row for MD-12 still reads **unresolved** with a sanitiser run as the next step.
- **Status** — `ANSWER ALREADY EXISTS` (that the condition is met and the candidate is not fileable), pointing at the same stale artefact as **F1**.
- **Bead** — none

### F5 — `$818040`: the window MAME maps does not contain it

- **Question** — "**What `0x818040` is.** One instruction, `move.w #$4,$818040.l`, data `0x0004`, never observed by the MAME tap that produced `adsp_control_seq.h`. Its data is non-zero where every recorded control write is `0x0000`." (findings **§77.9**, `:12223-12225`)
- **Where the answer may already be** — `harddriv.cpp:4830-4835`. Read in this pass: the multisync 68k map installs exactly three handlers in this neighbourhood — `0x810000-0x813fff` (ADSP buffer), `0x818000-0x81801f` (ADSP control), `0x818060-0x81807f` (ADSP IRQ clear). `$818040` is in the **gap between the last two**, and `multisync_68k_map` (`harddriv.cpp:599-616`) has no catch-all: `0xff8000-0xffffff` is RAM, and `$818040` as a 24-bit address is below `0xff8000`.
- **Status** — `candidate, unverified`. I established the MAME map's coverage; I did **not** determine what the real hardware does with an address in that gap, nor whether the observation came from a native model whose region is wider than MAME's (`native/musashi_cpu.c:524` claims `0x818000-0x81FFFF`, 8 KiB, against MAME's 32 bytes plus 32 bytes).
- **Bead** — none
- **Gloss** — the gap is real in the pinned source; whether it is a real gap or a modelling artefact is what is open.

### F6 — the 1-vs-54 LED write count: MAME's own handler is a documented no-op

- **Question** — "Why the native run sees **1** LED write where MAME sees 54. The dropped LED handler is the obvious candidate and is consistent with every other address matching, but it is a hypothesis." (findings **§77.9**, `:12217-12220`)
- **Where the answer may already be** — `harddriv_m.cpp:751-763`, `harddriv_state::hd68k_adsp_control_w`. Read in this pass: `case 0: case 1: /* LEDs */ break;` — an unconditional no-op that also ignores `data`, with the comment "bit 3 selects the value; data is ignored" at the top.
- **Status** — `candidate, unverified`. This **narrows** the question rather than answering it: MAME's handler drops the LED write too, so a MAME-side drop cannot by itself explain a count difference seen on a bus tap upstream of the handler. §77.9's own proposed instrument (`STUNRUN_WATCH_WRITE=0x818002` plus MAME's writer PC) is still the right next step. I did not trace the native side — `grep` for `0x818002` across `native/` and `reproduction/` returns nothing, so where our count of 1 is taken is itself unidentified.
- **Bead** — none

### F7 — the ADSP program-upload unit: three in-tree reads settle the driver half

- **Question** — "**`destination_word_index` and the recorded byte range in that metadata are not currently reconciled, and no claim that a 68010 window byte offset equals an ADSP program address is safe until they are.**" (findings **§82.3**, `:12939-12956`)
- **Where the answer may already be** — three reads, all made in this pass, that compose:
  1. `harddriv.cpp:4823` — `install_write_handler(0x800000, 0x807fff, write16s_delegate(…, hd68k_adsp_program_w))`, so the handler receives a **byte** offset.
  2. `harddriv_m.cpp:622-640` — `hd68k_adsp_program_w` indexes `m_adsp_pgm_memory[offset/2]` and, on `!(offset & 1)`, `COMBINE_DATA` into `oldword >> 16`, i.e. the **high** lane. An even byte offset takes the high lane; an odd one the low lane.
  3. `analysis/m1-landmarks.md:17` — the ROM's producer at `$02D35C` is `move.l D0,(A2)+`. A `MOVE.L` issues two 16-bit writes at offsets *k* and *k+2*, which are the **same parity** (gloss).
  - Composed: a 68010 `MOVE.L` to this window can only ever land in high lanes. The captured entry word's non-zero low half (findings §82.3) therefore cannot have come from `$02D35C`, which is the dichotomy §82.3 says it cannot yet choose between.
- **Status** — `candidate, unverified`. I did **not** establish where the low half *does* come from. That is the remaining half of §82.3 and it is now a much smaller question.
- **Bead** — none
- **Note** — `tools/disasm-adsp` consumes the capture and is independent of the window; §82.3 already says its output stands.

### F8 — `analysis/gsp-program-upload.md`'s untranscribed condition table is in the pinned tree

- **Question** — "`0xcb04`'s condition field is `0xb00`; the table above stops at `0x600` and the remaining codes were not read off `print_condition_code` before the end of this session — **the full condition list still needs transcribing**" (`analysis/gsp-program-upload.md:299-301`).
- **Where the answer may already be** — `/mnt/build/mame-ref/src/devices/cpu/tms34010/34010dsm.cpp:132-153`, `tms34010_disassembler::print_condition_code`. Read in this pass: all sixteen entries, `0x0000` → `0x0f00`.
- **Status** — `ANSWER ALREADY EXISTS`.
- **Bead** — none
- **Already corroborated** — findings §85.1 (`:13191-13194`) measured that all 18 JR condition variants occur in the real uploaded program, i.e. the table can be completed *and* checked against observed traffic in one pass. The note's own caveat says the table is what the decoder prints, so transcribing it is a copy, not an inference.

### F9 — §20.7's "Not yet ported" list is done

- **Question** — "Missing: selector resolution (`$25F6`-`$2604`), descriptor decode (`$2608`-`$262E`), and the loop nesting (`$262E`-`$269C`) … Filed as `stun-runner-txt1`." (findings **§20.7**, `:3484-3491`)
- **Where the answer may already be** — findings **§23** supersedes it, and `native/mc68010_text_writer.h:20,76` now carries the selector table with its `$25F6` provenance.
- **Status** — `ANSWER ALREADY EXISTS`.
- **Bead** — `stun-runner-txt1`
- **Gloss** — the caveat in §20.7 about `clr.w $608000` and `andi.w #$e7ff,$c00008` being part of the routine's observable effect still stands as a constraint on the port, not as a gap.

### F10 — §68.5's "the `$052F`-`$0544` scan is still untranscribed" is superseded

- **Question** — "The **`$052F`-`$0544` scan** that selects `I1`/`I2` before `$0564` is still untranscribed, so a record is derived from a supplied pointer pair rather than from game state." (findings **§68.5**, `:11069-11071`; repeated at **§67.7**, `:10975-10977`)
- **Where the answer may already be** — findings **§71** (`:11157-11234`), whose §71.1 reproduces the captured entry pointers for all nine records, and whose §71.5 re-lists what is still unmeasured.
- **Status** — `ANSWER ALREADY EXISTS`
- **Bead** — `stun-runner-alo` / `stun-runner-1fv` (the same lane)
- **Carried forward** — §68.5's other item, the `> $40` scaled branch of both helpers (`$05F5`/`$061F`), is **not** covered by §71 or §72 and remains genuinely open (see F16).

### F11 — §69.5's "the caller that fills the ring before `$0514`" is superseded

- **Question** — "Nothing between `DM($0036)` and the emitted record. The remaining unknowns are elsewhere: **the caller that fills the ring before `$0514`**, the `$0631` tail after `$05DC`, and the `> $40` scaled branch of the two helpers." (findings **§69.5**, `:11235-11240`)
- **Where the answer may already be** — findings **§72** (`:11242-11394`), which measured four writers and found only one survives; **§72.5** lists what a C replacement still needs, which is the successor list to §69.5's.
- **Status** — `ANSWER ALREADY EXISTS` for the ring-caller item. The `$0631` tail is not mentioned by §72 and is not claimed answered here.
- **Bead** — `stun-runner-alo` / `stun-runner-1fv`

### F12 — §39.6 item 1 vs §50 vs §53.3: the file contradicts itself on colour parity

- **Question** — §39.6: "**The colours are unvalidated.** … The obvious next check is service-mode palette equality against `reference/experiments/stunrun/gsp-direct-share-frame600-service/`" (`:7808-7814`).
- **Where the answer may already be** — findings **§50**, titled "Colour parity against MAME: **no**. The open question is now answered" (`:9521-9523`), and findings **§53.3**, which says the opposite about §50's own open question: "§50's open question — scanout geometry vs unfilled share — is **not** answered" (`:9894`). §81 (`:12795`) then re-localises the fault to the palette.
- **Status** — `ANSWER ALREADY EXISTS` (a measurement exists and it is negative), but with a live internal contradiction between §50 and §53.3 that I could not adjudicate from the text alone. §53.3 states the reason it disagrees: the committed fixture it tested rasterises blank, so its own comparison cannot attribute the failure. **The attribution question — scanout geometry, unfilled share, or something else — is still open, and §50's heading overstates.**
- **Bead** — none
- **What I did not conclude** — that one of the two is wrong. They may be about different questions; §50 measures "does our frame match MAME's" and §53.3 asks "which subsystem is at fault". Do not merge them.

### F13 — §44 / §39.6 item 2 (the sheared image) is superseded by §80

- **Question** — "**The image is sheared.** The horizontal bands are skewed, which is geometric, not a palette problem. Row-to-row advance follows `dpyadr_advance()` … so the suspect is the starting `rowaddr`/trajectory rather than the advance." (findings **§39.6**, `:7815-7818`)
- **Where the answer may already be** — findings **§80**, titled "The frame-400 scanout fault was the frame ORIGIN, and the 'oracle registers' were two different machines" (`:12559`), with §80.4 naming the one real renderer defect and §80.5 recording that the evidence set mixed two machines (`-nvram_directory`, not `-cfg_directory`). Findings **§38** (`:7398`) had already retired the row-base hypothesis for the units question.
- **Status** — `candidate, unverified`. I have not re-run §80's checks and have not established that the specific frame-600 shear §39.6 saw is the frame-400 fault §80 fixed. Read §80.4 and §80.6 before treating this as closed.
- **Bead** — none

### F14 — §6 item 3 (Musashi `RESET`) — the recorded fact still holds; the "does it matter" half does not

- **Question** — "**Musashi `RESET` is a no-op.** … MAME's RESET does reset peripherals (`harddriv_state::machine_reset`). The ROM executes `reset` at 0x228 on every pass through the boot loop, so this could matter." (findings **§6**, `:388-392`)
- **Where the answer may already be** — the premise, re-verified: `third_party/musashi/m68kconf.h:135-136` still reads `M68K_EMULATE_RESET M68K_OPT_OFF`. The question is whether the native shell's device-reset path covers what `harddriv_state::machine_reset` does; `native/musashi_cpu.c` has the region model to compare against.
- **Status** — `candidate, unverified`
- **Bead** — none
- **Note on the source** — §6 is headed "**Historical** investigation queue; not a current implementation plan" and says later sections supersede its boot and dump assumptions. §6 items 1 and 6 are likewise historical (§1 refutes the service-bit polarity; item 6's `/tmp/or600` does not exist). Item 4's five `jsr` targets are checkable against the static-listing recipe recorded in `reference/listings/stunrun.metadata.json`; I did not check them. **Item 5's `$8090`/`$8092` ADSP status words: `no candidate found`** in the notes I own.

### F15 — `analysis/gsp-program-upload.md`'s `m_gsp_speedup_addr` UNKNOWN: the expression is one grep away, and the arithmetic lands somewhere surprising

- **Question** — "**Whether MAME's watched address is right is UNKNOWN.** `m_gsp_speedup_addr` is a `uint16_t*`, and `(0xfff9fc00 - 0xffc00000) >> 4` uses `0xffc00000`, which is the *driver* board's base, while S.T.U.N. Runner is on *multisync*, mapped at `0xff800000`. … **Establishing it needs the actual share index arithmetic worked out from MAME's region setup, not from reading the expression.**" (`analysis/gsp-program-upload.md:244-250`)
- **Where the answer may already be** — `harddriv.cpp:5093-5094`, inside `init_stunrun()`. Read in this pass: the note's factual premises all hold — `init_stunrun` uses `0xffc00000` (`harddriv.cpp:5093`), `init_harddriv` uses `0xff800000` (`:5042`), and stunrun's own GSP map is at `harddriv.cpp:628`.
- **Status** — `candidate, unverified`
- **Bead** — none
- **Why this is worth the check** — the arithmetic the note asks for, done here and *not* claimed as a conclusion: with `0xffc00000` the index is `0x39fc0` = 236,992, which **is** inside the 262,144-word / 512 KiB share §34.1 established. With the `0xff800000` base that §34.2's own conversion implies, the index is `0x79fc0` = 499,136, which is **outside** it. So neither constant is obviously the right one, and "fix it to `0xff800000`" is not obviously the fix. The remaining step is what MAME allocates for the share under `.mirror(0x0400000)` — which is why this stays unverified.

### F16 — §85.2's 224 wholly-unknown ADSP addresses: §85.5's rule has not been applied to this class yet

- **Question** — "**We cannot yet tell which of the 224 are data and which are code MAME cannot decode**, and that distinction matters" (findings **§85.2**, `:13217-13219`); "That is still `stun-runner-yuv`, and it is why 224 addresses are still unclassified" (**§85.5**, `:13277-13279`).
- **Where the answer may already be** — `tools/disasm-adsp:96-97` names the exact MAME source lines that emit `??? (%06X)`: `2100dasm.cpp:274, 310, 317, …`. §85.4 resolved the *other* unclassified class (the 22 partial decodes) from `2100ops.hxx`; the 224 have not had that treatment. I confirmed the counts the rule rests on: `grep -c "case 0x"` gives **213** in `2100ops.hxx` and **49** in `2100dasm.cpp`, and MAME's ADSP disassembler's opcode switch is exhaustive over all 256 top-byte cases — it never declines an *opcode*, only MAME's `unsupported` dasmflag fires.
- **Status** — `candidate, unverified`
- **Bead** — `stun-runner-yuv`
- **Note** — §85.5's caveat is load-bearing and must travel with this row: the executor settles what an encoding *computes*, never whether an address was ever *fetched*. The data-vs-code split is the half the executor cannot answer.

### F17 — §62's non-reproducing capture: the provenance is a file read, not an investigation

- **Question** — "the cause is UNKNOWN: possible difference in the old capture's invocation, input/harness state, or runtime configuration. **We need its full provenance before using it to assert a live F1 field value.**" (findings **§62**, `:10617-10621`)
- **Where the answer may already be** — the committed 2026-10-10 capture's own `metadata.json` under `reference/experiments/stunrun/`, which per `EVIDENCE.md` is supposed to carry invocation, harness identity and start condition. Read it and diff against the failing command quoted in §62.
- **Status** — `candidate, unverified`
- **Bead** — `stun-runner-q9t` / `stun-runner-alo`
- **Do not** — treat §62's five-run result as refuting the committed capture. §62 says so itself, and AGENTS.md trap 4 (run five times, treat `--input` frames as mode-specific) applies.

### F18 — §77.9's IRQ5 21-delivery drop: §47.3 already flagged it as unexplained

- **Question** — "**IRQ5 does not reach `0x0222DE` before frame 364** in this native run … Not established." (findings **§77.9**, `:12227-12231`); and separately "why 21 fewer were taken is **UNKNOWN**" (**§47.3**, `:9107-9112`).
- **Where the answer may already be** — these are almost certainly the same observation, recorded twice from two runs (§47.3 from a 600-frame RTL run, §77.9 from a native run). `BUGS.md` SR-08 places the handler's `addq.l` at `$ff9042.l` / `$0x82C`-`$8C0` rather than at `$0222DE`, which §77.9 already notes — so the first thing to settle is which counter each number is reading.
- **Status** — `candidate, unverified`
- **Bead** — none

### F19 — `analysis/interconnect.md`'s IRQ-state-window and interrupt-direction UNKNOWNs

- **Question** — "UNKNOWN: whether the IRQ-state read window carries traffic after a verified gameplay transition or is only a modeled/debug status interface" (`analysis/interconnect.md:160-161`); and "UNKNOWN: interrupt/flag direction and acknowledgement sequence" (`:223`).
- **Where the answer may already be** — `docs/mame-machine-reference.md` (1,015 lines) for the window's address range and MAME's handler set; `analysis/interconnect.md:153-159` already records that in the bounded title path the 68010 writes the ADSP control window 73 times and **reads `0x838000–0x83ffff` zero times**, with two IRQ-clear writes from named PCs. That is the negative result; the open half is the post-gameplay half.
- **Status** — `candidate, unverified`
- **Bead** — none
- **Prerequisite** — this needs a run that reaches *verified gameplay*, and `analysis/interconnect.md:191-222` records three bounded negative results establishing that the currently available schedules do not. Resolve that first or the check is not runnable.

### F20 — §74.3's input-schedule finding: `drive_hold` still does not start the game, and the row is still true

- **Question** — "`drive_hold` never starts the game … Frame 1200 under it is the **attract** state, and its road buffer is byte-identical to an `--input none` run." (findings **§74.3**, `:11605-11610`)
- **Where the answer may already be** — `tests/test_input_modes.py:62,88` (the mode is still in the input-mode registry) and `QUESTIONS.md:410` (the schedule's definition). `course_sweep` is the recorded alternative that does reach gameplay, at frame 3207.
- **Status** — `candidate, unverified`
- **Bead** — none
- **Why it is in an index of open questions** — this is not a knowledge gap, it is a **trap**: `drive_hold` is a registered input mode that silently produces attract state. Anything that picks a schedule by name from the registry can pick it. §74.1 compounds it — the buffer it names is 384/384 verbatim ROM.

### F21 — `analysis/gsp-road-fifo-consumer.md`'s UNKNOWNs: the note is the candidate, and it is still open

- **Question** — "the GSP-side consumer's destination buffers as UNKNOWN" (findings **§75.4**, `:11702-11705`), carried in `analysis/gsp-road-fifo-consumer.md` (six UNKNOWN markers, including "buffer contents and field semantics UNKNOWN", `:42`, and "payload and semantics remain UNKNOWN", `:154`).
- **Where the answer may already be** — `analysis/gsp-road-fifo-consumer.md` itself, and findings **§76.3**, which transcribes what `$0xFFF48C20` does byte-exact against the oracle.
- **Status** — `no candidate`
- **Bead** — none
- **Why `no candidate` and not `candidate, unverified`** — §75.4 itself names the next step ("decoding what `0xFFF48C20` does with the stream") and §76.3 already did the byte-exact part of it, so the remaining work is a disassembly the tree does not contain. Per AGENTS.md, a read disassembly is not a deliverable; this is one routine's worth of new transcription, not a lookup.

### F22 — §66.6 header word order: a fixture limit, and the fixture is named

- **Question** — "**Header word order is not discriminated by the capture.** All nine records latch `DM(I1,M1)` and `DM(I2,M2)` to equal values, so `start_x` and `delta_x` are indistinguishable by value. … This is a limit of the fixture, not a finding about the hardware." (findings **§66.6**, `:10861-10866`)
- **Where the answer may already be** — §66.6 says the order rests on the disassembly and that the test carries a labelled synthetic record to stop a swap passing silently. So the answer is the producer disassembly at `$0565`/`$0567` (§66.1/§66.2), not a new capture.
- **Status** — `candidate, unverified`
- **Bead** — `stun-runner-alo` / `stun-runner-1fv`
- **Do not** — weaken the test to match. The synthetic record is the control that makes the unverified order safe to carry.

### F23 — §54.5 / §55.5 / §57.4 / §58.4: SOM consumer semantics, and the list is shorter than it looks

- **Question** — §54.5: "Which field is which vertex/slope/lighting term; whether the records are PIXBLT parameters at all … **None of that was measured — the consumer disassembly has not been done.**" (`:10007-10012`). §55.5 lists what the consumer disassembly still has not covered (`:10109-10116`); §57.4 and §58.4 re-scope it after the FILL retractions (`:10266-10277`, `:10360-10368`).
- **Where the answer may already be** — §57 retracted the PIXBLT reading entirely (`:10180`) and §59 mapped the primary record fields to FILL XY operands (`:10369-10458`), so §54.5's "whether the records are PIXBLT parameters at all" is **answered — no**. §60 (`:10459`) took the rest of the primary record as a stepping program. What §58.4 asks for specifically — the FILL worker's word3-6 updates into A8/A6/A7/A9/A10/B0-B2/B7, the `$FFF48FC0` validator, and the `$FFF45800` alternate path — has no successor section.
- **Status** — `ANSWER ALREADY EXISTS` for the §54.5 sub-questions; `no candidate` for §58.4's three targets.
- **Bead** — `stun-runner-q9t`
- **Note** — I am not claiming §54.5's record-shape items are all closed. §59 and §60 cover the *primary* record; §55.5's list (record types beyond the `$344D50`/`$344D60` branch, `$0x344D10`, `$0x345800`, the `$0xFFF71D20` constant) is not.

### F24 — `analysis/track-storage.md`'s remaining list, and one named self-candidate

- **Question** — "Whether one 384-word table is one segment, a ring of segments, or a command block … Why the producer selects each shared ROM slot for every progression state … How the GSP FIFO words become road pixels …" (`analysis/track-storage.md:555-567`)
- **Where the answer may already be** — findings **§75** and **§76** both touch this seam and §76.9 (`docs/mc68010-boot-findings.md:11916-11926`) re-lists what §76 did *not* establish, including "Which field of the window, if any, the subtract/add pair is ramping" — the same question `track-storage.md:565` states for the tunnel-mouth data. §76.9 names its own candidate: the `0x0298C0` writes in `track-storage.md`.
- **Status** — `candidate, unverified` for the ramp/field item specifically; `no candidate` for the rest of `track-storage.md:555-563`.
- **Bead** — none

### F25 — `analysis/hardware-lineage.md`'s modified-board UNKNOWN

- **Question** — "**UNKNOWN** whether S.T.U.N. Runner ran a modified Main Board. Margolin never mentions our title, and our MAME model treats it as stock MultiSync-nomsp. Nothing in SP-338 or our traces suggests otherwise, but absence of evidence from a source that never discussed the game is weak." (`analysis/hardware-lineage.md:168-171`)
- **Where the answer may already be** — **no candidate found.** S3 does not discuss the title; S6 (SP-338) is the board authority and records no deviation; MAME's `multisync` model is the thing in question, not a witness to it. Every source that could settle it is silent for the same structural reason the note gives.
- **Status** — `no candidate`
- **Bead** — none
- **Leave it `UNKNOWN`.** This is the row most likely to be closed by a plausible-sounding inference. Per `docs/sources.md` rule 6, a claim that cannot be sourced is `UNKNOWN`, and an incorrect citation is worse than none.

### F26 — `analysis/interconnect.md`: credit count, command framing, and the first input-dependent submission

- **Question** — three NARROWED/UNKNOWN items: whether the normal Coin 1 path leaves the displayed credit count at zero (`:204-208`); "the FIFO sink is now runtime-observed, but command framing, synchronization, and the first input-dependent/gameplay submission remain unresolved" (`:253-255`); and the ADSP upload's "source table/buffer semantics and exact instruction-level normalization of the paired `MOVE.L` halfword taps" (`:146-149`).
- **Where the answer may already be** — the third of these is now reachable through **F7** above: the `MOVE.L` halfword taps are the same object §82.3 could not reconcile. The first two have no in-tree candidate I could find — both need a schedule that reaches verified gameplay, which F19's prerequisite and F20 both block.
- **Status** — `candidate, unverified` for the upload-normalization item (via F7); `no candidate` for the other two.
- **Bead** — none
- **Gloss** — the "first input-dependent GSP submission" keeps failing to appear because the schedules that would produce it do not reach gameplay; that is one blocker wearing three question-shaped hats.

### F27 — §85.3's unreconciled ADSP nonzero-word count

- **Question** — "§'Completed baseline slices' records this tool capturing 'a repeatable full ADSP program image at frame 600 (**2,718 nonzero words**)'. Today's fresh capture of the same tool, same frame, same ROM set, reads **4,356**. … **Neither number should be trusted until it is reconciled.**" (findings **§85.3**, `:13281-13293`)
- **Where the answer may already be** — §85.3's own hypothesis was `analysis/mame-local-patches.md`'s statement that the two binaries are not interchangeable, and that section (`:162-200`) supplies exactly what §85.3 lacked: the two binary hashes and mtimes (oracle `75b09ee4…`, 2026-10-09; stunrun-observe `55956f2b…`, 2026-10-14), and "**The committed captures predate the `mame-ref` rebuild.**" See F28.
- **Status** — `ANSWER ALREADY EXISTS` (the cause is recorded: different binaries, different dates). **Not** reconciled as a *measurement*: the 2,718 capture's metadata does not appear to name its binary, and §85.3 notes `raw_artifacts_kept_local` means the image is not committed, so the two images cannot be diffed byte-for-byte from the tree. The row the patch file asks for — `reference/checkpoints/title/metadata.json` naming the binary that produced its capture — is recorded as wanted but not done (`analysis/mame-local-patches.md:202-207`).
- **Bead** — none
- **Action this points at** — `reference/checkpoints/title/metadata.json` and `fixtures/oracle/frame_600/metadata.json` are the two records that should name a binary. Both currently do not. That is a two-file edit with no ambiguity in it.

### F28 — the frame-600 oracle fixture no longer reproduces on either binary, and its metadata misdescribes it

- **Question** — findings **§53.3**: "the committed fixture is not the frame-600 framebuffer (it holds 5058 nonzero words of 262144 …)", and §53.4: "Until that pair exists, no scanout-parity claim is reachable from committed evidence, and §50's percentage differences should be read as 'our output differs from MAME's' with no attribution" (`:9886-9907`).
- **Where the answer may already be** — `analysis/mame-local-patches.md:162-200`, "Which binary produced the committed oracle, and why frame 600 differs today". Read in this pass; it records four things §53 does not:
  1. `fixtures/oracle/frame_600/frame_600_screen.png` (byte-identical to `reference/experiments/stunrun/normal-frames-1-600/screens/frame-000600.png`, `sha256 6d46e033e29b049e…`) shows **self-play** — 3D tunnel, ship, HUD.
  2. `fixtures/oracle/frame_600/metadata.json` describes that same frame as **"INSERT 1 COIN title/attract screen"**. "The description is wrong; the pixels are not in question."
  3. Running `tools/mame-gsp-state-snapshot` with the same `--input none` and the same frame targets against **either** binary today produces a **title screen** at frame 600 (`sha256 29f95e8820efecde…`). So the committed frame is not reproducible from the tree as it stands.
  4. The cause is F1's MD-12: "MD-12 is a defect in the *driver's* GSP register write path … A fix there changes GSP I/O behaviour on exactly the hardware the title screen depends on."
- **Status** — `ANSWER ALREADY EXISTS` — and it is load-bearing for three other rows, none of which mention it:
  - **F12.** §50's 99.81%-differing measurement compares our frame-600 output against `fixtures/oracle/frame_600/frame_600_screen.png`. That PNG is self-play and does not reproduce on either binary today, so §50's percentage has no stable reference.
  - **F13.** §80's frame-400 work and §39.6's shear are both frame-600/400 picture questions on the same fixture line.
  - **F27.** The 2,718-vs-4,356 count gap is the same rebuild.
- **Bead** — none
- **The rule this record states, which no section in the findings log currently enforces** — "**Any comparison must state which binary produced each side, or it is meaningless.**" That is a rule about *provenance*, and `EVIDENCE.md` is where it belongs. I have not checked whether `EVIDENCE.md` already carries it; I did not read that file this pass. **UNKNOWN whether the rule is already written down elsewhere.**
- **Caution** — this does not make §53's conclusion wrong. §53 found the fixture rasterises blank with MAME's own registers; this finds that the *screenshot* next to it shows a different scene than its own metadata claims, and that the scene does not reproduce. Both are reasons the frame-600 parity line is not settled, and they are different faults.

---

### Ranked summary (section A)

### 1. `ANSWER ALREADY EXISTS` — verified in this pass

Ten rows. In rough order of value:

| # | Row | What is already there | Why it matters |
|---|---|---|---|
| F1 | MD-12 | **Already fixed in the pinned tree** (`tms34010.h:353` = `m_IOregs[0x200]`, `34010ops.h:40` masks to `0x1ff`), recorded as the one behavioural patch in `analysis/mame-local-patches.md:131-152` | `BUGS.md` entries get reported upstream. `analysis/mame-bugs.md` is still describing the unfixed state and still queueing a sanitiser run for it. |
| F28 | the frame-600 fixture | `analysis/mame-local-patches.md:162-200`: the PNG shows self-play, its metadata says title/attract, and it does not reproduce on **either** binary today | Load-bearing for F12, F13 and F27, none of which mention it. It changes what a whole class of frame-600 comparison means. |
| F2 | STATUS.md's unrecorded-patch question | `analysis/mame-local-patches.json` + a 9-test guard that **passes**, including "a modified file the inventory never mentions" | Free to close, and STATUS.md is the file most-read at handoff. |
| F12 | §39.6/§50/§53.3 colour parity | A negative measurement, plus an internal contradiction about what it means, plus F28 removing its reference frame | Cheapest row here to reconcile, and the contradictions are the real content. |
| F23 | SOM consumer semantics | §57/§59/§60 answer §54.5's sub-questions | Splits one long "not established" list into a closed part (§58.4, listed in section 3) and a genuinely open part. |
| F27 | §85.3's 2,718-vs-4,356 | The cause, with both binary hashes and mtimes | Reconciled as *cause*, not as measurement — the images are not committed. |
| F10 | §68.5's `$052F`-`$0544` scan | Findings §71 | Direct transcription work removed from a bead's critical path. |
| F11 | §69.5's ring caller | Findings §72 | Same. |
| F9 | §20.7's "Not yet ported" | Findings §23 and `native/mc68010_text_writer.h` | Closes `stun-runner-txt1`'s recorded gap. |
| F8 | `gsp-program-upload.md`'s condition table | `34010dsm.cpp:132-153`, all 16 entries, already corroborated by §85.1 | Pure transcription, zero risk, self-checking. **Start here.** |
| F4 | §47.10's filing precondition | Met, and F1 shows the candidate is now moot for the reason §47.10 gave | Bookkeeping, but it is part of what kept F1's stale text alive. |

**Nothing in this section is a guess.** Every row was re-checked by reading the
cited code in this pass; F2's was additionally *executed* (the guard test run).
If a row here turns out to be wrong when you re-verify it, the failure is in a
file I quoted, not in the reading — quote the line and re-read it.

### 2. Candidates worth checking — ordered by how cheap the check looks

Cheapest first. Every one of these is a file read or a single grep.

1. **F3** — the TMS34010 I/O register-numbering question. **Ranked first among
   candidates despite being the hardest**, because it is not a plain gap: I found
   **three** recorded claims in the tree that do not line up (§47.6's program
   address list, §52.2's null `STUNRUN_GSP_IOBREPORT` run, and
   `analysis/mame-local-patches.md:138-140`'s "traced executing"), and F1/F28
   explain why the answer changed underneath the log. What settles it is S9's
   register-file address table — not held locally — so it may not settle cheaply.
   Read F1 and F28 before touching it.
2. **F5** — `$818040` vs `harddriv.cpp:4830-4835`. One grep settles whether the
   address is in any mapped window. Already narrowed; the gap is visible in the
   pinned source.
3. **F8** — transcribe 16 table rows out of `34010dsm.cpp` and check them against
   §85.1's observed variants. (Listed here as well as in section 1 because the
   *check* is cheap even though the answer is already in the tree.)
4. **F15** — `m_gsp_speedup_addr`: the expression is read; the remaining question
   is what MAME allocates for the share under `.mirror()`, one region-size
   lookup.
5. **F6** — LED write count. The MAME half is settled; the native half is one
   grep for where our count is taken (`0x818002` appears nowhere in `native/` or
   `reproduction/`).
6. **F7** — ADSP upload unit. Three in-tree reads already settle the driver half;
   the remainder is "where does the low half come from".
7. **F14** — §6 item 3. Re-verify the premise (done: `m68kconf.h:135-136` still
   off), then compare against `harddriv_state::machine_reset`.
8. **F17** — §62's non-reproduction. Read the committed capture's `metadata.json`
   and diff against the failing command.
9. **F22** — §66.6 header order. The producer disassembly is named in the source;
   the synthetic-record control makes carrying the answer safe.
10. **F27** — the two-file edit F27's row names:
    `reference/checkpoints/title/metadata.json` and
    `fixtures/oracle/frame_600/metadata.json` should each name the binary that
    produced them.
11. **F20** — `drive_hold`. Not a knowledge question but a live trap in the
    input-mode registry.
12. **F16** — the 224 ADSP addresses. Per-address work, but each step is the
    §85.5 rule and `tools/disasm-adsp:96` names the exact MAME lines.
13. **F18** — the IRQ5 delivery drop. First step is deciding which counter each
    of the two records is reading.
14. **F19**, **F26** — both blocked behind "a schedule that reaches verified
    gameplay", which none of the current ones does.
15. **F13** — §39.6's shear. Re-run §80's checks, and read F28 first: the
    reference frame may not be the one §80 used.

### 3. Genuinely open — no candidate found

These need new evidence or new work. They are listed so that nobody re-derives them from scratch and then reports them as new.

| # | Question | Why there is no candidate |
|---|---|---|
| F25 | Did S.T.U.N. Runner run a modified Main Board? | Every source that could witness it is silent for the same structural reason. **Leave `UNKNOWN`** — this is the row most likely to be closed by a plausible-sounding inference. |
| F21 | GSP-side road-FIFO consumer semantics | §76.3 did the byte-exact part; what remains is a disassembly the tree does not contain. One routine's worth of new transcription. |
| F24 | `track-storage.md`'s table-structure and producer-selection questions | No in-tree candidate; needs capture, not lookup. The ramp/field sub-item does have one (F24). |
| F26 | Credit-count semantics; FIFO command framing; first input-dependent GSP submission | Blocked, not unknown — the schedules that would produce the evidence do not reach gameplay (F19, F20). |
| F23 | §58.4's three targets: FILL worker's word3-6 register updates, `$FFF48FC0` validator, `$FFF45800` alternate path | No successor section; new disassembly. |
| F14 | §6 item 5's `$8090`/`$8092` ADSP status words | Not present in the notes or `STATUS.md`. Genuinely unlocated. |

---

## Method and limits of this pass

**What I verified, and how.** Every `ANSWER ALREADY EXISTS` row rests on a read in
this session, not on recall:

- `analysis/mame-local-patches.json` — parsed with `python3 -c` and printed
  field by field; `analysis/mame-bugs.md:28, 76-90` — read.
- `tests/test_mame_local_patches.py` — **run**:
  `python3 -m unittest tests.test_mame_local_patches` → 9 tests, OK.
- `34010dsm.cpp:132-153` — read (`print_condition_code`, all 16 cases).
- `2100dasm.cpp` / `2100ops.hxx` — counted: 49 vs 213 `case 0x`.
- `harddriv_m.cpp:751-813` (`hd68k_adsp_control_w`), `:622-640`
  (`hd68k_adsp_program_w`) — read.
- `harddriv.cpp:599-616, 619-628, 4822-4835, 5042-5043, 5082-5094, 5375`
  — read.
- `tms34010.cpp:34-41, 1568-1615` and `tms34010.h:63-96` — read.
- `tms34010.h:350-356` (`m_IOregs[0x200]` and its comment) and
  `34010ops.h:40` (`#define IOREG(reg) (m_IOregs[(reg) & 0x1ff])`) — read.
- `analysis/mame-local-patches.md:121-160, 162-217` — read.
- `third_party/tms34010_sv/rtl/io/tms34010_io_regs.sv:146-168`,
  `rtl/tms34010_pkg.sv:363-406`, `rtl/core/tms34010_core.sv:3140-3430` — read.
- `third_party/musashi/m68kconf.h:132-136` — read.
- `analysis/gsp-program-upload.md:262-305`, `analysis/interconnect.md:140-269`,
  `analysis/track-storage.md:550-588`, `analysis/mame-bugs.md`, `analysis/hardware-lineage.md:160-175`
  — read.
- `STATUS.md` (whole), findings §§6, 20.7, 22.10, 26.13, 31.548E.8-9, 39.6, 44,
  47.3, 47.6, 47.9-47.10, 50, 52.1-52.4, 53.1-53.4, 54.5, 55.5, 57.4, 58.4,
  61.4, 62, 66.6, 67.7, 68.5, 69.5, 71-72, 74.3, 75.4, 76.9, 77.9, 80.9, 81.4,
  82.3, 84.1-84.2, 85.1-85.5 — read.

**Bucket counts.** 28 rows: **10** `ANSWER ALREADY EXISTS` (F1, F2, F4, F8, F9,
F10, F11, F12, F27, F28), **14** `candidate, unverified` (F3, F5, F6, F7, F13,
F14, F15, F16, F17, F18, F19, F20, F22, F26), **2** `no candidate` (F21, F25),
and **2 rows whose status is split across sub-questions** — F23 (`ANSWER ALREADY
EXISTS` for §54.5's list, `no candidate` for §58.4's three targets) and F24
(`candidate, unverified` for the ramp/field item, `no candidate` for the rest).
The split rows say so at the row.

F3 is deliberately in section 2, not section 1. It is the most valuable row in
the file and it is **not** an `ANSWER ALREADY EXISTS` row: I did not find the
answer to the register-numbering question, I found that three recorded claims
about it disagree. Promoting it would be exactly the failure mode this file
exists to catch.

**What I deliberately did not conclude.**

- **F3.** I did not decide which I/O address rule is correct. I found three
  recorded claims that do not line up and stopped there. The runs have
  different windows and different binaries; I re-ran none of them, and guessing
  here would repeat MD-13.
- **F1 / MD-12.** I established that the fix is applied and recorded. I did
  **not** judge whether the fix is *correct* — `analysis/mame-local-patches.md:150-152`
  itself flags the surviving question (128 four-byte registers vs 512 bytes at
  two-byte spacing), and that is F3, not something I closed. Nor did I re-derive
  whether the GSP really writes at `0x110`–`0x160`; the patch file's claim and
  §52.2's null result disagree and that disagreement is reported, not resolved.
- **F15.** I did the arithmetic the note asks for and stopped short of calling
  the expression wrong, because whether an index is in range depends on a
  region size I did not read out of the machine config. The numbers are in the
  row so the next reader can check them, not so they can be taken as a verdict.
- **F7.** I established what the driver *can* do; I did not establish where the
  observed non-zero low half came from, and F7 is explicitly a candidate for
  that reason.
- **F28.** I did not re-run `tools/mame-gsp-state-snapshot` against either
  binary, and I did not open the PNG. Every claim in that row is quoted from
  `analysis/mame-local-patches.md:162-200`, which I read. I also did not check
  whether `EVIDENCE.md` already carries the provenance rule F28 quotes, so that
  one stays `UNKNOWN`.
- **F13.** I did not treat §80 as having answered §39.6's shear. Different
  frame, different reference image per F28, and I re-ran nothing.
- **F16.** I did not classify a single one of the 224 addresses. §85.5's caveat
  — the executor cannot say whether an address was *fetched* — means the
  data-vs-code split is out of reach of the route I am pointing at.
- **F12.** I did not pick a winner between §50 and §53.3. They may be about
  different questions.
- **`m_IOregs` bounds (F1).** I did not re-derive MD-12's out-of-bounds claim.
  §52.2's null result and the MAME source I read (`IOREG()` unmasked at
  `tms34010.cpp:1576-1577`) are compatible with *both* "no out-of-bounds access
  occurred" and "one did not fire the report" — I could not separate them, and
  the row says the retraction stands and the bug file is the stale one, not that
  MD-12 is void.

**Known gaps in this section.** It covers the findings log, `STATUS.md`, and the
seven highest-UNKNOWN `analysis/` notes named in the brief. The remaining 40
notes in `analysis/` were sampled by UNKNOWN-marker count and not read, so
questions recorded only in them are absent here — with one exception worth
naming: I read `analysis/mame-local-patches.md` and `.json` because F2 pointed
at them, and they turned out to carry more recorded answers than the brief's
UNKNOWN counts suggest, which is itself an argument for reading a note by
relevance rather than by marker count. `BUGS.md` was read for MD-12 and MD-13
only; its SR-* and other MD-* entries are another agent's section. `EVIDENCE.md`,
`QUESTIONS.md`, `TESTING.md` and `PREFLIGHT.md` were not read.

**Citation register.** External documents are cited by their `S#` per
`docs/sources.md`: **S3** (Margolin, F24's ramp source and F25's silence),
**S6** (SP-338, F25), **S9** (SPVU001A — F3's only real authority, **not held
locally**, cited by section). **S7** (MAME `harddriv.cpp`) and **S8** (MAME
TMS34010 device) cover the code F1, F3, F5, F6, F7 and F15 rest on.
`docs/mame-machine-reference.md` is a local synthesis of S7/S8, not itself a
register entry, so it is cited by path rather than by `S#` (F19).

---

## Section B — `QUESTIONS.md`, `BUGS.md`, and the bead tracker

# 1. IRQ register

Seven `IRQ-*` items, `QUESTIONS.md:5-689`. **Two** carry a `RESOLVED` status line
(`:7`, `:264`) and only **one** has a `### Resolution` section. The second of those
two is the one that is wrong — see the IRQ-0005 row.

| id | `file:line` | one-line question | `Status:` line | has `### Resolution`? | my judgement | evidence for the judgement |
|---|---|---|---|---|---|---|
| **IRQ-0001** | `QUESTIONS.md:5` | which MAME shortname/revision is the canonical baseline | `RESOLVED` (`:7`) | **yes**, `:29-31` | **resolved** | The Resolution answers 4 of the 6 requested fields inline — the ROM-hash one only by pointing at `mame/system-baseline.json` (`rom_manifest`, `launch`) and a passing `tools/check-roms`; the exact launch command and the deterministic-tracing flags are not in it, though `launch` in the same file is where they live. No open remainder anywhere in the item. |
| **IRQ-0002** | `:35` | smallest useful processor/device map for boot + title | `PARTIALLY RESOLVED` (`:37`) | no — has `### M1 progress` `:61` | **open, remainder named** | Matches the header. The remainder is enumerated by the item itself at `:87-89` (gloss: upload record semantics, post-gameplay traffic, input-dependent GSP path) and `:96-97` (sound fixture selection, producer-buffer semantics). Treat the header as honest and the remainder as the live text. |
| **IRQ-0003** | `:101` | minimum canonical checkpoint schema | `PARTIALLY RESOLVED` (`:103`) | no — `### M1 progress` `:120` | **resolved for the stated scope, with a deliberate deferral** | `analysis/checkpoint-schema.md` is named as the home and `:126-129` records *why* semantic gameplay fields are not promoted (gloss: the frame-600 image is not yet a proven gameplay selector). Nothing outstanding that the item did not choose. **No bead.** |
| **IRQ-0004** | `:134` | is track progression a walking cursor; where and what stride | `OPEN` (`:136`) | no | **half-answered; header honest** | Cursor half is **killed and reported**: the Kill criterion fired for every candidate (`:200-211`) and the hunt is `CLOSED` at 1-frame resolution (`:240`). March half is **answered** (`:227-258`, and `analysis/track-storage.md`). The live remainder is the segment→index map and course-dependent table selection (`:241-242`). |
| **IRQ-0005** | `:262` | do the ROM tables follow `base + course×stride + field` | **`RESOLVED (original field/width premise corrected)`** (`:264`) | **no** — 11 `### Investigation update` blocks | **the header is wrong; treat as OPEN** | (a) The item's own last recommendation contradicts its header: `:508-512` reads `Status recommendation: PARTIALLY RESOLVED — … open remainder is the segment→index map and field semantics`. (b) H1 is **killed as framed** (`:496`), not answered — `:522-524` says the three requested *Required output* items (reader PC / base / stride / fields per table; a course-3-vs-6 differential; confidence per table) were never produced in the requested form. (c) `:533-538` retires the original `0xFF9578` premise outright and leaves its producer an independent question. **No bead covers the remainder.** |
| **IRQ-0006** | `:548` | are spawns position- or frame-triggered | `OPEN` (`:550`) | no — one `### Investigation update` `:583` | **open, narrowed to one dichotomy** | `:590-592` states the delay probe result and then says plainly that frame-locked vs position-locked is `NOT DISCRIMINATED`. That is an honest `OPEN`. **No bead.** |
| **IRQ-0007** | `:599` | three sub-questions from the state log (`0x818002`, `0x818040`, `move.s` size selector) | `OPEN` (`:601`) | no | **open; 2 of 3 now partly answerable from the tree, 1 of those newly here** | See T17, T18 and section A F5/F6. Two are addressed by other rows in this file; the third (`move.s #imm,(xxx).L` size selector) is **new** — T17. **No bead**; bead `stun-runner-4bp` covers the neighbouring ADSP interrupt drive and does not cover any of the three. |

### Two structural defects in `QUESTIONS.md`, independent of any single IRQ

- **T1 — `IRQ-0005`'s updates are out of order and duplicated.** In file order
  they run 3, 4, 5, 6, 7, 8, **11, 12, 9, 10, 5** (`:296, :322, :351, :373, :401,
  :419, :452, :475, :488, :514, :525`). The two blocks that carry the *kills*
  and the C slice (update 9 at `:488`, update 10 at `:514`) sit **after** update
  12, and there are two blocks numbered "update 5" (`:351` and `:525`) that reach
  opposite conclusions about the course word. A reader who takes "the last update
  wins" takes update 12 and misses the kill. This is the same failure mode as
  MD-13: a superseded claim surviving because nothing says which text is
  current.
- **T2 — `PARTIALLY RESOLVED` is not machine-readable.** The header line is
  prose (`:7`, `:37`, `:103`, `:264`), so "still open" and "done" are the same
  token to any tool reading `QUESTIONS.md`. The only structural marker in the
  file is the presence of a `### Resolution` heading, which 1 of 7 items has.

**T17 — IRQ-0007 sub-question 3 is answerable from the oracle's own decode
table, and the item's framing describes an encoding that is not there.**
`QUESTIONS.md:645-660` asks for "the 68000 size-selector rule for
`move.s #imm,(xxx).L`, from a primary source", on the premise that
`33 fc` with an absolute-long destination is `move.s #imm,(xxx).L` whose size is
"selected by the low two bits of the immediate field". Verified against the
oracle's **executor** decode table, not a manual:
`/mnt/build/mame-ref/src/devices/cpu/m68000/m68000.lst:527` is
`33fc ffff move.w imm16 adr32` — a 16-bit MOVE of a 16-bit immediate to an
absolute-long destination, 2 + 2 + 4 = **8 bytes**, which is exactly the length
and PC advance the item already measured. Two independent confirmations:
Musashi's table routes `0x33FC` to `d68000_move_16` (`m68kdasm.c:3470`,
`{…, 0xf000, 0x3000, 0xfff}`) and to **neither** MOVEA entry
(`0x33FC & 0xf1C0 == 0x31C0 ≠ 0x3040` and `≠ 0x2040`, `:3472-3473`). So the size
comes from bits 15-12 of the instruction word, exactly as the `move.w` family
name says; **for `MOVE` with an immediate source there is no selector inside the
immediate to look up**, and the "would matter on an 8-bit device window" worry
applies to some other encoding. `*Not verified:*` what the real 68010 silicon
does — `UNKNOWN` without the MC68010 manual, and no `S#` covers
`src/devices/cpu/**` (see O9). The same table settles MD-09's encoding claim;
see T18.

---

# 2. Bead register

Source: `bd list --json` (44 rows), cross-checked against `bv --robot-triage`
project health (`total 77, open 36, closed 33, in_progress 8`). Grouped by
priority; within a group, alphabetical. **Acceptance** is the bead's own
`acceptance_criteria` field or an `Acceptance:` line in its description, one-line
gloss. `—` means the bead states none, which is 30 of 44.

## P1 — 21 beads

| id | title | status | acceptance (bead's own words, glossed) |
|---|---|---|---|
| `stun-runner-0o9` | Reconcile the 68010 ADSP program-window byte offset with the ADSP program address | open | — |
| `stun-runner-0p0` | Scripted MAME input is nondeterministic; committed input-scheduled evidence does not reproduce | open | — (notes carry a MEASURED/REPRODUCED matrix) |
| `stun-runner-1fv` | Capture one deterministic ADSP-to-SOM transaction for the first C producer slice | in_progress | — (acceptance is in the description: a one-transaction fixture, ROM manifest + binary hash, `input=none`, GINT/ack) |
| `stun-runner-4bp` | Drive ADSP /GINT as IPL1 (autovector 26, `$00020068`) | open | — (description: disassemble `$00020068`, identify the GINT handler, confirm `$818060` is the ack) |
| `stun-runner-8y8` | Classify every undecoded address by EXECUTION, not by whether MAME decodes it | open | — (four buckets A-D; bucket B is the only one that changes what we implement) |
| `stun-runner-9m4` | Find why the GSP raises INTOUT once instead of every third frame | open | — |
| `stun-runner-aao` | Fix frame-48 service scanout address encoding | in_progress | — |
| `stun-runner-alo` | native: replace ADSP-2100 with C geometry pipeline | open | — (notes record 3 slices landed; exact SOM stream as oracle, no lookup table) |
| `stun-runner-bp1` | `mame-gsp-state-snapshot` captures without an explicit `-nvram_directory` | open | — (impact to assess: which `vram_high_sum32` values predate the hygiene) |
| `stun-runner-djb` | Separate scanout-geometry error from unfilled-share error | open | — (description: render the committed frame-600 fixture with MAME's sampled registers) |
| `stun-runner-gsvp` | native: execute the GSP with a vendored TMS34010 SV core | in_progress | GSP instructions execute from the uploaded VRAM image; GSP VRAM/palette/display-register state matches the oracle at a bounded frame |
| `stun-runner-gsvp.3` | Write the C++ host harness: GSP memory map, host port, clock and reset | open | harness binary starts the core, releases reset from a captured `/GSPRES` event, advances PC into the uploaded program, reports observable state |
| `stun-runner-gsvp.4` | First milestone: GSP executes and its VRAM writes match the MAME oracle | open | at a bounded frame the RTL core's VRAM/palette/display registers are compared against MAME; the difference is zero or characterised as bounded |
| `stun-runner-gsvp.5` | Replay GSP inputs at oracle emulation times | in_progress | at frame 450 each captured event advances at its MAME timestamp on a 48 MHz GSP clock; malformed timing rejected; frame-only captures clearly marked |
| `stun-runner-nhc` | Palette: separate palette bank from palette contents, and check the 'not a memory share' claim | open | — (no description either) |
| `stun-runner-o1p` | capture and decode 68010→GSP display list format | open | — **blocked by `stun-runner-s5r`** |
| `stun-runner-q9t` | Decode the SOM payload format: which word is which vertex | open | — |
| `stun-runner-rg9` | Port the MC68010 `$25E8` text-draw routine end to end | in_progress | — |
| `stun-runner-s5r` | native: MC68010 stalls waiting on absent co-processors — find which handshake never completes | in_progress | — **blocks `o1p` and `4vf`; the only `in_progress` blocker on the board** |
| `stun-runner-w55` | Reconcile the ADSP frame-600 `nonzero_words`: 4,356 today vs 2,718 in STATUS | open | — (notes: record which binary produced which figure, or retire one number) |
| `stun-runner-yuv` | Add an ADSP executed-PC trace hook to the observer MAME build | open | — (gating step for `8y8`'s ADSP half) |

## P2 — 16 beads

| id | title | status | acceptance |
|---|---|---|---|
| `stun-runner-47h` | Establish the GSP share fold period: what the board does above 512 KiB | open | — |
| `stun-runner-4vf` | native: integrate GSP/TMS34010 video output | open | — **blocked by `stun-runner-s5r`** |
| `stun-runner-63p` | Adopt structured evidence bundles and preflight | open | — |
| `stun-runner-6tf` | Complete the TMS34010 JR condition table from the 7,411 real instructions | open | — |
| `stun-runner-8ix` | Decide the fate of three superseded agent branches | open | — (recommendation recorded: do not merge; one asset at risk, see row) |
| `stun-runner-8p4` | [epic] Capture and isolate TMS34010 program upload and replay | open | bounded provenance-linked capture of upload + release events + pre-release image; replay into a clean RTL board reconstructs the same state; ≥1 controlled variation has an asserted output difference |
| `stun-runner-9rm` | native: shell segfaults in GLX teardown when a ROM directory is set | open | headless runs with a ROM dir exit 0; GL window torn down before exit without faulting libGLX; not fixed as a drive-by |
| `stun-runner-bzq` | native: integrate JSA II / ADPCM audio | in_progress | — |
| `stun-runner-o1p.4` | Reconcile display-list register widths and identify high-GSP upload contents | open | oracle word-value parity for representative streams + evidence-backed identification of the smallest upload, or explicit remaining unknowns |
| `stun-runner-q81` | native: implement game state machine and race logic | open | — |
| `stun-runner-spx` | native: implement input system (wheel, ADC, shifters) | open | — |
| `stun-runner-t9y` | Close remaining partial-capture gaps in MAME wrappers | open | all six wrappers refuse a silently truncated capture (missing DONE marker / fewer targets / wrong frames → non-zero with a diagnosis naming the shortfall), each with a test proven to fail when the check is removed, plus a live clean re-run; **no recorded oracle value changes** |
| `stun-runner-wch` | Make the disassembly sweep a repeatable check, not a `/tmp` number | open | — |
| `stun-runner-xiw` | Label every MAME measurement as oracle or observer build | open | — |
| `stun-runner-xop` | Inventory static data and unused regions in STUN Runner ROMs | in_progress | — |
| `stun-runner-xop.1` | Trace and convert MC68010 GSP VRAM writers | open | a reproducible trace identifies writer PCs, and one confirmed writer routine has a C slice with an observable test |

## P3 — 7 beads

| id | title | status | acceptance |
|---|---|---|---|
| `stun-runner-4r9` | Fold `tools/disasm-gsp` onto the shared `tools/emu_shim.py` shim | open | — |
| `stun-runner-5wd` | native: save state slots — checkpoint JSON persistence + menu overlay | open | — |
| `stun-runner-61b` | native: SDL GameController mapping | open | — |
| `stun-runner-9iy` | native: state-sync network multiplayer | open | — |
| `stun-runner-gky` | native: Emscripten/WASM port as PWA | open | — |
| `stun-runner-srw` | native: FSR 1.0 upscaling | open | — |
| `stun-runner-zsu` | native: CRT scanlines post-process shader | open | — |

### Register observations

- **T3 — 35 of the 44 beads have no `acceptance_criteria` field at all.** Only
  nine carry one, and four of the nine are the `gsvp` family. Three more
  (`4bp`, `djb`, `1fv`) state an acceptance line inside the description, so
  **12 beads in 44 define "done"**. The five non-`gsvp` ones with a *testable*
  sentence are `t9y`, `9rm`, `8p4`, `o1p.4` and `xop.1`. This is the same shape
  as `BUGS.md`'s rule ("fill in every field"): the tracker dropped it, and `bv`
  scores the rest on centrality rather than on whether done is demonstrable.
- **T4 — two beads are the same work.** `stun-runner-4vf` (P2, open, "native:
  integrate GSP/TMS34010 video output") and the **closed** `stun-runner-1wz`
  (P2, identical title). Check whether 1wz's closure already discharged 4vf's
  content before spending 4vf.
- **T5 — `stun-runner-xiw` and `analysis/mame-local-patches.md` overlap.**
  `analysis/mame-local-patches.md:162-207` already performs xiw's labelling for
  the frame-600 case; xiw's remaining scope is "every measurement", which is a
  much larger job and is the only bead naming this rule.

---

# 3. Beads whose premise may now be wrong

One row per bead I assessed (36 of 44; the nine I did not are named at the
end): has anything landed since it was written that answers it, contradicts it,
or changes how it should be approached? `no candidate found` is a valid answer
and 25 of them are.

### Beads with a real candidate

| bead | bead says | the source says | verdict |
|---|---|---|---|
| **T6 — `8y8`** | the ADSP frame-600 program image has "224 wholly ??? (in 165 ranges) **+ 22 partial**" (`bd show …`) | the 22 partial are **resolved**; findings **§85.4** (`:13224-13267`) reads them out of MAME's executor, and the latest commit `9560c40` is that write-up. §85.5 (`:13269-13279`) states the general rule. | **Population is stale.** The remaining class is 224 wholly-`???` only; the "22 partial decode" half of this bead is free and already paid for. Its own palette premise is also wrong — see the next row. |
| **T7 — `8y8` (2nd premise)** | "GSP PALETTE data is NOT in the VRAM share at all. `harddriv.h:356-357` declares `m_gsp_paletteram_lo/hi` as **separate address_space members**" | `harddriv.h:356-357` declares `memory_share_creator<uint16_t> m_gsp_paletteram_lo;` / `_hi` — **shares**, created at `harddriv.cpp:399-400` as `gsp_palram_lo` / `gsp_palram_hi`, mapped at `harddriv.cpp:626-627` through `0xf5000000` / `0xf5800000` in `multisync_gsp_map`. | **Premise wrong, conclusion survives.** They are separate *shares*, not `address_space` members — so "palette bytes cannot be among the GSP share's `DW` cells" still holds, on a different footing than stated. `BUGS.md` MD-07 quotes those exact two lines and gets the type right; the bead's note does not. |
| **T8 — `nhc`** | "check the **'not a memory share'** claim" | as above: they **are** memory shares. | **The literal claim is false**, and `BUGS.md` MD-07 already quotes the declaration correctly. The live half of nhc is the *bank* half (which register/address bit selects a palette bank) — `harddriv.cpp:626-627` show it is the window, not the stored word. nhc has no description; a bead whose premise is false needs one rewritten before it can be worked. |
| **T9 — `6tf`** | "Complete the TMS34010 JR condition table **from the 7,411 real instructions**" | three things landed. (a) §85.1 (`:13191-13194`) already measured that all 18 JR condition variants occur in the real program, so the corpus question is answered. (b) The table the bead wants is in the pinned tree — see section A F8. (c) **New, and the reason this row exists:** MAME and the vendored RTL disagree about what condition code `0x0001` *is*. MAME's disassembler renders it `P ` (`34010dsm.cpp:137`) and MAME's **executor** takes the branch on `!N_FLAG() && !Z_FLAG()` (`34010ops.hxx:1655-1665`); the vendored RTL declares `CC_LO = 4'b0001` as **carry-set** and evaluates `branch_taken = st_c` (`third_party/tms34010_sv/rtl/tms34010_pkg.sv:731`, `rtl/core/tms34010_core.sv:2968`). The RTL also carries **two** carry-set codes (`0001` and `1000`, `pkg.sv:731,743`). | **Premise understated, not wrong.** "Complete the table" is a copy; the copy then collides with another in-tree table. This is executable divergence — a real `JR` with `cc=0001` branches on different flags in MAME and in our core — so it bears on `gsvp.4`'s core-vs-oracle acceptance too. The arbiter is S9 Table 12-8, which is **not held locally** (`docs/sources.md:37`): `UNKNOWN` which table is right, and no substitute settles it. |
| **T10 — `w55`** | hypothesis (stated, not measured): the 2,718-vs-4,356 gap is §81's non-interchangeable MAME binaries | the hypothesis is the recorded one (§85.3, `:13281-13293`). **Three things it does not mention.** (a) `tools/mame-adsp-program-dump:36` passes `-cfg_directory /tmp/stunrun-mame-cfg` — a **shared** directory — with `-nonvram_save` and **no** `-nvram_directory`, which is exactly the failure class bead `bp1` measured on a sibling tool, and what AGENTS.md trap 10 documents. (b) The 2,718 figure is not confined to `STATUS.md`: it is also `reproduction/adsp/adsp_control_seq.h:79` (`STUNRUN_ADSP_UPLOAD_COMPLETE_WORDS`), `native/checkpoint_test.c:59,111`, `native/state_watch_test.c:645,647`, `native/testdata/checkpoint_m1.json:19`, `reference/checkpoints/m1-machine-map/state.json:19`, `reference/checkpoints/m3-upload-boundary/state.json:14`. (c) **The binary half of the bead's own instruction is partly answerable already, and it turns up a third binary.** See T21 below the table. | **Genuinely open, and wider than the bead says.** If 4,356 is right for the current binary, the committed `stunrun-checkpoint/v1` fixture and two CTest expectations are also wrong, not just a status line. The cfg/NVRAM contamination is a *second* candidate the bead does not list, and it is the cheaper one to test (`bp1`'s method, one variable at a time). Note the counter-refutation that keeps it honest: `analysis/mame-local-patches.md:204-207` refuted NVRAM for the **frame-600 screen** case on mtimes, which does not carry over to the ADSP image's own captures. |
| **T11 — `47h`** | "Establish the GSP share fold period: what the board does above 512 KiB" | findings **§84.2** (`:13102-13130`) already frames the question, quotes S3 on dynamic memory organisation, and names the blocker: the bank decode was withheld from the published schematic set. §84.2 also settles the *alias-vs-void* half (gloss: it aliases, per Margolin). | **Premise intact, scope narrowed by §84.** Not a duplicate of the closed `stun-runner-2ua`, which settled the *size* (512 KiB) and explicitly left the fold period open. `no candidate found` for the fold period itself. |
| **T12 — `4bp`** | "Drive ADSP /GINT as IPL1 (autovector 26, `$00020068`)" | **the title is correct and I checked it three ways**, because it looked wrong: MAME drives `set_input_line(2, …)` (`harddriv_m.cpp:96`), which is interrupt level 2 = IPL1 = autovector `24+2` = 26; the ROM's own relocated vector table agrees — `020068 -> 0x0213FE vector 26 = level 2 ADSP` (findings `:7837`); and the S6 manual transcription puts `/ADSPIRQ` on IPL1 (`docs/manual-transcription.md:830,1113`). | **Premise sound — no disagreement between S6 and S7 here.** What *is* partly spent: the bead's own acceptance ("disassemble `$00020068`… confirm `$818060` is the acknowledge") is already delivered by IRQ-0002's M1 progress (`:79-89`) and `reference/experiments/stunrun/adsp-special-io-trace.metadata.json:84`. What remains is the native wire, plus the buffer-write and `/SOMCLK` side the bead lists. |
| **T13 — `9m4`** | two candidates: (a) the GSP does not re-enter the display-list routine ending in the HSTCTLL write; (b) host-port writes do not reach it as MAME's do | §47.6 (`:9204`) names `0xC00000F0` as the single INTOUT setter from the **RTL core's own** 600-frame run. §85.1 (`:13173-13199`) then swept the **real uploaded program** at the frame-404 boundary with **0 unknown encodings** — so candidate (a) is now answerable by reading a decode we already hold, rather than by another run. | **Approach changed, question not answered.** The program the question is about can now be disassembled with no decoder gaps; §85.1's own framing (`a linear decode says nothing` — `tools/gsp-program-map:8-19`) still applies, so the *fetch trace* is what discriminates. `no candidate found` for the cadence cause itself. |
| **T14 — `gsvp.3`** | "DONE, milestone 1 reached and asserted" (its own notes) | `STATUS.md:96-113` agrees, and §35 records the corrected units. | **Status/bead mismatch, not a premise error.** The notes say done; the bead is `open` and **blocks `gsvp.4`**, which `bv` therefore scores as blocked. I did not close it — that is the parent's call. |
| **T15 — `gsvp.4`** | acceptance is "core's VRAM matches MAME at a bounded frame" | notes say `REPLAY OBJECTIVE MET` (board model, byte-identical) and then retract it: "STILL GENUINELY OPEN … the TMS34010 core has not been shown EXECUTING the uploaded display list". §84's fold-period work and section A F3/F1 both touch its evidence base. | **Self-correcting; no contradiction left.** The one *new* dependency is the `6tf` row (T9): the JR condition-table disagreement (`6tf`) is a live behavioural divergence between our core and MAME, so `gsvp.4`'s comparison inherits it. |
| **T16 — `0o9`** | the `offset/2` lane question, from findings §82.3 | section A **F7** reads three in-tree sources and composes them into a much smaller question. | **Not stale; narrowed elsewhere.** Flagging it only so the index does not carry two different sizes for the same row. |
| **T21 — `xiw` / `w55`** | `xiw`: label every MAME measurement as oracle or observer build | **A third binary hash is already committed and matches neither of the two named.** `analysis/mame-local-patches.md:188-189` names the frozen oracle `75b09ee4…` and the observer `55956f2b…` (the observer mtime there, 2026-10-14, matches `ls -la /mnt/build/mame-ref/mame` today). `reference/experiments/stunrun/adsp-program-snapshot.metadata.json` records the 2,718 figure with `mame.executable_sha256 = fcb54790e1837ed6…` — **neither of those two**. Across the 21 ADSP/M3 metadata files under `reference/experiments/stunrun/`, that is the **only** one that names an executable hash at all; `m3-adsp-program-image.metadata.json` and `m3-adsp-upload-boundary.metadata.json`, the two files whose provenance chain `reproduction/adsp/adsp_control_seq.h:7-16` cites for the 2,718-word image, record only the version string `0.289 (mame0289-dirty)` — which **both** binaries report (`analysis/mame-local-patches.md`, hook 6). | **xiw's premise is confirmed and its scope is larger than one bead.** The binary inventory in the patches doc is incomplete, and the one committed hash that does identify a binary identifies an unnamed third one. That is `w55`'s own instruction ("record which binary produced which figure") already half-done, and it is the row `w55` cannot close without. |

### Beads with no candidate

`stun-runner-0p0` · `stun-runner-alo` · `stun-runner-bp1` · `stun-runner-djb` ·
`stun-runner-63p` · `stun-runner-8ix` · `stun-runner-8p4` · `stun-runner-9rm` ·
`stun-runner-o1p` · `stun-runner-o1p.4` · `stun-runner-q9t` · `stun-runner-q81` ·
`stun-runner-spx` · `stun-runner-t9y` · `stun-runner-wch` · `stun-runner-xop.1` ·
`stun-runner-yuv` · `stun-runner-4r9` · `stun-runner-5wd` ·
`stun-runner-61b` · `stun-runner-9iy` · `stun-runner-gky` · `stun-runner-srw` ·
`stun-runner-zsu` — **no candidate found.** Nothing in the tree answers or
contradicts them.

(`stun-runner-xiw` is **not** in this list — see T21.) One deserves a note
rather than a shrug:

- **`stun-runner-yuv`** — verified still open, and its own gating claim holds:
  `analysis/mame-local-patches.json` lists **8** observation-only tags
  (`STUNRUN-DEBUG`, `-DUMP`, `-DPY`, `-HOSTPROBE`, `-OPLOG`, `-FETCH`, `VERSION`,
  `IOB`) and **zero** ADSP hook, and
  `tests/test_mame_local_patches.py` would fail on an unrecorded one. So yuv's
  constraint ("MUST be recorded in the inventory or the suite fails") is the
  live one, not an obstacle.
And for completeness, the bead this pass moved out of "no candidate":
`stun-runner-xiw` (see T21 and T5).

**Nine outstanding beads are not assessed here**, and the section header's
"every open bead" should be read as "every open bead I looked at". Eight are
`in_progress` — `1fv`, `aao`, `bzq`, `gsvp`, `gsvp.5`, `rg9`, `s5r`, `xop` — and
`in_progress` means another lane owns them, so a premise verdict from this pass
would be a guess about someone else's work. The ninth is
**`stun-runner-4vf`** (P2, open, blocked by `s5r`): its title is duplicated by the
closed `stun-runner-1wz` (T4), which is the only thing worth saying about it
before anyone spends it, and I did not read `1wz`'s closure to check.

---

# 4. `BUGS.md` state

`BUGS.md:1-691`. Its own rule (`:19-29`, `:416-421`): driver-section entries get
**reported upstream**, so a false one costs more than silence.

## Live vs withdrawn

| entry | `file:line` | state |
|---|---|---|
| SR-01 … SR-19, all nineteen | `:35-412` | **live** — ROM-code, ROM-data and hardware-abuse entries, none withdrawn. |
| SR-08, SR-09, SR-14, SR-15 | `:334-412` | **live but filed under "Hardware abuse"** while carrying `Kind: ROM code` (`:337, :349, :366, :388`). `BUGS.md:332` still prints `*(none recorded yet — this section exists so the distinction stays visible)*` **above** four entries. A reader trusting the section header sees zero hardware-abuse findings; four exist. |
| MD-10, MD-11 | tombstone only, `:580-600` | **withdrawn** 2026-10-13, recorded as a note rather than as entries. Correctly retired; §28 holds the refutation. |
| MD-13 | `:647-682` | **withdrawn** 2026-10-17, with the reasoning inline. Correctly retired. |
| MD-05 | **does not exist** | **ghost ID.** See T20. |

## Ready to report upstream?

`analysis/mame-bugs.md:31-77` is the tree's own readiness column ("None of these
have been sent upstream. Several are close; none is ready"). My assessment, per
entry, against that column:

| entry | my read | why |
|---|---|---|
| MD-01, MD-02, MD-03 | **ready once reduced** | matches `mame-bugs.md:57`; a measured symptom without a reproducer is not a report. |
| MD-06, MD-07 | **the most likely to be accepted as-is** | matches `:60-61`; both are self-contained Lua-side, both are *misuse-shaped* like MD-10/MD-11 — MD-07 in particular is "the type is a finder object, use `.target()`", which is exactly the class MD-13 taught us to check for a primary-source "this is how it works" statement before filing. **Recommend one more pass of that check.** |
| MD-04 + MD-08 | **send as one**, not two | matches `:63-68`; two reports where one gets closed "by design" loses both. |
| MD-09 | **do not send — and one of its two stated reasons is now refuted** | see T18. |
| MD-12 | **do not send** | `mame-bugs.md:78+` wants a sanitiser run; independently, section A **F1** records that the pinned tree already carries the fix, so the tree's own comment and the analysis still describe an unfixed state. |
| MD-13 | **do not send** | withdrawn. Leave the ID retired, as the entry says. |

### The three rows I would not present as they stand

**T18 — MD-09 rests on an encoding claim the oracle's own decode table refutes.**
`BUGS.md:552-559` says the `movea.l #$FF945E,A1` at `$004F96` "has a
**non-canonical** encoding (`0x2279 00FF945E`) that does not match standard
`movea.l #<long>.L`… the byte pattern suggests `movea.w`". `analysis/mame-bugs.md:72-73`
repeats it, and `:70-74` makes it one of the two reasons not to report.
Verified against the oracle's own **executor** decode table:
`src/devices/cpu/m68000/m68000.lst:332` is `2079 f1ff movea.l adr32 ad`, and
`0x2279 & 0xf1ff == 0x2079` exactly — a canonical 6-byte MOVEA.L. Verified
independently against the vendored Musashi table
(`third_party/musashi/m68kdasm.c:3473`, `{d68000_movea_32, 0xf1c0, 0x2040, 0xfff}`;
`0x2279 & 0xf1c0 == 0x2040`). **The claim is false.** MD-09's other half — GSP
halt state, the `host_r()` path, the VRAM mapping — is separately recorded as
confirmed by measurement and is unaffected. **The entry's "do not report" verdict
survives; one of its stated reasons does not, and MD-09 is three copies deep
(`BUGS.md`, `analysis/mame-bugs.md`, and the `Impact on us` at `:562-572`).**
*Not verified:* what the real 68010 does — `UNKNOWN` without the M68010 manual,
and no `S#` covers `src/devices/cpu/**`.

**T19 — MD-12's stated evidence is contradicted inside this repository, and the
contradiction is what the behavioural patch rests on.** `BUGS.md:612-629` says
the uploaded program "contains seven such stores, and the addresses were
fetched, so they executed", listing `C0000110h` … `C0000160h`. Findings **§52.2**
(`:9750-9777`) measured that `STUNRUN_GSP_IOBREPORT` — `fopen`ed **inside**
`if (offset >= 64)` — never fired across ~1800 frames on either binary, and
concluded the claim is "stale or a misreading". `analysis/mame-local-patches.md:131-153`
never updated: it still says the stores "were traced executing", and is the stated
cause of the frame-600 title-vs-self-play divergence (`:191-195`). Section A **F1/F3**
flag the same two claims as opposites. **I did not re-run either instrument**, so
which is right is `UNKNOWN`; but a behavioural patch to the oracle should not rest
on a premise the tree has already contradicted once. (Also: `docs/sources.md` has
no `S#` for `src/devices/cpu/tms34010` **behaviour** beyond S8's scope — S8 says
"host-port semantics, register behaviour", which arguably covers it. Marking the
scope question rather than deciding it.)

**T20 — `MD-05` is a ghost ID in the upstream-report namespace.** It has **no
entry and no withdrawal note** in `BUGS.md` — the MAME-driver section runs MD-01,
-02, -03, -04, then -06. `analysis/mame-bugs.md:21` records the gap explicitly
(*missing — no entry, no withdrawal note* → *bookkeeping gap*), and
`docs/mc68010-boot-findings.md:7009` supplies the missing history (gloss: it was
"Lua cannot read the share at all", our stride error, not a MAME defect). But
`docs/mame-machine-reference.md:126` still tells the reader that "`BUGS.md`
MD-04/MD-05 … record the consequence", `:1015` still lists MD-05 as a `BUGS.md`
entry, and bead **`stun-runner-s5r` cites MD-05** as if it were live. This is
MD-13's shape one ID earlier: a retracted claim that keeps being cited because
nothing says it is dead. **A tombstone belongs in `BUGS.md` next to MD-10/MD-11.**

---

# 5. Orphans

Outstanding work visible in `QUESTIONS.md` or `BUGS.md` with **no bead covering
it**. Listed only — not filed. Id candidates are suggestions; `bd` assigns the
short suffix.

| # | source | the outstanding thing | suggested id + one-line title |
|---|---|---|---|
| O1 | `QUESTIONS.md:508-512` | segment→index map and field semantics — the remainder `IRQ-0005`'s own last recommendation leaves open, and the same remainder `IRQ-0004` carries at `:241-242` | `stun-runner-tk1` — Map course → segment sequence → ROM table index; decode the table words |
| O2 | `QUESTIONS.md:590-592` | `IRQ-0006`: frame-locked vs position-locked spawns, `NOT DISCRIMINATED` | `stun-runner-sp1` — Discriminate frame-locked vs position-locked object spawns for `0xFFDD00` |
| O3 | `QUESTIONS.md:599-689` | `IRQ-0007`'s three sub-questions. Sub 2 is addressed by the pinned map (section A F5, and T17 in section 1), sub 1 narrowed by F6; **sub 3 has no bead and no in-tree answer until this pass** (T17) | `stun-runner-aq1` — Characterise `0x818002`'s LED-write count against MAME, and record the `move.w imm16,(xxx).L` decode |
| O4 | `BUGS.md:552-559` + `analysis/mame-bugs.md:70-74` | MD-09's refuted encoding claim, in three committed places | `stun-runner-mb1` — Correct the MD-09 `$004F96` encoding claim against the oracle decode table |
| O5 | `BUGS.md:580-600`, `:647-682`, `analysis/mame-bugs.md:21` | the "entries get reported upstream" rule has **no bead at all** — across all 44 outstanding beads only `9m4`/`xiw` (MD-12) and `s5r` (MD-05/MD-08) name an `MD-*` at all, and none of the three is about reporting | `stun-runner-mb2` — Reduce and file the upstreamable MAME-driver reports (MD-01/02/03, MD-06/07, MD-04+08) |
| O6 | `BUGS.md:647-682` vs `analysis/gsp-program-upload.md:304-333` | the MD-13 retraction is propagated to `analysis/gsp-program-upload.md` and BUGS.md, but the **`tools/disasm-gsp` docstring is the place the false claim originated** and three committed files asserted it (per the task brief) — no bead tracks checking that no fourth file still does | `stun-runner-mb3` — Sweep the tree for surviving repeat-instruction claims (the MD-13 tombstone's actual lesson) |
| O7 | `analysis/mame-bugs.md:21` | MD-05's tombstone is missing from `BUGS.md` while three files still cite MD-05 as live (T20) | `stun-runner-mb4` — Write the MD-05 withdrawal note into `BUGS.md`, or renumber |
| O8 | `BUGS.md:332` vs `:334-412` | the hardware-abuse section header still says "(none recorded yet)" above four entries | `stun-runner-mb5` — Fix the `BUGS.md` hardware-abuse section header and the four entries' `Kind` |
| O9 | `docs/sources.md:35-37` | no `S#` for `src/devices/cpu/**` (ADSP-2100, MC68000), which findings §§82-85 and T17/T18 cite by `file:line` | `stun-runner-src1` — Add a source-register row for the pinned `src/devices/cpu` implementations |
| O10 | `QUESTIONS.md:262-545` | `IRQ-0005`'s header says RESOLVED with no `### Resolution`, contradicting its own last update (the IRQ-0005 row, section 1; and T1). No bead tracks the header | `stun-runner-qs1` — Reconcile the `IRQ-0005` status header and reorder/renumber its eleven update blocks |

**Also orphaned, but inside the bead board rather than beside it** — flagged, not
filed: `stun-runner-gsvp.3`'s own notes say `DONE` while it is `open` and blocks
`stun-runner-gsvp.4` (T3 table); and `stun-runner-4vf` duplicates the closed
`stun-runner-1wz` (T4).

---

# 6. What I deliberately did not conclude

Recorded because the index is more useful when its gaps are named.

1. **I did not re-run MAME.** Every "the source says" row above is a `grep`,
   `sed` or arithmetic check against the pinned tree and this repository's own
   files. I did not re-run `tools/mame-adsp-program-dump` (`w55`), the GSP upload
   boundary capture (§85.1, `9m4`, `6tf`), or `STUNRUN_GSP_IOBREPORT` (T19). Per
   AGENTS.md trap 4 a single `--input` run proves nothing anyway; a scripted
   five-run protocol was out of scope for an index.
2. **T19 I left as `UNKNOWN` on purpose.** I have two committed sources making
   opposite claims about whether the GSP writes at a MAME-offset ≥ `0x40`, and
   §52.2's null result depends on a hook whose *absence of file* is the evidence.
   Picking a side would be exactly the failure this index exists to catch.
3. **I did not resolve which JR condition table is right** (`6tf`). S9 is not
   held locally, and §85.5's rule ("ask the executor") cannot break a tie where
   MAME's executor and disassembler *agree* and only the vendored RTL differs.
4. **I did not confirm that the ROM's vector-26 handler is `0x0213FE`.** I read
   it in the findings' vector table (`:7837`); I did not read the ROM bytes at
   `0x20068`. For `4bp` this does not matter — the finding is enough to mark the
   bead's first two acceptance clauses as already spent.
5. **No bead was created, modified or closed**, and nothing was committed. O1-O10
   and the two in-board flags are for the parent to file.

---

### Method and limits, section A

**What I verified, and how.** Every `ANSWER ALREADY EXISTS` row rests on a read in
this session, not on recall:

- `analysis/mame-local-patches.json` — parsed with `python3 -c` and printed
  field by field; `analysis/mame-bugs.md:28, 76-90` — read.
- `tests/test_mame_local_patches.py` — **run**:
  `python3 -m unittest tests.test_mame_local_patches` → 9 tests, OK.
- `34010dsm.cpp:132-153` — read (`print_condition_code`, all 16 cases).
- `2100dasm.cpp` / `2100ops.hxx` — counted: 49 vs 213 `case 0x`.
- `harddriv_m.cpp:751-813` (`hd68k_adsp_control_w`), `:622-640`
  (`hd68k_adsp_program_w`) — read.
- `harddriv.cpp:599-616, 619-628, 4822-4835, 5042-5043, 5082-5094, 5375`
  — read.
- `tms34010.cpp:34-41, 1568-1615` and `tms34010.h:63-96` — read.
- `tms34010.h:350-356` (`m_IOregs[0x200]` and its comment) and
  `34010ops.h:40` (`#define IOREG(reg) (m_IOregs[(reg) & 0x1ff])`) — read.
- `analysis/mame-local-patches.md:121-160, 162-217` — read.
- `third_party/tms34010_sv/rtl/io/tms34010_io_regs.sv:146-168`,
  `rtl/tms34010_pkg.sv:363-406`, `rtl/core/tms34010_core.sv:3140-3430` — read.
- `third_party/musashi/m68kconf.h:132-136` — read.
- `analysis/gsp-program-upload.md:262-305`, `analysis/interconnect.md:140-269`,
  `analysis/track-storage.md:550-588`, `analysis/mame-bugs.md`, `analysis/hardware-lineage.md:160-175`
  — read.
- `STATUS.md` (whole), findings §§6, 20.7, 22.10, 26.13, 31.548E.8-9, 39.6, 44,
  47.3, 47.6, 47.9-47.10, 50, 52.1-52.4, 53.1-53.4, 54.5, 55.5, 57.4, 58.4,
  61.4, 62, 66.6, 67.7, 68.5, 69.5, 71-72, 74.3, 75.4, 76.9, 77.9, 80.9, 81.4,
  82.3, 84.1-84.2, 85.1-85.5 — read.

**Bucket counts.** 28 rows: **10** `ANSWER ALREADY EXISTS` (F1, F2, F4, F8, F9,
F10, F11, F12, F27, F28), **14** `candidate, unverified` (F3, F5, F6, F7, F13,
F14, F15, F16, F17, F18, F19, F20, F22, F26), **2** `no candidate` (F21, F25),
and **2 rows whose status is split across sub-questions** — F23 (`ANSWER ALREADY
EXISTS` for §54.5's list, `no candidate` for §58.4's three targets) and F24
(`candidate, unverified` for the ramp/field item, `no candidate` for the rest).
The split rows say so at the row.

F3 is deliberately in section 2, not section 1. It is the most valuable row in
the file and it is **not** an `ANSWER ALREADY EXISTS` row: I did not find the
answer to the register-numbering question, I found that three recorded claims
about it disagree. Promoting it would be exactly the failure mode this file
exists to catch.

**What I deliberately did not conclude.**

- **F3.** I did not decide which I/O address rule is correct. I found three
  recorded claims that do not line up and stopped there. The runs have
  different windows and different binaries; I re-ran none of them, and guessing
  here would repeat MD-13.
- **F1 / MD-12.** I established that the fix is applied and recorded. I did
  **not** judge whether the fix is *correct* — `analysis/mame-local-patches.md:150-152`
  itself flags the surviving question (128 four-byte registers vs 512 bytes at
  two-byte spacing), and that is F3, not something I closed. Nor did I re-derive
  whether the GSP really writes at `0x110`–`0x160`; the patch file's claim and
  §52.2's null result disagree and that disagreement is reported, not resolved.
- **F15.** I did the arithmetic the note asks for and stopped short of calling
  the expression wrong, because whether an index is in range depends on a
  region size I did not read out of the machine config. The numbers are in the
  row so the next reader can check them, not so they can be taken as a verdict.
- **F7.** I established what the driver *can* do; I did not establish where the
  observed non-zero low half came from, and F7 is explicitly a candidate for
  that reason.
- **F28.** I did not re-run `tools/mame-gsp-state-snapshot` against either
  binary, and I did not open the PNG. Every claim in that row is quoted from
  `analysis/mame-local-patches.md:162-200`, which I read. I also did not check
  whether `EVIDENCE.md` already carries the provenance rule F28 quotes, so that
  one stays `UNKNOWN`.
- **F13.** I did not treat §80 as having answered §39.6's shear. Different
  frame, different reference image per F28, and I re-ran nothing.
- **F16.** I did not classify a single one of the 224 addresses. §85.5's caveat
  — the executor cannot say whether an address was *fetched* — means the
  data-vs-code split is out of reach of the route I am pointing at.
- **F12.** I did not pick a winner between §50 and §53.3. They may be about
  different questions.
- **`m_IOregs` bounds (F1).** I did not re-derive MD-12's out-of-bounds claim.
  §52.2's null result and the MAME source I read (`IOREG()` unmasked at
  `tms34010.cpp:1576-1577`) are compatible with *both* "no out-of-bounds access
  occurred" and "one did not fire the report" — I could not separate them, and
  the row says the retraction stands and the bug file is the stale one, not that
  MD-12 is void.

**Known gaps in this section.** It covers the findings log, `STATUS.md`, and the
seven highest-UNKNOWN `analysis/` notes named in the brief. The remaining 40
notes in `analysis/` were sampled by UNKNOWN-marker count and not read, so
questions recorded only in them are absent here — with one exception worth
naming: I read `analysis/mame-local-patches.md` and `.json` because F2 pointed
at them, and they turned out to carry more recorded answers than the brief's
UNKNOWN counts suggest, which is itself an argument for reading a note by
relevance rather than by marker count. `BUGS.md` was read for MD-12 and MD-13
only; its SR-* and other MD-* entries are another agent's section. `EVIDENCE.md`,
`QUESTIONS.md`, `TESTING.md` and `PREFLIGHT.md` were not read.

**Citation register.** External documents are cited by their `S#` per
`docs/sources.md`: **S3** (Margolin, F24's ramp source and F25's silence),
**S6** (SP-338, F25), **S9** (SPVU001A — F3's only real authority, **not held
locally**, cited by section). **S7** (MAME `harddriv.cpp`) and **S8** (MAME
TMS34010 device) cover the code F1, F3, F5, F6, F7 and F15 rest on.
`docs/mame-machine-reference.md` is a local synthesis of S7/S8, not itself a
register entry, so it is cited by path rather than by `S#` (F19).
