# The GSP program-upload boundary

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

So the program the 68010 loads and the program the GSP runs are the same
block. This is the answer to "is there more than one program": there are several
bulk upload phases, but only this one is executed, and the range is now pinned
rather than inferred from a frame-600 dump.

Other large writers in the same capture, for context and to keep them from being
mistaken for the program:

| Writer PC | Words | Frames | Destination | Note |
|---|---:|---:|---|---|
| `$022416` | 262,144 | 38–81 | `$FF800000`–`$FFBFFFF0` | fills the first 512 KiB of the share |
| `$00069A` | 224,522 | 1–31 | `$00000000` | destination latch never set; see unknown below |
| `$0226F8` | 78,876 | 375–442 | `$FFE00000`–`$FFEEFDB0` | spans the boundary; overlaps `$02FE68` |
| `$02FE68` / `$02FE6E` | 6,091 each | 396–405 | `$FFF40000`–`$FFFFFFE0` | **the executed program** |

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

## Instrumentation defects found and worked around

These are defects in the installed MAME Lua bridge and in our first draft of the
tool. None is a fact about the board.

| Symptom | Measured behaviour | Response |
|---|---|---|
| Segfault, no Lua error | retaining one Lua table per event from a memory tap | preallocated ring buffer |
| Segfault, log truncated mid-line at event 339 | `file:write` from inside a memory tap | stream after the run |
| Callback silently stops after 1,493 events | growing a table from inside a memory tap | preallocate before installing taps |
| **Taps install cleanly and record nothing** | reading the GSP share from a frame-done callback | VRAM imaged in separate runs |
| Run 100× slower, 600 frames unfinished in 4 min | 4 MiB image per frame | boundary first, then two images |

The fourth is the dangerous one. The identical script records 1,000 events
without a VRAM read and **0 events** with one, exits cleanly in both cases, and
reports `consumer_capture_status=observed`. A tool that both dumps VRAM and
reports host-port traffic would have published "the 68010 never wrote to the GSP
host port" as a hardware finding. `tools/capture-gsp-program-upload` now refuses
to emit a result with zero captured events.

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