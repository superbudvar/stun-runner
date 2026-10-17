# GSP: executed versus decoded — the four buckets, and bucket B is empty

**Bead:** `stun-runner-8y8`, GSP half. **Date:** 2026-10-17.

The bead's rule is the whole finding: *classify by execution, not by whether
MAME decodes.* "MAME emits `DW`" says nothing about whether an address is code,
and "MAME emits a mnemonic" says nothing either — a data word that happens to
be a legal encoding prints just as confidently as an instruction does. Only a
**fetched** address proves it is an instruction.

This note reports the partition of the GSP's physical 512 KiB share under that
rule, for the program's own corpus (the frame-404 upload-boundary image), and
records what it changes: **nothing, because bucket B is empty.**

---

## 1. Provenance

### 1.1 The binary

| | |
|---|---|
| path | `/mnt/build/mame-ref/mame` |
| sha256 | `55956f2b26c0d5caeb86f346e6247ded04428eae07465d35470dea77d6bf4045` |
| name in AGENTS.md trap 2 | `md12-fixed` |
| reported version | `0.289 (unknown)` |

**The frozen oracle cannot produce this measurement at all.** AGENTS.md trap 2
names `/mnt/build/mame-oracle/mame` (`75b09ee4…`, `stock-behaviour`) as the
default, but the `STUNRUN-FETCH` hook is not in it. Measured, not assumed:

```console
$ for b in /mnt/build/mame-oracle/mame /mnt/build/mame-ref/mame; do \
    echo "== $b"; strings -a "$b" | grep -o "STUNRUN[-_][A-Z_]*" | sort -u; done
== /mnt/build/mame-oracle/mame
STUNRUN_BOARD
STUNRUN_BOARD_
STUNRUN-DPY
STUNRUN-DUMP
STUNRUN_GSP_HOSTPROBE
STUNRUN_GSP_OPLOG_DETAIL
STUNRUN_GSP_OPLOG_EVERY
STUNRUN_GSP_OPLOG_FROM
STUNRUN_GSP_OPLOG_OUT
STUNRUN_GSP_OPLOG_TO
== /mnt/build/mame-ref/mame
STUNRUN_BOARD
STUNRUN_BOARD_
STUNRUN-DPY
STUNRUN-DUMP
STUNRUN-FETCH
STUNRUN_GSP_FETCH_MAX
STUNRUN_GSP_FETCH_OUT
STUNRUN_GSP_FETCH_SKIP
STUNRUN_GSP_HOSTPROBE
STUNRUN_GSP_IOBREPORT
STUNRUN_GSP_OPLOG_DETAIL
STUNRUN_GSP_OPLOG_EVERY
STUNRUN_GSP_OPLOG_FROM
STUNRUN_GSP_OPLOG_OUT
STUNRUN_GSP_OPLOG_TO
```

Only `md12-fixed` carries `STUNRUN_GSP_FETCH_OUT`. Using `md12-fixed` is also
what AGENTS.md trap 2 now prescribes for all new work, and §86.2 measured that
MD-12 never fires on the paths we run, so the fix changes nothing recorded.
**No number in this note is comparable to anything in `reference/` that was
captured with `stock-behaviour`; none was used.**

### 1.2 The share image — a boundary capture, and it reproduces §85.1

```console
$ tools/capture-gsp-program-upload roms /tmp/opencode/gspx/cap-observer \
      --frame 600 --mame /mnt/build/mame-ref/mame
```

| | measured now | §85.1 / §36 |
|---|---|---|
| halt release frame | **405** | 405 |
| release event seq | **631205** | 631205 |
| HSTCTL at release | **0x7800** (30720) | `$7800` |
| nonzero words at frame 405 | **83,194** | 83,194 |

Exact agreement on all four. The image this analysis classifies is the
pre-release one, frame 404, 524,288 bytes, GSP still halted:

```
gsp-vram-upload-last/gsp_vram.bin   sha256 47e41d1b9d54055d965074ac2b6e9e0b844f024006e2555f6b8a17543d4a2715
  frame 404, 262144 words, nonzero_words 81899, base 0xff800000
```

### 1.3 The fetch trace

Produced from a bounded, **input-free** power-on run (no `-input` schedule, so
deterministic — AGENTS.md trap 4 does not apply), through
`mame_harness/process.run_mame` so MAME and its descendants are reaped, with
isolated `-cfg_directory` and `-nvram_directory` per run (trap 12). The exact
invocation:

```text
mame -rompath roms stunrun -video none -sound none -skip_gameinfo
     -noreadconfig -nowriteconfig
     -cfg_directory  <per-run>  -nvram_directory  <per-run>
     -nonvram_save -nothrottle -seconds_to_run 10
env STUNRUN_GSP_FETCH_OUT=<trace>  STUNRUN_GSP_FETCH_MAX=5000000  STUNRUN_GSP_FETCH_SKIP=0
```

| trace | fetches | records | sha256 |
|---|---|---|---|
| **W1 (primary)** | 0 – 5,000,000 of a 10 s / 600-frame run | 5,000,000 + the hook's own `TRUNCATED` marker | `3b479a539e6976d74b471b9a36aaaf5344048a77a8be14166b3e29c0134cfe7c` |
| W2 (second window) | 5,000,000 – 10,000,000 of a 30 s run | 5,000,000 + marker | `ce37f489315d05c4301f37a5dc08f384a3f1d8a447d42c0e8ca90e8b8dffd0f3` |

Determinism, five runs of one command, identical file hashes every time. The
throwaway wrapper is named explicitly here because §7 records that it is not
committed:

```console
$ for i in 1 2 3 4 5; do
    python3 /tmp/opencode/gspx/fetchrun.py /tmp/opencode/gspx/d$i.txt \
        --seconds 30 --max 100000 --skip 4500000 --mame /mnt/build/mame-ref/mame
    printf 'run%s %s lines=%s first=%s\n' $i \
      "$(sha256sum /tmp/opencode/gspx/d$i.txt | cut -c1-16)" \
      "$(wc -l < /tmp/opencode/gspx/d$i.txt)" \
      "$(head -1 /tmp/opencode/gspx/d$i.txt | cut -c1-52)"
  done
run1 399d106eac03b1ec lines=100001 first=0 1000000000000000000 fff40f80 1ffe81f0 4820
run2 399d106eac03b1ec lines=100001 first=0 1000000000000000000 fff40f80 1ffe81f0 4820
run3 399d106eac03b1ec lines=100001 first=0 1000000000000000000 fff40f80 1ffe81f0 4820
run4 399d106eac03b1ec lines=100001 first=0 1000000000000000000 fff40f80 1ffe81f0 4820
run5 399d106eac03b1ec lines=100001 first=0 1000000000000000000 fff40f80 1ffe81f0 4820
```

**The executed set saturates inside W1.** Measured over the three traces:

| corpus | records | distinct word addresses fetched |
|---|---|---|
| W1 (fetches 0 – 5,000,000) | 5,000,000 | **656** |
| the whole 10 s run, uncapped (5,409,054 records; measured in this session, trace not retained) | 5,409,054 | 656, and **the same 656** |
| W2 (fetches 5,000,000 – 10,000,000, a 30 s run) | 5,000,000 | 371, and a **strict subset** of W1's 656 |

So the GSP's entire 600-frame execution is a loop over 656 distinct word
addresses. The three hottest are fetched 1,186,984 times each; the next two are
fetched 324,880 times. It is a rasterisation loop, not a walk.

**Caveat, stated because it is measurable:** the hook's stdio buffer is 1 MiB
(`tms34010.cpp:296`) and is never flushed at process exit, so any trace that
ends without the marker loses up to one buffer from its tail. W1 does not have
that problem — the hook's own truncation branch `fclose()`s, which flushes, and
writes `# TRUNCATED at 5000000 fetches`, which the tool reports. A trace that
ends without that marker cannot be called complete.

---

## 2. The address fold, and the proof of it

A fetch record's third field is `pc_bits`, the TMS34010's own **bit** address
(`ROPCODE()` advances it by `2 << 3` and is the single choke point every executed
instruction passes through, `tms34010.cpp:234-243`). The share offset is

```text
share_offset = ((pc_bits - 0xff800000) >> 3) & 0x7ffff  ==  (pc_bits >> 3) & 0x7ffff
```

`harddriv.cpp:628` installs `map(0xff800000, 0xffbfffff).mirror(0x0400000)
.ram().share("gsp_vram")` over a 512 KiB backing, so one address unit is one bit
and the window holds **eight aliases** of the share. `tools/disasm-gsp`'s own
docstring records the derivation; what is new here is the check that it is not
merely plausible.

Every trace record carries the opcode the core read. Comparing it against the
frame-404 image at each candidate fold:

```console
$ IMG=/tmp/opencode/gspx/cap-observer/gsp-vram-upload-last/gsp_vram.bin
$ python3 -c "
img=open('$IMG','rb').read()
def word(o): return img[o]|(img[o+1]<<8)
recs={}
for line in open('/tmp/opencode/gspx/w1.txt'):
    if line.startswith('#'): continue
    p=line.split()
    if len(p)==5: recs.setdefault(int(p[2],16), int(p[4],16))
cands = {
 '(pc_bits >> 3) & 0x7ffff   <- tools/check-gsp-executed': lambda pc: (pc>>3)&0x7FFFF,
 'pc_bits & 0x7ffff          <- tools/gsp-program-map:88': lambda pc: pc & 0x7FFFF,
 '(pc_bits >> 4) & 0x7ffff':  lambda pc: (pc>>4)&0x7FFFF,
 '(pc_bits >> 2) & 0x7ffff':  lambda pc: (pc>>2)&0x7FFFF,
 'pc_bits & 0x3fffff':        lambda pc: pc & 0x3FFFFF,
 '0 (control)':               lambda pc: 0,
}
for name,f in cands.items():
    m=sum(1 for pc,op in recs.items() if 0<=f(pc)<0x80000 and word(f(pc))==op)
    print('%4d/%d records land on the matching word   %s'%(m,len(recs),name))
"
 656/656 records land on the matching word   (pc_bits >> 3) & 0x7ffff   <- tools/check-gsp-executed
   0/656 records land on the matching word   pc_bits & 0x7ffff          <- tools/gsp-program-map:88
   0/656 records land on the matching word   (pc_bits >> 4) & 0x7ffff
   0/656 records land on the matching word   (pc_bits >> 2) & 0x7ffff
   0/656 records land on the matching word   pc_bits & 0x3fffff
   0/656 records land on the matching word   0 (control)
```

A fold that is wrong by a factor of two or eight does not produce *different
answers*; it produces **none at all**, because the image is not self-similar at
those scales. (Share offset `0x40F50` — where the alternative fold puts the
hottest word — holds `0xa5a5`, not the `0x07E0` the trace recorded there.)

---

## 3. The four buckets

```console
$ tools/check-gsp-executed \
    /tmp/opencode/gspx/cap-observer/gsp-vram-upload-last/gsp_vram.bin \
    --fetch-trace /tmp/opencode/gspx/w1.txt
share   /tmp/opencode/gspx/cap-observer/gsp-vram-upload-last/gsp_vram.bin  524288 bytes  sha256 47e41d1b9d54055d965074ac2b6e9e0b844f024006e2555f6b8a17543d4a2715
        262144 word addresses, 32768 16-byte cells; this capture replicates each cell's word 8x: False
trace   /tmp/opencode/gspx/w1.txt  5000000 records, 656 distinct word addresses, truncated=True, unaligned=0, distinct time values=1
fold    (pc_bits >> 3) & 0x7ffff: 656 opcode(s) agree with the image, 0 disagree
decode  67683 words decode, 194461 are DW, 98 distinct mnemonics
        252601 words from 8 linear sweeps (one per 2-byte residue class), 9543 decoded individually after a sweep stepped over them

 bucket    count  meaning
      A      656  fetched, decodes -- code MAME reads
      B        0  fetched, does NOT decode -- REAL, needs our decoder
      C    67027  not fetched, decodes -- data that looks like code
      D   194461  not fetched, does not decode -- data
  total   262144
```

| bucket | count | share |
|---|---:|---:|
| **A** fetched, decodes | **656** | 0.25 % |
| **B** fetched, does not decode | **0** | 0 % |
| **C** not fetched, decodes | **67,027** | 25.6 % |
| **D** not fetched, does not decode | **194,461** | 74.2 % |
| total | **262,144** | 100 % |

### 3.1 The unit is the word, and §85.1's unit is not this unit

§85.1 counted **32,768 cells** (16 bytes). This counts **262,144 words**
(2 bytes), and the capture genuinely holds that many distinct words: a 2-byte
stride over the frame-404 image returns exactly the **81,899** nonzero words the
capture's own `result.json` records, while a 16-byte stride returns 10,539 —
one word in eight. That is AGENTS.md trap 11, confirmed on this image, and it
is why the tool decodes at word resolution. (Trap 11 quotes 10,989 for its
16-byte figure; on **this** capture it is 10,539. Different image or different
run — `UNKNOWN` which, and not load-bearing here.)

§85.1's own numbers reproduce exactly with one command, which is worth stating
because it means the disagreement below is about *meaning*, not arithmetic:

```console
$ IMG=/tmp/opencode/gspx/cap-observer/gsp-vram-upload-last/gsp_vram.bin
$ tools/disasm-gsp $IMG --at 0 --count 32769 \
    | awk '{if ($3=="DW") d++; else r++} END {print "cells reported:", r+d, " real instructions:", r, " DW:", d}'
cells reported: 31593  real instructions: 7411  DW: 24182
```

7,411 decoded + 24,182 `DW` = 31,593 = the number of 16-byte cells a linear
sweep reports; 32,768 − 31,593 = **1,175** cells were stepped over as parameters
of multi-word instructions. §85.1's table is reproduced to the digit.

### 3.2 …and 94 % of §85.1's "real instructions" are not instructions

```console
$ IMG=/tmp/opencode/gspx/cap-observer/gsp-vram-upload-last/gsp_vram.bin
$ tools/disasm-gsp $IMG --at 0 --count 32769 > /tmp/opencode/gspx/cells.txt
$ python3 -c "
import sys
ex=set()
for line in open('/tmp/opencode/gspx/w1.txt'):
    if line.startswith('#'): continue
    p=line.split()
    if len(p)==5: ex.add((int(p[2],16)>>3)&0x7ffff)
lo,hi=min(ex),max(ex)+2
ins=[0,0,0]; outs=[0,0,0]
for line in open('/tmp/opencode/gspx/cells.txt'):
    o=int(line.split(':')[0],16); d = line.split()[2]!='DW'
    t = ins if lo<=o<hi else outs
    t[0]+=1; t[1]+=d
print('executed span 0x%05X-0x%05X'%(lo,hi-2))
print('cells inside : %5d, decoded %5d'%(ins[0],ins[1]))
print('cells outside: %5d, decoded %5d'%(outs[0],outs[1]))
print('total decoded: %5d'%(ins[1]+outs[1]))
"
executed span 0x68000-0x6B3B8
cells inside :   586, decoded   444
cells outside: 31007, decoded  6967
total decoded:  7411
```

| | cells that decode |
|---|---:|
| inside the executed span `0x68000`–`0x6B3B8` | **444** of 586 |
| everywhere else in the share | **6,967** of 31,007 |
| total | 7,411 |

**6,967 of the 7,411 "real instructions" §85.1 recorded are at word addresses
the GSP never fetched** in a 600-frame power-on run — 94 % of the census.

### 3.3 Bucket B: empty, and that is a real result

**B contains zero addresses.** All 656 words the GSP fetched are decoded by
MAME's `34010dsm.cpp`, across 98 distinct mnemonics. There is no TMS34010
instruction in this program that MAME cannot name.

What that implies, precisely:

- **Nothing changes what we implement.** The bead asked for a decoder if B were
  non-empty. It is not, so no TMS34010 decoder is owed and none should be
  written.
- **It is not a claim that MAME's decoder is complete for the TMS34010.** The
  measured population is 656 words out of a possible 65,536 encodings. SPVU001A
  §8.7 Table 8-6 lists 9,984 architecturally illegal opcode words that MAME also
  renders as `DW` (S9), and the synthetic whole-space sweep recorded in
  `tools/disasm-gsp`'s docstring reaches 115 mnemonics + `DW` — so gaps exist,
  they are just not in this program. A program that does not use an encoding is
  not a decoder gap. **B empty is about this program, not about the part.**
- **It is a lower bound over the observed window.** One input-free 600-frame
  power-on run is one corpus. A run that reaches gameplay, or the second half of
  the attract loop, could fetch a word this one did not. §3.2 is the reason to
  keep looking: the decode census covers 262,144 words and the executed set
  covers 656, so the space between them is unexamined by execution.

### 3.4 Buckets C and D

- **D, 194,461 words in 3,646 ranges.** One range dominates: `0x00000`–`0x3BFFE`,
  122,880 words (245,760 bytes) of the share's low half, below the recorded
  upload range `$FFF40000`–`$FFFFFFE0` (physical `0x68000`–`0x7FFC0`). The other
  3,645 ranges are mostly 1–2 words (median 2) inside the uploaded program, with
  a longest of 39,087 words.
- **C, 67,027 words in 3,612 ranges**, median 2 words, longest 17,548. This is
  the class the bead calls out: data that decodes. It is 100× the executed set,
  and it is why a decode census cannot be read as a code census.
- **A, 656 words in 370 ranges** (median 3 words, longest 17), spanning
  `0x68000`–`0x6B3B8` (share bytes; lowest GSP bit aliases `0xffb40000`–
  `0xffb59dc0`; the trace itself was taken at alias 7 of the same offsets, e.g.
  `0xFFF40F50` for `0x681EA`).

---

## 4. Negative controls

The tests in `tests/test_gsp_executed.py` synthesise their own share and trace.
The two that matter mutate **only the trace**, never the share bytes:

| address | share word | not in the trace | in the trace | moved |
|---|---|---|---|---|
| `0x00000` | `0x8484` (`MOVE *A4,A4,0`) | bucket **C** | bucket **A** | C → A |
| `0x00002` | `0x0000` (`DW`) | bucket **D** | bucket **B** | D → B |

```console
$ python3 -m unittest tests.test_gsp_executed
............
----------------------------------------------------------------------
Ran 12 tests in 23.763s

OK
```

The `D → B` row is also the reachability control for bucket B: the measured
corpus has B empty, so without it a tool that never produced a B would pass
everything else in the file.

**The tests were seen to fail against a deliberately broken tool** — three
mutations, each applied and reverted:

| mutation | result |
|---|---|
| `fetched_here = True` (classify by decoding only) | `FAILED (failures=3)` — both negative controls and the B-list control |
| accept any image size | `FAILED (failures=4)` — the 4 MiB-window refusal |
| accept a file with no fetch records | `FAILED (failures=1)` |

Full public suite with the new file: **362 tests, OK, 88.7 s.**

---

## 5. A discrepancy in another tool — reported, not fixed

`tools/gsp-program-map:88` folds a trace with

```python
fetched.add(pc & (SHARE_BYTES - 1))     # pc is pc_BITS; this masks with the BYTE size
```

Section 2 measures that fold at **0 of 656** records against the correct
`(pc_bits >> 3) & 0x7ffff`. It should presumably use the trace's own fourth
column (`pc_byteaddr` = `pc_bits >> 3`) masked to the share, or the shifted form
above. **I did not edit that file** — this bead's write scope is its own three
files, and its output is a function/entry table whose `reached` flags and
`last_fetched_offset` inherit the wrong fold. Consequences for anything that
read those numbers, all reported rather than fixed:

- `last_fetched_offset` and the `reached` column;
- `tools/gsp-program-map`'s own docstring, which describes the same linear decode
  as running "from 0x340000 to 0x365c90 … the physical offsets are 0x40000 to
  0x65c90", and explains the window as "a capture read before the window was
  distinguished from the 512 KiB share". Under the correct fold the executed code
  starts at physical `0x68000`, inside the upload range `$FFF40000`–`$FFFFFFE0`
  that `STATUS.md` records. Share bytes `0x40000`–`0x67FFF` hold 81,920 words of
  which only 1,244 are distinct, the commonest being `0x0000` (18,961) and
  one-byte-repeated patterns such as `0x8282`, `0xb8b8`, `0xaaaa`; §3.2 shows
  94 % of §85.1's census is there. The docstring's *explanation* is also stale for
  a second reason: §84 (`0fef4c6`) already made the window/share distinction;
- `tools/disasm-gsp`'s docstring and driver comment, which state that "one
  instruction word occupies one 16-byte cell, replicated 8x". For this capture
  that is **false** (§3.1: a 2-byte stride reproduces the metadata's 81,899
  nonzero words exactly, a 16-byte stride returns 10,539). The conclusion the
  comment draws from it — the disassembler's position units are already bytes,
  do not divide by 8 — is independently correct and `disasm-gsp`'s output is
  unaffected; only the stated reason is wrong. This is AGENTS.md trap 11 /
  `stun-runner-a9q` seen from the other side.

Whether `0x40000` was ever a meaningful program origin is **`UNKNOWN`**; the
correct fold and the upload range agree, and I did not run anything further.

---

## 6. What this does not say

1. **Not that the program has 656 instructions.** 656 distinct words were
   *fetched*. Parameter words, the display list, and any code reached only in a
   different run are outside it.
2. **Not that C and D are data.** They mean "not fetched in W1". W1 is bounded
   at 5,000,000 fetches, so the tool prints a truncation warning and C/D are
   lower bounds. That W2 — a disjoint later window of a longer run — added
   *nothing* is what keeps the warning from dominating the result; it is
   evidence, not proof.
3. **Not anything about gameplay.** No input was injected. `STATUS.md`'s
   open `stun-runner-9m4` (INTOUT cadence) and `stun-runner-6tf` (JR condition
   table, where MAME's disassembler and executor are argued to disagree on
   condition `0x0001`) are untouched; §85.1 measured all 18 JR variants present
   in the *decoded* program, and this note does not revisit which of them the
   GSP executes.
4. **Not a claim about the share above 512 KiB.** Eight aliases of the share
   fill the window; whether the real board aliases there or leaves it unwired is
   `UNKNOWN` (Margolin, S3, withheld that schematic section; findings §84.2).
5. **Not about the trace's time column.** Every record in every trace measured
   here carries the same `time_attoseconds` (1000000000000000000, exactly 1.0 s).
   The column is frozen and cannot be used to window a trace; the tool counts
   distinct values and reports it rather than trusting it. Whether that is the
   hook or the machine is **`UNKNOWN`** — the hook reads
   `machine().time().as_attoseconds()` inside `ROPCODE()`, which ought to vary.
   This is a candidate MAME-instrumentation finding for `BUGS.md`, which this
   bead does not own.

## 7. Gaps left for the parent

- **The fetch-trace run has no committed tool.** §1.3 gives the exact argv and
  env; the wrapper that invokes it through `mame_harness/process.run_mame` was a
  throwaway script in `/tmp`. AGENTS.md's "promote throwaway scripts into
  `tools/`" applies and is not discharged here, because this bead's write scope
  is fixed at three files. It is worth a `tools/capture-gsp-fetch-trace`.
- **`stun-runner-8y8`'s ADSP half is untouched.** `analysis/mame-local-patches.json`
  records eight observation-only tags and no ADSP hook, so
  `stun-runner-yuv` still gates it.
- **§85.2's 224 wholly-`???` ADSP addresses** remain unclassified. This note is
  the GSP half of the bead only.

## Citation register

**S3** (Margolin) — the eight-alias / withheld-schematic point, §6.4.
**S6** (SP-338) — the 512 KiB share size, via §34. **S7** (`harddriv.cpp:628`) —
the map, the mirror and the share. **S8** (`tms34010.cpp`) — `ROPCODE()`, the
`STUNRUN-FETCH` hook, and its 1 MiB stdio buffer. **S9** (SPVU001A, **not held
locally**, cited by section) — §8.7 Table 8-6's 9,984 illegal opcode words, in
§3.3.