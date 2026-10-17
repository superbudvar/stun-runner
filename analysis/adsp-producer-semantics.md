# ADSP producer: object samples, 2-D transform, and SOM contour records

> **Cross-check, 2026-10-17:** this note deliberately isolates ADSP-side evidence.
> The fresh md12-fixed end-to-end runs in `analysis/adsp-output-semantics.md`
> now establish pixel/colour use for the ordinary emitted format across many
> records. They do not retroactively join this older-binary frame-551 capture
> to those different runs. With fresh NVRAM, the parent observed ADSP output
> beginning near frame 827; the no-transaction result below is bounded to 600
> frames, not an absence of the producer.

## Question and result

What does the active ADSP-2100 produce, from which inputs, and what can the
record values mean without borrowing the 68010 road path or the GSP consumer's
interpretation?

**Answer (KNOWN mechanism; LIKELY semantics):** on the captured frame-551 path,
the ADSP consumes three-word tuples from its data-space work area. Each tuple
contains two signed 16-bit samples and an address used to fetch a signed
coefficient from the banked `mainpcb:user1` object ROM through `SIMCLK/SIMBUF`.
At `$04B2`, it uses a fractional multiply to turn those samples into centered
two-dimensional values; a second pass preserves the first component and scales
the second by 5/8. It clips the resulting six-word, three-pair ring to 0..511,
then `$0514-$05D4` selects and walks two chains around that ring and emits a
control/header plus interpolated, row-counted spans through `SOMLATCH`.

The producer therefore does more than ferry opaque bytes: the source tuple,
coefficient fetch, transform, clamp, ring walk, interpolation, and serialized
span output are directly observable. The transformed values are **consistent
with projected 2-D contour coordinates** and the spans with a filled outline,
but the coordinate basis, screen units, which rendered object they describe,
and whether the shape is specifically a road cross-section are not established
by ADSP evidence alone. This is not the MC68010's 384-word road-table path.

## Evidence and provenance

### New bounded runtime capture

The current `tools/capture-adsp-som-transaction` invocation using the normal
fresh-per-run NVRAM directory completed but observed no SOMCLK transaction. A
paired run with the recorded local NVRAM copied into a per-run scratch directory
did observe it. This is a launch-state dependency, not evidence that the ADSP
producer is absent. The source NVRAM files were not modified.

The successful run used:

| Field | Value |
|---|---|
| ROM set | `stunrun`, validated with `tools/check-roms`; `roms/stunrun.manifest.json` SHA-256 `b3cf2b613752c494cb441f85090dd056f53b9006bdc4564eb53396d3c4ae729d` |
| MAME | `/mnt/build/mame-oracle/mame` (`stock-behaviour`), SHA-256 `75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06` |
| Initial NVRAM copies | `mainpcb_200e` SHA-256 `520cf5315b9c0c6727fa0de26c967180f1f207bbfa60feda7852f189bbf93077`; `mainpcb_210e` SHA-256 `0fe1c786431209f4c8e7762e469678d5856a5018cb7a83e440a17df3176d8f50` |
| Start and input | power-on, `input=none`; per-run cfg/state dirs and a copied, writable per-run NVRAM dir |
| Bound / taps | frames 1–600, post-GINT 5 frames, max 50,000 events, 180 s process timeout; existing `mame/lua/adsp-som-transaction.lua` plus same-run `mame/lua/adsp-program-dump.lua` at frame 551 |
| Local raw output | `/tmp/opencode/adsp-producer-semantics-same-run/out/mame.log`; SHA-256 `8ee4c0921aa9dca228682af5d587eaeec15e17b04b5dd6911986d906550254fa` |
| Same-run program image | `/tmp/opencode/adsp-producer-semantics-same-run/out/adsp2100.bin`; 8,192 program addresses, SHA-256 `821f3478ef4de4deb67c2b492fc726aaa73351d240b037e46b9f0d23014cecc4` |

The frame-551 interval is sequence `986534..990958`, exactly 4,009 ADSP
events, not truncated, nine `$0564` DM snapshots, and 146 SOMLATCH words. The
word stream SHA-256 is
`dffb76c5cc2869a9ffd35594e42c6a08ac9aabe9e289229cab9146082647d259`, byte-for-byte
equal to the already committed frame-551 fixture's stream. The existing fixture
metadata records the matching MAME/ROM provenance and the earlier capture
sequence; this new log is local scratch evidence and was not added under
`reference/`.

The combined same-run dump avoids pairing a live trace with a program image
from another MAME invocation. `tools/disasm-adsp` decoded this image using
MAME's ADSP disassembler; it is a linear sweep, so the excerpts below are
validated against the runtime PCs/events and not treated as control flow merely
because they disassemble. The full 0x2000-address image is not committed.

### Independent transaction form

A second bounded run with the same stock MAME, input, and initial NVRAM, but
searching from frame 552, caught the next SOMCLK/GINT interval at frame 554. It
had no `$0514` record snapshots and no 146-word block. Its exact ADSP event
sequence is:

```text
00BB  W DM($2003) = 0000     SOMCLK
00BC  R DM($001E) = 16D6
00BE  W DM($2002) = 16D7     SOMLATCH
00BF  R DM($0109) = 1599
00C0  W DM($2002) = 1599     SOMLATCH
00C1  W DM($2006) = FFFF     GINT
```

These are two SOM words and a different GINT value, not another contour record.
The code at `$00B4-$00C1` confirms this is a separate transaction form. It is a
bounded observation, not proof of the two words' higher-level meaning. Local
log: `/tmp/opencode/adsp-producer-semantics-nexttx/out/mame.log`, SHA-256
`6061cb29a9f8dae297fcd67979955b3cbc990f3766e7d0a96b304bdfe28fad7d`.

## Verbatim disassembly: the producer path

The following is MAME's disassembler output for the **same-run image** above.
Address notation and instructions are verbatim; comments below are
interpretation. The board's special data map is confirmed by the pinned MAME
driver (`src/mame/atari/harddriv_m.cpp:842-897`): `$2000=SIMBUF`, `$2001=SIMCLK`,
`$2002=SOMLATCH`, `$2003=SOMCLK`, `$2006=GINT`, `$2007=MP`; `MP=1` selects
object-ROM base `0x10000` words and each SIMBUF read advances that address.

### `$04B2-$04CA`: source tuple to centered pair

```text
04B2  CNTR = DM($0039)
04B3  I0 = DM($0928)
04B4  I1 = $0040
04B5  L0 = $0000
04B6  L1 = $0000
04B7  M1 = $0001
04B8  AX0 = $00FF
04B9  AX1 = $00FF
04BA  AY1 = $0200
04BB  AR = $0001
04BC  DM($2007) = AR
04BD  MX0 = DM(I0,M1)
04BE  DO $04CA UNTIL CE
04BF  SR1 = DM(I0,M1)
04C0  AR = DM(I0,M1)
04C1  AF = AR - AY1
04C2  DM($2001) = AR
04C3  IF LE JUMP $04E0
04C4  AR = -SR1, AY0 = AX0
04C5  MY0 = DM($2000)
04C6  MR = MX0 * MY0 (RND), MX1 = AR
04C7  AR = MR1 + AY0, AY0 = AX1
04C8  MR = MX1 * MY0 (RND), DM(I1,M1) = AR
04C9  AR = MR1 + AY0, MX0 = DM(I0,M1)
04CA  DM(I1,M1) = AR
```

Observed input to this loop is three repetitions of `(signed x, signed y,
SIMBUF address)` per record. The first x is preloaded at `$04BD`; the two
following source words and pointer are read at `$04BF/$04C0`, and `$04C9`
prefetches the next x while storing the prior y result. The runtime evidence
shows `I0=DM($0928)=0x0040`, `DM($0039)=3`, so the loop performs three tuples and
overwrites the source area with six output words.

The tuple's third word is demonstrably an object-ROM coefficient address, not
a coordinate: it is copied to `SIMCLK`, followed by a `SIMBUF` read, and that
returned word becomes `MY0` in both multiplies. With `/MP=1`, the observed
addresses `0x208C` and `0x23AC` select user1 word offsets `0x1208C` and
`0x123AC`; MAME returns `0x07DE` and `0x072D`, respectively. Across the nine
records, the coefficient sequence is `07DE, 072D, 07DE` for records 1–8 and
`07DE, 07DE, 072D` for record 9. The coefficient values are directly observed;
calling the addresses coefficients is strongly supported by their multiply
operand use.

The tested path's numeric rule is explicit. MAME's `mac_op_mr` implements
`RND` with signed X/Y operands, a one-bit fractional left shift when
`MSTAT_INTEGER` is clear, and a `0x8000` rounding add before MR1 is read
(`src/devices/cpu/adsp2100/2100ops.hxx:1380-1407`; device reset clears MSTAT at
`adsp2100.cpp:643`). In the captured path, for signed source values `x,y` and
coefficient `c`, the measured outputs are:

```text
x1 = 0x00FF + s16((2 * s16(x) * s16(c) + 0x8000) >> 16)
y1 = 0x00FF + s16((2 * s16(-y) * s16(c) + 0x8000) >> 16)
```

with 16-bit wrap at the ADSP register/write boundaries. The equation reproduces
the observed first-pass pair for all 27 tuples, including `(-179,28)` with
`c=0x07DE` → `(0x00F4,0x00FD)` and `(0,166)` with `c=0x072D` →
`(0x00FF,0x00F6)`. The coefficients act as Q1.15 scale factors in this
arithmetic. This numeric result is stronger than a visual guess; it still does
not identify the source coordinate frame.

### `$04CD-$04DC`: second pass

```text
04CB  AR = DM($1FE4)
04CC  AR = AR
04CD  IF EQ JUMP $04DD
04CE  CNTR = DM($0039)
04CF  I0 = $0040
04D0  I1 = $0040
04D1  MX0 = AR
04D2  MX1 = DM($1FE5)
04D3  MY0 = DM(I1,M1)
04D4  SE = $FFF6
04D5  DO $04DC UNTIL CE
04D6  MR = MX0 * MY0 (SS), MY1 = DM(I1,M1)
04D7  SR = LSHIFT MR0 (LO)
04D8  SR = SR OR LSHIFT MR1 (HI)
04D9  MR = MX1 * MY1 (SS), DM(I0,M1) = SR0
04DA  SR = LSHIFT MR0 (LO)
04DB  SR = SR OR LSHIFT MR1 (HI), MY0 = DM(I1,M1)
04DC  DM(I0,M1) = SR0
```

For all nine records, `DM($1FE4)=0x0200`, `DM($1FE5)=0x0140`, and `SE=-10`.
Each output pair is `(x,y)`: the x multiply is identity after the 10-bit shift;
the y multiply is `floor(5*y/8)`. The literal MAME event streams match that
rule for **all 54 output words** (27 x values and 27 y values). This is a
measured fixed-point transform, not a name assigned from the resulting image.
The `$04CD` zero guard and the `$04C3` alternate scale branch were not taken in
this transaction.

### `$04EB-$050D`: clipping and ring preparation

```text
04EB  CALL $064A
04EC  M0 = $0001
04ED  AY0 = DM($001E)
04EE  AR = $1FA4
04EF  AR = AR - AY0
04F0  AY0 = DM($002D)
04F1  AR = AR - AY0
04F2  IF GT JUMP $04F6
04F3  DM($002E) = M0
04F4  CALL $0631
04F5  RTS
...
04FE  I0 = $0040
04FF  I1 = $0040
0500  AR = DM($0039)
0501  SR = ASHIFT AR BY 1 (LO)
0502  CNTR = SR0
0503  M0 = $0001
0504  M1 = $0001
0505  AY0 = $0000
0506  AY1 = $01FF
0507  AX0 = DM(I0,M0)
0508  DO $050D UNTIL CE
0509  AR = AX0
050A  IF LT AR = AY0
050B  AF = AY1 - AR, AX0 = DM(I0,M0)
050C  IF LT AR = AY1
050D  DM(I1,M1) = AR
```

`$050A/$050C` implement a saturating clamp to `[0,0x01FF]`. They are no-ops
for all 54 final ring words in the captured transaction; thus the clamp's range
is statically clear, but a runtime saturation is **not** observed here.
`DM($0038)=0` on all nine records selects this `$04EB` path. The sibling at
`$0663` is selected when `$0038 != 0`; its disassembly directly emits pairs
from `DM($0040..)` to SOMLATCH and then two `$FFFF` words, rather than running
the captured `$04B2 -> $04EB` transformation. That alternative mode is visible
in code but was not executed by this capture.

### `$0514-$05D4`: ring walk and SOM output

The output code reads `DM($0036)` as the control; reads the two selected chain
heads at `$0565/$0567`; takes `start_y` from `$056B`; calls the two interpolation
helpers at `$05DD/$0607`; and emits each span as four 16-bit halves plus a run
length. The exact output instructions are:

```text
0514  AR = DM($0036)
0515  DM($2002) = AR
...
0564  AR = DM(I1,M1)
0565  DM($2002) = AR
0566  AR = DM(I2,M2)
0567  DM($2002) = AR
0568  I0 = I2
0569  AX1 = DM(I0,M0)
056A  AY1 = DM(I0,M3)
056B  DM($2002) = AX1
...
0573  AR = AY1 - AX1
0574  IF LE JUMP $05D1
0575  CALL $05DD
0576  AR = AY0 - AX1
0577  IF LE JUMP $05D1
0578  CALL $0607
...
057D  AF = AY1 - AX1
057E  AR = AF + 1
057F  AX0 = DM($003E)
0580  DM($2002) = AX0
0581  AX0 = DM($003F)
0582  DM($2002) = AX0
0583  AX0 = DM($003C)
0584  DM($2002) = AX0
0585  AX0 = DM($003D)
0586  DM($2002) = AX0
0588  DM($2002) = AR
...
05D1  AR = $FFFF
05D2  DM($2002) = AR
05D4  DM($2002) = AR
```

The walk uses a six-word circular buffer (`L0=L1=L2=L4=6`) and stride-2
forward/backward chains; the selected I1/I2 heads for this capture are 0x40,
0x42, or 0x44. It merges the two chains until their indices meet. The selected
run length is `min(AY0,AY1)-AX1+1`, checked against all 18 captured spans. The
16-word records are four header words, two five-word spans in this capture,
then two `$FFFF` terminators. The frame-551 stream contains two zero prologue
words followed by nine such records.

The helper PM table is a reciprocal table: `PM[$1036+n]` contains
`floor(0x8000/n)` in bits 23:8 for n=1..64. For example, record 1 uses n=7 and
n=5. Captured high:low side values are `DM($003F):DM($003E) = FFFF:6DB8` and
`DM($003D):DM($003C) = FFFD:CCDA`; interpreted as signed 16.16 these are about
`-4/7` and `-11/5`. The exact arithmetic is directly observed: the first
helper's input difference is `0x00FB-0x00FF=-4`, the second's is
`0x00F4-0x00FF=-11`, and the reciprocals are `0x1249` and `0x1999`. Thus the
two 32-bit words behave as per-edge increments normalized by each side's own
extent. “Slope per row” is a LIKELY use, not a proven GSP field name.

**ROM table structure independently checked (2026-10-17).** The two interleaved
`mainpcb:user1` lanes loaded at region offset `$020000` form 32,768 big-endian
words. `tools/analyze-adsp-projection-table roms --json` validates lane lengths
and manifest SHA-1s, then checks values: indices 1–512 are exactly nearest
integer `2^15/n` (512/512); indices 513–32766 are nearest integer `2^24/n`
(32,249/32,254), with five preserved one-unit exceptions. Indices zero and
`$7FFF` are zero. This confirms a piecewise reciprocal table, including the
`$07DE` and `$072D` values actually used as multiply operands. It still does
**not** identify the source coordinate basis or the ROM assets represented by
the three input sample pairs. [ROM: tool and manifest; no external claim.]

## Runtime transform results across all nine records

The 27 input tuples below were read by `$04B2`; ROM addresses are SIM address
offsets before adding the `/MP=1` bank base. Input values are signed 16-bit
`(x,y)`. The final six words are the ring contents after pass 2 and the clamp,
in emission order `(x0,y0,x1,y1,x2,y2)`.

| Record | Three observed `(x,y; SIM offset → coefficient)` tuples | Final three `(x,y)` pairs |
|---:|---|---|
| 1 | `(-179,28; 208C→07DE)`, `(0,166; 23AC→072D)`, `(-62,-29; 208C→07DE)` | `(F4,9E) (FF,99) (FB,A0)` |
| 2 | `(62,-29; 208C→07DE)`, `(0,166; 23AC→072D)`, `(179,28; 208C→07DE)` | `(103,A0) (FF,99) (10A,9E)` |
| 3 | `(-62,-29; 208C→07DE)`, `(0,166; 23AC→072D)`, `(62,-29; 208C→07DE)` | `(FB,A0) (FF,99) (103,A0)` |
| 4 | `(-206,191; 208C→07DE)`, `(0,166; 23AC→072D)`, `(-179,28; 208C→07DE)` | `(F2,97) (FF,99) (F4,9E)` |
| 5 | `(179,28; 208C→07DE)`, `(0,166; 23AC→072D)`, `(206,191; 208C→07DE)` | `(10A,9E) (FF,99) (10C,97)` |
| 6 | `(0,382; 208C→07DE)`, `(0,166; 23AC→072D)`, `(-147,310; 208C→07DE)` | `(FF,91) (FF,99) (F6,93)` |
| 7 | `(206,191; 208C→07DE)`, `(0,166; 23AC→072D)`, `(147,310; 208C→07DE)` | `(10C,97) (FF,99) (108,93)` |
| 8 | `(-147,310; 208C→07DE)`, `(0,166; 23AC→072D)`, `(-206,191; 208C→07DE)` | `(F6,93) (FF,99) (F2,97)` |
| 9 | `(0,382; 208C→07DE)`, `(147,310; 208C→07DE)`, `(0,166; 23AC→072D)` | `(FF,91) (108,93) (FF,99)` |

The coordinates occupy a narrow region around x=`0x00FF` and y=`0x0099` after
the unequal scaling. The tuples, multiplication, 5/8 pass, and output ring
identity were independently reduced from the same-run raw event order: the
`$04B2` multiplier matches **27/27 input pairs**, the `$04D9/$04DC` second pass
matches **54/54 output components**, and all 146 SOM words match the committed
fixture. This supports a three-point 2-D contour/profile interpretation. It
does not prove a coordinate origin in hardware pixels, identify a particular
game object, or make these points 3-D road vertices.

## Control word and mode branches

All nine observed records latch control `0x0030`. The instruction sequence
shows the control is constructed before the emitter: `$01A0` starts from the
low six bits of the current `SI` descriptor; with `DM($0038)=0`, `$01C7` calls
`$0214`, which shifts that selector and ORs it with a computed multiply result;
`$01CE/$01D0` then ORs `DM($002F)` (zero in these records). This explains the
mechanism that produces `0x0030`, but **does not establish any color meaning**.
The sibling `DM($0038)!=0` path at `$01C9-$01CC` sets bit `0x1000` before
emitting the alternate `$0663` form. That is evidence for a mode distinction;
whether either field selects color is for the GSP consumer evidence to decide.

Three alternatives remain unexercised here:

1. `$04C3`'s `IF LE` branch to `$04E0-$04EA` uses `SS` and `LSHIFT ... BY 9`
   instead of the captured `$04C4-$04CA` RND path. All observed coefficient
   addresses (`0x208C`, `0x23AC`) exceed the `0x0200` branch threshold.
2. `$04CD` skips pass 2 if `DM($1FE4)==0`; it is `0x0200` in all nine records.
3. Nonzero `DM($0038)` dispatches through `$0663/$068B` instead of the captured
   transformed/clamped path. The frame-551 capture has `DM($0038)=0` throughout.

These are code paths, not measured alternate geometry results. No `DM($0038)!=0`
runtime fixture was found or generated in this investigation.

## Geometry interpretation and ownership boundary

### Observed facts

- The source input is three pairs of signed 16-bit words with a third word used
  as a banked object-ROM read address; the returned value is the multiply
  coefficient. This is not a ROM-resident raw vertex list copied unchanged to
  SOM.
- The transformed six-word ring contains three `(x,y)` pairs, and a six-word
  circular-chain walk chooses the endpoints of two chains.
- The ADSP computes two normalized signed 16.16 increments and a run length per
  span, then emits those values through SOMLATCH.
- The pass-2/clamp result is not the 68010 road table: the latter is copied by
  MC68010 code from main-CPU ROM through `0xFF9584` and drained at `0x02248E`.
  These are distinct producers even though both ultimately have GSP-side data
  paths; see `analysis/track-storage.md` and `analysis/road-upload-path.md`.

### Inference with confidence

**LIKELY:** this is a compact 2-D polygon/contour preparation path. Three input
pairs become three transformed pairs; the ring walker orders two edge chains;
the emitter computes per-edge increments and emits vertical intervals with
`min(endpoint)-start+1` lengths. That is consistent with a filled contour or
cross-section representation, rather than a list of independent 3-D XYZ
vertices.

The source ring's three pairs and the walk's two-chain merge make a
three-vertex contour a stronger hypothesis than a generic six-scalar vector.
The usual output centers near `(255,159)`, and the `0x01FF` clamp is a 9-bit
bound, so a bounded projected-coordinate domain is plausible. Neither the
coordinate units nor a 512-pixel viewport is established; do not label these
values “screen pixels” or “road vertices” yet.

**UNKNOWN:** whether the coordinates correspond to an object silhouette, a road
cross-section, another polygon class, or an intermediate coordinate frame;
the physical units/origin; what `0x0030` encodes; which ROM table bounds the
three-word input tuples; and whether the GSP receives this exact frame-551 block
as a particular command. The separate GSP/FIFO trace is not used to fill those
gaps.

## Next experiment

The sharpest next discriminator is to reach a bounded case with
`DM($0038)!=0`, then capture the same three-word input tuples, `/MP`+`SIMCLK`+
`SIMBUF` values, `$0663` SOM writes, and `$0514`-class output in one run. Compare
that branch's geometry-word count and ring contents with the normal
`$04B2/$04EB` mode; do not assume the `0x1000` control bit is color. A second
useful test is a coefficient address at or below `$0200` to exercise the
`$04E0-$04EA` scaling branch. The transaction-capture tool currently fixes
`input=none` and its default launch uses fresh NVRAM; any reusable extension
must preserve explicit NVRAM provenance and avoid silently turning a no-transaction
run into a semantic negative result.
