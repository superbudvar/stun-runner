# TMS34010 (GSP) — Working Reference

Extracted from the OCR text of the **Texas Instruments TMS34010 Graphics System
Processor User's Guide**, literature number **SPVU001A (1988)**.

* Source PDF: `/tmp/tms34010ug.pdf`
* Working OCR text: `/tmp/ug.txt` (27,789 lines, produced with `pdftotext -layout`)
* Every citation below is `(ug.txt:NNNN)` and refers to line numbers in `/tmp/ug.txt`.

Transcription rules used here:

* Bit-field tables are **transcribed**, not paraphrased.
* Anything that could not actually be read out of the OCR is marked
  **`[OCR-UNREADABLE]`**. No bit position or numeric value has been invented.
* Where a garbled value has been disambiguated from surrounding prose, the
  disambiguation and the supporting line number are stated explicitly.
* Where a fact is genuinely absent, the text says **NOT IN THIS DOCUMENT**.

---

## 0. Reliability of this extraction

| Section | OCR quality | Trust |
|---|---|---|
| §3 Memory addressing, §3.2 memory map | Clean | High |
| §6 DPYADR / DPYSTRT / DPYTAP field tables | Clean | High |
| §6 DPYCTL field table | Clean | High |
| §6 DPYCTL **DUDATE increment-size column** | **Garbled** — column values render as `O N' CI' CO CO N d' CO` / `C'') CO N` (ug.txt:5186, 5196) | **Low — reconstructed, see §1.4** |
| §6 DPYADR **LAD pin-mapping diagram** | **Garbled** — ASCII art, mis-spaced, `Co:40sponding`, `Ccr•es•ording`, `P -ADP Bits:`, `DP ADR Hula:` (ug.txt:5074–5086) | **Low — superseded by clean §9 prose, see §1.3** |
| §6 DPYADR LAD prose (ug.txt:5027–5038) | Reads cleanly but is **internally inconsistent with §9** ("LAD12–LAD23") | **Suspect — see §1.3** |
| §9.9 Screen refresh / Figure 9-14 prose (ug.txt:9993–10017) | Clean | High — this is the authoritative LAD mapping |
| §4.2.3 display pitch, §4.3.1 XY→linear | Clean | High |
| §6 Table 6-1 I/O register summary (ug.txt:4426–4571) | Mostly clean; `HSTADRL` address OCR'd as `>C000 OODO`, `HSTCTLL` address collides with `HSTADRL`, `VCOUNT2` absent | High for names/fields, see §2 caveats |
| §6 CONTROL register address | Address line OCR'd as `>C000 0080` (ug.txt:4702) — **wrong**, Table 6-1 says `>C000 00B0` | Use Table 6-1 |
| §6 HSTDATA address | OCR'd `>c000 0000` / `>C000 OOCO` in prose (ug.txt:6023–6027) | Med — see §2 |
| §7 graphics operations (narrative) | Clean | High |
| §7.10 window checking | Clean | High |
| §12 Table 12-7 opcode summary | Readable but column-aligned badly in places; **several opcodes are suspect** (ANDNI printed with same opcode as ANDI; CLR mangled) | Medium — verify each opcode against MAME/datasheet |
| §12 per-instruction "Encoding" pages | ASCII art, 132 `^Encoding` blocks | Medium |
| §8 interrupt priorities | Clean | High |
| §8 Table 8-3 vector addresses (NMI/HI rows) | **Partially garbled** | Medium/Low — see §5 |
| §9.9.1 Figure 9-14 diagrams | ASCII art, badly mangled | Low — the surrounding prose is clean and sufficient |
| §11.5 memory organization | Clean | High |
| §13 instruction timings | Clean | High |
| Appendix B "Emulation Guidelines for Prototyping" | Clean | High — but about the XDS/22 emulator board, not silicon replication |

**Also note:** the request cited "register name ASCII arrays near ug.txt:1254–1262 and
1389–1397". Those lines are **not** the register list — ug.txt:1254–1262 is the
HINT pin description and ug.txt:1385–1397 is the §2.4 Video Timing Signals
heading. The authoritative register list is **Table 6-1, ug.txt:4426–4571**.

---

## 1. Display address generation

### 1.0 TL;DR for the emulator

The refresh address on the LAD pins is *entirely* determined by DPYADR and DPYTAP:

```
row-address time    LAD0..LAD11 = DPYADR[15..4]      (LAD0 = DPYADR bit 4 … LAD11 = DPYADR bit 15)
                    LAD12..LAD14 = 0
                    LAD15 = 1                        ("RF" bus-status bit)

column-address time LAD0..LAD5   = DPYTAP[5..0]
                    LAD6..LAD10  = DPYADR[6..2] | DPYTAP[10..6]
                    LAD11..LAD13= DPYTAP[13..11]
                    LAD14..LAD15= 0                 ("TR" and "IAQ" bus-status bits)
```

If `DPYCTL.ORG == 0` (the default), the DPYADR bits are **inverted** immediately
before being output; if `ORG == 1` they are output unaltered.

MAME's decode

```c
uint16_t dpyadr = IOREG(REG_DPYADR);
if (!(IOREG(REG_DPYCTL) & 0x0400))   /* ORG == 0 */
    dpyadr ^= 0xfffc;
params->rowaddr = dpyadr >> 4;
params->coladdr = ((dpyadr & 0x007c) << 4) | (IOREG(REG_DPYTAP) & 0x3fff);
```

is a **literal, correct transcription** of ug.txt:9993–10006 (see §1.3). The only
MAME line with no counterpart in this guide is
`params->yoffset = (IOREG(REG_DPYSTRT) - IOREG(REG_DPYADR)) & 3;` — see §1.6.

---

### 1.1 DPYADR — Display Address Register, `>C000 01E0`

Bit assignment line (ug.txt:5000):

```
15 14 13 12 11 10  9 | 8  7  6  5  4  3  2 | 1  0
<--------- SRFADR ---------->      <-- LNCNT -->
```

Field table, transcribed verbatim (ug.txt:5004–5006):

| Bits | Name | Description |
|---|---|---|
| 0-1 | LNCNT | Scan line counter |
| 2-15 | SRFADR | Screen refresh address |

**LNCNT** (ug.txt:5012–5025): counts the scan lines output between successive
screen-refresh cycles. The 2-bit field is loaded from the 2-bit LCSTRT field of
DPYSTRT at the end of each screen-refresh cycle. LCSTRT = 0 → refresh every line;
LCSTRT = 1, 2, 3 → refresh every 2, 3, 4 lines.

**SRFADR** (ug.txt:5027–5038), key sentence transcribed:

> "SRFADR is the source of the row and column addresses output during a
> screen-refresh cycle. The 14 bits of SRFADR are output as logical address bits
> 10-23 during screen-refresh cycles. During row address time, DPYADR4-DPYADR15
> are output on LAD12-LAD23, and Os are output on the remaining LAD pins (except
> as modified by the contents of the DPYTAP register). During column address time,
> DPYADR2-DPYADR6 are output on LAD6-LAD10 and Os are output on the remaining LAD
> lines. Following the completion of each screen-refresh cycle, the value in SRFADR
> is decremented by the amount indicated in the DUDATE field of the DPYCTL
> register."

**Logical-address correspondence** (ug.txt:5054–5058), transcribed:

> "The bits of a 32-bit logical address are numbered 0 to 31, beginning with the
> LSB. The 14 MSBs of DPYADR, shown in the diagram below, are output as logical
> address bits 10-23 during a screen-refresh cycle. DPYADR2 corresponds to logical
> address bit 10, DPYADR3 corresponds to logical address bit 11, and so on."

i.e. **`DPYADR bit n → logical address bit n + 8`, for n = 2 … 15.**

The DPYTAP correspondence follows the same rule one step lower:
**`DPYTAP bit n → logical address bit n + 4`, for n = 0 … 13** (derived; see §1.3
and §1.5, where it is what makes ug.txt:5091–5098 and ug.txt:9934–9946 consistent).

---

### 1.2 DPYSTRT — Display Start Address, `>C000 0090`

Bit assignment line (ug.txt:5392) and field table (ug.txt:5396–5398):

```
15 .. 10 | 9 8 7 6 5 4 3 2 | 1 0
<------ SRSTRT ------>  <LCSTRT>
```

| Bits | Name | Description |
|---|---|---|
| 0-1 | LCSTRT | Starting line count |
| 2-15 | SRSTRT | Starting screen-refresh address |

**LCSTRT table** (ug.txt:5408–5415), transcribed:

| LCSTRT | Scan lines between screen-refresh cycles |
|---|---|
| 00 | 1 |
| 01 | 2 |
| 10 | 3 |
| 11 | 4 |

LCSTRT is loaded into LNCNT at the end of each screen-refresh cycle and at the
start of the last h-blank preceding the first active scan line of a new frame.

**The SRSTRT complement paragraph — THE important one** (ug.txt:5432–5440),
transcribed verbatim:

> "The sense of the SRSTRT value depends on the value of the ORG (origin select)
> bit in the DPYCTL register. When ORG=0, SRSTRT is loaded with the 1's complement
> of the starting address. When ORG=1, SRSTRT is loaded with the unmodified starting
> address. Regardless of the value of the ORG bit, the starting address points to
> the location in memory of the first pixel output to the screen during each frame.
> For a typical CRT display, the first pixel of each frame is output to the top
> left corner of the screen."

Restated in §9.9.1.2 (ug.txt:9948–9954):

> "For a typical CRT display, the starting address is the address in memory of the
> pixel that appears in the upper left corner of the display. If ORG bit in DPYCTL
> is 0, the 1's complement of the starting address should be loaded into DPYSTRT.
> If ORG=1, the starting address loaded into DPYSTRT should not be complemented."

And the load itself (ug.txt:9956–9961):

> "DPYADR is automatically loaded with the starting display address from DPYSTRT
> prior to the start of each frame… bits 2-15 of DPYSTRT (SRSTRT) are loaded into
> bits 2-15 of DPYADR (SRFADR). The load occurs coincident with the start of the
> horizontal blanking interval that occurs just at the end of the last active scan
> line of the preceding frame."

#### Confirm / refute `dpyadr ^= 0xfffc`

**MAME's `dpyadr ^= 0xfffc` is correct, with one caveat about scope.**

The guide says "the 1's complement of the starting address". Taken over the whole
16-bit DPYADR register that would be `^= 0xffff`, but **DPYADR bits 0-1 are LNCNT,
which is *not* part of the address and must not be inverted** — LNCNT is loaded
separately from LCSTRT. The complement therefore applies only to the 14-bit SRFADR
field, i.e. bits 2-15, which is exactly `^= 0xfffc`. The guide states the complement
only where SRSTRT/SRFADR are discussed (bits 2-15) and never mentions inverting
LNCNT, which supports the narrower reading.

Two independent statements confirm that the *stored* value is the complement and the
*output* value is inverted back:

* ug.txt:9993–10006 — "If ORG=0, the DPYADR bits are inverted before being output;
  otherwise, they are output unaltered."
* ug.txt:10008–10017 — "If ORG=0, the screen-refresh address in DPYADR is
  effectively incremented; the one's complement of the address contained in DPYADR
  is decremented by the DUDATE amount, but is inverted before being output. This is
  equivalent to incrementing the address. If ORG=1, the address is decremented."

So the register always *stores* the complemented form and the complement is undone
at the pins. A complement applied at the pins is exactly what MAME does at the point
of use. **Verdict: confirmed.**

---

### 1.3 LAD pin mapping — reconstructed, and the §6 vs §9 conflict

#### 1.3.1 The §6 ASCII diagram is illegible — `[OCR-UNREADABLE]`

Raw OCR of the diagram (ug.txt:5074–5086), reproduced exactly as it came out:

```
LAD Pin No.:          15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
Logical Row           XX 26 25 24 23 22 21    10 19 18 17 16 15 14 13 12 1 Row
Address Bits:                                                       Address
Co:40sponding                                                     Time
 P -ADP Bits:              15 14  13  12  11  10 9 8 7 8 5 4

Logical Column        XX XX 29 28 27   14 13 12 11  10   9   8   7   6    5   4
Address Bits:                                                        Column
                                                                   Address
Ccr•es•ording                                                    Time
 DP ADR Hula:                  7    8    5   4    3   2
                                                                    }
```

What can be read with confidence:

* `LAD Pin No.:` header, 15 down to 0.
* Row time: LAD15 and LAD14 are `XX`; LAD13 … LAD2 carry logical row-address bits
  **26 … 15** in descending order; LAD1/LAD0 do not carry row data (they are part
  of the "Row Address" bracket, but the row of digits is visibly mis-spaced —
  `26 25 24 23 22 21 10 19 18 17 16 15 14 13 12`, the `10` being a mis-OCR of
  `17`).
* Column time: LAD15…LAD13 are `XX`; LAD12 … LAD3 carry logical bits **29 … 20**.
* `The symbol xx indicates status information output with the row and column
  addresses` (ug.txt:5071–5072).
* `Co:40sponding` = "Corresponding"; `P -ADP Bits:` = "DPYADR Bits";
  `Ccr•es•ording` / `DP ADR Hula:` = "Corresponding DPYADR Bits:".

What is **`[OCR-UNREADABLE]`**: the per-pin alignment of the column-address block.
The digit columns do not line up with the `LAD Pin No.:` header and the DPYADR
column contains only six entries (`7 8 5 4 3 2`) for ten pins. It is **not possible
to reconstruct the column-time pin alignment from this diagram alone** — do not
attempt it.

The corresponding-DPYADR-blocks for row time also cannot be read positionally
(`15 14 13 12 11 10 9 8 7 8 5 4`, where the second `8` is a mis-OCR of `6`, giving
only twelve entries for twelve pins and, crucially, no usable pin alignment).

**Do not build anything from this diagram.** §9 supersedes it.

#### 1.3.2 §9 Figure 9-14 prose is clean and authoritative

ug.txt:9993–10006, transcribed verbatim:

> "The address output during each screen-refresh cycle is contained in bits 2
> through 15 of the DPYADR register (the 14-bit SRFADR field). As shown in Figure
> 9-14 b, DPYADR bits 4-15 are output at the LAD0-LAD11 pins during the row address
> time of the screen-refresh cycle. If ORG=0, the DPYADR bits are inverted before
> being output; otherwise, they are output unaltered. Zeros (logic-low level) are
> output on LAD12-LAD14, and a one (logic-high level) is output on LAD15; this is
> the RF status bit."
>
> "During the column address time of the screen-refresh cycle, bits 2-6 of DPYADR
> are output at LAD6-LAD10. If ORG=0, the DPYADR bits are inverted before being
> output. DPYTAP bits 6-10 are ORed with DPYADR bits 2-6 and output at LAD6-LAD10.
> Bits 0-5 and 11-13 of DPYTAP are output at LAD0-LAD5 and LAD11-LAD13,
> respectively. Zeros are output at LAD14-LAD15 (the TR and IAQ status bits)."

Reconstructed as a clean table:

**Row-address time**

| LAD pin | Source | Logical address bit |
|---|---|---|
| LAD15 | constant 1 (RF status bit) | — |
| LAD14 | 0 | — |
| LAD13 | 0 | — |
| LAD12 | 0 | — |
| LAD11 | DPYADR15 | 23 |
| LAD10 | DPYADR14 | 22 |
| LAD9 | DPYADR13 | 21 |
| LAD8 | DPYADR12 | 20 |
| LAD7 | DPYADR11 | 19 |
| LAD6 | DPYADR10 | 18 |
| LAD5 | DPYADR9 | 17 |
| LAD4 | DPYADR8 | 16 |
| LAD3 | DPYADR7 | 15 |
| LAD2 | DPYADR6 | 14 |
| LAD1 | DPYADR5 | 13 |
| LAD0 | DPYADR4 | 12 |

**Column-address time**

| LAD pin | Source | Logical address bit |
|---|---|---|
| LAD15 | 0 (IAQ status) | — |
| LAD14 | 0 (TR status) | — |
| LAD13 | DPYTAP13 | 17 |
| LAD12 | DPYTAP12 | 16 |
| LAD11 | DPYTAP11 | 15 |
| LAD10 | DPYADR6 \| DPYTAP10 | 14 |
| LAD9 | DPYADR5 \| DPYTAP9 | 13 |
| LAD8 | DPYADR4 \| DPYTAP8 | 12 |
| LAD7 | DPYADR3 \| DPYTAP7 | 11 |
| LAD6 | DPYADR2 \| DPYTAP6 | 10 |
| LAD5 | DPYTAP5 | 9 |
| LAD4 | DPYTAP4 | 8 |
| LAD3 | DPYTAP3 | 7 |
| LAD2 | DPYTAP2 | 6 |
| LAD1 | DPYTAP1 | 5 |
| LAD0 | DPYTAP0 | 4 |

(All DPYADR-derived rows are inverted when `ORG == 0`.)

#### 1.3.3 Figure 11-1 — a third, CLEAN statement of the same two mappings

`ug.txt:11854–11898` contains "Figure 11-1. Triple Multiplexing of Addresses and Data",
which is the cleanest pin table in the whole document. Its two address columns are
the row-address and column-address logical bit numbers driven on each LAD pin:

| LAD pin | Row Address | Column Address | Data |
|---|---|---|---|
| LAD15 | RF (status) | IAQ (status) | 15 |
| LAD14 | 26 | TR (status) | 14 |
| LAD13 | 25 | 29 | 13 |
| LAD12 | 24 | 28 | 12 |
| LAD11 | 23 | 27 | 11 |
| LAD10 | 22 | 14 | 10 |
| LAD9 | 21 | 13 | 9 |
| LAD8 | 20 | 12 | 8 |
| LAD7 | 19 | 11 | 7 |
| LAD6 | 18 | 10 | 6 |
| LAD5 | 17 | 9 | 5 |
| LAD4 | 16 | 8 | 4 |
| LAD3 | 15 | 7 | 3 |
| LAD2 | 14 | 6 | 2 |
| LAD1 | 13 | 5 | 1 |
| LAD0 | 12 | 4 | 0 |

Legend, transcribed verbatim (ug.txt:11892–11895):

> "RF = DRAM-Refresh bus status bit
> IAQ = instruction acquisition bus status bit
> TR = VRAM Shift-Register-Transfer bus status bit"

OCR damage in this figure: the LAD4 row's row-address entry prints as `18` and LAD3's
as `1 15`; the LAD2 row is blank and its `14` has slid down onto the LAD1 line. These
are recovered as **LAD4 = 16** and **LAD2 = 14** from the strictly descending
row-address sequence 26,25,24,23,22,21,20,19,18,17,**16**,**15**,**14**,13,12 — a
recovery supported by §9.9.1.4, which puts DPYADR15 (logical 23) on LAD11.

**What Figure 11-1 confirms and one place it does not apply.** LAD0-LAD10 during
column address time carry logical bits 4-14, which matches §9.9.1.4 exactly
(DPYTAP0-5 → logical 4-9; DPYADR2-6 → logical 10-14). LAD12-LAD14 during row address
time carry logical bits 24-26 in the *general* local-memory cycle, whereas §9.9.1.4
says a screen-refresh cycle drives zeros there — consistent, because a refresh
address only reaches logical bit 23. LAD11-LAD13 during column address time carry
logical bits 27-29 in Figure 11-1, but §9.9.1.4 says a screen-refresh cycle drives
DPYTAP bits 11-13 there. Figure 11-1 is the whole-bus multiplexing table (it also
covers DRAM refresh and ordinary CPU accesses); §9.9.1.4 is specific to the refresh
cycle. **For the refresh path, use §9.9.1.4** — it is also what MAME implements and it
is the one consistent with §9.9.1.2's "bits 4-23 … overlap at bits 10-17".

**This resolves the conflict.** §6 DPYADR says "DPYADR4-DPYADR15 are output on
LAD12-LAD23" (ug.txt:5031). The LAD bus only has 16 pins (LAD0–LAD15), so "LAD23"
cannot be literal; it is an OCR error or a guide typo. §9's mapping is:

* self-consistent with the 16-pin LAD bus,
* self-consistent with the board-wiring example at ug.txt:5091–5098 (see §1.5),
* self-consistent with DPYTAP ↔ logical-bit correspondence in §9.9.1.2
  (ug.txt:9934–9946),
* and is exactly what MAME implements.

**Prefer §9.**

Minor conflict: §6 DPYTAP says the column address is "the OR of bits 2-7 of DPYADR
and bits 0-15 of DPYTAP" (ug.txt:5459–5467) — six DPYADR bits, 2-7 — whereas §9 says
bits **2-6**, five DPYADR bits. §9 is consistent with the row-time mapping and with
MAME (`& 0x007c` = bits 2-6). Treat "2-7" in §6 as a typo; use bits 2-6.

---

### 1.4 DPYCTL — Display Control Register, `>C000 0080`

Bit-assignment line as OCR'd (ug.txt:5110):

```
ENVI NIL IDXVISREISRTIORGI | DUDATE
```

i.e. ENV(15) NIL(14) DXV(13) SRE(12) SRT(11) ORG(10) DUDATE(2-9).

Field table, transcribed verbatim (ug.txt:5113–5122):

| Bits | Name | Description |
|---|---|---|
| 0 | HSD | Horizontal sync direction |
| 1 | Reserved | Not used |
| 2-9 | DUDATE | Display address update |
| 10 | ORG | Screen origin select |
| 11 | SRT | Shift register transfer enable |
| 12 | SRE | Screen refresh enable |
| 13 | DXV | Disable external video |
| 14 | NIL | Noninterlaced video enable |
| 15 | ENV | Enable video |

**HSD / DXV → sync direction table** (ug.txt:5144–5148), transcribed:

| HSD | DXV | HSYNC | VSYNC |
|---|---|---|---|
| 0 | 0 | Input | Input |
| 0 | 1 | Output | Output |
| 1 | 0 | Output | Input |
| 1 | 1 | Undefined | Undefined |

#### ORG — exact effect on the DPYADR increment direction

ug.txt:5228–5241, transcribed verbatim:

> "If ORG=0 then DPYADR is updated by being incremented by the value in the DUDATE
> field. If ORG=1 then DPYADR is updated by being decremented by the value in the
> DUDATE field. Unless explicitly stated otherwise, the discussion in this document
> assumes that the default origin (ORG=0) is used."

ORG origin table (ug.txt:5236–5241):

| ORG | Meaning |
|---|---|
| 0 | XY origin is the upper-left corner |
| 1 | XY origin is the lower-left corner |

The precise register-level mechanics (ug.txt:10008–10017), transcribed verbatim:

> "After the row and column addresses have been output, the address in DPYADR bits
> 2-15 is decremented by the 8-bit value in DPYCTL bits 2-9 (the DUDATE field)…
> The 8-bit DUDATE value is a binary number consisting of seven 0s and a single 1.
> This single 1 indicates the position at which DPYADR will be decremented. If ORG=0,
> the screen-refresh address in DPYADR is effectively incremented; the one's
> complement of the address contained in DPYADR is decremented by the DUDATE amount,
> but is inverted before being output. This is equivalent to incrementing the
> address. If ORG=1, the address is decremented."

**The two §6 statements that look contradictory are not:**

* §6 SRFADR description (ug.txt:5038) says the register is **decremented**.
* §6 ORG description (ug.txt:5228) says it is **incremented when ORG=0**.

They describe two different things: the *register* always subtracts DUDATE from the
stored (complemented) 14-bit field; the *effective* address increments when ORG=0
because the stored value is a one's complement that gets inverted at the pins.

#### DUDATE

ug.txt:5176–5183, transcribed verbatim:

> "The DUDATE field indicates the amount by which the SRFADR field in the DPYADR
> register is incremented (if ORG=0) or decremented (ORG=1) following completion of
> each memory-to-shift-register cycle used to refresh the screen. DUDATE is loaded
> with a value containing seven 0s and a single 1. The 1 indicates the bit position
> at which DPYADR is to be incremented (or decremented if ORG=1)."

**Reconstructed DUDATE table.** The DUDATE-value column of the original table reads
cleanly (ug.txt:5187–5203): `00000000, 00000001, 00000010, 00000100, 00001000,
00010000, 00100000, 01000000, 10000000`. The adjacent "Increment Size" column is
**`[OCR-UNREADABLE]`** — it comes through as `O N' CI' CO CO N d' CO` (ug.txt:5186)
and `C'') CO N` (ug.txt:5196), i.e. no numeral survives.

The reconstruction below is **derived, not read**: DUDATE is stated to be "seven 0s
and a single 1" whose "bit position" determines the increment, and ug.txt:10008–10017
states the 8-bit DUDATE value is used as a plain binary number subtracted from the
14-bit SRFADR field. Therefore the single 1 at bit position *n* contributes the
value 2ⁿ to that 14-bit subtraction:

| DUDATE (DPYCTL bits 9..2) | 1 at bit position | Increment applied to the 14-bit SRFADR field | Equivalent logical-address advance (bits) | Equivalent byte advance |
|---|---|---|---|---|
| `00000000` | none | 0 | 0 | 0 |
| `00000001` | 0 | 1 | 2¹⁰ = 1024 | 128 |
| `00000010` | 1 | 2 | 2¹¹ = 2048 | 256 |
| `00000100` | 2 | 4 | 2¹² = 4096 | 512 |
| `00001000` | 3 | 8 | 2¹³ = 8192 | 1024 |
| `00010000` | 4 | 16 | 2¹⁴ = 16384 | 2048 |
| `00100000` | 5 | 32 | 2¹⁵ = 32768 | 4096 |
| `01000000` | 6 | 64 | 2¹⁶ = 65536 | 8192 |
| `10000000` | 7 | 128 | 2¹⁷ = 131072 | 16384 |

"Logical-address advance" = increment × 2¹⁰, because SRFADR occupies logical address
bits 10-23 (ug.txt:5054–5058). **The "Increment Size" column values themselves are
`[OCR-UNREADABLE]` and must be confirmed against the datasheet or MAME if the exact
printed numbers matter.** The functional behaviour above is what the surrounding
prose supports.

Note the magnitude: because SRFADR only spans logical bits 10-23, the smallest
expressible screen pitch is 2¹⁰ bits = 128 bytes and the largest is 2¹⁷ bits = 16 KB.

ug.txt:5205–5206, transcribed:

> "The increment size is undefined when more than one bit in the DUDATE field is a
> 1."

ug.txt:5206–5219, transcribed:

> "When interlaced scan mode is enabled, SRFADR is incremented/decremented by half
> the value indicated in DUDATE at the start of a vertical blanking interval
> preceding the start of an odd field, just after DPYADR2-DPYADR15 have been loaded
> from DPYSTRT."
>
> "For noninterlaced scanning, DUDATE is programmed to increment the screen address
> by one scan line. For interlaced scanning, DUDATE is programmed to increment the
> screen address by two scan lines. Larger increments are typically not used since
> screen-refresh cycles do not occur more often than once per active scan line. In
> special applications, however, the value of DUDATE can be adjusted to achieve
> video effects such as vertical zoom in and zoom out. (Horizontal zoom must be
> implemented in the external shift register logic)."

§9.9.1.3 restates the interlaced rule (ug.txt:10167–10175):

> "The size of the DUDATE increment specified for an interlaced display should be
> twice that required for a noninterlaced display of the same dimensions… Before
> the start of the even field, half the value of the DUDATE increment is added to
> the starting address loaded into DPYADR to obtain the necessary starting
> displacement."

#### Remaining DPYCTL fields

* **SRT** (ug.txt:5243–5270): 0 = pixel access cycles occur normally; 1 = pixel
  access cycles are converted into VRAM shift-register-transfer cycles. Pixel
  **read** becomes memory-to-shift-register; pixel **write** becomes
  shift-register-to-memory. Only pixel accesses are affected — instruction fetches
  and non-pixel data accesses are unaltered.
* **SRE** (ug.txt:5278–5288): 0 = disable screen refresh, 1 = enable.
* **DXV** (ug.txt:5291–5308): 0 = external video source, 1 = internally generated
  timing. See also the HSD/DXV table above.
* **NIL** (ug.txt:5310–5318): 0 = interlaced, 1 = noninterlaced.
* **ENV** (ug.txt:5326–5339): 0 = blank the entire screen (BLANK held active low
  for the whole frame, display interrupt inhibited); 1 = enable video. The display
  interrupt is requested when VCOUNT = DPYINT.

---

### 1.5 DPYTAP — Display Tap Point Address, `>C000 01B0`

Field table, transcribed (ug.txt:5455–5457) — the OCR renders "14-15" as `1 4-1 5`,
disambiguated as 14-15 from the 16-bit width:

| Bits | Name | Description |
|---|---|---|
| 0-13 | DPYTAP | Display tap point address |
| 14-15 | Reserved | Not used |

ug.txt:5459–5467, transcribed verbatim:

> "The DPYTAP register contains a VRAM tap point address output during a
> screen-refresh (memory-to-shift-register) cycle. (The contents of DPYTAP are not
> output during a shift-register transfer initiated under program control while the
> SRT bit … is set to 1.) During a screen-refresh cycle, the 16 bits of the DPYTAP
> register are logical ORed with the value output at the LAD0-LAD15 pins during the
> column address time. DPYTAP bit 0 is ORed with LAD0, DPYTAP bit 1 is ORed with
> LAD1, and so on. This means that the column address output during the cycle is
> the OR of bits 2-7 of DPYADR and bits 0-15 of DPYTAP."
>
> (See §1.3 — the "bits 2-7" here conflicts with §9's "bits 2-6"; §9 wins.)

DPYTAP = 0 puts the leftmost pixel of the frame buffer on screen; incrementing
DPYTAP pans right. It is typically programmed only in the column-address bit
positions that SRFADR never increments — e.g. a VRAM needing an 8-bit column
address, where SRFADR alternates the two MSBs 00/01/10/11, so DPYTAP should hold 1s
only in the six LSB positions of the column address.

§9.9.1.2 states the overlap explicitly (ug.txt:9934–9946), transcribed verbatim:

> "The address output during a screen-refresh cycle identifies the starting pixel
> on the scan line about to be output to the monitor… The screen-refresh address
> consists of bits 4-23 of the logical address, which are generated by combining
> the values contained in SRFADR and DPYTAP. Where SRFADR and DPYTAP overlap (bits
> 10-17 of the logical address), the address bits are generated by logical ORing the
> corresponding bits of SRFADR and DPYTAP."

This is the direct source of the rule used in §1.3: DPYADR supplies logical bits
10-23, DPYTAP supplies logical bits 4-17, and they OR in the overlap 10-17.

Panning (ug.txt:10185–10189):

> "The DPYTAP register supports horizontal panning of the screen across a display
> memory that is larger than the screen. The value contained in the low-order bits of
> DPYTAP furnish the LSBs of the column address output during the screen-refresh
> cycle."

---

### 1.6 The concrete question, answered

> *Given SRFADR, LNCNT and DPYTAP for a scanline, what is the exact VRAM logical
> address of the first pixel output on that line, and what is the byte offset of the
> start of the next scanline?*

**Exact first-pixel logical address of a scan line.**

The screen-refresh address "consists of bits 4-23 of the logical address"
(ug.txt:9934–9946). Bits 0-3 are never output during a refresh cycle — every local
bus access is word-aligned and "the four LSBs of the 32-bit starting address are 0s
and do not appear on the local bus" (ug.txt:1560–1580, §3.1) — so the refresh
address always points at a 16-bit word, and the pixel offset *within* that word is
handled by the shift-register phase, not by the address.

```
SRFADR_eff = (ORG == 0) ? (~DPYADR[15:2] & 0x3fff) : (DPYADR[15:2])

logical[23:10] = SRFADR_eff
logical[17: 4] = DPYTAP[13:0]
logical[ 9: 4] = DPYTAP[ 5:0]                     (DPYTAP only)
logical[15:10] = DPYADR[ 6:2] | DPYTAP[13:8]...  (OR in the overlap, bits 10-17)
logical[ 3: 0] = 0
```

Equivalently, the 20-bit refresh address as driven onto LAD during row/column
address time is:

```
A = (SRFADR_eff << 10) | (DPYTAP << 4)          /* A[19:0], A[3:0] == 0 */
```

and `A` is the logical **bit** address of the first word of the scan line, which
holds the first pixel at bit offset 0 of that word.

**Byte offset of the start of the next scan line.**

The register-level update is a subtraction of the DUDATE value from the 14-bit
complemented field (ug.txt:10008–10017); because the field is a one's complement
when ORG=0, this is an effective *increment* of the same magnitude in logical space:

```
next_line_A  = A + (DUDATE_value << 10)          /* logical bit address      */
next_line_A_byte_offset = DUDATE_value << 7      /* bytes                    */
```

where `DUDATE_value` is the plain binary value of DPYCTL bits 9-2 (a single 1 at
bit position *n* → 2ⁿ). See the DUDATE table in §1.4. Equivalently:

```
bytes_per_line = (1 << (position_of_the_1_in_DUDATE + 7))
```

Sanity check against the guide's own worked example: 1024 pixels/line at 4 bits per
pixel gives a display pitch of 1024 × 4 = 4096 bits = 2¹² (ug.txt:2586–2589); the
corresponding DUDATE is `00000100` (single 1 at bit position 2), and
2^(2+10) = 4096 bits = 512 bytes. Consistent.

This also gives the number of bytes per display row, which is what the question is
ultimately after: **bytes per display row = screen-pitch-in-bits ÷ 8 = 1 <<
(DUDATE_bit_position + 7)**, i.e. the display pitch, which §9.9.1.1 confirms "is the
difference in starting memory addresses of the video data for two consecutive scan
lines" (ug.txt:9819–9827).

**LNCNT's role.** LNCNT is *not* part of the address. It is loaded from DPYSTRT's
LCSTRT at the end of every refresh cycle (ug.txt:5012–5025, ug.txt:5408–5415) and
its only function is to make a refresh cycle happen every 1, 2, 3 or 4 scan lines —
which, combined with DUDATE, lets one refresh serve several lines. With LCSTRT = 0
(LCSTRT bits = 00) and one refresh per line, the effective line advance is exactly
DUDATE per line; with LCSTRT > 0 the same DUDATE is applied once per *group* of
LCSTRT lines, so the per-line advance is DUDATE ÷ LCSTRT. (This last division is an
inference from the two mechanisms; the guide never states it in that form.)

**Is this derivable from the guide alone?** — **Yes for the LAD pin values and the
logical-address arithmetic. No for the row/column split at the VRAM pins.**

The guide is explicit that the final step is a board decision (ug.txt:5091–5098),
transcribed verbatim:

> "A board designer must select eight consecutive address lines from LAD0-LAD11 to
> connect to the multiplexed address inputs of the VRAMs. For example, by selecting
> the eight lines LAD2-LAD9, bits 14-21 of the logical address become the row
> address bits output to the RAMs, and bits 6-13 of the logical address become the
> column address bits. This means that during a screen-refresh cycle, bits 6-13 of
> DPYADR become the row address bits output to the RAMs, and bits 4-5 of DPYADR
> become the two MSBs of the tap point address."

That paragraph is an independent confirmation of the §1.3 reconstruction:
LAD2–LAD9 during row time carry DPYADR bits 6-13 = logical bits 14-21 (row
address); during column time LAD2–LAD9 carry logical bits 6-13 (column address);
and DPYADR bits 4-5, sitting on LAD8/LAD9 in column time, are the two MSBs of the
column address ("tap point address"). All three statements agree only under §9's
LAD mapping.

So an emulator has a free choice, and the choice is a property of the emulated
board, not of the GSP:

* Model the **raw LAD pins** exactly as in §1.3 and let whatever downstream logic
  picks the pins (this is what MAME's `params->rowaddr` / `params->coladdr` do).
* Or fold a specific board's wiring in. STUN Runner's actual VRAM organisation must
  be taken from MAME's driver / the schematics — **the guide cannot supply it**.

**`yoffset` is NOT IN THIS DOCUMENT.** MAME's

```c
params->yoffset = (IOREG(REG_DPYSTRT) - IOREG(REG_DPYADR)) & 3;
```

has no counterpart anywhere in ug.txt. The closest documented quantity is the
half-pitch displacement applied at the start of an even field in interlaced mode
("half the value of the DUDATE increment is added to the starting address loaded
into DPYADR", ug.txt:10167–10175), and that is a *half-line* displacement, not a
2-bit field offset. Take `yoffset` from MAME and confirm against the datasheet.

---

### 1.7 Memory organisation — logical vs physical, and display pitch

#### §3.1 Memory Addressing (ug.txt:1543–1621)

> "The TMS34010 is a bit-addressable machine with a 32-bit internal memory address.
> Each 32-bit address points to an individual bit within memory… The total memory
> capacity is four gigabits (or 512 megabytes); the TMS34010 supports external
> addressing of 128 megabytes. Bit addresses range from >0000 0000 to >FFFF FFFF."

The GSP talks to memory over a 16-bit data bus and always reads/writes a complete
16-bit word beginning on an even 16-bit boundary. The four LSBs of the 32-bit
starting address are 0s and do not appear on the local bus; they are used internally
for bit alignment. "The two MSBs of the 32-bit logical address are not output."

**Therefore: logical (bit) address = physical byte address × 8.** A byte address
`B` corresponds to logical `8·B`, and the low 4 bits of the logical address select a
bit within the 16-bit word at `B & ~1`.

#### §3.2 Memory Map (ug.txt:1631–1703)

* `>C000 0000` – `>C000 01F0`: internal I/O registers (32 words). Anything outside
  this window accesses off-chip memory.
* `>FFFF FC00` – `>FFFF FFE0`: 32 interrupt/reset/trap vectors, each a 32-bit
  address stored as two consecutive 16-bit words with the 16 LSBs at the lower
  address.
* `>C000 0200` – `>C000 1FF0`: reserved for future I/O expansion (480 words).
* `>FFFF E000` – `>FFFF FBF0`: reserved for interrupt-vector expansion.

#### §4.2.3 Display Pitch (ug.txt:2561–2590)

> "The term display pitch refers to the difference in memory addresses between two
> pixels that appear in vertically adjacent positions (one directly above the other)
> on the screen. In Figure 4-8, the pitch is calculated as P times the pixel size,
> where P is the X extent of the display memory."
>
> "The display pitch must be a power of two in order to support XY addressing of
> pixels on the screen. Linear addressing of pixels on the screen imposes fewer
> restrictions. In particular, the display pitch for linear addressing may be any
> value that is a multiple of 16; that is, the four LSBs of the address must be 0s.
> Of course, features such as automatic window checking are not available with linear
> addressing."
>
> "The pitch of a pixel array is the difference in memory addresses of two vertically
> adjacent pixels in the array. If the array occupies a rectangular area of the
> screen, the array pitch is the same as the display pitch."
>
> "During a pixel operation such as a PixBlt, the source and destination array
> pitches are specified in separate dedicated hardware registers. This facilitates the
> transfer of pixel arrays between on-screen and off-screen memory, which may have
> different pitches."

Worked example (ug.txt:2580–2589):

> "In this example, the pixel size is four bits and the X extent of the pixel display
> is 640 pixels. However, since XY addressing and windowing are to be used, the
> physical memory is organized so that there are 1024 pixels between successive scan
> lines. Thus, the X extent of physical display memory is 1024, and the display pitch
> is:
>     Display Pitch = (1024 pixels/line) x (4 bits/pixel) = 4096 (which is 2¹²)"

**The display pitch is programmed by the DUDATE field.** ug.txt:9819–9827, transcribed
verbatim:

> "The width of the display memory is referred to as the screen pitch, which is the
> difference in 32-bit memory addresses between two vertically-adjacent pixels on the
> screen. The screen pitch is also the difference in starting memory addresses of the
> video data for two consecutive scan lines. When XY addressing is used, the screen
> pitch must be a power of two to facilitate the conversion of XY addresses to memory
> addresses. The value loaded into the DUDATE field of the DPYCTL register represents
> the screen pitch, and is the amount by which the screen-refresh address is
> incremented (or decremented) following each screen-refresh cycle."

Note this is stated in units of **32-bit memory addresses** — i.e. the pitch is the
GSP's own bit-address arithmetic (see §1.4's "logical-address advance" column).

#### §4.3 / §4.3.1 XY Addressing and XY-to-Linear Conversion (ug.txt:2600–2765)

The XY address is a 32-bit GPR: **X in the 16 LSBs, right-justified to bit 0; Y in
the 16 MSBs, right-justified to bit 16.** Both are treated as 16-bit signed integers,
range (-32768,-32768) to (+32767,+32767). The clipping window is restricted to
(0,0)..(+32767,+32767), so negative X or Y always lies outside the window.

Three parameters drive the conversion:

* the logical pixel size — **PSIZE** register,
* a pitch conversion factor — **CONVSP** (source) or **CONVDP** (destination),
* an offset defining the XY origin — **OFFSET** register.

**Master formula** (ug.txt:2650–2658), transcribed verbatim:

> "Address = [(Y x display pitch) OR (X x pixel size)] + offset"

Because pitch and pixel size are powers of two, the multiply is a shift and the add
is a real add.

> "Linear addresses are formed from XY addresses by simply concatenating the binary
> numbers that represent the X and Y coordinate values… The number of 0s to the right
> of the X component depends on the number of bits per pixel, and equals log2(pixel
> size). The displacement of the Y component within the 32-bit logical address … is
> equal to log2(display pitch)."

The Y displacement is programmed by "inverting the five LSBs of the appropriate
CONVSP or CONVDP register" — i.e. a value of 4, 8, 16, 32 or 64 encodes
log2(pitch) = 2, 3, 4, 5 or 6 respectively.

**OFFSET is a virtual screen origin only.** ug.txt:2700–2712: OFFSET is the linear
memory address of the pixel at (0,0); it does **not** control which region of
display memory is output to refresh the screen — that is DPYSTRT's job. The `CVXYL`
instruction converts an XY address to a linear address for absolute addressing.

Figure 4-11's step-by-step (ug.txt:2741–2756): (b) extract the 18 LSBs; (c) rotate X
left by log2(pixel size); (d) extract the 18 MSBs; (e) rotate Y left by
16 + log2(display pitch); (f) OR; (g) add offset. Only the n LSBs of the X half may
be nonzero for a pitch of 2ⁿ; the X range after clipping is 0 … (X extent − 1),
where X extent = pitch / pixel size.

**Practical note for an emulator:** if the game uses linear addressing (PIXBLT L,*),
this conversion never runs and the pitch in DUDATE/DPYTAP must still match the
programmer's notion of a line, or the display will shear. If the game uses XY
addressing, PSIZE, CONVDP/CONVSP and OFFSET together define the mapping and DUDATE
must be consistent with `log2(pitch) - 10` as the position of DUDATE's single 1.

#### §4.4 Pixel Arrays (ug.txt:2775–2860)

An array is DX wide by DY high. Rows are separated by the array pitch (difference in
starting addresses of successive rows); `DX × pixel size ≤ pitch`. When a PIXBLT moves
pixels, "the pixels in each row are moved in sequence from left to right, and the
rows are moved in sequence from top to bottom." The starting corner may be any of
the four corners (PBH/PBV) so overlapping arrays do not lose data. The array start
may be an XY address ("XY array") or a memory address ("linear array").

#### §9.9.1.1 Screen Refresh mechanics (ug.txt:9802–9868)

> "A screen-refresh cycle loads the VRAM shift registers with a portion of the
> display memory corresponding to a scan line of the display. The internal requests
> for these cycles occur at regular intervals coinciding with the start of the
> horizontal blanking intervals defined by the video timing registers. When horizontal
> blanking ends, the contents of the shift registers are clocked out serially to
> drive the video inputs of a monitor. A screen-refresh cycle typically occurs prior
> to each active line of the display."

The 8-bit row address output during a refresh cycle selects the VRAM row loaded into
the shift register; bits transferred per cycle = number of VRAMs × shift-register
length. The guide's worked example (ug.txt:9844–9868): 64 TMS4161 (64K×1) VRAMs for a
1024×1024 display at 4 bits/pixel with 256-bit shift registers — 64 × 256 = 16,384
bits per refresh cycle = four complete scan lines.

#### §11.5 Addressing Mechanisms (ug.txt:12978–13140)

§11.5.1, transcribed (ug.txt:12979–12992):

> "The minimum number of bits of memory required to implement the display memory is
> the product of the total number of pixels (on-screen and off-screen areas combined)
> and the number of bits per pixel. The minimum number of VRAMs required to contain
> the display memory is calculated as follows:
>     Number of VRAMs = (pixels per line) x (lines per frame) x (bits per pixel)
>                       Number of bits per VRAM
> … XY addressing can be supported by making the number of pixels per line of the
> display memory a power of two, but this may require more than the minimum number of
> VRAMs needed to contain the display."

§11.5.2, transcribed (ug.txt:13000–13021):

> "During a single local memory cycle, one data word (16 bits) is transferred between
> the GSP and the selected bank of memory. … The number of devices per bank is
> therefore determined by dividing the width of the data bus by the number of data
> pins per device. The GSP data bus is 16 bits wide, and can access 16 memory data
> pins during a single cycle. This means, for example, that a bank composed of
> 64K-by-1 RAMs contains 16 RAM devices. A bank composed of 64K-by-4 RAMs contains 4
> RAM devices."
>
> "Within the display memory or system memory, some address bits are provided as the
> row and column addresses to the selected bank, while other address bits are used to
> select one of the banks."

§11.5.3 (ug.txt:13023–13140) documents the DRAM-refresh row address, which is
**not** part of the display path but is a useful model for how the GSP drives a
multiplexed address: `ROWADR` (REFCNT bits 8-15) is output on LAD0–LAD7, and the
seven LSBs are *also* output on LAD8–LAD14; LAD15 carries the F7 bus-status signal.
"Assume that LAD2-LAD9 are used as the 8-bit row address by a bank of DRAMs… The
address bits output on LAD2-LAD9 are the same eight bits output on LAD0-LAD7, but
in a different order." This duplicated-bit trick is how the GSP offers a choice of
which eight consecutive LAD lines carry a contiguous 8-bit address.
---

## 2. Complete I/O register reference (§6)

### 2.1 Naming caveat — read this first

This guide names the host-interface registers **HSTADRH, HSTADRL, HSTCTLH, HSTCTLL,
HSTDATA**. It has **no** registers named `HOSTADRH`, `HOSTADRL`, `HOSTDAT`,
`HOSTCTL`, `CONVMP`, `CONTROL2`, `CONFIG`, `PSCALE`, `CONVBL`, `CONVSL`, `CONVSHR`,
or `CONVTTR`. Those names in the request belong to other TI graphics parts (or are
MAME's own naming). Likewise **`VCOUNT2` is NOT IN THIS DOCUMENT** — it is a
TMS34020 register.

### 2.2 The register table

Source: **Table 6-1, "I/O Registers Summary", ug.txt:4426–4571**, cross-checked
against the individual register descriptions in §6.3. There are **28** on-chip
16-bit I/O locations (`ug.txt:1671`: ">C000 0000 – >C000 01F0 … 32 words"), of which
27 are named in Table 6-1 and the 28th (HSTDATA) is described in §6.3.5.

#### Host interface registers

| Reg | Address | Bit fields | Function |
|---|---|---|---|
| `HSTADRH` | `>C000 00E0` | — (whole register) | "Contains the 16 MSBs of a 32-bit Host interface address, high word." |
| `HSTADRL` | `>C000 00F0` (ug.txt:4435 prints `>C000 OODO`) | — (whole register) | "Contains the 16 LSBs of a 32-bit Host interface address, low word." Together with HSTADRH: the pointer a host uses for indirect accesses of GSP local memory. |
| `HSTCTLH` | `>C000 0100` | see below | Host interface control, high byte |
| `HSTCTLL` | see caveat | see below | Host interface control, low byte |
| `HSTDATA` | `>C000 00C0` (ug.txt:6027 prints `>C000 OOCO`) | — | Data port, one 16-bit word per indirect access |

`HSTCTLH` fields, transcribed verbatim from Table 6-1 (ug.txt:4442–4450):

```
NMI  (bit 8)   — Nonmaskable interrupt
NMIM (bit 9)   — NMI mode bit
INCW (bit 11)  — Increment pointer address on write
INCR (bit 12)  — Increment pointer address on read
LBL  (bit 13)  — Lower byte last
CF   (bit 14)  — Cache flush
HLT  (bit 15)  — Halt TMS34010 execution
Bits 0 through 7 and 10 are reserved
```

`HSTCTLL` fields, transcribed verbatim from Table 6-1 (ug.txt:4455–4461):

```
MSGIN (bits 0-2) — Input message buffer
INTIN (bit 3)    — Input interrupt bit
MSGOUT (bits 4-6)— Output message buffer
INTOUT (bit 7)   — Output interrupt bit
Bits 8 through 15 are reserved
```

**Caveat on HSTCTLL's address:** Table 6-1 prints `>C000 00F0` for HSTCTLL
(ug.txt:4453), which collides with HSTADRL as OCR'd at ug.txt:4435. The clean
per-register description heading (§6.3.4) also reads `>C000 00F0`. On real silicon
HSTCTLH and HSTCTLL are a single 16-bit location — the guide says so at
ug.txt:4591–4595:

> "The host interface registers occupy five GSP register locations, and are typically
> mapped into four consecutive 16-bit locations in the memory or I/O address space of
> the host processor. The host processor accesses the HSTCTLL and HSTCTLH registers as
> the eight LSBs and eight MSBs, respectively, of a single location (the HSTCTL
> register)."

**Take HSTCTLL's address from MAME / the datasheet** — the OCR is self-contradictory.

**STUN Runner board: host port address mapping (confirmed from Ghidra).**
The MC68010 accesses the GSP host port through a board-level address window at
`$C00000–$C0000F`. The ROM write sequences confirm this mapping:

| 68010 access | Ghidra label | 68010 address | Function |
|---|---|---|---|
| `move.w Dx, $c00002.l` | `HSTADR_HI` | `$C00002` | Host address register, high word |
| `move.w Dx, $c00004.l` | `HSTADR_LO` | `$C00004` | Host address register, low word |
| `andi.w #$e7ff, $c00008` | `HSTCTL` | `$C00008` | Host control (HLT at bit 15, INCR at bit 12, INCW at bit 11) |
| `move.w Dx, $c0000c.l` | `HSTDAT` | `$C0000C` | Host data port |

The GSP also has internal GSP-register space at `$604xxx` (confirmed from
`movep` accesses in the boot sequence: `$6040C`, `$6041C`, `$60410`, `$60402`,
`$60412` — used for GSP control, palette, and video timing).

#### Local memory interface registers

| Reg | Address | Bit fields | Function |
|---|---|---|---|
| `CONTROL` | `>C000 00B0` (Table 6-1 ug.txt:4521; the §6.5 address line at ug.txt:4702 is mis-OCR'd as `>C000 0080`) | see below | Memory control |
| `CONVDP` | `>C000 0140` | implied graphics operand | "Destination pitch conversion factor. Used during XY to linear conversion of a destination memory address." |
| `CONVSP` | `>C000 0130` | implied graphics operand | "Source pitch conversion factor. Used during XY to linear conversion of a source memory address." |
| `PMASK` | `>C000 0160` | implied graphics operand | "Plane mask register. Selectively enables/disables the various planes in the bit map of a display system in which each pixel is represented by multiple bits." |
| `PSIZE` | `>C000 0150` | implied graphics operand | "Pixel size register. Specifies the pixel size (in bits). Possible pixel sizes include 1, 2, 4, 8, and 16 bits." |
| `REFCNT` | `>C000 01F0` | `RINTVL` bits 2-7, `ROWADR` bits 8-15, bits 0-1 reserved | "Refresh count register. Generates the addresses output during DRAM refresh cycles and counts the intervals between successive DRAM refresh cycles" |

`CONTROL` fields, transcribed verbatim from Table 6-1 (ug.txt:4523–4531):

```
RM   (bit 2)     — DRAM refresh mode
RR   (bits 3-4)  — DRAM refresh rate
T    (bit 5)     — Transparency enable
W    (bits 6-7)  — Window violation detection mode
PBH  (bit 8)     — PixBlt horizontal direction
PBV  (bit 9)     — PixBlt vertical direction
PPOP (bits 10-14)— Pixel processing operation select
CD   (bit 15)    — Cache disable
Bits 0 and 1 are reserved
```

Field semantics from §6.5 (ug.txt:4700–4793):

* **RM** — 0 = RAS-only refresh cycles, 1 = CAS-before-RAS cycles
  (ug.txt:13060–13067).
* **RR** — `00` = refresh every 32 local clock periods; `01` = every 64;
  `10` = reserved code; `11` = no DRAM refreshing. At reset RM is forced to 0 and
  RR to `00`; while RESET is active no refresh cycles occur
  (ug.txt:13069–13081).
* **T** — "0 = disabled / 1 = enabled — a value of 0 resulting from a pixel operation
  on source and destination pixels is inhibited from overwriting the destination
  pixel".
* **W** — `00` = no writes inhibited, no interrupts; `01` = interrupt on write
  inside window **and** inhibit all pixel writes; `10` = interrupt on write outside
  window; `11` = inhibit writes outside window, no interrupt.
* **PBH** — 0 = increment X (left→right), 1 = decrement X (right→left); applies to
  PIXBLT XY,XY / L,XY / XY,L / L,L.
* **PBV** — same, vertical.
* **PPOP** — pixel processing operation, bits 10-14; see §3.6.
* **CD** — instruction cache disable.

#### Interrupt control registers

| Reg | Address | Bit fields | Function |
|---|---|---|---|
| `INTENB` | `>C000 0110` | `X1E` bit 1, `X2E` bit 2, `HIE` bit 9, `DIE` bit 10, `WVE` bit 11; bits 0, 3-8, 12-15 reserved | "Interrupt enable. Contains the interrupt mask used to selectively enable/disable the three internal and two external interrupts" |
| `INTPEND` | `>C000 0120` | `X1P` bit 1, `X2P` bit 2, `HIP` bit 9, `DIP` bit 10, `WVP` bit 11; bits 0, 3-8, 12-15 reserved | "Interrupt pending. Indicates which interrupt requests are currently pending" |

Note Table 6-1 OCRs `WVE` as `VVVE` and `WVP` as `WVP`/`WVP` (ug.txt:4553–4558);
the prose at ug.txt:8362 and ug.txt:8371 reads "WVE (bit 11)" and "WVP (bit 11)",
so the names are **WVE** and **WVP**.

#### Video timing and screen refresh registers

| Reg | Address | Bit fields | Function |
|---|---|---|---|
| `DPYADR` | `>C000 01E0` | `LNCNT` bits 0-1, `SRFADR` bits 2-15 | "Counts the number of scan lines output between successive screen refresh cycles and contains the source of the row and column addresses output during a screen refresh cycle" |
| `DPYCTL` | `>C000 0080` | `HSD` 0, `DUDATE` 2-9, `ORG` 10, `SRT` 11, `SRE` 12, `DXV` 13, `NIL` 14, `ENV` 15, bit 1 reserved | "Contains several parameters that control video timing signals" |
| `DPYINT` | `>C000 00A0` | 16-bit compare value | "Specifies the next scan line that will cause a display interrupt request" |
| `DPYSTRT` | `>C000 0090` | `LCSTRT` bits 0-1, `SRSTRT` bits 2-15 | "Provides control of the automatic memory-to-shift-register cycles necessary to refresh a screen" |
| `DPYTAP` | `>C000 01B0` | bits 0-13 used, 14-15 reserved | "Contains a VRAM tap point address output during shift register transfer cycles" |
| `HCOUNT` | `>C000 01C0` | 16-bit counter | "Counts the number of VCLK periods per horizontal scan line" |
| `HEBLNK` | `>C000 0010` | — | "Horizontal end blank. Designates the endpoint for horizontal blanking" |
| `HESYNC` | `>C000 0000` | — | "Horizontal end sync. Specifies the endpoint of the horizontal sync interval" |
| `HSBLNK` | `>C000 0020` | — | "Horizontal start blank. Specifies the starting point of the horizontal blanking interval" |
| `HTOTAL` | `>C000 0030` | — | "Horizontal total. Specifies the total number of VCLK periods per horizontal scan line" |
| `VCOUNT` | `>C000 01D0` | 16-bit counter | "Vertical count. Counts the horizontal scan lines in a video display" |
| `VEBLNK` | `>C000 0050` | — | "Vertidal end blank [sic]. Specifies the endpoint of the vertical blanking interval" |
| `VESYNC` | `>C000 0040` | — | "Vertical end sync. Specifies the endpoint of the vertical sync pulse" |
| `VSBLNK` | `>C000 0060` | — | "Vertical start blank. Specifies the starting point of the vertical blanking interval" |
| `VTOTAL` | `>C000 0070` | — | "Vertical total. Specifies the value of VCOUNT at which the vertical sync pulse begins" |

**HCOUNT** detail (§6, ug.txt:5486–5509): a 16-bit counter incremented on the
falling edge of the video input clock, compared against HESYNC, HEBLNK, HSBLNK and
HTOTAL. Reset to 0 when HCOUNT = HTOTAL (external sync disabled) or when external
HSYNC is driven low.

#### Reserved / unnamed I/O locations

`>C000 0200` – `>C000 1FF0` is reserved for future I/O expansion — 480 words
(ug.txt:1690–1695). Within the `>C000 0000` – `>C000 01F0` window, §6.1 notes that
"the two MSBs of an I/O register's address must both be 1" for on-chip decoding, so
`>C000 01F0` is the top named/decoded word. Individual reserved *bits* are called out
in each field table above.

#### Reset state

ug.txt:4341–4361, transcribed:

> "All I/O registers, with one exception, are cleared to 0 at reset. The exception is
> the HLT (halt) bit in the HSTCTL register, which is set depending on the value at
> the HCS input pin at the end of the reset pulse: If HCS is high at reset, the HLT
> bit is set to 1; If HCS is low at reset, the HLT bit is set to 0."

§6.2 (ug.txt:4368–4412) — a write to an I/O register may not complete before the next
instruction begins because the memory controller runs semi-autonomously; only one
field-insertion request can be queued at a time; a field insertion can take as many
as five cycles when a field of 18+ bits straddles two word boundaries.

---

## 3. Graphics operations (§7)

### 3.1 Overview (ug.txt:6624–6716)

PIXBLT and FILL manipulate 2-D pixel arrays. LINE implements the fast inner loop of
the Bresenham algorithm. DRAV (draw and advance) draws a pixel and increments the
pixel address. PIXT transfers individual pixels.

> "The PIXBLT B,XY and PIXBLT B,L instructions expand character patterns stored as
> bit maps (at one bit per pixel) into color or gray-scale characters of 1, 2, 4, 8
> or 16 bits per pixel."

There are three window-checking modes: clipping to a rectangular window, interrupt on
write outside the window, interrupt on write inside the window. `CPW` compares a point
to the window "in one machine state".

### 3.2 Implied graphics operands — the registers every graphics instruction uses

`CONTROL` (PPOP, W, T, PBH, PBV, CD), `PSIZE`, `CONVSP`, `CONVDP`, `PMASK` are all
"implied graphics operands" (flagged `t` in Table 6-1, ug.txt:4520–4535).

**Register file B** (ug.txt:3013–3069, §5.1.2) — Table 5-1, transcribed verbatim
(ug.txt:3133–3216):

| Reg | Function | Description |
|---|---|---|
| `B0` | SADDR | "Source Address. Address of the upper left corner of the source pixel array (lowest pixel address in the array). SADDR is a linear or XY address, depending on the instruction which uses it." |
| `B1` | SPTCH | "Source Pitch. Difference in linear start addresses between adjacent rows of a source pixel array." |
| `B2` | DADDR | "Destination Address. Address of the upper left corner of the destination pixel array (lowest pixel address in the array). DADDR is a linear or XY address, depending on the instruction which uses it." |
| `B3` | DPTCH | "Destination Pitch. Difference in linear start addresses between adjacent rows of a destination pixel array." |
| `B4` | OFFSET | "Offset. Linear bit address corresponding to XY-coordinate origin (X=0, Y=0)." |
| `B5` | WSTART | "Window Start Address. XY address of the upper left corner of the window (smallest X and Y coordinate values in the array)." |
| `B6` | WEND | "Window End Address. XY address of the lower right corner of the window (largest X and Y coordinate values in the array)." |
| `B7` | DYDX | "Delta Y / Delta X. The 16 LSBs of this register specify the width (X dimension) of the source array in terms of either pixels or bits, depending on the instruction. The 16 MSBs specify the height (Y dimension) of the source array. If either DY = 0 or DX = 0 then nothing is moved." |
| `B8` | COLOR0 | "Pixel value corresponding to 'color 0'. COLOR0 contains the source background color to be used during a bit-expand operation (PIXBLT B,XY or PIXBLT B,L). The pixel value should be replicated throughout the 16 LSBs of register B8 (see note below). Non replicated patterns may be entered for dithering effects. The 16 MSBs are ignored during the expand operation." |
| `B9` | COLOR1 | "Pixel value corresponding to 'color 1'. COLOR1 contains the source foreground color to be used during a bit-expand, fill, or draw-and-advance operation." (same replication note) |
| `B10`–`B14` | — | "PixBlt temporary registers. PixBlt instructions use these registers for storing temporary values and context information necessary to resume execution of a partially-completed PixBlt operation in the event of an interrupt." |
| `SP` | SP | "Stack pointer. SP contains the bit address of the top of the stack." |

Note on COLOR0/COLOR1 bus association (ug.txt:3203–3206), transcribed:

> "Each of the 16 LSBs of COLOR0 is associated with the corresponding pin of the local
> address/data bus, LAD0-LAD15. COLOR0 bit 0 is associated with bit 0 of the data bus
> (the bit transferred on LAD0), COLOR0 bit 1 is associated with bit 1 of the data bus,
> and so on."

And the forward-compatibility note (ug.txt:3215–3216):

> "To provide upward compatibility with future versions of the GSP, replicate the
> pixel value throughout all 32 bits of COLOR0 or COLOR1, as shown."

Register file A (ug.txt:2972–3010): A0–A14 plus a shared SP, 30 32-bit GPRs total
across files A and B. All register-to-register instructions (except `MOVE Rs,Rd`)
require both operands in the same file; SP may be substituted for an A-file register.

### 3.3 PIXBLT — Pixel Block Transfers (§7.2, ug.txt:6726–6808)

Two operands (a source array and a destination array), same width DX and height DY,
may have different pitches. Array parameters:

* **width** `DX` — 16 LSBs of `DYDX`
* **height** `DY` — 16 MSBs of `DYDX`
* **source start** `SADDR` (B0) — XY or linear
* **destination start** `DADDR` (B2) — XY or linear
* **source pitch** `SPTCH` (B1)
* **destination pitch** `DPTCH` (B3)
* **pixel size** `PSIZE` — 1, 2, 4, 8 or 16 bits
* default pixel processing = replace (`CONTROL.PPOP` = `00000`)

> "The default starting address is the address of the pixel in the upper left corner of
> the rectangle. (This assumes that the ORG, PBH, and PBV bits in the CONTROL register
> are all set to their default value of 0.)" (ug.txt:6746–6748)

> "During a binary expand operation, only the destination pixel size is specified; the
> source pixel size is assumed to be one bit." (ug.txt:6782–6784)

> "Window checking only usable when the DESTINATION array is an XY address." (see §3.7)

Address modes (Table 12-5, ug.txt:14013–14021): `PIXBLT B,L`, `PIXBLT B,XY`,
`PIXBLT L,L`, `PIXBLT L,XY`, `PIXBLT XY,L`, `PIXBLT XY,XY`. `B` = binary (one bit per
pixel source), `L` = linear, `XY` = XY-addressed.

`PIXBLT L,XY` / `PIXBLT XY,L` convert between linear and XY array formats.

> "PIXBLT L,XY / XY,L convert between linear and XY array formats." (ug.txt:6790–6792)

### 3.4 Colour expand (§7.2.1, ug.txt:6810–6900) — exact bit→pixel mapping

> "Two PIXBLT instructions, PIXBLT B,XY and PIXBLT B,L, provide the color-expand
> capability. The source array for either instruction is a bit map (one bit per pixel)
> stored off-screen in linear format… The pixel size for the destination array is
> governed by the value in the PSIZE register. The colors to which the 1s and 0s in
> the source array are expanded are specified in the COLOR1 and COLOR0 registers."
> (ug.txt:6816–6826)

> "the 1s in the bit map are expanded to the specified Color 1 value, and the 0s are
> expanded to the Color 0 value." (ug.txt:6848–6850)

The guide's worked example (Figure 7-1, ug.txt:6852–6880) is: 4-bit destination
pixels, one-bit source, "transforming 0-1-1-0 into yellow-red-red-yellow". The
figure's COLOR0/COLOR1 register bit layouts are OCR-garbled — the *rule* is legible
and is the one quoted above.

Two more rules, both transcribed verbatim:

> "Combining color expand with the replace-with-transparency operation yields a new
> operation that is particularly useful in drawing overlapping or kerned text. The
> color value used to replace the 0s in the source array is selected by the programmer
> as all 0s, which is the transparency code. The GSP defers the check for transparency
> until after the color-expand operation has been performed." (ug.txt:6868–6876)

> "Source pixels are expanded before being processed. Window checking can be used with
> PIXBLT B,XY." (ug.txt:6888–6890)

So, precisely: source bit 1 → destination pixel value = COLOR1 (the low
`PSIZE` bits of B9); source bit 0 → destination pixel value = COLOR0 (the low
`PSIZE` bits of B8). Because each COLOR register bit *n* feeds data-bus bit *n*, the
low `PSIZE` bits of COLOR1/COLOR0 are the pixel values that get replicated across the
corresponding lanes.

### 3.5 Starting corner selection (§7.2.2, ug.txt:6910–7000)

| Control bit | Value | Direction |
|---|---|---|
| `CONTROL.PBH` (bit 8) | 0 | pixels processed left→right (increasing X) |
| | 1 | right→left (decreasing X) |
| `CONTROL.PBV` (bit 9) | 0 | rows processed top→bottom (increasing Y) |
| | 1 | bottom→top (decreasing Y) |

> "All the pixels in one row are processed before moving to the next row."
> (ug.txt:6976)

Automatic starting-address adjustment is available for `PIXBLT L,XY` / `XY,L` /
`XY,XY`; for `PIXBLT L,L` the programmer must adjust SADDR/DADDR manually
(ug.txt:3234–3236).

### 3.6 Pixel processing / raster operations (§7.7, ug.txt:7364–7478)

> "Source and destination pixel values can be combined according to the pixel
> processing operation (or raster operation) selected. The TMS34010's pixel processing
> operations include 16 Boolean and 6 arithmetic operations."

Selected by `CONTROL.PPOP` (bits 10-14).

**Table 7-1, Boolean operations** (ug.txt:7382–7400), transcribed verbatim:

| PPOP | Operation |
|---|---|
| `00000` | Source → Destination |
| `00001` | Source AND Destination → Destination |
| `00010` | Source AND ~Destination → Destination |
| `00011` | 0s → Destination |
| `00100` | Source OR ~Destination → Destination |
| `00101` | Source XNOR Destination → Destination |
| `00110` | ~Destination → Destination |
| `00111` | Source NOR Destination → Destination |
| `01000` | Source OR Destination → Destination |
| `01001` | Destination → Destination |
| `01010` | Source XOR Destination → Destination |
| `01011` | ~Source AND Destination → Destination |
| `01100` | 1s → Destination |
| `01101` | ~Source OR Destination → Destination |
| `01110` | Source NAND Destination → Destination |
| `01111` | ~Source → Destination |

**Table 7-2, arithmetic operations** (ug.txt:7403–7412), transcribed verbatim:

| PPOP | Operation |
|---|---|
| `10000` | Source + Destination → Destination |
| `10001` | ADDS(Source, Destination) → Destination |
| `10010` | Destination - Source → Destination |
| `10011` | SUBS(Source, Destination) → Destination |
| `10100` | MAX(Source, Destination) → Destination |
| `10101` | MIN(Source, Destination) → Destination |
| `10110`–`11111` | Reserved |

Notes (ug.txt:7420–7434): codes `10000` and `10010` are two's-complement add/subtract
that wrap within the 4/8/16-bit pixel range, with the carry prevented from propagating
to adjacent pixels. `ADDS` overflow produces all 1s; `SUBS` underflow produces all
0s. `MAX`/`MIN` compare source against destination and write the greater/lesser.

### 3.7 Plane masking (§7.6, ug.txt:7176–7361)

**There are no `PMSTRT`, `PMCOUNT` or `PMSIZE` registers in this document.** Searches
for those three names across all 27,789 lines of `/tmp/ug.txt` return **zero hits**.
They are not TMS34010 registers — take them from whichever other part's datasheet they
actually belong to, or ignore them. The TMS34010's only plane-mask register is:

`PMMASK  >C000 0160`

**`PMSTRT` / `PMCOUNT` / `PMSIZE`: NOT IN THIS DOCUMENT.**

Semantics, transcribed verbatim (ug.txt:7196–7211):

> "The 1s in the mask designate pixel bits that are protected, while the 0s designate
> pixel bits that can be modified. Those pixel bits that are protected by the plane mask
> are always read as 0s during read cycles, and are protected from alteration during
> write cycles. While no single control bit enables or disables plane masking, it is
> effectively disabled by setting PMASK to all 0s; this is the default condition
> following reset."

The mask must be **replicated across all 16 bits** of PMASK — e.g. for 4-bit pixels
four copies in bits 0-3, 4-7, 8-11 and 12-15; replicate 16×, 8×, 2×, 1× for 1-, 2-,
8- and 16-bit pixels respectively. Masking affects only pixel accesses made by PIXBLT,
FILL, PIXT, DRAV and LINE.

**Order of operations** (Figure 7-6, ug.txt:7436–7444), transcribed verbatim:

> "Pixels are first read from memory and modified by the plane mask. Pixel processing is
> then performed on the modified pixel values. The plane mask is applied to the result.
> Bits which are 1s in the PMASK produce 0 bits in the result of this process. Thus,
> some processed pixels may become transparent as a result of plane masking. Next,
> transparency detection is applied to the data, and finally, a read-modify-write
> operation is invoked."

Read-modify-write is "Only necessary when plane masking or transparency is active and
the pixel size is not 16, or when the data is not word-aligned" (ug.txt:7470–7474).

Note: source pixels that originate from **registers** are not affected by the plane
mask. This applies to FILL, DRAV, LINE, `PIXT Rs,*Rd`, and `PIXT Rs,*Rd.XY`, all of
which take their source value from a register.

*Figure 7-4/7-5's per-cycle bit patterns are OCR-mangled (`11 1 0 0 1 0 0 0 1 1`,
`1 0 0 1 0 0 0 I`) and are `[OCR-UNREADABLE]`; the prose above is authoritative.*

### 3.8 Transparency (§7.5, ug.txt:7116–7166)

> "Transparency is enabled by setting the T bit in the CONTROL register to 1… a pixel
> that has a value of 0 is considered transparent, and will not overwrite a destination
> pixel. Transparency detection is applied not to the source pixel values, but to the
> pixel values resulting from plane masking and pixel processing."

Transparency applies to PIXBLT, FILL, DRAV, LINE and PIXT. It does **not** affect
non-pixel data writes.

### 3.9 PIXT — pixel transfers (§7.3, ug.txt:7057–7078)

PIXT transfers individual pixels: A/B-file register → memory, memory → A/B-file
register, or memory → memory. The address may be XY or linear — "Linear addresses
must be pixel aligned." Pixel size comes from PSIZE (restricted to 1, 2, 4, 8, 16
bits). PIXT works with window checking, Boolean/arithmetic pixel processing, plane
masking and transparency.

Six valid source/destination combinations (Table 12-6, ug.txt:14035–14048):

| Source | Destination |
|---|---|
| `Rs` | `Rd` |
| `Rs` | `*Rd.XY` |
| `*Rs` | `Rd` |
| `*Rs` | `*Rd` |
| `*Rs.XY` | `Rd` |
| `*Rs.XY` | `*Rd.XY` |

(`Rs`/`Rd` = register direct, `*Rs`/`*Rd` = register indirect/linear,
`*Rs.XY`/`*Rd.XY` = register indirect holding an XY address.)

### 3.10 FILL (§7.2, ug.txt:6780–6788)

> "The FILL instruction fills a specified destination pixel array with the pixel value
> specified in the COLOR1 register… The source pixel value used in pixel processing is
> the value in the COLOR1 register. The destination array of a FILL instruction can be
> specified in either XY or linear format."

Registers used: `COLOR1` (B9) for the pixel value; `DADDR` (B2) for the destination
start; `DYDX` (B7) for DX/DY; `DPTCH` (B3) for the destination pitch; plus `PSIZE`,
`CONVDP`, `OFFSET` and the CONTROL graphics operands.

Two forms: `FILL L` (opcode `0000 1111 1100 0000`) and `FILL XY`
(opcode `0000 1111 1110 0000`).

### 3.11 DRAV — draw and advance (§7.4, ug.txt:7081–7106)

DRAV draws a pixel to the location pointed to by a register (the pointer is an **XY
address**) and then increments X and Y independently and in parallel.

> "The value written to the destination pixel in memory is taken from the COLOR1
> register." (ug.txt:7092–7094)

DRAV works with pixel processing, window checking, plane masking and transparency.
Opcode: `DRAY Rs,Rd  1111 011S SSSR DDDD` — the guide spells the mnemonic **DRAY**
(ug.txt:14072), MAME spells it DRAV.

### 3.12 LINE (§7.4, ug.txt:7081–7106, and §7.10.4 clipping)

> "LINE implements the entire inner loop of the Bresenham algorithm."

It works with pixel processing, window checking, plane masking and transparency.
Opcode: `LINE Z  1101 1111 Z001 1010`. LINE uses B10–B14 (COUNT, INC1, INC2, PATTRN,
TEMP) as its working registers, whose previous contents are destroyed.

Line clipping is either **postclipping** (hardware compares all four window sides in
parallel, as in W=3 mode, with no per-pixel overhead) or **preclipping**
(a Cohen-Sutherland-style "outcode" technique that rejects trivially-outside lines).
Which one is used depends on the window mode: W=3 postclips LINE at draw time and
does not preclip (ug.txt:8017–8021); W=1 and W=2 abort the LINE at the offending
pixel (ug.txt:7943–7950, ug.txt:7989–7993).

### 3.13 Interrupting PIXBLTs and Fills (§7.2.3, ug.txt:7010–7050)

PIXBLT and FILL are interruptible. On an interrupt the `PBX` status bit is set to 1,
PC and ST are pushed, and RETI pops them and resumes the instruction. The resumable
state lives in SADDR, SPTCH, DADDR, DPTCH and B10–B14.

> "The PBX bit is not set to 1 when a PIXBLT or FILL instruction is aborted due to a
> window violation." (ug.txt:7044–7048)

### 3.14 Window checking (§7.10, ug.txt:7864–8154)

Window checking affects only pixel writes made by PIXBLT, FILL, LINE, DRAV and PIXT
(ug.txt:7870–7879); non-graphics data writes are unaffected. The window is an XY
rectangle from the minimum to the maximum corner — "The corner pixels are considered to
lie within the window" (ug.txt:7880–7882) — held in `WSTART` (B5) and `WEND` (B6).
**XY addressing is required:** "the destination pixel address must be an XY address"
(ug.txt:7884–7886).

**W field** (`CONTROL` bits 6-7), transcribed (ug.txt:7888–7898):

| W | Mode |
|---|---|
| `00` | disabled |
| `01` | window hit detection — interrupt on write inside window |
| `10` | window miss detection — interrupt on write outside window |
| `11` | window clipping |

The V (overflow) status bit, transcribed verbatim (ug.txt:7900–7904):

> "When window checking is enabled (modes 1, 2 or 3), an attempt to write to a pixel
> outside the window causes the V (overflow) bit in the status register to be set to 1;
> a write (or attempt to write) to a pixel inside the window sets V to 0. When window
> checking is turned off (mode 0), the V bit is unaffected during pixel writes."

**W=1, window hit detection** (ug.txt:7914–7966):

* All pixel writes are inhibited.
* If the destination array lies entirely outside the window: V = 1, no interrupt.
* If any pixel of the array lies inside: V = 0 and a window-violation interrupt is
  requested (`WVP` = 1).
* For PIXBLT/FILL the destination array is clipped to the window intersection by
  adjusting DADDR and DYDX (ug.txt:7931–7938).
* For DRAV/PIXT: outside → V = 1, no interrupt; inside → V = 0 plus interrupt.
* For LINE: V = 0 if any destination pixel is inside the window; a write inside the
  window aborts the LINE and requests the interrupt.
* The saved PC points to the instruction *after* the PIXBLT/FILL/LINE.

**W=2, window miss detection** (ug.txt:7976–7999): the write happens only if the whole
array is inside (V = 0, no interrupt); any pixel outside gives V = 1 plus an
interrupt. DRAV/PIXT draw only if inside. LINE aborts on the first outside pixel with
an interrupt.

**W=3, window clipping** (ug.txt:8001–8021): only writes inside the window are
permitted and **no interrupt is ever generated**. PIXBLT/FILL have their destination
array preclipped to the window at instruction start, so "no execution time is lost";
the PIXBLT source array is preclipped to the adjusted destination dimensions. DRAV/PIXT:
inside write OK with V = 0, outside inhibited with V = 1. LINE is postclipped at draw
time with no preclipping, and V is determined by the last pixel calculated.

**Window-violation interrupt** (ug.txt:8094–8115), transcribed:

> `WVP` is set when (W=1 and a write occurs inside the window) or (W=2 and a write
> occurs outside the window). It is taken only if `WVE` (INTENB bit 11) **and** `IE`
> (status bit 21) are both 1.
>
> "When a WV interrupt occurs, the registers that change during the LINE, PIXBLT and
> FILL instructions contain their intermediate values at the time the violation was
> detected."

---

## 4. Instruction set (§12)

### 4.1 Does the guide contain full opcode encodings? — YES

**This is a full instruction reference, not just a summary.** Two independent
confirmations:

* **Table 12-7 "Instruction Summary"** (ug.txt:14004–14409) has explicit
  `16-Bit Opcode MSB...LSB` columns.
* There are **132 per-instruction `Encoding` blocks** in the file, each an ASCII
  bit-field diagram of the form `Encoding  15 14 13 ... 0`
  (e.g. ABS at ug.txt:14518–14524), plus a per-instruction description page template
  with Syntax / Execution / Encoding / Operands / Fields / Description / Implied
  Operands / Special Graphics Topics / Interrupts / Words / Machine States /
  Status Bits / Examples (ug.txt:14417–14516).

**So opcode encodings do NOT have to be taken from MAME's
`src/devices/cpu/tms34010/34010ops.h`.** That file is still the best source for exact
per-instruction semantics and edge cases (undefined flags, masking, exceptional
behaviour), but the encodings themselves are in this document.

**Caveat:** Table 12-7's OCR column alignment is damaged in places and at least two
rows are internally inconsistent — `ANDNI IL` is printed with the *same* opcode as
`ANDI IL` (`0000 1011 100R`, ug.txt:14253 / 14261) and `CLR` is mangled
(`0101 011 DDDDR`, ug.txt:14229). Verify individual opcodes against the datasheet or
MAME before trusting them. Everything below is transcribed as printed; suspect rows
are flagged.

### 4.2 Addressing modes (§12.2, ug.txt:TOC at ug.txt:170–196)

| Mode | Syntax | Meaning |
|---|---|---|
| Register direct | `Rn` | contents of the register |
| Register indirect | `*Rn` | address from the register, linear |
| Register indirect, displacement | `*Rn(Disp)` | address = register + displacement |
| Register indirect with predecrement | `-*Rn` | decrement, then read address |
| Register indirect with postincrement | `*Rn+` | read address, then increment |
| Indirect XY | `*Rn.XY` | address from the register, XY format |
| Immediate | `IW` / `IL` | 16-bit / 32-bit immediate, an extra word |
| Absolute | `@Address` | 32-bit absolute address, extra words |
| Direct (with fields) | `*Rn,F` | field-addressed memory access |

### 4.3 Graphics and pixel instructions (Table 12-7, ug.txt:14058–14111)

Syntax / opcode / words / machine-states, transcribed as printed:

| Instruction | Opcode | Words | States | Description |
|---|---|---|---|---|
| `ADDXY Rs,Rd` | `1110 000S SSSR DDDD` | 1 | 4 | Adds a 16-bit constant in the 16 MSBs of `Rs` to the Y component of the XY address in `Rd`. |
| `CMPXY Rd,Rd` | `1110 010S SSSR DDDD` | 3 | 6 | Compares the X components of two XY addresses and sets status flags. |
| `CPW Rs,Rd` | `1110 011S SSSR DDDD` | 1 | 4 | "Compares point to window in one machine state" — tests whether the XY point in `Rs` lies inside the window defined by WSTART/WEND. |
| `CVXYL Rs,Rd` | `1110 1 00S SSSR DDDD` | 3 | 6 | Converts the XY address in `Rs` to a linear (absolute) address in `Rd`, using PSIZE/CONV*/OFFSET. |
| `DRAY Rs,Rd` | `1111 011S SSSR DDDD` | 1 | 4 | Draw and advance: writes COLOR1 to the XY location in `Rd`, then increments X and Y in parallel. |
| `FILL L` | `0000 1111 1100 0000` | — | — | Fills a linear destination array with COLOR1. |
| `FILL XY` | `0000 1111 1110 0000` | — | — | Fills an XY-addressed destination array with COLOR1. |
| `MOVX Rs,Rd` | `1110 110S SSSR DDDD` | 1 | 4 | Moves the X component (16 LSBs) of `Rs` into the XY register `Rd`. |
| `MOVY Rs,Rd` | `1110 111S SSSR DDDD` | 1 | 4 | Moves the Y component (16 MSBs) of `Rs` into the XY register `Rd`. |
| `PIXBLT B,L` | `0000 1111 1000 0000` | — | — | Colour expand, binary source → linear destination. |
| `PIXBLT B,XY` | `0000 1111 1010 0000` | — | — | Colour expand, binary source → XY destination. |
| `PIXBLT L,L` | `0000 1111 0000 0000` | — | — | Linear source → linear destination. |
| `PIXBLT L,XY` | `0000 1111 0010 0000` | — | — | Linear source → XY destination. |
| `PIXBLT XY,L` | `0000 1111 0100 0000` | — | — | XY source → linear destination. |
| `PIXBLT XY,XY` | `0000 1111 0110 0000` | — | — | XY source → XY destination. |
| `PIXT Rs,*Rd` | `1111 100S SSSR DDDD` | — | — | Writes register `Rs` to the linear address in `Rd`. |
| `PIXT Rs,*Rd.XY` | `1111 000S SSSR DDDD` | — | — | Writes register `Rs` to the XY address in `Rd`. |
| `PIXT *Rs,Rd` | `1111 101S SSSR DDDD` | — | — | Reads the linear pixel at `*Rs` into `Rd`. |
| `PIXT *Rs,*Rd` | `1111 110S SSSR DDDD` | — | — | Copies one pixel, linear → linear. |
| `PIXT *Rs.XY,Rd` | `1111 001S SSSR DDDD` | — | — | Reads the XY pixel at `*Rs.XY` into `Rd`. |
| `PIXT *Rs.XY,*Rd.XY` | `1111 010S SSSR DDDD` | — | — | Copies one pixel, XY → XY. |
| `SUBXY Rs,Rd` | `1110 001S SSSR DDDD` | 1 | 4 | Subtracts a 16-bit constant in the 16 MSBs of `Rs` from the Y component of the XY address in `Rd`. |
| `LINE Z` | `1101 1111 Z001 1010` | — | — | Bresenham line-draw inner loop; `Z` selects the initial condition. |

PIXBLT timing is in §13.4 (ug.txt:25375) and §13.5 for the expand forms
(ug.txt:25926); LINE timing is §13.6 (ug.txt:26404).

### 4.4 MOVE and MOVB (Table 12-7, ug.txt:14121–14195)

`M` in the MOVE opcode is the **field-extend** bit (0 = zero-extend, 1 = sign-extend).
The `F` field of a field-mode MOVE selects which of the two field descriptors in the
status register (FS/FE) applies.

**MOVE, register/field forms:**

| Instruction | Opcode | Description |
|---|---|---|
| `MOVE Rs,Rd` | `0100 11MS SSSR DDDD` | register-to-register move; S = source register select, M = field extend |
| `MOVE Rs,*Rd,F` | `1000 00FS SSSR DDDD` | extract field F from the linear address in `Rd` into `Rs` |
| `MOVE *Rs,Rd,F` | `1000 01FS SSSR DDDD` | insert `Rs` into field F of `Rd` |
| `MOVE *Rs+,Rd,F` | `1001 01FS SSSR DDDD` | as above, post-incrementing `Rs` |
| `MOVE Rs,*Rd-E,F` | `1001 00FS SSSR DDDD` | post-increment with `-E` displacement form |
| `MOVE -*Rs,Rd,F` | `1010 01FS SSSR DDDD` | as above, with predecrement of `Rs` |
| `MOVE Rs,-*Rd,F` | `1010 00FS SSSR DDDD` | predecrement the destination address |
| `MOVE *Rs,*Rd,F` | `1000 10FS SSSR DDDD` | field-to-field move |
| `MOVE -*Rs,-*Rd,F` | `1010 10FS SSSR DDDD` | field-to-field move, both predecremented |
| `MOVE *Rs+,*Rd+,F` | `1001 10FS SSSR DDDD` | field-to-field move, both post-incremented |
| `MOVE Rs,*Rd(Disp),F` | `1011 00FS SSSR DDDD` | 2 words; displacement on the destination address |
| `MOVE *Rs(Disp),Rd,F` | `1011 01FS SSSR DDDD` | 2 words; displacement on the source address |
| `MOVE *Rs(Disp),*Rd+,F` | `1101 00FS SSSR DDDD` | 2 words; displacement source, post-increment destination |
| `MOVE *Rs(Disp),*Rd(Disp),F` | `1011 10FS SSSR DDDD` | 3 words; displacements on both addresses |
| `MOVE Rs,@DAddress,F` | `0000 01F1 100R DDDD` | 3 words; absolute destination address |
| `MOVE @SAddress,Rd,F` | `0000 01F1 101R DDDD` | 3 words; absolute source address |
| `MOVE @SAddress,*Rd+,F` | `1101 01F0 000R DDDD` | 3 words; absolute source, post-increment destination |
| `MOVE @SAddress,@DAddress,F` | `0000 01F1 1100 DDDD` | 5 words; absolute source and destination |

**MOVB (byte moves):**

| Instruction | Opcode | Description |
|---|---|---|
| `MOVB Rs,*Rd` | `1000 110S` | byte from register to linear memory |
| `MOVB *Rs,Rd` | `1000 111S` | byte from linear memory to register |
| `MOVB *Rs,*Rd` | `1001 110S` | byte move, memory to memory |
| `MOVB *Rs,*Rd(Disp)` | `1010 110S` | 2 words; destination displacement |
| `MOVB *Rs(Disp),Rd` | `1010 111S` | 2 words; source displacement |
| `MOVB *Rs(Disp),*Rd(Disp)` | `1011 11 0S` | 3 words; both displacements |
| `MOVB Rs,@DAddress` | `0000 0101 111R SSSS` | 3 words; absolute destination |
| `MOVB @SAddress,Rd` | `0000 0111 111R DDDD` | 3 words; absolute source |
| `MOVB @SAddress,@DAddress` | `0000 0011 0100 0000` | 5 words; absolute source and destination |

### 4.5 General instructions (Table 12-7, ug.txt:14197–14350)

Opcode / words / machine-states as printed:

| Instruction | Opcode | Words | States | Description |
|---|---|---|---|---|
| `ABS Rs` | `0000 0011 100R` | | | Two's-complement absolute value. |
| `ADD Rs,Rd` | `0100 000S` | | | 32-bit add. |
| `ADDC Rs,Rd` | `0100 001S` | | | 32-bit add with carry-in from the C status bit. |
| `ADDI IW,Rd` | `0000 1011 000R` | 2 | | Add 16-bit immediate. |
| `ADDI IL,Rd` | `0000 1011 001R` | 3 | | Add 32-bit immediate. |
| `ADDK K,Rd` | `0001 00KK KKKR` | | | Add the 8-bit signed constant K. |
| `AND Rs,Rd` | `0101 000S` | | | Bitwise AND. |
| `ANDI IL,Rd` | `0000 1011 100R` | | | AND with a 32-bit immediate. **[opcode collides with ANDNI as printed — suspect]** |
| `ANDN Rs,Rd` | `0101 001S` | | | `Rs AND ~Rd`. |
| `ANDNI IL,Rd` | `0000 1011 100R` | | | AND with the complement of a 32-bit immediate. **[printed identically to ANDI — suspect]** |
| `BTST K,Rd` | `0001 11KK KKKR` | | | Bit test against constant bit K. |
| `BTST Rs,Rd` | `0100 101S` | | | Bit test against the low bit of `Rs`. |
| `CLR Rd` | `0101 011 DDDDR` | | | Clear. **[mangled as printed — suspect]** |
| `CLRC` | `0000 0011 0010 0000` | | | Clear the carry status bit. |
| `CMP Rd` | `0000 1011 010R` | | | Compare. **[collides with CMPI IW as printed — suspect]** |
| `CMPI IW,Rd` | `0000 1011 010R` | | | Compare against a 16-bit immediate. **[as printed]** |
| `CMPI IL,Rd` | `0000 1011 011R` | | | Compare against a 32-bit immediate. |
| `DEC Rd` | `0001 0100 001R` | | | Decrement by 1. |
| `DINT` | `0000 0011 0110 0000` | 3 | 6 | Disable interrupts (clear IE). |
| `DIVS Rs,Rd` | `0101 100S` | | 40, 43 | Signed divide. |
| `DIVU Rs,Rd` | `0101 101S` | | 37, 40 | Unsigned divide. |
| `EINT` | `0000 1101 0110 0000` | 3 | 6 | Enable interrupts (set IE). |
| `EXGF Rd,F` | `1101 01F1 000R` | | | Exchange GPR with a stack entry; F selects field 0 or 1. |
| `LMO Rs,Rd` | `0110 101S` | | | Long multiply, one operand. |
| `MMFM Rs,List` | `0000 1001 101R` | 2 | | Move multiple memory words to registers. |
| `MMTM Rs,List` | `0000 1001 100R` | 2 | | Move multiple registers to memory. |
| `MODS Rs,Rd` | `0110 110S` | | 40, 43 | Signed modulus. |
| `MODU Rs,Rd` | `0110 111S` | | 35, 38 | Unsigned modulus. |
| `MOVI IW,Rd` | `0000 1001 110R` | 2 | | Move a 16-bit immediate. |
| `MOVI IL,Rd` | `0000 1001 111R` | 3 | | Move a 32-bit immediate. |
| `MOVK K,Rd` | `0001 10KK KKKR` | | | Move an 8-bit constant with sign extension. |
| `MPYS Rs,Rd` | `0101 110S` | | 20, 23 | Signed multiply. |
| `MPYU Rs,Rd` | `0101 111S` | | 21, 24 | Unsigned multiply. |
| `NEG Rd` | `0000 0011 101R` | | | Two's-complement negate. |
| `NEGB Rd` | `0000 0011 110R` | | | Negate a byte. |
| `NOP` | `0000 0011 0000 0000` | | | No operation. |
| `NOT Rs,Rd` | `0000 0011 111R` | | | Bitwise complement. |
| `OR Rs,Rd` | `0101 010S` | | | Bitwise OR. |
| `ORI IL,Rd` | `0000 1011 101R` | | | OR with a 32-bit immediate. |
| `RL K,Rd` | `0011 00KK KKKR` | | | Rotate left by constant K. |
| `RL Rs,Rd` | `0110 10SS SSSR` | | | Rotate left by the low 5 bits of `Rs`. |
| `SETC` | `0000 1101 1110 0000` | | | Set the carry status bit. |
| `SETF FS,FE,F` | `0000 01F1 01 FS SSSS` | | | Set the field-size and field-extend fields of the status register. |
| `SEXT Rd,F` | `0000 01F1 000R` | 3 | 6 | Sign-extend a field of `Rd` in place. |
| `SLA K,Rd` | `0010 00KK KKKR` | | | Shift left arithmetic by constant K. |
| `SLA Rs,Rd` | `0110 000S` | | | Shift left arithmetic by `Rs`. |
| `SLL K,Rd` | `0010 01KK KKKR` | | | Shift left logical by constant K. |
| `SLL Rs,Rd` | `0110 001S` | | | Shift left logical by `Rs`. |
| `SRA K,Rd` | `0010 10KK KKKR` | | | Shift right arithmetic by constant K. |
| `SRA Rs,Rd` | `0110 010S` | | | Shift right arithmetic by `Rs`. |
| `SRL K,Rd` | `0010 11KK KKKR` | | | Shift right logical by constant K. |
| `SRL Rs,Rd` | `0110 011S` | | | Shift right logical by `Rs`. |
| `SUB Rs,Rd` | `0100 010S` | | | 32-bit subtract. |
| `SUBB Rs,Rd` | `0100 011S` | | | Subtract with borrow. |
| `SUBI IW,Rd` | `0000 1011 111R` | 2 | | Subtract a 16-bit immediate. |
| `SUBI IL,Rd` | `0000 1101 111R` | 3 | | Subtract a 32-bit immediate. |
| `SUBK K,Rd` | `0001 01KK KKKR` | | | Subtract the 8-bit signed constant K. |
| `XOR Rs,Rd` | `0101 011S` | | | Bitwise exclusive OR. |
| `XORI IL,Rd` | `0000 1011 110R` | | | XOR with a 32-bit immediate. |
| `Z_ Rd,F` | `0000 01F1 001R` | | | Zero (clear) field F of `Rd`. |

### 4.6 Program-control instructions (Table 12-7, ug.txt:14360–14409)

| Instruction | Opcode | Words | States | Description |
|---|---|---|---|---|
| `CALL Rs` | `0000 1001 001R DDDD` | | | Call the address in `Rs`. |
| `CALLA Address` | `0000 1101 0101 1111` | 3 | | Call an absolute 32-bit address. |
| `CALLR Address` | `0000 1101 0011 1111` | 2 | | Call with a 16-bit relative displacement. |
| `DSJ Rd,Address` | `0000 1101 100R DDDD` | 2 | | Decrement-and-skip-if-zero with a 16-bit displacement. |
| `DSJEQ Rd,Address` | `0000 1101 101R` | 2 | | Decrement-and-skip-if-equal. |
| `DSJNE Rd,Address` | `0000 1101 11 0R` | 2 | | Decrement-and-skip-if-not-equal. |
| `DSJS K,Address` | `0011 1 DKK KKKR DDDD` | 1 | | Decrement-and-skip-if-zero on a constant. |
| `EMU` | `0000 0001 0000 0000` | | 6, 9 | Emulation-mode trap / breakpoint. |
| `EXGPC Rd` | `0000 0001 001R` | | | Exchange the GPR with PC. |
| `GETPC Rd` | `0000 0001 010R` | | | PC → `Rd`. |
| `GETST Rd` | `0000 0001 100R` | | | Status register → `Rd`. |
| `JAcc Address` | `1100 code 1000 0000` | 3 | | Jump on condition `code`. |
| `JRcc Address` | `1100 code 0000 0000` | 2 | 3, 6 | Relative jump on condition (2-word form). |
| `JRcc Address` | `1100 code xxxx xxxx` | 1 | 2, 5 | Short relative jump on condition (1-word form). |
| `JUMP Rs` | `0000 0001 011R` | | | Jump to the address in `Rs`. |
| `POPST` | `0000 0001 1100 0000` | | | Pop the status register from the stack. |
| `PUSHST` | `0000 0001 1110 0000` | | | Push the status register. |
| `PUTST Rs` | `0000 0001 101R` | | | Push `Rs` to the stack as the new status register. |
| `RETI` | `0000 1001 0100 0000` | | | Return from interrupt — pops both the ST and PC. |
| `RETS [N]` | `0000 1 001 011N NNNN` | | | Return from subroutine, optionally popping N registers. |
| `TRAP N` | `0000 1001 000N NNNN` | | | Software trap to vector N. |

### 4.7 Status register (§5.2, ug.txt:3726–3787)

| Bits | Name | Meaning |
|---|---|---|
| 0-4 | FS0 | Field Size 0 |
| 5 | FE0 | Field Extend 0 — 0 = zero extension, 1 = sign extension |
| 6-10 | FS1 | Field Size 1 |
| 11 | FE1 | Field Extend 1 |
| 12-20 | Reserved | |
| 21 | IE | Interrupt Enable — 0 = all maskable interrupts disabled, 1 = enabled |
| 22-24 | Reserved | |
| 25 | PBX | PixBlt Executing — 0 = interrupt occurred at a PIXBLT/FILL boundary, 1 = in the middle of a PIXBLT or FILL |
| 26-27 | Reserved | |
| 28 | V | Overflow (also the window-checking status bit, §3.14) |
| 29 | Z | Zero |
| 30 | C | Carry |
| 31 | N | Negative |

> "When read, a reserved bit returns the last value written to it. At reset, all
> reserved bits are forced to 0s." (ug.txt:3770–3774)

Table 5-3, the field-size decoding (ug.txt:3792 onward), uses 5-bit encoded values
(`00001` … `11110`); the OCR is too damaged to transcribe the full table. The MOV/MOVB
timing charts in §13.2 (ug.txt:24680–24760) refer to field alignments "Case A"
through "Case G", which is the operational part of the same mechanism — those case
definitions *are* readable (ug.txt:24750–24776) if the field-size table is needed.

---

## 5. Host interface (§10) and interrupts (§8)

### 5.1 Host interface overview

§10 opening (ug.txt:10326–10328), transcribed:

> "A host processor may read from or write to TMS34010 local memory indirectly via
> an autoincrementing address register and data port… To allow block moves initiated
> by a host to take place more efficiently, the host may suspend TMS34010 program
> execution to eliminate contention with the TMS34010 for local memory. DRAM-refresh
> and screen-refresh cycles continue to occur while the TMS34010 is halted."

The guide says the host interface registers **occupy five GSP register locations** but
are presented to the host as **four consecutive 16-bit locations** (ug.txt:4591–4595):

> "The host interface registers occupy five GSP register locations, and are typically
> mapped into four consecutive 16-bit locations in the memory or I/O address space of
> the host processor. The host processor accesses the HSTCTLL and HSTCTLH registers as
> the eight LSBs and eight MSBs, respectively, of a single location (the HSTCTL
> register)."

So the five GSP-visible words are HSTADRH, HSTADRL, HSTCTLH, HSTCTLL, HSTDATA — of which
HSTCTLH+HSTCTLL merge into one host-visible 16-bit word, giving 4 host-visible
locations. **This is where the names HOSTADRH / HOSTADRL / HOSTDAT / HOSTCTL in the
parent's request come from: they are the same registers under different names.**

### 5.2 The five registers, transcribed

#### HSTADRH — host address, high word, `>C000 00E0` (ug.txt:5604–5647)

> "Contains the 16 MSBs of a 32-bit Host interface address, high word."

Host-accessible and GSP-accessible. Not auto-incremented by the register itself; the
increment applies to the **32-bit** pointer formed with HSTADRL.

#### HSTADRL — host address, low word, `>C000 00F0` (ug.txt:5653–5690)

> "Contains the 16 LSBs of a 32-bit Host interface address, low word."

Address OCR note: Table 6-1 prints this address as `>C000 OODO` (ug.txt:4513); the
register's own section gives `>C000 OODO` too — this is an OCR rendering of
`>C000 00F0`, consistent with §5.2's use of HSTADRL/HSTADRH at 00E0/00F0 and with
Table 10-1's `HFS1 HFS0 = 00 → HSTADRL` (ug.txt:10409–10417).

Two behaviours the emulator must model:

1. **The low four bits are forced to zero.** (ug.txt:5690 region) the 32-bit pointer is
   word-aligned; only 28 bits are usable. This is the same "four LSBs of the 32-bit
   starting address are 0s" rule as general local memory (§3.1, ug.txt:1543–1621).
2. **Wrap-around.** The pointer is 32 bits; ug.txt states: "If the address pointer is
   incremented past the largest word address in memory, it will wrap around to the
   lowest address (all 0s)."
3. **Write ordering is controlled by LBL** (HSTCTLH bit 13). With `LBL = 0` the host
   should write HSTADRH **last**; with `LBL = 1` write HSTADRL last — so that the
   address becomes valid only after the second write and no partial address is ever
   latched.
4. **A GSP write to HSTADRx does NOT trigger a memory cycle.** The GSP can load the
   pointer, but the transfer only happens when HSTDATA is accessed. In the INCR=0 case
   the host must read HSTDATA twice (the second read fetches the next word); with
   INCR=1 or "address unknown", the host must read HSTDATA first and then write the
   address register to fetch the following word.

#### HSTCTLH — host interface control, high byte, `>C000 0100` (ug.txt:5697–5763)

Bit field table, transcribed verbatim:

| Bits | Name | Description |
|---|---|---|
| 0-7 | Reserved | Not used |
| 8 | NMI | Nonmaskable interrupt |
| 9 | NMIM | Mode bit for NMI |
| 10 | Reserved | Not used |
| 11 | INCW | Increment pointer address on write |
| 12 | INCR | Increment pointer address on read |
| 13 | LBL | Lower byte last |
| 14 | CF | Cache flush |
| 15 | HLT | Halt GSP processing |

- **NMI (bit 8)** — setting this bit from the host requests a nonmaskable interrupt of
  the GSP. It is **auto-cleared to 0 when the interrupt is taken** (ug.txt:5741–5742):
  the guide notes the NMI bit clears itself once the interrupt is accepted, so the host
  does not have to clear it.
- **NMIM (bit 9)** — controls what is pushed on NMI entry: `NMIM = 0` pushes the PC and
  ST (a full interrupt frame, resumable by RETI); `NMIM = 1` pushes **nothing** and the
  GSP simply resumes at the interrupted instruction (ug.txt:8405–8455 covers both).
- **INCR (bit 12)** — transcribed (ug.txt:5871–5882):

  > "When INCR=1, the 32-bit address contained in registers HSTADRL and HSTADRH is
  > incremented by 16 before being used for the next read of the GSP memory…
  > Regardless of the value of the INCR bit, each time HSTDATA is read by the host, a
  > new word is automatically read into HSTDATA from the GSP's memory."

  **The increment is by 16 — one 16-bit word — not by one byte.** This is because the
  GSP's local bus always transfers whole 16-bit words on even boundaries (§3.1).
- **INCW (bit 11)** — transcribed (ug.txt:5884–5901): with INCW = 1 the 32-bit pointer
  is incremented by 16 *after* being used as the write address; regardless of INCW,
  **each** write to HSTDATA starts a new transfer cycle (that is, the data register
  write is self-clearing — the host does not have to re-arm it).
- **LBL (bit 13)** — byte-order latch control, see HSTADRH above.
- **CF (bit 14)** — cache flush. Set CF to flush the GSP's on-chip instruction cache
  (ug.txt:4089, 4100); documented use is together with HLT when the host is downloading
  new program code.
- **HLT (bit 15)** — transcribed verbatim (ug.txt:5903–5919):

  > "When the HLT bit is set to 1, the GSP suspends instruction processing at the next
  > instruction boundary. Once halted, the GSP does not respond to interrupt requests
  > (including NMI). Local memory refresh and video timing functions continue
  > unaffected while the GSP is halted. When HLT is again set to 0, the GSP continues
  > execution."

  **Screen refresh keeps running while the GSP is halted** — this is the key fact for a
  video emulator: halting the GSP freezes the game logic but *not* the display.
  Post-reset state of HLT depends on the HCS pin (§6.1, ug.txt:4341–4361): HCS high at
  reset → HLT = 1 (GSP starts halted); HCS low at reset → HLT = 0 (GSP runs).
  "Both the host processor and GSP can write to the HLT bit; this means the GSP can halt
  itself by loading a 1 into HLT."

#### HSTCTLL — host interface control/status, low byte (ug.txt:5926–6020)

Table 6-1 gives its address as `>C000 00F0`, which collides with HSTADRL as OCR'd —
**this address is [OCR-UNREADABLE]; take it from MAME.** The guide's own Table 10-1
register-select encoding (ug.txt:10409–10417) treats HSTCTL as a single host-visible
word, so the two GSP-visible halves are at consecutive GSP addresses. The bit fields
transcribe cleanly:

| Bits | Name | Description |
|---|---|---|
| 0-2 | MSGIN | Input message buffer |
| 3 | INTIN | Input interrupt bit |
| 4-6 | MSGOUT | Output message buffer |
| 7 | INTOUT | Output interrupt bit |
| 8-15 | Reserved | Not used |

Write-permission rules, transcribed (ug.txt:5954–5961):

- **MSGOUT** is writable by the GSP only (the host reads it).
- **MSGIN** is writable by the host only (the GSP reads it).
- The host may write a **1** to **INTIN**, but writing a 0 has no effect.
- The GSP may write a **0** to **INTIN**, but writing a 1 has no effect.
- The GSP may write a **1** to **INTOUT**, but writing a 0 has no effect.
- The host may write a **0** to **INTOUT**, but writing a 1 has no effect.

> "Internal arbitration logic permits the GSP and host processor to access HSTCTLL at
> the same time without hazard."

MSGIN/MSGOUT are 3-bit mailboxes: the host writes a code into MSGIN and raises INTIN to
interrupt the GSP; the GSP replies by writing a code to MSGOUT and raising INTOUT to
interrupt the host. **The exact handshake protocol above the register semantics is NOT
IN THIS DOCUMENT** at the register level — read the §10 prose for the intended usage
pattern, and MAME for the flag plumbing.

#### HSTDATA — data port, `>C000 00C0` (ug.txt:6023–6045)

> "HSTDATA can be accessed by the GSP at address `>C000 00C0`."

(Table 6-1 prints this address as `>c000 0000`, which is an OCR error — the guide's own
prose for the register says `>C000 00C0`. Note that HSTDATA at 00C0 sits *below*
HSTADRH at 00E0, so the five host-interface words are not contiguous in the GSP's own
map; the guide explains the 00E0/00F0/0100 + HSTCTLL arrangement only obliquely and the
HSTCTLL address in Table 6-1 is garbled (see HSTCTLL above). **Use 00C0 — it is what the
prose says — and take HSTCTLL's address from MAME.**)

Transcribed behaviour:

- Buffers one 16-bit word per indirect access.
- The host may perform the transfer as one 16-bit access or as two 8-bit accesses
  (which is what HFS0/HFS1 and the LBL/byte-latching logic exist for).
- **When the GSP itself reads or writes HSTDATA, NO automatic local-memory cycle
  occurs.** The transfer only happens when the *host* accesses HSTDATA. This is the
  asymmetry that trips up ports: writing HSTDATA from GSP code just moves a value in
  and out of a buffer.

### 5.3 Bus pins and cycle control (§10.1, §10.3, ug.txt:10354–10493)

| Signal | Direction | Function |
|---|---|---|
| HD0–HD15 | bidir | Host data bus (D0 = LSB) |
| HCS | in | Chip select; active low |
| HFS0, HFS1 | out | Field select — see Table 10-1 |
| HREAD | out | Host is reading from the GSP |
| HWRITE | out | Host is writing to the GSP |
| HLDS | out | Lower data strobe (byte enable for the low byte) |
| HUDS | out | Upper data strobe (byte enable for the high byte) |
| HRDY | in | Host ready / GSP not-ready (GSP stalls the host here) |
| HINT | out | Host interrupt request from the GSP |

Table 10-1, register selection by HFS1:HFS0 (ug.txt:10409–10417, transcribed):

| HFS1 | HFS0 | Selected register |
|---|---|---|
| 0 | 0 | HSTADRL |
| 0 | 1 | HSTADRH |
| 1 | 0 | HSTDATA |
| 1 | 1 | HSTCTL |

Read/write cycles are qualified by HCS, HWRITE, HREAD, HUDS, HLDS (ug.txt:10476–10493).
A not-ready handshake on HRDY is how the GSP stalls a host that starts an indirect
access while an external bus master holds the bus (see §11.3, quoted in §6.4 below).

### 5.4 Interrupts (§8)

#### Sources and priorities

Table 8-1 (ug.txt:8278–8296) — priority 1 is highest; "I" = internal/maskable-by-HLT,
"E" = external:

| Priority | Interrupt | Type |
|---|---|---|
| 1 | Reset | I |
| 2 | NMI | I |
| 3 | HI (horizontal blanking) | I |
| 4 | DI (display interrupt) | I |
| 5 | WV (window violation) | I |
| 6 | INT1 | E |
| 7 | INT2 | E |
| 8 | ILLOP (illegal opcode) | I |

Transcribed (ug.txt:8296 region): "If two interrupts are requested at the same time,
the highest priority interrupt is serviced first (assuming it is enabled). The reset and
nonmaskable interrupt cannot be disabled."

Internal ordering, §8.3 (ug.txt:8405–8455): **NMI > HI > DI > WV** among the internal
interrupts, and internal interrupts are serviced before external ones, the sole
exception being ILLOP (illegal opcode), which is not maskable by INTENB.

#### Enable and pending registers

INTENB, `>C000 0110` (ug.txt:8348–8372) — bits:

| Bit | Name | Description |
|---|---|---|
| 0 | Reserved | Not used |
| 1 | X1E | INT1 enable |
| 2 | X2E | INT2 enable |
| 3-8 | Reserved | Not used |
| 9 | HIE | Horizontal interrupt enable |
| 10 | DIE | Display interrupt enable |
| 11 | WVE | Window violation interrupt enable |
| 12-15 | Reserved | Not used |

INTPEND, `>C000 0120` (ug.txt:8348–8372) — bits:

| Bit | Name | Description |
|---|---|---|
| 0 | Reserved | Not used |
| 1 | X1P | INT1 pending |
| 2 | X2P | INT2 pending |
| 3-8 | Reserved | Not used |
| 9 | HIP | Horizontal interrupt pending |
| 10 | DIP | Display interrupt pending |
| 11 | WVP | Window violation interrupt pending |
| 12-15 | Reserved | Not used |

Gate, transcribed (ug.txt:8348–8372):

> "An interrupt is enabled when the status IE (global interrupt enable) bit and the
> appropriate bit in the INTENB register are both set to 1."

IE is **status register bit 21** (§5.2, ug.txt:3726–3787) — it is not an I/O register
bit. `DINT` clears it, `EINT` sets it.

#### External interrupts INT1/INT2 — §8.2 (ug.txt:8373–8395)

- LINT1 and LINT2 are **level-sensitive, active low**, and are **not** encoded — they
  are synchronised internally.
- A request must remain asserted until it is serviced.
- If the line is still active after the ISR returns, the interrupt is taken again.
- If both are asserted, INT1 is serviced first.

#### Display interrupt source — §8.3 (ug.txt:8405–8455), transcribed

> "At the start of each horizontal blanking period, the VCOUNT register is compared to
> the DPYINT register. When the vertical count value in VCOUNT = DPYINT, a display
> interrupt request is generated."

Corroborated by §6 DPYINT (ug.txt:5345–5381) and §6 DPYCTL ENV (ug.txt:5326–5339):
DIP is set when VCOUNT = DPYINT, and the display interrupt is inhibited when ENV = 0.

#### NMI — §8.3 (ug.txt:8405–8455)

- NMI is requested by writing the **NMI bit of HSTCTLH**.
- It cannot be disabled (not by IE, not by INTENB).
- It is delayed only for completion of the current instruction, or to the next
  interruptible point inside a PIXBLT/FILL.
- **NMIM = 0**: the PC and ST are pushed; a RETI at the end of the handler restores
  them. **NMIM = 1**: nothing is saved — execution resumes at the interrupted
  instruction, so the handler must be a leaf routine that does not return normally.
- "Following execution of a nonmaskable interrupt, screen-refresh and DRAM-refresh
  functions continue unaffected."

#### Interrupt acceptance and the frame pushed — §8.4 (ug.txt:8465–8514)

Interrupts are accepted at the end of an instruction, or at the next interruptible
point inside a PIXBLT/FILL. The documented sequence:

1. push PC;
2. push ST (a PIXBLT/FILL interrupted mid-array sets the **PBX** status bit before the
   push, so the return path can tell "interrupted at an instruction boundary" from
   "interrupted mid-array" — see §3.13);
3. modify ST;
4. fetch the vector from external memory into PC;
5. begin executing the handler.

On ISR entry (transcribed):

> "All interrupts are disabled (except NMI and reset); Field 0 is 16 bits long and is
> zero extended; Field 1 is 32 bits long and is zero extended."

The handler must end with **RETI**, which "pops both the ST and PC". This is the ST/PC
double-push convention that makes NMI-with-NMIM=0 resumable and is why the ST must
capture PBX.

#### Vector addresses

External vectors, Table 8-2 (ug.txt:8390–8395) — transcribed cleanly:

| Interrupt | Vector |
|---|---|
| INT1 | `>FFFF FFC0` |
| INT2 | `>FFFF FFA0` |

Internal vectors, Table 8-3 (ug.txt:8414–8431) — **the low two hex digits are
[OCR-UNREADABLE]**:

| Interrupt | Vector as OCR'd | Confidence |
|---|---|---|
| NMI | `>FFFF FEED` | low byte garbled — not trustworthy |
| HI | `>FFFF FECO` | low byte garbled — not trustworthy |
| DI | `>FFFF FEA0` | plausible |
| WV | `>FFFF FE80` | plausible |
| ILLOP | `>FFFF FC20` | consistent with §3.2 |
| Reset | (see §3.2, `>FFFF FFF0` region) | — |

**Do not use the OCR'd NMI/HI/DI/WV vector bytes.** Figure 8-1, the vector map
(ug.txt:8300–8338), is too mangled to recover them. §3.2 (ug.txt:1631–1703) states that
32 vectors occupy `>FFFF FC00`–`>FFFF FFE0`, i.e. a **descending 16-byte-spaced
sequence** starting at `>FFFF FF00`; that spacing pattern implies NMI `>FFFF FEE0`, HI
`>FFFF FED0`, DI `>FFFF FEC0`, WV `>FFFF FEB0` — but this is an **inference from the
spacing, not a transcription**. **Take all five from MAME's `34010ops.h` /
`tms34010.cpp` interrupt-vector table.** The 32-bit vectors are stored as two
consecutive 16-bit words with the **16 LSBs at the lower address** (§3.2, ug.txt:1690
region).

#### Interrupt latency — §8.4.1 (ug.txt:8524–8534)

The guide lists six potential sources of interrupt latency but only two are described
in the text present in this OCR; the remaining four are in the surrounding figure/
table and did not survive OCR. What survives, transcribed:

- (1) request recognition — the interrupt must be sampled at an instruction boundary;
- (2) a screen-refresh cycle read in progress — the refresh cycle has higher memory
  priority than the GSP's own cycles, so a refresh steals cycles ahead of the vector
  fetch.

**The full latency budget is [OCR-UNREADABLE]; take cycle-exact interrupt latency from
MAME.**

---

## 6. Anything else directly useful

### 6.1 Instruction timings (§13)

**The full timing chapters are present** — this is not a summary-only situation. The
chapter breaks down as follows (TOC at ug.txt:241–268):

| §13 subsection | Page | Content |
|---|---|---|
| 13.1 General Instructions | 13-2 | Machine States field semantics |
| 13.1.1 Best Case Timing — Considering Hidden States | 13-2 | hidden-state convention |
| 13.1.2 Other Effects on Instruction Timing | 13-3 | factors not counted |
| 13.2 MOVE and MOVB Instructions | 13-4 | incl. 13.2.3 a worked timing example |
| 13.3 FILL Instructions | 13-9 | setup time, transfer timing, interrupt effects |
| 13.4 PIXBLT Instructions | 13-16 | setup time, transfer timing, interrupt effects |
| 13.5 PIXBLT Expand Instructions | 13-26 | colour-expand timing |
| 13.6 LINE Instructions | (13-2x) | — |

**Every instruction in §12 also carries its own `Machine States` line** in its
description page, formatted (ug.txt:24603–24616):

> "Each instruction description in Section 12 contains a Machine States field that lists
> the number of CPU states required to execute the instruction. This description appears
> as:
>
> ```
> Machine
> States       <cache hit case>, <cache disabled case>
> ```"

and the two cases are defined (ug.txt:24617–24640):

> "The cache hit case gives the number of execution states if the instruction and its
> extension words reside entirely in cache… The cache disabled case gives the number of
> execution states if the cache is disabled when the instruction is executed… Cache
> disabled timing is not necessarily worst case timing. It may sometimes be exceeded
> when the cache is enabled but the instruction is not in the cache (this is known as a
> cache miss)."

**Hidden states** (§13.1.1, ug.txt:24640+):

> "Best case timing occurs when an instruction is executed entirely in parallel with the
> end of a previous instruction. According to some microprocessor conventions, many
> TMS34010 instructions would have a best case timing of 0 states. Since this is
> unrealistic, the convention used here assigns a finite (nonzero) timing value but
> allows for instruction overlap by using the concept" [of hidden states]

> "Up to nine machine states may be hidden by write cycles incurred by a single
> instruction."

Factors explicitly **not** counted in the Machine States figures (§13.1.2, ug.txt:246xx):
wait states, host-port accesses, display-refresh cycles, DRAM-refresh cycles, and
HOLD/HLDA bus arbitration. So a Machine States figure is a **CPU-state** count, not a
wall-clock cycle count — an emulator that also emulates VRAM refresh must add its own
refresh cycles on top.

The Machine States column in Table 12-7 (e.g. `DIVS 40,43`, `MPYU 21,24`, `EMU 6,9`,
`FILL/PIXBLT` — see §4.3–§4.6 of this document) duplicates the per-instruction figures
in compact form, which is the easiest table to work from.

### 6.2 Appendix B — "Emulation Guidelines for Prototyping" (ug.txt:26577–26701)

**Appendix B is about XDS *hardware* emulation pods, not about software-emulating the
GSP.** It contains no guidance on replicating the chip's video output or VRAM refresh.
The content, transcribed and summarised:

**B.1 Synchronizing a Host Processor with the TMS34010** (ug.txt:26583–26620+). Four
guidelines, in the guide's own words:

> "In a prototype system that contains an actual host processor instead of an emulator
> for the host processor, the host may have to avoid initiating accesses of the GSP's
> host interface registers while the GSP emulator is halted on a breakpoint condition."

> "If emulators are present for both the host processor and GSP, the two emulators may
> need to be synchronized to each other to permit a breakpoint in one emulator to halt
> both emulators. Without this capability, debugging software that performs
> communication between the host and GSP may be difficult."

> "If the TMS34010 emulator halts on a breakpoint and the host attempts to access the
> GSP's host interface, the XDS will prevent the access by intercepting the chip-select
> signal to the emulator (so that HCS remains inactive high) and by transmitting a
> not-ready signal (HRDY low) to the host. This forces the host to wait (by extending
> the access cycle) until the TMS34010 emulator begins running again. Host processors
> that cannot tolerate lengthy waits caused by not-ready signals should not attempt to
> access the GSP's host interface registers while the emulator is halted. For instance,
> if the host bus must be available to perform DRAM-refresh cycles at regular
> intervals, a long access (extended by a not-ready signal from the GSP) could delay
> refreshing for so long that data in memory becomes corrupted."

> "While the TMS34010 emulator is halted, host accesses of GSP registers must be
> prevented in order for the GSP to maintain a valid internal state while halted. When
> the TMS34010 emulator encounters a breakpoint, it stops execution and dumps an image
> of its internal registers to a buffer memory in the XDS. This image can be inspected
> and altered. When the emulator enters run mode again, the internal register image is
> loaded back into the GSP internal registers, and execution continues. During the time
> the TMS34010 emulator is halted, the host is not allowed to modify the state of the
> internal registers because this would invalidate the register image."

The final paragraph turns to the **HLDA/EMUA** signal, which the GSP drives active low
on halt and which a prototype may wire to the host's IRQ line. **The prose here runs off
the end of the OCR'd page; the remainder of B.1 and all of B.2 (target-cable grounding)
are of no use to a software emulator.**

Two things in B.1 do transfer conceptually: the **HRDY not-ready handshake** as the
mechanism by which the GSP stalls a host that tries to touch it at the wrong moment, and
the fact that a halted GSP must still be allowed to service refresh.

### 6.3 Host halt — and a correction to the parent brief

**The parent brief asked about "`CONTROL` bit `0x8000` / host halt". In this guide that
is not a thing.** CONTROL (`>C000 00B0`) bit 15 is **CD, Instruction Cache Disable**
(§2.2 and ug.txt:4700–4793); bit 0 is Reserved. **Host halt is HSTCTL / HSTCTLH bit 15
(`HLT`)** — see §5.2 above for the verbatim description. A register named CONTROL2 does
not exist in this guide, so there is no second CONTROL word to look in. If a port
documents a `CONTROL 0x8000` as a halt, it is describing the HSTCTL register under a
different name, or it is a bug.

The behaviours that matter for an emulator, all from HSTCTLH:

- HLT takes effect at the **next instruction boundary**, not immediately (ug.txt:5903).
- **Once halted the GSP does not service ANY interrupt, including NMI** (ug.txt:5903–5919).
  This is stronger than "maskable interrupts are disabled" — a halted GSP ignores NMI too.
- **Screen refresh, DRAM refresh and video timing all continue while halted**
  (ug.txt:5903–5919, and again ug.txt:10326–10328).
- HLT's reset value comes from the **HCS pin**: HCS high at reset → HLT = 1 (GSP starts
  halted); HCS low at reset → HLT = 0 (ug.txt:4341–4361).
- The **GSP can halt itself** by writing 1 to HLT (ug.txt:5903–5919).
- If the host also needs a cache flush when it is about to download code, **CF**
  (bit 14) does that (ug.txt:4089, 4100).

### 6.4 Host writes to VRAM vs a running screen refresh (§11.3)

This is documented, and it is the answer to "how do host writes interact with a running
refresh". Table 11-1, "Priorities for Memory Cycle Requests" (ug.txt:11802–11820),
transcribed verbatim — priority 1 is highest:

| Priority | Memory Cycle Requested |
|---|---|
| 1 | Hold request from external bus master device |
| 2 | Screen-refresh cycle |
| 3 | DRAM-refresh cycle |
| 4 | Host-initiated indirect read or write cycle |
| 5 | GSP CPU memory cycle |

**So a host indirect write has higher priority than the GSP's own CPU cycles but lower
priority than a screen-refresh cycle.** In practice that means:

- The host's write *can* land between GSP instructions, and it *can* land in the middle
  of what the GSP believes is a coherent sequence — but it will **never** delay or tear a
  screen-refresh cycle, which always wins.
- Corollary for an emulator: while a refresh cycle is in flight, a host access waits.
  A host write to VRAM therefore becomes visible on screen only from the *next*
  screen-refresh cycle onward.

Three further rules from the same section, transcribed (ug.txt:11822–11838):

> "A GSP CPU memory cycle is a read or write performed by the GSP's on-chip 32-bit
> processor. Insertion of a field (or a portion of a field spanning multiple words) into
> a word requires two CPU memory cycles when the field does not begin and end on word
> boundaries. The two cycles are a read followed by a write. This sequence is called a
> read-modify-write operation. The read and write are performed as separate memory
> cycles, but are treated as indivisible; that is, the memory controller will not permit
> another memory request to be serviced between the read and its accompanying write. The
> only exception to this statement is the hold request."

> "While a read-modify-write operation on an individual memory word is indivisible, the
> accesses necessary to extract or insert a field spanning multiple memory words are not.
> For example, if a field spans portions of two memory words, a higher priority access
> such as a host-indirect cycle can occur between the two read-modify-write operations
> required to insert the field."

> "A memory cycle already in progress will always be permitted to complete, even if a
> higher priority request is received while the cycle is still in progress."

And on bus holds (ug.txt:11840–11854):

> "The hold request has the highest priority. An external device requests control of the
> bus by signalling a hold request to the GSP. The external device may perform multiple
> memory cycles following acknowledgment from the GSP. However, the device should not
> control the bus for so long that necessary screen-refresh and DRAM-refresh cycles are
> prevented from occurring. Indirect accesses initiated by a host processor will be
> blocked as long as the external device continues to control the bus. If the host
> processor attempts to initiate another indirect access during this time, the host will
> be forced to wait at the host interface (the GSP sends it a not-ready signal) until the
> external device releases the local bus."

### 6.5 Documented behaviour an emulator may get wrong

A short list of things the guide states explicitly that are easy to miss:

1. **Screen refresh continues while the GSP is halted** (HLT) — ug.txt:5903–5919. A
   port that freezes video when the CPU halts is wrong.
2. **A halted GSP ignores NMI** — ug.txt:5903–5919. "Once halted, the GSP does not
   respond to interrupt requests (including NMI)."
3. **DPYADR is guaranteed not to change between the display interrupt firing and the
   completion of the next screen-refresh cycle** (at least one full line later) —
   ug.txt:5345–5381. This is what makes split-screen via a DPYINT handler well-defined.
4. **Screen-refresh cycles have higher priority than processor-initiated memory cycles**
   — ug.txt:5345–5381 and Table 11-1.
5. **The shift-register transfer performed by SRT=1 is a *memory* operation, not a pixel
   operation, and DPYTAP is not output during it** — ug.txt:5243–5270 (SRT) and
   ug.txt:5459 (DPYTAP exclusion). PIXBLT with SRT=1 therefore cannot pan the display.
6. **Interlaced displays need DUDATE set to twice the non-interlaced pitch**, and the
   half-increment is applied just after DPYSTRT is loaded, at the start of vertical
   blanking preceding an odd field — ug.txt:5206–5210 and ug.txt:10167–10175.
7. **NMI with NMIM=1 saves nothing** — no PC, no ST — ug.txt:8405–8455. Handlers must be
   leaves.
8. **Read-modify-write across two words is NOT atomic**; only within a word is it
   indivisible, and a host-indirect cycle may be interleaved between the halves —
   ug.txt:11822–11838.
9. **Plane masking is applied twice in a graphics transfer** — once to the values read
   from memory and again to the processed result, and because "bits which are 1s in the
   PMASK produce 0 bits in the result", a processed pixel can *become transparent* as a
   result of plane masking — ug.txt:7436–7478.
10. **Window checking only works with XY destination addresses** — ug.txt:7884–7886 —
    and W=1 mode inhibits *all* pixel writes, not just the ones outside the window
    (ug.txt:7914–7966).

### 6.6 Explicit "NOT IN THIS DOCUMENT" list

Everything the parent brief asked about that this guide does **not** contain:

| Item asked about | Status |
|---|---|
| `VCOUNT2` register | **NOT IN THIS DOCUMENT** — TMS34020 only |
| `CONVMP`, `CONTROL2`, `CONFIG`, `PSCALE`, `CONVBL`, `CONVSL`, `CONVSHR`, `CONVTTR` | **NOT IN THIS DOCUMENT** — no such registers; the guide has CONVSP and CONVDP only |
| `PMSTRT`, `PMCOUNT`, `PMSIZE` | **NOT IN THIS DOCUMENT** — zero occurrences anywhere in ug.txt; only `PMASK` exists |
| `CONTROL` bit 0x8000 = host halt | **REFUTED** — CONTROL bit 15 is `CD` (cache disable). Halt is HSTCTL/HSTCTLH bit 15 |
| The DUDATE increment-size column of the §6.4 table | **[OCR-UNREADABLE]** — reconstructed from the bit-position column, which reads cleanly |
| Figure 11-1's LAD4/LAD2 row-address entries | **[OCR-UNREADABLE]** — recovered as 16 and 14 from the descending sequence; marked in §1.3.3 |
| The DPYADR↔LAD per-pin alignment in the §6 ASCII diagram | **[OCR-UNREADABLE]** — superseded by §9.9.1.4 and Figure 11-1 |
| Exact byte addresses of HSTCTLL and (partly) HSTDATA | **[OCR-UNREADABLE]** in Table 6-1 — take from MAME |
| Internal interrupt vector addresses (NMI, HI, DI, WV) | **[OCR-UNREADABLE]** — only INT1/INT2 transcribed cleanly; see §5.4 |
| Full interrupt-latency budget | **partly [OCR-UNREADABLE]** — only 2 of 6 causes survived |
| The exact MSGIN/MSGOUT mailbox handshake protocol | **NOT IN THIS DOCUMENT** at register level |
| `yoffset` semantics as used by MAME's params | **NOT IN THIS DOCUMENT** — see §1.6 |
| Guidance on software-emulating the video output | **NOT IN THIS DOCUMENT** — Appendix B is about XDS hardware pods only |
| Which 8 of LAD0–LAD11 a given board routes to VRAM | **NOT IN THIS DOCUMENT** — this is a board-level choice; see §1.6 |
