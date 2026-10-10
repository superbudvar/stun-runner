# MC68010 boot: findings log

## Current state and reading guide (2026-10-09)

This is the authored interpretation record, not a raw trace. The numbered
sections preserve assembly listings, commands, measurements and earlier wrong
turns; their historical test counts and frame comparisons are not a fresh
acceptance verdict. Read this summary before using those sections as guidance.
Paths below are repository-root-relative unless stated otherwise. Companion:
[MAME driver findings](mame-stunrunner-findings.md), with its own supersession
notice; [JSA-II sound findings](jsa2-sound-findings.md) for the 6502 sound CPU and
the OKI6295 decode chain; [harness guide](../stun-runner/docs/mame-harness-guide.md).

### Machine and host bus

- **68010 RAM: 32 KiB at `$FF8000-$FFFFFF`**, not 512 KiB. See §9 and
  `stun-runner/native/fake_ports.h` (`STUNRUN_MAIN_RAM_BYTES = 0x8000u`).
- **GSP `gsp_vram` backing share: 512 KiB**, not 4 MiB; the TMS34010 map uses
  bit addresses. See §12.1. A program-space dump is not a backing-share dump.
- **68010 host decode: `(byte_offset >> 2u) ^ 1u`**. The main bus supplies a
  word offset to MAME, whose handler applies `(offset / 2) ^ 1`. See §3.1,
  §13 and `stun-runner/native/musashi_cpu.c:gsp_host_reg`.
- The native host bus models writes and display memory; it **does not execute
  GSP instructions**. No frame-total or memory-byte match proves GSP execution.

### Boot, palette and fills

- Input definitions and boot listings: §§0–2. The service-bit polarity conclusion
  in §1 is refuted; preserve the trace, not the proposed idle override.
- RAM march and corrected oracle range: §9/§9a. Display-share layout, palette
  and historical raster comparison: §12. Runtime timing is not established by
  either hand-derived slice tests or screenshot similarity.
- `$0530`: address latches, not four FIFO seed words (§11.10), implemented by
  `stunrun_gsp_boot_latch_draw_parameters()` in `stun-runner/native/gsp_boot.c`.
- `$0618-$0634` is inside the existing
  `stunrun_gsp_boot_walk_palette()` slice, not a missing standalone stub (§11.12).
- `$0C28` has a seed of 15 followed by `SUBQ/BNE`, hence **15 helper calls**,
  not 16. `$0C46` has 32 iterations. The isolated native caller slice and an
  original-ROM/MAME differential now agree for both host auto-increment modes;
  see §11.9 and `gsp_display_list_musashi_test.c`.
- `$0C64/$0C8A`: **boundary verification still needed**. The retained listing
  stops at `$0CA6` without a return from `$0C8A`; a full returning 241-pass
  ramp and its total FIFO sequence are not yet established.

### Upload producers and compression interpreter

- Current producer evidence and caller slices: §11.0–§11.0b. The caller
  differential test uses the **original ROM `$225E4` compiler callback**;
  it establishes caller address/word and selected RAM effects, not parity of
  `stunrun_gsp_display_list_run()` with the ROM compiler.
- **C compiler word fidelity remains unresolved.** Historical claims below
  that `MOVE.B` into a data register clears its upper bits are false: it replaces
  only the low byte. Consequently the alleged dead word loads, `$00bb`-only
  table output, discarded high bytes and associated original-ROM defect claims
  are not valid guidance. The current C implementation still contains that
  assumption; passing hand-derived tests does not repair it.
- Static JSR byte-pattern counts and ROM-bank interpretation in §11.1/§11.11
  are historical candidate evidence only. Runtime producer attribution in
  §11.0 supersedes treating raw hits as executed callers; do not build a new
  disassembly programme from those counts.

### Evidence and refutation index

| Earlier claim | Disposition / usable evidence |
|---|---|
| Service bit must be clear at idle (§1) | Refuted polarity inference; retain PC/write logs and boot listing, not the override. |
| Main RAM is 512 KiB (§3.6, old §7) | Refuted by map arithmetic and corrected 32 KiB oracle (§9). |
| GSP share is 4 MiB (§3.5, old §7/§10) | Refuted by bit-addressed mapping and backing-share extraction (§12.1). |
| `$0530` emits four `$2608…` data words (§10) | Refuted: those are address latches (§11.10). |
| `MOVE.B` clears data-register upper bits (§11.4–§11.7, §11.13) | Refuted instruction semantics; compiler word mismatch remains open. |
| `$0C28` makes 16 calls (§11.9) | Refuted loop count: 15 under the listed `SUBQ/BNE`. |
| `$0C8A` is a proven returning ramp helper | Unsupported without the missing boundary/return; do not adopt the total. |
| Host decode should be `(byte_offset >> 1) ^ 1` (§13) | Refuted; correct decode remains `(byte_offset >> 2) ^ 1`. |

Evidence comes from `mame-ref/` (historical MAME 0.289 observations), native
source and local de-interleaved ROM listings. MAME line references below are
to that source snapshot, not stable symbols. Raw `/tmp` paths record original
provenance and may no longer exist; commands are retained to permit regeneration.
Commercial ROM contents are not embedded here. The ROM manifest and listing
recipe must accompany any renewed acceptance run.

## Historical evidence, grouped by subsystem

The original numbered anchors are retained for existing references: machine /
boot (§§0–7), oracle and RAM (§§8–9), GSP memory (§10/§12), uploads and assembly
inventory (§11), and rejected host-decode hypothesis (§13).

## 0. Which MAME port definitions apply

STUN Runner has its **own** input block:

- `mame-ref/src/mame/atari/harddriv.cpp:1084` — `INPUT_PORTS_START( stunrun )`,
  ending `:1170`. Used by `GAME( 1989, stunrun, 0, stunrun_machine, stunrun, ... )`
  at `harddriv.cpp:5375`.

**Do not use `INPUT_PORTS_START( harddriv )` (`harddriv.cpp:789`)** — it has
different defaults and it is what sent me down a wrong path twice. (Its SW1
DIPs default to their own mask → 0xFF; STUN Runner's default to 0x00.)

Transcription of `INPUT_PORTS_START(stunrun)`:

| port | bit | definition | idle value |
|---|---|---|---|
| `mainpcb:IN0` (60c000) | 0x01 | "Diagnostic jumper" `PORT_DIPNAME(0x01, 0x01, ...)`, DEF_STR(Off)=0x01 | 1 |
| | 0x02 | `/HBLANK` `IP_ACTIVE_LOW` `IPT_CUSTOM` | 1 outside hblank |
| | 0x04 | `/VSYNCB` `IP_ACTIVE_LOW` `PORT_READ_LINE_DEVICE_MEMBER("mainpcb:screen", screen_device::vblank)` | 1 outside vblank |
| | 0x08 | 12-bit EOC `IP_ACTIVE_HIGH` | 1 |
| | 0x10 | `PORT_READ_LINE_DEVICE_MEMBER("mainpcb:adc8", adc0808_device::eoc_r)` | dynamic |
| | 0x20 | **`PORT_SERVICE( 0x20, IP_ACTIVE_LOW )`** | **← the boot-critical bit** |
| | 0x40 | `IPT_COIN2` `IP_ACTIVE_LOW` | 1 |
| | 0x80 | `IPT_COIN1` `IP_ACTIVE_LOW` | 1 |
| `mainpcb:SW1` (60c002) | all 8 | `PORT_DIPNAME(mask, 0x00, ...)` → **default 0x00** | 0x00 |
| `mainpcb:a80000` | 0x0001 | `IPT_BUTTON1` ACTIVE_LOW | 1 |
| | 0x0002 | `IPT_BUTTON2` ACTIVE_LOW | 1 |
| | 0x0004 | **`IPT_START1` ACTIVE_LOW** (not COIN3) | 1 |
| | 0xfff8 | `IPT_UNUSED` ACTIVE_LOW | ? |
| `mainpcb:8BADC.0` | 0xff | `IPT_AD_STICK_X` default 0x80 | 0x80 |
| `mainpcb:8BADC.2` | 0xff | `IPT_AD_STICK_Y` default 0x80 | 0x80 |
| `mainpcb:8BADC.1/3..7` | 0xff | `IPT_UNUSED` | ? |
| `mainpcb:12BADC.*` | 0xfff | `IPT_UNUSED` | ? |

`hd68k_port0_r()` — `harddriv_m.cpp:201-224`:

```c
int temp = (m_sw1.read_safe(0xff) << 8) | m_in0->read();
if (get_hblank(scr)) temp ^= 0x0002;
temp ^= 0x0008;     /* 12-bit EOC always high for now */
```

so the IN0 byte the ROM sees has **bit 3 forced to 0** by the final XOR.
`hd68k_a80000_r()` — `harddriv_m.cpp:196-199` — returns `m_a80000->read()` raw.

## 1. The boot loop (historical; polarity conclusion refuted)

Disassembly of the boot entry (`STUNRUN_DISASM=0x200:0x330`):

```
000212: stop   #$2700
000216: lea    $0.w, A0
00021a: movea.l ($0,A0), A7        ; SSP = long at 0x0000
00021e: movec  A0, VBR
000222: clr.w  $60c000.l           ; IRQ ack
000228: reset
00022a: clr.w  $608000.l           ; watchdog
000230: clr.w  $60c000.l
000236: clr.w  $818060.l           ; ADSP control
00023c: move.w $604000.l, D0
000242: move.b $600000.l, D0
000248: clr.w  $840000.l
00024e: clr.w  $84c000.l
000254: btst   #$5, $60c001.l      ; <== IN0 bit 5 (service)
00025c: beq    $2a0
000260: move   #$2f00, SR          ; all interrupts masked
000264: jsr    $3f8.l             ; GSP init
00026a: jsr    $570.l             ; palette cube
000270: clr.w  $608000.l
000276: move.w #$0, D0
00027a: jsr    $636.l             ; 2bpp VRAM fill
000280: clr.w  $608000.l
000286: lea    $20000.l, A0       ; jump table
00028c: movea.l ($0,A0), A7
000290: movea.l ($4,A0), A1
000294: movec  A0, VBR
000298: clr.w  $60c000.l
00029e: jmp    (A1)               ; -> 0x216, i.e. back to the top
0002a0: btst   #$0, $60c001.l
0002a8: bne    $2b2
0002ac: jmp    $8fee.l
0002b2: jmp    $506c.l             ; the real boot
```

**Mechanism.** `$20004` holds `$00000216`, so `jmp (A1)` at 0x29e returns to
0x216 and the whole block re-runs. The only exit is the `beq $2a0` at 0x25c,
which needs **IN0 bit 5 clear**. With bit 5 set the emulator re-runs the entire
GSP init (900×256 VRAM fill + palette cube + 2bpp fill) roughly 14 times in
600 frames — 3.8M GSP host writes instead of 234k — and never reaches the game
loop. That is the symptom that looked like "the CPU is stuck at 0x69c".

**Refuted proposed fix.** This investigation forced IN0 bit 5 low and observed
the following changed trace. `IP_ACTIVE_LOW` does not imply an idle zero:
inactive service reads high. The experiment is evidence of branch selection,
not proof that the forced value models the idle original machine.

Measured after the fix (`STUNRUN_PCLOG=/tmp/sr_pc2.txt`):

```
gsp_writes=234402 vram=233875 vram2bpp=0 pal_lo=256 pal_hi=256 control=4 unmapped=11
frame   0  PC=0x000054AC
frame  50  PC=0x000060D8
frame 200  PC=0x00005A10
frame 600  PC=0x00005A68
```

MAME for comparison (`/tmp/mame_pc2.txt`):

```
frame  1  0x0000069C     <- in the VRAM fill
frame 33  0x0000066A     <- 2bpp fill
frame 39  0x00022416     <- GAME
frame 84  0x00022450
frame 93  0x000234C2
frame 600 0x00022490
```

## 2. Where we still diverge: the diagnostics screen

**Historical boot snapshot, not the current blocker list.** The listing and
port-read question survive; the later attract-mode observations supersede the
claim that the port remains stuck on this diagnostics path.

MAME goes 0x22416 by frame 39. We do not get past ~0x6000. The boot at
`0x506c`→`0x50b0` (`STUNRUN_DISASM=0x506c:0x5180`):

```
0050b0: movea.l #$ffffc000, A7
0050b6: jsr    $570.l            ; palette cube
0050bc: bsr    $552c
0050c0: move   #$2400, SR        ; unmask interrupts
0050c4: move.w #$e, D0
0050c8: jsr    $65e0.l
0050ce: move.w #$0, D0
0050d2: jsr    $636.l            ; 2bpp fill
0050d8: btst   #$5, $60c001.l
0050e0: beq    $50e8
0050e2: jmp    $260.l
0050e8: btst   #$2, $a80001.l    ; <== a80000 bit 2 = IPT_START1
0050f0: bne    $50f8             ;   SET -> diagnostics
0050f2: jmp    $0f94.l           ;   CLEAR -> the game
0050f8: jsr    $4dae.l           ; diagnostics
0050fe: btst   #$5, $60c001.l  / beq $510e / jmp $260
00510e: btst   #$2, $a80001.l / bne $511e / jmp $0f94
005111e: jsr  $fbdc.l
005124: btst  #$5, $60c001.l  / beq $5134 / jmp $260
005134: btst  #$2, $a80001.l / bne $5144 / jmp $0f94
005144: jsr  $570.l
00514a: move.w $8090.w, D0 / cmpi.w #$5555,D0 / bne $5164   ; ADSP status
005156: move.w #$53,D0 / jsr $2326                          ; "pass"
005164: move.w #$54,D0 / jsr $2326                          ; "fail"
00516e: move.w $8092.w, D0 / cmpi.w #$5555,D0 ...            ; ditto
```

`btst #$2,$a80001` is the same test at three points; each time clear ⇒ `jmp
$0f94` (game) and set ⇒ the next diagnostic step. The repeating shape
(`btst #5` / `jmp $260`) is the same "return to top of boot" back-edge as
0x25c.

**Open:** bit 2 of the byte at 0xa80001 must read 0 for the game path, but
`IPT_START1` idle (not pressed, ACTIVE_LOW) is 1. Either MAME returns the other
byte half for a byte read at an odd offset inside a 16-bit handler, or
`IPT_UNUSED` bits 3..15 read 0 and the effective byte is 0x00 — which would make
`btst #2` clear. Needs an actual read of the port from MAME, not more static
reasoning.

### What the diagnostics screen does

`jsr $2326` is **not** an error handler — it is a message writer
(`STUNRUN_DISASM=0x2326:0x23c0`):

```
002326: clr.w  $608000.l              ; watchdog
00232c: andi.w #$e7ff, $c00008.l      ; clear HSTCTL auto-increment
002334: lea    $10136.l, A0
00233a: clr.l  D1 / move.w D0,D1 / lsl.l #2,D1 / adda.l D1,A0
002342: movea.l (A0), A0               ; entry = long at 0x10136 + 4*code
002344: move.b  ($2,A0), D0
002348: lsl.w  #8, D0
00234a: move.b  ($2,A0), D0
00234e: move.l  #$f4000000, $c00002.l ; GSP control_lo base
002358: move.w  D0, $c0000c.l          ; colour
...
00238c: rts
```

So messages #0x53..0x66 are drawn on the GSP. The RAM march tests
(`STUNRUN_DISASM=0x59e0:0x5b00`) walk A1=0xFFFF8000..A2=0xFFFFFFFE and report
through the palette:

```
0059e6..005a30  march C-   (lsr.w #1 mask, backwards)
005a32..005a7c  march C+   (lsl.w #1 mask, forwards)
005a7e..005abc  march D
005be0..005be4  clr.w $604000 / clr.w $604012   -> pass
005c20..005c23  clr.w $604010 / clr.w $604002   -> pass
005df0..005df6  clr.w $604010 / clr.w $604002
005dfe..005e0a  clr.w $604000 / clr.w $604012
005e48         move.w #$3f3f -> palette
005e64         tst.w D4 / move.w #$5f5f -> palette   (pass/fail colour)
005e82         move.w #$9f9f / jmp (A4)               (fatal)
005e9e         move.w #$ffff
```

Trace window output confirms the march is **passing** (D4=0) and simply slow:
The earlier timing estimate used an erroneous 512 KiB RAM size and is withdrawn.
Do not infer corrected timing from the 32 KiB map alone. The old estimate was
≈7.3M cycles / ≈55 frames per variant,
times four variants.

## 3. Bugs fixed along the way (all in `stun-runner/native/`)

1. **GSP host-port register decode.** `gsp_host_reg()` must be
   `return (byte_offset >> 2u) ^ 1u;`. MAME hands handlers a *word* index
   (`emumem_hedp.cpp:105` `>> (Width + AddrShift)`; `emumem_hea.h:30`
   `NATIVE_MASK = (1 << (Width + AddrShift)) - 1`; 68010 space Width=1
   AddrShift=0 — `m68000.cpp:2582`, `m68010.cpp:23`), then
   `harddriv_m.cpp:139-156` applies `offset = (offset / 2) ^ 1`. Mapping:
   0xC00000-03→ADDR_H, 0xC00004-07→ADDR_L, 0xC00008-0B→CONTROL,
   0xC0000C-0F→DATA. Decode proof from the ROM: `move.l #$f4800000,$c00002.l`
   at 0x3EC and `move.l #$f4000000,$c00002.l` at 0x6B4 only work if a long
   write at 0x02 splits as word@0x02 then word@0x04.
2. **Frame cycle budget.** Was one IRQ period (32768) per frame → 19.6M cycles
   at frame 600 where MAME has ~80M. Now `STUNRUN_FRAME_CYCLES 132886u`
   (8 MHz / 60.20180 Hz) with IRQ5 ticked every 32768 cycles inside the frame.
3. **IRQ5 ack.** `REGION_IRQACK = 10` for 0x60C000-0x60FFFF; reads return the
   port-0 status word, writes `musashi_cpu_set_irq(s_cpu, 0)` (Musashi's
   `m68k_set_irq` is level-sensitive). `harddriv.cpp:605` confirms the map.
4. **port-0 raster.** Added `dot_clock_advance()` (10 MHz dot / 8 MHz CPU = 5/4),
   `MUSASHI_H_TOTAL 634 / V_TOTAL 262 / H_VISIBLE 512 / V_VISIBLE 228 /
   HBLANK_POS 460`, `port0_status()` implementing `hd68k_port0_r()` exactly.
5. **GSP memory model** now matches the MAME handlers instead of storing raw
   words — banked palette, masked 2bpp expander (details in §2 of the earlier
   block; the corrected backing-share size is 512 KiB (§12.1), and
   `(uint32_t*)&m_gsp_vram[offset*4]` advances 8 bytes because the share is
   `optional_shared_ptr<uint16_t>`).
6. **Refuted RAM enlargement.** `$FFFFFF-$FF8000+1 = $8000` bytes, not
   `$80000`. The temporary 512 KiB edit was a regression; §9 records the
   correction. File-static storage in `stun-runner/native/fake_ports.c` does
   not change the mapped RAM size.
7. **Flow-log decoder.** jsr/jmp group masks are **0x4e80 (jsr) and 0x4ec0
   (jmp)** — not one contiguous range, because the addressing mode occupies
   op[8:3] and overlaps the mask. `abs.l` is mode **0x00b8**. Matching 0x0038
   silently discarded every `jsr $nnn.l` in the ROM. Added dbra (0x51c8),
   bra/bra.s/bra.l/bcc (0x6000 group), an opcode-independent catch-all
   (kind 3, any step >16 forward), a kind-3→real upgrade in `flow_edge_add`,
   and fixed the emit order to `from to kind count` (it printed `to from`
   against a header that said the opposite).
8. **IN0/SW1 defaults** — see §0. Both were invented before; now transcribed.

## 4. Tools added (all env-driven, all in the repo)

`stun-runner/tools/mame_oracle2.lua` — dumps `main_ram.bin`, `gsp_vram.bin`
(4 MiB), `gsp_vram_2bpp.bin` (1 MiB), `gsp_pal_lo/hi.bin` (2 KiB each) and
`frame_NNN_regs.txt`, at `STUNRUN_CHECKPOINT_FRAME` (default 600) into
`STUNRUN_CHECKPOINT_OUTPUT_DIR`. Directly diffable against our
`STUNRUN_DUMP_DIR` output.

`stun-runner/tools/mame_pclog.lua` — per-frame `frame 0xPC` to
`STUNRUN_PCLOG`. `stun-runner/tools/mame_ioports.lua` — **written but not
working**; see §6.

`native/shell.c` — `STUNRUN_PCLOG` (per-frame frame/PC/cycles),
`STUNRUN_TRACE_WINDOW=lo:hi` with `musashi_cpu_trace_window()` in
`native/musashi_cpu.c`, which executes instructions but prints only those inside
the window and reports A0/A1/A2/D0/D4. That window tracer is what finally showed
the `0x29e jmp (A1)` → 0x216 loop.

## 5. MAME-Lua facts

- `device.state` symbols are **UPPERCASE**; lowercase is nil.
- `device.spaces["program"]` (plural, string-keyed).
- `local mem = space:read_u8` is a syntax error — method refs need parens.
- GSP display registers (DPYADR etc.) are unreachable; `device.ior` is nil.
- **Per-space masks.** `(base+i) & 0xffffff` is right for the 24-bit 68010 but
  folds every GSP address above 0xffffff onto 0, which reads back as
  `unmap_value_high()` = 0xff and made a 4 MiB VRAM dump look like solid 0xff.
- `save_snapshot()` returns nil under `-autoboot_script`.
- Working invocation:
  `./mame-ref/mame stunrun -rompath stun-runner/roms -autoboot_script <script> -video none -sound none -seconds_to_run 300 -nosleep -skip_gameinfo`
  (`-rompath` must be the PARENT dir, not `roms/stunrun`.)
- `gsp_host_writes.bin` entries are `musashi_gsp_write_t {uint32_t addr;
  uint16_t data;}` = 8 bytes padded, host little-endian → parse with
  `struct.unpack_from('<IH', d, i*8)`.

## 6. Open items

**Historical investigation queue; not a current implementation plan.** Keep
these questions and commands as provenance. Later sections supersede its boot
and dump assumptions; do not force input defaults to obtain a preferred PC.

1. **`btst #$2,$a80001`** — decide empirically what the ROM reads there and set
   our a80000 default to match, so the boot takes `jmp $0f94`.
2. **`manager.machine.ioport` is not callable in MAME 0.289's Lua** — it is a
   `sol::ioport_manager*` userdata with a `ports` member that is itself a userdata
   and errors on `:size()`/`:at(i)`. The documented `ioport(tag)` form does not
   exist in this build. A different route to read the ports: install a read
   handler? Not available. Alternative: patch nothing, and instead brute-force the
   value by comparing behaviour — run our emulator with several a80000 defaults
   and diff the PC series against MAME's.
3. **Musashi `RESET` is a no-op.** `third_party/musashi/m68kconf.h:135-136`
   leaves `M68K_EMULATE_RESET` off, and `m68kcpu.c:810/832` implement the opcode
   as 130 cycles with no state change. MAME's RESET does reset peripherals
   (`harddriv_state::machine_reset`). The ROM executes `reset` at 0x228 on every
   pass through the boot loop, so this could matter.
4. **`jsr $65e0`, `jsr $552c`, `jsr $4dae`, `jsr $fbdc`, `jsr $f26c`** — not yet
   disassembled. These are the real-boot steps between the palette cube and the
   ADSP status poll.
5. **`$8090`/`$8092` ADSP status words** — the diagnostics screen compares them
   to `$5555`. Ours is a stub, so this will never pass; but that code is only
   reachable *after* the `btst #2` gate, so fix #1 first.
6. Re-diff `main_ram.bin` and `gsp_vram.bin` against `/tmp/or600` once the boot
   reaches the game loop.

## 7. Refutations of `stun-runner/docs/mame-stunrunner-findings.md`

The actual companion is [docs/mame-stunrunner-findings.md](mame-stunrunner-findings.md).
This section originally reversed two correct size facts and is itself refuted:

- Main RAM is **32 KiB**, not the claimed 512 KiB; see §9.
- GSP backing VRAM is **512 KiB**, not the claimed 4 MiB; see §12.1.
- "68010 can see the GSP palette directly" — wrong. `multisync_68k_map`
  (`harddriv.cpp:599-616`) exposes only 0xc00000-0xc03fff of GSP host I/O.
- "SW1 defaults to 0x00" — right for STUN Runner (`harddriv.cpp:1097-1121`),
  wrong for the `harddriv` block (`:802-826`, where they default to 0xFF).
---

## 8. The rendered ground truth (m07837) — attract mode IS a full game

`m:snapshot()` / `v:save_snapshot()` from Lua produce nothing under `-autoboot_script`
(both pcall clean, zero files, even with `-snapshot_directory` set). What works is an
**external X11 grab**: run MAME windowed and `import -window root` the framebuffer.

```bash
cd /home/budvar/projects/decomp/stun-runner
DISPLAY=:0 ../mame-ref/mame stunrun -rompath roms -window -video opengl \
    -sound none -nosleep -skip_gameinfo -seconds_to_run 60 &
sleep 14; DISPLAY=:0 import -window root /tmp/mame600.png
```

Result saved to `stun-runner/docs/mame-frame600-attract.png` (and
`mame-frame~1200-attract.png` for ~frame 1200).

**What it shows: the attract mode is not a check or diagnostics screen — it is a
complete playable run of the demo.** At ~frame 600 the screen shows:

- the craft centred, viewed from behind, on a tan/peach road strip running into a
  tunnel mouth with brown radial ribs and dark-red interior segments
- a yellow/olive angular object (a road-side marker or tunnel greeble) upper left
- HUD: `TIME 0:35.0`, `BEST 0:00.0`, `SCORE 200`, `LEVEL 1`, `MPH 955`,
  `Credits: 0`, `YOUR 1 BOOSTS / 0 KILLS / 0 STARS`, `BEST 0/0/0`

So MAME at frame 600 is already **in-game**, which is consistent with our PC now
landing on 0x2FCAC — an address inside the game loop's neighbourhood, not the
diagnostics screen. It also confirms the earlier reading that the diagnostics /
march-test path was the wrong target: the ROM has no reason to stop there.

Consequences for the remaining work:

1. **The ADSP-2100 is the next real blocker, and it is a hardware-fidelity gap,
   not a logic bug.** 24% of all retired instructions are the handshake delay at
   0x209E0 (`move.l #$1a0a,D0 / subq.l #1,D0 / bne $209e0`) bracketing
   `bsr $2053c`, which walks a command list whose head pointer lives at
   RAM `$ff94c2 + $2a` and whose targets are filled from ZRAM `$ff4001+`. Until the
   ADSP answers, the mailbox stays quiet and the 68010 never gets back to
   emitting FIFO words — 456 of 600 frames currently emit zero.
2. `RAM $ff9490`, `$ff9492`, `$ff9494`, `$ff9498..$ff94c2`, `$ff94b0` are the
   ADSP<->68010 mailbox. **Corrected measurement** — see §9: our RAM nonzero count
   is 2,413 bytes against MAME's 4,922 over the real 32 KiB, i.e. comparable, not
   150x smaller. Almost all of it is the mailbox.
3. ZRAM at `0xff4000..0xff4fff` (4 KiB window over two 2 KiB EEPROMs,
   `harddriv.cpp:612`) is used as scratch at 0x20726 (`lea $ff4001.l,A3`) and by
   `cmpm.b` at 0x20562. It is still an unwired region for us.

---

## 9. Main RAM is 32 KiB, and the oracle diff had been measuring ROM (refutation)

The oracle diffs quoted in §8 for `main_ram.bin` are **wrong**, and the size that made
them wrong came from a subagent report that had already been refuted in §7 and was
reintroduced anyway.

**The map line, verbatim** (`mame-ref/src/mame/atari/harddriv.cpp:615`):

```cpp
map(0xff8000, 0xffffff).ram();
```

`0xFFFFFF - 0xFF8000 = 0x7FFF`, so the window is `0x8000` bytes. MAME sizes a bare
`.ram()` device to `end - start + 1`; there is no explicit size to override it with.
**Main work RAM is 32 KiB.** The original `STUNRUN_MAIN_RAM_BYTES 0x8000u` was correct;
the "512 KiB" edit was a regression that made RAM 16x too large.

**Why the diff lied.** `tools/mame_oracle2.lua` dumped 0xFF8000 for `0x80000` bytes
under a 24-bit address mask. Every address past `0xffffff` wraps to 0, 1, 2 …, so the
dump beyond offset 0x8000 was reading **ROM**, not RAM. Measured on the frame-600
oracle: nonzero in the first 0x8000 = 4,922; in 0x8000..0x10000 = 25,538; past 0x10000
= 345,824. All of that is ROM content. This is why the count "376,284" kept appearing
and why RAM looked catastrophically empty.

Corrected comparison at frame 600, our dump `/tmp/srE` vs oracle `/tmp/or600c`:

| region | equal | ours nonzero | MAME nonzero | first diff |
|---|---|---|---|---|
| `main_ram.bin` (32 KiB) | **90.97%** | 2,413 | 4,922 | 0x0 |
| `gsp_vram.bin` (4 MiB) | 41.00% | 172,443 | 2,563,776 | 0x20205 |
| `gsp_vram_2bpp.bin` | 100.00% | 0 | 0 | — |
| `gsp_pal_lo.bin` | 23.73% | 32 | 1,552 | 0x0 |
| `gsp_pal_hi.bin` | 10.35% | 32 | 1,824 | 0x0 |

So main RAM is actually tracking MAME well. **The palette and VRAM are the real
divergence**, and both point at the same thing: the TMS34010 never runs, and the
68010's palette-cube routine at 0x570 is the only writer, so the game palette is
mostly never installed.

Do not "fix" `STUNRUN_MAIN_RAM_BYTES` to `0x80000`. There is a warning comment in
`native/fake_ports.h` saying so.

### 9a. `stunrun_march_test.c` — three real test bugs found while making it pass

The march slice (`native/ram_march.{h,c}` + test) went red once RAM was sized
correctly, and the failures were all in the harness or the transcription, not in
`musashi_cpu.c`:

1. **The fill's `limit` is exclusive.** `stunrun_march_fill` returns `RAM_LIMIT`
   (`0xFFFFFFFE`) having written 16,383 of the 16,384 words. The last word keeps its
   sentinel. `A1 = 0xFFFF8000 .. A2-2` covers `0x7FFE` bytes, which is 16,383 words —
   the ROM genuinely never touches the final word, and the test now asserts that.
2. **`march_c` verified the wrong address.** It read the word at address *N* and
   compared it against the accumulated pattern *before* writing it. A marching test
   must write then read back **the same address**: the accumulator advances per
   address, so verifying the not-yet-written neighbour fails on the second address
   every time. This was the actual cause of the fault at `0xFFFF8002`.
3. **`0xFFFF8000 + 0x80000` wraps uint32_t to 0.** Both the range checks in the test and
   the one in `native/ram_march_test.c:191` used `addr < RAM_BASE + RAM_BYTES`, which
   is *always false*. They are now written `addr - RAM_BASE < RAM_BYTES`. The same
   overflow is why the original additive form looked like a legitimate assertion.

`march_inverted` (0x5B2A, `eori.w #$ffff` / `beq`) is a destructive invert-and-verify
that is only meaningful when every word already holds `0xFFFF`, so it now primes the
block via `stunrun_march_fill(io, base, limit, 0xFFFFu)` to be usable standalone.

All five variants plus fault injection now pass: watchdog kicks = 16 for the four march
variants, 16,383 for the inverted phase; injected faults are caught at the exact
injected word with two `$9F9F` FIFO words and `hstadr` advanced by `0x20`. 32/32 CTest.

## 10. GSP program memory is 16 bytes wide, not 2 (the 41% -> 60% fix)

**Historical program-space probe.** Share-size and mismatch-cause conclusions
are superseded by §12.1 and the compiler caveats in §11. The measured byte cells
and source excerpts survive; this was not proof of whole-bus parity.

### Symptom
`gsp_vram.bin` matched the MAME oracle on only 41.00% of bytes at frame 600 while
`main_ram.bin` matched 90.97%. The divergence was not random: it was concentrated at
exactly the addresses the 68010 uses for the **display list** and the **GSP program
code**.

### Evidence

At VRAM offset `0x340000` (host `0xFFF40000`, where the 68010 writes its display
list):

| | bytes |
|---|---|
| ours | `05 50 00 00 00 00 00 00 ...` |
| MAME | `50 50 50 50 50 50 50 50  05 05 05 05 05 05 05 05` |

Same at the GSP's own PC (`0xFFF41070` -> offset `0x341070`): ours `0b 60 00 00 ...`,
MAME `60` x8 then `0b` x8.

Rather than infer the rule from those samples, I measured it directly with a live MAME
Lua probe at frame 2 (`sp = gsp.spaces["program"]`, `B = 0xff800000`):

```
sp:write_u16(B+0x100, 0x1234)   ->  B+0x100..0x107 = 34 x8, B+0x108..0x10f = 12 x8
sp:write_u16(B+0x110, 0xabcd)   ->  cd x8 then ab x8
sp:write_u32(B+0x400, 0xdeadbeef)->  ef x8, be x8, ad x8, de x8   (32 bytes)
sp:write_u16(B+0x500, 0x5566)   ->  odd halfword still lands on the 16-byte cell
```

**The rule:** a host word write of `V` at byte address `A` writes **16 bytes**:
`off = A & ~0xF`; bytes `off+0..7` = `V & 0xFF`; bytes `off+8..15` = `V >> 8`. The
address is masked to 16 bytes. Reads broadcast the same way — `read_u16` returns `V`
at *every* 2-byte offset in the cell, and `read_u32` returns `V` at cell offsets 0 and
4 but `V >> 8` at offsets 8 and 12.

### Why MAME behaves this way

GSP program memory is modelled as a 32-bit-wide address space with `AddrShift = 3`:

```c
memory_access<32, 1, 3, ENDIANNESS_LITTLE>::specific m_program;  // tms34010.h:992
```

so `TMS34010_WRMEM_WORD(A,V)` (`tms34010.cpp:316-318`) goes through
`memory_access_specific` (`emumem.h:1689`, `NATIVE_MASK` at `:1695`) and one 16-bit
host write covers a whole aligned cell.

### Fix

`native/musashi_cpu.c` `gsp_mem_write_word()` now replicates instead of storing a
big-endian pair, and `gsp_mem_read_word()` reassembles the value from the replicated
bytes rather than reading two adjacent ones. The 2bpp expander is deliberately left
alone: `hdgsp_vram_2bpp_w` (`harddriv_v.cpp:300-316`) casts to `uint32_t *` and writes
the share **directly**, bypassing the replication.

### Result

| region | before | after |
|---|---|---|
| `gsp_vram.bin` | 41.00% | **59.85%** |
| `main_ram.bin` | 90.97% | 90.97% |

### What the remaining 40% is

Not a model defect. `gsp_pal_lo.bin` has 32 nonzero bytes against MAME's 1,552, and
the VRAM blocks we lack entirely (`0x0d0000`..`0x1d0000`) are the rasterised frame.
Both are written by **the GSP executing**, not by the 68010. The 68010's contribution
is to upload a display list of ~12,120 words per frame (`0xfff40000`..`0xfff6f570`)
into VRAM; the GSP then walks it and paints. Our frame-600 write log confirms the 68010
writes only 256 distinct palette words (a single greyscale ramp) and moves the palette
bank register never, while MAME ends with 776 of 1024 entries lit. **Running the
TMS34010 is therefore the gate for pixels.**

### Corollary: the 68010 does not paint, it compiles

The frame-600 host-write log separates the two halves of the pipeline. Per frame the
68010 emits ~12,120 words to `0xfff40000`..`0xfff6f570` — the display list — and that
is *all* it does for video. Evidence it never touches the palette beyond one ramp:

```
distinct pal_lo addresses written: 256   (total 1024 writes)
distinct pal_hi addresses written: 256   (total 1024 writes)
control_hi offsets ever written:  0x00 0x10 0x20 0x30 0x70 0x90 0xa0 0xb0 0xf0
```

The palette bank is selected by writing `control_hi` offsets `0x02`/`0x03`/`0x04`
(`harddriv_v.cpp:211-256`), and **the 68010 never writes any of them** — so the bank
stays 0 for the whole run, which matches MAME ending with 776 of 1024 entries lit while
we hold 16. Those extra entries, and the fully-populated VRAM blocks at
`0x0d0000`..`0x1d0000`, are the GSP's rasteriser output.

So the remaining 40% needs no further work on the 68010 model. It needs a **TMS34010**.
The ROM cooperates: routine `0x530` seeds GSP program memory directly, writing four
words at `0x26088050/190/210/350` through the +0x10 host auto-increment before pointing
the host at the 2bpp window — so the GSP's own code is partially 68010-supplied, and
reproducing it is a decoding problem, not a reverse-engineering-of-unknowns problem.

## 11. The display-list compiler (0x225E4) and the GSP fill helpers — full transcription

### 11.0 Runtime producer-call trace correction

The bounded MAME producer trace added in October 2026 refuted the initial trace
assumption that all runtime entries to `$225E4` come directly from six-byte
`jsr $225e4.l` instructions. The static ROM does contain those direct call
sites, but live startup code also loads `$225E4` into an address register and
uses two-byte `jsr (An)` calls. For example, MAME observed return `$02D782`;
the bytes at `$02D780` are `4E92` (`jsr (A2)`), so the producer call site is
`$02D780`, not the refuted `$02D77C` obtained by subtracting six.

`mame/lua/display-list-trace.lua` therefore classifies the JSR/BSR encoding
immediately before the stacked return address and records `call_size` and
`call_opcode` alongside the caller and `4(A7)` stream argument. This changes
only producer attribution; the `$225E4` compiler semantics below are unchanged.

The confirming bounded oracle run was:

```bash
stun-runner/tools/mame-display-list-trace roms \
  /tmp/opencode/stunrun-display-list-trace-verified \
  --mame mame-ref/mame --frames 180 --start-frame 1 --end-frame 180 \
  --input drive --max-calls 32 --raw-bytes 1024 --max-fifo-words 65536
```

It completed with return code 0 and the DONE marker, recording 14 calls (two of
each row below). The canonical log SHA-256 was
`e0f7d180fe01b8ff40b4687c47e6f9502dcf2c1021e81218974ba70c5dc8e09a`.

| caller | encoding | stream argument | decode within 1024-byte snapshot |
|---|---|---|---|
| `$02D7D6` | `4EB9`, 6-byte direct JSR | `$060002` | truncated |
| `$02D780` | `4E92`, 2-byte `jsr (A2)` | `$094D09` | truncated |
| `$02D792` | `4E92`, 2-byte `jsr (A2)` | `$097F4B` | truncated |
| `$02D7A4` | `4E92`, 2-byte `jsr (A2)` | `$09841C` | complete, opcode `$40` |
| `$02D7B6` | `4E92`, 2-byte `jsr (A2)` | `$098457` | complete, opcode `$40` |
| `$02D7F2` | `4EB9`, 6-byte direct JSR | `$06118F` | truncated |
| `$02DB98` | `4EB9`, 6-byte direct JSR | `$06118F` | truncated |

The FIFO bound was reached, so this run proves producer attribution, stream
arguments, and grouped decoding but is intentionally **not** a complete FIFO
inventory for the long streams. Rerun with larger bounds when a specific
producer becomes the focus; do not infer a terminator beyond a truncated raw
snapshot.

### 11.0a Watchdog-confirmed uploads and raw-byte preview

The old 14-entry count above was a fetch count, not an execution count. The
revised trace promotes an entry candidate only on the existing compiler
watchdog write at PC `$22608`. The 180-frame run confirms **seven opcode-loop
executions**, one per caller above. Nonzero-header early exits are deliberately
outside this confirmation method. No new disassembly was needed for this
instrumentation; the transcription below supplies the watchdog decision site.

```bash
stun-runner/tools/mame-display-list-trace roms \
  /tmp/opencode/stunrun-display-list-confirmed-full \
  --mame mame-ref/mame --frames 180 --end-frame 180 --input drive \
  --raw-bytes 65536 --max-fifo-words 200000
```

Return code 0; DONE marker present; neither call nor FIFO limit reached.
Canonical log SHA-256:
`41954b8b09fd5095cbcf3be570069201e70307dbe2fcd6dcc55ae738a8ef2d0c`.
Every stream ended at opcode `$40`, and every decoded word count matched the
actual oracle writes. Host control was `$B800` for all seven. Per-write host
destination latches advanced by `$10` in the observed initial words.

| stream | caller | initial GSP destination latch | FIFO words |
|---|---|---|---|
| `$060002` | `$02D7D6` | `$FFDE0000` | 8192 |
| `$094D09` | `$02D780` | `$FFF23F00` | 1926 |
| `$097F4B` | `$02D792` | `$FFF2B760` | 5180 |
| `$09841C` | `$02D7A4` | `$FFF3FD80` | 40 |
| `$098457` | `$02D7B6` | `$FFF3FB20` | 38 |
| `$06118F` | `$02D7F2` | `$FFE00000` | 61440 |
| `$06118F` | `$02DB98` | `$FFE00000` | 61440 |

Confirmed: grouped instructions compress host-port upload payloads. **Not
confirmed:** those payloads are bitmaps rather than code or other data. The
high GSP destinations alone cannot identify their meaning. The smallest
upload is exported as 76 big-endian bytes in `small-upload.bin`; the associated
PGM has two byte-intensity columns and 38 rows, not an asserted game-pixel
layout. This uses actual expanded oracle words, not the native C decoder.

Native fidelity remains a separate unresolved discrepancy: the first words
for `$060002` and `$06118F` are `$1616` and `$8484`, respectively, while parts of
the C slice describe byte-register loads as clearing upper bits. Count parity
does **not** establish word-value parity. Reconcile register-width semantics
against the existing transcription and oracle before using the native slice
as a pixel source. This is a suspected port misreading, not an original-game
defect, and therefore does not belong in BUGS.md.

This section is the findings record for four routines read during the
`native/gsp_display_list.{h,c}` slice. ROM file `136070-2102.200r`, flat image
`/tmp/maincpu_flat1m.bin` (1 MiB, base 0), disassembled with `tools/dis68k.c`
(`cc -O2 -Wall -Wextra -o /tmp/dis68k tools/dis68k.c third_party/musashi/m68kdasm.o`).
Every listing below is `/tmp/dis68k` output pasted verbatim.

### 11.0b Producer callers: conversion evaluation and literal effect slices

Inspected on 2026-10-09 from the six-bank, de-interleaved 1 MiB image
`/tmp/opencode/stunrun-maincpu-flat.bin`. Bank containing these routines is
`136070-6104.200s` (even) / `136070-6103.210s` (odd), mapped at `$020000`;
ROM pointer tables are in `136070-2112.200w` / `136070-2111.210w`.
Bank layout is `stun-runner/native/musashi_loader.c:39-46`. Disassembler:
`cc -O2 -Wall -Wextra -o /tmp/opencode/dis68k stun-runner/tools/dis68k.c
stun-runner/third_party/musashi/m68kdasm.o`, MC68010 CPU type 2.
The initial read accidentally continued into `$2D80E` and `$2D858`; both
routines were then followed to RTS and included in the tested slice rather
than leaving unrecorded partial readings. No subcallee body was inspected.
Flat-image SHA-256:
`9c1a9189c0abd6d0f08b5f3e0785b5c72e9df9ef22b70c76692b0830e6263bcb`.
Even/odd caller-bank SHA-256 respectively:
`d7fd4632204b984884dfdc8959376605c7f463a6b0d11520ffecd79cb7b6a449`,
`fd03f12078656c1f68de8199989b921ccadedc5bba9ace42e82c5640d9e9762b`.

Mnemonic transcription below is verbatim Musashi output (byte columns omitted).
Read ranges are `$2D762-$2D89C` and `$2DB84-$2DBB2`, inclusive of final RTS.

```text
0002d762: link    A6, #$0
0002d766: movem.l A2, -(A7)
0002d76a: movea.l #$225e4, A2
0002d770: move.l  #$fff23f00, $c00002.l
0002d77a: move.l  $bfe78.l, -(A7)
0002d780: jsr     (A2)
0002d782: move.l  #$fff2b760, $c00002.l
0002d78c: move.l  $bfe80.l, -(A7)
0002d792: jsr     (A2)
0002d794: move.l  #$fff3fd80, $c00002.l
0002d79e: move.l  $bfe84.l, -(A7)
0002d7a4: jsr     (A2)
0002d7a6: move.l  #$fff3fb20, $c00002.l
0002d7b0: move.l  $bfe88.l, -(A7)
0002d7b6: jsr     (A2)
0002d7b8: movem.l (-$4,A6), A2
0002d7be: unlk    A6
0002d7c0: rts
0002d7c2: link    A6, #$0
0002d7c6: move.l  #$ffde0000, $c00002.l
0002d7d0: move.l  $bfe04.l, -(A7)
0002d7d6: jsr     $225e4.l
0002d7dc: jsr     $2d762.l
0002d7e2: move.l  #$ffe00000, $c00002.l
0002d7ec: move.l  $bfe08.l, -(A7)
0002d7f2: jsr     $225e4.l
0002d7f8: move.l  $47f58.l, $ff9bae.l
0002d802: move.w  #$2, $ff9bce.l
0002d80a: unlk    A6
0002d80c: rts
0002d80e: link    A6, #$0
0002d812: movem.l D2-D5, -(A7)
0002d816: move.l  ($8,A6), D3
0002d81a: move.l  ($c,A6), D2
0002d81e: move.l  #$10000, D4
0002d824: move.l  #$20000, D5
0002d82a: bra     $2d83c
0002d82c: move.l  D4, -(A7)
0002d82e: move.l  D3, -(A7)
0002d830: jsr     $2247a.l
0002d836: addq.l  #8, A7
0002d838: sub.l   D4, D2
0002d83a: add.l   D5, D3
0002d83c: move.l  D4, D0
0002d83e: cmp.l   D2, D0
0002d840: bcs     $2d82c
0002d842: move.l  D2, -(A7)
0002d844: move.l  D3, -(A7)
0002d846: jsr     $2247a.l
0002d84c: addq.l  #8, A7
0002d84e: movem.l (-$10,A6), D2-D5
0002d854: unlk    A6
0002d856: rts
0002d858: link    A6, #$0
0002d85c: move.l  ($8,A6), D1
0002d860: movea.l #$ff9c66, A0
0002d866: tst.w   $ff9c68.l
0002d86c: bls     $2d876
0002d86e: subq.w  #1, $ff9c68.l
0002d874: bra     $2d89a
0002d876: tst.w   (A0)
0002d878: beq     $2d890
0002d87a: moveq   #$0, D0
0002d87c: move.w  $ff9c64.l, D0
0002d882: lsl.l   #8, D0
0002d884: or.l    D1, D0
0002d886: move.w  D0, $c0000c.l
0002d88c: clr.w   (A0)
0002d88e: bra     $2d89a
0002d890: move.w  D1, $ff9c64.l
0002d896: move.w  #$1, (A0)
0002d89a: unlk    A6
0002d89c: rts
0002db84: link    A6, #$0
0002db88: move.l  #$ffe00000, $c00002.l
0002db92: move.l  $bfe08.l, -(A7)
0002db98: jsr     $225e4.l
0002db9e: move.l  $47f58.l, $ff9bae.l
0002dba8: move.w  #$2, $ff9bce.l
0002dbb0: unlk    A6
0002dbb2: rts
```

Confirmed mechanism: `$2D762` is four fixed destination writes, each followed
by a compiler call with a **pointer read from ROM**, not a hardcoded stream.
The six slots read as `$BFE04→$060002`, `$BFE08→$06118F`, `$BFE78→$094D09`,
`$BFE80→$097F4B`, `$BFE84→$09841C`, `$BFE88→$098457`, independently matching
the seven runtime calls in §11.0a. `$2D7C2` surrounds `$2D762` with the first
and last larger uploads. `$2DB84` repeats the last upload. Both copy the long
at `$47F58` (currently `$000000F0`) to `$FF9BAE` and write word 2 to `$FF9BCE`.
The roles of these fields are **unknown**; calling them dimensions or a
render-command marker is not justified. “Initial” and “reload” are descriptive
API names based on observed call order, not proven original routine names.

There are no blit opcodes or scene-object dispatch branches in these callers.
They are orchestration around the compression interpreter. High destinations
are mirrored `gsp_vram` RAM per `mame-ref/src/mame/atari/harddriv.cpp:628`;
that map does not identify payloads as pixels or executable code.

Additional inspected mechanisms: `$2D80E` calls opaque `$2247A` with
`(address, length)` in chunks of `$10000`, advancing address by `$20000`.
Unsigned **strict** comparison preserves the final call, including length 0.
Address-unit interpretation and `$2247A` behavior remain unknown. `$2D858`
decrements a nonzero word at `$FF9C68` without emitting data; otherwise it
stores a pending word at `$FF9C64`/flag `$FF9C66`, or emits the low word of
`(previous << 8) | full_argument` to `$C0000C` and clears the flag. The input
argument is not byte-masked, and pending data is not flushed on return.
These are literal mechanisms, not newly asserted original-game defects.

C conversion: `stun-runner/native/gsp_upload_callers.{h,c}` transcribes all
five routines at the application-effect boundary. CPU stack/register
save/restore instructions are intentionally abstracted, not a cycle/CCR model.
ROM reads, host writes, compiler calls, opaque transfer calls and state writes
are callbacks. No subcallee semantics are synthesized and no shell override
is installed. `stun-runner/native/gsp_upload_callers_test.c` asserts ordered
reads/writes/calls, alternate ROM pointers, marker values, chunk boundaries,
address wrap, countdown, pending-byte composition and non-byte input behavior;
CTest is `gsp-upload-callers-slice-c`.

Validation recipe (from repository root):

```bash
cmake -S stun-runner/native -B /tmp/opencode/stunrun-upload-callers-build
cmake --build /tmp/opencode/stunrun-upload-callers-build \
  --target stunrun-gsp-upload-callers-slice-test \
  stunrun-gsp-display-list-slice-test stunrun-gsp-display-list-musashi-test -j2
ctest --test-dir /tmp/opencode/stunrun-upload-callers-build \
  -R 'gsp-(upload-callers|display-list)' --output-on-failure
```

Conversion recommendation: `$2D762`, then `$2DB84`, then `$2D7C2` are small,
straight-line candidates with independently confirmed destination/argument
sequences. They can be converted without understanding pixel formats. Live
replacement still requires trustworthy `$225E4` word values, canonical host
long-write ordering and testing the two trailing state writes against MAME.
Treat `$2D80E` as a wrapper-only slice until `$2247A` is investigated; retain
`$2D858` as a small state-machine slice, not evidence of a blit dispatcher.

#### Caller integration and differential execution (2026-10-09)

`musashi_cpu_bind_gsp_upload_callers()` in
`stun-runner/native/musashi_cpu.{h,c}` now binds the caller slice to the
canonical CPU bus. A destination long write takes the **same two ordered
16-bit host-register writes** as an emulated instruction. ROM pointer reads
and trailing RAM writes use that same bus rather than asset-specific constants.
The caller owns a `musashi_gsp_upload_adapter_t` context and supplies explicit
compiler/transfer callbacks. Missing ports or callbacks fail binding; no default
C compiler, opaque transfer implementation, or live override is installed.

`stun-runner/native/gsp_upload_callers_musashi_test.c` first checks synthetic
ROM pointers, non-default state values, exact FIFO addresses/words, preservation
of state by `$2D762`, invalid bindings, and a bound CPU distinct from the current
memory-callback CPU. With `STUNRUN_UPLOAD_CALLERS_ROMDIR`, it then executes each
**original ROM caller** under Musashi and compares it with the C caller using
the **original `$225E4` compiler** as its callback. Both start from fresh matching
machine state with host auto-increment enabled. A synthetic RAM `BRA.S` sentinel
detects return; each invocation has a 50,000,000-cycle budget. No new ROM
disassembly was read for this test.

Local-ROM results: `$2D762`: 4 calls / **7,184** address-word pairs; `$2DB84`:
1 call / **61,440** pairs; `$2D7C2`: 6 calls / **76,816** pairs. All ordered
addresses and values, final host address, and `$FF8000-$FFFBFF` RAM bytes match.
Stack scratch at `$FFFC00+` is intentionally excluded; this does **not** prove
register/CCR preservation, cycle timing, or C compression-interpreter parity.
The separate idle MAME producer trace's 138,256 words equal the initial-plus-
reload total here, but equality of totals alone is not word-value parity.

MAME independently confirms the trailing writes in a 180-frame idle run:
frame 120, PC `$2D7F8`, words `0000`/`00F0` to `$FF9BAE`/`$FF9BB0`, followed
by PC `$2D802`, word `0002` to `$FF9BCE`; frame 166 repeats those values at
PCs `$2DB9E`/`$2DBA8`. Their meanings remain unknown. Evidence:
`/tmp/opencode/stunrun-upload-caller-state-full/{mame.log,result.json}`;
34 captured writes, no truncation, canonical log SHA-256
`49d3fe901f8de25123442ae3ad0c813a4fe71774af1504dbb6e0afb41c84026e`.
Earlier exploratory capture used the tool's default 680–705 capture window
with only 180 frames and yielded no events; a subsequent 32-event cap cut off
the reload writes. Neither is acceptance evidence.

Reproduction from repository root (ROMs remain local):

```bash
cmake --build /tmp/opencode/stunrun-upload-callers-build \
  --target stunrun-gsp-upload-callers-musashi-test -j2
STUNRUN_UPLOAD_CALLERS_ROMDIR="$PWD/roms/stunrun" \
  /tmp/opencode/stunrun-upload-callers-build/stunrun-gsp-upload-callers-musashi-test
python3 stun-runner/tools/mame-ram-write-trace roms \
  /tmp/opencode/stunrun-upload-caller-state-full \
  --device :mainpcb:maincpu --base 0xff9bae --end 0xff9bcf \
  --frames 180 --start-frame 1 --end-frame 180 --tap-frame 1 \
  --max-events 128 --nothrottle --mame "$PWD/mame-ref/mame"
```

CTest `gsp-upload-callers-musashi-c` always runs synthetic checks; without the
ROM environment variable it explicitly reports that differential execution was
not run. Live substitution remains off pending compiler word fidelity and a
register/CCR/timing contract for CPU replacement.

Validation: full native CMake build succeeded; all **37/37 CTests** passed with
the ROM differential environment enabled. Standalone compilation of the new
test passed `-std=c99 -Wall -Wextra -Werror`. Adding `-pedantic` hits the existing
duplicate `stunrun_fake_ports_t` typedef between `fake_ports.h` and
`musashi_cpu.h`; full-build trace-window format warnings are also pre-existing.
The first two-CPU isolation test incorrectly assumed per-instance fake-port
RAM, failed, and was corrected to use CPU-owned host latches: `fake_ports.c`
actually holds one static `main_ram`. This was a refuted test assumption, not
a ROM or MAME defect. Tests do not assert independent RAM instances.
Logs: `/tmp/opencode/stunrun-upload-callers-{full-build,full-ctest,rom}.log`.
The full check commands are:

```bash
cmake --build /tmp/opencode/stunrun-upload-callers-build -j2
STUNRUN_UPLOAD_CALLERS_ROMDIR="$PWD/roms/stunrun" \
  ctest --test-dir /tmp/opencode/stunrun-upload-callers-build --output-on-failure
```

### 11.1 Boundaries, and how they were decided

**Boundary inventory, not all confirmed entry points.** The retained bytes are
the evidence. Static byte-pattern hits below are obsolete as execution proof;
bank decoding and the `$0C8A` return boundary remain unresolved.

| range | routine | why the boundary is where it is |
|---|---|---|
| `0x225B0`–`0x225CC` | 16-byte lookup-table loader | own `lea $ff902a.l,A1` prologue, own `rts`, callers are the two interpreters only |
| `0x225CE`–`0x225E2` | terminator epilogue | `jsr` then register save/restore then `rts` |
| `0x225E4`–`0x22748` | **the interpreter** | own `movea.l ($4,A7),A0` + `movem.l D2-D5/A2-A3,-(A7)` prologue; last instruction is `0x22746 bra $22606`, so the body ends at `0x22748` |
| `0x2274A`–`0x22764` | the twin's epilogue | uses `D6`, saves `D2-D6`; unreachable from `0x225E4` |
| `0x22766`– | the twin's prologue | `movem.l D2-D6/A2-A3,-(A7)` — a *different* register mask, so not ours |
| `0x2418`–`0x2432` | `0x0BC4`'s address helper | own `rts`; `0x2434` disassembles as ASCII `"1234567"` — data |
| `0x0BC4`–`0x0C26` | **the nested sparse fill** | own `ori.w` prologue + `rts` |
| `0x0C28`–`0x0C44` | caller of `0x0C46` | `rts` at `0x0C44` |
| `0x0C46`–`0x0C62` | 32-step `$20`-stride fill | own `rts` |
| `0x0C64`–`0x0C88` | candidate ramp caller | caller has `rts`; callee's return not in retained listing |
| `0x0C8A`–`0x0CA8` | partial ramp listing | ends on `dbra`, no `rts`; cannot infer the continuation or returning sequence |
| `0x530`–`0x56E` | host address-latch routine | own `lea` prologue + `rts`; §11.10 corrects the microcode-seed claim |
| `0x44518`–`0x44550` | orphan clone of `0x536`–`0x56E` | own `rts`, and the `lea $c00002.l,A1` prologue is missing |

**`0x0BF4` is not a routine entry.** It is the 7th instruction of `0x0BC4`
(`lea $c0000c.l,A1`, between `move.w #$ffff,D3` and `move.l D0,$c00002.l`).
`0x0BC4` has exactly one caller, `bsr.w` at `0x14D0`, immediately preceded by
`0x14CA: lea $b9a.l,A0`.

**`0x225B0` is a separate routine, not part of `0x225E4`.** It has its own
prologue and epilogue and is also `jsr`ed from `0x22808` inside the twin
`0x22766`. The `bsr` at `0x22604` is **`BSR.B`** (`61 aa`, an 8-bit displacement),
not `BSR.W`.

Call sites, counted by scanning for the exact 6-byte `jsr` encodings rather than
by decoding branch targets. A naive `bsr` scanner that always reads a 16-bit
displacement produces garbage targets, because `61` with an 8-bit displacement is
`BSR.B`:

```
jsr $225e4.l : 19  -> 024aae 024ace 024f20 024f5c 024f98 02992e 029c08 029c28
                    02af5a 02af7a 02d7a2 02d7be 02db04 02db34 02db64 02dbdc
                    0404a8 0406de 040744
jsr $22766.l :  3  -> 02c382 02fbec 02fc0a
jsr $21286.l : 26  -> 0225ce 028f56 02c396 02da74 02df9a 02ec22 02f052 02f102
                    02fd02 02fd22 02fda4 02fe88 0305ec 030608 030624 030640
                    03065c 030678 030694 0306b0 0306d2 030818 030872 031ea0
                    033c52 033c98
```

### 11.2 Verbatim transcription: `0x225B0`–`0x22748`

```
000225b0: 43 f9 00 ff 90 2a  lea     $ff902a.l, A1
000225b6: 70 00  moveq   #$0, D0
000225b8: 72 0f  moveq   #$f, D1
000225ba: 10 18  move.b  (A0)+, D0
000225bc: 13 40 ff e0  move.b  D0, (-$20,A1)
000225c0: 32 c0  move.w  D0, (A1)+
000225c2: 51 c9 ff f6  dbra    D1, $225ba
000225c6: 43 f9 00 ff 90 2a  lea     $ff902a.l, A1
000225cc: 4e 75  rts
000225ce: 4e b9 00 02 12 86  jsr     $21286.l
000225d4: 4a 42  tst.w   D2
000225d6: 67 04  beq     $225dc
000225d8: 42 03  clr.b   D3
000225da: 34 83  move.w  D3, (A2)
000225dc: 20 08  move.l  A0, D0
000225de: 4c df 0c 3c  movem.l (A7)+, D2-D5/A2-A3
000225e2: 4e 75  rts
000225e4: 20 6f 00 04  movea.l ($4,A7), A0
000225e8: 48 e7 3c 30  movem.l D2-D5/A2-A3, -(A7)
000225ec: 45 f9 00 c0 00 0c  lea     $c0000c.l, A2
000225f2: 47 f9 00 60 80 00  lea     $608000.l, A3
000225f8: 76 00  moveq   #$0, D3
000225fa: 7a 00  moveq   #$0, D5
000225fc: 78 00  moveq   #$0, D4
000225fe: 74 00  moveq   #$0, D2
00022600: 10 18  move.b  (A0)+, D0
00022602: 66 ca  bne     $225ce
00022604: 61 aa  bsr     $225b0
00022606: 70 00  moveq   #$0, D0
00022608: 42 53  clr.w   (A3)
0002260a: 10 18  move.b  (A0)+, D0
0002260c: 08 80 00 07  bclr    #$7, D0
00022610: 66 00 00 b8  bne     $226ca
00022614: 08 80 00 06  bclr    #$6, D0
00022618: 66 58  bne     $22672
0002261a: 4a 00  tst.b   D0
0002261c: 67 e6  beq     $22604
0002261e: 4a 42  tst.w   D2
00022620: 67 0a  beq     $2262c
00022622: 16 18  move.b  (A0)+, D3
00022624: 34 83  move.w  D3, (A2)
00022626: 74 00  moveq   #$0, D2
00022628: 53 40  subq.w  #1, D0
0002262a: 67 da  beq     $22606
0002262c: 32 08  move.w  A0, D1
0002262e: 02 41 00 01  andi.w  #$1, D1
00022632: 67 22  beq     $22656
00022634: 12 00  move.b  D0, D1
00022636: e2 08  lsr.b   #1, D0
00022638: 53 40  subq.w  #1, D0
0002263a: 6b 0c  bmi     $22648
0002263c: 16 18  move.b  (A0)+, D3
0002263e: e1 4b  lsl.w   #8, D3
00022640: 16 18  move.b  (A0)+, D3
00022642: 34 83  move.w  D3, (A2)
00022644: 51 c8 ff f6  dbra    D0, $2263c
00022648: 08 01 00 00  btst    #$0, D1
0002264c: 67 b8  beq     $22606
0002264e: 16 18  move.b  (A0)+, D3
00022650: e1 8b  lsl.l   #8, D3
00022652: 46 42  not.w   D2
00022654: 60 b0  bra     $22606
00022656: 12 00  move.b  D0, D1
00022658: e2 08  lsr.b   #1, D0
0002265a: 53 40  subq.w  #1, D0
0002265c: 6b 0c  bmi     $2266a
0002265e: 34 98  move.w  (A0)+, (A2)
00022660: 51 c8 ff fc  dbra    D0, $2265e
00022664: 08 01 00 00  btst    #$0, D1
00022668: 67 9c  beq     $22606
0002266a: 36 10  move.w  (A0), D3
0002266c: 52 88  addq.l  #1, A0
0002266e: 46 42  not.w   D2
00022670: 60 94  bra     $22606
00022672: 53 40  subq.w  #1, D0
00022674: 6b 00 ff 5e  bmi     $225d4
00022678: 4a 42  tst.w   D2
0002267a: 66 2c  bne     $226a8
0002267c: 18 18  move.b  (A0)+, D4
0002267e: 3a 04  move.w  D4, D5
00022680: e6 0c  lsr.b   #3, D4
00022682: 02 44 00 1e  andi.w  #$1e, D4
00022686: 02 45 00 0f  andi.w  #$f, D5
0002268a: da 45  add.w   D5, D5
0002268c: 36 31 40 e0  move.w  (-$20,A1,D4.w), D3
00022690: 16 31 50 01  move.b  ($1,A1,D5.w), D3
00022694: 34 83  move.w  D3, (A2)
00022696: 51 c8 ff e4  dbra    D0, $2267c
0002269a: 60 00 ff 6a  bra     $22606
0002269e: 02 44 00 0f  andi.w  #$f, D4
000226a2: d8 44  add.w   D4, D4
000226a4: 36 31 40 e0  move.w  (-$20,A1,D4.w), D3
000226a8: 18 18  move.b  (A0)+, D4
000226aa: 3a 04  move.w  D4, D5
000226ac: e6 0d  lsr.b   #3, D5
000226ae: 02 45 00 1e  andi.w  #$1e, D5
000226b2: 16 31 50 01  move.b  ($1,A1,D5.w), D3
000226b6: 34 83  move.w  D3, (A2)
000226b8: 51 c8 ff e4  dbra    D0, $2269e
000226bc: 02 44 00 0f  andi.w  #$f, D4
000226c0: d8 44  add.w   D4, D4
000226c2: 36 31 40 e0  move.w  (-$20,A1,D4.w), D3
000226c6: 60 00 ff 3e  bra     $22606
000226ca: 08 80 00 06  bclr    #$6, D0
000226ce: 66 00 00 32  bne     $22702
000226d2: 4a 40  tst.w   D0
000226d4: 67 00 fe f8  beq     $225ce
000226d8: 16 18  move.b  (A0)+, D3
000226da: 4a 42  tst.w   D2
000226dc: 67 0a  beq     $226e8
000226de: 34 83  move.w  D3, (A2)
000226e0: 74 00  moveq   #$0, D2
000226e2: 53 40  subq.w  #1, D0
000226e4: 67 00 ff 20  beq     $22606
000226e8: e2 48  lsr.w   #1, D0
000226ea: 95 42  subx.w  D2, D2
000226ec: 3a 03  move.w  D3, D5
000226ee: e1 4b  lsl.w   #8, D3
000226f0: 16 05  move.b  D5, D3
000226f2: 53 40  subq.w  #1, D0
000226f4: 6b 00 ff 10  bmi     $22606
000226f8: 34 83  move.w  D3, (A2)
000226fa: 51 c8 ff fc  dbra    D0, $226f8
000226fe: 60 00 ff 06  bra     $22606
00022702: 4a 40  tst.w   D0
00022704: 67 00 fe c8  beq     $225ce
00022708: 18 18  move.b  (A0)+, D4
0002270a: 3a 04  move.w  D4, D5
0002270c: e6 0c  lsr.b   #3, D4
0002270e: 02 44 00 1e  andi.w  #$1e, D4
00022712: 02 45 00 0f  andi.w  #$f, D5
00022716: da 45  add.w   D5, D5
00022718: 4a 42  tst.w   D2
0002271a: 67 1e  beq     $2273a
0002271c: 16 31 40 01  move.b  ($1,A1,D4.w), D3
00022720: 34 83  move.w  D3, (A2)
00022722: 36 31 50 e0  move.w  (-$20,A1,D5.w), D3
00022726: 16 31 40 01  move.b  ($1,A1,D4.w), D3
0002272a: 55 40  subq.w  #2, D0
0002272c: 6b 00 fe d8  bmi     $22606
00022730: 34 83  move.w  D3, (A2)
00022732: 51 c8 ff fc  dbra    D0, $22730
00022736: 60 00 fe ce  bra     $22606
0002273a: 36 31 40 e0  move.w  (-$20,A1,D4.w), D3
0002273e: 16 31 50 01  move.b  ($1,A1,D5.w), D3
00022742: 53 40  subq.w  #1, D0
00022744: 6a ea  bpl     $22730
00022746: 60 00 fe be  bra     $22606
```


### 11.3 The two helpers the interpreter leans on

`0x21286` is the SSP **reset** entry. It never returns:

```
00021286: 4e 72 27 00  stop    #$2700
0002128a: 2e 78 00 00  movea.l $0.w, A7
0002128e: 46 fc 27 00  move    #$2700, SR
00021292: 20 7c 00 00 00 00  movea.l #$0, A0
00021298: 4e 7b 88 01  movec A0, VBR; (1+)
0002129c: 33 fc 00 04 00 81 80 40  move.w  #$4, $818040.l
000212a4: 42 b8 80 18  clr.l   $8018.w
000212a8: 20 78 00 04  movea.l $4.w, A0
000212ac: 4e d0  jmp     (A0)
```

Consequence: the interpreter's shared exit at `0x225CE` is a **hard SSP reset**,
and everything after `0x225CE` in that block — `0x225D4`..`0x225E2`, including the
`clr.b D3` / `move.w D3,(A2)` flush — is **dead code**. See §11.6 (SR-13).

`0x2418` is pure computation, no port side effects. It is `0x0BC4`'s address
helper, inlined into `stunrun_gsp_display_list_sparse_fill()`:

```
00002418: 02 80 00 00 00 ff  andi.l  #$ff, D0
0000241e: c0 fc 28 00  mulu.w  #$2800, D0
00002422: 02 81 00 00 00 ff  andi.l  #$ff, D1
00002428: e9 89  lsl.l   #4, D1
0000242a: d0 81  add.l   D1, D0
0000242c: 06 80 02 00 00 00  addi.l  #$2000000, D0
00002432: 4e 75  rts
00002434: 30 31 32 33  move.w  ($33,A1,D3.w*2), D0
```

`D0 = 0x02000000 + (D0 & 0xFF) * 0x2800 + (D1 & 0xFF) * 16`.

### 11.4 The interpreter, in prose

**Historical decoding notes.** Register/control-flow observations are retained
for comparison with the listing. Output-value claims that depended on clearing
the high bytes of `D3` have been refuted; see the corrections below. This is not
a validated specification of the current C compiler.

Register roles, from the prologue at `0x225E4` and the loader it calls:

* `A0` — stream pointer. `$4,A7` (the return address's `long` argument) is loaded
  before the frame is pushed, so the calling convention is "push a pointer".
* `A1` — **not** set by the prologue. It is left at `$FF902A` by the loader
  `0x225B0`, whose final instruction is `lea $ff902a.l,A1`. Every table opcode
  indexes off `A1`, so the interpreter only works because the loader runs first.
* `A2` — `$C0000C` (HSTDAT, the host data FIFO).
* `A3` — `$608000` (the watchdog).
* `D2` — the "parked half-word" flag. `0` means clean; `0xFFFF` means the previous
  copy opcode left an odd trailing byte behind.
* `D3` — the current word being emitted.
* `D0` — opcode, then count.
* `D4`, `D5` — table indices.

Control flow:

```
0x225E4  A0 = ($4,A7); save D2-D5/A2-A3; A2 = HSTDAT; A3 = watchdog; D3=D5=D4=D2=0
0x22600  move.b (A0)+,D0 ; bne 0x225ce        <- a NONZERO first byte ends the list
0x22604  bsr.b 0x225B0                          <- load 16 table bytes
0x22606  LOOP HEAD: D0 = 0 ; clr.w (A3) ; move.b (A0)+,D0
0x2260c  bclr #7,D0 ; bne 0x226ca               <- 0x80..0xFF
0x22614  bclr #6,D0 ; bne 0x22672               <- 0x40..0x7F
0x2261a  tst.b D0 ; beq 0x22604                 <- 0x00 = reload tables, NOT a stop
0x2261e  ...copy...                              <- 0x01..0x3F
```

The loader's 16-byte table (verified against the interpreter's indices):

```
0x225B0  A1 = $FF902A
0x225B6  D0 = 0 ; D1 = 0x000F
0x225BA  16 x { D0.b = (A0)+            <- 16 stream bytes consumed
0x225BC          (A1-0x20).b = D0      <- byte i at $FF900A+2i
0x225C0          (A1).w = D0, (A1)+    <- word i at $FF902A+2i: low byte = byte i
0x225C2          dbra D1 }
0x225C6  A1 = $FF902A
```

`dbra D1` is at the **bottom** of the body, so the body runs 16 times, not 15.

The `move.w D0,(A1)+` writes **two** bytes: `$FF902A+2i` (which is `0x00`,
because the loader explicitly clears `D0` before its byte loads) and
`$FF902B+2i` (which is table byte `i`).
Since `A1` ends at `$FF902A`, the readers resolve as:

* `move.b ($1,A1,Dn.w),D3` at `$22690`, `$226B2`, `$2271C`, `$22726`, `$2273E`
  reads `$FF902B + Dn.w` — **a real table byte**, index `Dn.w / 2`.
* `move.w (-$20,A1,Dn.w),D3` at `$2268C`, `$226A4`, `$226C2`, `$22722`, `$2273A`
  reads the word at `$FF900A + Dn.w`; only the high byte was written by this
  loader. A following `move.b …,D3` changes only the low byte, not the high byte.

**Refuted claim:** these five word loads were called dead, and table output was
said to be always `$00bb`. That conclusion used incorrect `MOVE.B` semantics.
The high byte must be tracked through the actual path; SR-11 is not evidence
that the original ROM discards it. The C compiler's word mismatch remains open.

### 11.5 Complete opcode table

**Historical control-flow inventory, not accepted output counts.** In particular
the copy row's words-emitted column conflicts with its own `N/2` loop below.
Retain branch addresses for comparison, but do not turn these counts into new
oracle expectations without original-ROM differential evidence.

`D0` has already been reduced by the dispatch `bclr`s, so "opcode" below means the
original byte and `N` is the derived count.

| opcode | count `N` | entry | operands consumed | words emitted |
|---|---|---|---|---|
| `0x00` | — | `0x22604` | 16 (reloads the table) | 0 |
| `0x01`–`0x3F` | `opcode` | `0x2261E` | 1 flush byte if `D2 != 0`, then `N` raw bytes | `N` words, or `N+1` if `D2 != 0` |
| `0x40` | — | `0x22672` -> `0x225D4` | 0 | 0 (dead epilogue) |
| `0x41`–`0x7F` | `opcode - 0x40` | `0x22672` | 1 byte per word | `N` words |
| `0x80` | — | `0x226D4` -> `0x225CE` | 0 | 0 |
| `0x81`–`0xBF` | `opcode & 0x3F` | `0x226D8` | exactly 1 | `N>>1`, or `1 + ((N-1)>>1)` if `D2 != 0` |
| `0xC0` | — | `0x22704` -> `0x225CE` | 0 | 0 |
| `0xC1`–`0xFF` | `opcode & 0x3F` | `0x22708` | exactly 1 | `N` words |

Nonzero first byte: terminator, before the loader ever runs (`$22602 bne $225CE`).

**`0x01`–`0x3F` — copy, and the pointer-parity branch.** The count is spent in
words if the stream pointer is even, in bytes if it is odd (`$2262C`
`move.w A0,D1` / `andi.w #$1,D1` / `beq $22656`). A count of 0x0A therefore
consumes 10 bytes, not 5 words' worth — and because `$22622` consumes the flush
byte *before* `$2262C` reads the parity, the parity test always sees the
post-flush pointer. Each half:

```
even pointer, $22656 :  N/2 x { move.w (A0)+,(A2) }
                       odd N -> $2266a move.w (A0),D3   <- reads a WORD, no post-inc
                                 $2266c addq.l #1,A0     <- advances ONE byte
                                 $2266e not.w D2         <- parks
odd pointer, $22634  :  N/2 x { move.b (A0)+,D3 ; lsl.w #8,D3 ; move.b (A0)+,D3 ; move.w D3,(A2) }
                       odd N -> $2264e move.b (A0)+,D3 ; $22650 lsl.l #8,D3
                                 $22652 not.w D2
```

**Refuted defect interpretations:** a look-ahead word read is not the same as
advancing the stream by two bytes; `$2266C` explicitly advances by one. No stream
author's intended parity is established here. Likewise `$22650`'s low-word value
depends on the preceding register state, and `$2263E`'s shifted high byte survives
the following byte load. Earlier claims of a discarded first byte and a dead
shift were our misreading, not proven ROM defects (SR-11/SR-12 references in
`BUGS.md` require separate reconciliation outside this docs-only edit).

**`0x41`–`0x7F` — table run.** The nibble is chosen **once per opcode** at
`$22678`, not per word:

```
$D2 == 0 -> $2267C : byte -> D4,D5 ; D4 = (byte>>3)&0x1E ; D5 = (byte&0x0F)*2  ; index by D5 (LOW)
             dbra back to $2267C   (a fresh operand byte each word)
$D2 != 0 -> $226A8 : byte -> D4,D5 ; D5 = (byte>>3)&0x1E                      ; index by D5 (HIGH)
             dbra back to $2269E   (a dead block, so it lands on $226A8 each time)
```

The historical interpretation called `$2269E`/`$226BC` dead blocks. That output
claim is not established: `$226B2` replaces only `D3.b`, so the earlier word
load's high byte can survive. Reconcile the loop targets with complete register
state before using this as a compiler specification.
`D2` is **never cleared** on this path, so a stream that parks a word and then
runs a table opcode keeps `D2 = 0xFFFF` for the rest of the list. SR-13.

**`0x81`–`0xBF` — fill.** One operand byte, consumed at `$226D8` **before** the
`D2` test:

```
$226D8  move.b (A0)+,D3              <- the fill byte, always consumed
$226DA  tst.w D2 ; beq $226E8
$226DE  move.w D3,(A2)               <- emits the OPERAND, not the parked word
$226E0  D2 = 0 ; subq.w #1,D0 ; beq $22606
$226E8  lsr.w #1,D0                  <- D0 = N>>1, X = N&1
$226EA  subx.w D2,D2                 <- D2 = X ? 0xFFFF : 0
$226EC  move.w D3,D5 ; lsl.w #8,D3 ; move.b D5,D3   <- preserves shifted high byte
$226F2  subq.w #1,D0 ; bmi $22606
$226F8  move.w D3,(A2) ; dbra D0,$226F8
```

**Refuted claim:** `$226EC`–`$226F0` was described and implemented as a no-op.
`MOVE.B` preserves the shifted high byte; exact fill/parked-word behaviour must
be checked against original execution. The retained sequence is the evidence,
not the earlier prose value calculation.

**`0xC1`–`0xFF` — table fill.** One operand byte, and the two nibbles are used
**swapped** relative to `0x41`–`0x7F`:

```
$D2 == 0 -> $2273A : move.w (-$20,A1,D4.w),D3      <- high byte survives following MOVE.B
                   $2273E move.b ($1,A1,D5.w),D3   <- index = (byte&0x0F)*2   (LOW)
                   $22742 subq.w #1,D0 ; bpl $22730   <- writes the SAME word N times
$D2 != 0 -> $2271C : move.b ($1,A1,D4.w),D3       <- index = (byte>>3)&0x1E  (HIGH)
                   $22720 move.w D3,(A2)          <- emitted unconditionally
                   $22722 move.w (-$20,A1,D5.w),D3 <- high byte survives following MOVE.B
                   $22726 move.b ($1,A1,D4.w),D3   <- recomputed, same HIGH index
                   $2272A subq.w #2,D0 ; bmi $22606
                   $22730 move.w D3,(A2) ; dbra D0,$22730
```

The earlier assertion that both paths emit the same `$00bb` word is refuted:
the word loads and preserved high bytes cannot be discarded. This section does
not establish a corrected full word sequence. The listing retains the `D2`
state mechanism for a subsequent differential check.

### 11.6 Watchdog, address latching, and what the interpreter never does

**Historical interpretation; epilogue behaviour needs execution evidence.**
The raw `JSR $21286` at `$225CE` does not by itself prove an unreturning reboot;
caller differential tests in §11.0b observe returns through a sentinel. Do not
use the old "only way out resets SSP" conclusion as an accepted contract.

* `clr.w $608000.l` at `$22608` fires **once per opcode**, *before* the opcode byte
  is read at `$2260A`. The kick therefore interleaves with that opcode's words in
  any port trace rather than sitting in front of them. The test asserts this exact
  ordering.
* The interpreter **never writes HSTADRH, HSTADRL or HSTCTL.** It only writes
  HSTDAT. Where the words land is entirely the caller's business;
  `native/gsp_display_list_test.c` has
  `expect_no_address_writes()` to keep it that way, and a separate test that
  pre-sets `hstctl = 0x0800` and asserts the +$10 walk.
* The retained branches reach `$225CE` or `$225D4`; the former contains a
  `JSR`, and both lead into epilogue code. The old claim that all lists reboot
  and only `$40` returns was not established. Keep SR-14 as a historical
  reference pending independent reconciliation, not an original-ROM defect proof.

### 11.7 Proven vs inferred

**Evidence retained:** the mnemonic/byte listings, table-loader stores and
control-flow targets. **Not confirmed:** static hit counts as executed calls,
the missing ramp return boundary, dead word loads, zero odd-copy leftovers,
`$00bb`-only output, or an unreturning `$225CE`. These were overstatements in
the earlier analysis. Hand-derived tests encode the chosen C model and do not
independently settle those claims.

**Inferred** — reasonable, but not proven from these bytes:

* that the stream is a *display list* in the GSP's own instruction format. The
  interpreter's output goes to HSTDAT at whatever the caller latched, and the
  bytes look like indices and counts, but nothing here identifies the GSP's
  program encoding. §10's measurement (the GSP reads `$FFF40000`..`$FFF6F570`
  after the 68010 writes ~12,120 words there per frame) is the only link.
* that the 16 table bytes are a palette or glyph index. Nibble indexing is
  consistent with such a use, but does not identify game semantics or prove
  the previously asserted `$00bb` output.
* that `0x0BC4`'s `$400` row stride is a tile height in some unit. `0x2418` proves
  `0x2800` is a *column* pitch (`tile * 0x2800`), so `$400` is a third, smaller
  pitch, but nothing names it.
* that `$140`-wide FIFO runs inside `$400`-strided rows are "sparse" rather than
  "a bug". See §11.8.

### 11.8 Refutation: the `0x0BC4` nested fill is not buggy

The shared `dbra` target was flagged as suspicious before the routine was read.
It is **not** a defect, and no C behaviour was "corrected".

`dbra D2,$c06` and `dbra D4,$c06` both land on `$C06` because `$C06` *is* the
innermost body — the classic 68k idiom of one body shared by two loops:

* `D2 = D1 = column count`, so the inner loop runs `columns + 1` iterations of
  "write `$FFFF` at the latched address, write `$FFFF` at the next one" = 2 pairs.
  The `+1` is `DBRA`'s, and the ROM uses it everywhere (`dbra` with a count of `N`
  runs `N + 1` times).
* `$C0E` `addi.l #$400,D0` then `$C14` `move.l D0,$c00002.l` re-latch the *next*
  row, and `$C1A` reloads `D2` from `D1` because `$C06`–`$C0A` clobbered it.
* `dbra D4,$c06` with `D4 = 7` then re-runs that whole block **8 times**.

So the block `$C06`–`$C1C` executes 8 times, producing **8 rows strided `$400`
apart**, each row `2 * (columns + 1)` contiguous `$FFFF` words. Per record the
observable port traffic is:

| | count |
|---|---|
| HSTCTL `$1800` writes | 1 (only for the first record) |
| HSTADRH/HSTADRL latch pairs | 1 (control) + 1 (base) + 8 (row relatches) = 10 |
| HSTDAT words | 1 (the control word) + 8 * 2 * (columns + 1) |

Cross-checked against the real table. The single caller at `0x14CA`–`0x14D0` is
`lea $b9a.l,A0` / `bsr $bc4`, and the data at `0xB9A` is:

```
000b9a: 00 04 00 10 00 04 00 09 05 05 00 10 00 06 00 09
000baa: 0A 0A 00 10 00 08 00 09 10 10 00 10 00 0A 00 09
000bba: 64 64 00 10 00 0C 00 09 FF FF 00 79 18 00 00 C0
```

(the last six bytes are already the head of `ori.w #$1800,$c00008.l` at `0x0BC4`
— the table is 42 bytes, `$0B9A`..`$0BC3`, and abuts the code exactly.)

A 2-byte count word `0x0004` followed by **five** 8-byte records `[0]=row term,
[+2]=tile, [+4]=columns, [+6]=control`. `dbra D5` runs `0x0004 + 1 = 5` passes,
and `5 * 8` bytes from `0xB9C` ends at `0xBC4` — the first byte of the routine.
That is the proof that the seed-plus-one reading is right and the stride is 8.
Base addresses from `0x2418`: `$0200A100`, `$0200F100`, `$02014100`, `$02019100`,
`$0201E100`. Whole table: **50 address-latch pairs, 805 FIFO words.** The test
asserts exactly those numbers.

The genuine oddities here, all reproduced verbatim:

1. `$C1C`'s last pass relatches a base one row **past** the last row it fills
   (`base + $400 * 8`, i.e. `$0200C100` for the first record) and `$C22` immediately
   moves to the next record without writing there. Observable on the port, dead
   on screen. Not worth an entry — recorded here.
2. `$BCC move.w (A0)+,D5` post-increments past the count word and then `$BCE`
   re-reads `(A0)` for the row term; `$BEC move.w ($4,A0),D1` overwrites `D1` with
   the *column* count, so the row term survives only inside the address computed by
   `0x2418`. Inferred intent, no defect.

### 11.9 Verbatim: `0x0BC4`–`0x0CA8`, and its caller

```
00000bc4: 00 79 18 00 00 c0 00 08  ori.w   #$1800, $c00008.l
00000bcc: 3a 18  move.w  (A0)+, D5
00000bce: 32 10  move.w  (A0), D1
00000bd0: 30 28 00 02  move.w  ($2,A0), D0
00000bd4: 4e b9 00 00 24 18  jsr     $2418.l
00000bda: 23 fc f4 00 00 00 00 c0 00 02  move.l  #$f4000000, $c00002.l
00000be4: 33 e8 00 06 00 c0 00 0c  move.w  ($6,A0), $c0000c.l
00000bec: 32 28 00 04  move.w  ($4,A0), D1
00000bf0: 36 3c ff ff  move.w  #$ffff, D3
00000bf4: 43 f9 00 c0 00 0c  lea     $c0000c.l, A1
00000bfa: 23 c0 00 c0 00 02  move.l  D0, $c00002.l
00000c00: 34 01  move.w  D1, D2
00000c02: 38 3c 00 07  move.w  #$7, D4
00000c06: 32 83  move.w  D3, (A1)
00000c08: 32 83  move.w  D3, (A1)
00000c0a: 51 ca ff fa  dbra    D2, $c06
00000c0e: 06 80 00 00 04 00  addi.l  #$400, D0
00000c14: 23 c0 00 c0 00 02  move.l  D0, $c00002.l
00000c1a: 34 01  move.w  D1, D2
00000c1c: 51 cc ff e8  dbra    D4, $c06
00000c20: 50 88  addq.l  #8, A0
00000c22: 51 cd ff aa  dbra    D5, $bce
00000c26: 4e 75  rts
00000c28: 20 3c 02 00 00 00  move.l  #$2000000, D0
00000c2e: 32 3c 00 0f  move.w  #$f, D1
00000c32: 34 3c 00 01  move.w  #$1, D2
00000c36: 61 00 00 0e  bsr     $c46
00000c3a: 06 80 00 00 40 00  addi.l  #$4000, D0
00000c40: 53 41  subq.w  #1, D1
00000c42: 66 f2  bne     $c36
00000c44: 4e 75  rts
00000c46: 26 00  move.l  D0, D3
00000c48: 38 3c 00 20  move.w  #$20, D4
00000c4c: 23 c3 00 c0 00 02  move.l  D3, $c00002.l
00000c52: 33 c2 00 c0 00 0c  move.w  D2, $c0000c.l
00000c58: 06 83 00 00 00 20  addi.l  #$20, D3
00000c5e: 53 44  subq.w  #1, D4
00000c60: 66 ea  bne     $c4c
00000c62: 4e 75  rts
00000c64: 00 79 18 00 00 c0 00 08  ori.w   #$1800, $c00008.l
00000c6c: 23 fc ff 80 00 00 00 c0 00 02  move.l  #$ff800000, $c00002.l
00000c76: 3c 3c 00 f0  move.w  #$f0, D6
00000c7a: 61 00 00 0e  bsr     $c8a
00000c7e: 42 79 00 60 80 00  clr.w   $608000.l
00000c84: 51 ce ff f4  dbra    D6, $c7a
00000c88: 4e 75  rts
00000c8a: 30 3c 00 00  move.w  #$0, D0
00000c8e: 33 fc ff ff 00 c0 00 0c  move.w  #$ffff, $c0000c.l
00000c96: 32 3c 00 fa  move.w  #$fa, D1
00000c9a: 33 c0 00 c0 00 0c  move.w  D0, $c0000c.l
00000ca0: 06 40 01 00  addi.w  #$100, D0
00000ca4: 52 00  addq.b  #1, D0
00000ca6: 51 c9 ff f2  dbra    D1, $c9a
```

(`0x0C28`–`0x0C62` is `0x0C46`'s caller plus `0x0C46` itself: `D0 = $02000000`,
`D1 = 15`, `D2 = 1`, then **15** passes of "`bsr $c46`, `addi.l #$4000,D0`".
The earlier 16-pass claim confused `SUBQ/BNE` with `DBRA`. Inside
`0x0C46` the loop is `$C4C`..`$C60` with the literal count `$20` and
`subq.w #1,D4` / `bne` — **not** a `dbra` — so it runs exactly 32 times, latching
`base + $20 * i` and writing `D2` at each. The listing shows a `bsr` from
`0x0C36`; the earlier claimed `0x0C7E` call is actually a watchdog clear here;
**not** called by `0x0BF4` or `0x0BC4`.)

**Validated isolated conversion (2026-10-10):** `stunrun_gsp_display_list_fill_stride_blocks()`
models the bus effects of `$0C28` by calling the existing `$20`-stride helper
15 times. It emits **480** writes. For block `b = 0..14` and column `i = 0..31`,
the observed GSP word address is `$02000000 + b*$4000 + i*$20`, with data word
`1`. This is consistent with 16-byte host address-latch units and the ROM's
`addi.l #$4000,D0` / `addi.l #$20,D3`; it does not assign a rendering meaning.
The isolated MAME oracle call used the exact ROM bytes at `$0C28`, a synthetic
return sentinel, and captured FIFO writes while the caller PC was `$0C52`:

| HOSTCTL | FIFO writes | final host address high:low | final D0 |
|---|---:|---|---|
| `$A000` (auto-increment off) | 480 | `$0203:83E0` | `$0203C000` |
| `$A800` (auto-increment on) | 480 | `$0203:83F0` | `$0203C000` |

The native differential test compares all 480 ordered address/data pairs and
the final latches/control in each mode against the original ROM. The test is
opt-in via `STUNRUN_UPLOAD_CALLERS_ROMDIR`; the full native CTest suite passed
with it set. This verifies only the isolated caller's bus effects—not live boot
integration, pixels, or the separate `$225E4` compression interpreter.

**Partial ramp evidence:** `$0C64` sets HSTCTL, latches `$FF800000`, seeds
`D6 = $F0`, and calls `$0C8A`. The listed `$0C8A` prefix writes `$FFFF`, then
has a `D1 = $FA` ramp loop; the listing ends at its `DBRA`, without the
continuation or return. The previous **241 × 252 = 60,732-word** returning
sequence and endpoint `$FBFB` are therefore withdrawn as accepted results.
Boundary verification is required before choosing a faithful standalone slice;
the native port does not execute a TMS34010 core.

The caller of the sparse fill:

```
000014c8: 61 94  bsr     $145e
000014ca: 41 f9 00 00 0b 9a  lea     $b9a.l, A0
000014d0: 61 00 f6 f2  bsr     $bc4
000014d4: 4e 75  rts
```

### 11.10 Refutation of §10: the four `0x26088050` writes are address latches, not a +$10 walk

§10 closed with "routine `0x530` seeds GSP program memory directly, writing four
words at `0x26088050/190/210/350` through the +0x10 host auto-increment before
pointing the host at the 2bpp window". **That is wrong on two counts.** The
correct reading:

```
00000530: 43 f9 00 c0 00 02  lea     $c00002.l, A1
00000536: 45 f9 00 c0 00 0c  lea     $c0000c.l, A2
0000053c: 22 bc f4 80 00 70  move.l  #$f4800070, (A1)
00000542: 34 bc 00 00  move.w  #$0, (A2)
00000546: 22 bc f4 80 00 f0  move.l  #$f48000f0, (A1)
0000054c: 34 bc 00 00  move.w  #$0, (A2)
00000550: 22 bc 26 08 80 50  move.l  #$26088050, (A1)
00000556: 22 bc 26 08 81 90  move.l  #$26088190, (A1)
0000055c: 22 bc 26 08 82 10  move.l  #$26088210, (A1)
00000562: 22 bc 26 08 83 50  move.l  #$26088350, (A1)
00000568: 22 bc 02 00 00 00  move.l  #$2000000, (A1)
0000056e: 4e 75  rts
```

1. `A1` is `$C00002`, the **HOST_ADDRESS** window (`HSTADRH` at `+$02`,
   `HSTADRL` at `+$00`). So the four `move.l` are four **address latches**, not
   four FIFO words. The only HSTDAT writes in the whole routine are the two
   `move.w #$0,(A2)`.
2. The `+$10` post-increment lives in HSTCTL bit `$0800` and applies to **host
   data** writes. It is not set anywhere inside `0x530` — the nearest
   `ori.w #$1800,$c00008.l` instructions are at `0x570`, `0x636` and `0xC64`. So
   nothing auto-increments here at all.
3. The four addresses step by `$140`, `$080`, `$140`. Irregular, so they are not a
   contiguous walk even by accident.

So the four latched addresses are `$26088050`, `$26088190`, `$26088210`,
`$26088350`, and the last `move.l` re-latches the host at `$02000000` (the 2bpp
window). Whether the GSP can *decode* `$26088050` is not answerable from the
driver: `harddriv.cpp:619-629` decodes only `$00000000`–`$0000200F` (noprw),
`$02000000`–`$020FFFFF` (2bpp VRAM), `$F4000000`–`$FF` (control lo),
`$F4800000`–`$FF` (control hi), `$F5000000` (palette lo), `$F5800000`
(palette hi) and `$FF800000` (main VRAM). `$26088050` is in **none** of them.
Marked **inferred/unknown**, and left as such.

This routine was **already transcribed** before this section:
`stunrun_gsp_boot_latch_draw_parameters()` in `native/gsp_boot.c:360-380`, asserted
by `test_latch_draw_parameters` in `native/gsp_boot_test.c:532-560` (16 events, 2
FIFO words, final `hstadrh` `$0200`). Nothing new was added for it, on purpose —
a second C function would have been a duplicate of an existing, tested slice.

### 11.11 `0x44518` is a clone of `0x536`–`0x56E` with the `A1` prologue dropped

**Historical static candidate.** Keep the bytes and scan result, but the ROM-bank
decoding and "orphan code" classification are not current execution evidence.

```
00044518: 45 f9 00 c0 00 0c  lea     $c0000c.l, A2
0004451e: 22 bc f4 80 00 70  move.l  #$f4800070, (A1)
00044524: 34 bc 00 00  move.w  #$0, (A2)
00044528: 22 bc f4 80 00 f0  move.l  #$f48000f0, (A1)
0004452e: 34 bc 00 00  move.w  #$0, (A2)
00044532: 22 bc 26 08 80 50  move.l  #$26088050, (A1)
00044538: 22 bc 26 08 81 90  move.l  #$26088190, (A1)
0004453e: 22 bc 26 08 82 10  move.l  #$26088210, (A1)
00044544: 22 bc 26 08 83 50  move.l  #$26088350, (A1)
0004454a: 22 bc 02 00 00 00  move.l  #$2000000, (A1)
00044550: 4e 75  rts
00044552: 00 00 00 ff  ori.b   #$ff, D0
00044556: 2a 00  move.l  D0, D5
00044558: d7 09  addx.b  -(A1), -(A3)
```

It is **not** byte-identical to `0x530` and must not be described that way: it is
`0x536`–`0x56E` verbatim (56 bytes: `0x44518 + $38 = 0x44550` matches
`0x536 + $38 = 0x56E`) with the first instruction, `lea $c00002.l,A1`, removed.
So it needs `A1 == $C00002` on entry, which nothing establishes.

Nothing references it. A scan for `bsr`/`bsr.b`/`bra`/`jsr`/`jmp` targets finds
**zero** hits for `0x44518`, against exactly one `bsr.w` for `0x530` (`0x62C`).
The bytes before it disassemble as `movep.w` / `ori.b` fragments and the bytes
after `0x44550` are data (`00 00`, `00 3f` …), so it sits inside what looks like a
data island. SR-10.

### 11.12 The GSP init stub that calls `0x530`

```
00000618: 61 00 fe fe  bsr     $518
0000061c: 30 3c 00 00  move.w  #$0, D0
00000620: 61 00 fe f6  bsr     $518
00000624: 30 3c 00 08  move.w  #$8, D0
00000628: 61 00 fe ee  bsr     $518
0000062c: 61 00 ff 02  bsr     $530
00000630: 61 00 fe 36  bsr     $468
00000634: 4e 75  rts
```

The prefix calls `$518` with the incoming `D0`, then explicitly with 0 and 8,
before `$530` and `$468`. This is already represented inside
`stunrun_gsp_boot_walk_palette()` in `stun-runner/native/gsp_boot.{h,c}`, tested
by `test_walk_palette` in `stun-runner/native/gsp_boot_test.c`. No duplicate
standalone slice is needed; the old "nothing transcribed" claim was stale.

### 11.13 The C slice

| ROM | C | test |
|---|---|---|
| `0x225B0`–`0x225CC` | `stunrun_gsp_display_list_load_tables()` | `test_load_tables` |
| `0x225E4`–`0x22748` | `stunrun_gsp_display_list_run()` | 7 tests, see below |
| `0x0BC4`–`0x0C26` + `0x2418` | `stunrun_gsp_display_list_sparse_fill()` | `test_sparse_fill_one`, `test_rom_table_0xb9a` |
| `0x0C46`–`0x0C62` | `stunrun_gsp_display_list_fill_stride_0x20()` | `test_fill_stride_0x20` |
| `0x530`–`0x56E` | `stunrun_gsp_boot_latch_draw_parameters()` (**pre-existing**) | `test_latch_draw_parameters` |

`native/gsp_display_list.{h,c}` + `native/gsp_display_list_test.c`, registered as
CTest `gsp-display-list-slice-c`. The slice uses a v-table
(`stunrun_gsp_display_list_t`) shaped like `stunrun_gsp_host_t` from
`native/gsp_boot.h`, plus one extra member, `poke_byte`, because `0x225BC` stores
the table with `move.b`. Every expectation in the test is hand-derived from the
listing above; the C was never used to generate a number.

11 tests, covering: the whole opcode table in one list (even stream pointer), the
odd-pointer path and all three flush/parity transitions, an immediate terminate,
all three terminators, opcode `$00` table reload, the table loader in isolation,
the `$400`-strided fill over the **real** `0xB9A` table (805 words / 50 latches),
a one-record fill, the `$20`-stride fill, the HSTCTL `$0800` address walk, and
NULL-safety.

The four transcription bugs the test caught in *our own* C are listed in §9a's
spirit — briefly, because they are the reason the test asserts **addresses**, not
just words:

1. the loader loop ran 15 times instead of 16 (`dbra` sits at the bottom of the
   body);
2. `0x0C46` was written as a `dbra` when `$C5E`/`$C60` are `subq.w`/`bne`, giving
   33 passes instead of 32;
3. the table-run nibble was chosen per *word* instead of per *opcode*;
4. **Refuted test correction:** a retained high byte was called a C bug, and
   `$5A5B` was changed to `$005B` on the false premise that `MOVE.B` clears it.
   That premise remains in the current compiler/tests; do not report it fixed.

The first three are historical hand-derived regression findings. Item 4 is a
misreading requiring compiler differential repair, not proof of ROM fidelity.

### 11.14 MC68010 direct GSP-VRAM clear at `$223F4`

The first writer attribution from the live Musashi run identified PC `$22418`
as the dominant regular-VRAM writer (524,288 host words at frame 600). The
containing routine was transcribed before writing the C slice:

```asm
$223F4:  movea.l #$c0000c,A0
$223FA:  move.l  #$ff800000,$c00002.l
$22404:  move.w  #$b800,$c00008.l
$2240C:  moveq   #$4,D1
$2240E:  move.l  #$ffff,D0
$22414:  bra     $2241C
$22416:  clr.l   (A0)
$22418:  dbra    D0,$22416
$2241C:  clr.w   $608000.l
$22422:  dbra    D1,$22416
$22426:  rts
```

The host-port setup selects GSP address `$FF800000`, sets HSTCTL to `$B800`,
and `clr.l (A0)` emits two zero HSTDAT words while the host address advances.
The first `dbra D1` is reached before the first clear pass, so the seed `4`
produces four outer passes, each containing 65,536 longword clears: 524,288
observable host words. The watchdog clear is repeated once per outer pass.

This is **confirmed boot initialization**, not a scene-drawing routine. The
same live run attributed the later direct writers to `$226FA`, `$22732`,
`$2FE6E`, and `$2FE76`, while 2-bpp writes came chiefly from `$0069C`,
`$021734`, `$234CE`, and `$234E0`. Those are separate follow-up targets.

The tested mechanism slice is `stun-runner/native/gsp_vram_clear.{h,c}` with
CTest `gsp-vram-clear-slice-c`. It models the observable host-port sequence,
not the absent TMS34010 execution. The attribution data was captured with the
native shell's GSP host-write dump; each record now includes the MC68010 PC.

### 11.15 MC68010 reversed host-data word pair at `$2FE5E`

The live Musashi trace reached this helper repeatedly during the first frame.
Its complete transcription is:

```asm
$2FE5E:  link    A6,#$0
$2FE62:  lea     ($8,A6),A0
$2FE66:  addq.l  #2,A0
$2FE68:  move.w  (A0),$C0000C.l
$2FE6E:  move.w  ($8,A6),$C0000C.l
$2FE76:  unlk    A6
$2FE78:  rts
```

`$C0000C` is the TMS34010 host-data register. With the usual 68k stack
layout, `$8(A6)` is the first word argument and `$A(A6)` is the second. The
routine therefore emits the second word first, then the first word. This is
confirmed mechanism; the meaning of the pair depends on the caller and is not
yet identified as text, display-list, or direct-VRAM data. The C slice is
`native/gsp_word_pair.{h,c}` and its observable order is asserted by
CTest `gsp-word-pair-slice-c`.

### 11.16 MC68010 2-bpp glyph-row expansion at `$234B6`

The first non-initialization writes that can plausibly carry the diagnostic
letters are the 2-bpp host writes at `$234CE` and `$234E0`. The relevant ROM
range is:

```asm
$234B6:  move.w  (A1)+,D5
$234B8:  move.w  #$F,D7
$234BC:  clr.l   D6
$234BE:  roxl.w  #1,D5
$234C0:  roxl.l  #2,D6
$234C2:  dbra    D7,$234BE
$234C6:  lsr.l   #1,D6
$234C8:  move.w  D6,$C0000C.l
$234CE:  swap    D6
$234D0:  addi.l  #$10,$C00002.l
$234DA:  move.w  D6,$C0000C.l
$234E0:  rts
```

The caller at `$23494` latches a destination and repeatedly advances it by
`$400`; `$234B6` converts one 16-bit source row into a 32-bit packed value,
emits its low half, advances the GSP host address by `$10`, and emits the
swapped high half. The initial X flag is not set in this routine; the C seam
therefore exposes it rather than silently choosing a caller policy. The tested
slice is `native/gsp_2bpp_glyph.{h,c}` with CTest
`gsp-2bpp-glyph-slice-c`. This establishes the glyph transport, but not yet
the source table's character encoding or the final raster placement.

### 11.17 MC68010 geometry primitive emitter at `$2F470` and caller `$2B7E8`

The pinned static listing gives this FIFO emitter:

```asm
$2F470: link    A6,#$0
$2F474: movem.l D2-D3,-(A7)
$2F478: move.w  ($A,A6),D3
$2F47C: move.w  ($E,A6),D2
$2F480: move.w  ($12,A6),D1
$2F484: moveq   #$0,D0
$2F486: move.b  ($1B,A6),D0
$2F48A: addi.l  #$2000,D0
$2F490: move.w  D0,$C0000C.l
$2F496: move.w  D3,$C0000C.l
$2F49C: move.w  D2,$C0000C.l
$2F4A2: move.w  D1,$C0000C.l
$2F4A8: move.w  ($16,A6),$C0000C.l
$2F4B0: movem.l (-$8,A6),D2-D3
$2F4B6: unlk    A6
$2F4B8: rts
```

This emits five halfwords to the GSP FIFO: `$2000 + byte 5`, then the low
words of arguments 1–4. The field meanings are not known. The caller is:

```asm
$2B7E8: link    A6,#$0
$2B7EC: movem.l D2-D4/A2,-(A7)
$2B7F0: move.l  ($8,A6),D2
$2B7F4: move.l  ($C,A6),D3
$2B7F8: move.l  ($10,A6),D4
$2B7FC: movea.l #$2F470,A2
$2B802: move.l  D4,-(A7)
$2B804: move.l  D3,-(A7)
$2B806: move.l  D2,D1
$2B808: addi.l  #$80,D1
$2B80E: move.l  D1,-(A7)
$2B810: move.l  D3,-(A7)
$2B812: move.l  D2,-(A7)
$2B814: jsr     (A2)
$2B818: move.l  D4,-(A7)
$2B81A: move.l  D3,D1
$2B81C: moveq   #$38,D0
$2B81E: add.l   D0,D1
$2B822: move.l  D1,-(A7)
$2B824: move.l  D2,D1
$2B826: addi.l  #$80,D1
$2B82C: move.l  D1,-(A7)
$2B82E: move.l  D3,-(A7)
$2B830: move.l  D2,D0
$2B832: addi.l  #$80,D0
$2B836: move.l  D0,-(A7)
$2B838: jsr     (A2)
$2B83A: move.l  D4,-(A7)
$2B83C: move.l  D3,D1
$2B83E: moveq   #$38,D0
$2B840: add.l   D0,D1
$2B842: move.l  D1,-(A7)
$2B844: move.l  D2,-(A7)
$2B846: move.l  D3,D1
$2B848: moveq   #$38,D0
$2B84A: add.l   D0,D1
$2B84C: move.l  D1,-(A7)
$2B84E: move.l  D2,D0
$2B850: addi.l  #$80,D0
$2B856: move.l  D0,-(A7)
$2B858: jsr     (A2)
$2B85A: move.l  D4,-(A7)
$2B85C: move.l  D3,-(A7)
$2B85E: move.l  D2,-(A7)
$2B860: move.l  D3,D1
$2B862: moveq   #$38,D0
$2B864: add.l   D0,D1
$2B866: move.l  D1,-(A7)
$2B868: move.l  D2,-(A7)
$2B86A: jsr     (A2)
```

Resolving the callee's frame slots against the caller's right-to-left pushes,
the four records are, in FIFO argument order:

```text
(D2,       D3,       D2+$80, D3,       D4)
(D3+$38,  D2+$80,   D3,      D2+$80,  D4)
(D3+$38,  D2,       D3+$38, D2+$80,  D4)
(D3,       D2,       D3+$38, D2,      D4)
```

Each row is passed to `$2F470`, which prepends `$2000 + low_byte(D4)` and
emits the four low words. D1 is a live caller register, not a formal
argument loaded by `$2B7E8`; therefore the native seam keeps D1 explicit
instead of inventing its meaning. The C++ transcription is
`native/geometry_fifo_emitter.{hpp,cpp}` and CTest
`geometry-fifo-emitter-cpp` asserts the complete 20-halfword observable FIFO
stream. This is a FIFO mechanism finding, not proof that these records are
text or any particular polygon type.

## 12. The display path — the picture is in the board share, and the rasteriser is now verified

This section closes the `rowaddr → vram_base` contradiction left open in §11, and
records the first end-to-end reproduction of a STUN Runner frame outside MAME.

### 12.1 The `gsp_vram` share is 512 KiB, not 4 MiB

Everything that looked wrong about the VRAM address map follows from one wrong
constant. MAME's `multisync_gsp_map` maps

```c
map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram")
```

(`mame-ref/src/mame/atari/harddriv.cpp:628`). The *window* is 4 MiB and is
mirrored every 4 MiB, but the **share itself is only 0x80000 bytes** — 262144
words. The probe added to `scanline_multisync()` prints
the resolved answer rather than an inferred one:

```
STUNRUN-DPY call=1 f=600 rowaddr=003c coladdr=0400 yoffset=0 heblnk=110 hsblnk=622
                 vcount=0013 vsblnk=0103 veblnk=0013 palbank=0 finescroll=7
  BASE off=61440 (0x00f000) byte=0x01e000 vram_words=262144 mask=0x003ffff
        first10=2727^27272727^27272727^27272727^27272727^2727
```

`off` is a **word** offset, `rowaddr << 10` (0x3c << 10 = 0xF000), and
`m_vram_mask = m_gsp_vram.bytes()/2 - 1 = 0x3FFFF`. Confirmed by
`stun-runner/docs/tms34010-reference.md` §1: the refresh address uses logical
bits 4–23, and a display row is `1 << (DUDATE_bit + 7)` bytes — DUDATE = 4 here,
so 512 bytes per line and `rowaddr` advances every 4th line, which is exactly
what the GSPLINE trace shows (0x3c→0x77 over 240 visible lines).

Earlier confusion: an old oracle dump read 4 MiB from the GSP address space, so
the 512 KiB share appeared 8 times, and the replicated 16-byte cells made the
picture *look* like it lived at `0x0c0000` with a 4096-byte row pitch.

`MUSASHI_GSP_VRAM_BYTES` in `stun-runner/native/musashi_cpu.h:61` was
`0x400000u`; it is now `0x80000u`.

### 12.2 The host write expands to a 16-byte cell

Proven by direct write/read probes against the live program space:

| write | result over 16 bytes at `off = addr & ~0xF` |
|---|---|
| `write_u16(A, 0x1234)` | `off+0..7 = 0x34`, `off+8..15 = 0x12` |
| `write_u16(A, 0xabcd)` | `off+0..7 = 0xcd`, `off+8..15 = 0xab` |
| `write_u32(A, 0xdeadbeef)` | 32 bytes: `ef`, `be`, `ad`, `de` each ×8 |

Reads broadcast too: `read_u16` at every offset of a cell returns the same
word, and `read_u32` at `+0/+4/+8` gives `0x00001234, 0x00001234, 0x00000012`.
Cause: the GSP program space is `memory_access<32, 1, 3, little>`
(`mame-ref/src/devices/cpu/tms34010/tms34010.h:992`), i.e. 32 bits wide with
`AddrShift = 3`.

The captured examples include palindromic words (`0x8484`, `0x8181`, `0x3030`).
This does not establish that **every** 68010 word has that form, or the author's
intent. For such a word, even/odd colour-index byte selection yields the same
index; compiler parity still requires complete address/word stream comparison.

### 12.3 Palette

1024 pens, **4 banks × 256**, bank selected by `control_hi` offsets `0x02`/`0x03`;
offset `0x04` applies only to family configurations with at least 2048 entries
(`mame-ref/src/mame/atari/harddriv_v.cpp:211-256`, `update_palette_bank`). Colour
is `red=(lo>>8)&0xff, green=lo&0xff, blue=hi&0xff` (`harddriv_v.cpp:326-331`).
At frame 600 MAME's palette holds 704 distinct colours and
**RGB(177,46,36) — the dominant tunnel red — is pen 42.**

### 12.4 The rasteriser is verified

**Historical image/colour-distribution comparison, not current pixel-exact
acceptance.** The reported `MATCH` belongs to that experiment. Its 512×240 output
differs from the configured 512×228 visible area; keep these dimensions distinct.

`stun-runner/tools/gsp_render.py` is a from-scratch transcription of
`scanline_multisync()` + `get_display_params()` + `gsp_palette_change()`.
Against MAME's own dump of the share and palette at frame 600:

```
rendered /tmp/frame600.png  512x240
  distinct colours          : 47
    RGB(177, 46, 36)   21.71%      RGB(182,151,120)   16.03%
    RGB(151, 40, 32)    5.60%      RGB(138, 37, 30)    5.34%
    RGB(  0,  0,  0)    5.33%      RGB(114, 75, 42)    4.88%
    RGB( 74, 51, 30)    4.24%      RGB( 94, 63, 36)    3.87%
  RESULT: MATCH
```

Every reference colour matches to 0.01%. **This is the whole picture** — tunnel,
road, ship, HUD, `TIME 0:35.0 / LEVEL 1 / SCORE 200 / MPH 955 / Credits: 0` —
produced from VRAM plus palette alone. Three bugs had to be fixed to get here,
all of them in our own harness rather than in the model: the palette is 1024
consecutive LE `uint16` entries (not two interleaved halves); the VRAM must be
read as 16-bit **words** (returning the raw byte string shifted every row by
half); and `DPYADR` only advances while `veblnk <= v < vsblnk`.

### 12.5 Where our port stands against that oracle

**Historical checkpoint measurements.** No fresh 600-frame comparison was run
for this cleanup. Matching a sampled PC to an earlier sampled PC does not
measure execution delay or identify a blocking handshake.

With `MUSASHI_GSP_VRAM_BYTES` corrected and a 600-frame run:

| region | ours vs MAME |
|---|---|
| `gsp_vram.bin` (512 KiB share) | **5.70%** byte-equal; ours 477,272 nonzero vs MAME 320,566 |
| `gsp_pal_lo.bin` | 191/2048 (9%) |
| `gsp_pal_hi.bin` | 854/2048 (42%) |
| host-port writes | 962,546 (`vram=843688 vram2bpp=51200 pal_lo=1024 pal_hi=1024 control=35 unmapped=65575`) |
| `total_cycles` | 81,335,846 (MAME ≈ 79.7M) |
| `pc` at frame 600 | ours `0x0002FCAC`; MAME `0x00022490` |

Rows `0x3c`–`0x77` — the rows the display actually scans — are **0.00% equal**,
and they are the rows MAME's *TMS34010* filled. That is the expected result: we
have the 68010, the host port, the memory model and a verified rasteriser, but
no GSP core, so nothing has executed the microcode that draws the picture. Ours
is also *denser* than MAME's (rows `0x00`–`0x0f` are ~1970 nonzero for us and
zero for MAME), i.e. our boot fill covers ground MAME leaves clear.

Note also `pc 0x0002FCAC` is exactly MAME's frame-300 PC from its PC-vs-frame
series, so with matched cycle counts our 68010 is roughly half a game's worth
of work behind — worth chasing, but orthogonal to the display.

### 12.6 What this implies for the remaining work

The historical "two parts done, only a GSP core missing" conclusion is withdrawn.
Separate the acceptance boundaries:

1. **Host → memory:** slice tests and observed program-space cells do not prove
   every live FIFO word/address is faithful.
2. **Memory → screen:** the image/colour-distribution comparison is useful
   evidence, not a new pixel-exact or timing acceptance verdict.
3. **Content production:** no native GSP instruction execution exists. Caller
   conversion is tested separately from C compiler parity (§11.0b). The old core
   proposal and `$340000-$379000` "share offsets" are obsolete guidance: those
   offsets exceed the 512 KiB backing share.

Do not infer current implementation scope from this checkpoint alone.
`docs/tms34010-reference.md` and MAME's
`mame-ref/src/devices/cpu/tms34010/34010dsm.cpp` / `34010ops.h` remain references,
not evidence that native GSP semantics have been implemented.

## 13. The stall — `stun-runner-s5r` — host-window register decode (REFUTED)

The historical frame-600 sample recorded 81,335,846 cycles (MAME ≈79.7M), a
PC also sampled in MAME at frame 300, and fewer populated palette entries.
Those observations motivated `stun-runner-s5r`; they do **not** prove "half the
work" or a handshake that never completes. The rejected decode hypothesis and
its probe evidence survive below.

### 13.1 The suspect, and why it looked right

`stun-runner/native/musashi_cpu.c:716` decodes the TMS34010 host-window byte
offset with

```c
static unsigned gsp_host_reg(unsigned byte_offset)
{
    return (byte_offset >> 2u) ^ 1u;
}
```

reading it as "emumem hands the handler a WORD index, and then
`harddriv_m.cpp:150` applies `offset = (offset / 2) ^ 1` on top, so two shifts
compose". That composition puts HSTADRH at `0xC00000-0xC00003`, HSTADRL at
`0xC00004-0xC00007`, **HSTCTL at `0xC00008-0xC0000B` and HSTDAT at
`0xC0000C-0xC0000F`**.

The competing reading — that `harddriv_m.cpp` receives a raw **byte** offset and
`reg = (byte_offset / 2) ^ 1`, giving HSTDAT at `0xC00006-0xC00007` — is
supported by the source text, because `emumem.h`'s `dispatch_write` passes
`offset` through unchanged. On paper the second reading is better. **It is wrong.**

### 13.2 Measurement, not argument

Both sides were instrumented and run over the same 600 frames.

- MAME: `mame-ref/src/mame/atari/harddriv_m.cpp`, `stunrun_hostprobe()` records
  every entry to `hd68k_gsp_io_w`. Enabled with `STUNRUN_GSP_HOSTPROBE=<path>`.
- Ours: `musashi_cpu.c`, `stunrun_hostprobe()`, same env var.

```
MAME (raw = the WORD index its handler receives)
  reg 0 HSTADRL   3023    first raw=0x02 data=0x0100
  reg 1 HSTADRH   3023    first raw=0x01 data=0xc000
  reg 2 HSTDAT  1101056    first raw=0x06 data=0xb800
  reg 3 HSTCTL     162    first raw=0x04 data=0xa000
  raw-offset histogram (raw & 15): 01:3023 02:3023 04:162 06:786104 07:314952

ours (raw = the BYTE offset)
  reg 0 HSTADRL      0
  reg 1 HSTADRH      0
  reg 2 HSTDAT  372722    first raw=0x0c data=0xb800
  reg 3 HSTCTL     19     first raw=0x08 data=0xa000
  raw-offset histogram (raw & 15): 08:19 0c:372722
```

**MAME's `raw=6` and our `raw=0x0c` are the same access**: word index 6 is byte
address `0xC0000C`. The two probes agree on the register sequence and on the
first data word (`0xb800` at HSTDAT, `0xa000` at HSTCTL). The decode in place
before this investigation was correct.

The decisive test was to change the decode and watch it break: with
`(byte_offset / 2) ^ 1`, HSTDAT moves to `0xC00006`, the ROM never touches it,
and **the frame-600 host-write count falls from 372,769 to ZERO**. A wrong fix
that produces a total, not partial, failure.

### 13.3 What the two probes *do* say

The counts differ by a factor of ~3 (372,722 vs 1,101,056 HSTDAT), which is the
same ~3× gap as the PC lag — consistent with the 68010 simply being behind, not
with a decode error. HSTADRL/HSTADRH show **0 writes** in our probe against 3,023
each in MAME's: our ROM issues those as **32-bit** `move.l` writes to
`0xC00002`, which `m68k_write_memory_32` splits into two 16-bit host writes at
`0xC00002`/`0xC00004`; the probe is only called from the 16-bit path, so those
land outside it. Not a defect — a probe-coverage gap.

**Conclusion: the stall is NOT a host-register decode bug.** It remains open at
`stun-runner-s5r`; the next steps from the bead stand — classify the remaining
`unmapped` writes by address, diff the first divergent instruction against MAME,
and find the read the CPU is spinning on. Note that with the decode correct, the
earlier suspicion (that a missing co-processor blocks a FIFO) is unchanged; the
TMS34010 is still absent, and `4vf`/`o1p` still gate on it.

### 13.4 Standing lesson

`emumem.h`'s `dispatch_write` passing `offset` through unchanged does **not**
imply the handler sees a byte offset — the reduction to a word index happens
earlier, when the address space's `addr_shift`/data-width are applied at map
install time. Reading one layer of `emumem` and inferring the units is not
enough; instrument both sides and compare the ROM's own traffic. Two probes on
opposite sides of an emulator boundary, driven by the same env var, settle in
one build what an afternoon of source reading did not.

---

## 14. The JSA-II sound command path — every 68010 access is a byte access (`$600000`)

Read while implementing the 68010→JSA transport (bead `stun-runner-bzq`, D1/D16).
Transcribed per the protocol: address range, ROM file, verbatim bytes, inferred
semantics with the inferences flagged, alternatives, and the C slice.

### 14.1 Method

ROM file offsets **are** maincpu addresses: `ROM_START(stunrun)` loads six 128 KiB
pairs flat at `mame-ref/src/mame/atari/harddriv.cpp:2691-2704` —
`136070-{2102.200r,2101.210r}` at `0x000000`, `{6104.200s,6103.210s}` at `0x020000`,
`{6106.200t,6105.210t}` at `0x040000`, `{2108.200u,2107.210u}` at `0x060000`,
`{2110.200v,2109.210v}` at `0x080000`, `{2112.200w,2111.210w}` at `0x0A0000`.
Reassembled to a 1 MiB flat image and disassembled with capstone 5.0.7
(`CS_ARCH_M68K`, `CS_MODE_BIG_ENDIAN`). [C]

### 14.2 Finding — every access to `$600000` is `move.b`. There is no `move.w`/`move.l`.

A byte-pattern scan for absolute-long `$600000` operands across the whole 1 MiB
image returns exactly five instructions:

| Address | Instruction | Role |
|---|---|---|
| `$008DCC` | `move.b $600000.l,D0` | boot handshake, poll for `$FF` |
| `$008DDA` | `move.b #$7,$600000.l` | boot handshake, the JSA kick command |
| `$008DFA` | `move.b $600000.l,D0` | boot handshake, read back |
| `$023EC0` | `move.b D0,$600000.l` | sound-command queue, first store |
| `$023EF6` | `move.b D0,$600000.l` | sound-command queue, retry store — **the one the MAME tap caught** |
| `$023F66` | `move.b $600000.l,(A4)` | IRQ4 handler, response read |

No word or long access to or from the window exists anywhere in the 68010 ROM.
This matters because it means the multi-byte split behaviour in the transport
(§14.5) is **unobservable in practice** — see `docs/jsa2-sound-findings.md` §5.3.

### 14.3 Verbatim: the sound-command queue, `$023EB0`–`$023F02`

```asm
023eb0  0839000f00a80000   btst.b  #$f,$a80000.l
023eb8  671a              beq.b   $23ed4
023eba  4aa90004          tst.l   $4(a1)
023ebe  6614              bne.b   $23ed4
023ec0  13c000600000      move.b  d0,$600000.l      <-- command write #1
023ec6  4840              swap    d0
023ec8  13400002          move.b  d0,$2(a1)
023ecc  23480004          move.l  a0,$4(a1)
023ed0  7001              moveq   #$1,d0
...
023ee0  2001              movea.l d0,a0
023ee2  302f0006          move.w  $6(a7),d0
023ee6  40c1              move.w  sr,d1
023ee8  46fc2400          move.w  #$2400,sr
023eec  0839000f00a80000  btst.b  #$f,$a80000.l
023ef4  670c              beq.b   $23f02
023ef6  13c000600000      move.b  d0,$600000.l      <-- command write #2 (the tapped one)
023efc  46c1              move.w  d1,sr
023efe  7001              moveq   #$1,d0
023f00  6008              bra.b   $23f0a
```

**Semantics.** *(inferred, but tightly: the gate shape, the queue-pair write and
the `swap` are read directly from the instructions, not inferred — what is
inferred is the meaning of the gate bit and of the two-word queue.)* The routine
runs with interrupts masked at `$0240`, gates on bit 15 of `$A80000` **set**,
checks `$4(A1)` for an empty queue word, stores the command byte to `$600000`,
then completes the queue entry with a `swap`ped copy of the command at `$2(A1)`
and the current `A0` at `$4(A1)` as a timestamp. The `$023EE0` entry is the retry
path: it re-reads the command from `$6(A7)` and re-attempts the store with SR
saved and restored, which is why the tapped PC is the *second* store and not the
first. [C for the instruction semantics, I for "timestamp" and for the reason the
gate bit exists.]

**Alternative reading, not ruled out:** `move.l a0,$4(a1)` could be a pointer to
a second command rather than a timestamp. Nothing observed here distinguishes
them, and it does not matter for this slice — the port models the latch, not the
firmware's queue.

### 14.4 Verbatim: the IRQ4 response read, `$023F66`, and the boot handshake

```asm
; IRQ4 handler at $023F0C
023f28  43f900ff9470      lea.l   $ff9470.l,a1
...
023f66  10b900600000      move.b  $600000.l,(a4)   <-- response read
023f6c  4cdf0301          movem.l (a7)+,d0/a0-a1
023f70  4e73              rte
```

```asm
; boot-time JSA handshake, bank 0
008db8  303900604000      move.w  $604000.l,d0
008dbe  323c00b4          move.w  #$b4,d1
008dc2  4eb9000013ec      jsr     $13ec.l           <-- delay loop
008dc8  5341              subq.w  #$1,d1
008dca  66f6              bne.b   $8dc2
008dcc  103900600000      move.b  $600000.l,d0
008dd2  0c0000ff          cmpi.b  #$ff,d0
008dd6  6600003e          bne.w   $8e16
008dda  13fc000700600000  move.b  #$7,$600000.l     <-- JSA kick command $07
008de2  4eb9000013ec      jsr     $13ec.l
008dee  303c0002          move.w  #$2,d0
008df2  4eb9000065e0      jsr     $65e0.l
008df8  4240              clr.w   d0
008dfa  103900600000      move.b  $600000.l,d0
```

**This is the open-bus handshake, and it settles the read direction.** The boot
writes `$FF` to the latch area, spins on a 180-iteration delay, and reads back:
it expects `$FF`, i.e. *nothing is there*. That is exactly the value our
`stunrun_jsa_transport_read()` returns when no response is pending, so the boot
sequence behaves identically with the transport installed as it did with the old
stub. [C for the code, I for the "open bus" reading — the alternative is that the
latch genuinely returns `$FF` until the 6502 answers, which is indistinguishable
here and irrelevant to us because we do not emulate the 6502.]

`move.b #$7,$600000.l` is the JSA kick: command `$07`. Under our translation
chain it reaches `oki_cmdmap_translate()`, which is what "the board answers
something" looks like at the port boundary. The command is one of the 32
class-8-class-and-below commands; **YM2151 is out of scope** and no class-8
handling was added.

**A trap worth recording:** `move.b #imm,<ea>.l` is **8 bytes** — opcode 2,
immediate word 2, absolute-long 4 (`13 fc 00 07 00 60 00 00`). The CTest that
imitates this instruction initially encoded it as 6 bytes and failed with
`latch byte == 0xFF (got 221, want 255)`, because the truncated encoding
produced a different opcode byte entirely. The test now asserts the real ROM
bytes at `$008DDA`–`$008DE0` when `STUNRUN_JSA_TRANSPORT_ROMDIR` is set.

### 14.5 The byte lane is `$FF00` — confirmed twice, once by the bus and once by the tap

A 68000/68010 byte access to an **even** address drives D8–D15, so MAME reports
`mem_mask == 0xFF00`. That is:

1. exactly the `mem_mask` hardcoded in `harddriv.cpp:4806` when the handler is
   installed over `0x600000-0x603fff`; **[M]**
2. exactly what the recorded tap shows — frame 447, PC `0x023EF6`,
   `bus_data 0x1E1E`, `mask 0xFF00`, `candidate_byte 0x1E`
   (`reference/experiments/stunrun/sound-boundary-tap.metadata.json`); **[C]**
3. exactly what `stunrun_jsa_command_byte(bus, 0xFF00)` returns. **[C]**

So the lane is **not a choice**, and the store of `0xAABBCCDD` with mask `$FF00`
yields **`0xCC`**, not `0xAA`. Odd-address byte accesses use lane `$00FF`; Musashi
passes the byte value in the low 8 bits and carries the lane in `ea & 1`
(`m68k_op_move_8_ai_al()`, `third_party/musashi/m68kops.c:17038`).

**Widths 2 and 4 are inference and are flagged as such.** `map().rw()` defaults to
`AMODE_DEVICE_WIDTH_8BIT`, so MAME should split a multi-byte access into per-byte
handler calls, big-endian, leaving the last byte in the single-byte holding
register. **[I]** This could **not** be confirmed: `src/emu/memory.h` and
`src/emu/memory.cpp` are trimmed out of *both* `/tmp/mame-src` and
`mame-ref/`, so `memory_device::write`'s split/merge arithmetic was never read.
§14.2 shows it does not matter — the ROM never issues a wider access.

### 14.6 Refutation: there is no per-address command/response classification on the 68010 side

The obvious design — classify a 68010 address as "command" or "response" the way
the 6502 side is classified — has **no** basis in MAME. `harddriv.cpp:4806`
installs *one* handler pair across the entire window with no offset decode:

```
install_readwrite_handler(0x600000, 0x603fff,
    read8smo_delegate(*m_jsa,  FUNC(atari_jsa_base_device::main_response_r)),
    write8smo_delegate(*m_jsa, FUNC(atari_jsa_base_device::main_command_w)),
    0xff00);
```

The direction is carried by the **access**, not by the address: a store is always
a command, a load is always a response. `stunrun_jsa_classify_read()` and
`stunrun_jsa_is_response_reg()` classify the **6502-side** registers (`$2802`/
`$280A`, `$2804`, `$2806`, `$2A02`) and have no 68010-side analogue. Building the
68010 transport on them would have invented a decode that the hardware does not
perform.

### 14.7 The C slice

- `native/jsa_transport.h` / `native/jsa_transport.c` — `stunrun_jsa_in_window()`,
  `stunrun_jsa_lane_mask()`, `stunrun_jsa_command_bytes()` (pure classification),
  `stunrun_jsa_transport_write()` (calls `stunrun_jsa_main_command_write()` per
  byte, so a later byte overrides an earlier one), `stunrun_jsa_transport_read()`.
  The header forward-declares `struct stunrun_jsa_latches;` so `musashi_cpu.h`
  stays free of any `../reproduction/` include.
- `native/musashi_cpu.c` — `REGION_SOUND` removed from every no-op group; each of
  read8/read16/read32/write8/write16/write32 now calls the transport.
- `native/shell.c` — `musashi_cpu_set_jsa_latches(g_cpu, g_sound_dispatch_active
  ? &g_sound_dispatch.latches : NULL)` binds the **live** dispatcher's latches.
- `native/jsa_transport_musashi_test.c`, CTest `jsa-transport-musashi-c` —
  **220 checks with ROMs, 119 without.** It plants synthetic `move.b`
  instructions in main RAM at `0x00FFF000` and runs them on the **real Musashi
  core**, so the routing under test is the routing the game uses. Asserts: the
  store lands the right byte; `0xAABBCCDD` with mask `$FF00` → `0xCC` and the
  wrong mask → nothing; a read does not mutate `command_pending`; a store at
  `$604000` is a no-op; the ROM bytes at `$023EF6`/`$023EC0`/`$008DDA`; and
  end-to-end command `$38` → `sound_dispatch_frame()` → `oki6295_is_playing()` →
  PCM at a stub `stunrun_audio_backend_t` with `sample_rate == 9039` and
  `format == STUNRUN_AUDIO_FORMAT_S16LE_STEREO`.

## 13. Frame-120 text path: `$508A` → `$25E8`

The normal and F2-input runs show the same frame-120 text. A bounded MC68010
instruction trace through frame 120 (`reference/experiments/stunrun/frame120-maincpu-text-trace/maincpu-68010.trace`)
shows the caller at `$508A` loading `D0=$47`, setting `A2=$5098`, and jumping to
`$25E8`; the routine eventually returns through `(A2)` to `$5098`, then
continues at `$6086`. This is distinct from the previously documented
 diagnostic-message helper `$2326`.

The caller is verbatim from the trace:

```text
00508A: move.w  #$47, D0
00508E: lea     ($5098,PC), A2
005092: jmp     $25E8.l
```

The executed `$25E8` setup and glyph loop are verbatim from the same trace:

```text
0025E8: clr.w   $608000.l
0025EE: andi.w  #$e7ff, $c00008.l
0025F6: lea     $10136.l, A0
0025FC: andi.l  #$ffff, D0
002602: lsl.l    #2, D0
002604: movea.l  (A0,D0.l), A0
002608: clr.w    D7
00260A: move.b   ($2,A0), D7
00260E: move.w   D7, D6
002610: lsl.w    #8, D6
002614: move.b   ($1,A0), D0
002618: mulu.w   #$a0, D0
00261C: lsl.l    #8, D0
002620: move.b   ($0,A0), D1
002624: lsl.l    #6, D1
002626: add.l     D1, D0
002628: addi.l    #-$800000, D0
00262E: move.b   ($3,A0), D3
002632: adda.l    #$4, A0
002638: move.b   (A0)+, D1
00263A: subi.b    #$2a, D1
00263E: andi.l    #$ff, D1
002644: lsl.w    #4, D1
002646: lea      $1fbc.l, A1
00264C: adda.w   D1, A1
00264E: move.w   #$7, D2
002652: move.w   #$7, D1
002656: move.l   D0, $c00002.l
00265C: move.w   (A1)+, D5
00265E: btst     #$0, D5
002662: beq      $266c
002666: move.w   D7, $c0000c.l
00266C: btst     #$2, D5
002670: beq      $267a
002674: or.w     D6, $c0000c.l
00267A: addi.l   #$10, $c00002.l
002684: lsr.w    #4, D5
002686: dbra     D1, $265e
00268A: addi.l   #$1000, D0
002690: dbra     D2, $2652
002694: subi.l   #$7fc0, D0
00269A: subq.b   #1, D3
00269C: bne      $2638
00269E: jmp      (A2)
```

For selector `$47`, the reconstructed ROM table at `$10136 + 4*$47` points to
`$6C53`. The descriptor bytes are `01 0F DF 1E`; the following 30 bytes are
`PRESS*START*TO*BYPASS*SELFTEST`. `*` is the ROM's space glyph. The descriptor
computes the first GSP host address as `$FF896040`, selects low colour `$00DF`
and high colour `$DF00`, and uses font words at
`$1FBC + 16*(character-$2A)`.

The routine writes an 8×8 glyph per character. Each font word supplies eight
four-bit nibbles; bit 0 contributes `$00DF`, bit 2 contributes `$DF00`, and the
resulting word is written through `$C0000C`. `$C00002` advances by `$10` for
the next pixel and `$1000` for the next row. This is therefore a direct
MC68010-to-TMS34010 host-port text path into GSP VRAM, not a direct MC68010
framebuffer store. The observable write sequence is implemented and tested by
`native/mc68010_text_writer.{h,c}` and
`native/mc68010_text_writer_test.c` (`mc68010-text-writer-slice-c`).

The current slice deliberately covers only the executed glyph path. The other
branches in the surrounding `$25E8` routine remain UNKNOWN until a trace
exercises their command forms.

## 13.1 Service-path GSP RAM initialization and reset release

The service-F2 trace reaches the existing GSP boot entry at `$003F8`. The
relevant MC68010 transcription is:

```text
$003F8: movem.l D2-D7/A2-A6,-(A7)
$003FC: clr.w   $60400C.l
$00402: bsr     $00370
$00406: clr.w   $60401C.l
$0040C: move.w  #$A000,$C00008.l
...
$00426: move.w  #$0001,D0
$0042A: bsr     $0037C
$0042E: move.w  #$0000,D0
$00432: bsr     $0037C
```

The `$60400C/$60401C` writes are the observed function-6 `/GSPRES` control
writes. The normal no-input run does not execute this entry; the service-F2
run executes it at frame 66, then again during later diagnostic transitions.
This attribution is KNOWN from the write trace, while the meaning of every
subroutine called by `$003F8` is not re-inferred here because the tested
`native/gsp_boot.{h,c}` slice already transcribes those routines.

The same service run reaches the already converted `$0067A` routine:

```text
$0067A: ori.w   #$1800,$C00008.l
$00682: lea     $C0000C.l,A0
$00688: move.l  #$FF800000,$C00002.l
$00692: move.w  #$0383,D2
$00696: move.w  #$00FF,D1
$0069A: move.w  D0,(A0)
$0069C: dbra    D1,$0069A
$006A0: clr.w   $608000.l
$006A6: dbra    D2,$00696
$006AA: rts
```

Its C implementation is `stunrun_gsp_boot_fill_main_vram()` in
`native/gsp_boot.c:445-466`, with observable FIFO/host-port tests. The
service trace records PC `$069A` repeatedly and 230,400-word-scale VRAM-fill
behavior, confirming this is initialization rather than polygon rendering.

The later diagnostic polling path at `$004F82-$00506A` reads GSP RAM through
the host address `$FFC00000` and `$FFC00020` after invoking `$003F8`; it is a
candidate for the next GSP-load/handshake investigation. Its semantics remain
UNKNOWN until the host-port address/data sequence is captured without the
initial VRAM-clear traffic.

### 13.1 Diagnostic GSP poll and status copy (`$004F82-$00506A`)

The current MAME-generated MC68010 listing for the rev-6 main CPU ROM gives
the following verbatim transcription:

```text
$004F82: clr.w   $608000.l
$004F88: jsr     $003F8.l
$004F8E: clr.w   $608000.l
$004F94: moveq   #$8, D1
$004F96: movea.l #$FF945E, A1
$004F9C: move.l  #$01010101, (A1)+
$004FA2: subq.l  #1, D1
$004FA4: bgt     $004F9C
$004FA6: jsr     $010932.l
$004FAC: jsr     $004A48.l
$004FB2: movea.l #$1, A1
$004FB8: move.w  #$1D4C, D0
$004FBC: clr.w   $608000.l
$004FC2: movea.w A1, A0
$004FC4: cmpa.w   #$8, A0
$004FC8: beq     $005016
$004FCC: move.l  #$0, $FF9042.l
$004FD6: move.l  $FF9042.l, D1
$004FDC: clr.w   $608000.l
$004FE2: cmp.w    D1, D0
$004FE4: bgt     $004FD6
$004FE6: move.l  #$FFC00000, $C00002.l
$004FF0: movea.w $C0000C.l, A1
$004FF6: cmpa.w   A0, A1
$004FF8: bgt     $004FB8
$004FFA: move.b  #$FF, $FF945E.l
$005002: clr.w   $608000.l
$005008: jsr     $003F8.l
$00500E: clr.w   $608000.l
$005014: rts
$005016: jsr     $004A48.l
$00501C: clr.w   $608000.l
$005022: movea.l #$FFC00020, A2
$005028: moveq   #$10, D1
$00502A: movea.l #$FF945E, A1
$005030: movea.l #$FF9048, A0
$005036: clr.w   $608000.l
$00503C: move.l  A2, $C00002.l
$005042: adda.l  #$10, A2
$005048: move.w  $C0000C.l, (A0)
$00504E: move.b  (A0)+, D0
$005050: move.b  (A0), (A1)+
$005052: move.b  D0, (A1)+
$005054: subq.l  #1, D1
$005056: bgt     $005030
$005058: clr.w   $608000.l
$00505E: jsr     $003F8.l
$005064: clr.w   $608000.l
$00506A: rts
```

**Confirmed mechanism.** The routine first calls the existing `$003F8`
GSP-reset sequence, fills eight longwords at `$FF945E` with `$01010101`,
then calls two opaque helpers at `$10932` and `$4A48`. It polls the main-CPU
word at `$FF9042` against timeout value `$1D4C`, selects GSP host address
`$FFC00000`, and reads a 16-bit value through `$C0000C`. A returned value
greater than the current counter becomes the next counter; reaching `8`
selects the status-copy branch. That branch reads sixteen words at
`$FFC00020`, advancing the host address by `$10` each time, stores each word
at `$FF9048`, and copies its low then high byte into the `$FF945E` buffer.
Both exits invoke `$003F8` again. The service trace observed the preceding
`$0067A` VRAM clear, but did not yet capture this routine's host reads, so the
relationship between the returned words and GSP readiness is **LIKELY**, not
yet KNOWN.

**Ambiguity preserved.** `$10932`, `$4A48`, `$FF9042`, `$FF9048`, and
`$FF945E` have not been assigned game-semantic names. The C slice models the
selected status-copy path through callbacks and bounds the timeout polling;
it does not claim those meanings. The fallback marker write at `$FF945E` is
also preserved literally.

**C slice.** `native/gsp_diagnostic_poll.{h,c}` and
`native/gsp_diagnostic_poll_test.c` assert the callback ordering, GSP address
sequence, eight marker writes, and sixteen-word copy. CTest name:
`gsp-diagnostic-poll-slice-c`.

Static callers found in the same rev-6 listing are `$001CA4`, `$009B80`, and
`$00AC38`. `$001CA4` is in a diagnostic branch that prints message selector
`$12` before invoking the poll; `$009B80` invokes it after selectors `$7C`,
`$74`, and `$7D`, then examines the sixteen words at `$FF945E`; `$00AC38`
invokes it after selector `$06` and a stopped diagnostic state. These callers
are **KNOWN xrefs**, but their menu/state semantics remain **UNKNOWN** until
the corresponding input/dispatch paths are traced.
## Correction: native final-PC attribution for the geometry wait loop

The earlier report that the native MC68010 stopped at `$0216` was a
misreading of the shell's startup PC line, which is printed before the run
advances. A bounded `STUNRUN_TRACE=120` after 1,200 native frames instead
showed the live CPU repeatedly executing `$022766` and returning through the
queue/display-list polling path:

```asm
$022766: movem.l D2-D6/A2-A3,-(A7)
$02276E: lea     $C0000C.l,A2
$02277E: move.l  $FF9BD0.l,D0
$022784: movea.l D0,A0
$022786: bne     $0227C0
$022788: move.w  $FF9BCC.l,D0
$02278E: beq     $022760
$022760: movem.l (A7)+,D2-D6/A2-A3
$022764: rts
```

The same trace showed callers `$02FC86/$02FC88` repeatedly invoking `$022766`
and `$02FCAC` clearing `$608000`. In this native run `$FF9BD0` and `$FF9BCC`
remain empty, so the display-list consumer has no queued source and never
reaches `$02F470`. This corrects the earlier `$0216` attribution; it does not
prove that the native model has reached the game's later geometry state.
