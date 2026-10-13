# Hardware contract: the 1989 manual against our machine model

Every claim here is sourced to one of three places, and the source is named:

* **[MANUAL]** — `reference/manuals/stunrunner.pdf`, Atari Games TM 238 / SP-338,
  1st printing 1989. Read from the rendered page images in
  `/tmp/stunrunner_pages/page-*.png`, **not** from
  `reference/manuals/stunrunner-ocr.txt`, whose page markers are unreliable and
  whose schematic text is largely noise. Vendor documentation for the board we
  emulate, so it outranks both ROM reading and MAME wherever they disagree.
* **[MAME]** — the pinned source at `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp`
  and `harddriv_m.cpp`, build `0.289`, sha256 `75b09ee4…`.
* **[ROM]** — our own measurements against the pinned ROM set.

Status values: `CONFIRMED`, `GAP` (the manual says hardware exists that our
model does not carry), `REFUTED` (we say something the manual contradicts).

## 1. Memory map

The Main Board Memory Map (SP-338 Page 15) is legible in the image at PDF page 72
and is the single most useful page in the document.

| Range | [MANUAL] name | [MAME] | Status |
|---|---|---|---|
| `$000000-$0FFFFF` | `ROM0..ROM7`, 128K each, 8 entries | `map(0x000000,0x0fffff).rom()` | CONFIRMED |
| `$404000/$404800` | `OPTO` optical steering wheel reader counter/centre | not mapped in the multisync map | GAP — see §6 |
| `$600000` | `SCOM` Serial Sound Communications (R/W) | `jsa:I` window | CONFIRMED |
| `$604000` | `SCRES` reset SCOM, plus the `LSBUS` latch bank | `hd68k_sound_reset_r` / `hd68k_nwr_w` | CONFIRMED |
| `$60400C` / `$60401C` | `GSP Reset Low` / `GSP Reset High` | part of the `nbus` write handler | CONFIRMED |
| `$608000` | `{W} WOCLR` Clear Watch Dog (Address Strobe) | `"watchdog"` device | CONFIRMED |
| `$60C000` | `{R} SW1` + `{W} Clear Timer IRQ` | `hd68k_port0_r` / `hd68k_irq_ack_w` | CONFIRMED |
| `$A00000` | `{R/W} WRO` shifter interface and coin counters, `SEL 1..4` | `hd68k_wr0_write` | PARTIAL — shifter semantics absent |
| `$A80000` | `{R} /SW2` **sixteen external switch inputs**; `{W} /WRI` shifter latch | `hd68k_a80000_r` / `hd68k_wr1_write` | GAP — see §5 |
| `$B00000` | `{R} /RD2` read 8-bit A/D; `{W} /WR2` **steering wheel latch** | `adc8` | PARTIAL — latch not modelled |
| `$B80000` | `{R} /RD3` read 12-bit A/D; `{W} /WR3` A/D control | `adc12` | CONFIRMED |
| `$C00000-$C03FFF` | **not in the legible main-board map** | `hd68k_gsp_io_r/w` | UNRESOLVED — see §7 |
| `$FF0000` | `DUART` | `mc68681_device` | CONFIRMED |
| `$FF4000-$FF4FFE` | `ZRAM` **4 KiB** | not separately mapped | GAP |
| `$FF8000-$FFBFFF` | `RAM 0` **16 KiB** | `.ram()` | CONFIRMED |
| `$FFC000-$FFFFFF` | `RAM 1` **16 KiB** | `.ram()` | CONFIRMED |

**Main RAM is 32 KiB.** [MANUAL] confirms this directly, which vindicates our
section 9 and refutes our own section 2.1 without needing MAME at all.

## 2. The `$60C000` word

The manual labels `$60C000` `SW1` and then lists **all sixteen bits** under that
one name, which initially looks like it contradicts MAME. It does not. The 68010
reads a 16-bit word, and [MAME] `hd68k_port0_r` composes it as
`(m_sw1.read_safe(0xff) << 8) | m_in0->read()`:

| bit | [MANUAL] | [MAME] `mainpcb:IN0` / `mainpcb:SW1` | Status |
|---|---|---|---|
| D15–D8 | Option Switch 7 … 0 | `mainpcb:SW1` eight DIPs, default `0x00` | CONFIRMED |
| D7 | Coin Switch 1 (`0` = on) | `PORT_BIT(0x80, … IPT_COIN1)` | CONFIRMED |
| D6 | Coin Switch 2 | `PORT_BIT(0x40, … IPT_COIN2)` | CONFIRMED |
| D5 | Self-Test Switch (`0` = on) | `PORT_SERVICE(0x20, IP_ACTIVE_LOW)` | CONFIRMED |
| D0 | Diagnostic Switch (`0` = on) | `PORT_DIPNAME(0x01, …)` | CONFIRMED |

So the manual's `SW1` is the whole word, DIP bank in the high byte and the
control byte in the low. Every bit reconciles. Note the polarity: these are all
`0 = on`, i.e. active low, which is what `IP_ACTIVE_LOW` already encodes.

## 3. Option switches are SOFTWARE, not DIPs

This is the most consequential correction, and nothing in our model or in MAME's
says so. [MANUAL] Coin Options and Game Options are **screens**, reached from the
test menu's `OPERATOR SCREAMS` entry, and they are edited with the controls:

> To move up or down the list, press the right or left trigger.
> To change a setting, press a trigger and either coin switch.
> To return to the original setting, hold a coin switch and press START.
> To exit, press the START button.

The settings themselves (Table 2-2) are Coin Mode 1–4 coins/credit, Left and
Right Mech Multiplier, and Bonus Adder including **Free Play**; the Game Options
screen sets difficulty and related operator options.

**Consequence:** `mainpcb:SW1` is *not* the coin/credit configuration. It is
eight option-switch bits that select boot behaviour. The coin/credit policy
lives in **RAM**, edited through the screens. Anything we model as a DIP-driven
coin mechanism is wrong in kind, not just in value.

## 4. What the self-test screens tell us

[MANUAL] Table 2-4 lists every screen. The sequence we have been decoding since
section 29 is this one:

```text
PROGRAM ROM / BAD PROGRAM ROM
PROGRAM RAM OK
GSP VRAM OK--SIMPLE TEST
GSP COLOR RAM OK
ADSP BOARD OK
SA II Test
PRESS START TO SELECT TEST MENU
```

then the Test Menu:

```text
TEST MENU
OPERATOR SCREENS
CONTROL INPUTS
MONITOR TEST PATTERNS
SPECIAL FUNCTIONS
S.T.U.N. RUNNER VERSION 20.0
COPYRIGHT ATARI GAMES 1989
```

**This identifies our two draws.** [ROM] selector `$47` is
`PRESS*START*TO*BYPASS*SELFTEST` at frames 45-47, and `$43`/`$50`/`$51`/`$52` are
`PROGRAM*ROM*OK`, `Y*X*W*V*U*T*S*R`, `200`, `210` at frames 165-167. So the
frames 45-47 / 165-167 captures are the **self-test and test-menu sequence
reached by default boot**, not attract mode.

`Y X W V U T S R` is eight **consecutive letters descending** (R=18 … Y=25), which
is not menu prose. The nearest documented candidate is the `SPECIAL FUNCTIONS`
submenu (Main Board GSP Tests, Main Board ROM Checksums, Main Board ZRAM Tests,
ADSP Board Tests, Audio Board, Sound Board Self-Test, Play Sounds). **We have not
matched it and must not claim to.** Recorded as an open identification.

## 5. GAP: the flight controls are sixteen switches, not three

[MANUAL] `$A80000` is `{R} /SW2` — *"Sixteen External Switch Inputs"* — with
`{W} /WRI, Shifter Interface Latch, D8-D15`.

[MAME] models the same address as three bits:

```c
PORT_START("mainpcb:a80000")
PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 )
PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 )
PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_START1 )
PORT_BIT( 0xfff8, IP_ACTIVE_LOW, IPT_UNUSED )
```

Thirteen bits are `IPT_UNUSED`. The manual says sixteen switches exist. Something
is being thrown away, and the evidence that it matters is already in our own
work: the test menus are navigated with **left and right triggers** (§3), and the
Coin Options screen changes settings with a trigger plus a coin switch. A
trigger-driven menu cannot be driven through a three-bit port.

This is consistent with `stun-runner-spx` ("native: implement input system
(wheel, ADC, **shifters**)"), which already names the gap without the manual
having told us.

## 6. The flight control is two devices, not one stick

[MANUAL] Chapter 3, Flight Control Maintenance:

> The flight control consists of a handle assembly and a flight control assembly.
> **The handle assembly controls the vertical motion**, and **the flight control
> assembly controls the horizontal motion of the display.**

Maintenance notes reference *potentiometer shafts* on both, and a spring handle
pin with a 60-tooth spur gear. [MANUAL] `$B00000` `{W} /WR2` is a **steering
wheel latch** — the wheel is latched, not continuously read.

So the control panel is at minimum: a vertical axis, a horizontal axis, a wheel
latch, triggers, and a shifter. [MAME] carries two ADC axes
(`8BADC.0` = X, `8BADC.2` = Y) which is consistent with the two axes, plus the
12-bit ADC. **The triggers and shifter are the confirmed gaps.**

Also noted, not modelled: [MANUAL] `$404000/$404800` `OPTO` — an optical steering
wheel reader with counter and centre flag, `NOT LOADED` on SP-338 Page 9 for this
game. Its absence from the map is consistent with not being fitted, so this is
probably correct, but it is worth knowing it exists.

## 7. Still unresolved after reading the manual

**The `$C00000` GSP host port is not in the legible Main Board Memory Map.** That
map stops at `$60C000` → `A00000` → `A80000` → `B00000` → `FF0000`. The port
lives on the video section, and SP-338 Page 10 gives its block diagram but no
address rows. So the word-offset host-port decode in
`docs/mc68010-boot-findings.md` §31.4 still rests on measurement alone, and the
manual neither confirms nor contradicts it. See §33.8 for the reach measurement
and §33.9 for the inconsistency that measurement created.

**VRAM size.** [MANUAL] SP-338 Page 10 reads `VRAM Array (16)` and the BOM reads
`64K x 4`, which is 512 KiB of silicon; [MAME] installs 4 MiB. Deferred by
explicit decision — see §33.8. The live question the user wants answered is
*what is in the share and how it is stored*, not how big the chip is.

**The video chain**, now transcribed from the Page 10 block diagram:

```text
34010-50 GSP --> 34012-50 PSP --> VDD-VD15 (15-bit digital video)
             \-> Address Buffers -> MA0-MA7 -> VRAM Array (16)
                                                    -> SD0-SD15 -> Pixel Scanner
                                                    -> SPIX0-SPIX7 -> Color RAM
                                                    -> RED0-7 GRN0-7 BL0-7
```

The GSP writes VRAM and does not scan out; a **separate TMS34012** serialises its
video; the Pixel Scanner shifts VRAM out and **Color RAM** produces RGB. So the
palette is a distinct device fed by an 8-bit pixel index, not "two 1024-entry
16-bit planes" inside GSP memory as `docs/gsp-display-scanout-contract.md` models
it. That is inference in our contract and should be relabelled as such.

Also from Page 10: `XO-SCH 40MHZ` pixel oscillator, `16 MHz` 68010 clock, jumpers
`Speed A,B` / `BCLK QB` / `VCLK QB/2`. Directly relevant to `stun-runner-bzq`,
which derives the OKI rate from a hardcoded `3579545/3` rather than from a clock
the manual states.
