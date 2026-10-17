# ADSP SOM records at the GSP raster consumer

> **Superseded correlation boundary, 2026-10-17:** The original investigation
> correctly noted that its frame-551 trace alone did not correlate to a GSP
> invocation. Fresh isolated same-run captures now do establish that join for
> multiple ordinary records and match primary raster writes. See
> `analysis/adsp-output-semantics.md` and the `adsp-output-use-20261017`
> evidence bundle. Do not repeat the old `UNKNOWN` as current state.

## Question and boundary

This investigation asks whether the captured ADSP SOM stream's `4 + 5N + 2`
layout matches the GSP consumer, what the consumer does to those fields, and what
that lets us say about fixed-point arithmetic and raster output. It is a
downstream-consumer analysis, not a producer/transport join. **No evidence yet
proves that the particular frame-551 SOM transaction below is the byte stream
read by this GSP invocation.** The MC68010 ferry and GSP source address/value
correlation are being pursued separately; all producer-to-consumer statements
here are conditional unless explicitly labelled otherwise.

## Exact local GSP code recovered from the uploaded image

The earlier analysis named PCs, but the readily available uploaded image was
outside the repository, not absent. I re-ran the committed disassembler against
the local frame-404 upload:

```sh
tools/disasm-gsp /tmp/opencode/6tf/upload-md12/gsp-vram-upload-last/gsp_vram.bin \
  --at 0x68A12 --count 135
tools/disasm-gsp /tmp/opencode/6tf/upload-md12/gsp-vram-upload-last/gsp_vram.bin \
  --at 0x68B12 --count 100
```

The first file's `result.json` identifies frame 404, 262,144 words, 81,899
nonzero words, dump SHA-256
`47e41d1b9d54055d965074ac2b6e9e0b844f024006e2555f6b8a17543d4a2715`, and
canonical MAME log SHA-256
`71e091391aa5a4bce0564ea283fac2fd44c9062eb34c9597ca8cf8613a2f3ed6`. This is
the `md12-fixed` upload capture, input `none`; MAME executable
`/mnt/build/mame-ref/mame` SHA-256 is
`55956f2b26c0d5caeb86f346e6247ded04428eae07465d35470dea77d6bf4045` (see
`AGENTS.md`'s binary table).
The dump hash and word count agree with the separately committed
`analysis/gsp-program-upload-boundary.md` frame-404 capture. The path under
`/tmp/opencode` is local and is not added to the repository.

The address conversion used here is the TMS34010 program PC folded to the
physical share: `share_byte = ((PC & 0x003fffff) >> 3)`. Thus, for example,
`PC 0xFFF45090` maps to disassembler byte offset `0x68A12`, while
`0xFFF45890` maps to `0x68B12`. Do not compare those share-byte offsets directly
to bit-addressed PCs.

### Verbatim primary worker excerpt (`0xFFF45000–0xFFF45630`)

The instruction words and MAME disassembly at share offsets `0x68A00–0x68AC6`
are:

```text
FFF45000  00940065  MOVE   *A3+,A5,0
FFF45010  00050025  ZEXT   A5,0
FFF45020  00e50085  CMPXY  A12,A5
FFF45030  00ce00cd  JRN    344D10h
FFF45040  00240085  SLL    4h,A5
FFF45050  00400025  ADD    A1,A5
FFF45060  000d003f  CALLR  345090h
FFF45080  00c000f7  JR     345000h
FFF45090  009400a4  MOVE   *A5+,A4,0
FFF450A0  001e0004  BTST   Fh,A4
FFF450B0  00cb00c9  JRNE   344D50h
FFF450C0  004f0089  MOVE   A12,B9
FFF450D0  001e0064  BTST   Ch,A4
FFF450E0  00cb00c7  JRNE   344D60h
FFF450F0  009400a8  MOVE   *A5+,A8,0
FFF45100  004f0000  MOVE   A8,B0
FFF45110  009400a6  MOVE   *A5+,A6,0
FFF45120  00100022  INC    A2
FFF45130  001f008d  BTST   3h,A13
FFF45140  00cb006b  JRNE   345800h
FFF45150  004c0080  MOVE   A4,A0
FFF45160  000b0080  ANDI   C00h,A0
FFF45190  00ca0009  JREQ   345230h
FFF451A0  000b0040  CMPI   C00h,A0
FFF451C0  00ca0063  JREQ   345800h
FFF451D0  000b0040  CMPI   400h,A0
FFF451F0  00ca000a  JREQ   3452A0h
FFF45200  000900f9  MOVI   1010101h,B9
FFF45230  004c0080  MOVE   A4,A0
FFF45240  000b0080  ANDI   FFh,A0
FFF45270  000b0040  CMPI   4h,A0
FFF45290  00cb0003  JRNE   3452D0h
FFF452A0  000900f9  MOVI   15151515h,B9
FFF452D0  002600c4  SLL    16h,A4
FFF452E0  002d00c4  SRL    12h,A4
FFF452F0  000b0024  ADDI   FFF71D20h,A4
FFF45320  00840080  MOVE   *A4,A0,0
FFF45330  00050080  MOVE   A0,@F4000000h,0
FFF45360  009400a8  MOVE   *A5+,A8,0
FFF45370  004f0008  MOVE   A8,B8
FFF45380  00240058  SLL    2h,B8
FFF45390  00690019  RL     B8,B9
FFF453A0  00260008  SLL    10h,A8
FFF453B0  00ef0006  MOVY   A8,A6
FFF453C0  004f0001  MOVE   A8,B1
FFF453D0  00ee0030  MOVY   B1,B0
FFF453E0  00e10066  ADDXY  A11,A6
FFF453F0  00180020  MOVK   1h,A0
FFF45400  002700e0  SLL    1Fh,A0
FFF45410  004c0004  MOVE   A0,A4
FFF45420  0018002a  MOVK   1h,A10
FFF45430  004c0012  MOVE   B0,B2
FFF45440  004e00c7  MOVE   A6,B7
FFF45450  00c00004  JR     3454A0h
FFF45460  00410020  ADD    A9,A0
FFF45470  00420030  ADDC   B1,B0
FFF45480  004000e4  ADD    A7,A4
FFF45490  00430006  ADDC   A8,A6
FFF454A0  004c0012  MOVE   B0,B2
FFF454B0  004e00c7  MOVE   A6,B7
FFF454C0  00e20057  SUBXY  B2,B7
FFF454D0  00cc0002  JRV    345500h
FFF454E0  000f00e0  FILL   XY
FFF454F0  00300099  RL     4h,B9
FFF45500  003d006a  DSJS   A10,345460h
FFF45510  009600a8  MOVE   *A5+,A8,1
FFF45520  00490088  CMP    A12,A8
FFF45530  00ca000f  JREQ   345630h
FFF45540  004d0007  MOVE   A8,A7
FFF45550  00260007  SLL    10h,A7
FFF45560  002a0008  SRA    10h,A8
FFF45570  000b0028  ADDI   10000h,A8
FFF455A0  009600a9  MOVE   *A5+,A9,1
FFF455B0  004f0021  MOVE   A9,B1
FFF455C0  00260009  SLL    10h,A9
FFF455D0  002a0011  SRA    10h,B1
FFF455E0  000b0031  ADDI   10000h,B1
FFF45610  009400aa  MOVE   *A5+,A10,0
FFF45620  00c000ed  JR     345500h
FFF45630  00090060  RETS
```

The disassembler prints byte offsets rather than full PCs; the PC annotations
above are `0xFFC00000 | (share_byte_offset << 3)` in this folded program window.
The multiword instruction offsets in the listing are shown at their first word.
This transcript is a linear decode at known routine entries and call targets;
the control-flow edges cited below make these bytes relevant code, not just
pattern matches.

### Verbatim second worker excerpt (`0xFFF45890–0xFFF45A50`)

This is the A13/control-selector alternate reached at `0xFFF45800`. It is
distinct from the ordinary primary FILL path above. Offsets `0x68B12–0x68B4E`:

```text
FFF45890  009400a8  MOVE   *A5+,A8,0
FFF458A0  004f0008  MOVE   A8,B8
FFF458B0  00240038  SLL    1h,B8
FFF458C0  00690019  RL     B8,B9
FFF458D0  00260008  SLL    10h,A8
FFF458E0  00ef0006  MOVY   A8,A6
FFF458F0  004f0001  MOVE   A8,B1
FFF45900  00ee0030  MOVY   B1,B0
FFF45910  00180020  MOVK   1h,A0
FFF45920  002700e0  SLL    1Fh,A0
FFF45930  004c0004  MOVE   A0,A4
FFF45940  0018002a  MOVK   1h,A10
FFF45950  004c0012  MOVE   B0,B2
FFF45960  004e00c7  MOVE   A6,B7
FFF45970  000900e4  MOVI   FFF45C60h,A4
FFF45980  004c0012  MOVE   B0,B2
FFF45990  004e00c7  MOVE   A6,B7
FFF459A0  004f0061  MOVE   A11,B1
FFF459B0  00e00037  ADDXY  B1,B7
FFF459C0  00e20057  SUBXY  B2,B7
FFF459D0  00cc0001  JRV    345A10h
FFF459E0  000f00e0  FILL   XY
FFF45A10  009600a7  MOVE   *A5+,A7,1
FFF45A20  00490087  CMP    A12,A7
FFF45A30  00ca001b  JREQ   345BF0h
FFF45A40  009600a9  MOVE   *A5+,A9,1
FFF45A50  009400aa  MOVE   *A5+,A10,0
FFF45A60  0014002a  DEC    A10
FFF45A70  00ca00f9  JREQ   345A10h
FFF45A80  0007006c  SETF   Ch,1,1
FFF45A90  005d0047  MPYS   A10,A7
FFF45AA0  005d0049  MPYS   A10,A9
FFF45AB0  00070060  SETF   20h,1,1
FFF45AC0  00ed0047  MOVX   A10,A7
FFF45AD0  00320007  RL     10h,A7
FFF45AE0  00ed0049  MOVX   A10,A9
FFF45AF0  00320009  RL     10h,A9
```

This confirms the six sites are not all stages of one same worker:
`0xFFF45000/90` are the indexed/primary path; `0xFFF45890` begins the
alternate worker's F0 setup; `0xFFF45A10/40/50` are its F1/F1/F0 tail after an
initial FILL. The alternate path multiplies each F1-derived A7/A9 value by the
F0 A10 count and converts to XY components before continuing. No semantic name
for that path's shape is assigned here.

## What the ordinary primary path computes

### F0 prefix and initial one-row FILL

The uploaded code sets F0=`0x10` and F1=`0x20` (`0xFFF40000/10`). In pinned
MAME, `SETF` selects 16-bit F0 and 32-bit F1 reads (`34010fld.hxx:663–670`,
`34010ops.hxx:1269–1283`, `tms34010.cpp:158–159`). The primary path therefore
consumes this F0 prefix:

```text
F0[0] control
F0[1] -> A8 -> B0 (X start)
F0[2] -> A6 (X end seed)
F0[3] -> A8 -> MOVY -> B0.Y and A6.Y
```

On the ordinary branch, `A11=0x00010001` is set by both observed callers at
`0xFFF44B30` and `0xFFF44BF0` (findings §63). The instructions produce:

```text
B0 = (Y=F0[3], X=F0[1])
A6 = (Y=F0[3], X=F0[2])
A6 = ADDXY(A11, A6) = (Y=F0[3]+1, X=F0[2]+1)
B2 = B0
B7 = A6
B7 = SUBXY(B2, B7)
FILL XY with B2=DADDR, B7=DYDX, B9=COLOR1
```

TMS34010 XY registers store X in the low 16 bits and Y in the high 16 bits
(`tms34010.h:331–348`, pinned MAME build on the little-endian host). MAME's
`ADD_XY`/`SUB_XY` independently add/subtract those signed 16-bit components
(`34010ops.hxx:116–146`). Thus the initial FILL dimensions are, modulo 16-bit
XY arithmetic:

```text
DADDR.X = F0[1]
DADDR.Y = F0[3]
DYDX.DX = F0[2] - F0[1] + 1
DYDX.DY = 1
```

That `+1` follows from `ADDXY A11,A6` and the subsequent `SUBXY`; it is not a
name inferred from the ADSP. MAME's FILL XY implementation treats DYDX halves
as signed 16-bit dimensions and returns without drawing if either is nonpositive
(`34010gfx.hxx`, `fill_xy`/FILL operation). Hardware XY coordinate clipping,
CONTROL/PSIZE configuration, palette interpretation, and actual written pixels
are separate from this operand calculation. It is accurate to call this an
initial one-row fill whose width is endpoint-minus-start-plus-one; calling the
fields “vertex X” or “polygon width” is not yet justified.

### F1 span arithmetic, repeat count, and color/control effects

After that initial fill, the primary worker reads F1 A8, compares the 32-bit
value to A12, then reads F1 A9 and F0 A10. A12 is `0xFFFFFFFF` in both callers.
If the first F1 equals that sentinel, it returns before reading the rest of a
span. Otherwise the exact transforms are:

```text
raw F1 r1 -> A7 = (r1 << 16) mod 2^32
             A8 = arithmetic_shift_right(r1,16) + 0x00010000
raw F1 r2 -> A9 = (r2 << 16) mod 2^32
             B1 = arithmetic_shift_right(r2,16) + 0x00010000
F0 A10 = repeat scalar
```

Then each repeated raster pass applies:

```text
A0 += A9;  B0 += B1 + carry(A0 addition)
A4 += A7;  A6 += A8 + carry(A4 addition)
B2 = B0
B7 = A6
B7 = SUBXY(B2,B7)
if V clear: FILL XY; if V set: branch over FILL
RL 4h,B9
DSJS A10,345460h
```

`A4` and `A0` are each seeded to `0x80000000`. At the bit level, the low 16
bits of each F1 are shifted into accumulator bits 31:16, so one such fraction
unit contributes `1/65536` of a carry unit; the seed is a half-unit bias.
The high 16 bits become the low half of the paired ADDC operand after
arithmetic shift and `+0x10000`. In the accumulator operation, that high word
drives the integer X-side increment, with fractional carry from A4/A0. This is
**consistent with a signed 16.16 edge-step representation**, and the literal
fractional granularity and half-unit bias are directly established by the
instructions. Do not silently collapse the full pair into a conventional
scalar Q16.16 value: `A8/B1` also have a high-half correction that participates
in the packed XY ADDC, and its cross-coordinate effect has not been checked
against live post-transfer registers. The exact per-iteration X/Y deltas in
screen units remain `UNKNOWN` pending that trace.

The `DSJS` operation decrements A10 and branches while nonzero (pinned
`34010ops.hxx:1517–1538`). Since the initial attempted pass happens before the
span fields are read, a positive span count N gives N total coordinate/FILL
attempts. `JRV` can suppress an individual FILL, so N is not proven to equal the
number of writes. This agrees structurally with the ADSP emitter's span scalar,
but the word-level values have not yet been joined in one execution.

For control, bit 15 skips/returns; bit 12 dispatches to `0xFFF48FC0`; otherwise
external A13 bit 3 and control bits 10–11 can divert to the alternate worker.
On the ordinary path, selector 0 with low byte 4 sets B9 to `0x15151515`,
selector `0x0400` sets that same value, selector `0x0800` sets B9 to
`0x01010101`, and selector `0x0C00` goes alternate. Selector 0 / low byte not
4 inherits B9=A12. Independently, the low byte is shifted into a table index
(`control << 4`) added to `0xFFF71D20`; that table value is written to
`@F4000000`. For captured ADSP control `0x0030`, the primary color branch
inherits A12 (thus B9 begins `0xFFFFFFFF`) and the table lookup offset is
`0x300`; the semantic meaning of that table entry/device write is unknown.

B9 is architectural COLOR1 for FILL XY (SPVU001A / repository
`docs/tms34010-reference.md` §3.10; pinned MAME FILL operand code). The exact
pixel value is not recoverable from the payload alone because PSIZE, CONTROL,
plane mask, destination mapping, and the generated VRAM writes are not joined
to this transaction. The per-pass `RL 4h,B9` is a demonstrated color-register
rotation; its screen effect is unknown.

## Instruction-level match to the ADSP `4 + 5N + 2` stream

The bounded ADSP frame-551 artifact is
`reference/experiments/stunrun/adsp-som-transaction-f551/`. Its metadata fixes
the scope: power-on, input none, frame 551, MAME `stock-behaviour` hash
`75b09ee4…`, 146 words from SOMCLK to GINT, 4,009 producer events,
`truncated=false`, and transaction SHA-256
`fcd8807f5eccca5f37d5829654169466fa4de6a9a5fc2db9ae3c5ffd8cba3c3f`.
Finding §66 attributes the SOMLATCH writes to the ADSP emitter: two prologue
zero words, then nine records. Each record is four header words, N groups of
five words, then two `0xFFFF` terminator words. All nine sampled records have
N=2, giving 4+10+2=16 words per record in this capture; this does **not** make
16 words a universal record size.

The GSP primary reader's independent instruction grammar is:

```text
F0 control, F0 word1, F0 word2, F0 word3       = 4 words
repeat:
    F1 A8 (2 words); if A8 == 0xFFFFFFFF: stop
    F1 A9 (2 words)
    F0 A10 (1 word)
```

That is exactly `4 + 5N + 2` words if the terminator is the F1 sentinel at the
first field of the next span: the ADSP's two `0xFFFF` words produce the
32-bit F1 value `0xFFFFFFFF` in the TMS34010's F1 reader. **Alignment caveat:**
the frame-551 SOM transaction has two zero prologue words before its first
record, so a verbatim consumer must start at SOM word offset 2 (or the ferry
must strip/skip those prologue words). No evidence in this lane establishes
which happens. The word order is compatible: ADSP emits each MR pair low word
then high word (`MR0`, `MR1`;
findings §66 and capture writer-PC attribution), while MAME's TMS34010
`TMS34010_RDMEM_DWORD` reads the word at address A into bits 15:0 and the word
at A+16 bits into 31:16 (`tms34010.cpp:396–400`). **This is a compelling exact
format match, not proof of the physical transfer:** no captured common timeline
currently demonstrates these particular 146 words being placed at the A5 stream
address consumed by the GSP.

### Independent multi-record arithmetic check on the captured SOM transaction

I independently parsed `result.json`'s 146 `som_words` (no code or expected
values were generated from the GSP interpretation): offset 0–1 are the zero
prologue; then each record is exactly 16 words in this one fixture, ends
`FFFF FFFF`, and contains two 5-word spans. Both header X fields are equal in
all nine records in this capture, so this vector set cannot distinguish the
producer's word-1 and word-2 semantic labels by values. It does allow checking
that the consumer sees the same raw word positions and that every F1 reads two
consecutive ADSP words low-word-first.

| ADSP record | F0 header `(control, word1, word2, word3)` | F1 pair 1 | F1 pair 2 | A10 |
|---:|---|---|---|---:|
| 0 | `0030 00FF 00FF 0099` | `FFFF6DB8` | `FFFDCCDA` | 6 |
| 0 | same | `FFFF6DB8` | `00038000` | 3 |
| 1 | `0030 00FF 00FF 0099` | `00023326` | `00009248` | 6 |
| 1 | same | `FFFC8000` | `00009248` | 3 |
| 2 | `0030 00FF 00FF 0099` | `00009248` | `FFFF6DB8` | 8 |
| 2 | same | `00000000` | `00000000` | 1 |
| 3 | `0030 00F2 00F2 0097` | `00068000` | `00004924` | 3 |
| 3 | same | `FFFDCCDA` | `00004924` | 6 |

For example, record 0 span 1's captured words are `6DB8 FFFF CCDA FFFD 0006`.
MAME's F1 word ordering yields `r1=0xFFFF6DB8`, `r2=0xFFFDCCDA`, A10=6.
The exact primary-worker transforms yield:

```text
A7 = 0x6DB80000
A8 = arithmetic(0xFFFF6DB8 >> 16) + 0x10000 = 0x0000FFFF
A9 = 0xCCDA0000
B1 = arithmetic(0xFFFDCCDA >> 16) + 0x10000 = 0x0000FFFD
```

Record 1 exercises positive, negative, and zero F1 values; record 2 includes a
zero/zero span. Thus the transformation is not being inferred from one chosen
example. This check verifies layout and arithmetic only. It does not show that
the GSP consumed those values, that these values produced a known pixel region,
or that the ADSP names (`start_x`, `delta_x`, span endpoints) equal GSP semantic
names. In particular, the observed equal header X words leave endpoint-vs-delta
intent unresolved.

## Confidence ledger

* **KNOWN — code / board-visible operation:** the primary ordinary path consumes
  four F0 words, establishes the initial FILL XY operands above, then reads F1,
  F1, F0 span groups until first-F1 sentinel `0xFFFFFFFF`; each F1 is 32-bit,
  composed low addressed word first. The span scalar controls N total
  coordinate/FILL attempts (including the initial attempt) when positive; JRV
  may skip a FILL.
* **KNOWN — ADSP capture:** one frame-551 SOM transaction contains the
  emitter-attributed `4 + 5N + 2` record structure; the nine captured records
  each use N=2. Raw words and provenance remain in the reference fixture.
* **KNOWN — representation compatibility:** the ADSP low/high MR word emission,
  two F1 reads, F0 scalar, and two-word all-ones terminator have exactly the
  widths/order the GSP consumer expects, conditional on a verbatim transfer.
* **LIKELY — same record family:** the structural agreement makes the GSP
  variable-span reader a plausible consumer of SOM-formatted records. This is
  not promoted to “the ADSP's frame-551 output is what the GSP renders” without
  the transport join.
* **LIKELY — F1 numeric form:** the split/shift/add sequence and 32-bit
  half-unit accumulators are consistent with signed 16.16-style X stepping,
  with `1/65536` fractional resolution and half-unit initialization. Exact
  cross-coordinate correction and screen-unit scale are not validated against
  runtime registers.
* **UNKNOWN:** whether any of the nine frame-551 SOM records reaches the GSP
  primary path; actual F0/F1/A10 values at the consumer; how all control bits
  and the table/device write affect state; exact X/Y delta per fill in screen
  coordinates; clipping/pixel size/palette mapping for this path; and the
  screenshot region caused by these records.
* **SPECULATIVE:** labels like polygon edge, scanline slope, sprite boundary,
  roadway, object, or color/lighting meaning. The stepping mechanism looks
  geometric but those semantic names are not demonstrated.

## Precise next evidence needed (handoff boundary)

The remaining decisive experiment is a same-run ordered join, not more decoding
of already-known static instructions: capture one SOMCLK-to-GINT ADSP block,
the 68010 reads from the actual output bank, each corresponding HSTDAT write,
and the GSP A5 source reads plus register snapshots at `0xFFF45090`,
`0xFFF45520`, `0xFFF455A0`, and `0xFFF45610`. Record raw words with their bit
addresses, A11/A12/A13, F0/F1, A0/A4/A6/B0/B2/B7/B9/A10 before and after each
operation, plus GSP VRAM writes and a paired screenshot. Keep the control-0x30
ordinary path separate from A13-bit-3 and selector alternate paths. The existing
frame-551 fixture has post-GINT SOM reads and HSTDAT writes, but explicitly
does **not** associate them one-to-one with those 146 words; it is a producer
oracle, not a transfer oracle. Until that capture is available, the exact
arithmetic above is demonstrated, while actual ADSP-to-screen semantics remain
partly unknown.
