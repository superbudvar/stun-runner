# The GSP program-upload boundary

> **Correction (2026-10-13):** this document's 4 MiB physical-share and
> 16-byte-HSTDATA-cell conclusions are superseded by
> `docs/mc68010-boot-findings.md` §34. The physical share is 512 KiB (16 ×
> 64K×4 chips, confirmed by a direct MAME `m_gsp_vram` share dump); the GSP map
> is a 4 Mi **bit-address** window because TMS34010 uses `addrshift=3`. The old
> 4 MiB Lua image is an address-space sample, not backing-share bytes. The
> ordered host-port event attribution and upload/release boundary below remain
> useful CPU-side evidence. Do not use the old VRAM image, share offsets or
> 16-byte-cell arithmetic as physical-memory evidence until re-derived.

## What was captured, and how to reproduce it

```text
./tools/capture-gsp-program-upload roms OUTDIR --frames 600 \
    --mame /mnt/build/mame-ref/mame
```

The tool runs in two phases. Phase 1 records the ordered 68010 host-port
transaction stream and reports the halt-release boundary. Phase 2 images the
whole 4 MiB share at the two frame edges around that boundary with the existing
`tools/mame-gsp-vram-dump` reader. Experiment record:
`reference/experiments/stunrun/gsp-program-upload.metadata.json`. The raw event
log is ~40 MiB and stays local; its hash is recorded in the metadata.

Two independent runs produced an identical event stream (sha256
`5640860abca7c79b…`), an identical boundary, and byte-identical VRAM images.

## The boundary is an HSTCTL halt release, not /GSPRES

The obvious guess is that the GSP starts when `/GSPRES` is released. It is not.
`/GSPRES` goes released once at power-on, at event 2, before a single word has
been written, and the board then parks the GSP with HSTCTL bit 15 (HLT). The
capture takes four `/GSPRES` transitions, all pairs at frames 362 and 396; the
GSP is running before both of them and stays halted across them.

| Event | Frame | Event index | Writer PC | Value |
|---|---:|---:|---:|---|
| `/GSPRES` assert | 0 | 1 | `$000003FC` | — |
| `/GSPRES` release | 0 | 2 | `$00000406` | 0 host data writes so far |
| HSTCTL halt assert | 31 | — | `$00000570`, `$00000636` | `$B800` |
| HSTCTL halt assert | 38 | — | `$00022464` | `$B800` |
| `/GSPRES` assert / release | 362 | 511,797 / 511,798 | `$00022370` / `$0002237A` | after 511,761 host writes |
| `/GSPRES` assert / release | 396 | 618,421 / 618,422 | `$00022370` / `$0002237A` | after 615,226 host writes |
| HSTCTL halt assert | 396 | — | `$0002FE92` | `$B800` |
| **HSTCTL halt release** | **405** | **631,205** | **`$0002C1C4`** | **`$7800`, after 627,943 host writes** |
| HSTCTL further clear | 405 | — | `$0002C1C8` | `$3800` |

`$0002C1C4` and `$0002C1C8` are six bytes apart, so they are adjacent
instructions in one routine, and `$0002146E` then writes HSTCTL 54 times between
frames 406 and 597 while the GSP is running.

Capturing on the first `/GSPRES` release would produce a correctly sized,
entirely zero image that looks like a valid capture. The tool therefore keys on
an HSTCTL HLT release that *follows* host data writes, and rejects a zero image.

## The uploaded range is the one the GSP then executes

`$02FE68` and `$02FE6E` write 6,091 words each across frames 396–405, immediately
before the release, covering `$FFF40000`–`$FFFFFFE0` (about 786 KiB). Every
sampled GSP PC after the release lies inside that range:

```text
frame 406  $FFF42E50      frame 411  $FFF414C0
frame 407  $FFF40F90      frame 412+ $FFF41060 (repeat)
```

~~So the program the 68010 loads and the program the GSP runs are the same
block. This is the answer to "is there more than one program": there are several
bulk upload phases, but only this one is executed, and the range is now pinned
rather than inferred from a frame-600 dump.~~

**REFUTED — see §"The share contains no instruction stream" below (2026-10-13).
This range is a sparse parameter table, not a program. The address range is
pinned; the claim that it holds code is wrong.**

Other large writers in the same capture, for context and to keep them from being
mistaken for the program:

| Writer PC | Words | Frames | Destination | Note |
|---|---:|---:|---|---|
| `$022416` | 262,144 | 38–81 | `$FF800000`–`$FFBFFFF0` | fills the first 512 KiB of the share |
| `$00069A` | 224,522 | 1–31 | `$00000000` | destination latch never set; see unknown below |
| `$0226F8` | 78,876 | 375–442 | `$FFE00000`–`$FFEEFDB0` | spans the boundary; overlaps `$02FE68` |
| `$02FE68` / `$02FE6E` | 6,091 each | 396–405 | `$FFF40000`–`$FFFFFFE0` | **not a program** — sparse table, see below |

`$00069A` writing 224,522 words to address `$00000000` is an unresolved oddity.
HSTADR is genuinely never set before those stores, so the derived destination is
the real composed latch value — but a destination of zero is not a mapped cell
of the share (`$FF800000`–`$FFBFFFFF`), so this is either a run whose effects
are not VRAM stores or a decode this capture does not yet explain. **UNKNOWN**;
recorded rather than smoothed over.

## The GSP's first output is separate from the upload

Diffing the two images (`tools/diff-gsp-upload-images`) gives **10,360 words
changed in 22 contiguous ranges**, all within `$FFB67530`–`$FFB6CC80`:

```text
changed_words      10360        newly_nonzero   10360
unchanged_words  2086792        newly_zero          0
                                 modified            0
```

Every range was zero before and is a **single repeated constant word** after —
e.g. 4,096 words of `0x7F00`, 1,424 words of `0xE100`, 768 words of `0x0505`.
Nothing was overwritten: the GSP is filling previously untouched memory with
uniform values, which is what a rasteriser clearing or filling a window looks
like, and it matches the `$FFF454E0` "FILL XY" landmark already in
`analysis/gsp-display-list-capture.md`.

Three limits on that sentence:

* the difference is **up to one frame of GSP execution**, because the images are
  taken at frame edges, not inside the release write. It is not the upload;
* the earlier image already contains whatever ran during the pre-upload
  `/GSPRES` release, so it is not "just the upload" either;
* the fill values are reported as raw words. No palette, stride, or field meaning
  is claimed.

## Instrumentation traps in the Lua tap path

These are properties of how MAME's pass-through handlers work, and two of them
cost us a wrong conclusion. None is a fact about the board, and none is a MAME
defect — see `docs/mc68010-boot-findings.md` §28 for the refutation of our
earlier claim that they were.

| Behaviour | Measured | Why it matters |
|---|---|---|
| A handler can be **silently dropped** by a handler change | 1000 tap events without a VRAM read, **0** with one; `passthrough:reinstall()` restores it (29,287 → 139,120) | A script counting its own events cannot tell "the board did nothing" from "my handler went away". |
| Callbacks are **not coroutines**, once per access (~500,000/min) | table per event → segfault; `file:write` → segfault truncated at event 339; growing a table → silently stops at 1493; preallocated ring → clean | Anything per-event is a hazard by construction. Preallocate, format after the run. |

The first row is the one that bit us. MAME documents it
(`ref-mem.rst:197-200`: `reinstall()` "may be necessary if the handler is removed
due to other changes to handlers in the address space"), and we had not read
that. The documented remedy works; the problem is only that the removal is
silent. The tool therefore images VRAM in separate runs and still refuses to
emit a capture with zero host events.

Two more traps worth keeping:

* MAME hands a write tap the **full address**, not a range-relative offset.
  Comparing against `0x02`/`0x04`/`0x08`/`0x0c` records nothing and still looks
  like a clean run.
* The `destination` column must apply the HSTCTL `0x0800` post-increment. Read
  off the raw latch, a 262,144-word contiguous fill reports *one* repeated
  address, and "which range did this writer fill" becomes unanswerable. The
  first draft of this tool had that bug.

## Open

* `$00069A`'s 224,522 writes to a zero destination.
* Whether the two writers `$02FE68`/`$02FE6E` are interleaved halves of one
  32-bit fill or two independent runs; their ranges overlap by a 0x10 shift.
* Whether the GSP executes from the `$FFE00000` region that `$0226F8` also
  writes, or only from `$FFF40000`–`$FFFFFFE0`. The sampled PCs are all in the
  latter but only 12 frames were sampled.
* What consumes the `$FFF41060` sentinel loop, and whether the first-frame fill
  window is the screen, a tile page, or a command buffer.
---

## The share contains no instruction stream (2026-10-13)

The claim above that `$FFF40000`–`$FFFFFFE0` is *the executed program* is
**refuted**. The upload is real and the boundary is real; what it carries is not
code. Everything in this section was checked live against the pinned MAME.

### The share is 16-byte-replicated

Walking the whole 4 MiB share as 262,144 cells of 16 bytes each:

```text
16-byte cells in the whole 4 MiB share: 262144
  uniform (all 8 words identical)     : 262144
  NON-uniform                          : 0
```

**Not one cell in 4 MiB holds two different words.** A TMS34010 instruction
stream cannot live in a memory where every 16 bytes is one repeated word, so
the share contains no program.

Reproduced directly, reading live at frame 450:

```text
read_u16 @0xFFF40000 : 0550 0550 0550 0550 0550 0550 0550 0550
read_u16 @0xFFF40008 : 0550 0550 0550 0550 0740 0740 0740 0740
read_u16 @0xFFF40010 : 0740 0740 0740 0740 0740 0740 0740 0740
read_u16 @0xFFF40100 : 09E0 09E0 09E0 09E0 09E0 09E0 09E0 09E0
```

The unaligned read at `+8` returns the first cell's value for its lower four
words and the *next* cell's value for its upper four. So the address is not
being masked — VRAM genuinely holds each host word across 16 bytes. The true
content of the share is 512 KiB of information, replicated 8× into the 4 MiB
image `tools/mame-gsp-vram-dump` writes. That tool is not misreading; the
memory is genuinely this redundant.

### The upload is sparse, and each word is broadcast

Per-writer destination deltas over frames 396–405, from
`gsp-upload-events.txt`:

```text
writer $0002FE68: 6091 writes, dest span 0xfff40000..0xffffffe0
   deltas: {32: 6088, 48: 1, 591536: 1}
writer $0002FE6E: 6090 writes, dest span 0xfff40010..0xffffffd0
   deltas: {32: 6087, 48: 1, 591536: 1}
```

A constant **32-byte stride**, two writers interleaved at `+0x0` and `+0x10`.
Each writer supplies one word per 32-byte cell, and the share stores it across
all 16 bytes of its half-cell. This is a parameter/command table laid out for
strided fetch by the GSP, not an instruction sequence.

### The image is nonetheless trustworthy

Checked every in-share `HSTDAT` write against `gsp_vram.bin` at its own offset:

```text
host HSTDAT writes into the upload range, frames 390-405: 12183
image matches written value at its own offset: 12182/12183
```

and the frame-405 and post-release images agree on 331,244 of 419,890 in-share
host writes (the remainder are addresses the running GSP overwrote afterwards,
since GSP-originated stores are not in the host event log).

So `tools/mame-gsp-vram-dump` faithfully records VRAM. What it faithfully
records is a memory with no code in it.

### Consequences for earlier claims

| Claim | Status |
|---|---|
| Upload boundary is HSTCTL bit 15 (HLT), release at frame 405 | **stands** — host-write validated |
| `$02FE68`/`$02FE6E` cover `$FFF40000`–`$FFFFFFE0` | **stands** — pinned above |
| "Every post-release GSP PC lies inside that range" | **stands**, but proves only that the GSP is fetching from the table area |
| "That range is the executed program" | **refuted** — it holds no instruction stream |
| "10,360 words of first GSP output in 22 ranges, each a single repeated constant" | **reinterpret** — a constant across a 16-byte cell is the *expected* shape of any host write, so this measures replication, not drawing |
| "The reset vector at `$0FFFFFFE0` is `$0x9920`" | **withdrawn** — a 32-bit read there composes `$9920` with the *next* cell's `$0FFF5`; a vector of `0x00009920` is not a VRAM address, and both halves come from replicated table data |

### Where that leaves the program

`stunrun` has **no GSP ROM region** — the ROM definitions are `maincpu` (68010),
`jsa:cpu`, `user1` (ADSP), `jsa:oki1`, `200e`, `210e`. VRAM is the only memory
the GSP can execute from, and VRAM holds no code. Two readings remain open:

1. the GSP does not actually execute in this driver, and the sampled PC values
   come from a source that has not been identified; or
2. its code arrives by a path this capture does not record — most plausibly the
   display-list/FIFO write path rather than HSTDAT.

Both are **UNKNOWN**. Distinguishing them needs execution evidence, which is
exactly what is missing: the GSP execute tap is unavailable on this build
(`gsp_execute_tap_status: unavailable`), so `tools/mame-gsp-boundary` exits
non-zero rather than publishing an empty `execution_landmarks`.

Do not call this range a program, in these or any other words, until
execution evidence says so.

---

## CORRECTION (2026-10-14): the share does contain the program

The section above is **wrong**. `$FFF40000`–`$FFFFFFE0` does hold an instruction
stream, and the GSP executes it. This was refuted by direct fetch evidence, not
by argument — see `analysis/gsp-program-upload.md`.

### What the refutation got wrong

The observation was correct and the inference was not: **all 262,144 sixteen-byte
cells of the share hold one word replicated eight times**. I read that as a data
fill and concluded there could be no code.

But `m_pc` on this part is a **bit** address — `ROPCODE()` advances it by
`2 << 3`, and MAME's own `34010dsm.cpp` advances its read position by 16 units
per word. A bit-addressed PC walking a byte-array sees each word through a 16-unit
stride, and the share presents its words that way. **The 16-byte replication is
how MAME presents memory to a bit-addressed program counter. It is a property of
the address space, not evidence about what the data is.**

I had this warning twice already — in `tools/check-gsp-replay`, and in the
`gsp-vram-dump.lua` header about stride-sampled readers — and still drew the
conclusion from structure alone instead of waiting for execution evidence.

### What the fetch trace shows

`STUNRUN-FETCH`, 400,000 fetches, observer build:

```text
fetches: 400000   distinct pc: 656   distinct opcodes: 309
traced opcodes present in the share image: 400000/400000 (100.00%)
pc(bit) range: 0xfff40000 .. 0xfff59dc0
most-fetched pcs: 0xfff40f50 (78994), 0xfff40f80 (78994), 0xfff40f90 (78994)
consecutive pc deltas: {+48: 136487, +16: 116900, -64: 84283, -48: 54803, -16: 2653}
```

656 distinct addresses, 309 distinct opcodes, real control flow in both
directions, every traced opcode present in the share. That is an instruction
stream, and it is the uploaded block.

`0xfff40f90` and `0xfff414c0` appear in the most-fetched list. Those are the
"sentinel dispatch" PCs recorded before any of this — they were real execution
sites all along, sampled correctly and interpreted wrongly.

### Status of the claims in this document

| Claim | Status |
|---|---|
| Upload boundary is HSTCTL bit 15 (HLT), release at frame 405 | **stands** |
| `$02FE68`/`$02FE6E` cover `$FFF40000`–`$FFFFFFE0`, 32-byte stride | **stands** |
| Post-release PC samples lie inside that range | **stands**, now explained |
| "The share contains no instruction stream" | **REFUTED** — see above |
| "The share is 16-byte-replicated" | **stands** — but it is the address space, not the content |
| "10,360 words of first output, each a single repeated constant" | **stands** — a constant across a cell is expected for any host write; it measures writes, not drawing |
| "`$0FFFFFFE0` holds a `$0x9920` reset vector" | **withdrawn** — a 32-bit read composes `$9920` with the next cell's `$0FFF5`; both come from the table |
