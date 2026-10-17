# ADSP SOM producer and GSP data path

> **Superseded boundary, 2026-10-17:** the lack of a producer-to-consumer join
> below applied to the older frame-551 evidence. New isolated, same-run
> observations now correlate multiple ADSP outputs through SOM and the 68010
> to GSP operand reads and actual pixel-mask writes. See
> `analysis/adsp-output-semantics.md` and the `adsp-output-use-20261017` evidence
> bundle. The older binary/transaction is not mixed into that comparison.

## Scope and conclusion

This note separates three mechanisms that share the GSP but are not one proven
geometry pipeline:

1. **ADSP SOM producer:** a bounded ADSP transaction emits words through
   `SOMLATCH` into the banked SOM buffer, then raises `GINT`.
2. **68010 road-table path:** the MC68010 copies selected main-CPU ROM words to
   its road work buffers and drains them directly to the GSP FIFO. This path
   does not use the ADSP buffer in the measured window.
3. **GSP consumers:** observed GSP code consumes FIFO-fed data into work buffers
   and other uploaded display-list streams into GSP primitives. The exact
   relationship between the captured ADSP SOM transaction and those particular
   GSP inputs is not established.

Therefore, “the ADSP produces geometry” is too broad for the current evidence.
The ADSP emits a structured SOM word stream using object-ROM reads and numeric
operations. The captured words' full game-level meaning and exact block-to-FIFO
correspondence remain `UNKNOWN`.

## ADSP producer — observed and reconstructed boundary

**KNOWN for one bounded title-path transaction.** The committed capture
`reference/experiments/stunrun/adsp-som-transaction-f551/` was made from power-on
with `input=none`, frames 1–600, the frozen MAME binary SHA-256
`75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`, and ROM
manifest SHA-256 `b3cf2b613752c494cb441f85090dd056f53b9006bdc4564eb53396d3c4ae729d`.
The frame-551 transaction is bounded from `SOMCLK` through `GINT`, has 146
SOMLATCH words and 4,009 producer events, and is not truncated. The ADSP writes
the bank opposite the current 68010-selected bank (selected bank 1, SOM output
bank 0). `GINT` is value `0x0045` at ADSP PC `0x004C`; the corresponding 68010
IRQ-2 handler acknowledges at `0x818060`, PC `0x021402`, in the same frame.
Evidence: that fixture's `metadata.json` and `result.json`; findings §§64–67,
72.

The captured stream consists of two zero prologue words followed by nine
16-word records. The record emitter at ADSP PCs `$0515`–`$05D4` writes a
four-word header, a variable number of five-word spans, and two `$FFFF` words.
For this capture there are two spans per record. The C slices
`reproduction/adsp/som_record.c` and `som_walk.c`, including the low-index
interpolation kernel in `som_math.c`, derive all 146 captured words from the
captured DM state and PM coefficient table; the source-level scan/walk is not an
ADSP emulator. The coefficient entries used are `floor(0x8000/n)` for
`n=1..64` (stored in PM bits 23:8), with observed selectors no greater than
`0x40`. Findings §§66–69, 73 and `reproduction/adsp/README.md` record the
transcription limits and controls.

The upstream ring producer is a chain, not a single writer. In this transaction
four routines write `DM($0040..$0045)`; the `$04B2` pass 2 output is the only
captured value set consumed by the `$0514` scan, and `$04EB`'s clamp reproduces
it unchanged for all nine records. Earlier `$01E8` / `$01B0` data and pass 1
are overwritten for this observed input. `$01E8` also preserves three words
per record in `DM($08E7..$08E9)` sourced from `DM($2000)` (`SIMBUF`). `SIMBUF`
reads object ROM selected by `/MP`; it is distinct from ADSP program RAM and
from the PM reciprocal table. These are capture-specific observations, not a
claim that other inputs take the same branches. Findings §72.

## Hardware transport and the unresolved join

**MAME-CONFIRMED transport model.** The target's ADSP special data ports are
`DM($2000..$2007)`. `SIMBUF` (`$2000`) reads the banked `mainpcb:user1` object
ROM and increments its address; `SIMCLK` (`$2001`) sets that address;
`SOMLATCH` (`$2002`) stores to `m_som_memory[(m_m68k_adsp_buffer_bank ^ 1) *
0x2000 + (som_address++ & 0x1fff)]`; `SOMCLK` (`$2003`) sets the SOM address;
`GINT` (`$2006`) asserts the ADSP interrupt state routed to 68010 IPL2; and
`MP` (`$2007`) selects the object-ROM base. The 68010 reads the currently
selected bank through `$810000–$813fff`; this is two-bank ping-pong storage,
not a direct ADSP-to-GSP bus. Source: `docs/mame-machine-reference.md` §§4.2,
4.3 (pinned `harddriv_m.cpp` handlers); runtime counts and IRQ pairing in
`analysis/interconnect.md` and `reference/experiments/stunrun/adsp-special-io-trace.metadata.json`.

**Observed but not joined.** In independent 1,200-frame traces, the 68010
reads 106 count/terminator blocks from the SOM buffer window, and its transfer
loop emits the counted number of GSP FIFO writes to `$C0000C`, ending each
block at the observed `0xFFFF` terminator address. This establishes a
68010-buffer-to-GSP-FIFO edge, not the producer semantics of any block. The
frame-551 SOM fixture additionally records 5,849 later SOM-window reads and
13,431 HSTDAT writes after the captured GINT boundary, but the metadata
explicitly says those later counts are not correlated one-to-one with the
146-word transaction. Do not infer that its nine SOM records are exactly the
106 transferred blocks or that the captured GINT delimits a particular
68010-ferried payload. Evidence: `analysis/interconnect.md` lines 43–59,
76–85; `reproduction/maincpu/fifo_block.h`; frame-551 fixture metadata.

## Separate MC68010 road-data path

**KNOWN as a distinct transport path; payload semantics remain UNKNOWN.** The
68010 selects a 384-word road table in its own ROM, copies it into
`0xFF9584–0xFF9883`, and PC `0x02248E` drains the buffer to the GSP FIFO at
`0xC0000C`. Runtime traces observed sequential `+2` ROM reads and the FIFO
writer; a corrected measurement shows consecutive source halfwords packed
high-halfword first into 32-bit writes, preserving all 384 words. This is a
68010→GSP path and is not the ADSP SOM producer. The road source buffer is a
verbatim copy of commercial ROM in measured cases, so it must not be committed
as a binary fixture. Evidence: `analysis/static-rom-inventory.md` lines 72–98,
`analysis/track-storage.md`, and findings §75.

The GSP routine at `0xFFF48C20` consumes a FIFO-fed stream into two
input-dependent work-buffer families around `0xFFF6F650` and `0xFFF70650`,
combining adjacent byte lanes. The two streams are not identical copies. Later
copies reach palette-plane addresses, but payload/device semantics there are
not established. This consumer trace does not currently provide a same-run
correlation to the ADSP's frame-551 SOM output. Evidence:
`analysis/gsp-road-fifo-consumer.md`,
`reference/experiments/stunrun/m5-gsp-road-fifo-consumer.metadata.json`.

## GSP display-list consumer — do not equate it to an ADSP record

The GSP indexed reader around `0xFFF44BF0–0xFFF45630` reads index-driven
16-bit values at `base + index*0x10` in **logical bit-address units** (one
index step is one 16-bit word / two physical bytes), then prepares raster
operations including `FILL XY`. `reproduction/gsp/record_parser` intentionally
keeps an eight-word sampled window raw; it does not establish a full entry
length or semantic field names. Separately, the primary GSP payload codec
proves the first four F0 fields of a display-list stream feed FILL XY setup;
later F1 values are 32-bit and the full entry is variable-length. Neither
artifact by itself identifies those input words as the frame-551 SOM records.
Evidence: `analysis/gsp-record-walk.md`, `reproduction/gsp/record_parser.h`,
`reproduction/gsp/payload_codec.h`, findings §§57, 61–63.

## Confidence and open questions

| Claim | Confidence | Limit |
|---|---|---|
| ADSP emits the captured 146-word SOM stream, then GINTs; 68010 IRQ2 acknowledges it | `KNOWN` for frame 551 / input none | One bounded transaction, not all gameplay states |
| SOMLATCH stores into the bank opposite the 68010-selected bank | `KNOWN` as the pinned MAME model; observed selected/output banks agree in fixture | Emulator wiring is not a separate hardware measurement |
| 68010 reads SOM-buffer blocks and forwards data to GSP FIFO | `KNOWN` as a title-path transport edge | Block-to-specific-SOM-transaction relationship is not established |
| 68010 road table is a separate direct FIFO producer | `KNOWN` for measured replay windows | Road payload field meaning is not decoded |
| GSP FIFO consumer converts incoming words into two distinct work streams | `KNOWN` for traced consumer execution | Does not identify those streams as SOM output or road geometry by semantic name |
| SOM records are road vertices, polygons, or object meshes | `UNKNOWN` / unsupported | Record shape and numeric operations alone do not establish intent |
| The complete 146-word transaction is the exact source of a particular GSP FILL/list entry | `UNKNOWN` | No same-transaction value/sequence join is recorded |

## Next experiment

Use the existing bounded `tools/capture-adsp-som-transaction` approach with the
current default MAME binary only for a **new** capture (do not compare its result
across binaries). Add one same-run correlation that begins at a fresh
`SOMCLK`/GINT boundary and records ordered events with: selected SOM bank,
every SOMLATCH address/value, the subsequent 68010 SOM-window reads (with PC),
the 68010 GSP FIFO/HSTDAT writes (with PC), and GSP consumer reads/writes in
the same interval. Require full event counts and no truncation, and first
reconcile the 68010 read bank with the producer's opposite-bank writes. If the
68010 ferry cannot be unambiguously associated with the captured transaction,
record that as a bounded non-join and try a controlled one-transaction
checkpoint/hold rather than naming the payload. Separately retain the road
FIFO trace as a negative control: it is an independent producer and must not be
used to stand in for the ADSP stream.
