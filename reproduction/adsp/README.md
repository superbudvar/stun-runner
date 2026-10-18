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

## Clipping the six-word ring (`som_clamp.c`)

`som_clamp.c` transcribes the six-word loop at `$04FE-$050D`, which clamps
signed DM words to `[0, 0x01FF]`. The frame-551 ring is entirely in range, so
the oracle-backed check proves the observed no-op and feeds the clamped result
into the existing SOM walk. A separate synthetic vector exercises negative,
in-range, and over-limit values as source-level coverage of the disassembled
branches; those saturation values were not observed in the captured run. The
earlier `$04EB-$04FD` caller/early-return checks and the alternate `$0663` mode
remain outside this slice.

## Walking the record schedule (`record_schedule.c`)

`record_schedule.c` transcribes `$0174-$01A9`, the routine that walks the
stride-2 pointer chain out of the object ROM and produces each record's SIMBUF
start. Its inputs are the sparse object-ROM words and one measured caller value,
`DM($0034) = $9989`; its outputs are the nine latched starts, the `DM($0036)`/
`DM($0035)`/`DM($0038)` writes, and the walk's exit. `$01A7` in the capture is
what the test compares against, so the schedule is derived rather than supplied.

Two things this slice deliberately does **not** do: it does not model `$01A8`'s
call to `$01E8` or the flags `$01A9` tests, and it refuses the fourteen
jump-table entries the capture never executed instead of transcribing them from
the disassembly. Both are recorded as `UNKNOWN` in findings §98, which also
carries the evidence that the walk exits through the `RTS` at `$0187` rather
than the `IF EQ` at `$019F`.

## Selecting the three source tuples (`som_source_select.c`)

The `$01E8/$01B0` data-side slice reads the measured MP=1 SIMBUF selector stream,
copies the first three-word source tuple and weight words from a supplied DM
table, then follows the two observed point selectors into `DM($0043..$0048)`.
The CTest checks the resulting nine-word `DM($0040..$0048)` window against the
later `$04B2` reads and composes it with `som_producer.c` to reproduce all nine
captured SOM records. Its DM table is produced by the bounded
`$0147-$0173` source-DM slice below.

Only the data writes are modeled: the E8 weighted accumulator result / MR2
caller branch is not yet modeled. The captured selector list has exactly two
point indices followed by `$FFFF`; other counts fail rather than guessing.

## Building the source-DM table (`source_producer.c`)

`source_producer.c` transcribes the observed `$0147-$0173` loops for the
frame-551 transaction. It consumes the same-run 58-word DM($2000) read stream,
the 30 auxiliary DM reads, and the full 171 same-run program-space read dwords
at PCs `$0157-$0159` and `$0169-$016B`. It emits 30 writes from `$015C` and 27
from `$0171`; the CTest compares all 57 words with the separate captured DM
write stream. `source_producer_fixture.c` keeps these operands and expectations
separate from the implementation. The observed `SE=-2` staging shift is
captured for this path; other incoming shift states are rejected.

`som_source_table.c` composes that routine into the bounded absolute table used
by the selector. `som_selector_fixture.c` contains only a sparse synthetic
`mainpcb:user1` word image populated at the captured selector and coefficient
addresses; it does not contain a commercial ROM image. Selector starts and the
control word remain explicit captured entry inputs, rather than inferred caller
semantics.

`adsp-som-source-select-slice-c` now builds the table with this producer,
selects source tuples with `$01E8/$01B0`, then composes the `$04B2` transform
and SOM walk. This reproduces the nine observed SOM records from the measured
DM/SIMBUF/PM entry operands. The transaction envelope below then composes the
derived records into the captured SOM bank/address/GINT stream. The measured
record-start schedule and control word remain entry inputs; the upstream
dispatch that selects them and E8 MR2/caller-loop flags are not modeled, and
alternate branches remain unsupported. The PM operands are same-run values,
not read from the later run-end program image; the retained capture is
`reference/experiments/stunrun/adsp-som-pm-reads-f551/run-1/result.json` and
its repeat is `run-2/result.json`.

## SOM bus transaction envelope (`som_transaction.c`)

`som_transaction.c` wraps the nine derived 16-word records with the two
observed zero prologue words, assigns each latch word its SOM address and
opposite-bank destination, and emits the completion value after the 146th
SOMLATCH write. The completion value is not a table constant: the SOM walk now
retains its terminal `AX0`, checked against the same-run register snapshots at
PC `$05D4`; the final record leaves `$0045`, which the caller writes to GINT at
PC `$004C`. The captured `$0046` command word has request bit `$4000` set, so
this bounded path asserts GINT. Other request states fail rather than pretending
to reproduce another caller branch.

The integrated source-selector CTest checks all 146 `(bank,address,value)`
writes in order against the captured transaction and verifies GINT follows the
last word. It uses selected bank 1, producer bank 0, and SOMCLK word 0 from the
capture; the address-wrap rule is separately covered at the 8K-word boundary.
This composes the observed transaction but does not emulate the host CPU's
subsequent IRQ acknowledge or general ADSP scheduling.

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
