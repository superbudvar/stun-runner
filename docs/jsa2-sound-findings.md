# JSA-II sound: findings log

Companion to [`mc68010-boot-findings.md`](mc68010-boot-findings.md) (68010 side) and
[`mame-stunrunner-findings.md`](mame-stunrunner-findings.md) (MAME driver). This file
records the **6502 sound CPU** side and the OKI6295 decode chain, which until now had
no findings record at all. It answers the investigation question behind bead
`stun-runner-bzq`: *where does a 68010 sound command turn into an OKI6295 write, and
what does it write?*

Confidence tags used below: **[C]** confirmed by byte-level disassembly of the real
ROM, **[M]** confirmed against the MAME source, **[I]** inferred.

Paths are repository-root-relative. ROM file offset **is** the 6502 address: MAME loads
`ROM_LOAD("136070-2123.10c", 0x00000, 0x10000, ...)` at
`mame-ref/src/mame/atari/harddriv.cpp:2707`, so `roms/stunrun/136070-2123.10c[i]` is
address `$IIII`. [C]

---

## 0. Summary — why there is no sound

*Status as of 2026-10-10: all three breaks below are **closed** at the C level
(§5.1, §5.3). What is still absent is the last edge — the 68010 never *issues* a
command during a real boot in our shell, because the boot stalls before it
reaches the sound-command queue (§5.3, "What the live run still does not do").
Audible output in a live run therefore still does not happen; the chain is
now complete and tested, but nothing feeds it yet.*

The port's audio chain was **structurally complete and semantically unreachable**.
Every link from decoded PCM to an audio backend existed; nothing linked *into* it.

There were three independent breaks, all upstream of the backend:

1. **No transport.** The 68010 writes a command byte to `$600000-$603FFF`; MAME wires
   that to `main_command_w` → NMI on a real 6502 (`atarijsa.cpp:295`,
   `harddriv.cpp:4806`). Our `stun-runner/native/musashi_cpu.c` routes that region
   (`REGION_SOUND`, line 269; `route_address`, line 354) but every `REGION_SOUND` case
   was a no-op stub. Nothing ever called `stunrun_jsa_main_command_write()` on
   `g_sound_dispatch.latches`. **No command byte was ever produced.**
   **CLOSED** — `native/jsa_transport.{h,c}`, CTest `jsa-transport-musashi-c` (§5.3).
2. **No translation.** Even with a byte in hand, that byte is **not** an OKI6295
   command. The 6502 firmware translates it through two class tables and four data
   tables (§2, §3). `sound_dispatch_send_command()` fed the raw byte straight into
   `oki6295_write()` — the wrong command space entirely.
   **CLOSED** — routed through `oki_cmdmap.c`, CTest `sound-dispatch-slice-c` (§5.1).
3. **Wrong codec.** `reproduction/sound/oki6295.c` implemented **IMA ADPCM**, not OKI
   ADPCM (§4). Even correctly-addressed samples would decode as wrong-pitched noise.
   **CLOSED** — `oki6295_test.c`.

Fixing only (3) would have yielded silence. Fixing (1)+(2) with (3) unfixed yields
noise. All three were required.

---

## 1. The 6502 sound board

`roms/stunrun/136070-2123.10c`, 64 KiB, vectors at `$FFFA`/`$FFFC`/`$FFFE`:
**NMI = `$57E3`, RESET = `$4000`, IRQ = `$41C1`.** [C]

Board memory map, from `mame-ref/src/mame/atari/atarijsa.cpp:103-143` (`atarijsa2_map`):
`$2000` YM2151 data, `$2001` YM2151 address, `$2800` OKI status (`/RDV`), `$2802` sound
command (`/RDP`), `$2804` JSA I/O (`/RDIO`), `$2806` mixer (`/MIX`), `$2808` WRIO write
mirror, `$2A00` OKI write (`/WRV`), `$2A02` 6502→68010 response (`/WRP`), `$2A04` WRIO,
`$2A06` `/MIX`.

`$0200-$024F` is the firmware's RAM work area (§2.4).

### 1.1 Reset

```
$4000: sei / $4001: cld / $4002: ldx #$ff / $4004: txs
$4005: lda #$01 / $4007: sta $2a04      ; assert WRIO
$400A: and #$fe / $400C: sta $2a04      ; de-assert
$400F: ora #$01 / $4011: sta $2a04      ; re-assert
$4014: ldx #$00 / $4016: lda $280c / $4019: and #$80 / $401B: bne $402a
$401D: lda #$00 / $401F: sta $00,x / $4021: inx / $4022: bne $401f
$4024: jsr $41bd / $4027: jmp $4113
```
[C] Three `$2A04` writes bracket a RAM clear.

### 1.2 Clocks — and the sample rate our port invented

`#define JSA_MASTER_CLOCK XTAL(3'579'545)` (`atarijsa.cpp:80`) [M]

```
M6502(config, m_jsacpu, JSA_MASTER_CLOCK/2);
YM2151(config, m_ym2151, JSA_MASTER_CLOCK);
OKIM6295(config, m_oki1, JSA_MASTER_CLOCK/3, okim6295_device::PIN7_HIGH);   // atarijsa.cpp:901
```

OKI6295 output rate = `(3579545 / 3) / divisor`, and
`int divisor = m_pin7_state ? 132 : 165;` (`mame-ref/src/devices/sound/okim6295.cpp`) [M]:

| pin 7 | divisor | rate |
|---|---|---|
| HIGH | 132 | **9039.255 Hz** |
| LOW  | 165 | **7231.404 Hz** |

The 6502 toggles pin 7 through WRIO bit 3: `m_oki1->set_pin7(data & 8)`
(`atarijsa.cpp:481` `wrio_w`). [M]

**Our port hardcodes 11025 Hz** at `reproduction/sound/sound_dispatch.c:132`
(`.sample_rate = 11025`) and `reproduction/sound/oki6295.h:121`. 11025 matches neither
derived rate. `oki6295_set_pin7()` is never called. [C]

---

## 2. The command path — the layer we do not have

### 2.1 MAME's half

`harddriv.cpp:4806` (`init_multisync`):
```cpp
if (m_jsa.found())
    m_maincpu->space(AS_PROGRAM).install_readwrite_handler(
        0x600000, 0x603fff,
        read8smo_delegate (*m_jsa, FUNC(atari_jsa_base_device::main_response_r)),
        write8smo_delegate(*m_jsa, FUNC(atari_jsa_base_device::main_command_w)), 0xff00);
```
[M] Mask `0xff00` ⇒ the byte at `0x600000 + n` is command byte `n` (0..255); the read
side returns the 6502's response.

`atarijsa.cpp:295` `main_command_w(u8 data) { m_soundcomm->main_command_w(data); }` →
`atariscom.cpp:99` `m_sound_write_timer->adjust(attotime::zero, data)` →
`delayed_sound_write` sets `m_main_to_sound_data` / `m_main_to_sound_ready = true` and
asserts NMI on the 6502. [M] The 6502 reads it at `/RDP`; `sound_command_r()`
(`atariscom.cpp:140`) clears `ready` and deasserts NMI. [M]

### 2.2 The NMI handler, `$57E3` [C]

```
$57E3: pha
$57E4: lda #$20 / $57E6: bit $280c / $57E9: bne $57e4     ; spin while /RDIO bit5 busy
$57EB: tya / $57EC: pha
$57ED: ldy $0234 / $57F0: bne $580a                     ; queue-mode flag
$57F2: inc $0232 / $57F5: bne $57fa / $57F7: inc $0233    ; 16-bit counter++
$57FA: ldy #$00
$57FC: lda $280a                                        ; <== READ COMMAND BYTE (/RDP)
$57FF: sta ($04),y                                      ; into the ($04),($05) ring
$5801: inc $04 / $5803: bne $5807 / $5805: inc $05
$5807: jmp $584b
$580A: iny / $580B: beq $5813 / $580D: lda $280a / $5810: jmp $584b
$5813: txa / $5814: pha
$5815: ldy $0231 / $5818: iny / $5819: cpy #$30 / $581B: bcc $581f / $581D: ldy #$00
$581F: sty $0231
$5822: cpy $0230 / $5825: bne $5834                     ; queue empty?
$5827: ldx $0230 / $582a: inx / $582b: cpx #$30 / $582d: bcc $5831 / $582f: ldx #$00
$5831: stx $0230
$5834: lda #$6d / $5836: sta $0200,y                    ; sentinel 0x6D = "empty slot"
$5839: ldx $280a                                        ; <== command byte again
$583c: cpx #$6d / $583e: bcs $5849
$5840: lda $6093,x                                      ; NMI class table
$5843: bpl $584f                                        ; class >= 0 -> dispatch now
$5845: txa / $5846: sta $0200,y                         ; else enqueue into $0200
$5849: pla / $584a: tax / $584b: pla / $584c: tay / $584d: pla / $584e: rti
$584f: cmp #$04 / $5851: bcs $5849                      ; class must be 0..3
$5853: asl a / $5854: tay
$5855: lda $61dd,y / $5858: pha / $5859: lda $61dc,y / $585c: pha / $585d: rts
```

**This is the decisive finding: the 68010 command byte is never written to the OKI6295.**
It is read at `$57FC`/`$5839`, classified through `$6093`, and either acknowledged
immediately or pushed into a 48-byte software queue at `$0200`.

### 2.3 The two class tables

There are **two**, indexed by the same command byte, and they do different jobs.

**`$6093`, 109 bytes (`$6093-$60FF`), read only by the NMI.** [C]
Values are `$FF` ("acknowledge, do not dispatch") except:

| command | class | handler | target |
|---|---|---|---|
| `$03` | 0 | `$5DAD` | `lda $75 / jsr $4553 / jmp $5849` |
| `$06` | 1 | `$4543` | `lda #$6d / jsr $4553`, falls into `$4548` |
| `$07` | 2 | `$4533` | `lda #$02 / jsr $4553 / … / jmp $4548` |
| `$08` | 3 | `$5CB8` | `lda #$01 / jsr $5cc5 / lda $63 / jsr $4553 / jmp $4548` |

Jump table `$61DC = AC 5D 42 45 32 45 B7 5C` (RTS adds 1 ⇒ `$5DAD $4543 $4533 $5CB8`).
[C]

**None of these four touch the OKI6295.** They are the 68010 handshake commands, and
they all funnel into:

```
$4553: pha / lda #$20 / $4555: and $280c / $4558: bne $4555   ; wait while /RDIO bit5
$455c: pla / $455d: pha / $455e: sta $2a02                    ; write /WRP
$4561: lda #$20 / $4563: and $280c / $4566: bne $4563         ; wait again
$4569: pla / $456a: rts
```
i.e. `sound_response_w` — the 6502→68010 reply. [C]

**`$6100`, 109 bytes (`$6100-$616C`), read by the main-loop dispatcher.** [C]
Class histogram: `{0:2, 1:6, 3:1, 5:2, 8:32, 9:1, 0x0A:5, 0x0B:2, 0x13:53, 0x14:1, 0xFF:4}`.

### 2.4 The main loop and the dispatcher, `$4149-$4189` / `$4371` [C]

```
$414c: lda $02 / $414e: and #$fe / $4150: sta $02
$4152: lda #$20 / $4154: bit $280c / $4157: bne $4171
$4159: ldy $0245 / $415c: cpy $0246 / $415f: beq $4171
$4161: lda $0235,y                     ; 16-byte response ring
$4164: sta $2a02                       ; -> /WRP to the 68010
$4167: iny / $4168: cpy #$10 / $416a: bcc $416c / $416c: ldy #$00 / $416e: sty $0245
$4171: ldx $0230 / $4174: cpx $0231 / $4177: beq $4189
$4179: inx / $417a: cpx #$30 / $417c: bcc $417e / $417e: ldx #$00 / $4180: stx $0230
$4183: ldy $0200,x                     ; <== pop one queued command byte
$4186: jsr $4371                       ; <== DISPATCH
$4189: jmp $4149
```

```
$4371: cpy #$6d / $4373: bcs $4389              ; cmd >= $6D -> no immediate action
$4375: lda $6100,y                            ; class byte
$4378: cmp #$15 / $437a: bcs $4389             ; class >= $15 -> no immediate action
$437c: asl a / $437d: tax
$437e: lda $4730,x / $4381: pha                ; RTS-trick jump,
$4382: lda $472f,x / $4385: pha                ;   handler entered with
$4386: lda $616d,y                            ;   A = $616D[cmd]  (the ARGUMENT)
$4389: rts
```

Note `$4386`: the handler is entered with **A already loaded** from the argument table.
This is the indirection our port collapses.

Jump table `$472F`, 21 entries (`$472F-$4758`) [C]:

| cls | target | cls | target | cls | target | cls | target |
|---|---|---|---|---|---|---|---|
| 0 | `$438A` | 6 | `$4410` | 12 | `$46A4` | 18 | `$46A4` |
| 1 | `$43AD` | 7 | `$4435` | 13 | `$44EC` | **19** | **`$5BFE`** |
| 2 | `$43BA` | 8 | `$4569` | 14 | `$4729` | **20** | **`$5C6F`** |
| 3 | `$43CA` | 9 | `$44D0` | 15 | `$46A4` | | |
| 4 | `$43D5` | 10 | `$445F` | 16 | `$44C4` | | |
| 5 | `$43EE` | 11 | `$4496` | 17 | `$438F` | | |

- **class 19 (`$13`) → `$5BFE` is the OKI play command.** 53 command bytes map here.
- **class 20 (`$14`) → `$5C6F` is the OKI stop command.** Exactly one byte: `$66`.
- class 8 (`$4569`, 32 commands) is the YM2151 music/stream path — not OKI. [I]
- classes 12/15/18 all alias `$46A4`. [C]

So the OKI effect set is **exactly class 19**, plus the class-20 stop.

### 2.5 The `$1E` gate — why one-shot effects can be muted

```
class 0 @ $438A:  0a 0a 85 1e 60                      ; ASL A / ASL A / STA $1E / RTS
class 19 @ $5BFE: ldy $001e / $5c01: bne $5c61          ; if $1E != 0 -> SKIP the play
```

[C] The handler bytes at `$438A` are `0a 0a 85 1e 60`, so the store is **`$1E = argument << 2`**
— *not* `argument & 3` as an earlier reading of this section claimed. The two `ASL A` are not
producing a scaled value; they are the cheapest "zero stays zero, non-zero stays non-zero"
idiom, so `$1E` is effectively a **boolean mute flag**, and its only two reachable values are
`$00` and `$F0`:

| command | class | argument | `$1E` written | effect on class-19 plays |
|---|---|---|---|---|
| `$01` | 0 | `$3C` | `$F0` | **gated off** |
| `$02` | 0 | `$00` | `$00` | allowed |

[C] These are the *only* two command bytes in the whole 109-entry table that map to class 0
(full histogram: class 0 = 2, class 1 = 6, class 3 = 1, class 5 = 2, class 8 = 32, class 9 = 1,
class 10 = 5, class 11 = 2, class 19 = 53, class 20 = 1, class 255 = 4 — the `$03/$06/$07/$08`
NMI handshake commands). While `$1E != 0`, **every class-19 OKI play is skipped**.

### The open part: is `$1E` ever non-zero when an effect is wanted?

**Not established, and it is a live risk for our port rather than a curiosity.** The ROM proves
the gate exists and proves which two commands drive it, but nothing in the 6502 code shows
whether the 68010 ever sends `$01` and then still expects a class-19 effect — or whether `$01`
and `$02` bracket a whole block of gameplay during which no effects are wanted at all.

This matters concretely. `reproduction/sound/oki_cmdmap.c` exposes the gate as a modelled
observable, and as of the dispatch-bridge slice (`sound_dispatch.c`, see §5.1) `sound_dispatch_t`
carries an `oki_cmdmap_state_t cmdmap`, so the port now **propagates** `$1E`: `sound_dispatch_init()`
and `sound_dispatch_shutdown()` both call `oki_cmdmap_init()`, and `sound_dispatch_send_command()`
feeds every command through `oki_cmdmap_translate()`, which applies the `$5BFE` gate. The port no
longer assumes `$1E == 0`; it reproduces whatever the command stream says.

**What that does and does not settle.** `sound_dispatch_test.c::test_mode_gate` asserts that `$01`
suppresses an effect command at the bridge boundary and `$02` re-enables it. That asserts the
ROM's *gate behaviour* — `ldy $001e / bne $5c61`, confirmed, nothing inferred — and it does not
assert anything about **sequencing**, which is the open question below. It is a different claim from
the one this section warned against.

Until the sequencing is settled, the caution stands unchanged: the gate is propagated because the
ROM propagates it, **not** because we have established that gating real audio on it is safe. The
residual risk is real and has simply moved: if the real command stream never sends `$01` outside a
muted block, our port is correct; if it sends `$01` and then still expects effects, our port goes
silent at exactly the wrong moments, and it fails as *silence* — the easiest defect in this
subsystem to mistake for "the decoder still has a bug". Nothing will be audible until the
68010→JSA transport exists (§5.1), so this cannot be hit live yet.

What would settle it: a MAME run with a breakpoint on `$5BFE` logging `$1E` alongside the
dispatched class over a full playthrough, and separately a trace of the 68010 side recording the
order of `$01`/`$02` relative to the 53 effect command bytes. Until one of those exists, do not file
a `BUGS.md` entry, and do not treat the boundary test as evidence about sequencing.

### 2.6 The `($04),($05)` queue-mode ring is write-only [C]

In queue mode (`$0234 != 0`) the NMI writes the command byte into a ring at `($04)`,`($05)`
and bumps the pointer (`$57FF`, `$5801`, `$5805`). A complete scan of the ROM finds **52**
`lda (zp),y` instructions, with zero-page targets
`{6, 21, 25, 32, 73, 76, 106, 175, 179}` — **`$04` is not among them**. There is no
`lda ($04),y` anywhere in the ROM, so nothing ever reads that ring back.

`$5CC5` sets the ring pointer to `$6300` (`lda #$00 / sta $0005 / lda #$63 / sta $0004`)
and clears `$0234`. Whether `$0234` is ever set non-zero by the three writers
(`$4127 stx`, `$454E sta`, `$5CD8 sta`) was **not established**; `stun-runner/native/CMakeLists.txt:59-66`
already carries a dead slice (`sound_queue.c`) chasing this same region. Treat the
queue-mode branch as unproven and out of scope for the OKI path — the class-19 effect
commands do not depend on it.

---

## 3. The OKI6295 write sequence — what our port must reproduce

### 3.1 Class 19, `$5BFE` [C]

Entered with `A = $616D[cmd]` (argument), `Y = cmd`, `X = class*2`.

```
$5bfe: ldy $001e / $5c01: bne $5c61                 ; gate: $1E must be 0
$5c03: php / $5c04: sei
$5c05: tay                                         ; Y = sound index (0..52)
$5c06: txa / $5c07: pha
$5c08: lda $6567,y                                 ; voice+volume byte
$5c0b: and #$30 / $5c0d: lsr x4                    ; bits 4-5 -> voice 0..3
$5c11: tax
$5c12: sty $50
$5c14: lda $5c67,x / $5c17: and $2808 / $5c1a: beq $5c23   ; voice busy?
$5c1c: ldy $52,x / $5c1e: lda $6567,y / $5c21: bmi $5c5e     ; playing a locked sound? -> bail
$5c23: lda $5c6b,x / $5c26: sta $2a00               ; *** WRITE 1: stop the voice ***
$5c29: ldy #$7f / $5c2b: nop / $5c2c: dey / $5c2d: bpl $5c2b  ; ~128-cycle delay
$5c2f: lda $5c67,x / $5c32: and $2808 / $5c35: bne $5c23     ; poll until idle
$5c37: ldy $50 / $5c39: sty $52,x
$5c3b: lda #$01 / $5c3d: sta $5a,x
$5c3f: lda #$00 / $5c41: sta $56,x
$5c43: lda $6532,y / $5c46: ora #$80
$5c48: sta $2a00                                   ; *** WRITE 2: sample | $80 ***
$5c4b: lda $6567,y / $5c4e: and #$0f
$5c50: ora $5c63,x
$5c53: sta $2a00                                   ; *** WRITE 3: volume | voicemask ***
$5c56: lda $659c,y / $5c59: sta $5e,x
$5c5b: jsr $5c87
$5c5e: pla / $5c5f: tax / $5c60: plp
$5c61: sec / $5c62: rts
```

This is a byte-for-byte match for MAME's two-byte `okim6295_device::write` protocol
(`okim6295.cpp:232`): first byte `sample | 0x80` sets the pending sample, second byte
`voicemask << 4 | volume` starts the voices. [M] Our `oki6295.c` parses the second byte
correctly (`voicemask = command >> 4`, `vol_idx = command & 0x0F`). [C]

### 3.2 Class 20, `$5C6F` — the OKI stop [C]

```
$5c6f: php / $5c70: sei
$5c71: ldx #$03
$5c73: ldy $52,x / $5c75: cmp $6532,y / $5c78: beq $5c7f
$5c7a: dex / $5c7b: bpl $5c73 / $5c7d: plp / $5c7e: rts
$5c7f: lda $5c6b,x / $5c82: sta $2a00               ; *** stop that voice ***
$5c85: plp / $5c86: rts
```
Command `$66`, argument `$30` = 48, stops whichever voice is playing sound index 48
(sample `$2A`). Matches MAME's stop path (`voicemask = command >> 3`; `$5C6B = 08 10 20 40`,
so `0x08 >> 3 == 1` = voice 0). [M]

### 3.3 The five tables

```
$5C63 voice-start mask   : 10 20 40 80      ; == 1 << (4 + voice)
$5C67 voice status bit   : 01 02 04 08      ; ANDed against /RDV at $2800
$5C6B voice-stop byte    : 08 10 20 40

$6100 class table   $6100..$616C  109 bytes  (indexed by command byte)
$616D argument      $616D..$61D9  109 bytes  (indexed by command byte)
$6532 sample number $6532..$6566   53 bytes  (indexed by sound index)
$6567 voice+volume  $6567..$659B   53 bytes  (indexed by sound index)
$659C priority      $659C..$65D0   53 bytes  (indexed by sound index)
```
[C] All byte-verified against the ROM.

`$6532` is a **permutation of `$01..$34` (1..52)** — exactly matching an independent
parse of the OKI6295 sample-header table in the four 1f* ROMs, which finds samples 1..52
contiguous and well-formed and entries 0 and 53..127 malformed. The two agree, which
independently validates the 8-byte header layout (3-byte BE start, 3-byte BE stop,
`& 0x3FFFF`) and the `0x3FFFF` mask already implemented at `oki6295.c:107-116`. [C]

---

## 4. The complete command → OKI6295 map

Computed directly from the five tables. `voice` from `$6567[idx] & 0x30 >> 4`,
`volume` from `$6567[idx] & 0x0F`, `locked` from bit 7. [C]

```
cmd  cls  arg | voice  sample  vol  locked | OKI byte1  OKI byte2
$05   19    0 |  v0      $01    1    -    |   $81       $11
$38   19    1 |  v0      $01    0    YES  |   $81       $10
$39   19    2 |  v0      $02    0    YES  |   $82       $10
$3A   19    3 |  v0      $03    0    YES  |   $83       $10
$3B   19    4 |  v0      $04    0    YES  |   $84       $10
$3C   19    5 |  v0      $05    0    YES  |   $85       $10
$3D   19    6 |  v0      $06    0    YES  |   $86       $10
$3E   19    7 |  v0      $07    0    YES  |   $87       $10
$3F   19    8 |  v0      $08    0    YES  |   $88       $10
$40   19    9 |  v0      $20    0    YES  |   $A0       $10
$41   19   10 |  v0      $22    0    YES  |   $A2       $10
$42   19   11 |  v0      $21    0    YES  |   $A1       $10
$43   19   12 |  v0      $23    0    YES  |   $A3       $10
$44   19   13 |  v0      $09    0    -    |   $89       $10
$45   19   14 |  v0      $0A    0    YES  |   $8A       $10
$46   19   15 |  v0      $0B    0    YES  |   $8B       $10
$47   19   16 |  v0      $0C    0    YES  |   $8C       $10
$48   19   17 |  v0      $0D    0    YES  |   $8D       $10
$49   19   18 |  v0      $0E    0    YES  |   $8E       $10
$4A   19   19 |  v0      $0F    0    YES  |   $8F       $10
$4B   19   20 |  v0      $10    0    YES  |   $90       $10
$4C   19   21 |  v0      $11    0    YES  |   $91       $10
$4D   19   22 |  v0      $12    0    YES  |   $92       $10
$4E   19   23 |  v0      $13    0    YES  |   $93       $10
$4F   19   24 |  v0      $14    0    YES  |   $94       $10
$50   19   25 |  v0      $15    0    YES  |   $95       $10
$51   19   26 |  v0      $16    0    YES  |   $96       $10
$52   19   27 |  v0      $17    0    YES  |   $97       $10
$53   19   28 |  v0      $18    0    YES  |   $98       $10
$54   19   29 |  v0      $19    0    YES  |   $99       $10
$55   19   30 |  v0      $1A    0    YES  |   $9A       $10
$56   19   31 |  v0      $1B    0    YES  |   $9B       $10
$57   19   32 |  v0      $1C    0    YES  |   $9C       $10
$58   19   33 |  v0      $1D    0    YES  |   $9D       $10
$59   19   34 |  v0      $1E    0    YES  |   $9E       $10
$5A   19   35 |  v0      $1F    0    YES  |   $9F       $10
$5B   19   36 |  v0      $24    0    -    |   $A4       $10
$5C   19   37 |  v0      $25    0    -    |   $A5       $10
$5D   19   38 |  v1      $29    0    -    |   $A9       $20
$5E   19   39 |  v1      $26    1    -    |   $A6       $21
$5F   19   40 |  v1      $33    2    -    |   $B3       $22
$60   19   41 |  v1      $2E    0    -    |   $AE       $20
$61   19   42 |  v2      $28    0    -    |   $A8       $40
$62   19   43 |  v2      $2C    1    -    |   $AC       $41
$63   19   44 |  v2      $2F    0    -    |   $AF       $40
$64   19   45 |  v2      $2D    0    -    |   $AD       $40
$65   19   46 |  v2      $30    0    -    |   $B0       $40
$67   19   47 |  v3      $27    1    -    |   $A7       $81
$68   19   48 |  v3      $2A    0    -    |   $AA       $80
$69   19   49 |  v3      $32    0    -    |   $B2       $80
$6A   19   50 |  v3      $31    1    -    |   $B1       $81
$6B   19   51 |  v3      $34    0    -    |   $B4       $80
$6C   19   52 |  v3      $2B    1    -    |   $AB       $81

$66   20   48 | OKI STOP of the voice playing sound index 48 (sample $2A)
```

The 53 arguments are exactly `0..52` — every class-19 command lands in range, and the
three 53-byte tables are indexed without an off-by-one. [C]

**Structure:** commands `$38-$5C` are a contiguous block of 37 one-shots all on voice 0.
Commands `$05`, `$44`, `$5D`-`$65`, `$67`-`$6C` are the 16 hand-assigned effects that pin
a specific voice (`$6567` bits 4-5) and carry a nonzero volume — engine, explosions,
loops. [I]

**`locked` is bit 7 of `$6567`, not a disable flag.** [I] Read at `$5C21`: if the voice
is busy and the sound *currently* playing on it has bit 7 set, the new play is **skipped**
rather than preempting. So bit 7 means "this sound is not preemptible". 34 of the 53
sound indices carry it; all 34 use volume nibble 0.

### 4.1 Consequence: our reversed volume table would silence 46 of 53 effects

MAME's `okim6295_device::s_volume_table[16]` (`okim6295.cpp:64-79`) is
`{0x20,0x16,0x10,0x0b,0x08,0x06,0x04,0x03,0x02,0,0,0,0,0,0,0}` divided by `0x20` — **index
0 is the loudest**, indices 9-15 are digital silence ("any volume parameter beyond the 9th
index results in silent playback"). [M]

Our `reproduction/sound/oki6295.c:35-38`:
```c
static const float s_volume_float[16] = {
    0.000,0.063,0.089,0.110,0.125,0.140,0.156,0.200,
    0.250,0.313,0.375,0.438,0.500,0.625,0.750,1.000 };
```
is the **exact reverse**: index 0 is silence, index 15 is loudest. [C]

Cross-referencing §4: **46 of the 53 effect commands use volume nibble 0** (only `$05`,
`$5E`, `$5F`, `$62`, `$67`, `$6A`, `$6C` use 1 or 2). Under our table, volume 0 ⇒ 0.000 ⇒
**digital silence**. Correcting the volume table is not cosmetic; it is the difference
between hearing the game and hearing seven effects.

---

## 5. Defects in *our* code (not original-game bugs)

Per `AGENTS.md` these are ordinary defects, not `BUGS.md` entries.

| # | File:line | Defect |
|---|---|---|
| D1 | `native/musashi_cpu.c` `REGION_SOUND` cases | No-op stubs. No 68010→JSA write handler. **RESOLVED** — see §5.3. |
| D2 | `reproduction/sound/sound_dispatch.c` | `sound_dispatch_send_command()` feeds the raw 68010 byte to `oki6295_write()`. Wrong layer; must translate through `$6100`/`$616D`/`$472F`/`$6532`/`$6567`/`$5C63`. **RESOLVED** — see §5.1. |
| D3 | `reproduction/sound/oki6295.c:8-20,69` | IMA ADPCM (89-entry step table, `(step*delta + step/2) >> 11`) instead of OKI ADPCM. `oki6295_index_table` carries the right *values* (`{-1,-1,-1,-1,2,4,6,8}` ×2) but the wrong clamp (88 vs 48). |
| D4 | `reproduction/sound/oki6295.c:35-38` | Volume table reversed — 46/53 effects become silence (§4.1). |
| D5 | `reproduction/sound/oki6295.c:56-62` + `oki6295.h:65-66` | pin7 divisor inverted: returns 1/2 instead of 132/165, and the header comment swaps the direction. |
| D6 | `reproduction/sound/sound_dispatch.c:132`, `oki6295.h:121` | 11025 Hz hardcoded; real rate is 9039.255 (pin7 high) / 7231.404 (low). `oki6295_set_pin7()` never called. |
| D7 | `reproduction/sound/oki6295.c:118` | Accepts `start == stop`; MAME requires `start < stop` (`okim6295.cpp` start path). |
| D8 | `reproduction/sound/oki6295.c:161` mix | Mixes at full int16 scale; MAME emits ±2048 and normalizes with `stream.add_int(..., 2048)`. |
| D9 | `reproduction/sound/oki6295.c:161` loop | `if (byte_idx >= count/2) { playing = false; continue; }` — MAME does `if (++m_sample >= m_count) { m_playing = false; break; }`. |
| D10 | `backend/sdl_audio.c`, `backend/sdl3_audio.c` | Each carries a duplicate IMA decoder with a further bug: `ima_index_table[nibble & 0x07]` drops the sign bit, so entries 8-15 are unreachable. |
| D11 | `backend/backend_registry.c:124,158` | `calloc(1, 256)` for per-instance audio state; `sdl_audio_backend_t` is larger. Struct overflow. |
| D12 | `backend/sdl_audio.c` | Uses legacy `SDL_OpenAudio` and never sets `b->dev`, so `sdl_audio_shutdown`'s `b->dev != 0` test never fires — the device is never closed. **RESOLVED** — shutdown now pauses and closes the legacy device. |
| D13 | `native/CMakeLists.txt:59-66` | Optional CTest slice references `sound_queue.c` / `sound_queue_test.c`, which do not exist. Dead target. |
| D14 | — | `oki6295.c` and `sound_dispatch.c` have **no CTest at all**. Only `jsa-latch-slice-c` exists. **SUPERSEDED** — see §5.1. |
| D15 | `reproduction/sound/sound_dispatch.c` `sound_dispatch_frame()` | Read the command latch with `stunrun_jsa_peek_command_byte()`, which takes a `const stunrun_jsa_latches_t *` and deliberately does not clear `command_pending`. One latched command was therefore re-dispatched on **every subsequent frame** for the life of the process — a permanently retriggering sound effect. **RESOLVED** — see §5.1. |
| D16 | `reproduction/sound/oki6295.c:101` (`oki6295_init`) | Residual of D6, not fixed by it: `pin7_state = 0` at init, and `sound_dispatch_init()` never calls `oki6295_set_pin7()`. MAME configures the JSA-II chip `PIN7_HIGH` (`atarijsa.cpp:901`), so everything plays at **7231 Hz** instead of 9039 Hz. The 6502 toggles the line through WRIO bit 3 (`m_oki1->set_pin7(data & 8)`, §3). **RESOLVED** — see §5.3. |
| D17 | `backend/sdl_audio.c:213-216` | Ring capacity counted stereo frames, but only that many `int16_t` elements were allocated while read/write index two per frame. Real PCM writes overran the heap. **RESOLVED** — allocate two channels per ring frame; CTest `native-shell-oki-audio-probe` exercises the real backend. |
| D18 | `backend/backend_registry.c:234-237` | Shutdown freed audio backend state without invoking its `destroy` callback, leaking backend-owned buffers; callback remained active because SDL device was not closed. **RESOLVED** — audio destroy runs after shutdown and before state free. |

### 5.1 Status of D2 and D15 (dispatch-bridge slice, 2026-10-10)

Both are closed by routing `sound_dispatch.c` through `oki_cmdmap.c`, with
`reproduction/sound/sound_dispatch_test.c` (CTest `sound-dispatch-slice-c`,
142 checks) as the proof. The test was verified to **fail** with either defect
reintroduced:

| Defect reintroduced | Result |
|---|---|
| D15 (peek instead of consume) | **36 of 142 checks fail.** `commands_dispatched` 8 instead of 1, `oki_bytes_written` 24 instead of 3, the voice's ADPCM read pointer stuck at 256 instead of 2048, and every PCM block from frame 2 on byte-identical to frame 1 — the literal signature of a sample reloading from the top once per frame. |
| D2 (`oki6295_write(&d->oki, cmd_byte)`) | **43 of 142 checks fail.** `$38` emits 1 OKI byte instead of 3, no voice starts, `base_offset` 0 instead of 424, nothing is enqueued. |

**Consume-on-dispatch, not a queue.** `sound_dispatch_frame()` now calls
`stunrun_jsa_sound_command_read()`, which clears `command_pending` and
`nmi_asserted` exactly as the real `sound_command_r` at `$2802` does
(`jsa_latch.c:50`). The firmware's NMI handler at `$57E3` consumes the byte
exactly once; a second dispatch has no counterpart on the board, so it is not
an approximation of anything. A queue was rejected because
`stunrun_jsa_latches_t` is a one-byte holding register, not a FIFO, and
`stunrun_jsa_main_command_write()` overwrites rather than appends — a queue
inside `sound_dispatch_t` would invent storage the hardware does not have.
**What remains approximate is the timing, not the count:** the real consume
point is NMI time, and the frame loop can consume at most one byte per video
frame, so a command arriving mid-frame is served up to one frame late. That is
a stated proxy, recorded at the decision site in `sound_dispatch.c`.

`stunrun_jsa_peek_command_byte()` is left in place; it is public API for
observation and the caller was the defect, not the peeker.

**`oki_cmdmap_note_play()` is deliberately NOT called from the bridge.**
`oki_cmdmap_translate()` already performs the `$52..$55` / `$2808` bookkeeping
inside `translate_play()` and `translate_stop()` (`oki_cmdmap.c:192-193`).
Calling it again would be redundant at best and actively wrong at worst:
`translate()` returning 0 means the 6502 deliberately started nothing — gate
closed at `$5C61`, voice locked and busy at `$5C59`, or `$66` naming a sample
that is not playing — and a `note_play()` call would then mark a voice busy
that the ROM never touched, poisoning every later command wanting that voice.

**Known limitation, not modelled:** the handler at `$5C2F-$5C35` polls the
chip's `/RDV` line and re-issues the stop write until the voice really is idle
before reloading it. We model `/RDV` from our own bookkeeping and never feed
the chip's actual "did the voice start" answer back into `d->cmdmap`. If the
chip refuses a sample (`start >= stop`, or a voice it still considers playing)
`cmdmap` would still believe that voice is busy. This is bounded: the
dispatcher at `$4371` does `cpy #$6D`, so only commands `$00-$6C` are
reachable, and **every one of the 53 class-19 commands — all of which are
reachable — names an OKI sample with a well-formed header (`start < stop`)**,
measured in full in §5.2. No reachable command triggers the divergence.

### 5.2 Measuring the command tables: both are exactly 109 entries

Needed to close the `/RDV` limitation above, so recording it here rather than
in `BUGS.md` — it is a measurement gotcha, not a ROM defect.

```
class   table  $6100 .. $616C   (109 entries)
argument table $616D .. $61D8   (109 entries)
dispatcher     $4371: cpy #$6d / bcs $4389   ; command >= $6D -> A untouched, RTS
```

Decoding both tables over their true 109-entry extent:

| Measurement | Value |
|---|---|
| class-19 ("OKI play") entries | **53** |
| …of those, reachable (`< $6D`) | **53 — all of them** |
| distinct arguments across the 53 | **53** |
| distinct OKI samples named | **52** (sample `$01` twice — BUGS.md **SR-17**) |
| class-19 commands naming a malformed OKI sample | **0** |
| class histogram (both tables) | `0`×2, `1`×6, `3`×1, `5`×2, `8`×32, `9`×1, `10`×5, `11`×2, `19`×53, `20`×1, `255`×4 |

Two traps in that table, both of which bit this slice before it was caught:

**The class table is 109 entries, not 256.** A script that iterates commands
`0x00..0xFF` and reads `$6100 + cmd` silently keeps going past `$616C` and
reads *argument-table bytes* as if they were class bytes. Doing that yields
"55 class-19 entries", inventing dead commands `$95` and `$B7` — the second
of which appears to name sample `$B8`, whose OKI header at concatenated offset
`$5C0` really is malformed (`start $00E149 > stop $001A82`), because that
region is past the last populated sample (`$34` ends at `$03FCA7`). It is a
convincing false positive: every individual number in it checks out, and only
the table bound is wrong. Bounding the loop by the 109 entries removes it.
Nothing was filed in `BUGS.md`, per the rule there that a wrong entry is worse
than no entry.

**"53 commands, 52 samples" is SR-17, not a second duplicate.** The duplicate
sample is `$01` at arguments 0 and 1 (`$05` and `$38`). The class *histogram*
also reads 53 for the raw table and for the reachable range, because every
class-19 entry is reachable — there is no dead class-19 entry to account for.

### 5.3 Status of D1 and D16 (transport slice, 2026-10-10)

Both are closed by `native/jsa_transport.{h,c}` plus the wiring in
`native/musashi_cpu.c` and `native/shell.c`, with
`native/jsa_transport_musashi_test.c` (CTest `jsa-transport-musashi-c`) as the
proof. The test drives the **real Musashi core**: it plants synthetic
`move.b` instructions in main RAM at `0x00FFF000` and runs them, so the address
routing under test is the routing the game actually uses, not a hand-call of
the transport. It additionally verifies the ROM bytes it imitates when
`STUNRUN_JSA_TRANSPORT_ROMDIR` points at `roms/stunrun`.

**What the MAME wiring actually is.** `harddriv.cpp:4806` installs *one*
read/write handler pair across the whole `$600000-$603FFF` window with
`mem_mask = 0xff00`:

```
install_readwrite_handler(0x600000, 0x603fff,
    read8smo_delegate(*m_jsa,  FUNC(atari_jsa_base_device::main_response_r)),
    write8smo_delegate(*m_jsa, FUNC(atari_jsa_base_device::main_command_w)),
    0xff00);
```

There is **no offset decode**: the same address is a command write on the write
path and a response read on the read path. **[M]**

**So `stunrun_jsa_classify_read()` / `stunrun_jsa_is_response_reg()` do not
apply here, and the brief was wrong to ask for them.** Those two helpers
(`jsa_latch.c:4`, `jsa_latch.c:16`) classify the **6502-side** registers —
`$2802`/`$280A` command, `$2804` rdio, `$2806` IRQ ack, `$2A02` response — and
those addresses never appear in the 68010's address space. On the 68010 side
there is no classification to make: a store inside the window is a command, a
load inside the window is a response. The transport is written to that fact
rather than to a classifier that would have been a fiction.

**The byte lane is `0xFF00`, and that is confirmed, not chosen.** A 68000/68010
byte access to an even address drives D8-D15, so MAME reports `mem_mask ==
0xFF00` — which is precisely the `mem_mask` hardcoded in `harddriv.cpp:4806`.
It also matches the recorded tap in
`reference/experiments/stunrun/sound-boundary-tap.metadata.json`: frame 447,
PC `0x023EF6`, `bus_data 0x1E1E`, `mask 0xFF00`, `candidate_byte 0x1E`. **[C]**
The consequence, which the test asserts explicitly: for a store of `0xAABBCCDD`
with mask `0xFF00` the correct lane is **`0xCC`**, not `0xAA`. A wrong mask
(`0x00FF` at an even address) latches nothing.

`stunrun_jsa_command_byte()` itself was **not** widened: `jsa_latch_test.c:39-43`
pins `stunrun_jsa_command_byte(0x1E1E, 0x00FF) == STUNRUN_JSA_NO_BYTE`, and that
is the correct behaviour for the extractor, which only ever saw the one observed
mask. The odd-address lane (`0x00FF`) is generalised in `jsa_transport.c` as a
private `lane_byte()` helper, because on a 68010 it is what the CPU actually
drives — `m68k_op_move_8_ai_al()` passes the byte value in the low 8 bits and
carries the lane in `ea & 1`.

**Widths 2 and 4 are a documented inference, flagged as such.** `map().rw()`
defaults to `AMODE_DEVICE_WIDTH_8BIT`, so MAME splits a multi-byte access into
per-byte handler calls in big-endian order and the single-byte holding register
keeps the last one. This could **not** be confirmed from source: `src/emu/memory.h`
and `src/emu/memory.cpp` are trimmed out of *both* MAME trees, so
`memory_device::write`'s split/merge arithmetic was never read. **[I]** The
transport splits the same way and says so at the decision site in
`jsa_transport.c`. It costs nothing in practice: the ROM never issues a
`move.w`/`move.l` to `$600000` (§14.2 of `mc68010-boot-findings.md` records that
every 68010 access to the window is a byte access), so no observed behaviour
depends on it.

**Read behaviour, and the response direction.** Reads never touch
`command_pending` — that is the property the test asserts, and it is also the
hardware behaviour, since `main_response_r` and `main_command_w` are separate
latches. The response *producer* is not modelled: `stunrun_jsa_main_response_write()`
is wired and reachable, but nothing calls it because the 6502 is not emulated
(non-goal). So reads return open bus (`0xFF`, widened to `0xFFFF`/`0xFFFFFFFF`)
— byte-for-byte the value the old stub returned. **The response direction is
therefore not needed for audio** and was deliberately left unexcited rather than
invented. Reinstating it costs one call.

Verified to fail when the defects are reintroduced (counts measured, not estimated;
the test reports its own total the way `oki6295_test.c` does):

| Defect reintroduced | Result |
|---|---|
| D1 (`write8` `REGION_SOUND` back to `break;`) | **25 of 220 checks fail.** `latch byte after store == 0x38 (got 0, want 56)`, `command_pending` and `nmi_asserted` never set, `stunrun_jsa_sound_command_read() == -1 (want 30)`, and the whole end-to-end block collapses: `commands_dispatched 0`, `oki6295_is_playing 0`, `samples_enqueued 0`, `stream_writes 0`. |
| D16 (`pin7_state = 0` in `oki6295_init`) | **3 of 220 checks fail** in `jsa-transport-musashi-c` (`last_sample_rate == 9039 (got 7231)`, plus the two direct `oki6295_sample_rate` assertions) and **1 of 146 checks fail** in `oki6295-slice-c` — the dedicated `default PIN7 is HIGH (atarijsa.cpp:901) -> 9039 Hz` assertion. |

Without `STUNRUN_JSA_TRANSPORT_ROMDIR` the same binary runs 119 checks and passes;
with it, 220.

**What the live run still does not do.** `native/shell.c` binds the live
dispatcher's latches — `musashi_cpu_set_jsa_latches(g_cpu,
g_sound_dispatch_active ? &g_sound_dispatch.latches : NULL)` — and a headless
700-frame run prints `shell: sound-dispatch stats frames=701 commands=0
oki-bytes=0 samples-enqueued=0 sample-rate=9039`. `commands=0` is **not** a
transport defect: a PC histogram over the same run contains **no entry at all**
for `$023E96-$023F02` — the sound-command queue routine is never called. The
68010 is still in the ADSP `$608000` program-RAM handshake poll
(`$022414`/`:$00069A`, 262144 and 230400 hits) and delay loops (`$0209E0`, 1.7M
hits) at frame 700. The transport is exercised end-to-end by the CTest through
the real core; what it is not yet exercised by is a boot that reaches the
routine.

Note that the shell's `shell: frame=447 command-byte=0x1E nmi=handled` line is
**not** evidence of this: it is the shell self-check block (`shell.c:1344-1355`)
replaying the recorded frame-447 constants into its own local
`stunrun_jsa_latches_t`, which this slice left untouched.

The queue routine gates its store on `btst.b #$f,$a80000.l` (`$023EB0`). The
old `fake_ports.c` default set the whole input word to zero, holding this gate
closed. This was not a hardware mystery: MAME's `PORT_BIT(0xfff8, IP_ACTIVE_LOW,
IPT_UNUSED)` uses `IP_ACTIVE_LOW` as the field default value (`ioport.h:45`,
`ioport.cpp:707`), masked to `0xfff8`. The native default is now `0xfff8`,
preserving the existing clear low three button bits while setting unused bits
to their MAME idle level. `fake-ports-slice-c` asserts the value and bit 15.
This removes the known input-gate blocker, but does not establish that the
ADSP upload/boot handshake completes or that a live boot reaches the queue;
those remain runtime questions, not grounds for assuming the entire producer
is implemented.

**Backend-only probe.** To separate audio output from that boot blocker,
`native/shell.c` accepts the opt-in `STUNRUN_AUDIO_PROBE_COMMAND` environment
variable after the real OKI ROM and backend are initialized. For example,
`STUNRUN_AUDIO_PROBE_COMMAND=0x38` asks the existing JSA command mapper to
start the real ROM-mapped engine effect directly, then the ordinary frame loop
decodes and submits its PCM through the selected backend. This deliberately
bypasses the MC68010 producer and is diagnostic only; it does not claim that
gameplay currently generates sound. With the variable unset, normal behavior
is unchanged.

The SDL2 ring was also found to allocate one `int16_t` per stereo frame while
indexing two. The backend now allocates both channels, closes the legacy SDL
device before destroying state, and invokes the audio backend's `destroy`
callback. When SDL2 and the local ROM set are present, CTest
`native-shell-oki-audio-probe` runs this exact real-ROM path with SDL's dummy
audio driver and requires a positive `samples-enqueued` count; the observed run
queued 4800 samples without a shutdown crash. This proves PCM reaches the SDL
backend queue, not that normal gameplay currently produces commands.

**Playback-rate caveat:** `sound_dispatch_frame()` currently fills a fixed 256
samples per emulated frame (60 Hz), while the OKI stream declares 9039 Hz; the
SDL2 backend opens at 48000 Hz and currently ignores each buffer's
`sample_rate`. Thus queue delivery is proven, but correctly paced/resampled
audible output is not. The follow-up must make frame production and SDL device
conversion honor the source clock before claiming faithful playback.

---

## 6. What a correct OKI ADPCM decoder must do

Transcribed from `mame-ref/src/devices/sound/okiadpcm.cpp`, class `oki_adpcm_state` —
this is the codec, not IMA. [M]

```c
static const int8_t index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };   /* 49 steps */
static int16_t diff_lookup[49 * 16];

static void compute_tables(void) {
    static const int8_t nbl2bit[16][4] = {
        { 1,0,0,0},{ 1,0,0,1},{ 1,0,1,0},{ 1,0,1,1},
        { 1,1,0,0},{ 1,1,0,1},{ 1,1,1,0},{ 1,1,1,1},
        {-1,0,0,0},{-1,0,0,1},{-1,0,1,0},{-1,0,1,1},
        {-1,1,0,0},{-1,1,0,1},{-1,1,1,0},{-1,1,1,1} };
    for (int step = 0; step <= 48; step++) {
        int stepval = floor(16.0 * pow(11.0 / 10.0, (double)step));
        for (int nib = 0; nib < 16; nib++)
            diff_lookup[step*16 + nib] = nbl2bit[nib][0] *
                (stepval * nbl2bit[nib][1] + stepval/2 * nbl2bit[nib][2]
               + stepval/4 * nbl2bit[nib][3] + stepval/8);
    }
}

int16_t clock(uint8_t nibble) {          /* reset(): signal = 0, step = 0 */
    m_signal += diff_lookup[m_step * 16 + (nibble & 15)];
    if      (m_signal >  2047) m_signal =  2047;
    else if (m_signal < -2048) m_signal = -2048;
    m_step += index_shift[nibble & 7];
    if      (m_step > 48) m_step = 48;
    else if (m_step <  0) m_step =  0;
    return m_signal;
}
```

Nibble order (`okim6295.cpp:342`):
```c
int nibble = rom.read_byte(m_base_offset + m_sample / 2) >> (((m_sample & 1) << 2) ^ 4);
```
— `m_sample` even ⇒ shift 4 (high nibble first); odd ⇒ shift 0. **Fixed** — the previous
`oki6295.c` read the low nibble first; `oki6295.c:280` now uses the same expression as MAME
(`rom_byte >> (((v->sample & 1) << 2) ^ 4)`), with `oki6295_test.c` asserting the ordering
against the real ROMs.

`oki_adpcm2_state` is a different chip variant (`index_shift { -2,-2,-2,-2,2,6,9,11 }`,
`floatstep = 64`, `1.08^n`, `signal` seeded to `-2`). Not used by JSA-II. [M]

---

## 7. Open questions

- Class 8 (`$4569`, 32 commands) — the YM2151 music/stream sequencer. Not transcribed.
  This is the largest remaining piece and the only thing between us and music.
- **Whether `$1E` is ever non-zero at the moment an effect is wanted (§2.5).** The gate is
  confirmed and its two driving commands are confirmed, and our bridge now propagates it
  (`sound_dispatch_t.cmdmap`, §5.1) — but the 68010-side ordering is still unknown, so we
  reproduce the command stream without knowing whether the stream it will receive wants effects
  enabled at any particular moment. If that ordering turns out to mute effects during normal
  play the failure mode is total silence, not wrong samples.
- Whether `$0234` (queue mode) is ever set non-zero (§2.6). The ring has no reader, so
  it does not affect the OKI path either way.
- Classes 2, 3, 4, 5, 6, 7, 9, 10, 11, 13, 14, 16, 17 — handler targets identified, not
  transcribed.
- `$659C` (priority) feeds `$5E,x` and the voice-share divisor at `$5C87`; its effect on
  which voice wins a contention is not modelled.

---

## 8. Method

Disassembled `roms/stunrun/136070-2123.10c` with capstone
(`CS_ARCH_MOS65XX` + **`CS_MODE_MOS65XX_6502`** — note the constant is `MOS65XX_6502`, not
`MOS65XX_02`), `skipdata = True`. Two rules that mattered:

1. **Dump raw bytes and verify the layout before trusting a linear sweep.** These ROMs
   interleave code and data tables; capstone desynchronizes across a table and every
   address after it is wrong. Every table in §3.3 was read as raw bytes first.
2. **Do not infer from a sweep that misses a target.** Early scans for the `$0200` queue
   produced hundreds of false hits because opcodes `$B1`/`$91` are *indirect* `(zp),y`,
   not `zp,y`; and `$0200-$024F` is reached with **absolute** addressing (`$AC 34 02` =
   `ldy $0234`), not zero-page. Both errors produced wrong conclusions until corrected.

Scratch tooling used (not repository files): `dis6502.py`, `findports.py`, `zp.py`,
`oki_probe.c`, all under `/tmp/opencode`.
