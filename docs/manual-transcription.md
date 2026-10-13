# Transcription of SP-338 (Atari Games TM 238, 1st printing 1989)

A clean transcription made from the **rendered page images**, not from
`reference/manuals/stunrunner-ocr.txt`. That OCR is unreliable in two separate
ways and must not be used for schematics:

1. **Its page markers are wrong.** It credits `34010-50` to SP-338 Page 9, but
   Page 9 is the ADC and shifter sheet. The marker does not reliably bound its
   own text.
2. **Schematic text is largely noise.** Component designators, net names and
   pin numbers come through as isolated letters; tables lose their alignment.

The OCR remains useful for body prose. It is superseded for anything tabular or
schematic.

Source images: `/tmp/stunrunner_pages/page-NN.png`, 72 pages. **SP-338 Page N is
PDF page 47+N**, because Page 10 is PDF 67 and Page 15 is PDF 72. Body pages use
the manual's own numbering (PDF 29 = page 3-5, PDF 17 = page 2-7), so body offset
differs by chapter.

Read so far: **1** (this file), **10**, **15**.

---

## SP-338 Page 1 — Main Wiring Diagram (PDF 58)

Board-level interconnect: power supply, video display, coin mechanism, and the
control panel. This is the page that settles what the controls are.

### 1.1 The modified flight control

```
S3 START ─┐
S4 START ─┴──────── both go to P25 (W, BK) and the J5 START net

S2  R FIRE ─┐
S1  L FIRE ─┴────── P24 nets W/OR, W/Y, and the shared W bus

R3  (ROLL RT)   ─┐   labelled "ROLL RT" with a right arrow
R4  (PITCH UP) ─┴   labelled "PITCH" with an up arrow
```

**Two START buttons, two FIRE buttons (left and right), and two
potentiometers — one roll, one pitch.** The manual's Chapter 3 prose agrees:
the handle assembly gives vertical motion and the flight control assembly gives
horizontal, which maps onto pitch and roll respectively.

### 1.2 Connector nets

`J5` control-panel header:

| pin | net | | pin | net |
|---|---|---|---|---|
| 1 | `TEST` | | 7 | `F IN` |
| 2 | `GND` | | 8 | `GND` |
| 3 | `AUD OUT` | | 9 | `+5V` |
| 4 | `AUD F` | | 10 | `+5V` |
| 5 | `KEY` | | 11 | `+5V` |
| 6 | `OUT` | | | |

`J9`:

| pin | net |
|---|---|
| 1 | `KEY` |
| 4 | `+5V` |
| 5 | `STEERING POT` |
| 6 | `AIM POT` |
| 11 | `GND` |

**A second pair of potentiometers exists on `J9`**, separate from the R3/R4 pair
on the flight control. So the board expects at least four analogue axes before
any ADC is considered: steering, aim, roll, pitch. MAME carries two ADC axes.

`J5` (right-hand group on the same drawing):

| pin | net |
|---|---|
| 1 | `RIGHT TRIGGER` |
| 4 | `KEY` |
| 7 | `START` |
| 8 | `LEFT TRIGGER` |
| 10 | `SW_GND` |
| 11 | `GND` |

**`LEFT TRIGGER` and `RIGHT TRIGGER` are on `SW_GND`, i.e. they are switches, not
analogue.** This is the hardware confirmation of §3 of `docs/hardware-contract.md`
from the wiring rather than from prose: the self-test menus that the manual says
are navigated with "the right or left trigger" are being driven by two of the
sixteen external switch inputs at `$A80000`, and MAME currently models three
bits there.

`JSCOM` (sound board header): `TEST, GND, DATA, F IN, KEY, OUT, CLK, GND, GND, +5V, +5V`.

`P6`: `GND, SELF TEST, CON CNTR 1, CON CNTR 2, GND, KEY, CON CNTR 1, CON CNTR 2`
— two coin mechanisms with separate counters, plus an explicit `SELF TEST` line.

### 1.3 Board switches

| ref | function | where |
|---|---|---|
| `S11` | `TEST SW` — the self-test switch | on `P29`, main board |
| `R1` | `VOLUME CONTROL` | on `P29` |
| `R3`, `R4` | roll, pitch pots | modified flight control |
| `P35` | `COIN COUNTER` | `P100`/`P55` coin interface |

So the self-test switch is a **physical switch on the main PCB** (`S11`), routed
to `P6`'s `SELF TEST` line, and readable by the 68010 at `$60C000` bit 5. The
manual's memory map and MAME's `PORT_SERVICE(0x20)` are both describing that one
switch; nothing here contradicts either.

### 1.4 Power and video

* Power supply wiring diagram `046989-01`: `+SENSE, +5V, +5V, +5V, GND, GND, -SENSE`
  — a three-wire sense arrangement, i.e. the +5 V rail is remotely sensed.
* Video display connector: `BK, GN, BN` / `R, BK, GN` — analogue RGB to the
  monitor, separate from the digital GSP side.
* Speaker `LS1` via `P30`, driven from the sound board through `P29`.

---

## SP-338 Page 10 — Video (PDF 67)

Block diagram, transcribed in full in `docs/hardware-contract.md` §7. The parts
of it that matter here:

```text
34010-50 GSP  ---> 34012-50 PSP ---> VDD-VD15  (15-bit digital video)
    \---> Address Buffers ---> MA0-MA7 ---> VRAM Array (16) 64K x 4
                                            --> SD0-SD15 --> Pixel Scanner
                                            --> SPIX0-SPIX7 --> Color RAM
                                            --> RED0-7 GRN0-7 BL0-7
```

Jumper settings printed on the sheet: `Speed = A, B`, `BCLK = QB`, `VCLK = QB/2`.

Video output stage: three `20S` 8-bit shift registers driven from `RRV`, `CRV`
and `BRV`, each with a `BCLK`/`/BLANK4` clock, feeding `2N3906`/`2N3904` output
stages and `RED` / `GREEN` / `BLUE` nets to `J10`. `Q6`/`Q7` are `2N37100`
synchronous 16-bit counters for red and blue.

Clocks on this sheet: `XO-SCH 40MHZ` to the pixel scanner; a `15.00Y 0.11431B 12`
crystal through an LS374 divider producing `4 MHz / 16 MHz` domains.

---

## SP-338 Page 15 — Main Board Memory Map (PDF 72)

Transcribed in full, as a table, in `docs/hardware-contract.md` §1. This is the
authoritative memory map and it resolves every address question except `$C00000`.

---

## Corrections this transcription forces on the OCR-derived notes

* `FF 8000 - FF BFFF RAM 0 (16K)` and `FF C000 - FF FFFF RAM 1 (16K)`. The OCR
  reads `FE BFEF` / `FF BFEF`; the image is unambiguous.
* GSP reset is `60 400C` / `60 401C`, not `60 4006` / `60 4016` as the OCR has it.
* `60 C000` is labelled `SW1`, and the table under it covers **all sixteen bits**
  (D15–D8 option switches, D7/D6 coin switches, D5 self-test, D0 diagnostic).
  The OCR renders this as two disconnected fragments.
