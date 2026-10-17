# What the ADSP produces, and how the game uses it

## Answer

**The observed rendering output is a polygon scan-conversion program, not raw
3D vertices and not a framebuffer.** It contains colour/style controls,
starting left/right horizontal endpoints and Y, then signed 16.16 increments
for the two edges and inclusive vertical run lengths. It also contains a
separate ordered list of offsets selecting which polygon records to draw.
The 68010 ferries this through the GSP host port; the GSP walks the order list,
steps the edges, and issues one-row horizontal fills into the board's masked
pixel-expansion aperture. The expander writes palette indices into VRAM.
[ROM: new tools/captures below; expander source S7/S8 below.]

This is demonstrated by **multiple fresh outputs and two time windows**, not
by naming values in the historical 146-word fixture. Upstream transforms and
alternate raster-worker limits are distinguished from the proven contract.

## Where those commands come from

The independently re-observed historical producer path (`stock-behaviour`,
not mixed into the new binary's numerical comparisons) reads tuples of two
signed samples plus a coefficient-table address. ADSP `$04B2` fetches a scale
through object-ROM `SIMBUF`, multiplies both samples, negates the second axis,
and adds `$00FF` to center each coordinate. A following pass uses
`DM($1FE4)=$0200` and `DM($1FE5)=$0140`: X is preserved and Y is scaled by
5/8. The code bounds the resulting components to 0..511 before the two-chain
ring walk and slope generation. All 27 measured pairs match the first-pass
calculation and all 54 output components match the second pass. These are
numeric facts; the earlier coordinate basis and named object identity remain
unknown. Full disassembly and evidence limits are in
`analysis/adsp-producer-semantics.md`. [ROM: that note's same-run evidence and
the existing frame-551 transaction fixture.]

## Evidence and reproducibility

Bundle: `reference/experiments/stunrun/adsp-output-use-20261017/metadata.json`.
All new runs use power-on, no input schedule, fresh per-run cfg/NVRAM/state,
and **md12-fixed**, executable SHA-256
`55956f2b26c0d5caeb86f346e6247ded04428eae07465d35470dea77d6bf4045`.
Full ROM, machine-map and harness hashes are in the bundle. No comparison
crosses MAME binaries. Raw streams are retained in the bundle's ignored `raw/`;
provenance and numerical analysis are distributable, without ROM or save states.

```sh
tools/capture-adsp-output-use roms OUT --start-frame 820 --frames 840 \
  --max-events 600000 --json
tools/analyze-adsp-output-use OUT --json
tools/capture-adsp-output-use roms LATER --start-frame 1100 --frames 1110 \
  --max-events 600000 --json
tools/analyze-adsp-output-use LATER --json
```

The early window contains 117,220 events. A separately isolated repeat is
byte-identical (`fdff30899dc10634…`). The later window contains 104,398 events.

## Packet and record meanings — KNOWN for the observed path

The first completed output has 317 polygon records. Its index list contains
exactly the same 317 record starts, but differs from emission order in 308
positions. Thus it is an actual reordered draw list. ADSP `$080B–$0833` walks
linked PM entries and emits their stored offsets at `$0828`, ending with
`$FFFF` at `$082B`. The sorting key's physical meaning (e.g. depth) is not
independently established here. [ROM: early event stream; `tools/disasm-adsp`
at `$080B`, count 41, on the uploaded program.]

The first two SOM words are initially reserved; a subsequent short ADSP
transaction rewrites them with total/count and index-list offset. For the
first packet the final words are `$16D7` (5847) and `$1599` (5529). The GSP base
points at SOM word 1: index `1` selects SOM word 2, the first polygon control.
It does not blindly consume the initial zero placeholders. [ROM: ADSP
`$0035/$0039`, `$00BE/$00C0`; GSP `$FFF44CA0`, `$FFF45000/$FFF45090`.]

| Words | Meaning |
|---|---|
| 0 | colour/style control |
| 1 | starting left X |
| 2 | starting **right X, inclusive**, not width |
| 3 | starting Y |
| repeated 5-word tuples | right increment low/high, left increment low/high, inclusive run length |
| `$FFFF,$FFFF` | 32-bit record terminator |

Complete record sizes observed: 6, 11, 16, 21 and 26 words, zero through four
tuples. There is no fixed eight-word record. The GSP reconstructs each F1
increment from the ADSP's low-halfword-first stream. [ROM: record-shape counts
and source/consumer address matches in the bundle.]

## Exact stepping contract — checked against live writes

The primary GSP worker's equivalent accumulator contract is:

```text
left  = startLeft * 65536 + 32768
right = (startRight + 1) * 65536 + 32768
y = startY
emit [floor(left/65536), floor(right/65536)) at y
for each (dRight, dLeft, run):
    repeat run - 1 times:
        right += signed32(dRight)
        left  += signed32(dLeft)
        y += 1
        emit the new horizontal interval
```

The run is inclusive: its initial endpoint row is already present. A tuple
advances by `run-1` rows, not `run` additional rows. The increments express
horizontal pixels per vertical row. The half-unit bias rounds the fixed-point
endpoint carry chain. This describes the observed primary path, not every
overflow/clipping branch. [ROM: producer-word predictions vs GSP register/write
observations in `tools/analyze-adsp-output-use`.]

For each checked primary row:

```text
B0 = (y, leftX)
A6 = (y+1, rightExclusiveX)
B7 = (1, rightExclusiveX-leftX)   # FILL height/width
B2 = destination XY
```

The first actual write in each fill has bit address
`align16(B4 + y*1024 + leftX*2)`, in `$02000000–$020FFFFF`.
Its word is the two-bits-per-pixel coverage mask ANDed with B9's low-halfword
fill pattern. Actual write PC is `$FFF454E0` (`FILL XY`), not merely a fetch.

MAME `harddriv_v.cpp:270–272` reads zero from this aperture; `:304–319` expands
writes through `m_mask_table` and `m_gsp_control_lo[0]` into physical VRAM.
Its multisync mask table at `:45–60` selects one byte per enabled pixel.
Thus these are coverage writes whose colour comes from the control latch, not
raw final-colour framebuffer words. [S7/S8]

Captured latch writes at `$F4000000` demonstrate colour/style meanings:
`$0030` gives `$3030`, `$0033` gives `$3333`, `$0133` gives `$2323` through the
colour table. `$0814` gives `$1414` with a different B9 fill pattern.
[ROM: `control_to_pen_rows`, early analysis; colour writer `$FFF45330`.]

## Actual coverage

| Check | Frames 820–840 | Frames 1100–1110 |
|---|---:|---:|
| SOM reads matching latest captured bank/address producer | 17,547 | 12,760 |
| unchanged CPU ferry words with captured producer | 17,541 | 12,754 |
| GSP reads matching ferried producer at the same physical word | 17,374 | 9,592 |
| complete emitted records observed | 1,251 | 870 |
| primary rows: endpoint, address and mask all match predictions | **10,851** | **7,869** |

The later window starts mid-flight: 3,376 SOM reads and 7,326 primary raster
groups lack a captured producer and are **not** credited. Another 8,967 write
groups are alternate workers, explicitly outside the primary predictor.
No mismatches occurred in the credited **18,720 rows**. These are fill groups,
not necessarily unique or visible final-screen pixels or every row of a record.

Sensitivity controls in `tests/test_public_tools.py`: asymmetric endpoints and
a fractional step discriminate the rules. Mutants with wrong right correction,
missing rounding bias, wrong run count or doubled pitch fail; changing a trace
fails its recorded hash. This is not merely checking self-reported success.

## Limits and correction

* This establishes the observed rendering output/usage, not an inventory of
  every ADSP service/game state or every upstream object/transform.
* Alternate workers execute later; their precise clipping/stipple/scaling
  policies are not credited to the primary predictor.
* The address/version/value join does not propagate arbitrary GSP copies or
  invalidate provenance on every non-host GSP store. It is not a universal
  taint engine. Direct operands plus actual raster writes establish this path.
* Full-screen native/MAME parity, final palette-bank RGB and named gameplay
  objects are not proven by these observations.
* The old word-2-as-DX interpretation was **our error**: `SUBXY` subtracts X
  as well as Y. It is corrected in `reproduction/gsp/payload_codec.c`; a live
  captured prefix regression verifies `right-left+1` against the observed fill
  width (issue `stun-runner-3vb`, now closed). The prior green tests alone were
  not an independent oracle.
