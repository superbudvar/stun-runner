# ADSP replacement image

## SOM span walk (`som_walk.c`)

`som_walk.c` transcribes the index/modifier walk at ADSP PCs `$0564`-`$05D1`:
the two header reads, then the merge of the two vertex chains until their
pointers meet. It owns the ADSP's **circular-buffer addressing**, which is not
`index + modifier` — the address is the current index, the modifier is applied
afterwards, and the index wraps inside the L register's window. That wrap is
load-bearing: the vertex data is a six-word ring, and it is what makes two of
the nine captured records take the back edge at `$056A`.

`adsp-som-walk-slice-c` derives all nine captured records from their DM rings
and entry pointers alone — header words, span count, each span's
`AX1/AY0/AY1`, both interpolated side values, and every run length — and
compares the assembled records against the capture.

`stunrun_adsp_som_derive_record` composes the whole `$0514`-`$05D1` path —
control word, the `$052F`-`$0544` pointer scan, the walk, and the
interpolation. It is the first entry point with **no measured input**: give it
the data-memory window covering `DM($0036)`, `DM($0039)` and the ring, plus the
coefficient table, and it produces the complete record. The test assembles all
146 captured SOMLATCH words that way and compares them word for word — no
pointer, span list, side value or record word is supplied by hand anywhere in
the path.

Both derived quantities come from one word: `DM($0039)` is the loop counter and
doubles into the ring length `L0`. `$052F`-`$0544` steps one word at a time
(`M0 = +1`), unlike the walk's stride of two, and it reproduces the captured
entry pointers for all nine records. The coefficient table at `PM[$1036 + n]` is
exactly `floor($8000/n)` for `n = 1..$40` and zero at `n = 0`, byte-identical at
frames 551 and 600 — so each helper divides by its own side's extent.

Two coverage gaps, both deliberate and recorded in findings §71:

- `$053D` has **zero** reads across the whole capture, so the `$053D`-`$0541`
  block (the `I2` comparison, its `IF LT` guard, and the `I2 = I3` update) is
  transcribed from the disassembly and is **not** exercised by the test.
- The `> $40` scaled branch of both helpers is unimplemented, and the helper
  declines those selectors rather than guessing.

The DM window is a capture limit: a read outside it fails rather than guessing.

## Source tuples to the pass-2 ring (`som_producer.c`)

`som_producer.c` transcribes the observed `$04B2-$04DF` two-pass producer for the
frame-551 path. It consumes the ten words read from `DM($0040..$0049)` (three
`x,y,SIMCLK` tuples plus the last, unused `$04C9` x prefetch), resolves each
`SIMCLK` through an MP=1 object-ROM word image, computes the six pass-1 stores,
then performs the `$04D6-$04DC` pass-2 transform into the ring consumed by
`som_walk.c`. The test uses the actual observed source words and the two observed
SIMBUF values in a sparse synthetic ROM image; it does not vendor the commercial
object ROM.

Findings §72 shows that the pre-pass-1 ring writers are overwritten in this
capture. The pass-2 result is the only writer output the emitter reads, and the
following clamp at `$04EB-$050D` preserves it byte-for-byte for all nine
observed records. The measured write histogram was:

| writer | writes | survives to the emitter? |
|---|---|---|
| routine `$01E8` (`$0201`/`$0206`/`$020F`) | 3 words | no — overwritten by pass 1 |
| scan at `$01B0` (`$01B8`/`$01BA`/`$01BC`) | 3 words | no — overwritten by pass 1 |
| pass 1 of `$04B2` (`$04C8`/`$04CA`) | 6 words | no — differs from pass 2 in 9/9 |
| pass 2 of `$04B2` (`$04D9`/`$04DC`) | 6 words | **yes** |
| clamp of `$04EB` (`$050D`) | 6 words | byte-identical to pass 2 in 9/9 |

`adsp-som-producer-slice-c` checks both intermediate pass-1 values and all six
final ring words for each of the nine records, then substitutes only the derived
ring into the existing `$052F-$05D1` pointer scan/walk and compares all nine
16-word SOM records. The current test fixture has observed SIMCLK addresses
`$208C`/`$23AC`, whose captured `/MP=1` SIMBUF values are `$07DE`/`$072D`.

This is still a bounded routine slice, not an integrated native ADSP producer.
Unexercised paths remain: `$04C3`'s `IF LE` sibling at `$04E0-$04EA`, the
`DM($1FE4)==0` skip at `$04CD`, and the clamp's two assignments. The test rejects
the alternate SIMCLK branch rather than guessing it. Inputs and output meanings
remain numeric; this does not identify an object, coordinate frame, or road
geometry.

## Looking at it: `./run.sh --adsp-som`

Everything below existed only in slice tests, so this is how you see it:

```sh
./run.sh --adsp-som                  # derive, self-check against the capture, draw
./run.sh --adsp-som --png out.png    # ...and write the frame
STUNRUN_ADSP_SOM_WINDOW=1 ./run.sh --adsp-som   # ...in a window
```

No ROM, no MAME, no Verilator: the transaction it derives is committed under
`reference/`, so it works on a machine that has never run the game. It prints
`derived words matching the capture: 9/9` and exits non-zero if that is not 9 —
that number is the claim; the picture is the view.

The picture is **not** the game's rendering. The GSP's consumer of the SOM
record is not decoded (`q9t`), so the axes are an index space defined in
`adsp_som_view.c` and labelled with raw values, the bar colour is a documented
ramp over `DM($003C)`/`DM($003D)` rather than a palette, and `AY0 != AY1` in
eight of eighteen spans with nothing here claiming what the larger one means.
See findings §73.

## SOM record emitter (`som_record.c`)

`som_record.c` transcribes the output sequence of the SOM record emitter at ADSP
PCs `$0515`-`$05D4`. Each record is four header words (`control`, `start_x`,
`delta_x`, `start_y`), then **N** five-word spans, then a two-word `$FFFF`
terminator. A span is `DM($003E)`, `DM($003F)`, `DM($003C)`, `DM($003D)`, then a
run length. All three span branches (`$059D`, `$057D`, `$05B7`) reduce to
`min(AY0,AY1) - AX1 + 1`, verified against all eighteen spans in the capture.

`adsp-som-record-slice-c` reassembles the complete captured 146-word transaction
from the nine measured records and compares it word for word with
`reference/experiments/stunrun/adsp-som-transaction-f551/result.json`. Fourteen
rebuilt negative controls fail.

Scope limits, all deliberate: this file implements the **output** sequence —
which word holds which value, in what order — not the walk that chooses the
spans, which lives in `som_walk.c`. No field is given a geometric meaning, and
the header word order is not discriminated by the capture (all nine records
latch `DM(I1,M1)` and `DM(I2,M2)` to equal values), so that order rests on the
disassembly and is pinned by a labelled synthetic vector.

## First SOM math kernel (`som_math.c`)

`som_math.c` is a small C transcription of an ADSP operation observed in
the SOM producer: the low-index branch of the interpolation helper at ADSP PCs
`$05DD` / `$0607`. It looks up `MY0` from PM, reads the two DM samples selected
by its caller's I registers, forms the signed 16-bit difference, executes
`MR = AR * MY0 (SU)`, and returns the `MR1:MR0` pair.

This is **not** an ADSP core, not the whole `$05DD`/`$0607` helper (the branch
for `AR > 0x40` is still unimplemented), and not a geometry generator.
The MAME-backed vectors are the two math pairs in
`reference/experiments/stunrun/adsp-som-transaction-f551/metadata.json`; the raw
same-run trace is `result.json`. `adsp-som-math-slice-c` checks both observed
products and rejects unsupported/bad input. The build uses the same source in
the standalone test and future native ADSP replacement slices.

The transaction itself is captured by:

```sh
tools/capture-adsp-som-transaction roms OUT \
  --frames 600 --start-frame 1 --post-frames 5 \
  --mame /mnt/build/mame-oracle/mame
```

The capture uses `--input none`; it records the SOMCLK→SOMLATCH→GINT interval,
the host bank state, and the 68010 acknowledgement. It does not claim that all
post-GINT HSTDAT traffic belongs to this SOM block.

The first M2 slice is a deliberately minimal ADSP-2100 program image. It uses
the known `NOP` encoding from the M0 emission proof, preserves the four-word
ADSP reset-vector area at `0x0000`, and records the MAME-confirmed reset entry
point at program address `0x0004`.

Build the image without ROM contents:

```sh
tools/build-replacement-image \
  --processor adsp2100 --fixture nop \
  --output /tmp/stunrun-m2-adsp-image
```

Validate placement and bounded execution in the pinned MAME runtime:

```sh
tools/mame-replacement-image \
  /tmp/stunrun-m2-adsp-image/image.json \
  --rompath /path/to/private/stunrun-roms \
  --output /tmp/stunrun-m2-adsp-run
```

The loader waits until after the original title-path upload, writes the fixed
image into the MAME-mapped ADSP program RAM, resets the ADSP PC to the image
entry, and records the read-back word and post-settle PC. This is an image
placement/runtime-integration proof, not yet a behavioral replacement for the
original ADSP program.

For a local, ROM-derived full-image experiment, capture the active ADSP RAM at
the established populated boundary (frame 136 is retained as an all-zero
negative control; the first captured populated image is frame 600):

```sh
tools/mame-adsp-program-dump /path/to/private/stunrun-roms \
  /tmp/stunrun-m3-adsp-original --frame 600
tools/mame-replacement-image \
  /tmp/stunrun-m3-adsp-original/image.json \
  --rompath /path/to/private/stunrun-roms \
  --output /tmp/stunrun-m3-adsp-run
```

The captured binary is local reference evidence and must not be committed.

## C port of the init slice (Agent 2, M3)

`adsp_init_image.h` / `adsp_init_image.c` re-emit the same four fixtures
(`nop`, `reset-loop`, `init-prefix`, `init-state`) in C99 with no
dependencies, plus the four observed DM landmarks (`DM($0955)=0x1242`,
`DM($0956)=0x124E`, `DM($0959)=0x7FFF`, `DM($095A)=0xFFFF` from
`analysis/ram-map.md`). Byte output is identical to
`tools/build-replacement-image` for all four fixtures (verified by direct
comparison, not by re-reading one side's output). The native target
compiles these same sources (`native/CMakeLists.txt`,
`adsp-init-slice-c`), so reproduction and native share one canonical
implementation. Public ROM-free checks: `tests/test_adsp_c_image.py` and
the compiled self-check `adsp_init_image_test.c`.

## C port of the control/IRQ contract and install boundary (Agent 2, M3)

`adsp_control_seq.h` / `adsp_control_seq.c` encode the 68010-visible ADSP
control-window contract from
`reference/experiments/stunrun/adsp-control-window.metadata.json`: the 11
operations totaling the 73 observed all-zero writes per 600-frame
title-path run (LED, `/BR`, `/HALT`, reset assert/release, bank, default
controls), the `0x818060` IRQ-clear pair from PCs `0x021426`/`0x02C234`,
and the zero-read IRQ-state window. A multiset tally consumes observed
`(address, data)` pairs and reports whether a run reproduced the
contract; intra-run write order was never recorded, so no sequence order
is claimed. The same module encodes the upload/install frame boundary
from `m3-adsp-upload-boundary.metadata.json` (empty through 407,
populating 408–410, complete at 411, install-ready at 412). Comparing a
replacement run against the canonical title checkpoint (M3 item 3) stays
Verifier-owned; the tally is the reusable checker that comparison
consumes. Native wiring: `adsp-control-slice-c` in `native/CMakeLists.txt`.
Public ROM-free checks: `tests/test_adsp_c_control.py` and the compiled
self-check `adsp_control_seq_test.c`.

## C port of the 68010 upload-stream framing (Agent 2, M3)

`adsp_upload_stream.h` / `adsp_upload_stream.c` encode the 68010-side
program-upload contract from
`reference/experiments/stunrun/adsp-program-upload.metadata.json`: the
ROM-resident source pointer (`0x0001702E` via `0x02C204`/`0x02D2E0`),
the transfer loop (`0x02D304–0x02D364`) with its single writer PC
`0x02D35C`, the 17-record table totaling 2728 words (= 5456 halfword
taps), the explicit destination gap (word indices 2203–4136), and the
`0xFF` terminator. The destination rule `address = 0x800000 + 4*index`
reproduces the observed `0x800000–0x8048D6` coverage exactly. Record
payload semantics remain unresolved and are not modeled. Native wiring:
`adsp-upload-slice-c` in `native/CMakeLists.txt`. Public ROM-free
checks: `tests/test_adsp_c_upload.py` and the compiled self-check
`adsp_upload_stream_test.c`.
