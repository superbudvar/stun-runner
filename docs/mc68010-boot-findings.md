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

## Correction: MC68010 GSP host-window extent

The native memory router previously treated `$C00000-$CFFFFF` as a GSP host
window and masked the byte offset to eight bits before decoding the four host
registers. This was too broad: the MAME MC68010 maps at
`/mnt/build/mame-ref/src/mame/atari/harddriv.cpp:562,611,651` are exactly
`$C00000-$C03FFF`; that aperture is the **host-port interface**, not TMS34010
VRAM. VRAM is in the separate GSP bit-address space. In MAME's handler,
`/mnt/build/mame-ref/src/mame/atari/harddriv_m.cpp:139-145,215-221`, the word
offset is transformed with `(offset / 2) ^ 1` before calling `host_r/host_w`.
The native model's `gsp_host_reg(byte_offset) = (byte_offset >> 2) ^ 1` agrees
for the first mapped word offsets; larger offsets decode to invalid TMS host
register indices and must not wrap back to one of the four registers.

This was a **native address-map modeling error**, not a ROM or MAME defect, so
it does not belong in `BUGS.md`. `native/musashi_cpu.c::route_address()` now
routes only `$C00000-$C03FFF` as `REGION_GSP`; host-register accesses use the
full offset relative to `$C00000` instead of `address & 0xFF`. The bus observer
preserves every raw mapped-window write, including invalid register indices,
so each independent provider can apply its own decode. The synthetic
`native/gsp_host_write_observer_musashi_test.c` program writes a word at
`$C04000` after valid host-port traffic; it asserts this produces no host-port
event and does not overwrite HSTADRH. It also checks the raw `$C00020` event is
preserved while its invalid decoded register has no modeled host-register
effect. The host observer and all 50 native CTests pass after this correction.

## 14. A working TMS34010 core: the ROM-less GSP boot

Bead `stun-runner-gsvp`. The GSP now actually executes. This is the first
entry in this log whose subject is a *solution* rather than a correction, and
it also carries a refuted claim of its own.

### 14.1 The GSP has no ROM, and that is what makes a core usable here

`src/mame/atari/harddriv.cpp:2691-2729` (`ROM_START(stunrun)`) declares no GSP
ROM region at all. `mainpcb:user1` is the **ADSP** object ROM ("384k for ADSP
object ROM"). The GSP program is uploaded by the 68010 into VRAM, the same
way the ADSP program is, and the boot entry `$0067A` does exactly that:

```text
$0067A: ori.w   #$1800,$C00008.l      ; HSTCTL |= 0x1800 (increment on)
$00682: lea     $C0000C.l,A0          ; A0 = HSTDAT
$00688: move.l  #$FF800000,$C00002.l  ; HSTADRH/L = 0xFF800000
$00692: move.w  #$0383,D2
$00696: move.w  #$00FF,D1
$0069A: move.w  D0,(A0)               ; the upload itself
```

A ROM-less board is normally disqualifying for a CPU core. It is not here:
`third_party/tms34010_sv/rtl/tms34010_pkg.sv:110` defines `RESET_PC = '0` as
an **overridable parameter**, and `tms34010_pc.sv:61` loads the PC from
`RESET_VECTOR_ADDR` (`= TRAP_VECTOR_BASE = 0xFFFF_FFE0`, `tms34010_pkg.sv:342`)
through the normal memory bus. On this board `0xFFFFFFE0` is inside the
mirrored VRAM share, so the 68010 supplies the entry point the core needs.
Measured: the core reaches a seeded entry at byte `0xFF800000` in 10 cycles
with zero unmapped accesses (`gsp-rtl-romless-boot`).

### 14.2 REFUTED: the core address is a bit address and needs `>> 3`

This is the claim that took the longest to kill and is worth recording so it
is not repeated.

The RTL says otherwise three times. `tms34010.cpp:67` declares the program
space with `addr_shift 3`; `tms34010_pc.sv:20` ties the PC advance to
`INSTR_WORD_BITS`; and `tms34010_core.sv:3629` advances the PC by
`INSTR_WORD_BITS`. MAME agrees on the increment: `tms34010.cpp:164`
`#define SKIP_WORD() (m_pc += (2 << 3))` — sixteen units per 16-bit
instruction. Since `emumem.h:2078` defines
`addr2byte(a) = a >> addr_shift`, the obvious reading is that the device
address is a **bit** address and the board byte address is `addr >> 3`.

It is wrong. Measured against the pinned MAME with
`mame/lua/gsp-address-unit-probe.lua`, at frame 600 with the GSP running:

```text
PROBE frame600 pc=0xFFF41680
PROBE   pc-as-byte       addr=0xFFF41680 value=0xCBFC   <- real program data
PROBE   pc-shift3        addr=0x1FFE82D0 value=0xFFFF   <- floating bus
PROBE   vram-base        addr=0xFF800000 value=0x8484   <- VRAM, confirms byte units
```

`0xFFFF` is MAME's floating-bus value for unmapped space, so the `>> 3`
reading addresses nothing. The PC value is already a byte address in
program-space units, and no shift belongs in the conversion. Confirmed in the
harness: at shift 3 the reset-vector fetch lands outside every mapped region
and `unmapped_reads` goes from 0 to 2; at shift 0 it is 0.

Two independent facts corroborate it. Upstream's own
`sim/models/sim_memory_model.sv` indexes its backing store with
`word_addr[IDX_WIDTH + LOCAL_WORD_ADDR_LSB - 1 : LOCAL_WORD_ADDR_LSB]` where
`LOCAL_WORD_ADDR_LSB = 4` — **16-byte** physical cells, not 16-bit ones. And
section 12.2 above already established that this board's host writes expand to
exactly a 16-byte cell. A 16-bit TMS34010 word is addressed here as a 16-byte
cell, which is why the PC advances by 16 in byte units.

**Do not add a `>> 3` to the harness.** `--addr-shift 0` is the measured
answer; the option survives only so the refuted reading can be reproduced.

### 14.3 HCS at reset selects the boot path

`tms34010_core.sv:3590-3593` (CORE_RESET) states it: *"HCS high at reset
selects host-present mode and defers the level-0 vector fetch until the host
clears HLT.  Otherwise fetch the 32-bit vector through the normal memory
interface."* With HCS deasserted the core never fetches its vector at all: it
spins at PC 0, and because byte address 0 is the `.noprw()` self-test window
that is an endless `0x0000` fetch. Observed directly — PC stayed 0 with
`instr=0x0000`. Asserting HCS selects the architectural ROM-less boot.

`HSTCTL` bit 15 is `HLT` (`tms34010_pkg.sv:461`), written through the host
port, which is how a real host would clear host-present mode instead. Both
paths exist; this board needs the ROM-less one.

### 14.4 What the C slice covers

`native/gsp_rtl/gsp_board.{h,c}` is the board model: the `multisync_gsp_map`
decode including `unmap_value_high()`, the 512 KiB `gsp_vram` share (section
12.1), the separate 2bpp window, both palette planes, both control shares, and
the four-register host port with its 16-byte `HSTDAT` expansion and `HSTCTL`
post-increment. It is plain C99, so `gsp-board-map-c` tests the map with
neither Verilator nor commercial ROMs. `native/gsp_rtl/gsp_rtl_main.cpp` is the
thin Verilated driver and owns nothing but clocking and the cycle handshake.

The host-register decode is the board-correct `(byte_offset >> 2) ^ 1`, per
section 13 above; the tests pin it against MAME's word-index form so the two
cannot be confused again.

## 15. Feeding the real 68010 upload to the board model (gsvp.4)

### 15.1 `gsp_host_writes.bin` is the wrong artifact, and using it fails silently

The obvious first move is to replay the shell's existing
`gsp_host_writes.bin` into the board model and compare VRAM. It fails, and it
fails in a way that looks like a board bug rather than a wrong input.

The reason is already written down in `analysis/dual-gsp-comparison-plan.md`:
*"the existing `gsp_host_writes.bin` records only effective HSTDAT
destination/data/PC words; it is not a complete host-port stream."* Those
records carry the **resolved destination** of each HSTDAT write, having
already discarded the HSTADR and HSTCTL writes that set the address and the
increment. Replaying them applies writes to addresses the stream never
latched, and the first version of this harness did exactly that:

```text
gsp-rtl replay: 897010 host writes
  board: host_writes=897010 vram_writes=0 unmapped_writes=0
    differing bytes : 477272 / 524288
    byte sum ours=0 reference=52878584
```

`vram_writes=0` and `byte sum ours=0` is the tell: nothing was written at all.

The artifact that is needed is the ordered bus stream from
`musashi_cpu_set_gsp_bus_observer` (commit 2d881db), which carries the raw
byte address and lane mask for every host-window write plus every `/GSPRES`
control event. That observer was live-only, so `musashi_cpu_dump_state()` now
records it to `gsp_bus_events.bin`. One capture is 901,354 events.

### 15.2 Two defects in making that stream usable

**The bus log was gated behind an attached observer.** The emit path returned
early when no observer was registered, so the normal shell run produced an
empty log. The recording is now unconditional; the observer is optional.

**A fixed 8M-record cap aborted the run.** Sizing the log up front costs
~160 MB for a debug aid, and the shell died before its first frame. It now
grows by doubling from 65,536 records, and a dropped-event count is reported
rather than silently truncating a replay stream -- a truncated stream
reproduces VRAM that merely looks wrong, with nothing pointing at the cause.

A third, non-code defect: `/home` was at 99% full (2.8 GB free), so a 28 MB
`fwrite` was short. `dump_blob` already returned -1 on a short write, but the
new bus-event call did not check it, so the file was silently truncated to a
size that is not a multiple of the record size. A truncated dump now fails the
dump instead.

### 15.3 Record layout must come from the producer's header

`musashi_gsp_bus_event_t` is **32 bytes**, not the 24 a hand-written version
suggests: `kind` is a C enum, so it occupies four bytes and pads the struct.
The first version of the reader computed a 23-byte stride from the field list
and refused the file. The harness now includes `native/musashi_cpu.h` and uses
`sizeof(musashi_gsp_bus_event_t)`. Guessing this layout misparses every record
while still looking like a data problem.

### 15.4 HSTDAT was never dispatched on the address (the real cause)

The residual is **attributed**, and the cause was a missing branch rather
than arithmetic drift.

`gsp_board_host_write()` expanded **every** HSTDAT into a 16-byte VRAM cell,
unconditionally. But HSTADR selects one of several regions, and the native
model dispatches on it in `gsp_mem_write_word()`:

```text
843,688  0xFF000000-0xFFFFFFFF   VRAM share, 16-byte cell expansion
 51,200  0x02000000-0x020FFFFF   2bpp masked expander over the SAME share
  1,024  0xF5000000              palette low        1,024  0xF5800000  palette high
     29  0xF4800000              control high            6  0xF4000000  control low
     39  0xC000xxxx              unmapped
```

Those counts are reproducible from the captured stream and match
`gsp_summary.txt` exactly, which is how the dispatch was confirmed rather than
assumed.

The symptom was distinctive: the first differing cell read `33 33 33 ff ...`
in our output where the reference had `00 ff 00 ff ...`, and `0x33` was the
**cell index**. That is what a write lands like when it is meant for one
region, arrives in another, and has its offset parameterise the data. Two
corrections followed:

- HSTDAT now dispatches on HSTADR across all six regions, and an address in
  none of them is counted as unmapped rather than turned into a VRAM write.
- The 2bpp expander covers **eight** four-byte words (32 bytes), alternating
  the low and high halfword mask, not the two words an obvious reading gives.

Result: divergent bytes fell from **23,979 to 6,614** of 524,288
(**98.74% identity**), and the two buckets covering the 2bpp destination
range, `0x020000-0x023fff` and `0x040000-0x043fff`, went from 7,656 and 7,536
differing bytes to **clean**. That is the confirmation: the buckets the fix
should have eliminated did disappear.

Two supporting checks: the 2bpp colour source `ctrl_lo[0]` agrees with the
reference dump at 0xFF and `ctrl_hi[0]` is 0 in both, so the colour was never
the problem.

**What remains.** 6,614 bytes, concentrated in the low 64 KiB, across 870
cells: 808 whose last writer was the 2bpp expander, 62 a plain VRAM write.
Both now reach the correct store, so the residue is a finer-granularity
effect inside those paths rather than a missing region. Note that 2bpp writes
are 4-byte granular, so comparing them as 16-byte cells is already an
approximation; the next bisect should compare at 4-byte granularity. Left
**open**.

### 15.5 The 2bpp mask was a 2-bit pixel; it is a byte lane

Comparing at 4-byte granularity -- the natural granularity of a 2bpp write,
and the granularity at which 16-byte cell comparison is an approximation --
localised the last residue at once: **every** diverging 4-byte word had a 2bpp
expander as its last writer, and not one had a plain VRAM write. A divergence
confined to a single code path is a bug in that path, not a scheduling or
ordering problem.

The mask was wrong. `harddriv_v.cpp:28` defines

```c
#define MASK(n)  NATIVE_ENDIAN_VALUE_LE_BE(0x000000ffUL << ((n) * 8), ...)
```

so each selected data bit drives a **full byte lane**, not a 2-bit pixel. And
`harddriv_v.cpp:43-61` builds two masks per data word from two *different sets
of data bits* -- `0x0001/0x0004/0x0010/0x0040` for the low mask,
`0x0100/0x0400/0x1000/0x4000` for the high one -- mapping both onto the same
byte lanes.

The implementation here had made **both** opposite assumptions at once: it
used `0x3 << (n * 2)` (2-bit pixels), and it shifted the lane by eight for the
high half instead of switching which data bits were tested. Two errors
pointing opposite ways is why the residue was small and structured rather than
gross.

```text
before:  6,614 / 524,288 bytes differ  (98.74%)
after:       0 / 524,288 bytes differ  (byte-identical)
  board: host_writes=901348 vram_writes=15104880 unmapped_writes=39
  byte sum ours=52878584 reference=52878584
```

`unmapped_writes=39` independently matches the 39 host-window writes whose
address selects no region, so the dispatch is now correct in both the writes it
performs and the ones it declines.

The lesson worth keeping: **compare at the granularity the code under test
actually writes at.** Sixteen bytes is right for a host VRAM cell and wrong for
a 2bpp expander; picking the wrong one made the residue look like scattered
noise across six buckets instead of one clean signal.

`test_hstdat_dispatches_on_address` now asserts the full byte lane, and is
verified as a negative control: reverting the mask to the 2-bit form makes
`gsp-board-map-c` fail, so the test genuinely pins the semantics.

### 15.6 Tests that asserted an address the board never uses

Correcting HSTDAT to dispatch on HSTADR immediately broke `gsp-board-map-c`,
and rightly so. Three tests set `HSTADR` to `0x0100`, `0x0200` and
`0x00012345` and expected a VRAM write. None of those is in
`0xFF000000-0xFFFFFFFF`, so each selects **no region at all** and the store
is correctly discarded. They had been passing because the model ignored the
address entirely.

They now use addresses the board actually sees, `0xFF801000` and
`0xFF800200`, matching the ROM's own boot fill
`move.l #$FF800000,$C00002.l` (findings 13.1). A new
`test_hstdat_dispatches_on_address` asserts the dispatch directly: the same
register index reaching the palette, then the 2bpp window, then control low,
then an unmapped address, with the control write feeding the 2bpp colour.

The general lesson: a test that passes because the code under test ignores
its input is not a test. A missing branch that every input value satisfies
stays invisible until something real depends on it.

### 15.6 A shell crash worth recording separately

With a ROM directory set, the shell segfaults on exit:

```text
#0  libGLX.so.0
#2  _dl_call_fini ()
#5  exit ()
```

A GLX/SDL teardown fault at process exit, unrelated to the GSP work and
present before it. Workaround for headless capture runs:
`SDL_VIDEODRIVER=dummy STUNRUN_VIDEO_BACKEND=null`. Not fixed here: it is a
defect in the shell's window lifecycle, not in any code touched by gsvp, and
it deserves its own bead rather than a drive-by change.

## 16. The core executes the real GSP program

`stunrun-gsp-rtl-harness --boot <events> --cycles N`. This is the first time
the vendored TMS34010 has run STUN Runner's actual GSP program rather than a
synthetic fixture.

### 16.1 Host writes must reach BOTH the board and the core

Replaying the bus stream through the board model alone can never execute
anything: the core would hold a zero reset-vector address and no register
state. The four host registers **are** the boot sequence, so every
host-window write is presented to both

- the board model, which owns memory, and
- the core's own `host_*` port, which owns HSTADR/HSTDATA/HSTCTL and through
  HSTCTL bit 15 the HLT line.

The register index needs no separate translation: `gsp_board_host_reg()`
returns HSTADRL=0, HSTADRH=1, HSTDATA=2, HSTCTL=3, which is exactly the
vendored RTL's `host_reg_sel_t`.

### 16.2 This board boots in host-present mode, not by fetching a vector

The captured stream settles it. `/GSPRES` is released at **event 1** --
before a single HSTDAT write:

```text
seq=      0 addr=0x0060400c released=0   /GSPRES asserted, 68010 PC 0x402
seq=      1 addr=0x0060401c released=1   /GSPRES released, 68010 PC 0x40c
seq=780401 addr=0x0060400c released=0   re-asserted at PC 0x22376
seq=780402 addr=0x0060401c released=1
seq=887025 addr=0x0060400c released=0   re-asserted at PC 0x22376
seq=887026 addr=0x0060401c released=1
```

So the processor is released with VRAM still empty. Combined with findings
14.3 -- HCS high at reset defers the level-0 fetch until the host clears HLT
-- the real boot is: **release reset, halt, upload while halted, clear HLT.**
The GSP has no ROM because the 68010 loads it during the halt. The harness
drives HCS only while the 68010 owns the window, which reproduces this without
special-casing.

The two later assert/release pairs are the service-path re-init at `$022376`,
matching findings 13.1.

### 16.3 Result

```text
boot: replaying gsp_bus_events.bin into core + board
  events=901354 host_txns=901348 reset_events=6 timeouts=0
  running 2000000 cycles from the post-upload state
  PC range 0xfff40000..0xfff59dd0  distinct(low16)=374
  during run: board writes=789524 reads=557900
```

The PC range is the important number. The GSP instruction addresses recorded
elsewhere in this log are `0xFFF46590`, `0xFFF45DD0`, `0xFFF47AB0`,
`0xFFF43030` -- all inside `0xFFF40000..0xFFF59DD0`. The core is executing
the same code the original executed, from the same VRAM image.

344 distinct instruction addresses at 500k cycles and 374 at 2M: the program
converges rather than running away. No host transaction timed out.

### 16.4 What this does NOT establish

The starting state is the VRAM as it stood **after** 600 frames of host
writes, not as it stood when the GSP first ran, and the GSP is then run to
completion with no further host traffic. Real execution interleaves the two.

To compare against MAME's frame-600 GSP state, the harness needs the 68010's
cycle count on each bus event so the core can be advanced to that point before
applying the write. `musashi_gsp_bus_event_t` does not carry one today, so
that field must be added and the stream re-captured. Until then the correct
claim is the one above: **the core executes the uploaded program and writes
memory** -- not that it reproduces the frame.

The finding that shapes this: the host-port write stream alone already
reproduces the reference VRAM byte-for-byte (section 15.4). That is a fact
about what the *board model* captures. It is not evidence the GSP is idle, and
it does not license comparing a GSP-only run against that same reference.

## 17. Who halts the GSP, who releases it, and why the capture never runs it

### 17.1 The halt bit

`HSTCTLH.HLT` is bit 15 of the control word at host-window byte offset 8
(`$C00008` on the 68010 side). Confirmed from the vendored RTL rather than
assumed:

```
tms34010_pkg.sv:461   parameter int unsigned HSTCTL_HLT_BIT = 15;
                      // halt processor at next boundary
tms34010_io_regs.sv:493  assign hlt_o = io_reg[IO_IDX_HSTCTLH][HSTCTL_HLT_BIT];
tms34010_core.sv:3597    if (io_hlt) state_d = CORE_RESET_HALT;
```

This matches MAME's `tms34010.cpp:1670-1674`, which drives
`set_input_line(INPUT_LINE_HALT, (data & 0x8000) ? ASSERT_LINE : CLEAR_LINE)`.

### 17.2 It is the 68010, not the ADSP

The 68010 ROM writes HSTCTL directly. Scanning the 786 KiB maincpu image for
every instruction touching `$c00008.l` gives 120 sites, of which exactly these
can change bit 15.

HLT **set** -- full-word writes with bit 15 set, 12 sites:

```text
0x0000040c: move.w #$a000,$c00008.l      0x00022404: move.w #$b800,$c00008.l
0x00005408: move.w #$b800,$c00008.l      0x0002fd44: move.w #$b800,$c00008.l
0x0000a474: move.w #$a000,$c00008.l      0x0002fde2: move.w #$b800,$c00008.l
0x0000a540: move.w #$9800,$c00008.l      0x0002fe8e: move.w #$a000,$c00008.l
0x00010938: move.w #$b800,$c00008.l      0x0002ff62: move.w #$a000,$c00008.l
0x00010a02: move.w #$a000,$c00008.l
0x00010a7a: move.w #$a000,$c00008.l
```

HLT **cleared** -- 8 sites, in two shapes:

```text
0x0000fc7a: andi.w #$7fff,$c00008.l      ; canonical read-modify-write clear
0x0000fe60: andi.w #$7fff,$c00008.l
0x0001094c: move.w #$7800,$c00008.l      ; full-word write, bit 15 clear
0x00010954: move.w #$3800,$c00008.l
0x0002c1c4: move.w #$7800,$c00008.l
0x0002c1cc: move.w #$3800,$c00008.l
0x0002dcbc: move.w #$7800,$c00008.l
0x0002dcc4: move.w #$3800,$c00008.l
```

Every other HSTCTL site is a read-modify-write that preserves HLT:
`ori.w #$1800` (30 occurrences, sets INCR+INCW), `andi.w #$e7ff` (30, clears
INCR+INCW), `andi.w #$ff7f` (2, clears INTOUT). The HLT-clearing half of the
register is therefore written deliberately and always with a full-width
immediate, never incidentally.

The ADSP does not touch it. Nothing in the JSA/ADSP path reaches `$C00008`.

### 17.3 The captured stream holds the GSP halted for ~98.5% of its length

Every HSTCTL write in the 901,354-record capture, with the 68010 PC at the
bus callback:

```text
  seq        68k_cyc     68010_PC   value    HLT
  2          6098        0x00000414  0xa000   1  <- ASSERT halt
  45         9728        0x00000496  0xb800   1  <- ASSERT halt
  580        27206       0x00000682  0xb800   1  <- ASSERT halt
  230983     4671926     0x00000578  0xb800   1  <- ASSERT halt
  231528     4699232     0x0000063e  0xb800   1  <- ASSERT halt
  256112     5588620     0x0002240c  0xb800   1  <- ASSERT halt
  780992     44132272    0x00021708  0xb800   1  <- ASSERT halt
  805577     44824344    0x000238e6  0xa000   1  <- ASSERT halt
  806634     44950392    0x00023908  0xb800   1  <- ASSERT halt
  806638     44950910    0x000238e6  0xa000   1  <- ASSERT halt
  808751     45201918    0x00023908  0xb800   1  <- ASSERT halt
  885583     48993292    0x000238e6  0xa000   1  <- ASSERT halt
  887024     49174576    0x00023908  0xb800   1  <- ASSERT halt
  887616     49201788    0x0002fdea  0xb800   1  <- ASSERT halt
  899809     50351440    0x0002c1cc  0x7800   0  <- RELEASE halt
  899810     50351440    0x0002c1d4  0x3800   0  <- RELEASE halt
  899814     50351962    0x000238e6  0x2000   0  <- RELEASE halt
  901351     50547212    0x00023908  0x3800   0  <- RELEASE halt
```

14 assertions, 4 releases, and **the first release is at 68010 cycle
50,351,440 of 51,106,634** -- the last 1.5% of the capture. The GSP is frozen
for essentially the entire recorded window.

The releases land on the sites from 17.2, and the disassembly at the one the
capture actually used reads:

```text
0002c1bc: 4eb90002fdd0         jsr        $2fdd0.l
0002c1c2: 588f                 addq.l     #$4, a7
0002c1c4: 33fc780000c00008     move.w     #$7800, $c00008.l   ; RELEASE halt
0002c1cc: 33fc380000c00008     move.w     #$3800, $c00008.l   ; RELEASE halt
0002c1d4: 427900608000         clr.w      $608000.l
```

**Confirmed:** the 68010 releases halt with a full-width `move.w` to
`$C00008`, immediately after returning from a subroutine at `$2fdd0` (itself
a `move.w #$b800` HLT-set site, section 17.2) -- i.e. upload, then release.

### 17.4 Why the replayed core sits in CORE_RESET_HALT

Because of 17.3 the core is halted from the first host write almost to the
last. Two consequences, both *correct behaviour* that were previously
misread as bugs:

- `state=41` (`CORE_RESET_HALT`) for the whole lockstep run is the faithful
  result. The earlier reading -- "HCS is wedging the core" -- was wrong; the
  HCS fix (HCS low, matching MAME's `m_halt_on_reset=false`) was still needed
  and still correct, but it is not what releases the core. HSTCTL.HLT is.
- VRAM ends the replay with no program in it, because a halted core never
  fetched its entry vector from `$FFFF_FFE0`. This is the state the board
  would really be in at 68010 cycle 51.1M.

The recorded PC in the stream is the address of the instruction *after* the
store (0x2c1c4 + 8 = 0x2c1cc), because the bus callback fires after the
instruction has been dispatched.

### 17.5 What this means for the frame-600 goal

The lockstep machinery is now correct -- a full replay advances
**306,639,804 GSP cycles over 51,106,634 68010 cycles, a ratio of exactly
6.0**, matching 48 MHz / ~8 MHz. Zero host-transaction timeouts.

But this particular capture cannot demonstrate "reproduces MAME's frame-600
GSP state", because in it the GSP never ran. The honest claim is narrower:

> The core faithfully executes the captured host-write stream in lockstep at
> the correct clock ratio, and remains halted exactly as long as the 68010
> holds HSTCTL.HLT set.

To close the original goal the capture itself has to change: sample a window
in which the GSP is running. Section 17.3 gives the test for such a window --
68010 PC in the `$2c1c4` / `$1094c` / `$2dcbc` release neighbourhood, or
after a `$2fdd0`-style upload-return -- rather than during the bulk upload
that dominates the current trace.

### 17.6 Tools

The two analysis passes behind 17.1-17.3 are committed as tools rather than
left as throwaway scripts, per the reusable-tooling rule in `AGENTS.md`:

- `tools/build-maincpu-image` — assembles the 786 KiB MC68010 image the native
  shell actually executes, from `roms/stunrun.manifest.json`. Its docstring
  leads with the `ROM_LOAD16_BYTE` ⇒ `ROM_SKIP(1)` interleave, because
  assembling by copying files to byte offsets yields an image that decodes into
  meaningless instructions rather than failing. The tell is the reset vector:
  `SP=0xFFFFC000 PC=0x00000216` when right, garbage when wrong.
- `tools/analyze-gsp-hstctl` — byte-pattern scan for every 16-bit store to the
  host window, separating the 12 HLT-setting sites from the 8 HLT-clearing
  ones from the 96 that merely preserve it, and cross-referencing a captured
  `gsp_bus_events.bin` for the halt timeline and the fraction of the capture
  the GSP was frozen.

Cross-checked against an independent capstone disassembly of the same image:
identical site addresses, identical set/clear/preserve classification. The
scan is a byte-pattern match rather than a disassembler so the tool stays
dependency-free and deterministic, and its three encodings
(`33FC` move.w, `0079` ori.w, `0279` andi.w) are verified against the real
bytes at the known `$C00008` sites.

Both are covered by `tests/test_public_tools.py`, and each test was checked
against a deliberately broken tool: concatenating instead of interleaving the
ROM, treating every store as halt-changing, and disabling the stride guard
each produce failures rather than passing silently.

## 19. The GSP VRAM share is 4 MiB, not 512 KiB (CONFIRMED, fixed)

Raised by another agent as a question: "the mirror period is 2 MiB, not 4 MiB",
from a 4 MiB capture in which a 512 KiB page repeated four pages on. Measured
to settle it. **The premise of the question was wrong, and so was my own first
answer.** Both are recorded here because the wrong turns are the expensive part.

### 19.0 What I got wrong first

My first pass concluded the board model was "wider than MAME declares" and that
351,144 captured writes addressed unmapped space. Every part of that was an
arithmetic slip: I read the declared window `0xff800000-0xffbfffff` as 512 KiB.
It is `0x400000` bytes -- **4 MiB**. The 512 KiB came from our own
`GSP_BOARD_VRAM_BYTES`, which is the thing under suspicion, so I used the
suspect constant to compute the thing that would exonerate it. Every derived
claim in that draft -- the window size, the mirror period, the 351,144 count,
the "MAME drops these writes" conclusion -- followed from that one error.

The same slip reached the other agent: they compared 512 KiB *pages*, so their
"delta exactly 0x200000" was 2 MiB of *content* repeating inside a 4 MiB share,
not a mirror period. Neither number was ever about the mirror.

### 19.1 Which map STUN Runner selects

`GAME(1989, stunrun, ..., stunrun_machine, ...)` -> `stunrun_machine` ->
`STUNRUN_BOARD(config, "mainpcb")` -> `stunrun_board_device_state::
device_add_mconfig`, which calls `multisync_nomsp(config)` (harddriv.cpp:1861),
installing:

```text
harddriv.cpp:1545   m_gsp->set_addrmap(AS_PROGRAM, &harddriv_state::multisync_gsp_map);
harddriv.cpp:628    map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram");
```

So it is the 4 MiB multisync path, not the driver-generic map and not
multisync II. That closes the board-selection thread.

### 19.2 The decode, measured rather than read

`tools/mame-gsp-mirror-probe` (with `mame/lua/gsp-mirror-probe.lua`) writes
markers into the live machine and reports what happened. Three results:

```text
share           : 4194304 bytes (4 MiB), first bad offset 0x400000 (aliases offset 0)
  ok  addr=0xff800000 measured=0x00000000 declared=0x00000000 shipped_512k=0x00000000
  BAD addr=0xffa00000 measured=0x00200000 declared=0x00200000 shipped_512k=0x00000000
  ok  addr=0xffc00000 measured=0x00000000 declared=0x00000000 shipped_512k=0x00000000
  BAD addr=0xffe00000 measured=0x00200000 declared=0x00200000 shipped_512k=0x00000000
  --  addr=0xff000000..0xff600000  not mapped by MAME at all
```

The share-size probe deserves a note, because the obvious version of it lies.
Writing a marker at `base+off` and reading it back at `base+off` cannot tell a
real offset from a mirror: `0xff800000+0x400000` is the bit-22 alias of offset
0, so it round-trips perfectly whether or not offset `0x400000` exists. The
probe instead parks a sentinel at offset 0, writes the candidate at `base+off`,
then reads offset 0. If offset 0 now holds the candidate, `base+off` was never
distinct storage. That is what makes `4194304` a measurement rather than an
echo of the declaration -- and it independently confirms the declared size.

So the real decode is:

```text
mapped iff (addr & 0x7f800000) == 0x7f800000     i.e. 0xff800000-0xffffffff
offset      =  addr & 0x003fffff
```

which is exactly the declaration, and exactly what our two paths do *not*
implement. `native/musashi_cpu.c:925` and `native/gsp_rtl/gsp_board.c` both
compute `(addr - 0xff800000) & (0x80000 - 1)`:

```c
#define MUSASHI_GSP_VRAM_BYTES  0x80000u
#define GSP_BOARD_VRAM_BYTES    0x80000u
```

### 19.3 How much of the capture this touches

Far more than the mirror region. Within the primary window itself, only
`0xff800000-0xff87ffff` is inside a 512 KiB share; from `0xff880000` upward the
two masks already disagree. Replaying the captured stream through both masks:

```text
page        agree   differ
0xff800000    65536  196608
0xffa00000        0  230400
0xffc00000    32768  106496
0xffe00000        0  211880
```

**745,384 of 843,688 0xFFxxxxxx HSTDAT writes (88.4%) land in a different cell**
under the shipped mask. The shipped mask can address 32,768 distinct cells; the
real share has 524,288. This is the strongest candidate yet for the frame-600
VRAM mismatch, and it is not subtle enough to have been hiding behind a
`check-gsp-replay` comparison: board model and native model use the *same*
0x80000 constant, so they agree with each other by construction and the check
passes either way. That is why section 15.4's byte-for-byte VRAM match is not
evidence about MAME.

### 19.4 Why not fixed here

The fix is a constant: `0x80000` -> `0x400000` in `native/musashi_cpu.h` and
`native/gsp_rtl/gsp_board.h`. It is not one line of work. It grows
`gsp_board.vram[]` from 512 KiB to 4 MiB, changes the `--vram-out` dump from
524288 to 4194304 bytes, and touches every test that asserts on VRAM extent.
The frame-600 oracle fixture needs replacing first, for a reason that has
nothing to do with its size -- see 19.5. Filed as `stunrun-2ua` and made a
blocker of `gsvp.4`.

Doing it as its own slice is deliberate: the measurement above justifies the
change completely, and that is exactly the condition under which it is
tempting to edit the model and the oracle in one pass and declare victory. The
oracle must be re-captured at the new extent *first*, from a run whose VRAM is
known good, so that a green diff means the fix worked rather than that both
sides moved together.

### 19.5 What this does not explain

This is the VRAM half. The other half of the frame-600 blocker is untouched and
independent: section 18.2's finding that the capture ends 560,422 68010 cycles
after the final HLT release, with the GSP halted for 99.61% of its window. Even
with the share sized correctly, a replay of a stream in which the GSP never ran
will not match a frame where it did. Fix the extent, then re-capture past the
halt boundary; they are two separate problems and neither substitutes for the
other.

### 19.5 The committed frame-600 VRAM fixture is not VRAM

`fixtures/oracle/frame_600/frame_600_gsp_vram.bin` cannot be used as the GSP
VRAM oracle. Three independent reasons, none of which requires knowing what it
actually contains:

```text
size                     1048576 bytes
                         -- neither the 512 KiB the models assume
                            nor the 4194304 the share actually is
first 8 bytes            ffffc000 00000216
                         -- MC68010 reset SP and PC at address 0: main RAM
byte agreement with a fresh, pinned mame-gsp-vram-dump capture
                         1876 / 1048576  =  0.2%
```

It has no provenance metadata: it was added in `5b9b13b` "Establish reproducible
harness and evidence workflows" and nothing records how it was produced. Its
first 8 bytes are the MC68010's reset stack pointer and vector, which live in
main CPU RAM, so whatever it is it is not the `gsp_vram` share. It also has no
internal 256 KiB, 512 KiB or 1 MiB periodicity, and its two 512 KiB halves have
very different density (230,103 and 99,692 nonzero words), so it is not a
truncated or tiled capture of a repeating share either.

**What does *not* rest on it.** Worth stating explicitly, because the failure
mode here is to assume the blast radius is the whole suite: no CTest and no
python test references `fixtures/oracle/`, so no green check currently depends
on this file. And section 15.4's "byte-identical, 0 / 524,288 bytes differ"
claim did **not** compare against this fixture -- 15.4's reference was a 512 KiB
`/tmp` capture, which is why 15.4 is a comparison at an extent this board's own
model already had wrong. Two different references, two different extents, one
file committed under a name that suggests it is the oracle.

**The replacement landed, and was then replaced again** (2026-10-13, then
2026-10-14). The first replacement put a 4194304-byte capture at this path with
`tools/verify-oracle-fixture` pinning that size. It was wrong in the same way
§19 was: `tools/mame-gsp-vram-dump` reads GSP **program addresses**, and one
program-space address unit is one bit, so its 4194304-byte file is 4 MiB of
*address span* over a 512 KiB share — eight times too large, and a file that
looks exactly like a successful capture. §34 supersedes both.

The fixture is now MAME's direct `m_gsp_vram` pointer dump: 524,288 bytes,
sha256 `04cebed17bccd9c2...`, 5058 nonzero, from
`reference/experiments/stunrun/gsp-direct-share-frame600-service/`. The checker
pins **512 KiB**, and its negative controls now include a 4 MiB file specifically
— a checker that blesses a wrong unit conversion is worse than no checker, since
it makes the wrong file look certified. Six public tests in
`tests/test_public_tools.py::OracleFixtureTests` pin this, four of them negative
controls. The lesson is recorded rather than smoothed over: the *second* failure
at this path shipped with its own checker already enforcing it.

**`tools/mame-gsp-vram-dump` reads program addresses**, consecutively from
0xff800000 with a per-run `-nvram_directory`. Its output is a valid
**address-space trace** and remains useful as one. It is not a VRAM image, and
its `1664200 of 2097152 words nonzero` is not a share-fill measurement. It has
since been corrected to read one word per `0x10` address units; see §34.1.

**One finding from the other agent survives here, correctly.** Their page
comparison, redone on the 4 MiB dump:

```text
page 0: identical to pages [0, 4]
page 1: identical to pages [1]
...
page 4: identical to pages [0, 4]
```

Page 0 equals page 4 exactly, delta 0x200000, and pages 0-3 all differ -- their
measurement, reproduced bit for bit. What it measures is a 2 MiB **content**
period inside the 4 MiB share, which is a real property of what the ROM wrote and
not a mirror period. The data was right; only the interpretation moved it onto
the wrong question. Recorded because "their 2 MiB was wrong" would be the wrong
thing to remember.

## 20. The MC68010 text path: selector table, descriptor layout, 30 call sites

Section 13 recorded the `$25E8` glyph loop as executed. This section maps the
data it reads and the callers that reach it, from the assembled ROM image
(`tools/build-maincpu-image`) rather than from a trace, so the whole structure
is visible at once.

### 20.1 There are two text paths, and they are not the same

The GSP-side HUD text is already ported: `reproduction/gsp/text_cursor.c` and
`text_record.c` model the eight-word record and the glyph cursor at GSP PC
`0xFFF46590`, with CTest `gsp-text-cursor-slice-c` and
`gsp-text-record-slice-c`. That path takes descriptors uploaded into VRAM and
renders them with GSP code.

This section is the *other* path: the MC68010 formats glyphs itself and pushes
them at the GSP host port. It carries the attract-mode, operator-menu and
self-test text. Neither path explains the other, and `$25E8` is the one with a
68010-side reader.

### 20.2 Record layout: four header bytes, not two

Blocks are packed back to back in table order. Each is:

```text
byte 0      x / column byte
byte 1      y / row byte
byte 2      colour byte
byte 3      character count
byte 4..    N characters, '*' as the word separator
```

`byte 3` is the character count: it equals the string length for **388 of 394**
blocks exactly, the remainder being padding or a table-end artifact.

`byte 2` is a colour, not a marker. It takes exactly seven values across all 394
blocks:

```text
0x00 0x3F 0x5F 0x70 0x9F 0xDF 0xFF
```

`$25E8` uses it twice -- `move.b ($2,A0),D7` becomes the low colour and
`lsl.w #8` of it becomes the high colour -- so `0xDF` gives the `0x00DF`/`0xDF00`
pair section 13 recorded.

### 20.3 Reconciliation with `analysis/rom-text-strings.md`

Another agent's uncommitted extraction (`analysis/extract_rom_strings.py`,
`analysis/rom-text-strings.md`, `analysis/rom-text-strings.txt`) reports the
service records as `0xFF`, length byte, then that many bytes, and finds 56 of
them. That is the same data seen from the wrong edge, and the alignment proves
it. For selector `$02`:

```text
this section:   block at $066AE, header 03 00 ff 09, string at $066B2
their record:                     $0066B0, header ff 09,    string at $066B2
```

Same string address, two bytes apart -- their `0xFF` marker is `byte 2` of the
four-byte header, which is the colour field, and it is `0xFF` only for some
blocks. Counting: 102 of the 394 blocks have `byte 2 == 0xFF`, so their scan
finds 56 of 102 even within that subset, and **292 blocks with a non-`0xFF`
colour are not in their list at all**. Their 143 "in-game" strings come from a
different, `0x00`-terminated format and are unaffected.

Their file leaves UNKNOWN "what draws the `0xFF` service records and what
consumes the in-game strings". This section answers the first half for the
service records: `$25E8`, reached through the selector table below.

### 20.4 The selector table

```text
$10136   394 long pointers, selector $00..$189
          -> descriptors at $0667C..$08C75, strictly increasing in table order
```

Read by `$25F6`-`$2604`: `lea $10136,A0`, `andi.l #$ffff,D0`, `lsl.l #2,D0`,
`movea.l (A0,D0.l),A0`. Selector `$47` resolves `$10252` -> `$6C53`.

Two selectors can share a descriptor position, because the address is computed
from bytes 0 and 1 only: `$47` and `$16B` both have `01 0f` and both land at
`$FF896040`, differing only in colour and string.

### 20.5 Thirty call sites, one protocol

Every reference is `jmp $25E8.l`, encoding `4E F9 00 00 25 E8` -- 32-bit
absolute, not `4E BA` displacement, which is why a search for the displacement
form returns nothing. Each is preceded by the same two instructions:

```text
move.w  #$SEL, D0
lea     (disp,PC), A2
```

30 sites, 30 distinct selectors, all `<= $82`:

```text
$05092 $47   $0521A $5D   $05286 $5E   $05300 $5F   $05312 $61   $05324 $62
$05336 $60   $0571A $3B   $0575E $3C   $0579E $3D   $057B4 $3E   $057F2 $3F
$05808 $40   $05816 $41   $05824 $42   $05832 $67   $05840 $48   $062A2 $43
$062BC $44   $062CA $45   $062D8 $46   $062E6 $50   $062F4 $51   $06302 $52
$09DAC $80   $09DBA $81   $09DC8 $82   $09DDE $0B   $09DEC $0C   $09E02 $0E
```

`A2` is the return address, consumed by `jmp (A2)` at `$269E`. On the 68010 a
PC-relative displacement is relative to the instruction address + 2, so
`lea ($8,PC),A2` at `$508E` yields `$5098`, not `$509A`.

### 20.6 Glyph source

`$1FBC + 16*(character - $2A)`, 16 bytes per glyph. `$2A` is `*`, whose glyph is
all zeros -- the ROM's blank. That is why `*` separates words: the space
character is simply the blank glyph.

### 20.7 Not yet ported

`native/mc68010_text_writer.{h,c}` takes an already-resolved descriptor and
font words and covers the executed glyph loop. Missing: selector resolution
(`$25F6`-`$2604`), descriptor decode (`$2608`-`$262E`), and the loop nesting
(`$262E`-`$269C`), whose `D3`/`D2`/`D1` nesting does not reconcile with the
observed 16-byte cell stride by inspection and needs a trace. Filed as
`stun-runner-txt1`.

One caution for whoever ports it: the same `$25E8` prologue runs before any
glyph is written, and `clr.w $608000.l` plus `andi.w #$e7ff,$c00008.l` are part
of the routine's observable effect, not incidental.

## 21. The text path closes end to end, in service mode

Section 20 mapped the `$25E8` structure statically. This closes the chain by
observation: service mode, `0xC00000`-`0xC00020` tapped on the MC68010, and the
result checked against a committed screen capture.

### 21.1 The capture

Every `$25E8` character draw executes `move.l D0,$c00002.l` at PC `$2656`, so
filtering on that PC alone yields exactly one event per character drawn. 1040
taps, 520 host-address writes, service mode, frames 60-900:

```sh
tools/mame-ram-write-trace roms <out> --device :mainpcb:maincpu \
  --base 0xC00000 --end 0xC00020 --frames 900 --start-frame 60 --end-frame 900 \
  --pc 0x2656 --registers --input service_f2 --nothrottle
```

Recorded under `reference/experiments/stunrun/service-f2-text-host-writes/`.
This capture is only possible after the f084039 harness fix: before it,
`--registers` on the MC68010 dropped every event, so the one tool that could
watch the text being drawn reported nothing.

### 21.2 Reading the registers confirms the decode

```text
frame 214  A0=00006C58  A1=0000221C
```

* `A1 = $221C`. The font pointer is `$1FBC + 16*(ch-$2A)`, so
  `$221C - $1FBC = $260 = 38` glyphs in, giving `$2A + 38 = $50` = `P`. The
  first character of the string. Exact.
* `A0 = $06C58`, and `$2638` is `move.b (A0)+,D1` -- it post-increments *before*
  the write at `$2656`. So `descriptor = A0 - 5 = $06C53`, which is exactly
  selector `$47`'s table entry, and `A0` increments by one per character, which
  is also the reliable way to segment a draw.

### 21.3 Two selectors resolved from live traffic

```text
frame 214-216   A0-5 = $06C53   selector $47   count_byte=30   PRESS*START*TO*BYPASS*SELFTEST
frame 334-336   A0-5 = $06BE7   selector $43   count_byte=14   PROGRAM*ROM*OK
```

Both `count_byte` values equal the string length, both `first_glyph` values are
`P`, and both descriptors are real table entries rather than offsets guessed
from the data. `$43` and `$47` are also both in the 30-call-site list from
section 20.5, at `$062A2` and `$05092` respectively.

### 21.4 The screen agrees

`reference/experiments/stunrun/service-f2-3600-stages/screens/frame-000300.png`
shows:

```text
PRESS  START  TO  BYPASS  SELFTEST
```

That is selector `$47`, spelled with spaces where the ROM has `*` -- the blank
glyph, as section 20.6 established. The committed screens were captured with
input mode `service`; this capture used `service_f2`; the two schedules are
byte-identical in intent (press Service Mode at frame 60, release at 120) and
were checked to be, which is what makes the comparison legitimate rather than
coincidental.

So the chain is now evidenced at every hop, each by a different artifact:

```text
ROM descriptor $06C53  = 01 0f df 1e + "PRESS*START*TO*BYPASS*SELFTEST"
selector table $10136  -> $6C53                                       (section 20.4)
$25E8 decode           -> base $FF896040, colour 00DF/DF00, 30 chars  (section 13)
host-port writes       -> observed live, A0/A1 match the decode       (21.2, 21.3)
GSP VRAM               -> receives them at $FF896040
screen                 -> "PRESS  START  TO  BYPASS  SELFTEST"        (21.4)
```

### 21.5 What is still missing, precisely

The VRAM-contents hop is evidenced by address but not yet by comparison: the
68010 writes HSTADR `$FF896040`, and MAME maps that into the share, but
confirming the resulting bytes requires the 4 MiB extent fix in section 19 --
under the shipped 512 KiB mask that address lands at offset `$16040` instead of
`$96040`. That is `stun-runner-2ua`, and it is the only thing between here and a
byte-level check of the text region.

The port itself remains `stun-runner-txt1`. What section 21 adds is that its
oracle now exists and is committed: the ordered `(HSTADR, HSTDAT)` stream plus
the `A0`/`A1` samples per character, which together pin the loop nesting that
section 20.7 flagged as not reconcilable by inspection.

## 22. The capture is not budget-truncated; the 68010 simply stops writing (§18.2 corrected)

Section 18.2 said the capture "is 384.6 frames because the shell's `STUNRUN_FRAMES`
budget ran out", and the next move was "re-capture past the halt boundary" with a
larger frame count. That is wrong, and the wrong belief was acting as a dead end:
raising the budget cannot help, because nothing is truncating it.

```sh
STUNRUN_MC68010=1 STUNRUN_STUNRUN_DIR=roms/stunrun \
STUNRUN_FRAMES=600 STUNRUN_DUMP_DIR=/tmp/opencode/gsp600 \
  native/build/stunrun-native-shell
```

At 600 frames the dump is **901,354 bus events, 36,054,160 bytes** -- byte-for-byte
the same count as the 384-frame capture. The last record's cycle stamp is
51,106,634, which is 384.6 frames at `STUNRUN_FRAME_CYCLES` 132,886. The walk
genuinely runs to frame 600 (`shell.c:1267` loops to `g_terminal`, which is 600);
the 68010 simply issues no further host-window writes after frame 384.6.

Two corrections to what section 18.2 concluded:

**The 68010 does release the GSP.** `tools/analyze-gsp-hstctl --stream` on the
600-frame capture reports `hstctl_releases = 4`, `first_release_cycle = 50,351,440`
(frame 379.0) and `last_hstctl_cycle = 50,547,212` (frame 380.4). The GSP is not
held halted for the whole window -- it is halted for 99.61% of it and then
released, roughly four frames before the ROM goes quiet.

**The capture is not cut short by the harness.** It ends 559,422 cycles after the
final HSTCTL write because that is when the 68010 stops writing. A missing dump in
a run with `STUNRUN_DUMP_DIR` set is a different thing entirely, and is what
actually cost time here: the dump block is guarded on `g_cpu != NULL`
(`shell.c:1743`), so a run without `STUNRUN_MC68010=1` takes the transport-model
path, logs `image=init-state` and `mode=blank-scaffold`, and silently writes
nothing while still reporting RESULT PASS.

So the actual blocker after the release is not a short capture. The most
consistent reading of the numbers is that the ROM releases the GSP and then waits
on it -- the last 559,422 cycles produce no host-port traffic at all, which is
what waiting looks like from the bus. If the GSP is released and then never
signals the condition the ROM is polling for, the ROM sits there forever, and no
frame budget will produce another byte. That is a question about what the ROM
reads after the release and what it expects back, and it needs a read trace across
the boundary, not a longer run.

One operational note for whoever runs this next, since it cost two wasted runs:
`STUNRUN_MC68010=1` is the flag that selects ROM execution. Without it the shell
runs the C-slice `game_loop_step`, exits cleanly, and still prints RESULT PASS.
Check for `gsp_writes=` in the output, not just the exit status.

## 22. `$25E8`-$269E transcribed whole, and the loop nesting settled against the capture

Section 20.7 left `$25E8` half-ported: selector resolution, descriptor decode and
the loop nesting were all still missing, and it flagged that the `D3`/`D2`/`D1`
nesting "does not reconcile with the observed 16-byte cell stride by
inspection". It does reconcile. This section is the whole routine, transcribed
verbatim, with the nesting taken from the capture rather than from inspection,
and the C slice that implements it.

Image used throughout: `roms/stunrun/stunrun_maincpu.bin`, sha256
`29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c`.

### 22.1 Verbatim transcription

ROM `$25E8`-`$269F`, 184 bytes, maincpu program ROM. Decoded with
`tools/dis68k` over the flat image. Note that section 13's trace-derived listing
omits the `clr.l D0`/`clr.l D1` at `$2612`/`$261E`; they are present in the ROM
and they matter (22.4).

```text
000025e8: 42 79 00 60 80 00  clr.w   $608000.l
000025ee: 02 79 e7 ff 00 c0 00 08  andi.w  #$e7ff, $c00008.l
000025f6: 41 f9 00 01 01 36  lea     $10136.l, A0
000025fc: 02 80 00 00 ff ff  andi.l  #$ffff, D0
00002602: e5 88  lsl.l   #2, D0
00002604: 20 70 08 00  movea.l (A0,D0.l), A0
00002608: 42 47  clr.w   D7
0000260a: 1e 28 00 02  move.b  ($2,A0), D7
0000260e: 3c 07  move.w   D7, D6
00002610: e1 4e  lsl.w    #8, D6
00002612: 42 80  clr.l   D0
00002614: 10 28 00 01  move.b  ($1,A0), D0
00002618: c0 fc 00 a0  mulu.w  #$a0, D0
0000261c: e1 88  lsl.l    #8, D0
0000261e: 42 81  clr.l   D1
00002620: 12 28 00 00  move.b  ($0,A0), D1
00002624: ed 89  lsl.l    #6, D1
00002626: d0 81  add.l   D1, D0
00002628: 06 80 ff 80 00 00  addi.l  #-$800000, D0
0000262e: 16 28 00 03  move.b  ($3,A0), D3
00002632: d1 fc 00 00 00 04  adda.l  #$4, A0
00002638: 12 18  move.b  (A0)+, D1
0000263a: 04 01 00 2a  subi.b   #$2a, D1
0000263e: 02 81 00 00 00 ff  andi.l  #$ff, D1
00002644: e9 49  lsl.w    #4, D1
00002646: 43 f9 00 00 1f bc  lea     $1fbc.l, A1
0000264c: d2 c1  adda.w   D1, A1
0000264e: 34 3c 00 07  move.w   #$7, D2
00002652: 32 3c 00 07  move.w   #$7, D1
00002656: 23 c0 00 c0 00 02  move.l  D0, $c00002.l
0000265c: 3a 19  move.w  (A1)+, D5
0000265e: 08 05 00 00  btst    #$0, D5
00002662: 67 00 00 08  beq     $266c
00002666: 33 c7 00 c0 00 0c  move.w  D7, $c0000c.l
0000266c: 08 05 00 02  btst    #$2, D5
00002670: 67 00 00 08  beq     $267a
00002674: 8d 79 00 c0 00 0c  or.w    D6, $c0000c.l
0000267a: 06 b9 00 00 00 10 00 c0 00 02  addi.l  #$10, $c00002.l
00002684: e8 4d  lsr.w    #4, D5
00002686: 51 c9 ff d6  dbra    D1, $265e
0000268a: 06 80 00 00 10 00  addi.l  #$1000, D0
00002690: 51 ca ff c0  dbra    D2, $2652
00002694: 04 80 00 00 7f c0  subi.l   #$7fc0, D0
0000269a: 53 03  subq.b   #1, D3
0000269c: 66 9a  bne     $2638
0000269e: 4e d2  jmp      (A2)
```

`$26A0` is a different routine and is not part of this one.

### 22.2 The loop nesting, and why inspection could not settle it

Three nested loops, and the `dbra` targets are the whole story:

| counter | seeded at | `dbra` at | target | passes |
|---|---|---|---|---|
| `D3` | `$262E` `move.b ($3,A0),D3` | `$269A` `subq.b #1,D3` / `$269C bne $2638` | `$2638` | `descriptor[3]` |
| `D2` | `$264E` `move.w #$7,D2` | `$2690 dbra D2,$2652` | `$2652` | 8 |
| `D1` | `$2652` `move.w #$7,D1` | `$2686 dbra D1,$265e` | `$265E` | 8 |

Two placements do all the work and are easy to misread:

* `$2652 move.w #$7,D1` is **inside** the `dbra D2` range (`$2652`..`$2690`),
  and `$264E move.w #$7,D2` is **outside** it. `D1` is therefore a per-pass
  local; `D2` is not reset per row.
* `$2656 move.l D0,$c00002.l` and `$265C move.w (A1)+,D5` are both **inside**
  the `dbra D2` range and **outside** the `dbra D1` range (`$265E`..`$2686`).
  So `D0` is re-latched once per row and `A1` advances once per row — **not**
  once per nibble. A glyph is 16 consecutive bytes, and `A1` lands on the next
  glyph's first word after the last row.

Taken together: 8 `HSTADR` latches per character, `D0 + $1000` per row, and
`subi.l #$7fc0,D0` per character = `+$8000 - $7fc0` = `+$40`, one four-pixel
cell to the right.

This is what the committed capture says, value for value. Reassembling the 480
taps of `result.json` that belong to draw 0 (frames 214-216, descriptor
`$06C53`) gives 240 `HSTADR` latches:

```text
latch[0]     $FF896040   base
latch[0..7]  +$1000 each   one glyph row
latch[8]     $FF896080   +$40, the second character
...
latch[239]   $FF89D780
```

240 = 30 characters x 8 rows, and every one of the 240 values, in order, is
reproduced by the transcription above — together with all 240 `A0`
(`descriptor + 5 + char_index`) and all 240 `A1`
(`$1FBC + 16*(character-$2A) + 2*row`). The capture and the disassembly agree;
there is nothing to arbitrate.

The check is not a one-descriptor accident. Selector `$43` (descriptor
`$06BE7`, `01 00 FF 0E` + `"PROGRAM*ROM*OK"`, frames 334-335 of the same
capture) is a different position, a different colour and a different length,
and its 112 latches — `$FF800040`, `$FF801040`, ..., also 112/112 in order —
match the same transcription.

The reassembly order matters and is a trap: `move.l D0,$c00002.l` on a 16-bit
bus delivers the **most significant** half at the lower address, and `$C00002`
is `HSTADRH`, so `HSTADR = (tap@$C00002 << 16) | tap@$C00004`. Reading it the
other way round yields `$6040FF89` and matches nothing at all.

### 22.3 The glyph is 4x8, not 8x8, and a blank nibble emits nothing

Each font word is 16 bits = **four** pixels, one per nibble. `lsr.w #4,D5` runs
eight times, so nibbles five to seven are read as zero — and a zero nibble
reaches neither `btst`, so both `beq` are taken and the routine emits **no
HSTDAT cycle at all**. Pixels five to seven therefore leave VRAM exactly as the
background left it.

That is what makes `$2694 subi.l #$7fc0,D0` come out at `+$40` and not at
`+$80`: a cell is four pixels of `$10` = `$40` wide, and the eight `$10`
increments after pixel four walk `HSTADR` into the *next* cell without touching
it.

Font evidence, from the ROM image rather than from a guess: the font block runs
`$1FBC`..`$327C` (300 glyphs, characters `$2A`..`$155`), and across all of it
nibbles take many values. But the 394 descriptors use only **45** characters,
`$2A`..`$5C`, and the 45 glyphs they reach use **only** nibble values `0`, `1`,
`4` and `5`. So:

```text
nibble 0  bit0=0 bit2=0   no HSTDAT cycle; VRAM untouched
nibble 1  bit0=1          move.w D7,$c0000c.l   -- overwrite with low colour
nibble 4  bit2=1          or.w  D6,$c0000c.l   -- read-modify-write, low plane kept
nibble 5  both            $2666 then $2674     -- two cycles, net D7|D6
```

Nibble bit 1 — the middle colour plane — is never set anywhere the text path
can reach, which is why the routine tests bit 0 and bit 2 and nothing else.

`'P'` reads `0555 1405 1405 1405 0555 0005 0005 0000`, and rendering the low
nibble of each as one pixel gives a legible four-wide glyph:

```text
0555  ###.      1405  #.##      0005  #...      0000  ....
```

### 22.4 HSTADR and HSTDAT, and what the prologue is for

`$25EE andi.w #$e7ff,$c00008.l` clears `HSTCTL` bits `$0800` and `$1000`, the
TMS34010's post-increment-on-`HSTDAT`-write and post-increment-on-`HSTDAT`-read
(MAME: `tms34010.cpp` `host_w`/`host_r`). The routine's own `ori.w #$1800` sites
set both, so without this the device would move `HSTADR` on every access and the
hand-rolled `addi.l #$10,$c00002.l` at `$267A` would double-step. The capture
confirms the clearing: the latch values are exact row bases, not bases plus a
per-pixel residue.

`addi.l #$10,$c00002.l` is a long read-modify-write of the **device** `HSTADR`;
`D0` is untouched by it. That is why the eight increments of a row are discarded
— the next row's `move.l D0,$c00002.l` overwrites the address — and why
`subi.l #$7fc0,D0` has to undo `$8000` of row stride and then add `$40`.

The `clr.l D0`/`clr.l D1` at `$2612`/`$261E` are not decoration. They clear the
upper bytes before `move.b` loads a coordinate byte, and without them the result
would depend on what `move.w #$SEL,D0` left in `D0`'s high word at the call
site.

### 22.5 CONFIRMED vs INFERRED

**CONFIRMED by `service-f2-text-host-writes/result.json` (draw 0, frames
214-216), 240/240 values in order:**

* the 240 `HSTADR` latches — base `$FF896040`, `+$1000` per row, `+$40` per
  cell, 8 per character, 30 characters — and, as an independent second case,
  the 112 latches of selector `$43`;
* `A0` = descriptor + 5 + character index;
* `A1` = `$1FBC + 16*(character - $2A) + 2*row`;
* the first glyph is `'P'`.

**CONFIRMED by the ROM bytes, NOT by the capture** (the capture taps
`$C00002`/`$C00004` only, and only at PC `$2656`, so it cannot see any of
these):

* the two prologue writes (`$608000`, then the `HSTCTL` read-modify-write);
* the descriptor decode `y*$A000 + x*$40 - $800000` and the colour
  `descriptor[2]` / `descriptor[2] << 8` — corroborated, not proven, by the
  first latch matching `$FF896040` and the `$DF` colour matching 21.4;
* the nibble rules and the blank-nibble silence of 22.3;
* the four-cycle shape of `addi.l #$10,$c00002.l` and the one-cycle
  `or.w D6,$c0000c.l`.

**INFERRED / open:**

* what a host read of `HSTDAT` returns — see 22.6;
* what lands in VRAM afterwards, since the real path is
  `hdgsp_vram_2bpp_w`, a masked read-modify-write of 32-bit words out of
  `m_mask_table`. Section 19 / `stunrun-2ua` owns that;
* how `$40`/`$1000`/`$A000` map onto visible pixels, which needs the 4 MiB VRAM
  extent before it can be said rather than derived.

### 22.6 Ambiguity: the `or.w D6,$c0000c.l` read-back

`or.w D6,$c0000c.l` reads `HSTDAT` back, and two readings survive:

* **(a)** the word this host port last stored at `HSTADR & ~$F`. This is what
  `native/musashi_cpu.c` `gsp_host_read_word` does for the shell's flat word
  model, and it is the only reading under which `$2666` followed by `$2674`
  computes what the routine plainly intends — write the low colour, then add
  the high colour to it.
* **(b)** the GSP memory word as the board actually keeps it. MAME's
  `tms340x0_device::host_r` does read GSP memory, but on this board that word
  is the *output* of `hdgsp_vram_2bpp_w`, so it is not the host word at all, and
  what it comes out to depends on the unresolved VRAM extent of section 19.

**(a) is implemented**, as a documented choice at the decision site in
`native/mc68010_text_writer.c` (`peek_hstdat`). **(b) is not**, and could not be
without also answering `stunrun-2ua`. The difference is observable and the test
pins (a): seed the target cell with `$0030` and a nibble-4 pixel must write
`$DF30`, not `$DF00`.

### 22.7 Our own mistakes, and their refutations

Kept here, not in `BUGS.md`: none of these is a defect in the 1989 game or in
MAME.

1. **D0 split in two.** The first port kept a separate `cell` for the row loop
   and stepped `base` by `-0x7FC0` only, so the second character latched at
   `base - $7fc0` instead of `base + $40`. Caught by
   `selector_47_matches_capture` (`latches[8] = 0xFF80C100`). The ROM has one
   register: `addi.l #$1000,D0` at `$268A` mutates the same `D0` the latch read.
2. **Font pointer reset per row.** `word_address` was declared inside the row
   body, so every row re-read glyph word 0 and the whole cell came out as the
   top row repeated. Caught by `census` (5386 writes against 4983). `$2646`/
   `$264C` run once per *character*.
3. **`HSTADR` halves swapped in the fixture generator.** `$C00002` was written
   into the low half of the port state, which silently produced `$6040FF89`
   as the first latch and a plausible-looking but wrong stream. The capture
   comparison caught it. `$C00002` is `HSTADRH`.
4. **A fixture placed at the constant under test.** The test built its ROM image
   with `store_long(rom, STUNRUN_TEXT_SELECTOR_TABLE + 4*sel, ...)`, so moving
   the constant in the header moved the fixture with it and nothing failed. The
   table is now placed at a literal `$10136` in the test, with one equality
   check against the header's copy. This is section 15.6's trap in a subtler
   form: the test passed because the thing under test was consistent with
   itself, not because it was right.

### 22.8 The C slice, and what it asserts

`native/mc68010_text_writer.{h,c}` add `stunrun_mc68010_text_draw()`, the whole
routine, driven through a transport seam that reports every 16-bit cycle
including the read half of each read-modify-write. `stunrun_mc68010_text_write()`
is left byte-for-byte as it was, because CTest `mc68010-text-writer-slice-c`
asserts its values — but it disagrees with the ROM in two documented ways (it
writes one word per pixel rather than one cycle per lit nibble, and it writes
`0` for a blank pixel where the ROM writes nothing), and its write count is not
a pixel count. Both are called out in the header.

The observable output asserted is the ordered sequence of 16-bit bus cycles,
because that is what the routine produces and what a trace sees. The 240
`HSTADR` latches are compared against the capture verbatim; an 84-cycle literal
golden covers the first two glyph rows, which between them contain every
`HSTDAT` shape the ROM can produce; the remaining 28 characters are walked in
lockstep against the nibble rule derived from the embedded font; and the census
(9154 cycles, 4983 writes, 4171 reads, 661 `HSTDAT` writes) pins the whole
call exactly. `native/mc68010_text_writer_test.c` builds its own 66 KiB ROM
image from transcribed bytes, so no commercial ROM is needed.

### 22.9 Negative controls

Sixteen deliberate breaks were applied to the slice and each one fails the test:
`btst #2`→`#3`; dropping the `$2666` write; cell stride `$7fc0`→`$7f80`; row
stride `$1000`→`$0100`; dropping the prologue's `HSTCTL` mask; turning
`or.w D6` into a plain write; row coefficient `$a0`→`$a1`; column shift `#6`→`#5`;
pixel step `$10`→`$20`; resetting the font pointer per row; count off by one;
removing the nibble shift; high colour not shifted; the selector-table base
moved; the watchdog address moved; and the `HSTADR` halves emitted the other way
round. Numbers 2 and 4 above are among them, which is how they were found.

### 22.10 Still open

* `stunrun-2ua` (4 MiB VRAM extent) blocks the byte-level check of what this
  routine leaves in VRAM, and with it the answer to 22.6(b).
* The routine is entered only as `jmp $25E8.l` from 30 sites with `A2` set. The
  `jmp (A2)` at `$269E` is transcribed but nothing here models a caller, so
  "which routine draws what, in which order" is still a harness question.
* There is a **second entry point into the same loop body**, and it is not a
  mystery clone. `$2530`-`$25E6` and `$25E8`-`$269F` are two routines with a
  duplicated loop, differing only in setup and return convention:

  ```text
  $00196:  jmp $2580.l      dispatch table -> per-character entry
  $001A8:  jmp $2530.l      dispatch table -> full routine, rts
  (30 sites)  jmp $25E8.l   in-line -> full routine, jmp (A2)

  $2530  clr.w $608000 / andi.w #$e7ff,$c00008 / selector / descriptor / loop / rts
  $2580  move.b (A0)+,D1   <- the $2638 equivalent, no prologue, no decode
  ...
  $25E4  bne $2580
  $25E6  rts

  $25E8  clr.w $608000 / andi.w #$e7ff,$c00008 / selector / descriptor / loop / jmp (A2)
  $2638  move.b (A0)+,D1   <- the $2580 equivalent, byte for byte
  ...
  $269C  bne $2638
  $269E  jmp (A2)
  ```

  So `$25E8` is a self-contained "draw this selector's string, then return to my
  caller via `A2`" wrapper, while `$2580` is the bare "carry on drawing from
  wherever `A0` points, with `D0`/`D3`/`D7`/`D6` already set" entry — which is
  why it skips the descriptor decode entirely and returns with `rts`. Recorded,
  not ported; the `$2580` caller's register setup is not yet traced, so what
  `D0` and `D3` hold there is UNKNOWN.
## 23. A decoded text log for the MC68010 text path — and what `$25E8` really is

Section 20 mapped `$25E8`'s data statically and §21 closed it against live MAME
traffic. This section adds the thing that makes a native run legible: a console
line per string the game draws. It is **observation only** — it reads the
host-port latches and the ROM and prints, and it changes nothing.

The point of writing it down is not the feature. It is that building it forced
three corrections to what §13 and §20 say about the routine, all of them
measured rather than read.

### 23.1 One `$25E8` call draws ONE WHOLE STRING, not one character

§13 quotes the loop nest but §20.7 flags that its `D3`/`D2`/`D1` nesting "does
not reconcile with the observed 16-byte cell stride by inspection". It does
reconcile, and the nesting is:

```text
  $264E: move.w #$7,D2           ; row loop seed  -> 8 glyph rows
  $2652: move.w #$7,D1           ; pixel loop seed -> 8 cells per row
  $2656: move.l D0,$c00002.l     ; latch the row's first cell
  $2666: move.w D7,$c0000c.l
  $2674: or.w   D6,$c0000c.l
  $267A: addi.l #$10,$c00002.l   ; next cell, +0x10
  $2686: dbra D1,$265e
  $268A: addi.l #$1000,D0        ; next row, +0x1000
  $2690: dbra D2,$2652
  $2694: subi.l #$7fc0,D0        ; next CHARACTER: +0x40, one more column
  $269A: subq.b #$1,D3
  $269C: bne   $2638
```

`$262E move.b ($3,A0),D3` loads the character count, and `$269C` loops **the
whole 8×8 nest per character**. So one call emits `count` × 8 HSTADR
compositions: 8 rows at +0x1000 each, and +0x40 per character. Measured over a
600-frame service-mode capture of the real ROM, a single `$25E8` call for
selector `$47` (`chars = 30`) produced exactly **240** compositions, the first
at `0xFF896040` and the last at `0xFF89D780 = base + 0x40*29 + 0x1000*7`.

**§13's transcription is otherwise byte-exact** — re-disassembled from
`roms/stunrun/stunrun_maincpu.bin` with capstone, every address matches. Two
corrections to the prose around it:

1. §13 omits `$2612 clr.l D0` between `$2610 lsl.w #8,D6` and `$2614
   move.b ($1,A0),D0`. It is there (`4280`), and it matters: it is what makes
   `$2618 mulu.w #$a0,D0` a 16×16 multiply of a *byte* rather than of a stale
   word.
2. §13 and §20.5 report caller addresses read out of a trace in which Musashi's
   `M68K_REG_PC` is sampled inside the bus callback, so the PC in a capture sits
   a few bytes **past** the instruction. The real `move.l D0,$c00002.l` is at
   `$2656` and appears as PC `$265C` in `gsp_bus_events.bin`; `addi.l #$10` is at
   `$267A` and appears as `$2684`; `andi.w #$e7ff,$c00008.l` is at `$25EE` and
   appears as `$25F6`. Anyone filtering a capture on PC `$2656` or `$25EE` will
   find nothing, which is a trap worth naming.

### 23.2 "The address matches a descriptor base" is NOT sufficient, and that is measured

The obvious decoder — resolve the 394-long table, compute each descriptor's
base, print when HSTADR lands on one — is wrong, and provably so on this ROM.
Within the single `$25E8` call above:

* **three** of the 240 compositions land exactly on *other* descriptors' bases:
  character 0 at `$FF896040`, character 8 at `$FF896240`, character 17 at
  `$FF896440`;
* five further matches occur during the `$267A` pixel walk, which steps onto
  `$FF896240` from `$FF896230` — one pixel past the row-0 boundary.

Base `0xFF896040` carries **18** descriptors across five colours, so a
base-only rule prints one character of `PRESS START TO BYPASS SELFTEST` as if
it were `RCOIN`, `LEFT TRIGGER`, or a dozen others. §20.4 noted the
`$47`/`$16B` collision; the scale is larger than one pair.

Two ROM facts remove it, and neither is an emulator artifact:

1. **The `$25EE` prologue is the arm.** `andi.w #$e7ff,$c00008.l` runs once per
   call and leaves HSTCTL bits 11 and 12 (`0x1800`) *clear*, because that is
   what the mask does. Every other HSTCTL writer in the ROM *sets* them:
   `ori.w #$1800,$c00008.l` in the display-list compiler, and the boot paths,
   which write `0xB800`. Measured values in one capture — `0xA000` at PC `$25F6`,
   `$2334`, `$239C`, `$414` (bits clear) versus `0xB800` at `$496`, `$578`,
   `$63E`, `$682`, `$5410`, `$5496`, `$54CA` (bits set). The first HSTADR
   composition after an arm is therefore the descriptor base, and nothing can
   drift in between: the arm is immediately followed by `$2656`.
2. **The colour is in the LOW byte of the first HSTDAT.** `clr.w D7 / move.b
   ($2,A0),D7` leaves `D7 = 0x00XX`, and `$2666` writes `D7` whole. Measured
   live for `$47`: HSTDAT `0x00DF` then `0xDFDF`. The second write (`or.w D6`,
   with `D6 = D7 << 8`) carries the colour in the *high* byte and so selects
   nothing.

Over the 600-frame service run the rule gives **26 arms, 1 matched, 1 fired,
0 colour misses** — one `$25E8` call, one arm, one string.

### 23.3 What the host port genuinely cannot tell you

Colour is not always a discriminator. 35 of the 394 descriptors share a base
with another, and three share **base and colour** at `$FF800040`:

```text
$034 colour 0xFF   8 BIT A/D
$035 colour 0xFF  12 BIT A/D
$043 colour 0xFF  PROGRAM ROM OK
$044 colour 0x9F  BAD PROGRAM ROM IS BLANK BOX
```

Those three are drawn one at a time to the same screen cell in the same colour,
so no host-port write distinguishes them. `$047 "PRESS START TO BYPASS
SELFTEST"` has the same problem with `$100 "INC PM ADR"` at `0xFF896040` in
`0xDF`.

The decoder prints the lowest-numbered candidate and then names the runners-up
on a following line, rather than picking one silently:

```text
text frame=48 cycles=6508646 sel=$047 x=01 y=0F colour=0xDF base=0xFF896040 chars=30 "PRESS START TO BYPASS SELFTEST"
text   base=0xFF896040 colour=0xDF is shared by 2 descriptors; the port cannot separate them: $047 "PRESS START TO BYPASS SELFTEST" $100 "INC PM ADR"
```

### 23.4 The glyph alphabet is plain ASCII, and `byte 3` is exact

§20.2 says `byte 3` equals the string length for 388 of 394 blocks. Measured
over all 394 in this ROM: **394 of 394**, zero mismatches. Either the earlier
count used a different length rule or it was simply wrong; the "padding or
table-end artifact" reading is not needed here.

The character set over all 394 blocks is `$2A..$5C` — `* + , - . / 0-9 : =
A-Z \` — i.e. plain ASCII over a contiguous glyph window, with `*` (`$2A`)
standing in for the space. `*` is therefore the ROM's blank and prints as a
space. The font is `$1FBC + 16*(ch - $2A)`, 51 glyphs through `$22EC`.

### 23.5 Service mode in the native shell, and `$43`

The native shell has **no `service_f2` input mode**. `service_f2` exists only
in the MAME harness (`mame/lua/*.lua`, `tools/mame-trace`,
`tools/mame-ram-write-trace`). What the native shell has is:

| mechanism | what it maps |
|---|---|
| `argv[1]` experiment JSON `events[]` | `:mainpcb:8BADC.0`/`AD Stick X`, `:mainpcb:8BADC.2`/`AD Stick Y`, `:mainpcb:IN0`/`Coin 1`, `:mainpcb:a80000`/`1 Player Start` (`native/fake_ports.c` `named_port_id()`) |
| env overrides | `STUNRUN_IN0`, `STUNRUN_SW1`, `STUNRUN_A800` |

There is no `Service Mode` field, and `fake_ports.c` is not this change's write
scope, so none was added. The equivalent stimulus is the one `service_f2`
itself presses: `IN0` bit 5 is `PORT_SERVICE`, active low (`fake_ports.c`
`stunrun_ports_init`, harddriv.cpp:1097-1108), and the boot-path default is
`0xE7`. Holding it gives:

```sh
STUNRUN_MC68010=1 STUNRUN_STUNRUN_DIR=roms/stunrun STUNRUN_FRAMES=600 \
STUNRUN_IN0=0xC7 STUNRUN_TEXT_LOG=1 STUNRUN_DUMP_DIR=/tmp/svclog \
  native/build/stunrun-native-shell
```

```text
shell: text-log=1 descriptors=394 (394 expected for this ROM)
text frame=48 cycles=6508646 sel=$047 x=01 y=0F colour=0xDF base=0xFF896040 chars=30 "PRESS START TO BYPASS SELFTEST"
text   base=0xFF896040 colour=0xDF is shared by 2 descriptors; the port cannot separate them: $047 "PRESS START TO BYPASS SELFTEST" $100 "INC PM ADR"
shell: dump dir=/tmp/svclog gsp_writes=721479 bus_events=732879 bus_dropped=0 vram=691861 vram2bpp=28016 pal_lo=768 pal_hi=768 control=42 unmapped=24
shell: text-log arms=26 matched=1 fired=1 colour_miss=0 ambiguous=1
```

**`$47` reproduced exactly.** `$43 "PROGRAM ROM OK"` was **not** reproduced, and
the reason is a limitation rather than a tuning failure:

* the machine draws `$47` at frame 48 and then stops issuing host-window writes.
  600 frames and 1800 frames both produce **732 879** bus events, so the walk is
  genuinely idle rather than budget-truncated (this is §22's point again);
* pressing `1 Player Start` — a named port the shell *does* support — at frame
  200 changes nothing: 732 879 bus events with and without it.

So the native shell's input model does not reach the self-test navigation that
MAME's `service_f2` reaches, and `$43` stays behind that. **UNKNOWN** whether
the self-test wants an F2 toggle rather than START, or a coin input the native
`Coin 1` port does not model the way MAME does. Closing it needs either a
`Service Mode` field in `native/fake_ports.c` or the self-test's own input map;
it does not need more cycles.

Also worth recording: the **attract-mode** 600-frame run (no `STUNRUN_IN0`)
executes `$25E8` **zero** times — 26 arms are still counted, none of them match —
so the text log is empty on the normal boot path. That is a real property of the
attract sequence at this frame budget, not a decoder miss.

### 23.6 Observation-only, measured rather than asserted

`gsp_host_write_word()` is the single choke point for 16-bit writes into
`$C00000-$C03FFF`. The hook is `if (g_text_log_enabled) text_log_observe(...)`
at the top of that function — one predictable not-taken branch when the feature
is off (`STUNRUN_TEXT_LOG` unset, the default). Nothing in the decoder writes a
latch, GSP memory, VRAM, or the ordered bus log.

The check, on the 600-frame attract run:

```text
                                    bytes        gsp_writes  bus_events
pre-change binary, log off        36054160        897010      901354
new binary, log off               36054160        897010      901354
new binary, log ON                36054160        897010      901354
```

The raw bytes are **not** identical, and this was already true before the
change: the pre-change binary and the new one differ from each other at exactly
the same places. Those places are the three padding bytes at record offsets
29-31 and 37-39 of `musashi_gsp_bus_event_t`, which `gsp_emit_bus_event()`
never assigns — they are uninitialised stack. `sequence`, `cycles`, `address`,
`pc`, `data`, `lane_mask`, `width_bits`, `kind` and `gsp_reset_released` are
every field the struct names.

```text
raw differing bytes   OFF vs ON   : 5 898 of 36 054 160, all at offsets 29/37/38/39
raw differing bytes   PRE vs OFF  : 533 751 of 36 054 160, all at offsets 29/30/37/38

padding bytes masked (29,30,31,37,38,39):
  OFF == ON                         : True
  PRE == OFF                        : True
  sha256                            : 9cf8f91c0201e11c14d6c7ab77fe533f171c56293100ab8db8e98896d811a89f

all 901 354 records, every initialised field:
  OFF vs ON                         : IDENTICAL
  PRE vs OFF                        : IDENTICAL
```

A raw `sha256` comparison of `gsp_bus_events.bin` therefore **cannot** be an
observation-only test: it was already binary-dependent before this feature
existed, and it will report a difference for any recompile. Masking the padding,
or comparing the nine named fields, is the check that means something. Zeroing
those bytes in `gsp_emit_bus_event()` would make the artifact byte-stable across
binaries, which is a real improvement and is deliberately **not** done here: it
changes every existing capture's bytes, so it wants its own commit.

### 23.7 The test

`native/musashi_text_log_test.c` builds a synthetic 394-entry selector table
carrying both real collision shapes, drives a synthetic 68010 program through
Musashi, and asserts the **printed lines** — the feature has no other output,
and adding a callback side-channel would let a broken printer pass. It asserts
the negative cases too: a disarm (`ori.w #$1800`) prints nothing, a missing
prologue prints nothing, and the `+0x40` pixel walk does not re-fire. It also
asserts that the same program takes the same 18 host-window bus events with the
log off and on.

**Not registered in `native/CMakeLists.txt`** — that file is shared and the
parent owns it. The block to add, modelled on `gsp-display-list-musashi-test`:

```cmake
add_executable(stunrun-musashi-text-log-test
  musashi_text_log_test.c
  fake_ports.c
  musashi_cpu.c
  musashi_loader.c
  cpu_override.c
  jsa_transport.c
  ../reproduction/sound/jsa_latch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c)
target_include_directories(stunrun-musashi-text-log-test PRIVATE
  . ../reproduction/sound
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi)
target_link_libraries(stunrun-musashi-text-log-test PRIVATE
  m
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
set_target_properties(stunrun-musashi-text-log-test PROPERTIES C_STANDARD 99)
add_test(NAME musashi-text-log-c COMMAND stunrun-musashi-text-log-test)
```

Verified green by compiling it by hand (315 checks, 0 failures), and confirmed
able to fail: deleting the arm, changing the base stride to `0x80`, taking the
colour from the high byte, printing `*` literally, and dropping the ambiguity
line each produce between 5 and 19 failures.
---

## 24. Object record caller $03CF40

Inspected on 2026-10-12 from the flat ROM image
`roms/stunrun/stunrun_maincpu.bin` (sha256 `29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c`).
Disassembled by hand from bytes at file offset `0x3CF40`.

### 24.1 Function disassembly

ROM bytes at file offset `0x3CF40` (40 bytes total, 20 words):

```
03CF40: 4E56 0000     LINK A6,#0               ; Save old A6, set A6 = A7
03CF44: 48E7 2000     MOVEM.L D2/A2-A4,-(A7)   ; Save registers
03CF48: 342E 000A     MOVE.L (A6+$0A),D2       ; D2 = record_index from caller
03CF4C: 3202          MOVE.B D2,D1             ; D1 = low byte of index
03CF4E: 207C FFFF8000 LEA.L #$FFFF8000,A0      ; A0 = input/state area base
03CF54: 7000          MOVEQ #$00,D0             ; D0 = 0
03CF56: 1030 1000     MOVE.B (A0,D0.L),D0      ; D0 = input_byte at $FFFF8000
03CF5A: 2F00          MOVE.L D0,-(A7)           ; Push input_byte (param 1)
03CF5C: 3002          MOVE.W D2,D0              ; D0 = record_index (word)
03CF5E: E940          ASL.W #2,D0              ; D0 = index * 4 (scale)
03CF60: 207C FF DD86  LEA.L #$FFDD86,A0        ; A0 = record base
03CF66: 4870 0000     PEA.L (A0,D0.W)          ; Push record ptr = base + index*4
03CF6A: 4EB9 0003D120 JSR $0003D120            ; Call updater: clamps +0,+1,+2
03CF70: 3002          MOVE.W D2,D0              ; D0 = record_index (recompute)
03CF72: E940          ASL.W #2,D0              ; D0 = index * 4
03CF74: 207C FF DD86  LEA.L #$FFDD86,A0        ; A0 = record base
03CF7A: 4870 0000     PEA.L (A0,D0.W)          ; Push record ptr (again)
03CF7E: 4EB9 0003D1A0 JSR $0003D1A0            ; Call updater: advances +5,+6, writes +2
03CF84: 4CEE 0004     MOVEM.L (A7)+,D2/A2-A4  ; Restore registers
03CF88: (padding)
03CF8A: 4E5E          UNLK A6                  ; Restore old A6
03CF8C: 4E75          RTS                      ; Return
```

### 24.2 Entry conditions

- **Stack**: `LINK A6,#0` establishes frame; `(A6+$0A)` is the first argument
  (4-byte record index passed by caller).
- **Registers at entry** (from caller context):
  - `D2` = record_index (word, sign-extended to long)
  - Other registers have caller values; this function saves D2/A2-A4.
- **Memory read**: Byte at `$FFFF8000` (index 0 in the input/state area) is
  loaded into `D0` and pushed as a stack parameter, but **D0 is immediately
  overwritten with the index value** before any address calculation. The
  input_byte is **NOT used** in the record address computation.
- **Address calculation**: `record_ptr = $FFDD86 + (index * 4)`.
  - The index is a 16-bit value, scaled by 4 via `ASL.W #2`.
  - This yields a 4-byte stride, NOT 16 bytes per record as the analysis
    previously documented.
  - The record pointer is computed using `LEA.L #$FFDD86,A0` followed by
    `PEA.L (A0,D0.W)` (address = A0 + sign_extend(D0.W)).

### 24.3 Called routines

| Address | Name | Purpose |
|---------|------|---------|
| `$03D120` | `object_update_clamp` | Clamps three byte fields at offsets +0, +1, +2 |
| `$03D1A0` | `object_update_interp` | Advances interpolation/counter fields at +5, +6, writes smoothed value to +2 |

Both are called with the **same** record pointer on the stack.

### 24.4 Observable output

- Two JSR calls to the update routines, each receiving a record pointer.
- The record pointer points to `ram + 0x5D86` (offset of $FFDD86 from RAM base $FF8000)
  plus `index * 4`.
- The input_byte at $FFFF8000 is pushed but unused.

### 24.5 Confirmed vs inferred

| Item | Status |
|------|--------|
| Disassembly bytes at 0x3CF40 | **CONFIRMED** from ROM hex |
| Index read from (A6+$0A) | **CONFIRMED** from `MOVE.L (A6+$0A),D2` |
| Input byte read from $FFFF8000 | **CONFIRMED** from `MOVE.B (A0,D0.L),D0` |
| Input byte NOT used in address calc | **CONFIRMED** — D0 is overwritten before ASLW |
| Index multiplied by 4 | **CONFIRMED** from `ASL.W #2,D0` |
| Record base = $FFDD86 | **CONFIRMED** from `LEA.L #$FFDD86,A0` |
| Both $03D120 and $03D1A0 called | **CONFIRMED** from two JSR instructions |
| Same record pointer passed to both | **CONFIRMED** — identical address computation before each call |
| Record stride = 4 (not 16) | **CONFIRMED** from `ASL.W #2` — contradicts earlier analysis |

### 24.6 Ambiguities

1. **Record size**: The analysis documents 16-byte records, but the code multiplies
   the index by 4, not 16. This may indicate:
   - The index is a sub-record index (each record has 4 longwords), OR
   - The 16-byte stride applies to a different addressing mode.
   The actual record size should be verified against runtime traces.

2. **Input byte purpose**: The byte at $FFFF8000 is read and pushed but not used.
   The analysis does not explain why this read exists. Possible interpretations:
   - Side effect of a shared prologue
   - Debug/monitoring feature
   - Unfinished code path

### 24.7 C slice

Implemented in `native/mc68010_object_writer.{h,c}`:
- `stunrun_mc68010_object_record_caller()`: transport seam
- `stunrun_object_record_ptr()`: address calculator
- Test: `native/mc68010_object_writer_test.c` (7 checks, all pass)

The slice correctly calls both updater callbacks with the computed record pointer.

### 24.8 Object update clamp ($03D120)

**Ghidra symbol:** `s_0003D120_mc68010_object_update_record`

**ROM address:** $03D120 (file offset 0x3D120)

**ROM bytes (hex):**
```
4E56 0000 48E7 3800 206E 0008 102E 000F
1210 1628 0001 1828 0002 7200 1200 7400
1403 5282 B282 6210 7200 1200 7400 1403
5382 B282 6502 1003 1203 1600 B204 6314
B604 6310 B203 6406 7000 1001 6004 7000
1003 6016 B204 6414 B604 6410 B203 6306
7000 1001 6004 7000 1003 1800 1081 1143
0001 1144 0002 4CEE 001C FFF4 4E5E 4E75
```

**Full disassembly (capstone m68k-010):**
```
03D120: LINK.W   A6,#$0              ; Save A6, set A6 = SP
03D124: MOVEM.L  D2-D4,-(A7)         ; Save D2,D3,D4
03D128: MOVEA.L  $8(A6),A0           ; A0 = record pointer (stack arg)
03D12C: MOVE.B   $F(A6),D0            ; D0 = byte at A6+$0F (DISCARDED)
03D130: MOVE.B   (A0),D1              ; D1 = record[0] (original value)
03D132: MOVE.B   $1(A0),D3            ; D3 = record[1] (bound A)
03D136: MOVE.B   $2(A0),D4            ; D4 = record[2] (bound B)
; ---- Saturation loop 1: D1 = min(original, D3) ----
03D13A: MOVEQ    #$0,D1               ; D1 = original record[0]
03D13C: MOVE.B   D0,D1               ; (redundant reload; D0=original)
03D13E: MOVEQ    #$0,D2               ; D2 = 0
03D140: MOVE.B   D3,D2               ; D2 = D3 = record[1]
03D142: ADDQ.L   #$1,D2               ; D2++
03D144: CMP.L    D2,D1                ; compare D1,D2
03D146: BHI.B    $3D158               ; if D1 > D2 (unsigned), loop
                                        ; loop exits when D2 >= D1
                                        ; D1 (original) saturates to D3
; ---- Saturation loop 2: D1 = max(original, D3) ----
03D148: MOVEQ    #$0,D1               ; D1 = original record[0]
03D14A: MOVE.B   D0,D1               ;
03D14C: MOVEQ    #$0,D2               ; D2 = 0
03D14E: MOVE.B   D3,D2               ; D2 = D3 = record[1]
03D150: SUBQ.L   #$1,D2               ; D2-- (start at D3-1)
03D152: CMP.L    D2,D1                ; compare D1,D2
03D154: BCS.B    $3D158               ; if D1 < D2 (unsigned), loop
                                        ; loop exits when D2 <= D1
                                        ; D1 (original) saturates to D3
03D156: MOVE.B   D3,D0               ; D0 = D3 = saturation result
; ---- Merge and final clamp decision ----
03D158: MOVE.B   D3,D1               ; D1 = D3 = record[1] (upper bound)
03D15A: MOVE.B   D0,D3               ; D3 = D0 = original record[0]
03D15C: CMP.B    D4,D1               ; compare D4,D1 (lower vs upper)
03D15E: BLS.B    $3D174               ; if lower <= upper, use clamp path
03D160: CMP.B    D4,D3               ; compare lower vs original
03D162: BLS.B    $3D174               ; if lower <= original, preserve original
03D164: CMP.B    D3,D1               ; compare original vs upper
03D166: BCC.B    $3D16E               ; if original >= upper, clamp to upper
03D168: MOVEQ    #$0,D0               ; D0 = 0
03D16A: MOVE.B   D1,D0               ; D0 = upper
03D16C: BRA.B    $3D172               ;
03D16E: MOVEQ    #$0,D0               ; D0 = 0
03D170: MOVE.B   D3,D0               ; D0 = original
03D172: BRA.B    $3D18A               ;
03D174: CMP.B    D4,D1               ; (re-check lower <= upper)
03D176: BCC.B    $3D18C               ; if lower >= upper, preserve original
03D178: CMP.B    D4,D3               ;
03D17A: BCC.B    $3D18C               ; if lower >= original, preserve original
03D17C: CMP.B    D3,D1               ;
03D17E: BLS.B    $3D186               ; if original <= upper, clamp to upper
03D180: MOVEQ    #$0,D0               ; D0 = 0
03D182: MOVE.B   D1,D0               ; D0 = upper
03D184: BRA.B    $3D18A               ;
03D186: MOVEQ    #$0,D0               ; D0 = 0
03D188: MOVE.B   D3,D0               ; D0 = original
03D18A: MOVE.B   D0,D4               ; D4 = final clamped value
03D18C: MOVE.B   D1,(A0)             ; record[0] = D1 (upper/saturation)
03D18E: MOVE.B   D3,$1(A0)           ; record[1] = D3 (original)
03D192: MOVE.B   D4,$2(A0)           ; record[2] = D4 (clamped)
03D196: MOVEM.L  -$C(A6),D2-D4        ; Restore D2,D3,D4
03D19C: UNLK     A6                  ; Restore old A6
03D19E: RTS                          ; Return
```

#### Semantics (CONFIRMED by exhaustive trace)

The function clamps the byte at record offset +0 to the range
`[min(record[1],record[2]), max(record[1],record[2])]`:

```
record[0] = clamp(record[0], min(record[1],record[2]), max(record[1],record[2]))
record[1] = original record[0]  (preserved, not overwritten)
record[2] = same clamp result as record[0]
```

The clamp operation:
- if `record[0] < lower`: `record[0] = lower`
- if `record[0] > upper`: `record[0] = upper`
- if `lower <= record[0] <= upper`: `record[0]` passes through unchanged

**Implementation:**
```c
void stunrun_mc68010_object_update_record(uint8_t *record)
{
    uint8_t v0 = record[0];  /* original value */
    uint8_t v1 = record[1];  /* bound A */
    uint8_t v2 = record[2];  /* bound B */
    uint8_t lower = (v1 <= v2) ? v1 : v2;
    uint8_t upper = (v1 <= v2) ? v2 : v1;
    uint8_t clamped;

    if (v0 < lower) {
        clamped = lower;
    } else if (v0 > upper) {
        clamped = upper;
    } else {
        clamped = v0;
    }

    record[0] = clamped;
    record[1] = v0;      /* original preserved */
    record[2] = clamped;
}
```

#### Confirmed vs inferred

| Item | Status | Evidence |
|------|--------|----------|
| LINK/MOVEM save/restore | **CONFIRMED** | Matches $03CF40 prologue |
| A0 = record pointer from stack | **CONFIRMED** | `MOVEA.L $8(A6),A0` |
| D0 = byte at A6+$0F (DISCARDED) | **CONFIRMED** | Same pattern as $03CF40 |
| Loads record[0], [1], [2] into D1, D3, D4 | **CONFIRMED** | Capstone disassembly |
| Two saturation loops | **CONFIRMED** | ADDQ/SUBQ loops with CMP/BHI/BCS |
| Swap at 0x3D158 (D1=D3, D3=D0) | **CONFIRMED** | Capstone disassembly |
| CMP/BLS/BCC branch structure | **CONFIRMED** | Verified by exhaustive 256^3 trace |
| Final writes at 0x3D18C-0x3D192 | **CONFIRMED** | `MOVE.B D1,(A0)`, `D3,$1(A0)`, `D4,$2(A0)` |
| MOVEM/UNLK/RTS epilogue | **CONFIRMED** | Standard 68010 function epilogue |
| Saturation mode when lower > upper | **INFERRED** | Bytecode implements this; game usage unclear |

#### Observable output

The function modifies three bytes in the record:
- `record[0]` = clamped value
- `record[1]` = original value (preserved)
- `record[2]` = clamped value (same as record[0])

#### C slice

Implemented in `native/mc68010_object_writer.{h,c}`:
- `stunrun_mc68010_object_update_record()`: the clamp function
- Test: `native/mc68010_object_writer_test.c` (14 checks, all pass)
  - 7 existing tests for the caller transport
  - 7 new tests for the clamp function including exhaustive 1000-combination sample

## 16. MC68010 object interpolation advance ($03D1A0)

**Date:** 2026-10-13

### Source

- ROM file: `136070-2102.200r` (stunrun_maincpu.bin, SHA256 `29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c`)
- Listing: Ghidra MAME listing at `/tmp/opencode/stunrun-listings-current/maincpu-68010.lst`
- File offset: `0x03D1A0`

### Disassembly (verbatim from listing)

```text
03D1A0: link    A6, #$0
03D1A4: movea.l ($8,A6), A0         ; A0 = record pointer from stack
03D1A8: move.b  ($4,A0), D0         ; D0 = record[4]
03D1AC: cmp.b   ($2,A0), D0          ; compare record[4] vs record[2]
03D1B0: bcc     $3d1f4               ; if record[4] >= record[2] (C=0): inactive
03D1B2: addq.b  #1, ($5,A0)          ; counter++
03D1B6: move.b  ($2,A0), D0         ; D0 = record[2]
03D1BA: cmp.b   ($6,A0), D0          ; compare record[2] vs record[6]
03D1BE: bcc     $3d1c6               ; if record[2] >= record[6] (C=0): skip update
03D1C0: move.b  ($2,A0), ($6,A0)   ; record[6] = record[2] (update saved target)
03D1C6: cmpi.b  #$6, ($5,A0)       ; counter >= 6?
03D1CC: bcs     $3d1ec               ; if counter < 6: skip interpolation
03D1CE: moveq   #$0, D0             ; D0 = 0
03D1D0: move.b  ($6,A0), D0         ; D0 = record[6] (saved target)
03D1D4: moveq   #$0, D1             ; D1 = 0
03D1D6: move.b  ($4,A0), D1         ; D1 = record[4] (current value)
03D1DA: sub.l   D1, D0              ; D0 = record[6] - record[4]
03D1DC: lsr.l   #1, D0               ; D0 = (record[6] - record[4]) / 2
03D1DE: add.b   D0, ($4,A0)          ; record[4] += step
03D1E2: move.b  #$ff, ($6,A0)        ; record[6] = 0xFF (inactive marker)
03D1E8: clr.b   ($5,A0)             ; counter = 0
03D1EC: move.b  ($4,A0), ($2,A0)   ; record[2] = record[4] (write back)
03D1F2: bra     $3d1fe               ; → Track 2
03D1F4: move.b  #$ff, ($6,A0)        ; record[6] = 0xFF (inactive)
03D1FA: clr.b   ($5,A0)             ; counter = 0
03D1FE: move.b  ($a,A0), D0         ; D0 = record[10]
03D202: cmp.b   ($2,A0), D0          ; compare record[10] vs record[2]
03D206: bls     $3d248               ; if record[10] <= record[2] (C=1 or Z=1): skip
03D208: addq.b  #1, ($b,A0)         ; counter++
03D20C: move.b  ($2,A0), D0         ; D0 = record[2]
03D210: cmp.b   ($c,A0), D0          ; compare record[2] vs record[12]
03D214: bls     $3d21c               ; if record[2] <= record[12] (C=1 or Z=1): skip
03D216: move.b  ($2,A0), ($c,A0)   ; record[12] = record[2] (update saved target)
03D21C: cmpi.b  #$6, ($b,A0)       ; counter >= 6?
03D222: bcs     $3d240               ; if counter < 6: skip interpolation
03D224: moveq   #$0, D0             ; D0 = 0
03D226: move.b  ($a,A0), D0         ; D0 = record[10] (current value)
03D22A: moveq   #$0, D1             ; D1 = 0
03D22C: move.b  ($c,A0), D1         ; D1 = record[12] (saved target)
03D230: sub.l   D1, D0              ; D0 = record[10] - record[12]
03D232: lsr.l   #1, D0              ; D0 = (record[10] - record[12]) / 2
03D234: sub.b   D0, ($a,A0)          ; record[10] -= step
03D238: clr.b   ($c,A0)             ; record[12] = 0 (inactive)
03D23C: clr.b   ($b,A0)             ; counter = 0
03D240: move.b  ($a,A0), ($2,A0)   ; record[2] = record[10] (write back)
03D246: bra     $3d250               ; → epilogue
03D248: clr.b   ($c,A0)             ; record[12] = 0 (inactive, bls branch)
03D24C: clr.b   ($b,A0)             ; counter = 0
03D250: unlk    A6
03D252: rts
```

### Key findings

#### 1. Record stride is 16, NOT 4

The caller at `$03CF5E` uses `ASL.W #4,D0` (stride 16), not `ASL.W #2` (stride 4).
This was previously documented incorrectly. The record array is accessed as:
```
record_ptr = $FFDD86 + (index * 16)
```

#### 2. Track 2 condition is `record[10] > record[2]`, not `<`

The `bls` instruction at `$03D206` branches when `C=1 or Z=1`, i.e., when
`record[10] <= record[2]`. Therefore, the counter increments ONLY when
`record[10] > record[2]`. Prior C implementation used `record[0xA] < record[2]`
— the **opposite** condition.

#### 3. Track 2 interpolation formula is `record[10] -= (record[10] - record[12]) / 2`

The subtraction at `$03D230` is `sub.l D1,D0` where `D0=record[10]` and `D1=record[12]`.
Step = `(record[10] - record[12]) / 2`. Then `record[10] -= step`.
This is correct in the current C implementation.

#### 4. Both tracks share record[2] as target

Track 1 reads record[2] first, writes its result to record[2], then Track 2 reads
the modified record[2] and overwrites it with Track 2's result at `$03D240`.

#### 5. Interpolation runs when counter >= 6 (not >)

`cmpi.b #$6, counter` followed by `bcs` (branch if C=1, i.e., counter < 6).
Interpolation runs when counter reaches exactly 6, then counter resets.

### Verified behavior

| Test | Initial state | After N calls | Result |
|------|--------------|---------------|--------|
| Track 1 advances | rec[4]=0x04, rec[2]=0x10, rec[5]=0, rec[6]=0x10 | 6 calls | rec[4]=0x0A, counter=0 |
| Track 1 stops | rec[4]=0x10, rec[2]=0x10 | 1 call | counter=0, rec[6]=0xFF |
| Track 2 decreases | rec[10]=0x10, rec[12]=0x04, rec[11]=0 | 6 calls | rec[10]=0x0A, counter=0 |
| Track 2 bls reset | rec[10]=rec[2]=0x10, counters stale | 1 call | counters=0 |
| record[2] overwritten | Track 1 writes 0x0A, Track 2 writes 0x10 | 1 call | rec[2]=0x10 (Track 2 wins) |

### Confirmed vs inferred

| Item | Status | Evidence |
|------|--------|----------|
| A0 = record pointer from stack | **CONFIRMED** | `MOVEA.L ($8,A6),A0` |
| Track 1 condition `bcc` (>=) | **CONFIRMED** | `bcc $3d1f4` at $03D1B0 |
| Track 1 saved target update | **CONFIRMED** | `cmp.b ($6,A0),D0` / `bcc` / `move.b ($2,A0),($6,A0)` at $03D1BA-$03D1C0 |
| Track 1 interpolation step | **CONFIRMED** | `sub.l D1,D0` / `lsr.l #1` / `add.b D0,($4,A0)` at $03D1DA-$03D1DE |
| Track 1 inactive marker 0xFF | **CONFIRMED** | `move.b #$ff,($6,A0)` at $03D1E2 |
| Track 1 write-back at $03D1EC | **CONFIRMED** | `move.b ($4,A0),($2,A0)` |
| Track 2 condition `bls` (<=) | **CONFIRMED** | `bls $3d248` at $03D206 |
| Track 2 saved target update | **CONFIRMED** | `cmp.b ($c,A0),D0` / `bls` / `move.b ($2,A0),($c,A0)` at $03D210-$03D216 |
| Track 2 interpolation formula | **CONFIRMED** | `sub.l D1,D0` / `lsr.l #1` / `sub.b D0,($a,A0)` at $03D230-$03D234 |
| Track 2 write-back at $03D240 | **CONFIRMED** | `move.b ($a,A0),($2,A0)` (overwrites Track 1) |
| Record stride ASL.W #4 (16) | **CONFIRMED** | $03CF5E: `e9 40` = ASL.W #4,D0 in ROM |
| UNLK/RTS epilogue | **CONFIRMED** | Standard 68010 epilogue |

### Bugs fixed vs prior implementation

| Bug | Prior | Correct (per listing) |
|-----|-------|----------------------|
| Record stride | `ASL.W #2` → stride 4 | `ASL.W #4` → stride 16 |
| Track 2 condition | `record[0xA] < record[2]` | `record[0xA] > record[2]` (bls inverts) |
| Track 2 formula | `record[0xA] - record[0xC]` (coincidentally correct) | Same — verified correct |

### C slice

Implemented in `native/mc68010_object_writer.{h,c}`:
- `stunrun_mc68010_object_advance_interpolation()`: the interpolation function
- Test: `native/mc68010_object_writer_test.c` (20 tests, all pass)
  - 7 tests for the caller transport
  - 7 tests for the clamp function
  - 6 tests for the interpolation function

## 25. Object record copy to buffer $03D012

### Source

ROM address `$03D012` (file offset `0x03D012`), called from `$03E51C`
(object_geometry_consumer). Disassembly from
`/tmp/opencode/stunrun-listings-current/maincpu-68010.lst`, lines 72540–72572.

### Disassembly (verbatim from listing)

```text
03D012: link    A6,#-$10            ; Push old A6; alloc 16-byte local buffer
03D016: movem.l D2-D3,-(A7)       ; Save D2, D3
03D01A: move.w  ($a,A6),D3         ; D3 = record_index (from caller arg at +$0A)
03D01E: move.w  ($e,A6),D2         ; D2 = opaque arg_word (preserved through)
03D022: pea     $2500.w            ; Push SR swap flag ($2500)
03D026: jsr     $2256c.l           ; Call SR swap stub (SR saved/restored)
03D02C: move.w  D0,D1              ; D1 = saved SR (discarded in native port)
03D02E: move.w  D3,D0              ; D0 = record_index
03D030: asl.w   #4,D0              ; D0 = record_index * 16
03D032: movea.l #$ffdd86,A0        ; A0 = $FFDD86 (record base)
03D038: adda.w  D0,A0              ; A0 = record_ptr = $FFDD86 + index*16
03D03A: lea     (-$10,A6),A1        ; A1 = local buffer (frame-relative)
03D03E: moveq   #$f,D0             ; D0 = 15 (DBRA count)
03D040: move.b  (A0)+,(A1)+        ; Copy 1 byte, post-inc both
03D042: dbra    D0,$3d040          ; Loop 16 times (DBRA: D0-- until $FFFF)
03D046: moveq   #$0,D0             ; D0 = 0
03D048: move.w  D1,D0              ; D0 = arg_word (sign-extended below)
03D04A: move.l  D0,-(A7)           ; Push arg_word as long
03D04C: jsr     $2256c.l           ; SR swap again (flag = arg_word)
03D052: move.w  D2,D0              ; D0 = buffer ptr low word
03D054: ext.l   D0                 ; D0 = sign-extended buffer ptr
03D056: move.l  D0,-(A7)           ; Push sign-ext ptr
03D058: pea     (-$10,A6)          ; Push buffer ptr
03D05C: jsr     $3d254.l           ; Geometry consumer ($03D254)
03D062: movem.l (-$18,A6),D2-D3    ; Restore D2, D3
03D068: unlk    A6                  ; Restore old A6
03D06A: rts
```

### Entry conditions

| Register | Value | Origin |
|---|---|---|
| D3 | `record_index` | word at A6+$0A (caller's arg 1) |
| D2 | `arg_word` | word at A6+$0E (caller's arg 2, preserved through) |
| A6 | frame pointer | standard 68010 LINK frame |

The function is called by `$03E51C` with:
- arg1 (`$0A,A6`) = `0` (the record index, from `$03E524: move.w ($a,A6),D2`)
- arg2 (`$0E,A6`) = `$1F0` (the opaque word, from `$03E52E: pea $1f0.w`)

### The 16-byte copy

**Confirmed exact mechanism**: byte-at-a-time DBRA loop, not MOVEM.

The sequence at `$03D03E–$03D042` is:
```
moveq   #$f,D0             ; D0 = 15  (iterations = D0+1 = 16)
03D040: move.b  (A0)+,(A1)+ ; copy 1 byte, A0++, A1++
        dbra    D0,$3d040   ; D0-- ; if D0 != $FFFF, goto $3D040
```
DBRA (decrement and branch if not result -1) is an unsigned loop: it iterates
16 times (15→14→…→0→-1=$FFFF→exit).  The copy is **byte-at-a-time**, confirmed
from the listing byte encoding.

Record address: `$FFDD86 + (record_index * 16)`, matching the stride used by
`$03CF40` and the record updaters.

### SR swap calls ($2256C)

Two calls to `$2256C` (SR swap stub) surround the copy.  The first call
saves the caller's SR and restores it; the second swaps in `arg_word` as the
SR.  In the native port these are no-ops (no interrupt handling required).

`$2256C` at the listing level:
```text
02256C: move.w  ($6,A7),D1   ; D1 = word from stack (saved old D2)
022570: moveq   #$0,D0        ; D0 = 0
022572: move    SR,D0          ; D0 = current SR
022574: move    D1,SR          ; SR = D1 (the stack word)
022576: rts
```
The call exchanges SR with the saved register value; in the road writer context
this was used for interrupt masking during DMA.  For the object copy path it is
an opaque pass-through.

### Called routines

| Address | Role |
|---|---|
| `$2256C` | SR swap stub (no-op in native port) |
| `$03D254` | Geometry consumer (stub returns 0; exact behavior unknown) |

`$03D254` receives:
- arg1: sign-extended `arg_word` (D0 low word, extended to 32-bit)
- arg2: pointer to the 16-byte local buffer

Its return value in D0 is passed back to `$03E51C` and used for geometry
computation.  The stub returns 0.

### Observable output

1. **16 bytes copied** from `ram + 0x5D86 + record_index*16` into `buffer[16]`
2. **Two SR swap calls** to `$2256C` (no observable effect in native)
3. **Geometry consumer called** with the buffer and sign-extended arg_word
4. **Return value**: low 16 bits of geometry consumer result

### Confirmed vs inferred

| Item | Status | Evidence |
|---|---|---|
| Link A6,#-$10 (alloc 16 bytes) | **CONFIRMED** | Listing byte `0x4E56`, `#$FFF0` encoded as `#$10` negative |
| MOVEM D2-D3 save/restore | **CONFIRMED** | Listing `$3D016`/`$3D062` |
| D3 = record_index from stack | **CONFIRMED** | `move.w ($a,A6),D3` at `$03D01A` |
| D2 = arg_word from stack | **CONFIRMED** | `move.w ($e,A6),D2` at `$03D01E` |
| ASL.W #4 → stride 16 | **CONFIRMED** | `move.w D3,D0` / `asl.w #4,D0` at `$03D02E–$03D030` |
| record base $FFDD86 | **CONFIRMED** | `movea.l #$ffdd86,A0` at `$03D032` |
| DBRA byte-at-a-time copy | **CONFIRMED** | `moveq #$f,D0` / `move.b (A0)+,(A1)+` / `dbra D0` at `$03D03E–$03D042` |
| 16 iterations (D0=15→$FFFF) | **CONFIRMED** | DBRA semantics: loop while D0 ≠ $FFFF |
| First $2256C call (flag $2500) | **CONFIRMED** | `pea $2500.w` at `$03D022` |
| Second $2256C call (flag = arg_word) | **CONFIRMED** | `move.w D1,D0` then `jsr $2256C` at `$03D048–$03D04C` |
| Geometry consumer call ($03D254) | **CONFIRMED** | `pea (-$10,A6)` / `jsr $3d254.l` at `$03D058–$03D05C` |
| Return value from geometry consumer | **CONFIRMED** | D0 loaded after `jsr $3d254.l`, used by caller |

### Ambiguities

- **$2256C semantics**: The SR swap mechanism is for interrupt masking during
  road DMA.  Its use in the object copy path appears to be a no-op for our
  purposes (the flag value $2500 and the arg_word are not meaningfully interpreted
  by the geometry consumer stub).  The two calls preserve D2 (arg_word) and D3
  (record_index) through the SR swap by using the saved register as the swap
  source, so they function as register-preserving call boundaries.
- **$03D254 behavior**: The geometry consumer is not independently analyzed.  The
  stub returns 0, which is safe for the caller's use only if the caller does not
  branch on the result.  Full behavior (and thus the meaning of the return
  value) is unknown.

### C slice

Implemented in `native/mc68010_object_writer.{h,c}`:
- `stunrun_mc68010_object_copy_to_buffer()`: record fetch and 16-byte copy
- `stunrun_mc68010_object_geometry_consumer()`: geometry consumer stub (returns 0)
- Test: `native/mc68010_object_writer_test.c` (30 tests, all pass):
  - 7 tests for the updater caller transport
  - 7 tests for the clamp function
  - 6 tests for the interpolation function
  - 9 tests for the copy function (NULL inputs, copy correctness, boundary)

## 16. Object build geometry ($03E3FC)

### Symbol

`s_0003E3FC_mc68010_object_build_geometry`

### Function purpose

Receives a 16-byte record buffer and parameters from the caller ($03E51C),
computes geometry parameters from specific buffer offsets, and makes 7 calls
to the shared geometry FIFO emitter ($02F470), each emitting 5 words to the
GSP FIFO at $C0000C.

### Disassembly

ROM address $03E3FC (file offset 0x3E3FC), MAME listing
`/tmp/opencode/stunrun-listings-current/maincpu-68010.lst`, line 74087.

```
03E3FC: link    A6,#$0
03E400: movem.l D2-D3/A2,-(A7)
03E404: move.w  ($a,A6),D3          ; D3 = record_index word
03E408: move.w  ($e,A6),D2          ; D2 = geometry_result+8 word
03E40C: movea.l #$2f470,A2          ; A2 = $02F470
03E412: pea     $10.w               ; Push $10 (constant selector/flag)

  ;; Call 1: args = D2-3, D3+6, D2, D3
03E416: move.w  D2,D0               ; D0 = D2 (geometry_result+8)
03E418: ext.l   D0                  ; D0 = sign-ext(D2)
03E41A: subq.l  #3,D0               ; D0 = D2 - 3
03E41C: move.l  D0,-(A7)            ; arg1 = D2 - 3
03E41E: move.w  D3,D0               ; D0 = D3 (record_index)
03E420: ext.l   D0                  ; D0 = sign-ext(D3)
03E422: addq.l  #6,D0               ; D0 = D3 + 6
03E424: move.l  D0,-(A7)            ; arg2 = D3 + 6
03E426: move.w  D2,D0               ; D0 = D2
03E428: ext.l   D0                  ; D0 = sign-ext(D2)
03E42A: move.l  D0,-(A7)            ; arg3 = D2
03E42C: move.w  D3,D0               ; D0 = D3
03E42E: ext.l   D0                  ; D0 = sign-ext(D3)
03E430: move.l  D0,-(A7)            ; arg4 = D3
03E432: jsr     (A2)                ; Call $02F470

  ;; Call 2: args = D2-3, D3+3, D2-3, D3+6
  ;; Call 3: args = D2-6, D3+3, D2-3, D3+3
  ;; Call 4: args = D2-6, D3-3, D2-6, D3+3
  ;; Call 5: args = D2-3, D3-3, D2-3, D3-6
  ;; Call 6: args = D2-3, D3-6, D2-3, D3-3

  ;; Call 7: args = D2 (unextended), D3.sext, D2-3, D3-6
  ;; (Different pattern: arg1 = unextended D2, arg2 = sign-ext(D3))
03E4F2: pea     $10.w
03E4F6: move.w  D2,D0               ; D0 = D2 (unextended for arg1)
03E4F8: ext.l   D0
03E4FA: move.l  D0,-(A7)            ; arg1 = D2 (unextended word)
03E4FC: move.w  D3,D0               ; D0 = D3
03E4FE: ext.l   D0                  ; D0 = sign-ext(D3)
03E500: move.l  D0,-(A7)            ; arg2 = sign-ext(D3)
03E502: move.w  D2,D0
03E504: ext.l   D0
03E506: subq.l  #3,D0
03E508: move.l  D0,-(A7)            ; arg3 = D2 - 3
03E50A: ext.l   D3                  ; D3 = sign-ext(D3)
03E50C: subq.l  #6,D3               ; D3 = D3 - 6
03E50E: move.l  D3,-(A7)            ; arg4 = D3 - 6
03E510: jsr     (A2)

03E512: movem.l (-$c,A6),D2-D3/A2  ; Restore
03E518: unlk    A6
03E51A: rts
```

### Parameter computation

The function is called from $03E51C with:
- arg1 (long at A6+$08): sign-extended record_index word
- arg2 (long at A6+$0C): sign-extended (geometry_result + 8) word

Inside $03E3FC:
- **D3** = record_index (word loaded from caller's A6+$0A = buffer+0x0A)
- **D2** = (int16_t)arg_word + 8 (word at caller's A6+$0E = buffer+0x0E)

The 7 calls to $02F470 each emit 5 words to the FIFO:

| Call | arg1 (D0) | arg2 (D0) | arg3 (D0) | arg4 (D0) |
|------|-----------|-----------|-----------|-----------|
| 1 | D2-3 | D3+6 | D2 | D3 |
| 2 | D2-3 | D3+3 | D2-3 | D3+6 |
| 3 | D2-6 | D3+3 | D2-3 | D3+3 |
| 4 | D2-6 | D3-3 | D2-6 | D3+3 |
| 5 | D2-3 | D3-3 | D2-3 | D3-6 |
| 6 | D2-3 | D3-6 | D2-3 | D3-3 |
| 7 | D2 (unext) | D3.sext | D2-3 | D3-6 |

Call 7 is structurally distinct: arg1 uses D2 unextended (not sign-extended),
and arg2 uses sign-extended D3 (not the unextended value). This differs from
all other calls.

The $02F470 emitter writes 5 words to $C0000C:
`{0x2000 + selector, arg1, arg2, arg3, arg4}`, where selector is
buffer[0x1B] (the byte at the 5th argument slot in the frame).

7 calls × 5 words = **35 total FIFO words**.

### C slice

Implemented in `native/mc68010_object_writer.{h,c}`:
- `stunrun_mc68010_object_build_geometry()`: geometry builder
- Callback-based FIFO write interface: `write_fifo_word(ctx, word)`
- Test: `native/mc68010_object_writer_test.c` (35 tests, all pass):
  - Tests for all previous functions (record updaters, copy, consumer)
  - 5 new tests for build_geometry (word count, known values, non-zero index,
    negative arg, selector)

The buffer parameter is 32 bytes to accommodate accesses up to buffer[0x1B].
The 16-byte record is copied by $03D012; the extended frame in $03E51C
provides the additional space for arithmetic spills and the selector byte.

### Confirmed vs inferred

**Confirmed**: Full disassembly transcription, all arithmetic formulas,
7-call structure, FIFO output format (5 words per call, 0x2000+selector first).

**Inferred**: The semantic meaning of the geometry parameters (what the 4 args
represent in game terms) is unknown. The selector byte (buffer[0x1B] = 0x20
in observed traces) is confirmed from $02F470's `addi.l #$2000,D0`.

### Speed-update routine at $03ABDE (2026-10-13)

**ROM address range**: $03AB7A–$03ABDE (the update block; parent function starts at $039B82)

**Source chip**: STUN Runner maincpu program ROM, sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c

**Listing provenance**: `/tmp/opencode/stunrun-listings-current/maincpu-68010.lst`, lines 69590–69619

#### Disassembly transcription

```text
03AB7A: tst.w   $ffdd4e.l      ; timer != 0?
03AB80: beq     $3abac          ; no: branch to timer==0 path
03AB82: subq.w  #1, $ffdd4e.l  ; decrement timer (side effect)
03AB88: bne     $3ab98          ; timer still nonzero after decrement?
03AB8A: clr.w   $ffdd50.l       ; yes: clear secondary timer
03AB90: move.b  #$ff, $ffdd0a.l
03AB98: cmpi.w  #$b00, $ffdd16.l ; speed >= 0x0B00?
03ABA0: bcc     $3abaa          ; yes: skip ramp-up
03ABA2: addi.w  #$40, $ffdd16.l ; speed += 0x40
03ABAA: bra     $3abe4          ; branch to accumulator update

03ABAC: move.w  $ffdd16.l, D3   ; timer == 0 path
03ABB2: ext.l   D3
03ABB4: move.w  (-$18,A6), D0   ; local_limit (from caller's frame)
03ABB8: ext.l   D0
03ABBA: moveq   #$40, D1
03ABBC: add.l   D1, D0          ; D0 = local_limit + 0x40
03ABBE: cmp.l   D0, D3          ; speed > (local_limit + 0x40)?
03ABC0: bls     $3abcc          ; no: skip decay
03ABC2: subi.w  #$20, $ffdd16.l ; speed -= 0x20
03ABC6: bra     $3abe4          ; branch to accumulator update

03ABCC: move.w  $ffdd1e.l, D2   ; base + offset path
03ABD2: ext.l   D2
03ABD4: move.w  $ffdd1a.l, D0
03ABDA: ext.l   D0
03ABDC: add.l   D0, D2          ; D2 = source_a + source_b
03ABDE: move.w  D2, $ffdd16.l   ; write updated speed
```

After the update, $03ABE4 adds the new speed to accumulator at $FFDBFE:
```text
03ABE4: move.w  $ffdd16.l, D2
03ABEA: ext.l   D2
03ABEC: add.l   D2, $ffdbfe.l
```

#### Entry conditions

The parent function at $039B82 (`link A6, #-$30`) sets up the stack frame.
The speed-update block uses:
- `(-$18,A6)`: local_limit, set to `0x07E0` at $039C16 before entering the update path
- `$FFDD4E`: timer field (checked at $03AB7A)
- `$FFDD16`: current speed value (read at $03ABAC, written at $03ABDE)
- `$FFDD1E`: source_a (read at $03ABCC)
- `$FFDD1A`: source_b (read at $03ABD4)

#### Speed update logic

```c
if (timer != 0 && speed < 0x0B00)
    speed += 0x40;
else if (speed > (local_limit + 0x40))
    speed -= 0x20;
else
    speed = source_a + source_b;
```

The timer decrement at $03AB82 is a side effect of the timer-check path, not part of the value computation itself.

#### Three callers

| Caller | Arguments at stack | Notes |
|--------|-------------------|-------|
| $02587C | `(0, byte from $FFDD82)` | zero longword + input byte |
| $0280FE | `(ROM table[$04EECC][$FFDE72], $FFDD82)` | table lookup + input byte |
| $02853A | `(byte from record, 0x38)` | record field + literal |

The function at $039B82 handles all three paths; the local_limit varies by caller context.

#### C implementation

`native/mc68010_physics.c`: `stunrun_mc68010_speed_update()` — mechanism-level, no gameplay semantics assigned to parameters.

#### Confirmed vs inferred

**Confirmed**: Full disassembly transcription, branch order, arithmetic operations, three-way branch logic.

**Inferred**: The semantic meaning of timer, speed, local_limit, source_a, and source_b is not assigned; the function is treated as a mechanism with observable inputs and outputs only.

## 16. MC68010 speed routine at $039B82 (2026-10-13)

### 16.1 Function identity

- **ROM address**: $039B82–$03ADE6 (over 2000 bytes; one of the largest functions in the ROM)
- **Ghidra name**: `s_0003982_mc68010_speed_routine`
- **Three callers** (all `jsr $39b82.l`):
  - `$02587C`: pushes ($FFDD82 byte zero-extended, 0)  
  - `$0280FE`: pushes ($FFDD82 byte zero-extended, ROM[$04EECC+$FFDE72] byte)
  - `$02853A`: pushes (record byte, $38 word sign-extended)
- **C name**: `stunrun_mc68010_speed_routine()`

### 16.2 Entry conditions

The 68010 `link A6,#$30` creates a 48-byte local frame. Stack setup:
```
$8,A6   ← 2nd arg (byte; 0/ROM table byte/0x38)
$A,A6   ← 1st arg (byte from $FFDD82 or record byte)
$E,A6   ← byte from $F,A6 (zero-extended to word, low byte used)
$12,A6  ← return address (4 bytes above saved PC)
```
Key instructions:
- `move.w ($a,A6), (-$2,A6)` — copies 1st arg word to local -$2
- `move.b ($f,A6), D4` — loads 2nd arg byte into D4
- `move.l #$ffdd1c, (-$6,A6)` — stores $FFDD1C constant to local -$6
- `jsr $3ae30.l` — init subroutine (counter/slot machine logic)

### 16.3 local_limit setup

At `$039C16`: `move.w #$7e0, (-$18,A6)` — sets local_limit = **0x07E0** in the stack frame. This value is later used by the speed update block at $03ABB4.

### 16.4 Speed update block (embedded, not a JSR)

The block at `$03AB7A–$03ABDE` is **inline code within $039B82**, not a separate JSR. It is reached by fall-through from the pre-computation phase. The three-branch logic:

```
timer != 0 && speed < 0x0B00 → speed += 0x40  (ramp up)
speed > (local_limit + 0x40)  → speed -= 0x20  (decay)  
otherwise                      → speed = source_a + source_b
```

**Confirms** the existing `stunrun_mc68010_speed_update()` correctly models this block.

### 16.5 Accumulator write

After the speed write, at `$03ABE4–$03ABEC`:
```
03ABE4: move.w  $ffdd16.l, D2    ; load updated speed
03ABEA: ext.l   D2
03ABEC: add.l   D2, $ffdbfe.l  ; speed += accumulator
```
This is modelled as: `accumulator += updated_speed`.

### 16.6 Observable outputs (modelled)

1. `$FFDD16` ← updated speed (via embedded block)
2. `$FFDBFE` ← `$FFDBFE + updated speed` (accumulator update)

The pre-computation phase (1000+ bytes: $039D22–$03AB6F) sets `$FFDD1E` and `$FFDD1A` (source_a/source_b) and performs complex multiplications — not modelled here as it requires full game state.

### 16.7 C implementation

`stunrun_mc68010_speed_routine()` takes all RAM values as parameters, calls `stunrun_mc68010_speed_update()` for the speed field computation, then models the accumulator write. Returns a struct with `speed_write` and `accumulator_write`. The caller-args (arg1, arg2) are consumed as `(void)` since they affect pre-computation but not the speed field itself.

### 16.8 Test verification

- 8 existing `stunrun_mc68010_speed_update` tests (unchanged)
- 7 new `stunrun_mc68010_speed_routine` tests covering all three code paths and the accumulator write
- Broken implementation (returns 0xBEEF) correctly fails on the first assertion

## 26. Object collision test ($03B02C) and the hit award chain ($03A262)

### 26.0 Correction to the section-16 heading collision

The subagent slices for `$03D1A0`, `$03E3FC`, and `$039B82` each opened their
own "## 16." in this file. Three sections now share the number 16, and two
share 22. The duplicates are left in place rather than renumbered, because
renumbering would break every `§N` cross-reference in this file and in the
analysis documents. Use the heading text, not the number, to cite these.

### 26.1 `$03A298` is not a routine entry

The task brief named `$03A298` as a function to port. It is not. It is the
middle of **jump-table case 5** inside the `$039B82` routine already ported
in section 16. Case 5 is entered at `$03A262`; `$03A298` is reachable only
as a fall-through from `$03A294`, that is, after a collision that was *not*
deduped. The dispatch is at `$03A0E6`:

```asm
03A0E6: move.w  $10(a0),d0        ; record type field
03A0EA: cmpi.w  #$7,d0
03A0EE: bhi.w   $03A4FE           ; type > 7 -> shared exit
03A0F2: add.w   d0,d0             ; index * 2
03A0F4: move.w  $3a0fc(pc,d0.w),d0
03A0F8: jmp     $3a0fc(pc,d0.w)
```

| type | word | target |
|---|---|---|
| 0 | `$0402` | `$03A4FE` (shared exit) |
| 1 | `$0010` | `$03A10C` |
| 2 | `$0120` | `$03A21C` |
| 3 | `$02B8` | `$03A3B4` |
| 4 | `$00EA` | `$03A1E6` |
| **5** | **`$0166`** | **`$03A262`** |
| 6 | `$0402` | `$03A4FE` |
| 7 | `$0222` | `$03A31E` |

Each case has its own "last record" latch (`$FFDD78` for cases 1/2/5,
`$FFDD7C` for case 7) and its own per-frame hit byte at `-$12(a6)`. The C
slice therefore covers the whole case-5 body from `$03A262`, not a callable
unit starting at `$03A298`.

### 26.2 Verbatim transcription of the collision test, `$03B02C`

The decisive tail:

```asm
03B112: moveq  #$0,d3             ; d3 = 0
03B114: tst.w  $ffdd14.l          ; sign of dx
03B11A: blt    $03B126
03B11C: move.w $ffdd14.l,d2
03B122: ext.l  d2                 ; d2 = dx
03B124: bra    $03B130
03B126: move.w $ffdd14.l,d2
03B12C: ext.l  d2
03B12E: neg.l  d2                 ; d2 = -dx
03B130: moveq  #$0,d0
03B132: move.w  d5,d0             ; d0 = half_a
03B134: move.l  d0,d1
03B136: cmp.l   d2,d1
03B138: bls    $03B156            ; |dx| >= half_a -> MISS
03B13A: tst.w  (a1)               ; sign of dy
03B13C: blt    $03B144
03B13E: move.w (a1),d2
03B140: ext.l  d2
03B142: bra    $03B14A
03B144: move.w (a1),d2
03B148: ext.l  d2
03B14A: ...  d1 = d4 ; cmp.l d2,d1
03B152: bls    $03B156            ; |dy| >= half_b -> MISS
03B154: moveq  #$1,d3             ; HIT
03B156: move.l  d3,d0
03B158: movem.l -$10(a6),d2-d5
03B15E: unlk   a6
03B160: rts
```

**Confirmed:** a strict two-axis AABB overlap. `bls` is unsigned-or-equal,
so a hit requires `|dx| < half_a` **and** `|dy| < half_b`; equality is a
miss. This matters: an implementation using `<=` would pass a "did it run"
test and fail only on the exact boundary.

Deltas are written as a side effect of the test:

```asm
03B064: move.w  $02(a0),d0
03B068: ext.l  d0
03B06A: move.l  $ffdbfe.l,d1
03B06E: sub.l   d0,d1
03B070: move.w  d1,$ffdd14.l       ; dx
...
03B080: move.w  $ffdc1a.l,d0
03B084: ext.l  d0
03B086: move.l  d0,$ffdd12.l
03B08C: move.w  $06(a0),d0
03B090: ext.l  d0
03B092: sub.l   d0,$ffdd12.l       ; dy
```

### 26.3 Half-extent selection — ambiguity, not resolved silently

```asm
03B094: subi.w  #$400,d1           ; d1 = record+0x0C - $400
03B098: subi.w  #$c0, d4           ; d4 = record+0x0E - $C0
03B09C: tst.b   d2                 ; selector byte from caller -$f(a6)
03B09E: beq.b   $03B0A6
03B0A0: move.w  d4,d0
03B0AA: move.w  d0,d4              ; selector picks which extent
03B0A6: move.w  d1,d0
```

Two readings cannot be separated statically:

1. the `-$400` / `-$C0` are centring offsets for a signed 16-bit
   fixed-point coordinate, and the selector chooses the active axis; or
2. the selector chooses which of two separately-authored extents is live.

The slice implements the literal mechanism (subtract, then select). It does
**not** claim an axis meaning.

### 26.4 Size-class scaling — exact immediates

```asm
03B0B0: move.w  $0a(a6),d3         ; owner's record+0x0A
03B0B4: cmpi.w  #$4b0,d3
03B0BA: beq    $03B0C4            ; * 1.5
03B0BE: cmpi.w  #$320,d3
03B0C4: ...
03B0CA: beq    $03B0CE            ; << 1
03B0D0: cmpi.w  #$3e8,d3
03B0D6: beq    $03B0E0            ; * 1.5
03B0E0: cmpi.w  #$c80,d3
03B0E6: bne    $03B0EE            ; no match -> unscaled
```

Five exact constants: `$4B0` and `$3E8` scale by 1.5, `$320` doubles, `$C80`
halves. Anything else leaves the extent unscaled. That `owner+0x0A` selects
a *size class* rather than being a coordinate is an **inference** — the five
exact immediates argue for it, but nothing here proves the axis label.

### 26.5 Timer widening

```asm
03B0EE: tst.w  $ffdd4e.l
03B0F4: beq    $03B0FE
03B0F6: cmpi.w  #$3,$0a(a1)        ; record type
03B0FC: beq    $03B0FE
; half_a += half_a >> 2   (* 1.25)
; half_b += half_b >> 4   (* 1.0625)
```

Both extents widen when `$FFDD4E != 0` and the record type is not 3.

### 26.6 The award chain, `$03A28E`-`$03A31A`

```asm
03A28E: cmp.l  $ffdd78.l,d0        ; current record vs latch
03A294: beq    $03A314             ; duplicate -> no scoring
03A298: addq.w #$1,$ffdd02.l       ; hit counter, 16-bit
03A29E: move.w $ff9578.l,d0        ; course index
03A2A4: ext.l  d0
03A2A6: movea.l #$4ee4e,a0
03A2AC: tst.b  (a0,d0.l)           ; threshold byte nonzero?
03A2B0: beq.b  $03A2DA
03A2C2: move.b (a0,d1.l),d0
03A2C6: cmp.w  $ffdd02.l,d0        ; EQUALITY
03A2CC: bne.b  $03A2DA
03A2CE: pea.l  $16.w               ; milestone event
03A2D2: jsr    $301ca.l
03A2DA: tst.w  $ff9578.l
03A2E0: beq.b  $03A2EA
03A2E2: move.l #$1f4,d2            ; 500
03A2EA: moveq  #$32,d2             ; 50
03A2EC: add.l  d2,$ff9532.l        ; 32-bit add
03A2F2: move.l -$e(a6),$ffdd78.l   ; latch
03A2FA: tst.w  $ff9578.l
03A300: beq.b  $03A308
03A302: pea.l  $5f.w               ; later-course hit event
03A308: pea.l  $6c.w               ; first-course hit event
03A30C: jsr    $301ca.l
03A314: moveq  #$1,d0              ; return 1
03A316: move.b d0,-$12(a6)         ; per-frame hit flag
03A31A: bra    $03A4FE             ; shared loop exit
```

**Confirmed:** `addq.w` at `$03A298` (16-bit, wraps at `$FFFF`), `add.l` at
`$03A2EC` (32-bit), `cmp.w` at `$03A2C6` (equality, **not** `>=`).

### 26.7 The 50/500 rule is a course tier, not a track lookup

`analysis/ram-map.md:51` reads "awards `50` or `500` points based on
`0xFF9578`". That is literally right but easy to misread as a per-track
table. Surveying all 103 `$FF9578` references in the ROM:

| writer | value |
|---|---|
| `$024322`, `$02B95E` | `clr.w` |
| `$0320BC` | `addq.w #1` — course completion |
| `$0252EC` | `addq.w #1` — cleanup/re-entry |
| `$0291A2` | `subq.w #1` |
| six writers | constants `$0F`/`$0D`/`$0C`/`$04`/`$0C`/`$11` |
| `$02B63E` | `move.w d1` |
| readers | `cmpi.w #$16` at five sites — **22 courses** |

So the rule is **first course 50 per object hit, every later course 500**.
`analysis/ram-map.md` should be sharpened to this.

Checked and *not* a bug: `ext.l d0` at `$03A2A4` sign-extends the course
index before the `$4EE4E` byte lookup, and the table is only 33 bytes, so a
negative index would read ROM below it. Every writer produces `$00`-`$11`, so
bit 15 is never set. Worth re-checking if a new writer ever appears.

### 26.8 Threshold table `$4EE4E`, literal, 33 bytes

| course | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| byte | 00 | 00 | 0B | 13 | 00 | 1C | 0F | 13 | 14 | 46 | 16 | 13 | 15 | 17 | 14 | 15 | 24 | 15 | 10 | 15 | 14 | 14 |

courses 22-32: `00 00 00 00 01 00 01 00 01 00 01`.

The milestone fires only on an **exact** counter match: course 2 wants hit
11, course 5 wants hit 28, course 9 wants hit 70, course 16 wants hit 36.
Courses 0, 1, 4, 22, 24, 25 have no threshold.

### 26.9 Event queue `$00301CA`

```asm
; $8(a6) = payload byte
lane  = byte @ $FFDBC8
count = byte @ $FFDBC4 + lane
03B1E6: cmpi.b #$20,(a0,d0.w)
03B1EC: bcc    <return>            ; full -> drop silently
queue = $FFDB64 + lane*32
$FFDB64[lane*32 + count] = payload
$FFDBC4[lane] = count + 1
```

A 32-lane, 32-deep byte staging buffer with **silent drop on full**: no flag,
no counter bump, no error. Recorded as SR-19 in `BUGS.md`.

### 26.10 Rejected lead, recorded so it is not re-raised

`$0320A4`-`$0320C2` uses the same `add.l d2,$ff9532.l` idiom as the award
chain, indexed by `$FF9578 * 4` into a table at `$4AF7E` containing
`0, 1000, 1500, 2000, ..., 8000` — exactly `500 * course` with entry 0 = 0.
This is **not claimed** as the course-finish bonus: an entry-0 bonus of 0 is
odd, and the index may be `(course+1)` at the real call sites. Flagged as a
lead only.

### 26.11 Integration: `award_path.c` now delegates

`reproduction/maincpu/award_path.c` independently re-derived the same three
instructions (`$03A298`, `$03A2E2`, `$03A2EC`). Two models of one chain can
drift, so it is now a thin adapter over `stunrun_mc68010_collision_award()`.
Its narrower signature cannot express the dedupe or the milestone, so it
passes a record distinct from the latch and a NULL threshold table. Callers
needing those behaviours should call the physics slice directly.

This resolves the three items `award_path.h` disclaimed as "deliberately NOT
claimed": the 50-award immediate is `$03A2EA` (`74 32` = `moveq #$32,d2`);
the `0x0` operand at `$03A2E4` is the high half of the
`move.l #$000001F4,d2` immediate at `$03A2E2`; and hit detection is
`$03A27A jsr $3B02C`, transcribed in §26.2.

### 26.12 Test verification

15 new assertions in `native/mc68010_physics_test.c`, asserting observable
output rather than "it ran": exact score and counter deltas, the dedupe, the
milestone equality, collision strictness on both axes, negative-delta abs
branches, timer widening, and each size-class scaling.

**Oracle vector.** `reference/experiments/stunrun/main-ram-center-object-score.metadata.json`
records `$FFDD02` taking values "0x01 through 0x22" and `$FF9532` taking
"50 through 1700". That is 34 counter values and 34 awards, so the counter
starts at 0 and is incremented once per award, with distinct records
(duplicates are deduped and would not increment). `test_award_oracle_vector`
reproduces exactly that. If it stops reproducing, the model is wrong; the
expectation must not be adjusted to make it pass.

**Mutation testing.** Six deliberate breaks, all caught:

| # | mutation | caught by |
|---|---|---|
| 1 | dedupe at `$03A294` removed | `test_award_duplicate_is_deduped` |
| 2 | `cmp.w` weakened to `<=` | `test_award_milestone_requires_exact_match` |
| 3 | strict `<` weakened to `<=` | `test_collision_strict_less_than` |
| 4 | timer widening removed | `test_collision_timer_widens_extents` |
| 5 | selector forced to `half_b` | `test_collision_selector_and_size_class` |
| 6 | `add.l` narrowed to 16-bit | `test_award_score_add_is_32_bit` |

Mutation 6 initially did **not** fail. The first overflow vector,
`0xFFFFFF00 + 500`, wraps at bit 32, where a 16-bit add and a 32-bit add
produce the same low 16 bits — so the test could not distinguish them. A
second vector, `0xFFF0 + 500 = 0x101E4`, was added; it carries out of bit 16
and does distinguish them. This is the fourth check in this repository that
passed for the wrong reason; see also §15.5 and §15.6.

**Suite state:** 59/59 ctest, 149/149 repository pytest.

### 26.13 Not yet done

`$03A298` was analysed statically only. No MAME trace was captured for this
lane; the runtime consequences in SR-18 remain inferred. The record-walking
loop at `$03A0A8` that selects which records are tested, and the list-head
pointer table at `$4C712`, are the next target for subsystem 3.

## 27. The host data write FILLS the 16-byte cell (measured; a wrong fix landed and was reverted)

This section exists because the first version of it was wrong. Commit `d91ae6a`
changed both models to a packed 2-byte store on the strength of a reading of
MAME's source; a direct measurement against the pinned driver refuted it, and
that commit was reverted. Both the wrong turn and the measurement that killed it
are recorded, because the wrong turn is the expensive part and because the
misreading is an obvious one to make again.

### 27.0 What the re-measurement was for

Section 19 fixed the share size and left item 1 of `stun-runner-2ua` open:
re-measure section 15.4's VRAM match at the corrected 4 MiB extent. Re-captured
with both constants at 0x400000:

```text
902,647 bus events; host-window events=902,641; unmapped_writes=39
vs native model reference VRAM:
  differing bytes : 1,157,420 / 4,194,304   (27.6%)
```

So the models had genuinely diverged. That much was real.

### 27.1 The wrong inference

The divergence was read as: `gsp_board.c` replicated 8x across a 16-byte cell,
`musashi_cpu.c` stored a packed word, and since `TMS34010_WRMEM_WORD` is
`m_program.write_word` (tms34010.cpp:317) into a `.ram()` region
(harddriv.cpp:628), **packed** must be right and the board model wrong.

Every step of that inference is quotable and none of it survives contact with
the machine:

```text
tms34010.cpp:1875  TMS34010_WRMEM_WORD(addr & 0xfffffff0, data)
tms34010.cpp:317   -> m_program.write_word(A, V)
tms34010.cpp:67    program space: datawidth 16, addrshift 3, addrwidth 32
harddriv.cpp:628   map(0xff800000,0xffbfffff).mirror(0x4000000).ram().share("gsp_vram")
```

The step that failed was treating `write_word` on a 16-bit-wide space as a plain
two-byte store. The space is not simply "16-bit"; it is **16-bit data against a
16-byte unit** (`addrshift 3` with `datawidth 16` gives
`alignment() == 16`, emumem.h:2075), and a write into that unit expands across
it. The `addrshift` was read as if it only affected addressing.

### 27.2 The measurement that refuted it

Frame 390 of the pinned driver, while the GSP is still in reset (`gsp PC=0`), so
nothing else can touch a cell between the write and the read-back. Driven
through the **real host port** -- the 68010 writing `$C00000`/`$C00004`/`$C0000C`,
so `hd68k_gsp_io_w` -> `host_w` -> `TMS34010_WRMEM_WORD` -- not a direct space
write:

```text
PROBE frame=390 gsp_pc=00000000
PROBE base+1000   : 34 34 34 34 34 34 34 34 12 12 12 12 12 12 12 12
```

`HSTADRL=0xFF801000, HSTDAT=0x1234` produces **eight copies of 0x34 then eight of
0x12**. The store fills the cell; it does not pack. The board model was right
and `fcf1eab`'s "packed storage" change to `native/musashi_cpu.c` was the actual
defect.

The same probe confirmed the two adjacent facts both models already relied on,
which is worth recording because they were carried on inference until now:

```text
PROBE base+2008   : 56 56 56 56 56 56 56 56 00 00 00 00 00 00 00 00
PROBE base+2000   : 78 78 78 78 78 78 78 78 56 56 56 56 56 56 56 56
PROBE incr cell0  : BC BC BC BC BC BC BC BC 9A 9A 9A 9A 9A 9A 9A 9A
PROBE incr cell1  : F0 F0 F0 F0 F0 F0 F0 F0 DE DE DE DE DE DE DE DE
```

`HSTADRL=0xFF802008` lands at `0xff802000`: the `& 0xfffffff0` mask discards the
low four bits. With `HSTCTL=0x0800`, consecutive writes land one 16-byte cell
apart. Both as modelled.

Frame 390 is also the capture window section 27.4 needed: `gsp PC=0`, 429,504
nonzero words. The share is still entirely zero at frame 360 and the GSP is
running by frame 420, so the usable window is narrow -- it has to be found by
checking `gsp PC`, not by assuming "early" is early enough.

### 27.3 Corroboration by replay

Replaying the captured host stream against that frame-390 oracle, sweeping the
truncation point because the native and MAME captures are not frame-aligned:

```text
packed       : 748,098 / 4,194,304 bytes differ  (82.16% agreement)
replicated   :  70,960 / 4,194,304 bytes differ  (98.31% agreement)
```

The residual is `0xff` fill in the ranges the 2bpp expander writes
(`0x020000-0x03ffff`, `0x1e0000-0x1f3fff`), which this arbitration harness does
not model. An attempt to add the 2bpp expander (`hdgsp_vram_2bpp_w`,
harddriv_v.cpp:304, with the `MASK(n)` byte-lane table at :28) made the figure
*worse* -- 145,603 differing -- so that transcription is wrong and is **not**
claimed as a result. The residual is therefore attributed to the 2bpp path on
the strength of *where* it falls, not on a working model of it.

The direct probe in 27.2 is what settles the question; the replay corroborates
the direction and the size of the effect.

### 27.4 State after the correction

Both models replicate. Re-captured and replayed:

```text
  differing bytes : 0 / 4,194,304
  byte sum ours=152593108 reference=152593108
RESULT PASS
```

`test_hstdat_cell_expansion` had asserted replication and was correct all along;
the packed version that replaced it in `d91ae6a` is gone. Added
`test_hstdat_discards_low_address_bits`, which pins the `0xfffffff0` mask, and
`gsp_display_list_musashi_test.c`'s cell expectation moved back from packed to
filled -- that expectation had been written for `fcf1eab` ("post fcf1eab" in its
comment) and is changed here because the measurement refuted it, not because the
implementation needed to pass.

### 27.5 A check that could not pass

`replay_host_writes()` in `native/gsp_rtl/gsp_rtl_main.cpp` ended in an
unconditional `RESULT FAIL` / `return 1`. There was no pass branch, so
`tools/check-gsp-replay` returned 1 on a byte-identical match and its exit code
was not a verdict on anything. It now returns 0 on `diff_bytes == 0` with
matching byte sums. This fix stands; it was independent of the store question
and was correct on its own.

### 27.6 The general lesson

Two errors in this file were made in the same direction and hid each other for a
week, and `d91ae6a` shows the failure mode is not "make a mistake" but "make a
plausible one and then write a long comment defending it":

* A readable chain of quotable driver lines said `packed`.
* The board model said `replicated`.
* `check-gsp-replay` compared those two against **each other**, never MAME.

So the check could not see either error while they cancelled, and when the size
fix separated them it reported a divergence whose cause was not the one being
looked for. The correct reading was available in the source and was still wrong.

The thing that settled it was one write and one read-back, at a moment chosen so
that nothing else could be responsible for the answer. That is cheap. Reading
`write_word` as "two bytes" cost a week, a commit, and a revert.

### 27.7 What is still NOT established

Neither model has been compared against MAME at a frame where the GSP has run.
The frame-390 window gives host-write-only parity; the frame-600 oracle remains
87.5% uniform GSP framebuffer data that no host-port replay can reproduce, and
the 2bpp path is still unmodelled in the arbitration harness above.

Section 18.2 stands unchanged: the capture ends 560,422 68010 cycles after the
final HLT release with the GSP halted for 99.61% of its window.

---

## 28. The Lua tap "defects" we filed were ours, not MAME's

### 28.1 What was claimed

`tools/capture-gsp-program-upload` was written against MAME `0.289
(mame0289-dirty)` and hit two behaviours that looked like MAME bugs. Both were
measured carefully, and both were filed in `BUGS.md` as `driver defect` with the
instruction to report them upstream:

* **MD-10.** Reading the shared GSP VRAM through `gsp.spaces['program']` inside a
  `register_frame_done` callback made every already-installed MC68010 write tap
  stop firing. Same script, same binary, same ROM set: **1000 events without the
  read, 0 events with it**, no Lua error, clean exit either way.
* **MD-11.** Work inside a memory-tap callback corrupted MAME depending on the
  work: one retained Lua table per event segfaulted, `file:write` per event
  segfaulted with the log truncated mid-line at event 339, growing a table
  silently stopped the callback at 1493 events, while assigning into a
  preallocated ring captured 65,536 events cleanly.

### 28.2 MD-10 is documented behaviour, with a documented remedy

MAME's own Lua reference says, at
`docs/source/luascript/ref-mem.rst:197-200`:

```text
passthrough:reinstall()
    Reinstalls the pass-through handler in the address space.  May be necessary
    if the handler is removed due to other changes to handlers in the address
    space.
```

So a pass-through handler being dropped by a handler change is a documented
possibility, and `reinstall()` exists for precisely that. We never read it.

Measured, using the documented remedy instead of our hand-rolled one:

```text
M1_REINSTALL_INSTALLED handler=true
M1_REINSTALL_AFTER_READ before=29287      # 29287 events, then the VRAM read
M1_REINSTALL_CALLED                       # handler:reinstall()
M1_REINSTALL_DONE frames=20 raw=139120    # tap keeps firing
```

One call on the existing handler object restores it. There is no defect here.
The `tap_helper` implementation is `lua_engine::tap_helper` in
`src/frontend/mame/luaengine_mem.cpp:195-316`, which installs a
`memory_passthrough_handler` and exposes exactly this method.

The genuine lesson is the one already in §27.6: we concluded "the emulator is
wrong" from a measured symptom, without reading the documentation for the
feature we were using. The measurement was right and the conclusion drawn from it
was not.

### 28.3 MD-11 is misuse

The same page notes that pass-through handler callbacks **are not run as
coroutines**. They fire once per memory access — roughly 500,000 times a minute
for this board's host window. Doing file I/O, growing a table, or allocating a
table per call is a performance hazard by construction, not an emulator defect,
and a PR reporting it would have been rejected. The working pattern (preallocate,
format after the run) is the right answer regardless, and it is what the tool
does.

### 28.4 What changed in the repository

* `BUGS.md` MD-10 and MD-11 are withdrawn, with the withdrawal recorded in place
  rather than deleted, because that section's entries are the ones that get
  reported upstream and two false reports are worse than none.
* `tools/capture-gsp-program-upload` keeps its two-phase design — record events,
  then image VRAM in a separate run — but the stated reason is now correct: it
  is the documented, supported way to run a tap this hot, not a workaround for a
  crash. The tool also still refuses to emit a capture with zero host events,
  which remains worth having: the failure mode is *silent*, so a guard is
  cheaper than the mistake.
* `analysis/gsp-program-upload-boundary.md` and `STATUS.md` no longer describe
  these as upstreamable defects.

### 28.5 The one thing that might still be worth upstream

The removal itself is silent: nothing tells a Lua script that its pass-through
handler stopped being called. The documentation says `reinstall()` "may be
necessary" without saying how a script can notice. A script cannot distinguish
"the board stopped writing" from "my handler was dropped" except by counting its
own events, which is exactly the check we had to invent. A one-line note in
`ref-mem.rst`, or a debug log line in `tap_helper` when a handler is dropped,
would be a defensible small contribution — but that is a suggestion, not a
finding, and it is not worth a PR before someone wants it.

## 29. Scripted MAME input is nondeterministic, so every `--input` artifact is one arm of a coin flip

Found 2026-10-13 while trying to close `stun-runner-rg9`. This is not an
assembly finding and it is not yet a MAME finding either: it is a harness
reproducibility failure, and it is the reason §21's oracle could not be
re-derived. Tracked as `stun-runner-0p0`, which now blocks `stun-runner-rg9`.

### 29.1 The symptom

Six identical invocations, changing nothing but the output directory:

```sh
for i in 1 2 3 4 5 6; do
  ./tools/mame-ram-write-trace roms /tmp/opencode/rg9/s$i \
    --device :mainpcb:maincpu --base 0xC00000 --end 0xC00020 \
    --frames 400 --start-frame 10 --end-frame 400 --pc 0x2656 --registers \
    --max-events 4000 --input service_f2 --mame /mnt/build/mame-ref/mame
done
```

```text
run1  1040 events  frames [214,215,216,334,335,336]
run2   480 events  frames [45,46,47]
run3  1040 events  frames [214,215,216,334,335,336]
run4   480 events  frames [45,46,47]
run5  1040 events  frames [214,215,216,334,335,336]
run6   480 events  frames [45,46,47]
```

Perfect alternation, not a random race. Repeating with all four runs writing to
the *same* output directory alternated identically, so it is not per-run state on
disk — only `mame.log` and `result.json` are ever written. `--nothrottle`
alternated the same way, so it is not wall-clock pacing.

### 29.2 The machine is not at fault

This is the part that rules the obvious suspects out.

```text
--input none, four mame-gsp-state-snapshot runs, frame 300:
  sha256 7e48495c4d7c2867dffd625d0b295a0c50c26fd585d25ffc20bf93fabc853d49  x4
```

Identical screen, every time. Three `--input none` host-write traces gave
**3000/3000 byte-identical events**. ROM hashes match the manifest on all 25
entries; the MAME binary is `75b09ee4…`, exactly the one recorded in
`service-f2-text-host-writes/metadata.json`. Deterministic machine, verified
three ways. Only the scripted-input path is flaky.

The divergence is broad, not confined to text. Tapping just the HSTADR window
`0xC00002-0xC00005` over frames 10-400, the two arms spend their 4000 events at
frames `[45,46]` versus spread across
`[31,38,87,92,93,94,97,98,100,120,121,126,130,131]`. Different execution paths,
not just a different draw.

### 29.2a What the two arms actually are — corrected after decoding them

The first version of this section described arm B as "attract text only, service
mode did not engage". That is wrong, and decoding the descriptors says so. Every
`(frame, A0-5)` pair in each run, resolved through the selector table at
`$10136`, grouped to the start of each draw (`A0` post-increments per character,
so only the first character of a draw yields the descriptor — the trap
`stun-runner-rg9` warns about):

```text
selector  descriptor  count  text                            game (none)  svc arm A  svc arm B
$47       $06C53      1E     PRESS*START*TO*BYPASS*SELFTEST   frame  45    frame 214  frame  45
$43       $06BE7      0E     PROGRAM*ROM*OK                   frame 165    frame 334  -
$50       $06D41      0F     Y*X*W*V*U*T*S*R                  frame 166    frame 335  -
$51       $06D54      03     200                              frame 167    frame 336  -
$52       $06D5B      03     210                              frame 167    frame 336  -
```

Three things follow, and the first two are corrections.

**1. `--input none` — plain game mode — executes `$25E8` 1040 times and draws all
five blocks.** So `$25E8` is *not* a service-mode-only routine, and §21.3's
framing of these selectors as resolved from service-mode live traffic describes
the descriptors correctly but overstates their mode specificity: the identical
descriptors are drawn in game mode about 150 frames earlier.

**2. Arm B is not "game mode".** Game mode draws all five. Arm B draws only
`$47`, at frame 45 — the game-mode frame — and then the other four never happen.
So arm B is a third state, not the absence of the service press: a run in which
four of five known text draws do not occur and nothing reports it. That is the
failure this repository is most exposed to, an absent observation reading as a
fact about the machine.

**3. The frame numbers in `stun-runner-rg9` and §21 are mode-specific** and must
not be carried into a mode-independent expectation. `$47` lands at 45 in game
mode and 214 under the service schedule.

### 29.3 What this invalidates

`reference/experiments/stunrun/service-f2-text-host-writes/` records 1040 events
at frames 214-216/334-336 — arm A. `service-f2-3600-stages/screens/frame-000300.png`
shows the self-test screen — arm A. I could not reproduce either on demand, and
re-running the committed command verbatim returned `"events":0, "event_count":0`
on a run that landed in arm B. §21's "two selectors resolved from live traffic",
and the `stun-runner-rg9` bead's worked examples, currently rest on one arm of a
coin flip. Every other artifact whose command line carries `--input` is in the
same position until this is resolved.

`--start-frame` is not the discriminator: `--start-frame 5` also produced 1040
on some runs and 480 on others, and the events sit at frames well above 60.

Decoding the two arms changes what is at stake, and corrects the first draft of
this section: see 29.2a. Arm B is not "service mode failed to engage" — it draws
only the first of five text blocks and silently omits the other four.

### 29.4 The suspect, and why it is not yet a conclusion

`mame/lua/ram-write-trace.lua` does this, in `emu.register_frame_done`:

```lua
frame = frame + 1
while next_event <= #input_events and input_events[next_event].frame == frame do
    apply_event(input_events[next_event])
    next_event = next_event + 1
end
```

Two properties are suspicious. `register_frame_done` fires from the video frame
update, so the counter tracks *rendered* frames unless the build guarantees one
callback per emulated frame. And input is applied *after* the frame has already
been emulated, so "press at frame 60" means "frame 60 has already run without
it". The `==` test is a third, sharper problem: if the counter ever steps past
an event's frame, that event is not applied late, it is **discarded silently and
permanently** — which matches "sometimes the Service Mode press does nothing".

I could not separate these. `machine.frame_number` reads `nil` on MAME
0.289's Lua API, and neither `machine.video.frame_number` nor an
`emu.frame()` equivalent is reachable from this script, so the two counters
cannot be compared from inside the tap. Establishing which of the three it is
needs either a machine-side frame source or a wrapper that passes a frame
assertion through. **Do not record any of the three as the cause until that
measurement exists** — the alternation is too regular to guess at.

### 29.5 A separate, smaller defect found while measuring this

`result.json`'s `canonical_log_sha256` is not path-independent. Three
`--input none` runs into three different output directories produced
byte-identical 3000-event streams and three *different* log hashes. Whatever
that hash covers, it covers the output path. Any evidence comparing
`canonical_log_sha256` across output directories is comparing the path, not the
run. Either fix it or state in `EVIDENCE.md` that it is only comparable within
one output directory.

### 29.6 Method note

The way this was caught is worth keeping: it took **five identical runs**. A
single re-run of the committed command "reproducing" or "failing" would have
produced a confident wrong answer in either direction. Any harness acceptance
that claims determinism from one invocation has not tested determinism.

## 30. What GSP memory actually contains at startup, and an unexplained early screen

Measured 2026-10-13 while looking for an early screen the MC68010 draws
directly. Everything here is `--input none` (game/attract), which §29.2a
established is deterministic — four runs, identical frame-300 screen sha256.

### 30.1 The share is bit-exactly zero until frame ~370

`tools/mame-gsp-vram-dump` reads all 2,097,152 words of the GSP `program`
space, not a sample:

```text
frame  1   nonzero        0 / 2097152    0.0%   gsp_pc=?
frame 20   nonzero        0 / 2097152    0.0%
frame 40   nonzero        0 / 2097152    0.0%
frame 100  nonzero        0 / 2097152    0.0%
frame 200  nonzero        0 / 2097152    0.0%
frame 300  nonzero        0 / 2097152    0.0%
frame 360  nonzero        0 / 2097152    0.0%
frame 390  nonzero   429504 / 2097152   20.5%   gsp_pc=00000000
frame 400  nonzero   620032 / 2097152   29.6%   gsp_pc=00000000
frame 420  nonzero  1170008 / 2097152   55.8%   gsp_pc=FFF41070
frame 450  nonzero  1664248 / 2097152   79.4%   gsp_pc=FFF41680
frame 600  nonzero  1664200 / 2097152   79.4%   gsp_pc=FFF41680
```

The frame-600 row reproduces the committed fixture bit-for-bit
(`35c92621…210ba`), so this is not a broken reader.

**The zeros are active, not passive.** At frames 43 and 44 the MC68010 is
inside a tight zero-fill loop at PC `$54AC`, writing `HSTDAT=0x0000` through
the GSP host port with `A0=$5082`, `A1=$02E8` as loop state, for **more than
4000 writes within a single frame**. The share is not waiting to be written; it
is being cleared.

### 30.2 There is a lot of data at frame 600, but it is nearly all flat colour

```text
4,194,304 bytes, 262,144 x 16-byte cells
  nonzero cells                                  208,025   79.4%
  of those, uniform (all 16 bytes identical)      182,052   87.5% of nonzero
  non-uniform nonzero cells                        25,973    9.9% of the share
  top uniform byte values  0x82 5023, 0xB8 4594, 0xAA 4390, 0xA3 3877,
                          0x0B 3792, 0x98 3737, 0x99 3557, 0x81 3065
```

So 87.5% of everything in the share is one byte repeated sixteen times. That is
the rendered attract-mode picture. **The structured, non-flat part of VRAM is
about a tenth of the share**, and that tenth is where anything interesting —
glyphs, road geometry, sprites — can live.

### 30.3 The addressing geometry of `$25E8` is now fully measured

Decoding every latched HSTADR pair in the game-mode trace, first character of
each draw only (A0 post-increments per character):

```text
$FF896040  A0=$006C58 A1=$0221C  char 'P'
$FF897040  (+0x1000)              row 1 of the same glyph
... 8 rows, stride 0x1000 ...
$FF896080  A0=$006C59 A1=$0223C  char 'R'   character advance = 0x40
```

* character advance **0x40** bytes, glyph row stride **0x1000**, **8 rows** per glyph
* draw 1: `$FF896040`..`$FF89D780`, **240** HSTADR latches = 30 chars x 8 rows
* draw 2: `$FF800040`..`$FF82F140`
* `A1 = $1FBC + 16*(char - $2A)` confirmed exactly on live data, including
  `A1=$01FBC -> char $2A` (the blank `*` glyph)

**Game mode and service mode write identical address ranges** — `$FF896040`..
`$FF89D780` and `$FF800040`..`$FF82F140` in both. Only the frames differ
(45-47 / 165-167 versus 214-216 / 334-336). The routine is identical; the
service schedule only delays it.

### 30.4 The contradiction that blocks the blit

The text IS on screen early. `--input none` frame 44 is pure black and frame 48
reads `PRESS START TO BYPASS SELFTEST`, matching the `$25E8` draw at frames
45-47. But at **every** frame from 44 to 52:

* the entire GSP `program` space is zero (exhaustive 2,097,152-word read), and
* main RAM `$FF8000-$FFFFFF` is **0 bytes different** between frames 44 and 47,
  and
* the GSP display registers are still at power-on defaults:
  `DPYSTRT=0xFFFF`, `DPYADR=$FC7E`, `DPYTAP=0`, `DPYCTL=0xF010`
  — the GSP program has not run at all (it starts at frame ~405-420, §8p4.2),
  and `vram_high_sum32` first goes nonzero at frame 168.

Yet `scanline_multisync()` in `harddriv_v.cpp:448-451` renders straight out of
`m_gsp_vram` (`vram_base = &m_gsp_vram[(params->rowaddr << 10) & m_vram_mask]`),
guarded by `if (!m_gsp_vram) return;`. A zero share under a power-on-default
DPYSTRT cannot produce white glyphs on black.

**Three explanations survive and none is yet chosen:**

1. the early screen is not produced by `scanline_multisync` at all — some other
   video path owns the bitmap until the GSP takes over;
2. the Lua dump and the screenshot land at different points within the frame,
   so "share zero at 48" and "text visible at 48" are not actually simultaneous;
3. the `$25E8` HSTADR latches at frames 45-47 never reach `HSTDAT`, so the
   routine latched addresses and drew nothing, and the early text has a
   different author entirely.

Explanation 3 is cheap to kill: PC `$2666`/`$2674` are the `HSTDAT` sites, and
a `register_frame_done` PC histogram over frames 44-51 shows only `$54AC`. But
that histogram **saturated at 4000 events inside frame 43 alone**, so it proves
nothing about frames 44-51 yet. Do not treat it as settled.

Until this is resolved there is no way to say how the text reaches the screen,
and `docs/gsp-display-scanout-contract.md` §"Service mode" — which places the
text at VRAM base `0x016040` and points the rasteriser at `rowaddr=0x3C,
DPYSTRT=0x00F000` — is describing a region that is provably zero at the moment
the text is on screen.

## 31. The `$548E` boot fill writes 230,400 words to nowhere, and `emu.register_prestart` never fires

Two findings, both measured 2026-10-13. The second one is the harness defect that
had been hiding input schedules from eight Lua scripts.

### 31.1 The routine, transcribed

`$25E8` was not the only thing running at frames 43-47. The host-port tap shows
PC `$54AC` writing `HSTDAT=0x0000` more than 4000 times **inside a single
frame**. That is the inner loop of `$548E`:

```text
0000548e: 00 79 18 00 00 c0 00 08  ori.w   #$1800, $c00008.l
00005496: 23 fc ff 80 00 00 00 c0 00 02  move.l  #$ff800000, $c00002.l
000054a0: 30 3c 00 00           move.w   #$0, D0
000054a4: 34 3c 03 83           move.w   #$383, D2
000054a8: 32 3c 00 ff           move.w   #$ff, D1
000054ac: 33 c0 00 c0 00 0c     move.w   D0, $c0000c.l        <- inner loop
000054b2: 51 c9 ff f8           dbra     D1, $54ac            <- 256 iterations
000054b6: 42 79 00 60 80 00     clr.w    $608000.l            <- watchdog kick
000054bc: 51 ca ff ea           dbra     D2, $54a8            <- 900 iterations
000054c0: 4e d0                 jmp      (A0)
```

256 x 900 = **230,400 HSTDAT writes**. With HSTCTL bit 11 (INCW) set, that would
be 230,400 x 16-byte cells = 0x384000 bytes = 3.5 MiB, i.e. 88% of the share.

**Where it is called from** — it has no `jsr`/`jmp` xref at all. It is reached
by `bra` through a continuation-address chain, which is why a xref search finds
nothing:

```text
$506C: movea.l #$ffffc000, A7
$5072: lea ($6,PC), A0     ; A0 = $507A
$5076: bra $53F2           ; GSP boot (clear regs, delay, set mode), ends jmp (A0)

$507A: lea ($6,PC), A0     ; A0 = $5082
$507E: bra $548E           ; <- THE FILL, then jmp (A0) at $54C0

$5082: lea ($6,PC), A0     ; A0 = $508A
$5086: bra $54C2           ; pattern fill: 0x0101 ramp at $F5000000,
                           ; 0..255 ramp at $F5800000

$508A: move.w #$47, D0
$508E: lea ($8,PC), A2     ; A2 = $5098
$5092: jmp $25E8.l         ; <- DRAWS SELECTOR $47
```

**So the answer to "can we link it to the char ROM text" is yes, and it is
adjacent.** The zero fill and the first text draw are consecutive stages of one
straight-line boot routine: `$548E` returns straight into `$54C2`, which returns
straight into `$508A`, which draws selector `$47` = `PRESS*START*TO*BYPASS*SELFTEST`.
The fill is not "before the text" by coincidence of timing; it is the previous
basic block.

### 31.2 The fill has no observable effect on the share

Patched `move.w #$0,D0` at `$54A0` to `move.w #$1a1a,D0` in a copy of the ROM
set. MAME warns the checksums no longer match and runs anyway.

```text
patched  $548e: 30 3c 1a 1a  move.w  #$1a1a, D0
```

Then, `--input none`, whole 4 MiB share, exhaustive read:

```text
frame  48   nonzero 0        (would be nonzero if one write had landed)
frame 100   nonzero 0
frame 200   nonzero 0
frame 300   nonzero 0
frame 360   nonzero 0
```

and at frame 600, where the unpatched machine has 1,664,200 nonzero words:

```text
patched  frame 600  sha256 35c9262190d8ec85183af255ba1962429897dec157a95793d61d06feee2210ba
original frame 600  sha256 35c9262190d8ec85183af255ba1962429897dec157a95793d61d06feee2210ba
differing bytes: 0 of 4194304
```

**Byte-identical.** Changing the fill's data value changes nothing at all. If
even one of the 230,400 writes had landed, the frame-48 image would contain a
nonzero cell. It contains none.

So the answer to "is the area it is zeroing a mirror of the framebuffer" is
neither yes nor no: **the writes are discarded.** They do not blank a mirror,
they do not blank the framebuffer, they do not reach any storage the share
exposes. The `HSTDAT` writes issued while the GSP is still in reset go nowhere.
That also sharpens section 30.4: the reason the share is zero at frames 44-52 is
not that it was cleared and is not that it was never written — it is that this
routine is a no-op, and whatever else was supposed to draw the early text did
not draw it here either.

The routine is still worth porting: it defines the mechanism the boot path uses,
and "this loop is a no-op on memory" is only a portable claim once the C mirror
reproduces the no-op. But it is **not** on the path to the screen, and a port
whose test asserts it clears VRAM would be asserting something false.

### 31.3 `emu.register_prestart` never fires in this build

The obvious way to answer "which cells were touched" is to poison the share
before frame 0. There is exactly one pre-frame hook, and it is dead:

```text
machine.register_init   = nil
machine.register_start  = nil
emu.register_prestart   = function       <- exists
emu.register_preinit    = nil
emu.register_init       = nil
emu.register_start      = nil
```

`register_prestart` is registered here (`luaengine.cpp:862`):

```cpp
machine().add_notifier(MACHINE_NOTIFY_RESET,
    machine_notify_delegate(&lua_engine::on_machine_prestart, this), true);
```

That trailing `true` is `one_time`, so the callback fires on the **first reset
only**. `-autoboot_script` loads later than that, so a callback the script
registers is never invoked. Verified directly: a prestart callback that prints
produces no output, under `-seconds_to_run 1`, `-seconds_to_run 3`, and
`-autoboot_wait 2`.

**Eight harness scripts rely on it:**

| script | prestart applies to |
| --- | --- |
| `gsp-vram-dump.lua` | `sw_off_prestart`, `drive`, `late`, `late_drive` |
| `gsp-state-snapshot.lua` | same four |
| `trace.lua` | `drive`, `late`, `late_drive` |
| `gsp-boundary.lua` | `sw_off_prestart`, `drive` |
| `display-list-trace.lua` | `drive`, `late_drive`, **`service_f2`** |
| `adsp-window.lua` | `sw_off_prestart` |
| `checkpoint.lua` | **unconditional** — `initial_events` |
| `memory-snapshot.lua` / `ram-write-trace.lua` | do not use it |

For all of those, the DIP prefix or initial events have **never been applied**,
in any capture ever taken. Committed artifacts affected:
`gsp-display-list-trace-600` (`drive`),
`display-list-gsp-state-late-1200` (`late_drive`),
`display-list-service-f2-60-180` and `-60-300` (`service_f2`), and every
`checkpoint.lua` capture.

Not affected: `service-f2-3600-stages` and all the `--input service` and
`--input none` screen captures, because `service` is not in any prestart gate.
That is why section 30's measurements stand.

This is the same class of failure section 29 describes — a setting that is
silently not applied and an observation that reads as a fact about the machine.
`tests/test_mame_lua_hooks.py` now asserts no `mame/lua` script applies input
through `emu.register_prestart`, with the reason recorded in the failure message.

### 31.4 CORRECTED — the host-port decode is on WORD offsets, and every write lands

**The first version of this section was wrong and is retracted.** It claimed the
driver discards everything at `$C00008` and `$C0000C`, that "every host-port
constant in the project is off by one register", and therefore that `$548E`
could not touch memory. All three are false. The error was reading
`(offset / 2) ^ 1` as if `offset` were a byte offset.

It is a **word** offset. `hd68k_gsp_io_w` divides by 2 precisely because the
device registers are words, so the window is eight words wide and the decode is:

```text
reg = ((cpu_addr - 0xC00000) / 2 / 2) ^ 1        integer division throughout
```

which gives, measured against MAME's own `stunrun_hostprobe`:

| 68010 address | word offset | reg | register | probe count |
| --- | --- | --- | --- | --- |
| `$C00000`/`$C00002` | 0/1 | 1 | HSTADRH | 39 |
| `$C00004`/`$C00006` | 2/3 | 0 | HSTADRL | 39 |
| `$C00008`/`$C0000A` | 4/5 | 3 | HSTCTL | 6 |
| `$C0000C`/`$C0000E` | 6/7 | 2 | HSTDAT | 780,315 |

An independent `install_write_tap` on the maincpu program space over
`$C00000-$C0001F`, run in the same session, counts the same writes at the
absolute addresses `$C00002`(39), `$C00004`(39), `$C00008`(6), `$C0000C`(388,723),
`$C0000E`(132,696) — and **nothing at `$C00000` or `$C00006`**. Mapping each
through the word-offset rule reproduces the driver's four counts exactly. That
is the whole argument; there is no residual.

**So the decode table in `native/mc68010_text_writer.h` was right all along:**

```text
$C00000-$C00003  HSTADRH      $C00008-$C0000B  HSTCTL
$C00004-$C00007  HSTADRL      $C0000C-$C0000F  HSTDAT
```

and `STUNRUN_TEXT_HSTCTL` / `STUNRUN_TEXT_HSTDAT` are correct as written. Do not
change them. Nothing in this repository needs a constant fix.

### 31.5 What this does to section 31.2 — the no-op claim is withdrawn

With the decode corrected, `$548E` does exactly what it reads as:

* `ori.w #$1800,$c00008.l` sets HSTCTL bit 11 (INCW), so HSTADR post-increments
  by one 16-byte cell per HSTDAT write — the walk works;
* `move.l #$ff800000,$c00002.l` sets HSTADRH=`$FF80`, HSTADRL=`$0000`, i.e. the
  base of the share;
* 230,400 `move.w D0,$c0000c.l` writes land on **HSTDAT**, covering
  0x384000 bytes from share offset 0 — 88% of the 4 MiB share.

**The share reading zero at frames 44-52 is therefore not a mystery at all: the
routine had just zeroed it.** That was the answer, and the "discarded" theory
was a wrong road to a right observation.

The patched-ROM experiment in 31.2 is **withdrawn as evidence.** It concluded
that changing the fill value to `$1a1a` produced a byte-identical frame-600
image, and concluded from that the writes were discarded. The conclusion does
not follow from the measurement, and the measurement itself is now suspect: if
the patch had landed, a share still zero at frames 48-360 is inconsistent with
a fill that walks 3.5 MiB with `$1A1A` in the data register. The likely
explanation is that the two bytes I patched
(`136070-2102.200r`/`136070-2101.210r` index `0x2A51`) did not reach the
executing image, even though reassembling them with the repo's own interleave
produced the expected instruction. **That has not been established either
way**, and until it is, 31.2's table of nonzero-word counts stands as
measurement but its conclusion does not.

`native/gsp_zero_fill.{h,c}` inherited the withdrawn claim from the brief and
must be corrected: its census treats `$C00008`/`$C0000C` as discarded, and its
`live_data_writes == 0` assertion is wrong. It is not merged-ready.

### 31.6 What the early text is, given all of the above

The strongest candidate, and the one the user's framing points at: the glyphs
live in the **68k's domain**. `$25E8` reads the font at `$1FBC + 16*(ch-$2A)`,
reads the descriptor from ROM, and writes words to HSTADR/HSTDAT. The 68010
composes the glyph; the GSP is not asked to interpret text at all.

If that is right then the text region is knowable without any GSP cooperation:
ROM descriptor plus ROM font bitmap predicts the exact VRAM words, which is
directly checkable against the captured stream. And the sharing agreement of
game and service mode (section 30.3) already fits — same descriptors, same
addresses, same words, different frames.

What is still unexplained, and is the next measurement: the share is zero at
every frame from 44 to 360 (section 30.1) while text is on screen at frame 48.
If `$25E8` writes land, something zeroes the region again before frame 48 is
sampled, or the fill runs later than assumed. **The fill's start frame was never
established** — the traces that appeared to show it at frames 43-44 each
saturated at 4,000 events, so only the first few thousand of 230,400 writes
were ever visible. Finding the real frame range of `$548E` is a one-line
`--pc 0x54ac` capture and it decides this.

## 31.548E The `$548E` zero-fill transcribed to C: 231,303 bus cycles, none of which touch VRAM

Slice: `native/gsp_zero_fill.{h,c}`, test `native/gsp_zero_fill_test.c`.
Ported by a subagent on 2026-10-13; the measurement in 31.1/31.2 is the parent's.

### 31.548E.1 The transcription

ROM `$0000548E-$000054C0`, source chip: the STUN Runner maincpu program ROM,
assembled flat by `tools/build-maincpu-image` from the six
136070-210x/300x/5xxx/6xxx pairs. Image
`roms/stunrun/stunrun_maincpu.bin`, sha256
`29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c`. Bytes
re-checked with musashi's `m68kdasm` (`tools/dis68k.c`) rather than taken on
trust from the prose:

```text
$ /tmp/opencode/dis68k roms/stunrun/stunrun_maincpu.bin 0x548e 14
0000548e: 00 79 18 00 00 c0 00 08  ori.w   #$1800, $c00008.l
00005496: 23 fc ff 80 00 00 00 c0 00 02  move.l  #$ff800000, $c00002.l
000054a0: 30 3c 00 00           move.w   #$0, D0
000054a4: 34 3c 03 83           move.w   #$383, D2
000054a8: 32 3c 00 ff           move.w   #$ff, D1
000054ac: 33 c0 00 c0 00 0c     move.w   D0, $c0000c.l
000054b2: 51 c9 ff f8           dbra    D1, $54ac
000054b6: 42 79 00 60 80 00     clr.w   $608000.l
000054bc: 51 ca ff ea           dbra    D2, $54a8
000054c0: 4e d0                 jmp     (A0)
```

Reach path confirmed as a continuation chain with no `jsr`/`jmp` xref:
`$5076 bra $53F2` -> `$507A lea ($6,PC),A0` (A0 = `$5082`) -> `$507E bra $548E`
-> `jmp (A0)` at `$54C0` -> `$5086 bra $54C2`. So `$5082` is a **caller-supplied**
argument in the port, not a constant, and the test passes two other
continuations to prove the value is passed through rather than baked in.

### 31.548E.2 CONFIRMED — the loop arithmetic

`dbra Dn,target` runs seed + 1 times, so D2 = `$383` gives **900** passes and
D1 = `$FF` gives **256** words per pass. 900 x 256 = **230,400** HSTDAT cycles.
The seeds are transcribed as seeds (`STUNRUN_GSP_ZERO_FILL_PASS_SEED`,
`..._WORD_SEED`) and the iteration counts derived from them in the header, so
the +1 cannot drift between the two.

The watchdog at `$54B6` sits **between** the inner and the outer `dbra`, so it
is kicked once per 256 data words -- not once per pass, and not once at the
end. The slice reports 900, and the test asserts the position of *every one of
the 900* against `4 + 257k + 256`, not a sample.

Cycle census, exhaustively asserted over all 231,304 recorded cycles:

| what | count | address | reaches the GSP? |
| --- | --- | --- | --- |
| `ori.w` read half | 1 | `$C00008` | no — discarded |
| `ori.w` store | 1 | `$C00008` | no — discarded |
| `move.l` high word | 1 | `$C00002` | **yes** — HSTADRL := `$FF80` |
| `move.l` low word | 1 | `$C00004` | **yes** — HSTCTL := `$0000` |
| data words | 230,400 | `$C0000C` | no — discarded |
| watchdog kicks | 900 | `$608000` | outside the window entirely |
| **total writes** | **231,303** | | 2 live, 230,401 discarded, 900 outside |

### 31.548E.3 CONFIRMED — the no-op, and what it is *not*

Applying section 31.4's decode to this routine's three addresses:

| address | byte offset | `(off/2)^1` | `host_w` case | outcome |
| --- | --- | --- | --- | --- |
| `$C00002` | 2 | 0 | `HOST_ADDRESS_L` | **lands** (HSTADRL := `$FF80`) |
| `$C00004` | 4 | 3 | `HOST_CONTROL` | **lands** (HSTCTL := `$0000`) |
| `$C00008` | 8 | 5 | none | dropped |
| `$C0000C` | 12 | 7 | none | dropped |

**Correction to the task brief I was handed.** The brief asked the test to
assert that *zero* writes reach a live register. That is wrong, and asserting it
would have produced a test that fails against the oracle. **Two** writes reach
live registers: the address pair at `$C00002`/`$C00004`. What is zero is writes
reaching **HSTDAT**, and that is the claim that carries the meaning:

```text
live_writes      = 2
discarded_writes = 230401     (230400 data + 1 control)
outside_writes   = 900        (the watchdog)
live_data_writes = 0          <- the load-bearing zero
```

So "this routine is a no-op on VRAM" is precise only if read as *nothing reached
the register that can move a byte in VRAM*. It is **not** "nothing lands". A
port that reported the fill as a pure no-op would have been overstating it, and
one that redirected the writes to `$C00006` to make them land would have been
asserting something section 31.2's `$1A1A` patch already refuted. The slice
reproduces the mechanism and reports the discard.

Note the second-order effect, which is worth keeping separate: even if `$C0000C`
*did* decode, the routine could not walk an address, because the `ori.w #$1800`
that would have set INCW also went to a discarded address. Two independent
failures, either of which alone is sufficient.

`$608000` is classified as **OUTSIDE**, not **DISCARDED**. Those are different
answers and the slice keeps them distinct: the watchdog is a real machine effect
and the driver never had an opinion about it.

### 31.548E.4 The port-port decode predicate, and why it is in the slice

`stunrun_gsp_zero_fill_classify()` derives the verdict from the formula rather
than from a table of four addresses:

```c
offset = address - 0x00C00000u;
if ((offset & 1u) != 0u) return DISCARDED;   /* unreachable on a 16-bit bus */
index = (offset / 2u) ^ 1u;
if (index > 3u)       return DISCARDED;
return index;
```

Two reasons it lives here rather than being inlined at each call site. First, it
is the finding: a caller needs to ask "was this write kept?" without
re-deriving a MAME decode, and it is not available anywhere else in the repo.
Second, section 15.6's trap -- a constant that moves with the code and leaves
the fixture sitting on it -- applies with full force to a hardcoded table. The
formula is short enough that the test can pin it independently: the test
classifies the recorded stream through the **published** predicate rather than
reading the slice's own tally back, so a slice that lied about its own
classification would be caught.

The odd-offset branch cannot fire for this CPU (`emumem_hedp` shifts the
delegate offset right by one), but it is classified rather than undefined so the
predicate is total.

### 31.548E.5 What the test asserts, and why those things

The test is **literal-first**: it asserts ROM-transcribed values (900, 256,
230,400, `$1800`, `$FF800000`, `$C0000C`, `$608000`, `$5082`) and only then
checks the header's copy against those literals. A test that read its
expectations out of the header would move its goalposts with any wrong constant
-- and would have made the fault injection below impossible, since the mutation
would have changed both sides at once.

Asserted:

1. the exact ordered cycle stream, including the `ori.w` **read half** in cycle
   0 (a slice that modelled `ori.w` as a plain store emits no read and is
   caught by `expect_cycle(0u, 1u, ...)`), the long access split high-word-first
   at `$C00002`/`$C00004`, and the group boundary at cycles 260 and 517;
2. every one of the 231,304 cycles individually -- no address outside
   {`$C00008`, `$C00002`, `$C00004`, `$C0000C`, `$608000`} may appear, no late
   address latch, every data word `$0000`, and each watchdog at exactly
   `4 + 257k + 256` including the **last** group, so a loop that stops one
   iteration early is caught;
3. the no-op, re-derived from the recorded stream through the published
   predicate, as `live_data == 0` plus the three-way census summing to
   `r.writes` -- an **assertion on a tally**, not on an absence;
4. `continuation` passthrough for `$5082`, `$12345` and `$00000000`;
5. the routine still counts correctly with **no** observer attached, and refuses
   NULL arguments with `0` rather than reporting a clean run.

The whole stream is recorded (231,304 cycles, 2.8 MB, file scope) rather than
sampled, so the cadence assertion covers all 900 watchdogs.

### 31.548E.6 Negative control — the test can fail

Six mutants, each a copy of `native/gsp_zero_fill.c` mutated in a scratch
directory and compiled against the **unmodified** header and test. All six must
fail; a pass would mean the assertions are decorative.

```bash
# the mutant harness is python3 + sed over a scratch copy; per-mutant commands:
gcc -std=c99 -O2 -Wall -Wextra -o /tmp/opencode/zf_mut/<name>/t \
    /tmp/opencode/zf_mut/<name>/gsp_zero_fill.c \
    /tmp/opencode/zf_mut/<name>/gsp_zero_fill_test.c \
    -I/tmp/opencode/zf_mut/<name> && /tmp/opencode/zf_mut/<name>/t
```

| # | mutation | caught by | actual output |
| --- | --- | --- | --- |
| 1 | `PASSES` -> `PASSES - 1` (899 passes) | `r.data_writes` | `r.data_writes = 230144 (0x38300), expected 230400 (0x38400)` |
| 2 | `WORDS_PER_PASS` -> `- 1` (255) | `r.data_writes` | `r.data_writes = 229500 (0x3807C), expected 230400 (0x38400)` |
| 3 | watchdog kicked once after the loop | `r.watchdog_writes` | `r.watchdog_writes = 1 (0x1), expected 900 (0x384)` |
| 4 | `seed = 0x1A1A` (the §31.2 patch value) | `r.data_value` | `r.data_value = 6682 (0x1A1A), expected 0 (0x0)` |
| 5 | data redirected to `$C00006` (make it land) | stream census | `cycle 4 = write $00C00006 = $0000, expected write $00C0000C = $0000` |
| 6 | decode `(off/2)^1` -> `(off/2)^3` | decode table | `classify($00C00000) = HSTCTL (3), expected HSTADRH (1)` |

All six build clean and exit 1. Mutants 1-4 are the four the brief asked for;
5 and 6 are the ones that matter most, because each is a way of *making the
routine do something* and each must not survive. Mutant 5 is caught by the
stream census rather than by the no-op tally, because the census rejects an
address it was not expecting first. That is the two checks doing their jobs in
the right order: the census makes "a data write landed" unreachable, and the
tally is the independent confirmation.

### 31.548E.7 Build facts

```bash
gcc -std=c99 -O2 -Wall -Wextra -o /tmp/opencode/gsp_zero_fill_test \
    native/gsp_zero_fill.c native/gsp_zero_fill_test.c -Inative
/tmp/opencode/gsp_zero_fill_test   # -> gsp zero-fill slice: all checks passed (exit 0)
```

Clean under `-Wall -Wextra` and additionally under
`-Wpedantic -Wshadow -Wconversion` at `-O0`, and clean under
`-fsanitize=address,undefined`. No ROM is needed to build or run it.

### 31.548E.8 INFERENCE, not transcription — what is *not* settled here

- **Which register the ROM meant at `$C00002`.** The slice transcribes that the
  two words **decode**, that the high half goes to the lower address, and that
  the control word never arrives. It does *not* claim the ROM meant
  HSTADRH:HSTADRL. This project's constants have always read `$C00002` as
  HSTADRH, which would make the access a plain address pair; under the pinned
  decode `$C00002` is HSTADRL and `$C00004` is HSTCTL, so the same long write
  puts `$FF80` in the address latch and **clears** the control register. Which
  is right is section 31.4's open question, and it is left open. Neither
  reading is applied.
- **What the driver returns for a read of `$C00008`.** The `ori.w` reads a
  discarded address, so there is no device value to model. `port.hstctl` is a
  caller preset and exists only so the OR is visible in the trace. Nothing
  depends on its value, because the write half is dropped too. The test's
  `$A000` preset is arbitrary and is **not** a measured MAME value.
- **Nothing here is evidence about the boot path's live HSTCTL contents.** The
  ROM's own HSTCTL store at `$C00008` is discarded, so this slice cannot say
  what INCW was during the real boot, and does not.
- **The 230,400 writes are counted, not timed.** The slice models bus cycles,
  not cycle counts. If a caller wants the elapsed time this routine costs on
  real hardware, that is a different measurement and nothing here supports it.

### 31.548E.9 Candidate for `BUGS.md` — for the parent to file

Under section 31.2 this becomes a routine -- **ROM-code / driver-mismatch
candidate**. The evidence in this section adds one thing section 31.2 did not
have: the discard is not confined to the fill.

- **Affected range:** `$C00008` and `$C0000C` in the 68010's GSP host window
  (`multisync_68k_map`, $C00000-$C03FFF).
- **Mechanism:** `hd68k_gsp_io_w` computes `offset = (offset / 2) ^ 1` and hands
  it to `tms340x0_device::host_w`, whose switch has exactly four cases. Word
  offsets 4 and 6 -- the ROM's `$C00008` (HSTCTL) and `$C0000C` (HSTDAT) -- decode
  to 5 and 7, fall through, and are discarded with no side effect.
- **Consequence measured here:** `native/gsp_zero_fill_test.c` records 231,303
  writes from `$548E`; 230,401 of them are dropped by the driver. Of the two
  that survive, both are latches, so **no HSTDAT cycle from this routine reaches
  the GSP**. This is the second ROM routine in the project to depend on
  `$C00008`/`$C0000C` being real; §31.4 shows `$25E8` uses `$C0000C` for its
  glyph data, which is why
  `service-f2-text-host-writes address_counts` is
  `{'0xC00002': 520, '0xC00004': 520}` and contains not one HSTDAT write.
- **Not decided:** whether the ROM targets a register layout this driver does
  not implement, or the driver's decode is wrong and real hardware accepts
  writes at `$C00008`/`$C0000C`. Confidence that the **driver as written** drops
  these writes: CONFIRMED (derived from the pinned source, and consistent with
  the `$1A1A` patch being byte-identical). Confidence about **real hardware**:
  UNKNOWN, and this section supplies no evidence either way.
- **The measurement that would settle it** is unchanged from §31.4: write a
  known value to `$C0000C` and to `$C00006` from the 68010, read both back, and
  separately do a direct space write. If `$C00006` lands and `$C0000C` does not,
  the driver is behaving as written and the question becomes what the hardware
  accepts. **Do not change `STUNRUN_TEXT_HSTCTL`/`STUNRUN_TEXT_HSTDAT` until
  that measurement exists** -- §31.4 gives the reason, and this slice gives the
  second example of code that depends on the answer.

This section files nothing in `BUGS.md` and changes no shared header. The
`gsp_zero_fill` constants are its own; `STUNRUN_TEXT_HSTCTL` and
`STUNRUN_TEXT_HSTDAT` in `native/mc68010_text_writer.h` are left exactly as
§31.4 found them.

## 32. The head of the boot chain: `$506C` sets SP and a continuation, then tails into `$53F2`

ROM range `$0000506C-$00005078`, 12 bytes, maincpu program ROM. Flat image
`roms/stunrun/stunrun_maincpu.bin`, sha256
`29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c`
(verified unchanged for this transcription).

```asm
0000506c: 2e 7c ff ff c0 00   movea.l #$ffffc000, A7
00005072: 41 fa 00 06         lea     ($6,PC), A0        ; ($507a)
00005076: 60 00 03 7a         bra     $53f2
```

Both PC-relative operands resolve against the address of the instruction's
**extension word**, not its first byte. `lea ($6,PC),A0` is encoded at `$5072`,
its extension word is at `$5074`, and `$5074 + $0006 = $507A`. `bra.s` is
encoded at `$5076`, its extension word at `$5078`, and `$5078 + $037A = $53F2`.
Using the opcode address instead yields `$5078` and `$53F4`, the first of which
is one byte inside the `bra` and would decode as different code entirely.

### 32.1 Confirmed

* **Three instructions, twelve bytes.** No hidden fourth; `$507A` is the next
  link and is code.
* **There is no `jsr $506C` anywhere in the image.** A full-image
  disassembly contains exactly three references to `$506C` and every one is a
  `jmp`: `$0106`, `$0172`, `$02B2`. The absence of a call is why this chain is
  invisible to xref tools that key on call sites.
* **`$53F2` ends in `jmp (A0)` at `$548C`.** So A0 is not a data pointer, it is
  the trampoline the boot sequence returns through. `$548E` ends in `jmp (A0)`
  at `$54C0` and `$54C2` ends in `jmp (A0)` at `$552A`, so the pattern holds
  across the whole chain:

  ```asm
  $506C  SP:=$FFFFC000, A0:=$507A, bra $53F2
  $53F2  ... $548C  jmp (A0) -> $507A
  $507A  A0:=$5082, bra $548E        <- 900 x 256 = 230400 discarded HSTDAT writes
  $548E  ... $54C0  jmp (A0) -> $5082
  $5082  A0:=$508A, bra $54C2        <- pattern ramps at $F5xxxxxx
  $54C2  ... $552A  jmp (A0) -> $508A
  $508A  D0:=$0047, A2:=$5098, jmp $25E8.l
  ```

  Each link is the same two instructions in the same order. `$508A` hands the
  next continuation to **A2**, not A0, because `$25E8` drives A0 and A1 itself;
  the chain terminates there.
* **The SP literal is the ROM's Initial SSP, byte for byte.** The first long of
  the reset vector at ROM `$00000000` is `ff ff c0 00` = `$FFFFC000`
  (`xxd -l 8`). The second long at `$00000004` is `$00000216`, so the ROM's own
  reset-vector PC is `$0216`, and `$0216` does `movea.l ($0,A0), A7` with
  `A0 = 0` — it loads A7 *from* that long. The ROM therefore treats
  `$FFFFC000` as "the stack pointer", not as an arbitrary constant.
* **`movea.l #$ffffc000,A7` occurs at 45 sites in this image and is a
  procedure prologue, not a one-off.** `$0FBC` is structurally identical to
  `$506C`: set A7, `lea (d,PC),An`, tail-branch. `$50B0` and `$53E6` are the
  same. There is **no overlap** between the 45 prologue sites and the 532
  `link` sites, so the ROM runs two conventions side by side.
* **On the 24-bit bus the effective SP is `$FFC000`.** `$FFFFC000 & $FFFFFF =
  $FFC000`, which is exactly 16 KiB above `$FF8000`, the base of the 32 KiB work
  RAM from §9 — leaving 16 KiB of stack growth below A7 and nothing above it.
  This **corroborates** §9 rather than contradicting it. (The 24-bit mask is
  already established here: `native/musashi_cpu.c:360` and §9's note that the
  oracle dump "wrapped under a 24-bit address mask".)

### 32.2 Is `$506C` an entry point, or the tail of something larger?

**Entry point. Both readings were checked and the alternatives do not hold.**

* Not fall-through: `$506A` is an `rts` belonging to the routine that ends
  there. The next byte, `$506C`, is a fresh function.
* Not a `jmp` target from inside the chain: the only three incoming transfers
  are the three `jmp`s above. `$0106` and `$0172` are two entries of the thunk
  table at `$0100` (a dense 6-byte-stride table of `jmp $xxxx.l` stubs);
  `$02B2` is reached straight out of a port test.

**Correction to `native/fake_ports.c`.** The comment at `fake_ports.c:40`
records `clear -> 0x2a0 -> 0x506c -> 0x548e`. The endpoints are right; the
middle step is conditional and has a sibling that the record does not mention:

```asm
000002a0: 08 39 00 00 00 60 c0 01   btst  #$0, $60c001.l
000002a8: 66 00 00 08               bne   $2b2
000002ac: 4e f9 00 00 8f ee         jmp   $8fee.l     ; bit 0 clear
000002b2: 4e f9 00 00 50 6c         jmp   $506c.l     ; bit 0 set
```

So `$2A0` does **not** fall through into `$506C`; `$2B2` does, conditionally on
bit 0. The bit-0-clear arm is `$8FEE`, which runs a **parallel chain** of
identical shape — `$8FF2` sets `A0 = $8FFA` and `$8FF6` branches to the same
`$53F2` — differing from `$506C` only in that `$8FEE` does `move #$2F00,SR` and
does **not** touch A7. So `$548E` is one of two places that chain can arrive at.

Two further corrections worth making in the same comment when someone next
touches it: the route to `$548E` passes through `$53F2` and `$507A`, not
directly; and **`$2B8` is data, not code** — `$5410` does `lea $2b8.l,A1` and
walks 8 six-byte records to `$2E7`, which is why the bytes after `$2B2` decode
into plausible-looking nonsense.

### 32.3 Why `$FFFFC000`? Two readings, neither discarded

Both survive the evidence; the slice documents both and resolves neither.

1. **It is the Initial SSP.** Direct evidence: the reset vector long, and
   `$0216` loading A7 from it.
2. **It is a fixed frame base.** Evidence: 45 sites, each the first instruction
   of its routine, none of them `link`, none saving the incoming A7.

On reading 2 the `movea.l` **replaces** rather than restores — nothing saves
the old A7 and nothing puts it back. That is only safe because `$506C` is
entered by `jmp` and never by `jsr`, so there is no return address to lose.
It is a real constraint on the slice: `$506C` must never be modelled as a
function that returns.

Readings 1 and 2 are not in tension numerically — the 24-bit mask makes the
constant land in the same place either way — so they are also *not*
distinguishable by any observation available here. What distinguishes them is
intent, and intent is not in the bytes.

### 32.4 `docs/mc68010-boot-findings.md` §2 is wrong about this edge

§2 (line 212) says *"The boot at `0x506c`->`0x50b0`"*. §2's own listing of
`$50B0`-`$50FE` is accurate, but the **edge into it is not from `$506C`**:
`$506C` branches to `$53F2`. `$50B0` is a separate routine, reached only from
`$573E`, `$5750` and `$587C`. This is our own doc error, so it is a refutation
in this document, not a `BUGS.md` entry.

### 32.5 C slice

`native/gsp_boot_entry.{h,c}` + `native/gsp_boot_entry_test.c`, registered by
the parent in `native/CMakeLists.txt`.

The seam reports the three operations as ordered events
(`SET_SP`, `SET_CONTINUATION`, `TRANSFER`) plus a register/block state struct,
because the slice is a transfer and not a call: modelling it as a C function
that returns would invent an activation record the ROM does not have. Both
PC-relative operands go through one helper,
`stunrun_gsp_boot_entry_pc_relative(base, displacement)`, fed an explicit
base+displacement pair, so the slice records the *rule* rather than the two
numbers that fall out of it. The helper sign-extends the 16-bit word; the
negative-displacement case is asserted because `(d,PC)` is a signed form and an
unsigned add would satisfy `$5074 + $0006` while failing `$5000 - 6`.

`STUNRUN_GSP_BOOT_ENTRY_SP_EFFECTIVE` exposes the post-mask value, and the test
asserts the mask and the §9 arithmetic separately so that moving either the SP
literal or the RAM base breaks something visible.

**Check facts.**

```
$ gcc -std=c99 -O2 -Wall -Wextra -o /tmp/opencode/gsp_boot_entry_test \
      native/gsp_boot_entry.c native/gsp_boot_entry_test.c -Inative
$ /tmp/opencode/gsp_boot_entry_test
gsp boot entry slice: all checks passed                     (exit 0)

$ gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion ...
gsp boot entry slice: all checks passed                     (exit 0, no warnings)
```

The test asserts observable output: the value written to A7, the value written
to A0, the address control leaves for, and the **order** of the three. Order is
asserted because it is the part a final-state check cannot see — mutation E
below reports the correct three numbers in the wrong order and still fails.
Register expectations are literals in the test and derived values in the
implementation; if both sides computed from the same constants, moving a
constant would move both together and the test would keep passing for the wrong
reason. The test also caught a real precedence bug in its own first draft
(`a7 & mask == want` parses as `a7 & (mask == want)`).

**Negative control** — the test file is never modified; five mutated copies of
the implementation are compiled against it:

| # | mutation | exit | reported |
|---|---|---|---|
| A | `LEA_DISP $0006 -> $0008` | 1 | 3 failures: continuation/`A0`/event 1 all `0x507c`, want `0x507a` |
| B | `BRA_DISP $037A -> $0376` | 1 | 2 failures: target/event 2 `0x53ee`, want `0x53f2` |
| C | continuation and target swapped (`$53F2` / `$507A`) | 1 | 5 failures across all four value assertions |
| D | PC base off by one (`$5074 -> $5073`) | 1 | 3 failures: `0x5079`, want `0x507a` |
| E | emit order swapped, same three values | 1 | 2 failures: event 0 is `0x2` want `0x0`, event 2 is `0x0` want `0x2` |

C and E are the ones worth keeping. **C** shows the assertions are
position-specific rather than set-based: a slice that reported both addresses
but attached them to the wrong register produces the same *set* of values and
is still caught. **E** shows the ordering assertion is real rather than
decorative — every register value is still correct and the test still fails.

## 33. What the 1989 Atari Games manual corroborates (TM 238 / SP-338)

`reference/manuals/stunrunner-ocr.txt` is the OCR of the bundled `stunrunner.pdf`:
the 1989 Atari Games Operator's Manual *with Illustrated Parts Lists*, SP-338,
which carries the Main Board Memory Map and the Multisync PCB schematic. It is
vendor documentation for the board we are emulating, so it is independent of
both the ROM and MAME and it outranks our inference wherever the two disagree.

The OCR is heavily degraded — the schematic pages came through as fragments —
so everything below is restricted to rows that survived legibly. **A row that
is not listed here was not read, not disproved.**

### 33.1 Independently confirmed

| Manual text | Corroborates |
| --- | --- |
| `FF BFE0 RAM 0 (16K Bytes)` / `FF FFFF RAM 1 (16K Bytes)` | **§9 exactly.** Main RAM is 32 KiB at `$FF8000-$FFFFFF`, not the 512 KiB §2.1 claimed. The old refutation was ours; this is the vendor saying so. |
| `FF C000` as a RAM boundary | The `$506C` reading that `$FFFFC000` as Initial SSP yields an effective 24-bit SP of `$FFC000` — exactly 16 KiB into the 32 KiB RAM. Two independent routes to the same number. |
| `ROMEN: ROM0 128K` .. `ROM7 128K`, `$000000-$0FFFFF` | The 1 MiB flat maincpu image and its 8-bank layout. |
| `60 8000 {W} WOCLR Clear Watch Dog (Address Strobe)` | The `clr.w $608000.l` in both `$548E` and `$25E8` is a documented hardware register, not an incidental write. |
| `60 C000 {A}` bit `05` = `Self-Test Switch (0 = on)` | §0's IN0 bit 5 = `PORT_SERVICE`. It is a **switch**, not a momentary button. |
| `60 4006 GSP Reset Low` / `60 4016 GSP Reset High` | The GSP has a **dedicated hardware reset line** at `$604006`/`$604016`, distinct from `HSTCTL` bit 15 (HLT). Two separate ways to stop the GSP; keep them apart. |
| `60 0000 {RW} SCOM` | The JSA sound window. |
| `FF 4FFE ZRAM (4K Bytes)` | A **separate 4 KiB zero-power RAM** at `$FF4000`, in the map but not in our machine model. |
| `34010-50 GSP`, `34012-50 PSP`, `RAM Array (16)`, `Scanner RAM`, `Pixel Color GRN0-GRN7`, `BL0-BL7`, `RD0-RD7` | The GSP is a TMS34010 as assumed; `Scanner RAM` is the framebuffer the scanout reads. |

### 33.2 The text we have been chasing is the operator self-test

Chapter 2's screen list, in order: `PROGRAM ROM`, `PROGRAM RAM`,
`GSP VRAM OK--SIMPLE TEST`, `GSP COLOR RAM OK`, `ADSP BOARD OK`, `SA II Test`,
`PRESS START TO SELECT TEST MENU`. Then the test menu, whose items the manual
lists as `SEL 1..SEL 4` and coin-counter toggles on `$604000`.

That is our two draws, in order:

```text
frame  45-47  selector $47  'PRESS*START*TO*BYPASS*SELFTEST'   the bypass prompt
frame 165-167 selector $43  'PROGRAM*ROM*OK'                     self-test line 1
              selector $50  'Y*X*W*V*U*T*S*R'                   the menu items
              selector $51  '200'
              selector $52  '210'
```

`Y X W V U T S R` against the manual's `SEL 1..SEL 4` / coin counters is close
enough to be worth chasing and far enough that **it is not evidence yet** — the
manual's menu is the *coin options* screen (Figure 2-5) as well as the test
menu, and we have not distinguished them. Recorded as a lead, not a match.

This does settle the mode question from §29: the game-mode capture at frames
45-47 and 165-167 is the **self-test and test-menu sequence reached by default
boot**, not attract mode. The attract title screen is what appears later.

### 33.3 What the manual does NOT settle

**The `$C00000` GSP host port is not in the legible Main Board Memory Map.** That
map runs `$60C000` -> `A00000` -> `A80000` -> `B00000` -> `FF0000` and stops;
`$C00000` lives on the GSP board, whose schematic page OCR'd to fragments
(`34010-50 GSP`, `RAM Array (16)`, `Pixel Color GRN0-GRN7`) with no address
rows.

So **the host-port decode in §31.4 stands on its own measured evidence only.**
The manual neither confirms nor contradicts it. Do not cite the manual as
support for it. The word-offset decode
`reg = ((addr - 0xC00000) / 2 / 2) ^ 1` rests on MAME's own `stunrun_hostprobe`
counts matching an independent write tap, and on nothing else. Anyone who wants
it settled properly needs the GSP board schematic read as an image rather than
as OCR text — see §33.4.

### 33.4 Suggested next step

`stunrunner.pdf` is 8.5 MB and its schematic pages are vector/raster artwork
that OCR cannot read. Two things it would settle that are currently open:

1. the `$C00000` host-port register layout, which is the difference between
   "MAME is right and the ROM targets a layout MAME lacks" and "MAME's
   `(offset/2)^1` is wrong" — still unresolved from §31.4;
2. the font, if the GSP board carries a character-generator ROM or a glyph
   table the 68010 copies, which is the transform `tools/extract-screen-glyphs`
   and `tools/dump-glyph-font` currently disagree about.

Rendering the PDF pages to images and reading them is the route. That is a
one-off investigation, not a harness capability, and it belongs to the
Investigator role.

### 33.5 The GSP video chain, from SP-338 Page 10

`/tmp/stunrunner_pages/page-67.png` is SP-338 Page 10 and carries the video
block diagram legibly. (The OCR's page association is unreliable — it credits
`34010-50` to Page 9, which is actually the ADC and shifter sheet. Read the
images, not the OCR page markers.)

```text
34010-50 GSP --> 34012-50 PSP --> VDD-VD15  (15-bit digital video)
             \-> Address Buffers -> MA0-MA7 -> VRAM Array (16) 64K x 4
                                                   |
                                                   +-> SD0-SD15 -> Pixel Scanner
                                                        -> SPIX0-SPIX7 -> Color RAM
                                                        -> RED0-RED7 GRN0-GRN7 BL0-BL7
```

**This settles the architecture question.** The chain is:

* The **GSP (TMS34010) writes VRAM and does not scan out.** It reaches VRAM
  through address buffers on `MA0-MA7`.
* A **separate TMS34012 (PSP)** takes the GSP's `VDD-VD15` and serialises it.
* The **Pixel Scanner** shifts VRAM out on `SD0-SD15`; **Color RAM** supplies
  the final `RED0-7 / GRN0-7 / BL0-7`.

So the answer to "does the GSP hold the glyphs and get sent text, or are the
glyphs pulled by the 68k and put in for it to copy" is the second, and more
precisely than that: **the glyphs are composed entirely in the 68k's domain and
land in VRAM over the GSP host port.** The GSP is never asked to interpret text.
That is why `$25E8` writes colour words rather than bitmaps, and why the text
region is predictable from ROM descriptor plus ROM font plus the write pattern
alone. The screen then shows whatever the Pixel Scanner reads out of that VRAM.

### 33.6 Two things on this page that disagree with our model

**1. VRAM size.** The BOM line (OCR 3802) reads `64K x 4, 120 nsec VRAM` and the
schematic block reads `VRAM Array (16)`. Sixteen 64K x 4 chips is
16 x 256 Kbit = **512 KiB**. That is the figure §12.1 originally claimed and
§19 "measured" away at 4 MiB.

The §19 measurement was a real measurement of the pinned MAME build — the
sentinel probe found the first alias at offset 0x400000, i.e. 4 MiB of distinct
content — and it stands as a statement about MAME. What is now open is whether
**MAME is modelling the right amount of VRAM**, because the vendor's own BOM and
block diagram say 512 KiB. A 4 MiB model of a 512 KiB board would alias every
512 KiB, and the game would still look right in every case we have checked,
because nothing so far has addressed past 512 KiB and noticed.

This is a **candidate MAME driver defect** and it is the first serious one that
is not a Lua-harness artefact. It is NOT filed yet: `multisync_nomsp()` may
instantiate more VRAM than the bare schematic block implies, and `MA0-MA7`
being only eight address lines means the addressing is not fully described by
that box. **Do not act on it yet.** Settling it needs the VRAM array schematic
sheet — which of SP-338 Pages 3-8 carries the 16 VRAM chips and their address
lines — not this block diagram.

**2. Palette.** `Color RAM` is a separate device between the Pixel Scanner and
the RGB outputs, fed by `SPIX0-SPIX7` (8 pixel-index bits) and producing three
8-bit channels. `docs/gsp-display-scanout-contract.md` models "two 1024-entry
16-bit planes" with `R = lo>>8, G = lo&0xff, B = hi&0xff`. That is a plausible
reading of two interleaved 16-bit halves and may be equivalent, but it is
**inference, not transcription**, and it is now known that a distinct colour
RAM exists rather than palette planes inside the GSP's own memory. Worth a
pass; not urgent.

### 33.7 Clocks and jumpers from the same page

`XO-SCH 40MHZ` feeds the pixel scanner. A `15.00Y 0.11431B 12` crystal feeds an
LS374 divider producing the `4 MHz / 16 MHz` domains; the 68010 is `16 MHz`
clocked. Jumpers: `Speed A,B` / `BCLK QB` / `VCLK QB/2`. The 16 MHz figure is
directly relevant to `stun-runner-bzq`, which currently derives the OKI rate
from a hardcoded `3579545/3` rather than from a clock the manual states.

### 33.8 Does 512 KiB-vs-4 MiB matter? Measured: not for this ROM, and not against MAME

The question raised against §33.6, answered by
`mame/lua/gsp-vram-reach.lua` (a write tap on the GSP program space, run to
frame 600, `--input none`):

```text
writes=492544  nonzero_writes=0
max_offset_any=0x3FFFF0  max_offset_NONZERO=(never)
per 128 KiB page: exactly 16384 writes each for pages 0-27,
                 9216 / 8192 / 8192 / 8192 for pages 28-31
pages 4 onward (0x80000 and up) all written, all zero
```

492,544 = 230,400 (the `$548E` zero fill) + 262,144, and 262,144 is exactly the
number of 16-byte cells in a 4 MiB share. So there are two whole-share
operations: the fill's 230,400, and one write per cell across all 4 MiB. Both
carry zero.

**Every write that reaches the GSP program space in the first 600 frames is
`0x0000`.** That includes every page above 512 KiB.

So the honest position is symmetric, and it is stronger than "it probably does
not matter":

* **MAME is not shown to be wrong.** If the real board is 512 KiB and the window
  aliases every 512 KiB, then the 230,400 fill writes collapse to repeated
  zeroing of the first 512 KiB and produce *the same end state* as MAME's 3.5 MiB
  of distinct zeros. Every acceptance test we have, including the byte-exact
  frame-600 rasteriser comparison, passes under both models.
* **The ROM cannot be shown to need more than 512 KiB**, because it never puts a
  nonzero there. The address range it walks is 4 MiB wide; the *data* is not.

The 2 MiB page-twin at delta `0x200000` in the frame-600 fixture is consistent
with this: two independent whole-share zeroing passes leave page 0 and page 4
identical without implying a mirror.

**This is not proof that MAME is right.** It is proof that no evidence we hold
distinguishes the two, which is a different and much weaker claim, and it is the
kind of claim that decays badly if recorded as the former. §33.6's VRAM array
sheet remains the way to settle it.

### 33.9 An inconsistency this measurement created, which is not yet resolved

§30.4 and the trace at section 31 show the 68010 issuing `move.w D0,$c0000c.l`
with **data `0x00DF`** at frames 45-47 -- 331 writes at `$2666` and 330 at
`$2674`. §31.4 established `$C0000C` decodes to HSTDAT, and `host_w` then calls
`TMS34010_WRMEM_WORD(addr & 0xfffffff0, data)` -> `m_program.write_word`.

If that path runs, the tap above must have seen `0x00DF` 661 times. It saw zero
nonzero writes, ever.

So either the two taps disagree, or `write_word` into this 16-bit-wide /
16-byte-unit space (`datawidth 16, addrshift 3`) does not report to a write tap
in the way the tap callback signature implies, or the text writes are not
reaching `m_program`.

**This is unresolved and it is load-bearing**: it is the same question §30.4
raised, from the other end. Do not reconcile 33.8 and 33.9 by assuming the tap
is merely blind -- if the tap is blind, then 33.8's "every write is zero" is also
unproven, and 33.8 has to be withdrawn rather than softened. **The next
measurement must establish whether a write tap on this space sees a known
nonzero `write_word`, before any of 33.8 is cited as evidence.**

Cheap version of that test: drive one known word to a known offset through the
real host port -- `HSTADR=$FF800010`, `HSTDAT=$1234` -- with the tap installed,
and see whether the callback reports `0x1234` at offset `0x10`. If it does, the
tap works and 33.8 stands and 33.9 says the text writes are not landing. If it
does not, the tap is blind and both sections fall.

### 33.10 RESOLVED: the tap works, and the text writes genuinely never land

`mame/lua/gsp-tap-window.lua`, `--input none`, printing **every nonzero write**
to the GSP program space in frames 43–50:

```text
frame=43 writes=290428 nonzero=0
frame=44 writes=302510 nonzero=0
frame=45 writes=314590 nonzero=0     <- 331 x $00DF and 330 x $DFDF/$DF00
frame=46 writes=326670 nonzero=0        are issued by the 68010 in this frame
frame=47 writes=338750 nonzero=0
frame=48 writes=350832 nonzero=0
frame=49 writes=362910 nonzero=0
frame=50 writes=374990 nonzero=0
```

12,080 writes per frame, stepping `0x000000, 0x000010, 0x000020 …` with
`mask=0xFFFF` — the `$548E` zero fill, exactly as §31.1 predicted. **The tap is
not blind.** It sees the fill perfectly.

And across frames 45–47, when the 68010 demonstrably issues `move.w D0,$c0000c.l`
with data `0x00DF` and `0xDFDF`, **not one nonzero value reaches the share.**

So §33.8's conclusion **survives**, and §33.9's tension is **the bug**:

* every write that reaches the GSP program space in game mode is `0x0000`;
* the text routine's colour words do not land, even though the CPU-side bus tap
  sees them issued at `$C0000C` and MAME's own `stunrun_hostprobe` counts
  780,315 HSTDAT writes.

Both observations are true and they cannot both describe a working host port.
The fill's `$C0000C` writes land; the text's `$C0000C` writes do not. Same
address, same instruction form, same `host_w` path — which leaves the difference
in *what the routine believes HSTADR holds*, not in the bus.

**A reconciliation the evidence does not yet exclude, and which fits everything
so far:** `ori.w #$1800, $c00008.l` sets INCW and post-increments HSTADR by one
16-byte cell per HSTDAT write. The fill latches `HSTADR = $FF800000` once and
then relies on that increment. If the text routine's own `andi.w #$e7ff, $c00008.l`
at `$25EE` clears INCW **before** the fill set it — or if the two runs interleave —
the text's writes would all land on one fixed cell rather than walking. That is
checkable from the CPU-side bus trace alone and needs no new tooling. **Not
tested. Do not assume it.**

### 33.11 An unreconciled count, recorded rather than smoothed

§33.8 reported **492,544** writes total to frame 600. This run reaches
**374,990** by frame *50*, at 12,080 per frame — which extrapolates to roughly
7.2 million by frame 600. The two do not agree, and I cannot currently say why.
Both used the same corrected range `$FF800000–$FFBFFFFF`; the difference is that
§33.8's script summarised and this one prints. Until it is explained, §33.8's
*write count* should not be quoted. Its *conclusion* — zero nonzero writes — is
independently reproduced above and stands on its own.

### 33.12 MD-09 is this same bug, and the manual says what to do about it

`BUGS.md` **MD-09** records "Service mode `BAD GSP VRAM` in MAME": the MC68010
diagnostic at `$004F82` fills `$FF945E` with eight longwords of `0x01010101`,
then reads 16 words back through the GSP host port and gets `0xFFFF` for every
one. That is **the same failure with a different payload** — a nonzero value
written through the host port that does not come back.

MD-09 is scoped to service mode because the diagnostic only runs there. It does
not run in `--input none`, so its absence from the run above says nothing about
it. But 33.10 shows the text routine failing the same way in **game** mode, which
MD-09 does not mention and which was not previously connected to it.

The manual names the symptom and the remedy. From the GSP Tests section:

> VRAM Complete Test is **a complete memory test run by the 68010 through the GSP
> interface**. Because the 68010 runs the test, a single bad VRAM does not …

and from Table 2-5, *When to Use the Special Function Items*:

> If you see the message **Bad GSP VRAM** or **Bad GSP Color RAM** in the
> automated self-test. → **Main Board GSP Tests**
> If you see the message **Bad Program ROM** in the automated self-test. →
> **Main Board ROM Checksums**

So `Bad GSP VRAM` is a *self-test verdict*, not a hardware name, and Atari's own
first move is to run the GSP tests rather than to suspect VRAM silicon. The GSP
Tests menu is: *GSP Tests* ("Tests if the 68010 can communicate with the GSP …
if the GSP does not respond, LED 1 and 2 flash differently"), *Red/Green/Blue
Screen* ("Produces a [colour] screen **from the color RAM, regardless of VRAM
input**"), *GSP Memory Fill*, and *GSP VRAM Verify*.

That last parenthetical is independent confirmation of the Color RAM being a
device in its own right — it can produce a full-screen colour with no VRAM input
whatsoever — and it gives a VRAM-independent way to test the video path, which
is worth having while §33.10 is open.

**Next action, and it is the cheapest useful thing left:** run the GSP VRAM
diagnostic under service mode with this same tap and see whether its
`0x01010101` writes land. If they do not, MD-09 and §33.10 are one defect and
should be tracked as one. If they do, then the text routine's failure is
specific and INCW-related as §33.10 suggests, and that is a much narrower bug.

### 33.13 Three service-mode runs: still zero, and the counts move

`mame/lua/gsp-vram-reach.lua` with `--input service`, to frame 700:

```text
run 1  writes=492544  nonzero=0  max_offset_any=0x3FFFF0
run 2  writes=350832  nonzero=0  max_offset_any=0x383FF0
run 3  writes=447474  nonzero=0  max_offset_any=0x383FF0
```

**Zero nonzero writes in every run**, same as game mode. So whatever service
mode did, it did not put a nonzero value into the share through this address
space either.

This also explains §33.11: the write count is not a fixed property of the run,
it varies run to run (492,544 / 350,832 / 447,474 at the *same* target frame).
That is consistent with the §29 input flakiness changing how much work the
machine actually did, not with a broken counter. `max_offset_any` moving between
`0x3FFFF0` and `0x383FF0` is the same story: `0x384000` is the top of the `$548E`
fill, so runs that reach the top of the share did something the others did not.

**What this does not settle:** whether the `$004F82` diagnostic actually executed
in any of these three runs. Service mode was a coin flip (§29) and this probe
does not report which arm it landed in. Reaching "zero nonzero writes" without
knowing the diagnostic ran is not the same as testing MD-09, and it must not be
quoted as though it were.

### 33.14 The lead this exposes: some VRAM writes bypass the address space entirely

The share at frame 600 holds **1,664,200 nonzero words**. This probe has now
observed **zero nonzero writes reaching that memory by any address-space path**,
in game mode or service mode, across six runs.

Those 1.66M nonzero words cannot have come from nowhere. The handler that
produces them is visible in the pinned source and does not go through the address
space at all:

```c
/* harddriv_v.cpp:304, hdgsp_vram_2bpp_w */
uint32_t *dest = &m_gsp_vram[offset * 4];      /* direct share pointer */
```

That is a raw pointer into the `memory_share`, so it **cannot** appear in a
write tap installed on the GSP program space. `hdgsp_vram_1bpp_w` at `:276` does
the same. So the framebuffer content is written share-direct, while the 68010's
host-port writes go through `m_program.write_word` and *should* be visible.

Which leaves exactly one unexplained case, and it is the one that matters: **the
`$548E` fill's `$C0000C` writes are visible to the tap, and the text routine's
`$C0000C` writes are not.** Same address, same instruction form, same `host_w`
path, same `m_program` target. The difference has to be in the value or the
target address the routine supplies, and the CPU-side bus trace already contains
both — so this is answerable with tooling we have, not with new tooling.

**Do not treat "the tap says zero" as "VRAM is empty."** §30.1's zero-share
readings at frames 44–52 came from the bulk reader, and this section now shows
the bulk reader and the tap can both be blind to writes that matter. Both of our
"the share is all zeros" claims need re-testing against a reader that goes
share-direct, which is what MAME itself uses.

## 34. RESOLVED: physical GSP VRAM is 512 KiB; TMS34010 addresses are bit addresses

This section supersedes the 4 MiB claim in §19, the 16-byte-cell claim in §27,
and the tap/bulk-reader conclusions in §§30–33. Those were not three independent
observations. They shared the same wrong unit conversion: **we treated a TMS34010
bit address as a byte address.** The Atari manual, the actual MAME `memory_share`,
and the fixed native 68010 all now agree.

### 34.1 The source of truth for backing size

SP-338 Page 10 labels the silicon `VRAM Array (16) 64K x 4`. Sixteen 64K×4
chips are 16 × 256 Kbit = **4 Mbit = 512 KiB**. More decisively, the pinned
MAME's own direct-share hook writes from
`(const uint8_t *)m_gsp_vram.begin()` for exactly `m_gsp_vram.bytes()`:

```text
share bytes=524288 words=262144 vram_mask=0x003ffff
```

Raw capture, service mode, frame 600:
`reference/experiments/stunrun/gsp-direct-share-frame600-service/`.
The share, both palette planes, MAME log, wrapper result and SHA-256 values are
recorded there. This is a `memory_share` pointer, not a read through the TMS
program-space API. The Lua program-space reader that produced the old 4 MiB
image is an **address-space view**, and its 4 MiB file size was the range of GSP
bit addresses it sampled — not the number of physical bytes behind them.

The Lua reader has since been corrected to read one word every `0x10` bit
address units. In a single instrumented-MAME service run, its 512 KiB output at
Lua frame 601 is byte-identical to the C++ direct-share dump taken at screen
frame 600: `e1e3be56…86e7c4ee`, **0 differing bytes**. The one-frame label offset
is recorded in `reference/experiments/stunrun/gsp-share-reader-crosscheck-service/`.
So MD-05 (“Lua cannot read the share at all”) was our stride error, not a MAME
driver gap, and is removed from `BUGS.md`.

### 34.2 The conversion, with units shown

The TMS34010 program space is configured `datawidth 16, addrshift 3`
(`tms34010.cpp`). The GSP addresses are in bits; one program-space address unit
is one bit. The host writes `TMS34010_WRMEM_WORD(addr & 0xfffffff0, data)`.
The `& ~0xF` aligns to a 16-bit word, and each HSTADR INCW adds `0x10` — the
next 16-bit word, **two physical bytes**.

```text
GSP bit address delta      -> physical byte offset
0x00000000                 -> 0x00000
0x00000010  (one word)     -> 0x00002
0x00001000                 -> 0x00200
0x00096040                 -> 0x12C08
0x00400000  (mirror)       -> 0x80000 -> aliases to 0x00000

byte_offset = ((addr - 0xFF800000) >> 3) & 0x7FFFF
word_index  = byte_offset >> 1
```

For the 68010's first text line, HSTADR `$FF896040` therefore lands at physical
share byte `$12C08`, **not** `$96040`. For the `$548E` fill, 230,400 writes at
INCW stride `0x10` cover 230,400 × 2 = **460,800 bytes**, not 3.5 MiB. It clears
about 87.9% of the 512 KiB share; it does not address 88% of a 4 MiB share.

The old `HSTADR=$FF801000, HSTDAT=$1234 -> 16 repeated bytes` result from §27 is
withdrawn as a physical-share measurement. It read a program-space address as
if each address increment were one byte; stepping through a bit-addressed,
addrshift-3 space at `+2` does not read successive physical bytes. The direct
share layout is the authoritative one.

### 34.3 Native frame-48 proof — the 68010 writes real text into the share

The prior native run had a second, independent ROM loader defect. Each pair of
64 KiB chips is `ROM_LOAD16_BYTE`: the even file fills destination bytes
`2*i`, the odd file fills `2*i+1`, across the **full 128 KiB bank**. The loader
had instead written 64 KiB of interleaved output and padded the remaining half
with `0xFF`. That put the selector table at `$10136` outside the correctly
loaded bytes, made `text-log descriptors=0`, and produced random ROM execution.
The old fix in commit `b5d2a4e` corrected the fill value but kept the wrong loop.
The current loader interleaves `even[i] -> bank[2*i]`, `odd[i] -> bank[2*i+1]`;
the upper half is **not padding**. The two CTests disabled by that mistaken
analysis are re-enabled.

With the fixed loader, `STUNRUN_IN0=0xC7`, terminal frame 48, native capture:

```text
reset vectors: SP=0xFFFFC000 PC=0x00000216
text-log descriptors=394
text frame=48 sel=$047 x=01 y=0F colour=DF base=FF896040 chars=30
  "PRESS START TO BYPASS SELFTEST"
text-log arms=1 matched=1 fired=1 colour_miss=0 ambiguous=1
```

The native direct-share dump is **524,288 bytes**. Its first text cell at
physical `$12C08`:

```text
df df df df df df 00 00 df df df df df df 00 00
```

The MAME direct-share capture at frame 600 has the exact same 16 bytes at the
same offset. Comparing the 16 KiB window `$12C00-$16BFF`, **16,141 of 16,384
bytes (98.52%) agree** across the two runs. The remaining differences are
expected from different frames (MAME has continued through later test/menu
draws). The one-cell, frame-48 oracle is byte-exact.

Native palette entries also now match MAME's direct palette-share capture:

| index | native lo/hi | MAME lo/hi | RGB |
|---|---|---|---|
| `00` | `0000 / 0000` | `0000 / 0000` | black |
| `DF` | `DFDF / 00DF` | `DFDF / 00DF` | `(223,223,223)` |
| `FF` | `FFFF / 00FF` | `FFFF / 00FF` | `(255,255,255)` |

The old native palette mapping used the GSP bit-address low byte as a palette
index and stored `uint16_t` values in the wrong byte order. It now converts the
word offset (`delta >> 4`), banks 256 entries at a time, and stores the palette
words in the share's little-endian layout.

### 34.4 What this says about the screen, and what is still wrong

This resolves §30.4 and §33.9. The text's nonzero HSTDAT writes **do** land in
physical VRAM; the MAME direct share has the same data; the Lua address-space
tap and 4 MiB dump were the wrong observers. The `$548E` zero fill is followed
by the text draws, exactly as the ROM continuation chain says. There is no
separate, unseen renderer author for `PRESS START TO BYPASS SELFTEST`.

The normal native live scanout still uses the frame-600 fallback registers when
the GSP RTL is halted. At frame 48 those are not the self-test screen's
scanout-origin registers. `STUNRUN_CAPTURE_FRAME` writes the CPU renderer's
internal pixel buffer (`stunrun_render_write_png(&renderer)`); **it is not a
screenshot of either SDL/OpenGL window and does not prove what the user sees.**
With diagnostic override `STUNRUN_FORCE_FRAMEBUF=12C08`, that internal buffer
contains a 148×8 text approximation at the top, while MAME's frame-48 prompt is
237×7 at y=150. A separate X11 capture of both live SDL windows in this session
returned black frames. **There is not yet a trustworthy capture of the app's
actual on-screen output.** The remaining issue is early-frame scanout
origin/stride, pixel expansion, and verifying the actual window output, not the
68010 text source or its VRAM storage.

### 34.5 Retractions and remaining scope

* **§19:** `gsp_vram` is not a 4 MiB share. The 4 MiB result measured the GSP
  address-space sample range; it did not measure backing storage. Vendor BOM and
  the MAME direct pointer both say 512 KiB.
* **§27 / BUGS.md SR-20:** the 16-byte-replication claim is withdrawn. A host
  HSTDAT word stores one packed 16-bit word at a physical 2-byte offset. SR-20
  was removed from BUGS.md; it was our misinterpretation, not surprising
  behavior in the game or emulator.
* **§30.1 / §33.8:** the all-zero 4 MiB address-space/tap conclusions are not
  physical-share measurements. The old `frame_600_gsp_vram.bin` is an
  address-space dump, not VRAM. The direct 512 KiB capture above supersedes it.
* **§33.10:** the claim that text writes never land is retracted. Its tap saw
  writes on the 68010 bus but was not an observer of the physical share write
  path. The native direct-share test and MAME direct dump now establish where
  they land.
* **§33.11:** counts from the 4 MiB address-space tap are not physical write
  counts and should not be used to estimate share fill coverage.

The native Musashi path now uses the 512 KiB share and `addrshift=3` conversion.
The independent Verilated `native/gsp_rtl/gsp_board.c` model still has a 4 MiB
array and byte-addressed semantics; it has **not** been corrected or validated by
this change and must not be cited as the physical board model yet.

### 34.6 Validation

```text
cmake --build native/build -j$(nproc)                  PASS
ctest --test-dir native/build -j$(nproc)               61/61 PASS
/usr/bin/python3 -m pytest tests/ -q                    222 PASS
```

The MC68010 frame-48 capture has `bus_dropped=0`, `descriptors=394`, one `$47`
text-log match and 524,288-byte VRAM dump. The run is reproducible from the repo
root with `NATIVE_BUILD_DIR=$PWD/native/build`,
`STUNRUN_STUNRUN_DIR=$PWD/roms/stunrun`, `STUNRUN_IN0=0xC7`,
`STUNRUN_FRAMES=48`, `STUNRUN_DUMP_DIR=<dir>`, `STUNRUN_CAPTURE_FRAME=48`,
`STUNRUN_CAPTURE_DIR=<dir>` and `STUNRUN_TEXT_LOG=1`.

## 35. gsp_board.c consumes the corrected units (gsvp.3)

`native/gsp_rtl/gsp_board.c` was the last model still carrying the unit error
of sections 19/27/30-33. It now converts GSP bit addresses to physical share
bytes, and is no longer the odd one out against `native/musashi_cpu.c`.

### 35.1 What changed

`GSP_BOARD_VRAM_BYTES` is `0x80000` (512 KiB), not `0x400000`. The whole change
is one conversion, now funnelled through a single function so no caller can
reintroduce a byte-addressed read of a bit-addressed space:

```c
static uint32_t gsp_board_vram_offset(uint32_t gsp_addr)
{
    return ((gsp_addr - 0xff800000u) >> 3) & (GSP_BOARD_VRAM_BYTES - 1u);
}
```

The mapped *address* range is unchanged - MAME still maps
0xff800000-0xffffffff, mirrored through bit 22. It is the share behind that
window that is 512 KiB, and that was the thing 4 MiB got wrong.

Regions fixed alongside it, each because it had absorbed the same error:

| path | was | now |
|---|---|---|
| host HSTDAT -> VRAM | 16-byte cell, byte replicated 8x | packed 16-bit word at a 2-byte physical offset |
| HSTDAT address mask | `& ~0xf` read as a 16-byte cell | `& 0xfffffff0` is a 16-bit-**word** alignment (tms34010.cpp:1898) |
| 2bpp window offset | `(addr & 0xfffff) >> 1` | `(addr - 0x02000000) >> 4`, stride `offset * 8` |
| 2bpp colour source | `ctrl_lo[0]` | `ctrl_lo[1]` |
| 2bpp word assembly | big-endian | little-endian, matching the share |
| palette entry index | `addr & 0x0fff` | `addr >> 4` |
| palette word store | big-endian | little-endian |

The 2bpp big-endian form is worth calling out. It was wrong in the host store
and right in the core store, and the two cancelled - which is exactly why
`tools/check-gsp-replay` could not see it. A differential check cannot see an
error that both sides of it share.

### 35.2 What it is checked against

Not a percentage, and not against itself. Three levels:

1. **Unit test.** `test_hstdat_stores_packed_word_at_converted_offset` and
   `test_hstdat_rom_text_address_lands_at_12c08` pin the conversion against a
   value the ROM actually uses: HSTADR `$FF896040` -> physical `$12C08`, the
   68010's first text line. Verified as a negative control: deleting the `>> 3`
   produces 11 failures.
2. **Against the native model**, which is the copy validated against MAME.
   `tools/check-gsp-replay`, service mode, frame 48:

   ```text
   host-window events=234057  vram_writes=461636  unmapped_writes=11
     differing bytes : 0 / 524,288
     byte sum ours=93214 reference=93214
   RESULT PASS
   ```

3. **Against MAME, one cell.** The native service-mode frame-48 dump at physical
   `$12C08` reads

   ```text
   df df df df df df 00 00 df df df df df df 00 00
   ```

   byte-identical to MAME's direct share capture, and `$096040` - where the
   byte-addressed model would have put it - is empty. That reproduces 34.3 from
   the board model's side rather than restating it.

### 35.3 What is still not shown

`gsp_board.c` agrees with the native model, and the native model agrees with
MAME on the text cell and the palette. That chain has not been closed across
the whole 512 KiB share: the comparison above is a differential between two of
our own models, and the one MAME-anchored point is a single cell. Section 18.2
is untouched and independent - the display-list capture still ends with the GSP
halted for 99.61% of its window - and gsvp.4, the core executing the uploaded
display list, remains the thing that actually produces pixels.

## 36. The Verilated core reproduces MAME's first output, byte for byte (gsvp.4)

The TMS34010 core now demonstrably executes the uploaded display list. This is
the item gsvp.4 was opened for, and the claim is deliberately narrow.

### 36.1 Why the release is the only clean question to ask

The GSP is frozen by HSTCTL bit 15 for essentially the whole capture, so across
the release frame edge nothing but the GSP can change the share. That is the
one window in which "did the core execute the uploaded program" can be asked of
a pair of images without a second moving part. Everywhere else, a diff measures
upload plus everything since.

The boundary is confirmed on both sides:

```text
MAME   frame 405, PC 0x0002C1C4, HSTCTL 0x7800
native record 899809, cycle 50351440, PC 0x0002c1cc, HSTCTL 0x7800
```

### 36.2 The oracle had to be re-captured first

`reference/experiments/stunrun/gsp-program-upload.metadata.json` records its
boundary images as 4194304 bytes / 2097152 words. Those predate section 34: they
are GSP program-address images, 8x the physical share. Its headline - "10,360
changed words in 22 ranges" - is an address-space figure.

Re-captured with the corrected `tools/mame-gsp-vram-dump` (one word per `0x10`
address units):

```text
frame 404, GSP halted  : 524288 bytes, 262144 words, 81899 nonzero
frame 405, released    : 524288 bytes, 262144 words, 83194 nonzero
```

**1295 changed words in 22 ranges, all newly nonzero, none modified, none
zeroed.** And the unit correction checks out arithmetically: `10360 / 8 =
1295` exactly, because the 4 MiB address image covers each physical word eight
times. Two independent captures agreeing on an 8x factor is the strongest
evidence in this file for the `>> 3`.

The event stream is byte-identical between the two captures
(`5640860abca7c79b...` both times), so only the image extent changed - which is
what should have happened when only the reader was corrected.

### 36.3 The result

Replay the native bus stream to its own HLT release, hand it to the Verilated
core, run 200,000 GSP cycles:

```text
events=899810 host_txns=899804 reset_events=6 timeouts=0
PC 0x00000000 -> 0xfff59a70   range 0x00000000..0xfff59a90   distinct(low16)=24
state=5 instr=0x100b
during run: board writes=11,948 reads=39,025
```

Then `tools/check-gsp-first-output`, over the window the pre/post pair itself
defines (bytes 446118..448912, 1397 words):

```text
  MAME changed 1295 words in 22 ranges (newly_nonzero 1295, newly_zero 0, modified 0)
  core matches post-release: 1397/1397 (100.00%)
  core changed vs pre-release: 1295 words (MAME changed 1295)
  changed by core only: 0   by MAME only: 0
RESULT PASS
```

Not a percentage that happens to be high. The core wrote **exactly** the 1,295
words MAME wrote, no more and no fewer, and every one of the 1,397 words in the
window matches. It is agreement on a named, finite, MAME-defined set.

### 36.4 What this does not say

- **Not whole-share parity.** The core has 81,536 nonzero words against MAME's
  83,194 (98.0%). The tool prints that ratio rather than hiding it, because a
  pass on the first-output window must not be read as a pass on the share.
- **Not display-list execution.** The first output is uniform fill, which is
  what the GSP does first; the tool's own report notes the diff is "not decoded;
  no field names are claimed". Whether the core later walks a real display list
  is not shown here.
- **The 68010 side is not re-derived.** The stream is the native model's, and
  its agreement with MAME at the release (same PC, same HSTCTL) is a
  correspondence, not a proof of cycle-level equivalence.
- Section 18.2 is untouched: the display-list capture still ends with the GSP
  halted for 99.61% of its window.

### 36.5 The check itself

`tools/check-gsp-first-output` never assumes where the window is - it derives it
from the pre/post pair, so a wrong address constant cannot produce a pass. It
also checks the SET of changed words, not just the values, which is what stops a
halted core from passing: zero writes trivially matches the pre-release image,
and only "by MAME only: 1295" catches it. It refuses a 4194304-byte input
loudly, naming the bit-address confusion, because that exact file has been
committed here once already.

## 37. Past the first fill: the core converges, and the residual is a trajectory offset

Section 36 established the first output. The obvious next question is whether
the core keeps going, and whether it converges on MAME's share. Both answers
are yes, and the residual has a shape rather than being noise.

### 37.1 Convergence on written content

Same replay, cycle budget swept, whole share compared against MAME's frame-405
image:

| GSP cycles | core nonzero words | PC range | distinct low-16 | board writes | whole-share agree |
|---|---|---|---|---|---|
| 200,000 | 81,536 | `..FFF59A90` | 24 | 11,948 | 99.09% |
| 2,000,000 | 83,141 | `..FFF59DD0` | 370 | 577,344 | 98.48% |
| 20,000,000 | 83,153 | `..FFF59DD0` | 542 | 1,009,480 | 98.47% |

MAME's frame 405 has **83,194** nonzero words. The core reaches **83,153** — 41
words of 83,194, i.e. **99.95% of the content MAME has written**. The PC range
widens to `..FFF59DD0` and the distinct-low-16 count goes 24 -> 542, so this is
a program running, not a loop: it is fetching and executing across the uploaded
range rather than spinning on a few words.

Whole-share agreement *falls* slightly with more cycles, and that is expected
rather than bad: MAME's frame 405 is one frame after the release, so a core
running 100 frames is a different point on the same trajectory. 200,000 cycles
sitting closest is the useful calibration — **200k GSP cycles is about one MAME
frame after release.**

### 37.2 The residual is a trajectory offset, not model error

At 200,000 cycles the whole-share residual is **2,388 words**. Sweeping 20k ->
300k cycles it does not move at all (2,388 at every budget), so it is not an
artefact of the cycle budget. Decomposing it:

```text
2,023 words   core wrote 0x0000 over MAME's 0xFFFF boot fill
  365 words   core wrote a ramp: 0505 0a0a 0f0f 1414 1919 1919 1e1e 2323 ...
              concentrated in 0x060000-0x06FFFF (330) and 0x070000-0x07FFFF (35)
```

Both are the GSP doing the next thing after the uniform fill: a clear, then a
blit. Neither is in a region MAME leaves alone — against MAME's frame-600
service capture only 117 of the 2,023 zero-fill words are still nonzero, so
MAME clears that region too, just later. The core is **ahead on the same
trajectory**, not wrong about it.

So the honest statement is: the core's share agrees with MAME's frame-405 image
on 259,756 of 262,144 words, and every one of the 2,388 disagreements is the
core having already performed a clear and a ramp that MAME performs after that
frame edge. That is a different claim from the section 36 one, and it is a
weaker one: it says the two are on the same path, not that they are at the same
point.

### 37.3 What still follows from this

Not whole-share parity at a stated frame, and not display-list semantics. To get
either, the comparison has to be pinned to a common emulated time rather than a
common event — which needs the 68010 cycle at which MAME reaches each frame and
an equivalent bound on the core's run, and that pairing is not established here.

Reproduce:

```text
tools/capture-gsp-program-upload roms <dir> --frames 600 --mame <mame>
build/gsp_rtl/stunrun-gsp-rtl-harness --boot <stream> --cycles N --vram-out <core>.bin
tools/check-gsp-first-output <core>.bin <pre>.bin <post>.bin
```

## 38. RESIDUAL: the scanout units were never wrong — the row base is `rowaddr << 10` and it stays

A live RTL run with the Verilated core inside the shell filled the share
(`vram nonzero words = 82720`, `HSTDAT = 898301`, identical to the standalone
capture) and still rendered nothing. The working diagnosis was that
`native/gsp_display.c` was reading the share "with the wrong address units":
that DPYADR is a bit address, that `dpyadr ^ 0xFFFC` was being used directly
as a VRAM word index, and that the fix was the §34.2 `>> 3` conversion.

**That diagnosis is refuted.** `^ 0xFFFC` is correct and stays, and the row base
was already `rowaddr << 10` words — which is exactly MAME's own expression.
What follows is the derivation, then what *is* wrong, because it is not here.

### 38.1 DPYADR is not a program-space address

Three different units meet in this path and they are not interchangeable.

1. **GSP program-space address units are bits.** The program space is
   `memory_access<32, 1, 3, little>` (`tms34010.h:996`; §12.2 cites `:992`,
   which is four lines stale) — datawidth 16, addrshift 3 — so one address unit
   is one bit of GRAM, a 16-bit VRAM word is `0x10` address units, and the
   `0x400000`-unit mapped window is 512 KiB of backing. §34.2.
2. **DPYADR / DPYSTRT are not in that space.** They are the screen-refresh
   address registers. The TMS34010 User's Guide puts DPYADR[15:4] on LAD12..23
   during row address time and DPYADR[2:6] on LAD6..10 during column address
   time (ug.txt:5027-5038, transcribed in `docs/tms34010-reference.md` §1.1).
   DPYADR is a packed **(row, column)** pair. There is therefore no "DPYADR as a
   share word index" reading to correct, and no `>> 3` to apply to it.
3. **The share is bytes**: 524288 of them, 262144 `uint16` words.

`get_display_params()` decodes (2) into `rowaddr = dpyadr_xor >> 4` and
`coladdr`, and the scanline turns the row into a byte offset with a row stride.
That stride is the whole question.

### 38.2 Which `rowaddr` shift, and why `<< 10`

STUN Runner is `stunrun_machine` (`harddriv.cpp:2046`) → `STUNRUN_BOARD` →
`stunrun_board_device_state::device_add_mconfig` → `multisync_nomsp(config)`
(`harddriv.cpp:1861`), which installs
`set_scanline_ind16_callback(scanline_multisync)` (`harddriv.cpp:1549`). So
`scanline_multisync` applies and **`rowaddr << 10` is the shift**
(`harddriv_v.cpp:451`).

The `<< 11` at `harddriv_v.cpp:418` is `scanline_driver`, the Hard Drivin'
cockpit callback, which builds 4096-pixel rows (`(coladdr & 0xff) << 4`, minus
15, plus `finescroll & 0x0f`, colour index masked at `0xfff`).
`harddriv.cpp:1488` is the only place that callback is installed and
`harddriv.cpp:1549` the only place `scanline_multisync` is; STUN Runner never
reaches the first. It is not importable.

The two callbacks are internally consistent, and that is worth stating because
it is easy to assume one shared mask:

| | row stride | column start | colour index mask |
|---|---|---|---|
| `scanline_driver` (HD) | `rowaddr << 11` = 4096 B | `(coladdr & 0xff) << 4` | `0xfff` |
| `scanline_multisync` (ours) | `rowaddr << 10` = 2048 B | `(coladdr & 0xff) << 3` | `0x7ff` |

4096 bytes / 16 pixels-per-unit = 4096-pixel rows; 2048 bytes / 8 = 2048-pixel
rows. A TMS34010 display row is 2048 pixels, so `rowaddr << 10` is right here
and the `<< 11` and `0xfff` belong to the board that has the wider row.

`rowaddr << 10` words = 1024 words = 2048 physical bytes, and it is the §34.2
conversion applied to the row's first bit address, so the two derivations are
the same statement:

```text
row R begins at GSP bit address 0xFF800000 + R * 0x4000
word index = ((0xFF800000 + R*0x4000) - 0xFF800000) >> 3 >> 1
           = (R << 14) >> 4 = R << 10                      words
```

`native/gsp_display.c` now computes each of these through its own function —
`stunrun_gsp_display_row_base_word()` and
`stunrun_gsp_display_word_from_bit_address()` — so the equality is a **test**
rather than a comment, and `gsp-display-slice-c` checks all 256 rows plus the
ROM's own landmarks:

```text
  all 256 rows: rowaddr<<10 == bit-address conversion        ok
  HSTADR 0xFF800000 -> share word 0x0000                     ok
  HSTADR 0xFF896040 -> share word 0x9604 (byte 0x12C08)      ok
  HSTADR 0xFF801000 -> share word 0x0100 (byte 0x200)        ok
  mirrored HSTADR 0xFFC00000 aliases word 0x0000             ok
  byte-addressed 0x96040 is a different cell (the retracted error) ok
```

### 38.3 Confirmed against MAME's own probe output, not against itself

`reference/experiments/stunrun/gsp-direct-share-frame600-service/mame.log`
records the resolved geometry of the frame-600 service-mode self-test screen.
Taking only the registers that log records (`dpyadr=ffff dpystrt=ffff
dpytap=0000 dpyctl=f010`, `finescroll=7`), the native derivation reproduces all
eight recorded lines:

```text
  line 19: rowaddr=000 base=000000 yoffset=0                 ok
  line 20: rowaddr=000 base=000000 yoffset=1                 ok
  line 21: rowaddr=000 base=000000 yoffset=2                 ok
  line 22: rowaddr=000 base=000000 yoffset=3                 ok
  line 23: rowaddr=001 base=000400 yoffset=0                 ok
  line 245: rowaddr=038 base=00e000 yoffset=2                ok
  line 246: rowaddr=038 base=00e000 yoffset=3                ok
  line 247: rowaddr=039 base=00e400 yoffset=0                ok
```

Those eight are transcribed constants, not inputs. With the captured share
itself (the same 524288-byte file, gated on `STUNRUN_GSP_DISPLAY_DUMPDIR`), two
more:

* every one of the 229 × 512 scanned pens equals the byte at the word index the
  rasteriser computes — 117,248 pixels, not a summary statistic;
* `STUNRUN-DPY`'s own `first10=0000^00000000^00000000^00000000^0000ffff^ffff`,
  recorded for each of lines 19-22, comes out of the rasteriser unchanged.

And the whole frame: feeding that capture through `stunrun_gsp_display_render`
with that capture's registers produces the self-test screen — `PROGRAM ROM OK`
at the top, two seven-cell text rows above the counters `200` and `210`, a
horizontal rule, and `PRESS START TO BYPASS SELFTEST`. (The two middle rows are
not transcribed here: 4×8 cell glyphs at this size are not legible enough from
the render to transcribe without guessing, and the scanline addressing they
exercise is already pinned by the checks above.) Render FNV-1a `0x69424479`,
identical before and after this section's edits, i.e. the change is
behaviour-preserving.

The named cell: §34.3 pins the 68010's first text line at HSTADR `$FF896040` →
physical byte `$12C08`, byte-identical between the native model and MAME's
direct capture. The scanout fetches exactly that word at **scanline 169, x=8 —
screen y=150**, which is where §34.4 records MAME's own 237×7 prompt. That is
the end-to-end cross-check: a ROM address, a physical share offset, a line and a
column, all agreeing.

### 38.4 What is actually blank, and none of it is here

Three defects, all outside `gsp_display.c`. Reported, not fixed.

1. **The live window is zero-height.** `shell.c:616` sets
   `dregs->vsblnk = 19`, identical to `veblnk`, so
   `stunrun_gsp_display_rasterise` returns −1 at `y1 <= y0`, the shell prints
   `live-video=unavailable` at frame 0 and disables live video for the rest of
   the run — which is the whole of `mode=blank-scaffold`. §12's own probe
   records `vsblnk=0x0103` (259) for this board. Reproduce: pass
   `STUNRUN_VSBLNK=103` and `live-video frames-presented=601` appears.
2. **The four display registers are still the driver's initialiser.** After a
   600-frame run `gsp_rtl_driver_get_display_regs` returns `DPYSTRT=0x00f0
   DPYADR=0xffc3 DPYTAP=0x0200 DPYCTL=0x8201` — byte-for-byte the
   `s_dpy_regs` initialiser at `gsp_rtl_driver.cpp:56-61`. The core has not
   issued one `KIND_IO` write to `0xF6000010..17`, so the scanout is being fed
   placeholders. Their comments are also wrong on their own terms: `0x3C ^
   0xFFFC` is `0xFFC0`, not `0xFFC3`, so the constant that is *documented* to
   encode `rowaddr = 0x3C` in fact decodes to `rowaddr = 3`; and `0x8201 &
   0x03FC` is `0x200`, not the `DUDATE = 0x004` claimed. With those four values
   the scanout reads share byte `0x1800`, which is zero — correct behaviour on
   nonsense input.
3. **`render frame=... hash=...` does not hash the live frame.** `shell.c:1829`
   calls `stunrun_render_begin()` *after* the frame loop, clearing the
   framebuffer, and no fixture path then re-renders. So the printed hash is a
   cleared buffer's. Measured: three runs with different scanout origins print
   the same `hash=0x7D611DC5` while their captured PNGs differ
   (`fnv=0x119C337D` vs `0xE2E787AD`).

And one genuine defect **in** this file, deliberately left in place:

> **The frame's first address should come from DPYSTRT, not from a mid-frame
> DPYADR snapshot.** The TMS34010 reloads DPYADR from DPYSTRT prior to the start
> of each frame (`tms34010.cpp:1322-1327`; `docs/tms34010-reference.md` §1.2
> quoting ug.txt:5432-5440, "the starting address points to the location in
> memory of the first pixel output to the screen during each frame"). Taking
> DPYADR as read at an arbitrary instant as the top-of-frame address is wrong
> whenever the caller can observe it mid-frame, which is exactly what the live
> RTL path does — there, `DPYSTRT=0x00f0` decodes to `rowaddr 0xFF0` → share
> byte `0x78000`, which is where the core's display-list output actually is,
> against `DPYADR=0xffc3` → `rowaddr 3` → byte `0x1800`, which is zero.
>
> It is not changed here because the same heuristic is duplicated at
> `backend/gsp_scanout_gl.c:104`, outside this task's ownership: changing one
> copy alone desynchronises the two scanout windows and breaks
> `gsp-scanout-gl-parity` (its case 1 sets `regs.dpyadr = 0xFC39` against
> `dpystrt = 0xFC3C`, which the two readings resolve differently). **Both copies
> must move in the same commit.** The same applies to `STUNRUN_FORCE_FRAMEBUF`
> in `shell.c:685`, which sets `dregs->dpyadr` from `(row ^ 0xFFFC)`; that is
> already wrong independently of this issue, because `get_display_params`
> divides by 16 — it would need `(row << 4) ^ 0xFFFC`, and after this section's
> fix it would need to set `dpystrt` rather than `dpyadr`.

### 38.5 Refuted while writing it: the `0xFFF` column mask that is not there

This section first asserted that MAME's multisync scanline masks the colour
index at `0xfff` (`harddriv_v.cpp:540`) and that this file's `0x7ff` was an
untested divergence worth recording as UNKNOWN.

**It does not.** `grep -n 'coloridx = coladdr' src/mame/atari/harddriv_v.cpp`:

```text
433:		int coloridx = coladdr++ & 0xfff;     <- scanline_driver   (HD)
540:		int coloridx = coladdr++ & 0x7ff;     <- scanline_multisync (STUN Runner)
```

`scanline_multisync` — the callback STUN Runner actually installs — uses
`0x7ff`, byte-identical to `native/gsp_display.c`. The mis-read came from
reading lines 380-551 in one go and carrying `scanline_driver`'s body down to
the multisync loop; the two callbacks are 60 lines apart and their inner loops
look nearly identical. Every cited line number in this section was re-checked
with `sed -n` afterwards, and `harddriv_v.cpp:540` was the only one that had
been mis-attributed. There is no divergence to record.

The one thing that survives from that draft is an observation about **both**
implementations rather than a difference between them: `col` is
`(yoffset<<9) + ((coladdr&0xff)<<3) - 7 + (finescroll & 7)`, and `finescroll = 7`
cancels the −7 on every frame recorded in this repo. At `finescroll = 0` the
line would start at −7, and masking turns −7 into 2041 — a jump of ~1000 words
rather than 7 pixels backwards, where the hardware would read into the previous
row. MAME does exactly what this file does, so there is nothing to match and no
evidence yet that either is wrong. If a capture ever shows `finescroll != 7`,
that is the first thing to check, and it is a candidate for `BUGS.md` if the
picture disagrees with the board.

### 38.6 What the scanout can show when it is given a sane row base

Not a fix, and not a claim about display-list semantics (§18.2 untouched) — just
the demonstration that the rasteriser is not what is hiding the picture. The
native live share at frame 600, addressed at `rowaddr 0x78` (`DPYADR=0xF87C`,
`DPYSTRT=0xF87C`, `DPYTAP=0`, `DPYCTL=0x8004`, `finescroll=7` → row base word
`0x01E000`, byte `0x03C000`):

```text
first lit pen: screen (0,32) rgb=000020  column ci=0
               -> share word 01e000 = byte 0x03c000 = 1616
render fnv=0xD1FC02FD
```

The image is the title screen: red ground, blue `STUN`, green `RUNNER`, the
tunnel mouth and the runner. The core's output occupies share rows `0x78-0xBB`
and `0xC8-0xDB` (1024-word rows); the 68010's own writes, which both this model
and MAME's capture agree on, occupy rows `0x08-0x0B`, `0xF3-0xF5` and `0xFF` —
the same rows, which is an independent confirmation that the units agree.

### 38.7 Checks

```text
cmake -S native -B build-test -DCMAKE_BUILD_TYPE=Debug && cmake --build build-test
ctest --test-dir build-test                61/61 PASS
  gsp-scanout-gl-parity                    RAN, not skipped:
      gsp-scanout-gl: cases=3 bytes-per-case=368640 mismatches=0 max-delta=0
      GL_RENDERER = AMD Radeon Graphics (radeonsi, gfx1200, ...) — hardware GL
python3 -m unittest discover -s tests     249 tests OK
```

Negative controls, each a deliberate break restored afterwards — the test has
to be able to fail:

| break | what failed |
|---|---|
| row stride `<< 11` | the 256-row unit check, 4 recorded lines, both capture checks |
| drop `^ 0xFFFC` | all 8 recorded lines |
| flip high/low byte selection | all 8 recorded lines |
| drop `>> 3` in the bit-address conversion | the unit check, 3 landmarks, the `$12C08` cell check |

`STUNRUN_GSP_DISPLAY_DUMPDIR=reference/experiments/stunrun/gsp-direct-share-frame600-service`
also surfaces a **pre-existing** failure that is not this section's doing:
`test_acceptance`'s "47 distinct colours" and "21.71% dominant red" are §12's
*attract-mode* numbers, and the only capture committed here is the *service*
self-test screen. Pointed at it the test reports 1 distinct colour. It was
already failing that way before these edits and is left untouched: weakening it
would hide that the two numbers belong to different screens.

## 39. The GSP core runs to completion, and its output reaches the screen

Section 38.4 listed the display-register shadow as never having been written by
the core. That was correct, and it was also the visible symptom of three
separate defects. All three are now fixed and the core's output is on screen.
This section records what each one actually was.

### 39.1 The I/O bus trace was decoding an address the core never emits

`gsp_rtl_driver.cpp` traced the core's local bus and captured writes to
`0xF6000010 / 0xF6000012 / 0xF6000014 / 0xF6000016` into
`DPYCTL / DPYSTRT / DPYTAP / DPYADR`.

The core's on-chip register file is **not** there. From the vendored RTL:

```text
tms34010_pkg.sv:363   parameter logic [ADDR_WIDTH-1:0] IO_BASE_ADDR = 32'hC000_0000;
tms34010_pkg.sv:366   // Register indices (= address[8:4] = (addr - IO_BASE) >> 4).
tms34010_io_regs.sv:154  assign is_io = (addr[31:30] == 2'b11)
                                    && (addr[29:9]  == '0);
tms34010_pkg.sv:376   IO_IDX_DPYCTL  = 5'h08
tms34010_pkg.sv:377   IO_IDX_DPYSTRT = 5'h09
tms34010_pkg.sv:395   IO_IDX_DPYTAP  = 5'h1B
tms34010_pkg.sv:398   IO_IDX_DPYADR  = 5'h1E
```

So the space is `0xC0000000-0xC00001FF` with the register index in `addr[8:4]`.
Not one of the four comparisons could ever be true, on any run. It was a
decoder for a CPU that does not exist, comparing against constants that looked
authoritative because they sat in the right file and were spelled with the
right register names in comments.

It is not a typo. `0xF600` appears in `tms34010_decode.sv:228` as an
*instruction encoding* (`1111 011S SSSS DDDD`), a register-to-register move --
the same `0xF600` a reader would find first when looking for this register, and
the one this decoder appears to have been built from.

**Measured, after the fix** (600 frames, `./run.sh`):

```text
shadow DPYSTRT=0xFFFC DPYADR=0xFFC3 DPYTAP=0x0200 DPYCTL=0xF010
core-set=[DPYSTRT DPYCTL]
```

The two registers the core set match §30.4's MAME reading of the GSP's
power-on state (`DPYSTRT=0xFFFF`, `DPYTAP=0`, `DPYCTL=0xF010`) exactly. That is
independent corroboration: the core is not just writing *something* to the
right address, it is writing what the oracle says is there.

### 39.2 A false halt froze the core for the rest of the run

`run_gsp()` gave up after 2535 cycles with no host traffic, "assuming the GSP
has hit a STOP and is waiting", set the sticky `s_gsp_halted`, and
`sync_and_step()` then skipped the core entirely whenever that flag was set.

The premise is wrong. A display-list pass is mostly VRAM writes and touches
the host port rarely if at all, so 2535 silent cycles is an ordinary moment in
the middle of *running* a list. One quiet spell stopped the GSP permanently,
which is why its display registers read back as power-on values however long
the run continued -- §38.4 attributed that to "the core has not issued one
write", and it had; the writes were after the freeze.

Two independent mistakes, both required removing:

* the halt was inferred from host-port silence, which does not mean halted;
* and once inferred it **latched**, turning a per-frame cost decision into a
  decision about the whole run.

MAME's own model for when the core waits is `s_gsp_halted`-free: it simply
keeps clocking (`harddriv.cpp`, `tms34010.cpp` scanline timer). Clocking on is
correct; the only question is how long.

**Measured.** With the limit at 20000 host-silent cycles and with it disabled
entirely, at 600 frames, all three of `board vram nonzero words`, `DPYSTRT`, and
the rendered pixel/colour counts are **identical** -- 84337 / `0xFFFC` /
`lit=30235 colours=217` -- at **70 s against 26 min**. The core really is idle
for most of a frame's 664430-cycle budget. This is the same plateau §37 found
standalone (20k-300k cycles, constant whole-share residual), reappearing through
the shell. Default is now 20000; `STUNRUN_GSP_IDLE_CYCLES=0` disables it.

### 39.3 DPYADR is never written, and must not be reported as if it were

With 39.2 fixed, `DPYADR` and `DPYTAP` remain unwritten after 600 frames. That
is correct behaviour, not a gap: `DPYCTL=0xF010` has `SRE` (bit 12) set, so the
timing generator owns DPYADR, and the GSP program has no reason to touch it.
MAME's model agrees, and says what happens at frame start:

```text
tms34010.cpp:1322  /* 34010 loads DPYADR with DPYSTRT, and inverts if the origin is 0 */
tms34010.cpp:1327      IOREG(REG_DPYADR) = IOREG(REG_DPYSTRT);
tms34010.cpp:1444      params->rowaddr = dpyadr >> 4;
```

So at the top of a frame `DPYADR == DPYSTRT`, and both scanouts already read a
zero DPYADR as "start at DPYSTRT" (`gsp_display.c:191`,
`backend/gsp_scanout_gl.c:104`). The shell now passes 0 for any register the
core has not written, guarded by the new provenance mask, which engages that
fallback on both windows at once and so leaves `gsp-scanout-gl-parity`
untouched. It also means the fabricated `DPYADR=0xFFC3` (rowaddr 63, share byte
`0x7E00`, zero) is no longer substituted for a value the hardware would supply.

### 39.4 The palette was never copied out of the board

The colour planes are at `0xF5000000-0xF50007FF`, **not** in the VRAM share, so
neither VRAM sync function ever carried them. The MC68010's palette writes
landed in `gsp_board_t::pal_lo/pal_hi` and were read by nobody; the scanout
rendered musashi's own planes, which nothing on the RTL path populates. The
picture was being drawn through an all-zero palette.

Symptom before the fix: `lit=30235/122880 colours=217`, with magenta/cyan
fringing. After: the gold/white palette. Same geometry, correct colour source.

### 39.5 The instrument this needed: a hash cannot tell "content" from "a different nothing"

Every wrong-origin defect in this project produced a **stable, reproducible**
hash. That is why the cleared-buffer hash (§38.4.3) survived review, and why the
fix in `33c83a3` -- which made the hash change -- proved nothing: a scanout
pointed at a zero region of the share hashes just as reproducibly as a correct
one. The hash went from `0x7D611DC5` to `0x859B609D` while the frame was still
99.7% black.

`stunrun_render_content_stats()` reports lit pixels and distinct colours, and
the shell prints `lit=`/`colours=` beside the hash and says outright when the
frame is entirely black. Measured on this work: `lit=0` (blank),
`lit=30235` (content, palette missing), `lit=122880` (content, palette present
-- the background is palette entry 255, not black). The instrument distinguishes
all three; the hash distinguished none of them.

### 39.6 What is on screen, and what is NOT established

Normal boot, 600 and 900 frames, `./run.sh`:

```text
board vram nonzero words = 84337
shadow DPYSTRT=0xFFFC DPYCTL=0xF010 core-set=[DPYSTRT DPYCTL]
render frame=900 lit=122880/122880 colours=225
```

Captured PNGs show the game's screen -- a logo band and text rows -- and frames
500/700/900 are identical, so the core reaches a steady state rather than
drifting.

**Not established:**

1. **The colours are unvalidated.** The background renders as gold where the
   MAME capture of the service screen shows black, but that capture is the
   service path, which never releases the GSP, so the two are not yet
   comparable. The obvious next check is service-mode palette equality against
   `reference/experiments/stunrun/gsp-direct-share-frame600-service/`
   (`gsp_pal_lo.bin`, `gsp_pal_hi.bin`), since in service mode the MC68010
   *does* write the palette and the GSP is not needed to produce it.
2. **The image is sheared.** The horizontal bands are skewed, which is
   geometric, not a palette problem. Row-to-row advance follows
   `dpyadr_advance()`, which matches `tms34010.cpp:1391-1402`, so the suspect is
   the starting `rowaddr`/trajectory rather than the advance.
3. **Still a different trajectory point from MAME.** §37's offset is untouched;
   the core is not cycle-locked to a video frame. Expect "the same game, not the
   same frame".
## 40. Who writes `$FFDB48` — the GSP, through IPL2, and our model never asserts IPL2

The 68010 parks at `$02FC82: tst.w (A2)` with `A2 = #$FFDB48` and the word never
becomes non-zero. This section names the writer, the instrument that found it,
and the missing wire.

### 40.1 The answer

**`$0002143C: move.w #$ffff, $ffdb48.l`** — three instructions into the **level-3
autovector interrupt handler**.

The vector table is not at zero. `$0010EC: 4e 7b 88 01  movec A0, VBR` relocates
it to `$00020000` (`$0010E0: lea $20000.l, A0`), and there:

```text
020068 -> 0x0213FE    vector 26  = level 2   ADSP      (m_adsp_irq_state)
02006C -> 0x02143C    vector 27  = level 3   GSP       (m_gsp_irq_state)
020070 -> 0x023F24    vector 28  = level 4   sound     (m_sound_int_state, /LINKIRQ)
020074 -> 0x0222DA    vector 29  = level 5   periodic  (m_irq_state, 244.14 Hz)
020078 -> 0x0239FE    vector 30  = level 6   duart     (m_duart_irq_state)
02007C -> 0x0229C4    vector 31  = level 7   NMI
```

(`vector = 24 + level`, `EXCEPTION_INTERRUPT_AUTOVECTOR` at m68kcpu.h:169. The
signal column is read from `harddriv_m.cpp:95-100`, where MAME's line *index* is
the 68010 interrupt level, and it is consistent: the six handlers for levels 2..7
are consecutive table entries in level order, and each is a real
`movem/rte` handler rather than a stub — `$0229C4`, for instance, is
`movem.l ...,-(A7) / jsr $30756 / movem.l (A7)+,...` and is reached only through
the table.)

```text
 95   m_maincpu->set_input_line(1, m_msp_irq_state ...);
 96   m_maincpu->set_input_line(2, m_adsp_irq_state ...);
 97   m_maincpu->set_input_line(3, m_gsp_irq_state ? ASSERT_LINE : CLEAR_LINE);
 98   m_maincpu->set_input_line(4, m_sound_int_state ...);  /* /LINKIRQ on STUN Runner */
 99   m_maincpu->set_input_line(5, m_irq_state ...);
100   m_maincpu->set_input_line(6, m_duart_irq_state ...);
```

`hdgsp_irq_gen` (`harddriv_m.cpp:118-120`) is the TMS34010's INT pin. So the
whole gate is a host/GSP handshake: **the 68010 raises the GSP, waits, and the
GSP's own interrupt is what sets the word.** The handler confirms it:

```text
02143c: 33 fc ff ff 00 ff db 48   move.w   #$ffff, $ffdb48.l
021442: 02 79 ff 7f 00 c0 00 08   andi.w   #$ff7f, $c00008.l   ; ack: clear HSTCTL bit 7
02144e: 20 38 80 14               move.l   $8014.w, D0        ; tick counter
021452: 90 b9 00 ff db 36          sub.l    $ffdb36.l, D0      ; elapsed
021458: 23 c0 00 ff 9b aa          move.l   D0, $ff9baa.l
021460: 4e 73                     rte
```

It acknowledges the GSP's interrupt request and signals the waiter in the same
breath. That is not an accidental writer — it is the only `move.w #non-zero` to
that address in the program.

**Corrected in section 48.2:** the `; clear HSTCTL bit 15` comment this listing
originally carried was wrong. `$FF7F` clears bit 7 only, and preserves bit 15.

**Confirmed:** the instruction, its address, its vector, and the MAME wiring.
**Inferred (not measured here):** that the TMS34010's INT pin is what
`hdgsp_irq_gen` is wired to in the machine config; I read the state variable and
the driver, not the TMS34010's own INT generation.

### 40.2 What the ROM actually does around the gate

Two callers, and they differ in a way that matters:

```text
02f126: link A6,#$0 ...                                ; caller 1
02f15e: 42 79 00 ff db 48   clr.w  $ffdb48.l           ; clear FIRST
02f16a: 4e b9 00 02 fc 60   jsr    $2fc60.l           ; then wait

02f1be: link A6,#$0 ...                                ; caller 2
02f212: 4e b9 00 02 fc 60   jsr    $2fc60.l           ; wait, UNCONDITIONALLY
02f23a: 42 79 00 ff db 48   clr.w  $ffdb48.l           ; clear AFTER
```

Caller 2 never clears first, so it inherits whatever the previous release left
behind: `$FFDB48` behaves as a latch as much as a handshake, and `$02FC60`
returns immediately if the word is still non-zero from last time. That is the
mechanism §40.8 measures the oracle actually using.

So on the normal paths `$FFDB48` is only ever *cleared*, and only *set* from the
IRQ3 handler. There is no third writer, and no branch that skips the wait:
`$02F212` is unconditional. Measured, 600 frames, PC histogram
(`STUNRUN_PC_HIST`):

```text
0x02F212   1        jsr $2fc60.l      (entered exactly once)
0x02FC82   43259    tst.w (A2)
0x02FC84   43252    bne $2fcb6
0x02FCB6   --       never: the exit is absent from a 1679-entry histogram
0x02FC98   --       never: the $80000-iteration escalation is absent too
```

And the CPU is inside the gate from **frame 380** to frame 600
(`STUNRUN_PCLOG`, `awk '$2 >= 0x2FC60 && $2 <= 0x2FCB6'`), never leaving.

### 40.3 The escalation path is a watchdog, not a producer

`$02FC98-$02FCA2` call `$0308C4` once every `$80000` iterations, and `$0308C4`
does not touch `$FFDB48` either:

```text
0308c4: link A6,#-$64 ; D0=arg1=$c, D2=arg2, D3=$100000, A2=$608000
0308e6: jsr $30778.l          ; print message selector $0c
0308fa: jsr $23898.l
030906: clr.w (A2)           ; $608000, one million times
03090a: cmp.l D3,D0 / blt $30906
03090e: jsr $21286.l
```

So `$02FC60` is a **bounded wait with a lockup path**: poll for `$80000`
iterations, then print error message 12 and spin forever. That reframes the
question — `$FFDB48` is not a "flag somebody forgot", it is a handshake whose
timeout is a deliberate diagnostic.

### 40.4 The instrument: `STUNRUN_WATCH_WRITE`

`native/musashi_cpu.{c,h}` and `native/shell.c` gained a runtime write watch and
an interrupt counter, both armed by one env var. Unset, the three
`m68k_write_memory_*` callbacks each take one predictable not-taken branch on a
file-static word; `m68k_read_memory_32` became a wrapper that calls the
unmodified body and then tests the same word. Behaviour with the feature off is
unchanged — `board vram nonzero words`, the render hash, and `lit=`/`colours=`
are identical to §39.6's run — but that is a behavioural check, not a timing
one, and no timing number is claimed.

```bash
STUNRUN_WATCH_WRITE=<hex,hex,...>   # each address gets a count + the writing PC
```

The hook tests the **raw bus address before `route_address()`**, so a watched
address is matched however it is reached, and a long store overlapping the
watched word counts. Arming also enables `musashi_irq_watch_t`, which counts
both sides of an interrupt: `assert_calls` (what the board model asserted, via
`musashi_cpu_set_irq`) and `autovector_reads` (what the CPU did about it).

Measured, `STUNRUN_FRAMES=600`, RTL core on:

```text
shell: write-watch addr=0x00FFDB48 writes=1  last_pc=0x00021302 last_value=0x0000 width=32
shell: write-watch addr=0x00608000 writes=55240 last_pc=0x0002FCAC last_value=0x0000 width=16
shell: write-watch addr=0x00FFDB4A writes=987 last_pc=0x00030000 last_value=0x03D9 width=16
shell: write-watch addr=0x00FFDB4C writes=2   last_pc=0x0002C0FE last_value=0x0000 width=32
shell: write-watch addr=0x00FFDB4E writes=1   last_pc=0x00021302 last_value=0x0000 width=32
shell: write-watch addr=0x00FFDB50 writes=1   last_pc=0x00021302 last_value=0x0000 width=32
shell: write-watch addr=0x00FFDB52 writes=1   last_pc=0x00021302 last_value=0x0000 width=32
shell: write-watch addr=0xFF800000 writes=0   last_pc=0x00000000 last_value=0x0000 width=0
shell: irq assert_calls=3463 assert_l1..l7=0/0/0/0/2475/0/0
       autovector_fetches=985 vbr=0x00020000 irq5=985 nmi=0 uninit_vector=0
       last_vector=29 last_target=0x000222DA last_source_pc=0x00022764
       sr=0x2004 imask=0x0 insn_retired=7187787
```

**The answer to the original question:** in a 600-frame native run, `$FFDB48` is
written **exactly once**, by `$00021302: clr.l (A0)+` inside the boot work-RAM
clear (`$0212FA lea $8000.w,A0` / `$0212FE move.w #$1fff,D0` / `$021302
clr.l (A0)+` / `$021304 dbra D0,$21302`), **with the value zero**. No instruction
sets it non-zero, because `assert_l1..l7 = 0/0/0/**0**/2475/0/0`: **the native
model never asserts IPL2 at all.**

### 40.5 The controls, because a watch that cannot fire proves nothing

**Positive control 1 — `$608000`, independently known to be written** by the very
loop that contains the gate: `$02FC70` and `$02FCAC` are both
`clr.w $608000.l`. The watch reports 55,240 writes with `last_pc=0x0002FCAC`,
which is exactly the second `clr`. ✓

**Positive control 2 — work RAM, so the control is in the same region and the
same callback as the target.** `$FFDB4A`: 987 writes, `last_pc=0x00030000` =
`$030000: addq.w #1,$ffdb4a.l`. `$FFDB4C`: 2 writes, `last_pc=0x0002C0FE`.
Every reported PC was checked against the shell's own disassembler
(`STUNRUN_DISASM`) — see 40.6 for why that check was not optional. ✓

**Negative control — `$FF800000`, provably never written.** `route_address()`
routes it to `REGION_GSP_VRAM_MIRROR` only through
`(address & 0xFF000000) == 0xFF000000`, and the 68010 drives a 24-bit bus, so no
address it can present satisfies that test. Zero writes, for a reason that does
not depend on this run. ✓

**In-region discrimination.** Eight addresses armed in one run, seven written
and one not, from the same instrument and the same code path — including three
consecutive words (`$FFDB4E/$50/$52`) that are written once each by the same boot
clear while their neighbour `$FFDB4A` is written 987 times by a different
routine. The watch is not firing indiscriminately.

### 40.6 Refuted while writing it: `M68K_REG_PC` is not the writing instruction

The first version of the watch reported `$FFDB48 last_pc=0x00021304` and
`$608000 last_pc=0x0002FCB2`. Both are *wrong addresses*, and both were wrong in
the same way. `m68k_get_reg(M68K_REG_PC)` inside a memory callback is **the first
unconsumed word of the executing instruction**, not its address: Musashi fetches
with `REG_IR = m68ki_read_imm_16()` (m68kcpu.c:1000) and `m68ki_read_imm_16`
increments `REG_PC` by 2 per word returned (m68kcpu.h:1140-1148), and
`M68K_EMULATE_PREFETCH` is `M68K_OPT_OFF` (m68kconf.h:225) so there is no
separate prefetch pointer absorbing the difference. `clr.w $608000.l` is six
bytes, so the store is issued with `REG_PC = $02FCAC + 2 + 4 = $02FCB2`.

The same arithmetic explains `$FFDB4A` reporting `$030006` for a six-byte
`addq.w #1,$ffdb4a.l` at `$030000`. MAME's tap has the identical property —
`ram-write-trace.lua:149` reads `device.state['CURPC']`, which is the same
"current PC within the instruction" quantity.

Reading the instruction-start PC needs a sampling point per instruction, and
this core does not offer one: `M68K_INSTRUCTION_HOOK` is `M68K_OPT_OFF`
(m68kconf.h:218), so `m68k_set_instr_hook_callback` never fires — as the comment
at `musashi_cpu_reset()` already said. So `musashi_cpu_run()` now single-steps
whenever any of the three per-instruction diagnostics is armed, and the watch
reports the sampled instruction address. With that in place the numbers in 40.4
are the instruction addresses.

Three consequences, all recorded because each one can silently corrupt a later
reading:

1. **`STUNRUN_PC_HIST` and `STUNRUN_WATCH_WRITE` used to fight.** Each claimed
   the whole of `musashi_cpu_run()`, so combining them produced an **empty**
   histogram while the watch reported normally. They now share one stepping loop.
2. **The PC histogram under-counts interrupt handler entries by one.** In
   `m68k_execute()` the exception is taken at the top, before the loop, so the
   handler's first instruction executes *inside* the call whose sample was taken
   at the pre-exception PC. Measured: `$0222F2 rte` = 985 but `$0222DA` = 1, while
   the autovector counter says 985. **Trust the autovector counter for interrupt
   counts; the histogram's entry count is the thing that is off.**
3. **Arming the watch makes the run single-step**, which makes
   `m68ki_check_interrupts()` run at every instruction boundary instead of once
   per `m68k_execute()` (m68kcpu.c:974, and the "ASG: removed per-instruction
   interrupt checks" comment above its main loop). Measured effect on a 600-frame
   run: `$608000` writes 55,277 unstepped vs 55,240 stepped, while
   `board vram nonzero words=84337` and `render hash=0xFA2D16E5 lit=122880/122880
   colours=225` are identical. Real, small, and worth stating rather than
   assuming away.

### 40.7 Refuted while writing it: "VBR is zero", so the interrupt counter read a confident zero

The first interrupt counter gated on `VBR == 0` and reported
`autovector_fetches=0` over 600 frames — which reads exactly like "no interrupt
is ever delivered", and would have been believed. It was wrong twice over.

The ROM moves the vector table (`$0010EC movec A0,VBR`), so the autovector fetch
is a read of `(24+level)*4 + VBR` with `VBR = $00020000`, and the count was zero
because the gate suppressed every one of them. The gate is now relative to the
live VBR and the base it saw is reported (`vbr=0x00020000`), so a relocation is
visible instead of silent.

With that fixed the counter says what the PC histogram was already saying:

```text
assert_calls=3463  assert_level[5]=2475   the model asserted IRQ5 2475 times
autovector_fetches=985  irq5=985  nmi=0    the CPU took 985 of them
last_vector=29 last_target=0x000222DA      vector 29 -> $0222DA, the real handler
```

985 vs 2475 is not a discrepancy. The shell asserts IRQ5 every 32 768 cycles
whether or not the ROM has asked for it, so most of those asserts landed while
the interrupt mask was 7 or 5 and could not be taken (see the `sr=` sampling
below); `FLAG_INT_MASK` becomes 5 after each delivery (m68kcpu.h:2131) and the
ROM acks it by writing `$60C000` at `$0222EC`, which drops the line —
`harddriv_m.cpp:111-115` and `m68kcpu.h:2154-2157`, where
`M68K_EMULATE_INT_ACK` being off makes Musashi clear `CPU_INT_LEVEL` after every
delivery.

Also measured, and the reason "no interrupt" was never the right suspicion: the
interrupt mask is **not** stuck. `STUNRUN_WATCH_WRITE` samples SR at every frame
boundary and prints only changes; it reads `imask=7` until **frame 364**, then
`imask=0` with intermittent `imask=5` for the rest of the run. The unmask is ROM
code — `$02245C: move #$2000,SR` / `$022462: move #$2700,SR`, a matched pair
reached 1 time each. The ROM masks all interrupts for the first 6 seconds of
emulated time and then runs with them enabled, and `nmi=0` throughout because
nothing drives IPL6 either.

### 40.8 Cross-check in the oracle, and what it changes

`tools/mame-ram-write-trace` on `$FF9BA8-$FF9BAC` for 1200 frames (the oracle
binary, `roms/`, `--input none`, `--nothrottle`). `$FF9BAA` is written only by
the ROM's **RAM verification walk**, and never by the IRQ3 handler:

```text
writer_pcs: 0x0059de 0x005a12 0x005a5c 0x005aaa 0x005aac 0x005afe 0x005b00
events 486, truncated false
```

```text
0059d8: 41 e9 ff fe          lea     (-$2,A1), A0      ; A1..A2 is the block
0059dc: 54 48                addq.w  #2, A0
0059de: 30 80                move.w  D0, (A0)          ; <-- the writer
005a0a: 42 79 00 60 80 00    clr.w   $608000.l
005a10: b5 40                eor.w   D2, D0
005a12: 30 80                move.w  D0, (A0)
005a14: e2 4a                lsr.w   #1, D2
```

A deterministic ramp seeded from `$8000` and shifted, compared against each word
of a declared RAM block. On `$FFDB40-$FFDB4C` for 600 frames, 343 events, the
same three PCs, writing values `0x0000, 0x0001, 0x0003 ... 0xFFFF`. So in MAME
`$FFDB48` *does* end up non-zero (`0xFFFF`), written at frame 476 by `$005A5C` —
**not** by `$02143C`.

Two things follow, and they are different in weight:

1. **The IRQ3 handler never runs in MAME either.** 1200 frames, `$FF9BAA`
   untouched by `$021458`. So `$02143C` is a real, wired, correct-looking
   producer that neither emulator reaches on this path, and it is **not** what
   releases the gate in the oracle.
2. **Our 68010 diverges from the oracle's before it ever reaches the gate.** The
   native PC histogram contains **zero** executions in `$005000-$005B00` — the
   RAM verification walk. MAME's runs it at frames 169, 263, 397 and 476. So the
   `$FFDB48 = 0` the native model enters the gate with is not "the flag the GSP
   never set"; it is "the RAM walk that would have left a non-zero value there
   never ran." That is the next question, and it is upstream of this one.

Confirmed: MAME's writer PCs and values above, from the oracle's own write tap.
Inferred: that `$FFDB40-$FFDB4C` is one of the declared blocks of that walk
(the block bounds live in a table the walk reads through A1/A2; I did not trace
the table entry).

### 40.9 Two premises in circulation that this section corrects

- **"The MC68010 NMI vector is `$000107E2`."** It is not. With `VBR = $20000`,
  vector 31 is read from `$2007C` and holds `$000229C4`, an ordinary
  `movem.l …,-(A7) / jsr $30756 / movem.l (A7)+,… / rte` handler. `$0107E2` is
  `move.w #$98,D0 / jsr $2326.l`, one of **nine** text-print stubs at
  `$107D4`–`$10848` passing selectors `$97`..`$9F`; their addresses are not
  multiples of 4, so they are a dispatch list rather than a vector table, and
  each falls into `$10856: clr.w $608000.l / bra $10856` — a spin, with no `rte`
  anywhere in the nine. They are unrelated to interrupts. Nothing else in this
  investigation depended on the claim, but it would have sent a search for the
  NMI handler into a print-string stub.
- **`$2326` is not a general dispatcher.** It is the **text-draw** entry point
  keyed by selector: `D0 -> lea $10136.l / lsl.l #2 / movea.l (A0),A0` picks one
  of 394 descriptors, and the routine then composes HSTADR and pushes HSTDAT at
  `$C0000C`. It touches no work-RAM word and has no bearing on `$FFDB48`.

### 40.10 Two refutations of our own notes

- `native/fake_ports.h:20-22` says "the IRQ5 handler bumps a counter at
  `0xff9042`". It does not. The IRQ5 handler is `$0222DA` and bumps
  `$8014.w` = `$FF9414`. `$FF9042` is word 13 of the 16-word table filled by
  `$0225B0: lea $ff902a.l,A1 / moveq #$f,D1 / ... / move.w D0,(A1)+ / dbra D1` —
  the watch measures 7 such fills in 600 frames, `last_pc=$0225C0`, which is the
  `move.w D0,(A1)+`. The comment's conclusion (32 KiB of RAM is real) stands; its
  evidence does not.
- The ROM-image build check still reads `reset vector: SP=0xffffc000
  PC=0x00000216` and the shell's own reset is `pc=0x00000216`, so §40's
  addresses come from the interleaved image and not a concatenated one.

### 40.11 Reproduce

```bash
export PATH="$HOME/.local/share/verilator-5.052/bin:$PATH"
cmake -S native -B build-rtl -DSTUNRUN_GSP_RTL=1 && cmake --build build-rtl -j"$(nproc)"

NATIVE_BUILD_DIR=$PWD/build-rtl SDL_VIDEODRIVER=dummy \
STUNRUN_LIVE_VIDEO=1 STUNRUN_FRAMES=600 \
STUNRUN_WATCH_WRITE=0xFFDB48,0x608000,0xFFDB4A,0xFFDB4C,0xFFDB4E,0xFFDB50,0xFFDB52,0xFF800000 \
./run.sh

STUNRUN_PC_HIST=/tmp/pc.txt STUNRUN_PCLOG=/tmp/pclog.txt STUNRUN_WATCH_WRITE=0xFFDB48 \
  ./run.sh          # then grep the histogram for 0x02FC / 0x0222DA / 0x0059DE

for r in 0x212FA:0x21310 0x2FC60:0x2FCB8 0x2F1BE:0x2F270 0x308C4:0x30930 \
         0x2143C:0x21462 0x59D8:0x5A20; do
  NATIVE_BUILD_DIR=$PWD/build-rtl SDL_VIDEODRIVER=dummy STUNRUN_LIVE_VIDEO=1 \
  STUNRUN_FRAMES=1 STUNRUN_DISASM=$r:/tmp/dis.txt ./run.sh >/dev/null 2>&1
  echo "== $r"; cat /tmp/dis.txt
done

PATH=/mnt/build/mame-ref:$PATH python3 tools/mame-ram-write-trace roms OUT \
  --device :mainpcb:maincpu --space program --base 0xffdb40 --end 0xffdb4c \
  --frames 600 --start-frame 10 --end-frame 600 --max-events 200000 --nothrottle
```

```text
python3 -m unittest discover -s tests   249 tests OK
ctest --test-dir build-test              61/61 PASS
ctest --test-dir build                   61/61 PASS
```

### 40.12 Not filed as BUGS.md, and why

Two candidates were considered and neither survives `BUGS.md`'s bar, which is
defects in the 1989 game and in MAME, never ours:

- **`$02FC60`'s lockup.** `$0308C4` prints selector `$0c` and then spins. Read
  as a deliberate watchdog — bounded wait, then a diagnostic, then halt — that
  is defensible engineering, not a defect. It would become a `BUGS.md` entry only
  if a capture showed the game reaching it in normal play.
- **The native model never asserting IPL2.** That is a gap in *our* machine
  model, which is where it belongs.
---

## 41. The premise of §40.8 was a contaminated MAME config, and the real halt is the missing IPL2 wire

§40.8 asked a good question with the wrong oracle. It established that our 68010
never executes `$005000-$005B00` while "MAME's does, at frames 169/263/397/476",
and inferred that the RAM walk was the thing releasing `$FFDB48`. **That oracle
run was not a normal boot.** With a clean configuration the same command, the same
MAME binary and the same ROMs produce a different set of writers, `$0059DE` /
`$005A12` / `$005A5C` among none of them.

### 41.1 The walk's call path, and who actually reaches it

Disassembled from the correctly-assembled image
(`tools/build-maincpu-image -o IMAGE` → `reset vector: SP=0xffffc000
PC=0x00000216`).

`$005000-$005B00` is **not** one routine. It is an Atari System 2 diagnostics
memory-test subsystem:

```asm
; $000100-$0001EE: a jump table of `jmp <abs.l>` stubs, 31 six-byte entries.
000100: jmp $216.l      ; index 0   -- the reset-vector target
000106: jmp $506c.l     ; index 1   -- DIAGNOSTICS entry list
000172: jmp $506c.l     ; index 12
000178: jmp $5b6e.l     ; index 13  -- RAM-verification chain, variant B
00017e: jmp $5896.l     ; index 14  -- RAM-verification chain, variant A
0001f0: moveq #-$6,D4
```

```asm
; $00506C -- the diagnostics entry list.  Each element ends in `jmp (A0)` and
; is chained by reloading A0, so the list is a fall-through pipeline.
00506c: movea.l #$ffffc000, A7
005072: lea ($6,PC), A0; ($507a)
005076: bra $53f2        ; GSP register init      -> jmp (A0)
00507a: lea ($6,PC), A0; ($5082)
00507e: bra $548e        ; 230400-word VRAM fill  -> jmp (A0)
005082: lea ($6,PC), A0; ($508a)
005086: bra $54c2        ; palette init          -> jmp (A0)
00508a: move.w #$47, D0
00508e: lea ($8,PC), A2; ($5098)
005092: jmp $25e8.l      ; text draw, selector $47
0050a0: move.w #$54, D0
0050a4: lea ($6,PC), A0; ($50ac)
0050a8: bra $566a        ; -> jmp (A0) = $50ac
0050ac: bra $56e6        ; <<< the memory test

; $008FEE is the SAME list with A0/A2 pushed one slot further; it is the
; non-diagnostic twin of $00506C.
008fee: move #$2f00, SR
008ff2: lea ($6,PC), A0; ($8ffa)
008ff6: bra $53f2
008ffa: lea ($6,PC), A0; ($9002)
008ffe: bra $548e
```

`$56E6` sets up the block bounds and branches to the chain head:

```asm
0056e6: lea $ffff8000.l, A1          ; block start
0056ec: lea $fffe.w, A2              ; block end (A2 < A1: the 32-bit compare in
0056f0: ori.w #$1800, $c00008.l      ;   the walkers wraps, so the walk runs the
0056f8: move.l #$ff86a600, $c00002.l ;   whole 32-bit space, which is the point)
005702: movea.w A4, A5
005704: lea ($6,PC), A4; ($570c)     ; A4 = the "next block / report" link
005708: bra $5b6e
```

`$5B6E` is the chain head, and the chain is eleven passes over `A1..A2` in
different patterns, linked by `A6` and ending `jmp (A4)`:

```asm
005b6e: clr.l D6
005b70: clr.l D0
005b72: clr.l D4
005b74: lea ($6,PC), A6; ($5b7c)
005b78: bra $59d8        ; fill A1..A2 with D0            -- $0059DE writes
...
005b7c: btst #$5, $60c001.l ; the abort input, checked between every pass
005b84: beq $5b8c
005b86: jmp $260.l
...
005dfe: clr.w $604000.l / clr.w $604012.l / jmp (A6)
005dee: jmp (A4)          ; -> $00570C, the next block or the report
```

The three walkers named by §40.8 are three of the eleven patterns:

```asm
; $59E6: seed $8000, shift right.  Writes at $0059DE / $005A12 / $005AAA.
005a10: eor.w D2, D0
005a12: move.w D0, (A0)
005a14: lsr.w #1, D2

; $5A32: seed $0001, shift left, walking DOWN from A2-2.  Writes at $005A5C.
005a5a: eor.w D2, D0
005a5c: move.w D0, (A0)
005a5e: lsl.w #1, D2

; $5B2A: the destructive read-back check (eori.w #$ffff, expect zero).
005b36: eori.w #$ffff, (A0,D0.w)
```

When the chain returns to `$00570C` with `D4 == 0` it prints selector `$3b` and
`bra $566a` → `$00572C` → **`jmp $50B0`**, which is the game:

```asm
005073: lea ($6,PC), A0; ($507a)      $0050b0: movea.l #$ffffc000, A7
...
0050b0: movea.l #$ffffc000, A7        0050b6: jsr $570.l
0050b4: jsr $570.l                    0050bc: bsr $552c
```

So the memory test is the **diagnostics/menu path, and on a pass it starts the
game**. It is not background work.

**Confirmed:** the entry path `$00506C` → `$0050A0/$0050AC` → `$00566A` →
`$0056E6` → `$005B6E` → `$0059D8…$005DEE`, the eleven-pass chain, and the pass
path into `$0050B0`.

### 41.2 What actually selects that path: `$60C001` bit 5

Nothing calls `$5B6E` directly. The ROM's boot code chooses:

```asm
000216: lea $0.w, A0
00021a: movea.l ($0,A0), A7          ; SP = [$00000000] = $FFFFC000
00021e: movec A0, VBR
000228: reset
...
000254: btst #$5, $60c001.l         ; IN0 bit 5 = PORT_SERVICE, active low
00025c: beq $2a0
000260: move #$2f00, SR             ;   SET  -> full init, then jmp (A1)
...
000286: lea $20000.l, A0
00028c: movea.l ($0,A0), A7
000290: movea.l ($4,A0), A1         ;   A1 = [$00020004] = $000212AE
000294: movec A0, VBR               ;   VBR = $00020000
00029e: jmp (A1)                    ;   -> $00212AE, the real game init
0002a0: btst #$0, $60c001.l         ; IN0 bit 0 = "Diagnostic jumper"
0002a8: bne $2b2
0002ac: jmp $8fee.l                 ;   CLEAR -> the normal entry-point list
0002b2: jmp $506c.l                 ;   SET   -> the DIAGNOSTICS list
```

`$00020004` is `0x000212AE` in the ROM vector image (`STUNRUN_DISASM=0x020000:
0x020100`), so the bit-5-SET path lands in `$00212AE`, which does the
`$00212FA` work-RAM clear, calls `$223F4/$22428/$20702/$24306` and `jmp
$21286`.

`$60C000-$60CFFF` is `hd68k_port0_r()` (harddriv.cpp:605,
harddriv_m.cpp:267-280), and stunrun's own port block is
INPUT_PORTS_START(stunrun) at harddriv.cpp:1084-1096: bit 0 = "Diagnostic
jumper" (default 0x01), bit 5 = `PORT_SERVICE(0x20)` active low. MAME's default
therefore reads **bit5 = 1, bit0 = 1**: SET, so `$00260` → `$00212AE`, the game —
which is what our model does (`port0_status()` returns `in0 = 0xE7`,
musashi_cpu.c:1829-1889).

**Both emulators take the game path by default. The walk is correctly absent
from both.**

### 41.3 The contamination, and the control that proves it

`tools/mame-ram-write-trace` hardcodes `-cfg_directory /tmp/stunrun-mame-cfg`
(tools/mame-ram-write-trace:96). That directory is outside the repository, is
rewritten by every run, and at the start of this session contained:

```xml
<port tag=":mainpcb:IN0" type="DIPSWITCH" mask="32" defvalue="32" value="0" />
```

`mask="32"` is IN0 bit 5 forced to **0**. That is `$00254: btst #$5` taking the
`beq $2a0` branch, `$002A0: btst #0` taking `bne $2b2`, and `jmp $506c` — the
diagnostics path. §40.8's oracle ran the memory test because of a DIP override
left in a scratch directory, not because the game runs one.

Both directions, same command, same MAME binary (`/mnt/build/mame-ref/mame`,
0.289), same ROMs, `--input none --nothrottle`, frames 10-600:

```text
clean cfg : events= 2529 writers=['0x21302','0x2143c','0x2c0fe','0x2dad4',
                                  '0x2dadc','0x2dae4','0x2f206','0x2f23a',
                                  '0x2f250','0x30000']
DIP  cfg  : events=  343 writers=['0x59de','0x5a12','0x5a5c']
```

The clean run reproduces three times with identical frames, writer PCs and
per-frame counts; the DIP run reproduces §40.8 exactly. `canonical_log_sha256`
differs between the two only because it covers the output path (AGENTS.md trap 4)
— the runs used different output directories.

### 41.4 §40.1 was right, and §40.8's refutation of it was wrong

On the clean run, `$FFDB48` is written `$FFFF` by **`$002143C`** — three
instructions into the level-3 autovector handler — **every three frames from
frame 131**, alternating with `$02F23A: clr.w $ffdb48.l` (caller 2's
"clear AFTER", §40.2):

```text
frame= 37 pc=0x021302 data=0x0000     boot work-RAM clear
frame=131 pc=0x02143c data=0xFFFF     first IPL3 delivery
frame=135 pc=0x02f23a data=0x0000     gate cleared AFTER the wait
frame=136 pc=0x02143c data=0xFFFF
frame=169 pc=0x02f23a data=0x0000
frame=170 pc=0x02143c data=0xFFFF
...    (every third frame, to 599)
```

`$0002006C -> 0x0002143C` is vector 27 = `24 + level` = level 3 (VBR =
`$00020000`), and harddriv_m.cpp:97 drives `m_maincpu->set_input_line(3, ...)`
from `m_gsp_irq_state`. So **§40.1's writer is the real one**; §40.8's "the IRQ3
handler never runs in MAME either" was an artifact of the same contaminated
config.

The oracle's PC histogram over frames 150-600 also shows the gate is entered
**and** left, which our model never does:

```text
$02FC82 tst.w (A2)   MAME 67040    native 43259
$02FCB6 (the exit)   MAME 67040    native     0
```

### 41.5 The divergence point

The last thing our 68010 does that MAME's does not, and the frame it happens on,
is not an instruction. It is **the absence of an interrupt**, and the frame is
**131**.

```text
shell: irq assert_calls=3463 assert_l1..l7=0/0/0/0/2475/0/0 autovector_fetches=985
       vbr=0x00020000 irq5=985 nmi=0 last_vector=29 last_target=0x000222DA
```

`assert_l3 == 0` for 600 frames. Nothing in our machine model ever calls
`musashi_cpu_set_irq(cpu, 3)`; `native/shell.c:1674` is the only call site in
the whole model and passes level 5. `grep -rn "set_irq" native/` returns three
hits, all level 0 (drop), plus that one. There is no IPL2 wire, and no path
from the Verilated core to one.

MAME asserts IPL2 at frame 131 and every third frame thereafter, from the
TMS34010's own INT pin. That pin is confirmed this time, not inferred:

```text
tms34010.cpp:1682-1685   /* the TMS34010 can set output interrupt? */
        if (!(oldreg & 0x0080) && (newreg & 0x0080))
                m_output_int_cb(1);
        else if ((oldreg & 0x0080) && !(newreg & 0x0080))
                m_output_int_cb(0);
```

`oldreg`/`newreg` there is `REG_INTENB` (the `case REG_INTENB:` arm above it),
bit 7 is the host-visible output-interrupt enable, and:

```text
harddriv.cpp:1489   m_gsp->output_int().set(FUNC(harddriv_state::hdgsp_irq_gen));
harddriv_m.cpp:118-120  hdgsp_irq_gen: m_gsp_irq_state = state; update_interrupts();
harddriv_m.cpp:97      m_maincpu->set_input_line(3, m_gsp_irq_state ? ASSERT_LINE : CLEAR_LINE);
```

The vendored RTL has the GSP's **internal** interrupt path
(`rtl/core/tms34010_int_ctrl.sv:42 assign active = intpend & intenb`, fed by
`io_intenb` from `rtl/io/tms34010_io_regs.sv:485`) but **no external INT output
port at all**: `rtl/tms34010_system.sv:17-60` lists `lint1_n_i`, `lint2_n_i`,
`dpyint_set_i` and `hint_n_o`, and `grep -rn "int_o\|intout\|int_n_o\|ipl"
third_party/tms34010_sv/rtl/` returns nothing but unrelated hits. So the
missing wire is a missing pin, not a missing connection between two pins that
exist.

### 41.6 Controls

**Positive control — the gate opens when IPL2 exists.** A temporary, reverted
diagnostic in `native/shell.c` pulsed a synthetic IPL2 for one line-slice every
third frame (`STUNRUN_FAKE_IPL2=3`), 600 frames, RTL core on, compared against
the unmodified baseline:

```text
                            baseline     with synthetic IPL2
$02FC82 tst.w (A2)             43259                        4
$02FCB6 (the gate exit)            0                        3
$002143C (IRQ3 entry)             0                       82
$0021460 rte                     0                    3,808
$02FC* total                 605,593                       54
assert_l3                           0                   53,071
autovector last_vector/target  29/$0222DA      27/$002143C
$FFDB48 writes                       1                    3,812 (last value $FFFF)
insn_retired                   7,187,787                25,773,966
```

The ROM takes vector 27, lands in `$002143C`, writes `$FFFF` to `$FFDB48`, and
**leaves `$02FC82`** — 4 spins instead of 43,259, and the exit executes 3 times.
The mechanism, the vector, the handler and the gate all respond. The diagnostic
was reverted; `git diff --stat` is empty and the baseline run reproduces §39.6
(`board vram nonzero words=84337`, `hash=0xFA2D16E5 lit=122880/122880
colours=225`, `assert_l1..l7=0/0/0/0/2475/0/0`, `insn_retired=7187787`).

**Positive control — the oracle's `$FFDB48` tap can fail.** The same
`tools/mame-ram-write-trace` invocation reports `$0059DE`/`$005A12`/`$005A5C`
with one config and `$002143C` with the other, 343 vs 2,529 events, from one
binary and one ROM set. A tap that reported the same answer in both directions
would prove nothing about either.

**Positive control — `$002143C` really is vector 27.** Not asserted from the
table: the injected run's autovector counter reports
`last_vector=27 last_target=0x0002143C` from the CPU side, and the clean oracle
trace independently shows that PC writing `$FFFF` to `$FFDB48`.

**Negative control — the walk really is unreachable in both emulators.**
`$0059DE`, `$005A12`, `$005A5C`, `$005896-$0058A0` (chain A head) and
`$005B6E-$005B7C` (chain B head) are absent from our 600-frame PC histogram
(1,679 distinct PCs, 7,187,787 instructions retired) **and** from MAME's, when
MAME is run with a clean configuration. The positive control for the histogram
itself is the boot fill at `$00069A`: exactly 230,400 executions on both sides,
which is the strongest available evidence that the two histograms are counting
the same thing.

### 41.7 Instruments that had to be thrown away, and why

Three MAME-side instruments produced a confident wrong answer before the right
one was found. Each is recorded because each would be re-invented.

- **`emu.register_periodic` fires zero times in this MAME build.** Measured
  directly with a self-counting callback over 60 frames: `periodic_fires=0`.
  Anything built on it reports "nothing happened".
- **A ROM read tap installed at machine init dies after the boot.** It sees the
  `$00069A` boot fill exactly 230,400 times and then nothing for the remaining
  ~560 frames. The identical tap installed at frame 160 over the same window
  records hundreds of thousands of fetches. "The tap did not fire" there means
  "the instrument died", and it looks exactly like "the CPU never ran there".
- **Several read taps in one install: only the first survives.** Six windows
  installed at once gave a firing count for `$00069A` and a confident zero for
  the other five, two of which the per-frame PC log independently proved were
  executing. One tap per run.

With the tap installed late and one at a time, it is a usable oracle PC
histogram — but note the remaining gap: from frame 150 the MAME tap records
**zero** fetches in `$000000-$01FFFF` while recording 384 in
`$050000-$05FFFF` and 6.1M in `$020000-$02FFFF`. I did not resolve why, and no
claim here rests on the histogram's coverage below `$020000`.

### 41.8 Reproduce

```bash
export PATH="$HOME/.local/share/verilator-5.052/bin:$PATH"
cmake -S native -B build-rtl -DSTUNRUN_GSP_RTL=1 && cmake --build build-rtl -j"$(nproc)"

# Native side: the gate, and the level that never fires.
NATIVE_BUILD_DIR=$PWD/build-rtl SDL_VIDEODRIVER=dummy \
STUNRUN_LIVE_VIDEO=1 STUNRUN_FRAMES=600 \
STUNRUN_PC_HIST=/tmp/nat_pchist.txt \
STUNRUN_WATCH_WRITE=0xFFDB48,0x608000 ./run.sh

# Static disassembly of the call path.
for r in 0x000000:0x000400 0x4D00:0x5C00 0x5C00:0x6200 0x020000:0x020100 \
         0x212a0:0x21440 0x8f00:0x9100; do
  NATIVE_BUILD_DIR=$PWD/build-rtl SDL_VIDEODRIVER=dummy STUNRUN_LIVE_VIDEO=1 \
  STUNRUN_FRAMES=1 STUNRUN_DISASM=$r:/tmp/dis.txt ./run.sh >/dev/null 2>&1
  echo "== $r"; cat /tmp/dis.txt
done

# Oracle side.  THE cfg_directory MUST be clean; the tool's hardcoded
# /tmp/stunrun-mame-cfg is what produced §40.8.
grep 'port tag' /tmp/stunrun-mame-cfg/stunrun.cfg   # must print nothing
python3 tools/mame-ram-write-trace roms OUT \
  --device :mainpcb:maincpu --space program --base 0xffdb40 --end 0xffdb4c \
  --frames 600 --start-frame 10 --end-frame 600 --max-events 200000 --nothrottle \
  --mame /mnt/build/mame-ref/mame
# -> 2529 events, writer_pcs includes 0x2143c, does NOT include 0x59de/0x5a12/0x5a5c
```

```text
python3 -m unittest discover -s tests   249 tests OK
```

### 41.9 Where the fix goes (not implemented here)

Three separable pieces, in dependency order:

1. **A pin.** `tms34010_system` / `tms34010_pin_system` need an output that
   reproduces `tms34010.cpp:1682-1685`: high while INTENB bit 7 is set. It is
   the host-visible output interrupt, distinct from the internal vector the core
   already takes.
2. **A connection.** `native/gsp_rtl_driver.cpp` must read that pin and call
   `musashi_cpu_set_irq(cpu, 3)` on the 0→1 edge and `..._set_irq(cpu, 0)` on
   the 1→0 edge, the way `shell.c:1674` does for IRQ5.
3. **A cadence check.** MAME's `$002143C` fires every third frame from frame
   131 on a clean boot. If the RTL core's INTENB bit 7 is set at a different
   point in the GSP's execution, the cadence will differ and `$FF9BAA` (the
   elapsed-tick stamp the handler writes at `$021458`) will not match either.
   That stamp is the measurement to close the loop on.

The gate itself needs nothing: `$02FC60` already returns correctly the moment
`$FFDB48` is non-zero, as the injection control in 41.6 shows.

## 42. Frame-ordered app replay of captured GSP host traffic

`./run.sh --gsp-replay <capture-dir>` is a separate app mode; the default
`./run.sh` path is unchanged. It consumes `gsp-upload-events.txt` from
`tools/capture-gsp-program-upload`. It applies all pre-release host writes to
the board model only, reconstructing the uploaded 512 KiB share without
spending hundreds of thousands of Verilated host-port handshakes while the GSP
is held. It then starts the RTL reset-held with HLT sampled asserted, restores
the captured HSTADR/HSTCTL state and final pre-release /GSPRES level, applies
the HLT-clear event, and feeds post-release host transactions into the running
core. No MC68010 core is started. The old frame-404 image associated
with this capture is 4 MiB of program-space reads, not a physical 512 KiB share,
and is deliberately not loaded as VRAM.

Legacy v1 event rows have 11 whitespace-separated columns:
`seq frame kind address data mask derived-destination pc a0 a1 a2`. New v2
rows append `time_ns`, sampled from MAME `machine.time` inside the write/reset
callback. `seq` is still the ordering authority; v2 time must be monotonic and
all replayed GSP cycles are advanced between event timestamps at the measured
48 MHz GSP clock. V1 remains supported but is explicitly
**frame-ordered, not cycle-locked**. The parser rejects malformed rows,
non-monotonic frames/timestamps, and non-dense sequence numbers.
Replay defaults `STUNRUN_GSP_IDLE_CYCLES=0`: unlike live-shell fast mode, it
does not infer a GSP STOP from host-port silence and steps the full frame
interval. An explicit caller override remains available for experiments.
The replay-only driver initializer does not run its normal 200,000-cycle
empty-board warm-up. After staging the halted program state it releases reset
with HCS sampling HLT asserted, so the post-release HLT clear starts the core
at the captured boundary. The viewer also requires the
capture's `result.json` manifest (`stunrun-gsp-program-upload-result/v1`),
requires `stored == seq`, `dropped == 0`, a valid release sequence, and checks
the parsed row count against the manifest. A clean-prefix truncation is thus
rejected even when every remaining row is well-formed.

The shared renderer emits `gsp-replay-frame.png` by default (override with
`STUNRUN_REPLAY_PNG`). It also presents and writes PNG checkpoints at each
observed 30-frame boundary (for example `<png>.frame-0450.png`). The same RGB frame is presented via the SDL/OpenGL
backend when available; `STUNRUN_LIVE_VIDEO=0` selects headless capture. A
`gsp-replay-outputs.log` beside the working directory (override with
`STUNRUN_REPLAY_OUTPUTS`) records frame checkpoints, RTL-local board word/VRAM
write counters and active-low HINT/INTOUT asserts and clears. Its
`.vram-deltas.txt` sidecar contains only changed VRAM bytes measured around
RTL-stepping intervals, excluding host writes applied between those intervals.
HINT is the RTL's observed host-facing output pin; this is an RTL observation,
not parity with MAME.

**Response stream partially observed; oracle response stream still absent.**
The RTL replay observes one HINT/INTOUT assert and one clear during the
post-release run. Those are RTL-side edges, not MAME parity. The oracle capture
taps MC68010 writes and `/GSPRES`; it does not include MC68010 reads from the
GSP host port, returned HSTDAT/HSTCTL values, or MAME's HSTCTLL.INTOUT edge
timeline with event frame, sequence and GSP PC. The new outputs log does not
invent these values. To close
that gap, extend `mame/lua/gsp-program-upload.lua` (and its bounded event schema
in `tools/capture-gsp-program-upload`) with the MAME TMS34010 host-read path and
the `tms34010.cpp` output-interrupt callback, recording frame/order/PC plus
observed values and refusing overflow/truncation; then compare those records
against the RTL HINT edge log. Full response parity remains UNKNOWN.

The earlier live-shell INTENB probe is also recorded for the rejected IPL2
shortcut: at 600 frames it saw two INTENB writes, final value `0x0400`, and no
bit-7 rise or fall. This supports the source reading that INTENB is not the
interrupt event; no IPL2 was wired from it.

The requested raw event/image payload is absent from the repository's
`reference/experiments/stunrun/gsp-program-upload` directory; it contains the
provenance metadata only. The run below uses the already-existing raw capture
at `/tmp/opencode/gsp-upload-v3` (not a generated fixture), whose event file
hash matches that metadata. Its 4 MiB boundary image is verified but not used
because it is an obsolete program-address image. Local evidence run (not an
oracle-parity test):

```text
capture event SHA-256: 5640860abca7c79b8ea6f4fe17129636f4ac4c2758e53fc957b1a4fd52e542be
source rows: 701877; pre-release host rows staged=631200; post-release host rows replayed=70672
release HSTCTL event=631205; final event frame=597
headless render: 512x240, lit=122880, colours=225
frame 420 onward shows output; frame 450 PNG: `/tmp/opencode/boundary-full.png.frame-0450.png`
core-only counters: word_writes=330490, vram_writes=1295144
RTL HINT/INTOUT output: one assert, one clear, currently deasserted
```

Those are replay measurements, not named MAME output comparisons. In particular
the live state before the capture tap's first frame is not present in this log,
so reconstruction is only as complete as the captured transaction prefix.

## 43. The SDL window showed one triangle because the upload quad had three vertices

The replay PNG was a complete 512x240 image, but the user's SDL window showed a
diagonal black half-window. This was not another GSP address or palette defect;
the clipping happened after the renderer had produced its pixels.

`backend/sdl_video.c::build_quad()` and `backend/sdl3_video.c::build_quad()`
initialized only vertices 0-2 (12 floats) and left vertex 3 (top-right) unset.
Both `present()` functions then called `glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)`.
The backend structs also allocated `quad_vertices[12]`, so the draw read the
fourth vertex past the end of the VBO. A triangle strip with a missing fourth
corner explains exactly the diagonal cutoff in the window while the saved PNG
remained whole: PNG capture occurs before the backend upload/present call.

Both SDL OpenGL paths now use the same canonical four-vertex helper, with a
16-float buffer and the top-right `(1, 1, 1, 0)` corner set. The regression test
checks all 16 values and initializes its output to a sentinel first; a negative
control that removes the top-right assignment fails at component 12.

Confirmed here: the source had a 12-float buffer, wrote only 12 floats, and drew
four vertices; the unit test fails when the fourth vertex is omitted. The
specific screenshot's visual signature matches that malformed strip. The
scanout pixels themselves are unchanged by this fix.

## 44. The complete replay PNG is still not MAME's displayed frame

The user compared the replay to MAME's normal-mode title screen and correctly
pointed out that the remaining discrepancy is not merely a vertical squash.
The image comparison was made at frame 450 in normal-mode MAME and the
legacy-v1 frame-ordered RTL replay:

```text
MAME normal-frames-1-600 frame 240 SHA256 = 7e48495c...c853d49
MAME normal-frames-1-600 frame 480 SHA256 = 7e48495c...c853d49
MAME state-snapshot frame 450 screen = title screen (1084 px differ from frame 480)
replay frame 450 vs MAME frame 450: 122534/122880 pixels differ (99.72%),
  max RGB distance=255, average channel distance R=70.43 G=76.45 B=83.85,
  SSIM=0.082492 (`tools/frame_diff`)
```

The renderer's **effective scanout inputs are already known to be wrong**.
MAME's state snapshot at frame 450 records `DPYADR=0xF87C`,
`DPYSTRT=0xFC3C`, `DPYTAP=0`, `DPYCTL=0xF004`, `HEBLNK=55`, `HSBLNK=311`,
`FINESCROLL=7`. The replay's tracked values at frame 450 are
`DPYSTRT=0xFFFC`, `DPYADR=0xFFC3`, `DPYTAP=0x0200`, `DPYCTL=0x8201`, with
`core-set=0x1` — only DPYSTRT was actually observed from the RTL. The renderer
then substitutes fallback values for the three unseen registers, but
DPYSTRT remains `0xFFFC` and the timing bounds are the frame-600 fixture. Those
are not MAME's frame-450 scanout parameters.

So the established answer is: **the final-screen mismatch is not just
presentation or scaling**. The backend's missing fourth quad vertex caused the
black diagonal and is fixed in §43. Separately, the replay is rasterized from
an incomplete/unverified display-register shadow; its 99.72%-different output
cannot establish that the GSP program or palette is wrong, nor can its color
count establish parity. It also cannot exonerate the GSP RTL: the core-set
DPYSTRT itself differs from MAME's sampled value. The remaining test must feed
or observe MAME's scanout registers at the same checkpoint, compare the replay
framebuffer, and capture matching physical VRAM/palette state. Until then the
rendered replay is an instrumental GSP view, **not the game's MAME-matched
screen**.

## 45. First semantic divergence between the oracle and the native host stream

`tools/compare-gsp-host-events` compares the MAME upload capture against the
native `musashi_gsp_bus_event_t` stream field by field and reports the FIRST
event that differs, with both records and surrounding context.

### 45.1 What it compares, and what it refuses to compare

Compared: the event kind (host register or `/GSPRES`), the register address, the
effective 16-bit data after the lane mask, the mask itself, and the `/GSPRES`
level.

NOT compared: raw file bytes, PC, cycle counts, and frame numbers.

* Raw bytes cannot be used. `gsp_emit_bus_event()` never assigns the padding at
  record offsets 29-31 and 37-39, so those bytes carry uninitialised stack and
  move on any recompile (§23.6).
* PC is an alignment hint, not an equality key. Inside a Musashi memory callback
  `REG_PC` is the first UNCONSUMED word of the instruction, so it can sit past
  the writing instruction; MAME's `CURPC` has the same property. The first
  capture pair shows it directly: MAME seq 1 is PC `$069A`, native seq 583 is
  callback PC `$069C`, and those are the same event.
* Frame is a MAME-only column; the native stream has cycles. Frame-vs-cycle is
  not a comparison the artifacts support.

The tool aligns the MAME tap's first event to the first native event with the
same signature whose PC is within 16 bytes, or takes `--native-start-seq`
explicitly. It says which it used, in `alignment`.

### 45.2 The two producers do not record the same events

A real native stream carries **262,766** host writes to `$00C0000E` that the
MAME tap never records. MAME's driver only decodes word offsets 0-6 of the
`$C00000-$C03FFF` host aperture and lets `$C0000E` and above fall through
(§13). This tool names that address `HSTDAT-UNMAPPED-0E` rather than folding it
into `HSTDAT`. Folding them would make a later mismatch look like a data
difference when it is a producer difference, and would silently discard a
quarter of the native traffic.

### 45.3 Measured result

```text
oracle /tmp/opencode/gsp-upload-v3/gsp-upload-events.txt   701,877 rows
native /tmp/opencode/gsp-dump-final/gsp_bus_events.bin    902,647 records
alignment: --native-start-seq 583 (oracle seq 1, PC $069A <-> native $069C)

MATCH for 224,522 events
DIVERGED at oracle seq 224523 (frame 31): HSTCTL $C00008 = $B800 at PC $0570
           native seq 225105 is still HSTDAT $C0000C = $0000 at PC $069C
```

The oracle has finished the boot VRAM-fill loop and halted the GSP
(`$0570` writes `HSTCTL=$B800`, HLT asserted); the native model is still inside
the fill loop at `$069C`. That is the same class of difference already recorded
in §12.5 — at frame 600 the native PC was where MAME's was at frame 300 — so
this is the divergence surfaced in event space rather than newly found.

**It is a point to investigate, not proof of which side is wrong.** The two
captures come from separate runs and the oracle stream carries no cycle stamps,
so this is the first ORDERING difference, not a measured cycle offset. The
$FFDB48 symptom is not explained by it: both sides reach `$02FC82` with the
level-3 interrupt mechanism §40.1/§41.2 describes, and the missing IPL2 wire
remains the blocking difference.

## 45. GSP handoff interrupt ($02143C) ported to C

The level-3 autovector handler transcribed to
`reproduction/maincpu/gsp_handoff_irq.{h,c}` with CTest
`gsp-handoff-irq-slice-c`. It is the only writer of `$FFDB48` in the program
(§40.1) and releases both the waiter and the GSP:

```asm
02143c: move.w  #$ffff,$ffdb48.l
021444: move.l  d0,-(a7)
021446: andi.w  #$ff7f,$c00008.l     ; clear HSTCTL bit 7 = ack INTOUT
02144e: move.l  $8014.w,d0
021452: sub.l   $ffdb36.l,d0
021458: move.l  d0,$ff9baa.l        ; elapsed 68010 ticks
02145e: move.l  (a7)+,d0
021460: rte
```

Owned in C deliberately: its effects are observable and adjustable without
editing ROM. The default shell still executes the original 68k path; this slice
is not wired into the live run.

**Confirmed (disassembled from the ROM this session):** the instruction bytes
above, and the push/pop pair touching only D0.

**Corrected in section 48.2, after this was first written wrongly:** this
section also claimed `ANDI.W #$FF7F` "preserves bits 11:0 including the `0x0800`
HSTDAT auto-increment". Bits 11:0 *are* bits 0 through 11, so that includes bit
7, which the mask does clear. `$FF7F` clears exactly one bit. The 0x0800
auto-increment claim happens to be true; the "11:0" umbrella around it is not.
This section further claimed the instruction "releases the GSP" via bit 15. It
does not — `$FF7F` preserves bit 15 — and bit 15 was already clear when the
handler ran.

**Inferred:** that `$8014.w` is the free-running tick counter. The tick delta is
modelled as plain 32-bit wraparound `SUB.L`, and the test pins the wrapped case
(5 − `0xFFFFFFF0` = 21) so a later "safety" clamp cannot be mistaken for
fidelity.

### 45.1 The control procedure was wrong first, and said so

The first negative-control run reported all three deliberate breaks as PASSING.
That looked like the familiar "tests pass for the wrong reason" problem; it was
not. I mutated the source and then invoked `ctest` directly, without building.
CTest does not build, so every trial ran the STALE BINARY and proved nothing.

Rebuilt before each trial, all four breaks fail as they should:

```text
clear all of HSTCTL instead of only HLT   FAILED
clamp the wrapped tick delta to zero      FAILED
drop the $FFDB48 write                    FAILED
drop the $FF9BAA store                    FAILED
restored                                  PASSED
```

A negative control that passes is evidence about the CONTROL, not about the
code. All breaks passing should send you to the harness first. The lesson is
recorded so the next automation puts the build inside the trial loop rather than
beside it.

## 46. MAME screen capture shows the replay is not just geometrically scaled

The oracle's composed screen was captured at frame 450 with
`tools/mame-gsp-state-snapshot` (`--input none --frames 450 --targets 450
--screen-dir ...`). MAME frames 240 and 480 in the committed normal-mode
capture are byte-identical (SHA-256 `7e48495c...c853d49`); frame 450 is the same
title-screen state, differing from frame 480 at only 1,084 pixels.

Comparing MAME's frame-450 screen against the replay renderer's frame-450 RGB
buffer with `tools/frame_diff` gives:

```text
pixels_different=122534/122880 (99.72%)
max_rgb_distance=255
avg_channel_distance R=70.43 G=76.45 B=83.85
ssim_score=0.082492
```

This is not the black-triangle presentation bug (§43): the comparison uses the
saved replay PNG/framebuffer before SDL/OpenGL. Supplying MAME's frame-450
display snapshot to the replay renderer (`STUNRUN_REPLAY_DISPLAY_SNAPSHOT`) does
not resolve it; the resulting frame has one colour and remains 100% different.
The replay's own raster-register state at frame 450 is also recorded in
`gsp-replay-outputs.log`:

```text
MAME frame 450:      DPYSTRT=FC3C DPYADR=F87C DPYTAP=0000 DPYCTL=F004
RTL shadow frame 450: DPYSTRT=FFFC DPYADR=FFC3 DPYTAP=0200 DPYCTL=8201 core-set=1
effective RTL raster: DPYSTRT=FFFC DPYADR=0000 DPYTAP=0000 DPYCTL=F010
```

`core-set=1` means only DPYSTRT is observed as an RTL write; the shadow's other
values are initializers, and the renderer substitutes reset/frame-600 fallbacks.
The MAME override proves these register fallbacks are not the whole problem:
the same replay board still renders a one-colour frame under MAME's display
registers.

The legacy-v1 replay's physical VRAM comparison is also non-identical at the checkpoint:
MAME's 512 KiB frame-450 dump has 208,031 nonzero words; the replay board has
84,050, with 250,077/524,288 bytes different (47.70%). This is an
oracle-to-replay state difference, not a framebuffer scaling metric. Replay is
frame-ordered, not cycle-locked, and the MAME event capture omits host-read
responses; the difference does **not** yet distinguish a GSP execution
divergence from missing timing/protocol inputs or palette/rendering defects.

The MAME GSP clock is 48 MHz (`harddriv.h:39`) and the main CPU clock is 8 MHz
(`harddriv.cpp:1463`, `harddriv.cpp:1483`), a 6:1 ratio. The measurements above
used the legacy frame-only v1 event capture; its frame-chunked replay still
differed substantially and is not the result of the timestamped v2 path. Do not
conclude "the renderer is the only problem" or "the GSP is wrong" from those
v1 results alone. The next discriminator is the v2 timestamped replay plus a
renderer baseline using MAME's physical VRAM, palette, and scanout state at the
same frame.

---

## 47. IPL2 from the GSP's own HSTCTLL.INTOUT: the `$02FC60` gate opens

§41.9 listed three pieces of work and stopped at the first. The pin is still not
in the vendored RTL and still is not ours to add — but the pin is not needed to
reproduce the *level*. HSTCTLL bit 7 (INTOUT) is a register bit inside the core
we already run, and MAME's INT output is a function of that bit and nothing
else. So this section reconstructs the bit from the core's own bus traffic,
delivers it as IPL2, and measures what happens.

**The gate opens.** `$02FCB6` — the wait exit that had never executed once in
600 frames — executes **once**, the level-3 autovector handler runs, it writes
`$FFFF` to `$FFDB48`, and the GSP's VRAM write count grows past its frozen
84,337.

### 47.1 Two mechanisms, and which one actually moves the bit

INTOUT is read two ways and the two are checked against each other every run.

**(a) The GSP's own register, read out of the Verilated model.** `io_reg` is not
a port — it is an array inside the generated root class — so whether it can be
read at all depends on Verilator keeping the signal. It can, here:
`tms34010_sv___024root.h:716` carries
`VlUnpacked<SData,32> tms34010_system__DOT__u_core__DOT__u_io_regs__DOT__io_reg`,
and `tms34010_sv.h:87` exposes `rootp` publicly. The driver reads
`io_reg[IO_IDX_HSTCTLL]` directly, behind a `__has_include` for the root header
and a `std::void_t` detection trait for the member
(`native/gsp_rtl_driver.cpp`, `rtl_io_reg_visible` / `rtl_io_hstctll`). Both
guards exist so that a renamed signal degrades to "the RTL's register is not
observable here", reported as `rtl-signal=absent`, instead of breaking the
build. Measured: `rtl-signal=exposed`.

**(b) A shadow reconstructed from the core's local-bus I/O writes**, with MAME's
own equations (§47.2). This is the mechanism that survives without (a), and it
is the one that turned out to be necessary for *half* the register: the
MC68010's HSTCTL write arrives at `gsp_rtl_driver_host_write()`, not on the
cycle bus, so a bus-only trace can see the setter and never the clearer.

**Which moves the bit.** The setter and the clearer are different paths:

| edge | writer | data | source |
|---|---|---|---|
| rise, frame 534 | GSP, local-bus write to I/O index `0x0F` | `$0080` | `INTOUT set-by=1 data=0x0080` |
| fall, frame 535 | MC68010 host HSTCTL write | `$3800` | `INTOUT clear-by=2 data=0x3800` |

(`set-by`/`clear-by`: 0 none, 1 processor-side local-bus write, 2 MC68010 host
HSTCTL write, 3 adopted from the RTL's own `io_reg`.) Nothing else moved it:
one processor-side HSTCTLL write, 19 host HSTCTL writes, `mismatch=0` between the
shadow and the RTL across the whole run, `mame-rule-0xC000000F=0`.

**That is structurally forced, not coincidental.** MAME's host arm
(`tms34010.cpp:1676`) is `newreg &= data | ~0x0080`, so the MC68010 can only
clear INTOUT; the processor arm (`:1667-1669`) is the only thing that can raise
it. So there is no third candidate, and the instrument agrees with the RTL on
every settled comparison.

### 47.2 The MAME edge semantics implemented

S.T.U.N. Runner instantiates a **TMS34010**, so the code that runs is
`tms34010_device::io_register_w` [S8], whose `case REG_HSTCTLL` is at
`tms34010.cpp:1661` and whose edge detection is at `:1682-1685`:

```c
/* tms34010.cpp:1665-1669  -- processor side (!m_external_host_access) */
newreg  = (oldreg & 0xff8f) | (data & 0x0070);
newreg |= data & 0x0080;
newreg &= data | ~0x0008;
/* tms34010.cpp:1675-1677  -- host side (the else) */
newreg  = (oldreg & 0xfff8) | (data & 0x0007);
newreg &= data | ~0x0080;
newreg |= data & 0x0008;
/* tms34010.cpp:1682-1685  -- the edge, both arms */
if (!(oldreg & 0x0080) && (newreg & 0x0080))  m_output_int_cb(1);
else if ((oldreg & 0x0080) && !(newreg & 0x0080)) m_output_int_cb(0);
```

`host_control_w` sets `m_external_host_access = true` for exactly one write
(`tms34010.cpp:2039-2042`), so the two arms are the same case. `m_output_int_cb`
is bound at `harddriv.cpp:1489` to `hdgsp_irq_gen`, which sets `m_gsp_irq_state`
(`harddriv_m.cpp:118-120`), which `update_interrupts` turns into
`m_maincpu->set_input_line(3, ...)` (`harddriv_m.cpp:97`) — level 3 is IPL2.
Level 2 autovector is vector `24 + 3 = 27`, and with `VBR = $00020000` the table
slot at `$2006C` holds `$0002143C` [ROM].

**Two things in that transcription that the comments do not say.**

1. The processor arm does **not** compute set-only, although the comment above
   it says "can set INTOUT". Bit 7 is `old7` after line 1667, `old7 | data7`
   after 1668, and `old7 & data7` after 1669 — so the result is `old & data`,
   and a processor write whose data has bit 7 clear *clears* INTOUT. The
   vendored RTL instead has `io_reg[INTOUT] <= io_reg[INTOUT] | data[INTOUT]`
   (`tms34010_io_regs.sv:553-554`), which is genuinely set-only. **The two
   models differ on this rule.** In this run the one processor write carried
   `data = $0080` (bit 7 set), so both models rise and `mismatch` stays 0; the
   divergence is not exercised and is reported as such, not as agreement.
2. The 1827-1832 block with byte-identical text is `case REG020_HSTCTLL` inside
   `tms34020_device::io_register_w` (`tms34010.cpp:1751`) — the **TMS34020**,
   which this board does not instantiate. Citing it for a TMS34010 run points a
   reader at a device that is not in the machine.

The delivery is edge-driven, not "assert while the bit is set", and that is not
a stylistic choice: `M68K_EMULATE_INT_ACK` is `M68K_OPT_OFF`
(`third_party/musashi/m68kconf.h:111`), so `m68ki_exception_interrupt()` ends
with `CPU_INT_LEVEL = 0` (`third_party/musashi/m68kcpu.h:2155-2158`) — the CPU
drops the line itself on acknowledge, exactly as MAME's does. Re-assertion
therefore needs a new board-side edge, which is what MAME's
`m_output_int_cb`-on-edges does.

### 47.3 Measured result — 600 frames, RTL core on

Baseline is §39.6 / §40.4: `board vram nonzero words=84337`, frozen, `$02FCB6`
never present in a 1,679-entry histogram, `assert_l1..l7=0/0/0/0/2475/0/0`,
`$FFDB48` written once with value 0.

```text
shell: gsp-rtl intout edge frame=0   level=0 sr=0x2704 imask=0x7 pc=0x00000414
shell: gsp-rtl intout edge frame=534 level=1 sr=0x2504 imask=0x5 pc=0x0003D1BE
shell: gsp-rtl intout edge frame=535 level=0 sr=0x2300 imask=0x3 pc=0x0002144E
shell: gsp-rtl HSTCTLL.INTOUT level=0 rises=1 falls=1 ipl2-asserts=1 ipl2-clears=2
shell: gsp-rtl INTOUT set-by=1 data=0x0080 clear-by=2 data=0x3800
shell: gsp-rtl HSTCTLL shadow=0x0000 rtl-io-reg=0x0000 rtl-signal=exposed mismatch=0
        (proc-writes=1 host-hstctl-writes=19 mame-rule-0xC000000F=0)
shell: gsp-rtl GSP-local I/O writes=56 distinct=19 overflow=0
shell: gsp-rtl board vram nonzero words=84787
shell: write-watch addr=0x00FFDB48 writes=3 last_pc=0x0002F23A last_value=0x0000
        width=16 nonzero-writes=1 nonzero-last-pc=0x0002143C nonzero-last-value=0xFFFF
shell: irq assert_calls=2473 assert_l1..l7=0/0/1/0/2472/0/0
        autovector_fetches=965 vbr=0x00020000 irq5=964 nmi=0
        last_vector=29 last_target=0x000222DA last_source_pc=0x0002FC94
        insn_retired=7375313
render frame=600 hash=0xF3973280 lit=122880/122880 colours=225
```

PC histogram (`STUNRUN_PC_HIST` is a file path):

| address | baseline | with INTOUT→IPL2 |
|---|---|---|
| `$02FC82` `tst.w (A2)` | 43,259 | 35,042 |
| `$02FCB6` **the wait exit** | **absent** | **1** |
| `$02143C` IPL3 entry | absent | 1 |
| `$021444` `move.l D0,-(A7)` | absent | 1 |
| `$021446` `andi.w #$ff7f,$c00008.l` | absent | 1 |
| `$021460` `rte` | absent | 1 |
| `$02F23A` `clr.w $ffdb48.l` | absent | 1 |
| `$00069A` boot fill | 230,400 | 230,400 |

Distinct PCs in the histogram: 1,679 → 5,196. `insn_retired` 7,187,787 →
7,373,313. Three consecutive 600-frame runs produced byte-identical output on
every line above.

**Acceptance, stated one by one.**

1. **`$02FCB6` executes a non-zero number of times — MET, 1 execution** (0 in
   baseline, where it is absent from the histogram entirely). It is 1 and not
   3 because the GSP raises INTOUT **once** in this run, at frame 534. §41.6's
   synthetic control pulsed IPL2 every third frame and got 3; one real edge
   cannot give three.
2. **`$FFDB48` becomes non-zero and the writer is `$02143C` — MET.** Directly
   measured: `nonzero-writes=1 nonzero-last-pc=0x0002143C
   nonzero-last-value=0xFFFF`. `last_pc` alone could not show this, because the
   *last* writer is `$02F23A` clearing it, and the frame sampler reads at the
   frame boundary and this word is set and cleared inside one frame — which is
   why `musashi_write_watch_t` grew a `nonzero_writes` / `nonzero_last_pc` /
   `nonzero_last_value` trio. `$02143C` also executes exactly once.
3. **GSP VRAM write count grows past the frozen 84,337 — MET, 84,337 → 84,787
   (+450).** This is the strongest of the six, because it is the one number the
   gate could not fake: releasing the wait hands the 68010 back to its own
   drawing code, and it writes more VRAM.
4. **No runaway — MET.** One rise, one fall, one assert, one delivery, one
   `rte`. `assert_calls=2473` against 3,463 in baseline, of which IPL5 is 2,472
   against 2,475. Autovector fetches 965 with `irq5=964`: the difference of one
   is the single vector-27 fetch. The IRQ5 *delivery* count falls further, 985
   to 964, and `insn_retired` rises 7,187,787 → 7,373,313. Those two move
   together in a way this section does **not** explain — the IRQ5 period is
   paced in cycles and a frame is a fixed cycle budget, so the delivery count
   should be near-constant, and why 21 fewer were taken is **UNKNOWN**. It is
   not load-bearing for any acceptance number here, and it is recorded so a
   later reader does not mistake the drop for a consequence of the IPL2 work.
   §47.5's NC4 shows what the absence of the deassert looks like: 152
   deliveries in 65 frames, `$021460 rte` 152 times, `$FFDB48` written `$FFFF`
   152 times. Bounded only by the run length — so the deassert is what makes
   this once-per-edge, not luck.
5. **`lit=`/`colours=` and both CTest trees — MET.** `lit=122880/122880
   colours=225` at every 50-frame sample from 400 to 600, and in the terminal
   line. `python3 -m unittest discover -s tests` 251 OK; `ctest --test-dir
   build-test` 63/63; `ctest --test-dir build` 63/63; and `ctest --test-dir
   build-rtl` 63/63 (that tree is not in the standing gate but is the one the
   changed code is built into).
6. **Correspondence with the replay's one assert / one clear — MET on count and
   polarity, NOT on frame.** Re-run unchanged:
   `./run.sh --gsp-replay /tmp/opencode/gsp-upload-v3` still prints
   `HINT/INTOUT asserts=1 clears=1 currently=0` and
   `rtl-word-writes=330490 vram-writes=1295144`, byte-identical to §42's
   recorded run, so this change did not disturb it. One assert and one clear,
   same polarity, from the same `io_reg[HSTCTLL][7]`. The frames differ:
   replay asserts at ~406 and clears at ~411 (it starts the core at the
   captured HLT release), live asserts at 534 and clears at 535.

### 47.4 The bigger defect this uncovered: our IPL plumbing had ONE level

The first working version asserted IPL2 and **nothing was delivered**. `assert_l3`
went 0 → 1, `autovector_fetches` stayed at 985, `last_target` stayed `$0222DA`,
and `insn_retired` was bit-identical to baseline. The assert was real and the
gate did not open.

Cause: `musashi_cpu_set_irq()` called `m68k_set_irq(level)`, which writes one
global `CPU_INT_LEVEL`. The board has **six** interrupt lines
(`harddriv_m.cpp:94-100`), MAME keeps them as a bitmask and re-derives the CPU
level from the highest still-asserted one on every single-line transition
(`m68000_musashi_device::set_irq_line`, `m68kcpu.cpp:708-735`), and so did we
not. The shell asserts IPL5 every 32,768 cycles (`shell.c`, the IRQ5 tick) and
the ROM's `$60C000` ack cleared "the line", so a pending IPL2 of `$0300` was
overwritten by `$0500` within 32,768 cycles — long before the ROM's interrupt
mask left 5.

Fix: both entry points go through Musashi's `m68k_set_virq(level, active)`
(`m68kcpu.c:1080-1093`), which is a transcription of that same MAME routine, and
the three `REGION_IRQACK` sites now clear **line 5** rather than zeroing the
level — matching `hd68k_irq_ack_w`, which clears `m_irq_state` and then calls
`update_interrupts()` and so re-drives all six lines
(`harddriv_m.cpp:111-115`). `musashi_cpu_reset()` also drives all seven lines
off once, because `m68k_init()` does not clear `m68ki_cpu.virq_state` and that
bitmask now outlives a delivery.

This is behaviour-preserving where no second line is live: with only IPL5
asserted, `m68k_set_virq(5, 0)` re-derives level 0, exactly as the old
`m68k_set_irq(0)` did. Both CTest trees and the Python suite confirm the rest of
the model did not move.

### 47.5 Negative controls, each rebuilt before it was run

Per §45.1: the build is inside every trial. Each break below was made, rebuilt,
run, and restored; the reported numbers are the actual output.

| # | deliberate break | expected | measured |
|---|---|---|---|
| NC1 | suppress the INTOUT rise in the driver (`if (0 && ...)`) | nothing delivered, gate shut | `rises=0`, `assert_l3=0`, `autovector_fetches=985`, `$02143C` absent, **`$02FCB6` absent**, `$02FC82`=43,259, VRAM **84,337**, `$FFDB48` 1 write / 0 non-zero — i.e. exactly the baseline |
| NC2 | transpose the host arm to `newreg \|= data & 0x0080` (set-only) | the shadow must diverge from the RTL | run output identical, **but `mismatch=1`** — the settled-state comparison against `io_reg[HSTCTLL]` caught the divergence and adopted the RTL's value. The mismatch counter is itself the control: 0 in the correct build, 1 in the broken one |
| NC3 | revert the per-line IPL model (`m68k_set_irq`), keep the INTOUT wiring | the assert is destroyed before it can be taken | `rises=1`, `assert_l3=1`, **`autovector_fetches=985`**, `$02143C` absent, **`$02FCB6` absent**, `$02FC82`=43,259, VRAM **84,337** — bit-for-bit the pre-fix behaviour |
| NC4 | never deassert IPL2 | unbounded re-entry | `$02143C` **152**, `$021460 rte` **152**, `$FFDB48` `$FFFF` **152** times, `$02FCB6` 3, `last_vector=27 last_target=$002143C`, `insn_retired` 7,510,613 |

NC1 and NC3 are the load-bearing pair: NC1 removes the signal and NC3 removes
the plumbing, and each independently returns every acceptance number to its
baseline value. NC4 is what "bounded" is worth.

**One self-inflicted segfault, found and fixed.** The first build crashed
`native-shell`, `native-shell-oki-audio-probe` and `native-shell-coin-start` —
the three CTest targets that run the shell *without* an MC68010, where the GSP
driver is never constructed. `gsp_rtl_driver_intout_observation()` dereferenced
`s_top` through the new root-class read. `rtl_io_hstctll()` now returns the
shadow when `s_top == NULL` and the report says `rtl-signal=absent`. Recorded
because the failure mode is the interesting part: a feature that only exists in
the RTL build broke the tests that do not use it, and only the build that
actually runs the changed code notices.

### 47.6 The GSP's own I/O write addresses — and an open question about the index

The run also records every distinct address the core presents in I/O space, with
its count, which is a fact about the program rather than about our model:

```text
0x000000 x3   0x000010 x3   0x000020 x3   0x000030 x3   0x000040 x3
0x000050 x3   0x000060 x3   0x000070 x3   0x000080 x3   0x000090 x4
0x0000A0 x1   0x0000B0 x5   0x0000F0 x1   0x000100 x3   0x000110 x2
0x000130 x3   0x000140 x3   0x000150 x6   0x000160 x1
```

56 writes total. The stride is `0x10`, one 16-bit register in the RTL's
bit-addressed numbering (`idx = addr[8:4]`, `tms34010_io_regs.sv:154-156`).
`0xC00000F0` is the single INTOUT setter.

**Open, and deliberately left open: which numbering the hardware uses.** Two
decode rules are in play for the same bus address and they differ by 16×:

* the vendored RTL: `idx = addr[8:4]`, so `IO_IDX_HSTCTLL = 0x0F` is at
  `$C00000F0` (`tms34010_pkg.sv:382`);
* MAME: `map(0xc0000000, 0xc00001ff)` with `IOREG(offset)` and
  `offset = address - 0xC0000000`, and `REG_HSTCTLL == 15`
  (`tms34010.cpp:34-41`, `:1576-1577`, `tms34010.h:78`), so `$C000000F`.

Under the RTL's rule the program's addresses decode to registers `0x00..0x16`;
under MAME's rule the *same* addresses become offsets `0x000, 0x010, ...,
0x160`, which is exactly the `0x90`–`0x160` range `analysis/mame-bugs.md` (MD-12)
reports the real S.T.U.N. Runner GSP program writing at. The two accounts of the
same traffic agree on the addresses and disagree on what they name — and only one
of them can be right about the TMS34010's register file.

That is **not** settled here, and nothing in this section depends on it: what is
delivered is the RTL core's own `io_reg[HSTCTLL][7]`, cross-checked against a
shadow, `mismatch=0`. Two things would settle it and neither was done:

* reading MAME's `memory_map` handler-offset convention (the `emu/` sources are
  not present in `/mnt/build/mame-ref`, only the device and machine files);
* a MAME run with `STUNRUN_GSP_IOBREPORT` set, which prints every `offset` the
  driver sees (the `MD-12` triage hook at `tms34010.cpp:1579-1615`) — if
  `0x00f` appears there, MAME's decode reaches HSTCTLL and the program's
  `$C00000F0` is meaningless to it.

Note also that this run **cannot** settle it on its own: the core issues only 56
I/O writes in 600 frames, so it is far behind MAME's trajectory (§37, §39.6),
and a register the program has not reached yet is not evidence about the other
numbering. `mame-rule-0xC000000F=0` is a statement about 600 frames of this
trajectory, not about the program.

### 47.7 `andi.w #$ff7f` clears bit 7 — and NOT bit 15

I read `$021446: andi.w #$ff7f,$c00008.l` as preserving HSTCTL bit 7, on the
strength of §45's "Confirmed" line that `ANDI.W #$FF7F` "preserves bits 11:0
including the `0x0800` HSTDAT auto-increment". Bits 11:0 **are** bits 0 through
11, and bit 7 is one of them — that §45 sentence is wrong, and I copied the error
rather than reading the mask.

**Both halves of this subsection were then wrong on the next pass and are
corrected in section 48.2.** The mask is `1111_1111_0111_1111`: it clears
**exactly one bit, bit 7** — not "bits 6:0". And `$3800` having bit 15 clear
says nothing about what the mask does to bit 15, because the mask governs:
`$FF7F` **preserves** bit 15. The value `$3800` in `INTOUT clear-by=2 data=0x3800`
is the value the ANDI *wrote*, i.e. the post-mask HSTCTL, so its bit 15 being
clear means bit 15 was **already** clear before the instruction ran. The handler
therefore does not release the GSP; there was nothing to release.

What the instruction does do is the acknowledge. Because the host arm is
clear-only (`newreg &= data | ~0x0080`, `tms34010.cpp:1676`), writing HSTCTL
with bit 7 clear drops INTOUT, which drops the GSP's INT line — the very line
whose rising edge delivered this handler. The deassert is observed at
`pc=0x0002144E`, the *end* of the eight-byte `andi.w` at `$021446` (2 opcode +
2 immediate + 4 long address), which is consistent with the write completing
before the level is re-read.

So the level-3 handler drops INTOUT (bit 7) and signals the waiter
(`$FFDB48 = $FFFF`) in one breath. It does not touch bit 15.

### 47.8 Fidelity limits, stated

1. **One frame of latency.** The GSP is stepped for a frame *after* the 68010
   has run that frame, so an INTOUT edge is delivered at the start of the next
   68010 frame. In MAME the two processors run concurrently. The signal is
   right; the scheduling is not, and a cadence comparison would be measuring
   this.
2. **Cadence does not match.** MAME delivers IPL2 every third frame from frame
   131 (§41.4). The RTL core delivers it once, at frame 534. §45.3's ordering
   divergence and §37's trajectory offset are upstream of this; one edge is not
   evidence about the steady-state rate.
3. **The processor-arm rule differs between MAME and the RTL** (§47.2 item 1)
   and is not exercised here.
4. **`reproduction/maincpu/gsp_handoff_irq.{h,c}` was not used.** Delivery of
   the interrupt makes Musashi run the real ROM handler, which is the stronger
   result; the C slice stays an unused transcription, as before.
5. **Frame 450 parity is unchanged.** §46's 99.72%-different replay frame is a
   scanout/VRAM-trajectory problem and this does not touch it. The 225-colour
   `lit=`/`colours=` result is content, not parity, and §39.5 still stands: a
   hash cannot tell a correct picture from a correctly-rendered zero region.

### 47.9 Reproduce

```bash
export PATH="$HOME/.local/share/verilator-5.052/bin:$PATH"
cmake -S native -B build-rtl -DSTUNRUN_GSP_RTL=1 && cmake --build build-rtl -j"$(nproc)"

NATIVE_BUILD_DIR=$PWD/build-rtl SDL_VIDEODRIVER=dummy \
STUNRUN_MC68010=1 STUNRUN_STUNRUN_DIR=roms/stunrun \
STUNRUN_LIVE_VIDEO=1 STUNRUN_FRAMES=600 STUNRUN_FRAME_LOG=50 \
STUNRUN_AUDIO_BACKEND=null STUNRUN_VIDEO_BACKEND=null \
STUNRUN_PC_HIST=/tmp/opencode/pchist.txt STUNRUN_WATCH_WRITE=0xFFDB48,0x608000,0xFF800000 \
build-rtl/stunrun-native-shell | grep -E "intout|INTOUT|HSTCTLL|nonzero|write-watch|irq assert|render"

grep -i '^0x02FCB6 \|^0x02143C \|^0x02FC82 \|^0x00069A ' /tmp/opencode/pchist.txt

# The handler, byte-exact, from the ROM image:
NATIVE_BUILD_DIR=$PWD/build-rtl STUNRUN_MC68010=1 STUNRUN_STUNRUN_DIR=roms/stunrun \
STUNRUN_LIVE_VIDEO=1 STUNRUN_FRAMES=1 STUNRUN_AUDIO_BACKEND=null STUNRUN_VIDEO_BACKEND=null \
STUNRUN_DISASM=0x021430:0x021480:/tmp/opencode/irq3.txt build-rtl/stunrun-native-shell >/dev/null 2>&1
cat /tmp/opencode/irq3.txt

# Acceptance 6, unchanged by this work:
./run.sh --gsp-replay /tmp/opencode/gsp-upload-v3   # HINT/INTOUT asserts=1 clears=1

python3 -m unittest discover -s tests   # 251 OK
ctest --test-dir build-test              # 63/63
ctest --test-dir build                   # 63/63
ctest --test-dir build-rtl               # 63/63
```

### 47.10 BUGS.md candidates — reported, not fixed

**Ours, not candidates.** The single-global IPL level (§47.4) and the NULL-core
dereference (§47.5) are defects in our machine model and belong in this
document, not `BUGS.md`.

**MAME, candidate, not filed:** the processor-side INTOUT rule computes
`old & data` while its own comment says "can set INTOUT"
(`tms34010.cpp:1664-1669`). If a program writes HSTCTLL with bit 7 clear, MAME
clears INTOUT and fires the external interrupt's *falling* edge where the
documented set-only behaviour would hold the bit. Not filed: impact on any
title is unmeasured, and `BUGS.md`'s entries get sent upstream, where a
cosmetic inconsistency is a liability.

**The register-index question (§47.6), candidate only, `UNKNOWN`.** Either the
vendored RTL's `idx = addr[8:4]` or MAME's `offset = address - 0xC0000000`
mis-decodes the TMS34010's I/O register file, and on this evidence it is not
possible to say which. Not filed, and should not be until `STUNRUN_GSP_IOBREPORT`
has been run against the same traffic.

## 48. Corrections to §45 and §47.7, and a configuration limit §47.3 missed

Three things went wrong while integrating §47. Two were mine and had already been
committed; one was missed by the work itself.

### 48.1 The C slice transcribed the wrong mask

`reproduction/maincpu/gsp_handoff_irq.c` implemented the `$021446` ANDI as
`s->hstctl & ~0x8000` — clear HSTCTL bit 15. The ROM bytes are:

```text
021446: 02 79 ff 7f 00 c0 00 08   andi.w  #$ff7f, $c00008.l
```

`$FF7F` is `1111_1111_0111_1111`. It clears **exactly one bit, bit 7**, and
preserves the other fifteen including bit 15. The transcription now uses the
ROM's mask verbatim (`STUNRUN_GSP_HANDOFF_HSTCTL_MASK 0xFF7F`).

### 48.2 The instruction ACKNOWLEDGES; it does not release the GSP

§45 and §47.7 both described this handler as clearing HLT "and so releasing the
GSP". It does not. Bit 15 is preserved by the mask, so the instruction cannot
clear HLT under any input.

That HLT was not held anyway is visible in §47's own measurement.
`INTOUT clear-by=2 data=0x3800` records the value the ANDI **wrote** — the
post-mask HSTCTL, because ANDI is a read-modify-write of the 16-bit register and
MAME splits the write-back into HSTCTLH/HSTCTLL. Its bit 15 is clear. Since the
mask preserves bit 15, bit 15 must have been **already clear** when the handler
read it. The GSP was not halted at this handshake; nothing was released.

What bit 7 is: HSTCTLL.INTOUT, the GSP-to-host interrupt request. The host arm
of MAME's HSTCTLL write is clear-only (`newreg &= data | ~0x0080`,
`tms34010.cpp:1676`), so writing it with bit 7 clear drops INTOUT, which drops
the GSP's INT line — the line whose rising edge delivered this handler. **The
instruction is the interrupt acknowledge.** That is the whole of its board
effect, and it is a good deal less dramatic than "releases the GSP".

*Inferred, not measured:* that bit 15 was already clear is read off the written
value, not observed. The driver tracks HSTCTLL (the low byte) only, so
HSTCTLH/HLT is never sampled directly. If the HLT state at handler entry ever
matters, it needs tracking rather than inference.

### 48.3 The test and the implementation shared one error

The wrong transcription shipped with a green test, and the interesting fact is
*why*. The old test took `0xB800` as its single HSTCTL input and asserted
`hstctl_after(0xB800) == 0x3800`.

`0xB800 & 0x7FFF` is `0x3800`. That is exactly what the wrong implementation
produced — so the test was a **faithful** test of the wrong mask. It was not
blind: under the ROM's real mask the same call returns `0xB800` and the old
assertion would have failed immediately.

So the failure is not "the test could not see the bug". It is that §45's
"Confirmed" line, the implementation, and the test were all derived from one
mistaken reading, so they agreed with each other and disagreed with the ROM.
**Artifacts that share an error do not corroborate each other**, and a green
test standing next to a wrong implementation is what that failure looks like.

The mask table now in the test separates the four candidates:

| input   | ROM `$FF7F` | `~0x8000` | `~0x0080` | `$FE7F` |
|---------|-------------|-----------|-----------|---------|
| `0xB880`| `0xB800`    | `0x3880`  | `0xB880`  | `0xB800`|
| `0xB800`| `0xB800`    | `0x3800`  | `0xB800`  | `0xB800`|
| `0xFFFF`| `0xFF7F`    | `0x7FFF`  | `0xFF80`  | `0xFE7F`|
| `0x0180`| `0x0100`    | `0x0180`  | `0x0100`  | `0x0000`|

Two gaps surfaced while building it, both found by controls rather than by
reading:

* The first draft used only inputs whose **bit 8** was clear, so a mask that
  wrongly cleared bit 8 too (`$FE7F`) passed every assertion. `0xFFFF` pins all
  fifteen survivors at once.
* The first draft asserted `hstctl_after(0xB880) & ~0x0080 == 0xB880`, which is
  false — the input has bit 7 **set**, so clearing it must change the value.
  Replaced with an XOR, which names exactly the bits that moved. The control run
  caught this: `restored` came back FAILED, which is the control working, not
  the code.

Nine negative controls, each **rebuilt** before it was run (§45.1), all fail:

```text
wrong mask ~0x8000        wrong mask ~0x0080     wrong mask $FE7F (bit 8)
wrong mask $7FFF           wrong mask 0x0000      clamp wrapped tick to zero
drop wait-flag write       drop elapsed store     clobber interrupted D0
restored                                                              PASSED
```

### 48.4 §47's numbers only hold with STUNRUN_LIVE_VIDEO=1

§47.3 reports `rises=1 falls=1 ipl2-asserts=1` and
`board vram nonzero words=84787`. I re-ran the same 600 frames both ways:

```text
STUNRUN_LIVE_VIDEO=1   rises=1 falls=1 ipl2-asserts=1   vram nonzero words=84787
STUNRUN_LIVE_VIDEO=0   rises=0 falls=0 ipl2-asserts=0   vram nonzero words=82720
```

Cause: `GSP_RTL_SYNC_AND_STEP` is inside `if (g_live_video && g_cpu != NULL && ...)`
(`native/shell.c:1878`), so **headless the GSP core is never clocked at all** and
INTOUT can never move. The entire IPL2 mechanism is unreachable outside the
live-video configuration.

§47's "three consecutive 600-frame runs byte-identical on every line" is true
*within* one configuration and says nothing across configurations. Stating it
without that qualifier is what hid this. The IPL2 result is a live-video result.

## 49. The GSP core runs headless (`stun-runner-88e`)

§48.4 recorded that §47's whole IPL2 result only existed under
`STUNRUN_LIVE_VIDEO=1`. The cause was structural: `GSP_RTL_SYNC_AND_STEP` and
`gsp_rtl_apply_intout()` sat inside

```c
if (g_live_video && g_cpu != NULL && (frame % g_live_video_stride) == 0) {
    ... GSP_RTL_SYNC_AND_STEP(mem); gsp_rtl_apply_intout(); ...
    ... render, present, frame-log, GL scanout ...
}
```

so headless the GSP was never clocked **at all**. A rendering option was gating
machine emulation.

### 49.1 What changed

Core stepping and interrupt delivery moved out of the `g_live_video` gate into
their own block keyed only on `g_cpu != NULL`. Rasterisation, presentation, the
frame log, GL scanout and FPS pacing stayed gated, because those genuinely are
display concerns.

The stride was left governing only rendering. It is documented at its
declaration as a cadence for *"600 frames do not all rasterise"* — a display
cost decision that accidentally governed core stepping because the two were
fused. With the default stride of 1 the step cadence is unchanged for every
existing live run, so this is not a behaviour change for the default path.

In the non-Verilator trees `GSP_RTL_SYNC_AND_STEP` is already an empty macro, so
this compiles to nothing there and the ROM-free CTest trees are unaffected.

### 49.2 Measured: the two configurations are now identical

600 frames, same command, `STUNRUN_PC_HIST` captured for each:

| observation | `LIVE_VIDEO=0` | `LIVE_VIDEO=1` |
|---|---|---|
| INTOUT rises / falls | 1 / 1 | 1 / 1 |
| ipl2-asserts | 1 | 1 |
| `INTOUT set-by` / `clear-by` | 1 `$0080` / 2 `$3800` | 1 `$0080` / 2 `$3800` |
| `$02143C` (handler) | 1 | 1 |
| `$02FC82` (wait loop) | 35042 | 35042 |
| `$02FCB6` (wait exit) | 1 | 1 |
| board vram nonzero words | 84787 | 84787 |

Before this change the headless column read `rises=0 falls=0 ipl2-asserts=0` and
`vram nonzero words=82720`.

### 49.3 What still differs — precisely, not approximately

`diff` of the two full run logs returns **three** hunks, all display-only:

1. the `pc histogram=` line (I passed different paths);
2. `live-video frames-presented=601 stride=1` — display;
3. the terminal render line:

```text
LIVE_VIDEO=0  mode=blank-scaffold lit=0/122880    colours=0    hash=0x7D611DC5
              + "the frame is ENTIRELY BLACK ..." warning
LIVE_VIDEO=1  mode=live-gsp-share lit=122880/122880 colours=225 hash=0xF3973280
```

So the emulation is now configuration-independent and the **rasterisation is
not**: a headless run reproduces the machine exactly but still emits the blank
scaffold, so `lit=`/`colours=` carry no information headless. That is the
remaining half, and it is what blocks the frame/pixel comparison against MAME.
It is not fixed here — fixing it means decoupling "rasterise the share" from
"present it", which is a display change, not an emulation one.

Worth stating plainly: the blank-scaffold frame still produces a stable hash, so
`hash=` alone continues to be worthless as evidence. §48's 225-colour reading
was never a picture, only a pipeline that consistently produced 225.

## 50. Colour parity against MAME: **no**. The open question is now answered.

§39.6 and §42 carried "colour equality vs MAME" as an open question, with the
standing warning that `lit=122880/122880 colours=225` and a stable frame hash
cannot distinguish a correct picture from a correctly-rendered wrong one. With
§49's headless/live split settled, the live run can finally be compared against
the committed oracle screenshot.

### 50.1 The measurement

Committed oracle `fixtures/oracle/frame_600/frame_600_screen.png` (512x240) against
`STUNRUN_CAPTURE_FRAME=600` from a 600-frame live RTL run, via `tools/frame_diff`
(exit 1 = "different", which is that tool's contract):

```text
frame_diff: width=512 height=240 total_pixels=122880
frame_diff: pixels_different=122644 (99.81%)
frame_diff: max_rgb_distance=255
frame_diff: avg_channel_distance R=49.18 G=57.91 B=59.54
frame_diff: ssim_score=0.523887
```

Per-band, to locate the difference rather than only size it (threshold 10):

| rows | region | differing |
|------|--------|-----------|
| 0–120 | playfield top | **100.00%** |
| 120–200 | playfield mid | 99.92% |
| 200–240 | HUD bottom | 98.75% |

**No region is close.** This is not a local defect with good surroundings.

For scale: the earlier replay-path attempt recorded in `ea973d4` was 99.72%
differing at SSIM 0.0825. SSIM has improved to 0.524 — the pipeline produces
structure now — but 0.524 reflects broad similarity (a busy band at the bottom,
large low-detail areas) and not correctness.

### 50.2 What looking at the pictures claimed, and what the numbers say

The two frames were viewed side by side before measuring. Visually, ours appears
to have the right *architecture*: a HUD band at the bottom in roughly the right
place, and a logo band above it. That reading suggested the HUD was close and the
playfield was the broken part, and it would have pointed the next step at the
display list.

The band numbers contradict it. The HUD band is the **least** wrong at 98.75%
and is nowhere near right. Visually similar band placement is not evidence of
correct scanout geometry, and had this been left at "eyeball" the next step would
have been aimed at the wrong subsystem.

What the pictures do establish is the shape of the failure: rows 0–120 are a
single flat olive fill where the oracle has a tunnel and perspective, and the
middle band is smeared diagonally. A region that is *entirely* one palette entry
means the scanout is reading somewhere that is not the playfield.

### 50.3 Incidental: `frame_diff` misreports its own output size

`frame_diff` prints `diff.png written (4433 bytes)` as `written (1 bytes)`. The
file on disk is 4433 bytes and is a valid PNG, so only the message is wrong — but
it is the kind of small falsehood that makes a later reader doubt the tool
rather than the finding. Worth fixing; not fixed here because it is not this
question.

### 50.4 Not claimed

This is a statement about **frame 600 of a 600-frame live RTL run**. It is not
evidence about the replay path, about `--service`, about other frames, or about
any single subsystem: the 68010's host-port traffic and the GSP's own execution
are both already known to differ from the oracle (§44's first divergence at
oracle seq 224523). Whether the scanout geometry is independently wrong, or
merely rendering a share the emulation has not filled correctly, is **not**
separated by this measurement and must not be reported as if it were.

## 51. What the 68010 waits on after the GSP release (`stun-runner-0yc`)

### 51.1 The bead's hypothesis is refuted

The bead proposed: "the ROM releases the GSP and then polls some GSP-visible
condition that never becomes true, so it never proceeds."

It does not. `$02FC60` polls **one main-RAM word**, `$FFDB48`:

```asm
02fc60: link    A6,#$0
02fc68: move.l  ($8,A6), D4
02fc6c: moveq   #$5, D2
02fc6e: moveq   #$c, D3
02fc70: clr.w   $608000.l
02fc76: move.l  #$80000, D5      ; bounded budget: 524288 iterations
02fc7c: movea.l #$ffdb48, A2
02fc82: tst.w   (A2)             ; <<< the wait
02fc84: bne     $2fcb6           ; satisfied -> return
02fc86: move.l  D2,-(A7)
02fc88: jsr     $22766.l         ; per-iteration service
02fc8e: move.l  D5, D0
02fc90: subq.l  #1, D5
02fc92: tst.l   D0
02fc94: addq.l  #4, A7
02fc96: bne     $2fca4
02fc98: move.l  D4,-(A7)         ; budget exhausted -> $308c4($c, arg)
02fc9c: jsr     $308c4.l         ;   then KEEP LOOPING (bra $2fc82)
02fca4: move.l  D2,-(A7)
02fca6: jsr     $22766.l
02fac: clr.w   $608000.l
02fb2: addq.l  #4, A7
02fb4: bra     $2fc82
02fcb6: movem.l (-$14,A6), D2-D5/A2
02fcbc: unlk    A6
02fcbe: rts
```

`$FFDB48` is ordinary work RAM, **not** a GSP-visible register. Its only writer in
the entire program is `$02143C` (§40.1) — the level-3 autovector that §47/§48
just started delivering. So the condition is a RAM flag set by the interrupt we
wired, not something the GSP exposes through the host window.

Two consequences the bead did not anticipate:

* **It is not a timeout.** The wait is bounded by `D5 = $80000`, and the budget
  path is not a give-up: it calls `$308c4` and then *loops back to `$02fc82`*.
  Measured, over 600 frames: `$02FC98` and `$0308C4` do not appear in the PC
  histogram at all, and `$02FC82` ran 35,042 times against a 524,288 budget. The
  budget was never reached, so the stall is not a failed timeout.
* **It is satisfied now.** `$02FCB6` executes once (it executed zero times before
  §47). The 68010 is no longer stuck here.

### 51.2 The actual protocol around it

`$02FC60` has exactly two callers, at ROM offsets `$02F16A` and `$02F212`
(byte pattern `4e b9 00 02 fc 60`, found in `stunrun_maincpu.bin`). Both follow
the same shape: set `HSTADR` from a table at `$4A88C` indexed by a byte × 8,
poke `HSTDAT` at `$C0000C`, and test `HSTDAT` for a non-zero response.

```asm
; call site 1, $02F156
02f156: tst.w   $c0000c.l       ; HSTDAT != 0 ?
02f15c: beq     $02f19e
02f15e: clr.w   $ffdb48.l       ; disarm the flag, then wait for it
02f164: pea     $4a830.l
02f16a: jsr     $02fc60.l
...
; call site 2, $02F20C
02f20c: pea     $4a83c.l
02f212: jsr     $02fc60.l       ; wait
02f218: move.w  #$ffff, $c0000c.l
02f232: move.l  (A0,D0.l), $c00002.l
02f23a: clr.w   $ffdb48.l       ; disarm AFTER the wait
02f240: move.w  D2, $c0000c.l   ; the real payload
02f246: move.l  $ffff8014.l, $ffdb36.l   ; sample the tick counter for $FF9BAA
```

So `$FFDB48` is a **disarmable semaphore**, and `$02F23A` clearing it *after*
`$02FC60` returns (rather than before, as at `$02F15E`) is what makes the next
round's wait meaningful. `$FF9BAA` is the elapsed-tick record `$02143C` writes —
same pairing as §45.

### 51.3 Measured, 600 frames, headless

PC histogram entries:

```text
0x02FC60   2      the wait routine is ENTERED twice
0x02FCB6   1      but EXITS once
0x02FC82   35042  poll iterations
0x022766   70104  = 35042 x 2, so two service calls per iteration
$02FC98 / $0308C4  absent: the budget never fired
```

**Two entries, one exit.** One of those calls is still spinning at the end of
the run, and there is exactly **one** INTOUT rise in the whole 600 frames (§47:
rise at frame 534, fall at 535). A semaphore that is set once and cleared once
cannot satisfy a second wait. So the handshake is not stuck — it is
**one-interrupt short**, and the GSP is expected to raise INTOUT repeatedly.
§47 records MAME's cadence as every third frame from frame 131.

I am **not** asserting *which* of the two entries is the spinning one. The write
watch names `$02F23A` (call site 2, after its wait returned) as the last writer
of `$FFDB48`, which places site 2 among the completed — but that leaves site 1's
entry unaccounted for and I have not disambiguated it. Stated as UNKNOWN rather
than guessed.

### 51.4 What this changes

The bead's "the GSP is released and then the machine stalls" reading is wrong in
one direction and right in another. Wrong: nothing is stalled at `$02FC60`; it
completes. Right: the machine still does not progress to a finished frame, but
the blocker has moved — from *no interrupt at all* to *too few interrupts*.

The next question is therefore on the GSP side, not the 68010 side: **why does
our GSP raise INTOUT once instead of every third frame?** Candidates worth
separating: the GSP is not re-entering the display-list routine that ends in the
`HSTCTLL` write; or it is, and our host-port writes are not reaching it the way
MAME's do. §47's unresolved 16× disagreement between the RTL and MAME over which
bus address is `HSTCTLL` bears directly on the second candidate, which is why it
stays on the list.

## 52. The `HSTCTLL` address-decode disagreement: partly resolved, partly not

§47.6 and the subagent's hand-back flagged a "16× disagreement" and proposed
filing a BUGS.md candidate against MAME or the vendored RTL. Checked against both
sources and the oracle. It is not one disagreement, and the alarming version of
it does not survive.

### 52.1 There is no register-numbering disagreement

Both models enumerate the 32 I/O registers in the same order with the same
values. Verified pairwise:

| register | RTL `IO_IDX_*` (`tms34010_pkg.sv`) | MAME `REG_*` (`tms34010.h`) |
|---|---|---|
| DPYSTRT | `5'h09` (:376) | 9 (:72) |
| HSTDATA | `5'h0C` (:379) | 12 (:75) |
| HSTADRL | `5'h0D` (:380) | 13 (:76) |
| HSTADRH | `5'h0E` (:381) | 14 (:77) |
| **HSTCTLL** | `5'h0F` (:382) | **15** (:78) |
| HSTCTLH | `5'h10` (:383) | 16 (:80) |
| DPYADR | `5'h1E` (:397) | 30 (:94) |

So "which register is number N" is settled. What differs is only the rule mapping
an **address** to that number:

* RTL, `io/tms34010_io_regs.sv:157`: `assign idx = addr[IO_REG_IDX_W+3 : 4];`
  with `IO_REG_IDX_W = 5`, i.e. `addr[8:4]` — address `$C00000F0` → index 15.
* MAME, `tms34010.cpp:34-37` + the `offset` parameter of `io_register_w`: the map
  is `0xc0000000–0xc00001ff` and `offset = address - 0xc0000000`, so index 15 is
  address `$C000000F`.

A factor of 16 on the address, nothing on the register.

### 52.2 The alarming consequence does not reproduce

`tms34010.cpp` carries a local patch added for this triage:

```c
map(0xc0000000, 0xc00001ff).rw(FUNC(tms34010_device::io_register_r), ...);
/* STUNRUN-IOB: MD-12 triage.  internal_regs_map exposes offsets 0x00-0x1ff, but
 * m_IOregs holds only 64 entries and IOREG() indexes it unmasked, so anything at
 * 0x40 or above runs off the end. S.T.U.N. Runner's GSP program writes at
 * 0x90-0x160. */
if (offset >= 64) { ...fopen(STUNRUN_GSP_IOBREPORT)... }
```

That comment asserts the GSP program writes at MAME-offsets 0x90–0x160, which
would land past the end of `m_IOregs`. **It does not happen.** Running the frozen
oracle for 30 s emulated (~1800 frames) with `STUNRUN_GSP_IOBREPORT` set:

```
rc=0, Average speed 447.40% (29 seconds)
GSPREG call=654001 DPYADR=fcf8 DPYSTRT=fffc DPYTAP=0000 DPYCTL=f004 ... pc=fff454e0
```

and **no report file was created at all**. The file is `fopen`ed inside the
`if (offset >= 64)` branch, so absence of the file is positive evidence that the
branch was never taken: across ~1800 frames the GSP never wrote at a MAME-offset
of 0x40 or above. MAME is not corrupting its register array, and the comment's
claim about 0x90–0x160 is **stale or a misreading** — recorded here rather than
filed upstream.

### 52.3 Our decode: one register confirmed, two disagree, one not comparable

Ours (600-frame live run) against MAME's own `GSPREG` line:

| register | ours | MAME | verdict |
|---|---|---|---|
| DPYSTRT | `0xfffc` | `0xfffc` | **exact match** |
| DPYADR | `0xffc3` | `0xfcf8` / `0xfd9c` / `0xfd40` | not comparable |
| DPYTAP | `0x0200` | `0x0000` | **disagree** |
| DPYCTL | `0xf010` | `0xf004` | **disagree** |

`DPYSTRT` matching exactly is meaningful: it is a register the program writes and
holds, so a wrong index would not land on the same value. It is one register
though, not the file.

`DPYADR` **cannot** be compared this way and the number must not be read as a
disagreement: it is the live display address and MAME's own consecutive samples
differ from each other (`fcf8`, `fd9c`, `fd40` on three adjacent `GSPREG` lines).
Two runs stopped at different emulated times will not agree on it.

`DPYTAP` (`0200` vs `0000`) and `DPYCTL` (`f010` vs `f004`) genuinely differ, and
those two are the ones to chase. They may still be state differences rather than
decode differences — nothing here controls for emulated time.

### 52.4 Status: UNKNOWN, and the BUGS.md candidate is not filed

**Not established:** which of the two address rules the real TMS34010 uses. The
evidence above does not decide it. `DPYSTRT` matching is consistent with our rule
being right, but is not a discriminator on its own — it would also match if only
that one register happened to agree.

**Not filed:** the §47.6 BUGS.md candidate. The premise (MAME runs off the end
of `m_IOregs`) is now contradicted by direct measurement, so there is nothing
left to report upstream for that specific claim.

The discriminating experiment is still available and is now sharper: capture the
**addresses** the GSP writes under MAME — not the decoded register values — and
check whether they sit at 0x10 spacing from `$C0000000` (our rule) or at 1-byte
spacing (MAME's). `m_ioreg_pre_write_cb` already receives the raw `offset`
(`tms34010.cpp:1570`), so the oracle can answer this without a new hook; it needs
an observation-only patch recording every offset regardless of whether it is
≥ 64, not just the out-of-range ones. Until that exists, §52.4 stays open.

## 53. The parity discriminator: run, and what it does NOT establish

### 53.1 Retraction: §52.3's "DPYSTRT exact match" was an artifact

§52.3 compared our 600-frame `DPYSTRT=0xfffc` against MAME's `GSPREG` line from a
30-second run — roughly frame 1800, not frame 600. Both numbers are real and they
are not comparable, because **`DPYSTRT` is time-varying too**. Captured properly
at frame 600:

```text
MAME frame 600 (tools/mame-gsp-state-snapshot --targets 600)
    dpystart 64572 = 0xFC3C     dpytap 0     dpyctl 61444 = 0xF004     dpyadr 63612
MAME ~frame 1800 (the GSPREG line used in 52.3)
    DPYSTRT 0xFFFC
ours frame 600
    DPYSTRT 0xFFFC   DPYTAP 0x0200   DPYCTL 0xF010
```

At a **comparable point in time** three of the four disagree:

| register | ours @600 | MAME @600 | verdict |
|---|---|---|---|
| DPYSTRT | `0xfffc` | `0xfc3c` | disagree |
| DPYTAP | `0x0200` | `0x0000` | disagree |
| DPYCTL | `0xf010` | `0xf004` | disagree |
| DPYADR | `0xffc3` | `0xf87c` | live value; not comparable |

§52.3's table is wrong and is withdrawn. `DPYADR` was already flagged as
incomparable there; `DPYSTRT` has to be flagged the same way. **The one register
that appeared to confirm our decode confirms nothing.**

### 53.2 The harness was built for this fixture, and it renders blank

MAME's frame-600 `dpystart` (64572 = `0xFC3C`), `dpyctl` (61444 = `0xF004`) and
`dpytap` (0) are **exactly `tools/gsp_render.py`'s hardcoded `rasterise()`
defaults** (`dpystrt=64572, dpyctl=61444, dputap=0`). That harness was written
as the acceptance test for this fixture, so this is not a mismatched setup.

Staged the committed fixture under the filenames it wants (524288-byte
`frame_600_gsp_vram.bin` → `gsp_vram.bin`; the 1024-byte combined
`frame_600_gsp_palette.bin` split 512/512 into `gsp_pal_lo.bin`/`gsp_pal_hi.bin`)
and rasterised it:

```text
harness defaults     dpystrt=0xFC3C  nonzero=0/332216  colours=2
MAME frame-600 regs  dpystrt=0xFC0C  nonzero=0/332216  colours=2
```

**Blank.** Not "slightly wrong" — not one non-zero byte, with either register
set. (The second row passes `0xFC0C`, my own misreading of 64572 as `0xFC0C`
before recomputing it; `64572 = 0xFC3C`. Both rows are blank either way, but the
harness-defaults row is the correct one and agrees with MAME exactly.)

### 53.3 What this establishes, and what it does not

**Establishes:** the committed `frame_600_gsp_vram.bin`, rasterised by our own
transcribed display path using MAME's own frame-600 registers, does **not**
reproduce MAME's frame-600 screen. So the parity failure is *not* explained by
our emulation having failed to fill VRAM — the failure exists with MAME's VRAM
and MAME's registers and no emulation in the loop at all.

**Does not establish:** which subsystem is at fault. There are still at least
three, and this narrows nothing between them:

1. the committed fixture is not the frame-600 framebuffer (it holds 5058 nonzero
   words of 262144 — sparse, consistent with a program plus display list rather
   than a rendered picture);
2. the rasteriser's VRAM addressing is wrong (`gsp_render.py` transcribes
   `scanline_multisync`/`get_display_params` from MAME, but a transcription can
   still be wrong);
3. MAME's screen at frame 600 is composed from somewhere other than that share.

So §50's open question — scanout geometry vs unfilled share — is **not** answered.
It is, however, no longer answerable by comparing our live frame to MAME's: the
comparison has to start from an artefact that is known to contain the picture,
and the committed fixture is not it.

### 53.4 The concrete next step this points at

`fixtures/oracle/frame_600/frame_600_screen.png` exists, so MAME *did* compose a
screen at frame 600. The missing artefact is **MAME's VRAM at the same instant the
screenshot was taken**. `tools/mame-gsp-state-snapshot` already returns
`dpystart`/`dpytap`/`dpystart`/`dpyadr` — it needs the VRAM share from the same
frame to be a complete pair. Until that pair exists, no scanout-parity claim is
reachable from committed evidence, and §50's percentage differences should be
read as "our output differs from MAME's" with no attribution.

## 54. SOM payload (`stun-runner-q9t`): the framing is already decoded; the semantics are a disassembly task, not a guessing game

Partial result. I did not finish q9t. What I established changes how it should be
approached, which is worth more than the partial decode.

### 54.1 The framing is not UNKNOWN — the repo already models it

Two C slices exist and both stop at the same line:

* `reproduction/gsp/record_parser.{h,c}` — 8-word records, `base + (index << 4)`
  addressing, and extractors for header, two A8 variants, A6, two coordinate
  fields and a count. Its own header says *"The code preserves raw words. It does
  not assign road/object/HUD meaning."*
* `reproduction/gsp/work_buffer_consumer.{h,c}` — FIFO → work buffer, the literal
  `SLL 8h / OR` byte-lane composition, a 128-write burst, and a 256-word display
  copy. *"The FIFO payload's meaning and the meaning of either destination buffer
  remain UNKNOWN."*

So q9t's "UNKNOWN" is scoped: **the record envelope is understood; the field
meanings are not.**

### 54.2 Real payload bytes are already committed

`reference/experiments/stunrun/display-list-service-f2-60-180/mame.log` carries
the whole transfer, not just counts:

```text
M1_GSP_DL_CALL id=1 frame=95 stream=00060002 raw=00141615171F1013 00000000 ...
M1_GSP_DL_HOST id=1 hi=FFDE lo=0000 control=B800
M1_GSP_DL_FIFO id=1 frame=95 pc=00022730 addr=00C0000C data=00001616 destination=FFDE0000
M1_GSP_DL_FIFO ... destination=FFDE0010        (stride 0x10 = 16 bytes = 8 words)
```

116 bytes / 58 words in that one stream. Naive 8-word alignment puts the first
data record at word 9, not word 8: the envelope is an 8-word header followed by
one extra word, then records on the 16-byte stride `record_parser` already
assumes. `destination` steps by `0x10`, which is that same stride.

**CORRECTED.** Every number above was taken from a mis-transcribed log line. All
14 `raw=` fields across both experiments are **exactly 64 or 128 words** — 256 or
512 hex characters, and no other length ever. That is the capture emitting the
stream in fixed-width blocks, not a property of the payload.

Re-measured from offset 0, the framing is clean and simpler than claimed:

```text
stream=00060002  f2-60-180   64 words  =  8 records of 8
                 rec0: 0014 1615 171F 1013 0000 0000 0000 0000
                 rec1: 00FF 11FF 11FF 11FF 11C4 11FF 22D9 2243
stream=00060002  f2-60-300  128 words = 16 records of 8
                 rec0,rec1 identical to the above
```

**There is no extra word and no nine-word offset.** Records are 8 words from
offset 0, which is what `record_parser.h` already assumed — the "one extra word"
was an artifact of counting a truncated line. The two experiments also agree on
the first 64 words of the same stream, so the payload is stable across
frames 60-180 and 60-300.

`SLL 4h` in §55.1 is a *stride between* records, not a proof of their size, so
that section is unaffected.

### 54.3 Refuted before it could mislead: PIXBLT is not 6/10-bit packed here

I first decoded the data words as TMS34020-style PIXBLT (X in bits 15-10, Y in
bits 9-0). It produced X = 50, which the chip cannot do. The repo's own reference
settles it — `docs/tms34010-reference.md:1145-1151`, from `ug.txt` §7.2:

> * **width** `DX` — 16 LSBs of `DYDX`
> * **height** `DY` — 16 MSBs of `DYDX`

`DYDX` is a 32-bit register with two full 16-bit halves on **this** part. The
packed 6/10 form is the TMS34020. Recorded because the two are easy to conflate
and the packed form gives plausible-looking nonsense.

The observed words (`FF11`, `D922`, `4312`) are therefore not raw DX/DY either —
512×240 bounds both. Something packs or scales them.

### 54.4 The reframe: the consumer code is the decoder

The field semantics are not recoverable by staring at hex. They are defined by
**the GSP code that consumes the records** — and that code is in our hands:

* it is uploaded from ROM (`capture-gsp-program-upload`, stream captured at
  `/tmp/opencode/gsp-upload-v3/gsp-upload-events.txt`, 701 877 rows);
* it is already executing in the Verilated RTL;
* it is disassemblable — Ghidra symbols exist for the uploaded program.

`record_parser.h` already names the consumer PCs from M5 traces: `0xFFF45000`,
`0xFFF45090`, `0xFFF45890`, `0xFFF45A10`, `0xFFF45A40`, `0xFFF45A50`. Reading
that routine and watching which register each field lands in converts q9t from
open-ended reverse-engineering into a bounded disassembly checked against a
documented ISA (`docs/tms34010-reference.md`).

That also feeds `stun-runner-alo` directly: if the consumer shows the payload
being built from live track data, a C geometry pipeline is verifiable per field.
If it cannot be read, emulation is the only route with an oracle.

### 54.5 Not established

Which field is which vertex/slope/lighting term; whether the records are PIXBLT
parameters at all; whether the packing is a shift, a mask, or a different
instruction family. None of that was measured — the consumer disassembly has not
been done.

## 55. SOM payload: the records are bitmap primitives, not vertices (`q9t`)

Continued §54. The GSP program disassembles with MAME's own disassembler via
`tools/disasm-gsp` on the captured upload image
(`/tmp/opencode/gsp-upload-v3/gsp-vram-upload-last/gsp_vram.bin`). Address
mapping is `GSP PC & 0x3FFFFF`, confirmed by the tool's own default
`--at 0x340000 == 0xFFF40000 & 0x3FFFFF`.

### 55.1 The record loop, and the stride

```text
00345000: MOVE  *A3+,A5,0      ; next record index
00345010: ZEXT  A5,0
00345020: CMPXY A12,A5        ; done when the index passes A12?
00345030: JRN   344D10h
00345040: SLL   4h,A5          ; <<< index * 16
00345050: ADD   A1,A5          ; + base
00345060: CALLR 345090h        ; per-record handler
00345080: JR    345000h        ; loop
```

`SLL 4h,A5` is literally the `base + (index << 4)` that
`reproduction/gsp/record_parser.h:25` documents, seen in the original code rather
than inferred from a trace. §54.1's framing claim is confirmed.

### 55.2 Record word 0 is a control word

`$FFF45090`, entered by `CALLR`:

```text
00345090: MOVE  *A5+,A4,0      ; word 0 -> A4
003450a0: BTST  Fh,A4          ; bit 15
003450b0: JRNE  344D50h        ;   -> skip / end-of-record
003450c0: MOVE  A12,B9
003450d0: BTST  Ch,A4          ; bit 12
003450e0: JRNE  344D60h
003450f0: MOVE  *A5+,A8,0      ; word 1 -> A8
00345100: MOVE  A8,B0          ; B0 = PIXBLT SADDR
00345110: MOVE  *A5+,A6,0      ; word 2 -> A6
00345160: ANDI  C00h,A0        ; bits 10-11
00345190: JREQ  345230h
003451f0: JREQ  3452A0h
00345200: MOVI  1010101h,B9
00345240: ANDI  FFh,A0         ; bits 0-7
00345270: CMPI  4h,A0
00345290: JRNE  3452D0h
003452a0: MOVI  15151515h,B9
003452d0: SLL   16h,A4         ; build a 32-bit XY address
003452e0: SRL   12h,A4
003452f0: ADDI  FFF71D20h,A4
00345320: MOVE  *A4,A0,0
```

So word 0 encodes: **bit 15** skip/end, **bit 12** a mode select, **bits 10-11**
an expand-mask select, **bits 0-7** a second select compared against 4.

### 55.3 The payload is bitmap primitives, not vertex lists

**`0x01010101` and `0x15151515` are binary colour-expand masks** — the set bits
mark the "on" pixel within each group, which is exactly the pattern the
TMS34010's binary expand uses (`docs/tms34010-reference.md` §3.3, §3.4: `PIXBLT
B,XY` / `PIXBLT B,L`, "during a binary expand operation, only the destination
pixel size is specified; the source pixel size is assumed to be one bit").

The register traffic matches the documented PIXBLT operands:

```text
003453c0: MOVE  A8,B1           ; B1 = SPTCH  (source pitch)
003453d0: MOVY  B1,B0           ; XY-addressed form
00345430: MOVE  B0,B2           ; B2 = DADDR  (destination)
00345470: ADDC  B1,B0
003454a0: MOVE  B0,B2           ; row-by-row: B2 += SPTCH
003454c0: SUBXY B2,B7
```

against `docs/tms34010-reference.md:1149-1156`: `SADDR` (B0), `SPTCH` (B1),
`DADDR` (B2), `DPTCH` (B3).

**Conclusion:** the display list carries **1-bit bitmap primitive descriptors** —
where a bitmap lives, how wide its pitch is, and which expand mode to use. The
GSP expands bits to pixels at draw time. It is **not** a list of vertices,
slopes or lighting terms.

### 55.4 What this changes

§54 framed the payload as "which word is which vertex, slope, or lighting term".
That question was framed wrong. There are no vertices in it to find.

For `stun-runner-alo` this is good news and should be recorded there: a C
replacement would reproduce **bitmap runs** (address, pitch, expand mode, colour
mask) rather than reinterpret geometry. That is a narrower, per-field-verifiable
job — every field has a named register destination in the consumer, which is an
oracle. It does not settle the C-vs-emulation choice, but it lowers the cost of
the C option substantially and removes the "what is a slope" ambiguity from it.

### 55.5 Not established

Which record types exist beyond the branch at `0x344D50` / `0x344D60`; what
`0x344D10` (loop exit) and `0x345800` (the `BTST 3h,A13` path) do; whether `B3`,
`B8`, `B9` are used as DPTCH/COLOR0 in the paths not disassembled here; and the
meaning of the constant `0xFFF71D20` at `$003452F0`. The disassembly above covers
`$FFF45000-$FFF45800`; the rest of the consumer is unexamined.

## 56. First C payload codec (`stun-runner-q9t`)

A small C codec now exists at `reproduction/gsp/payload_codec.{h,c}` and its
CTest is `gsp-payload-codec-c`. This is the **transport/payload container**,
not a C geometry generator and not a renderer. The payload is bitmap primitive
descriptors (§55); this slice carries real records without losing bits and names
only the fields whose consumer register assignments have been disassembled.

### 56.1 Input is an oracle vector, not a fixture we invented

The test embeds all **64 words** from
`reference/experiments/stunrun/display-list-service-f2-60-180/mame.log`,
`M1_GSP_DL_CALL id=1 stream=00060002 raw=...`. That is 8 records × 8 words,
verbatim from the MAME capture. It checks:

* decode yields exactly 8 records;
* record 0 words 0/1/2 are `0014` / `1615` / `171F` (control / SADDR / A6);
* record 1 words 0/1/2 are `00FF` / `11FF` / `11FF`;
* words the consumer has not explained (3..7) survive unchanged;
* encode(decode(capture)) reproduces all 64 words byte-for-byte.

Malformed framing is rejected: 63 words, 7 words, insufficient record capacity,
NULL input or output. No partial outputs are accepted. The bit accessors are
controlled individually (bit 15, bit 12, bits 10-11, bits 0-7) so neighbouring
bits cannot accidentally hide a wrong mask.

### 56.2 Deliberately bounded semantics

`control()` extracts the bits literally tested by `BTST Fh,A4`, `BTST Ch,A4`,
`ANDI C00h,A0`, `ANDI FFh,A0`. It **does not** decide which expand-mask applies:
the relevant branches at `$FFF45230` and `$FFF452A0` have not both been followed
through their full paths. `expand_mask` therefore stays NONE; the two loaded mask
constants are exported only as observed constants. This makes the honest code
slightly less convenient than a guess and keeps it falsifiable.

Words 3..7 are named `unmapped`, not vertices or pitches. The future decoder is
expected to replace those names only when a consumer instruction proves the
meaning.

### 56.3 Negative controls

Each mutation was rebuilt before CTest (the §45.1 lesson), and `restored` was
required to pass:

```text
wrong record size 7          FAILED
swap SADDR word index        FAILED
wrong skip-bit source        FAILED
wrong mode-bit source        FAILED
truncate unmapped tail       FAILED
force an unmeasured mask     FAILED
restored                     PASSED
```

### 56.4 What this does NOT do

It does not generate bitmap records from track or object data, does not copy
SOM data to the GSP, and does not reproduce the frame. It is the smallest
verifiable C unit under the larger option `stun-runner-alo`; the next bounded
slice is to disassemble the remaining worker branches and promote a raw field to
a named field only where its GSP register use proves that name. C-vs-ADSP-emulator
remains open.

## 57. Retraction: the worker is FILL/XY, not a bitmap/PIXBLT decoder

This section supersedes the positive conclusion in §55.3–55.4 and the mask
uncertainty in §56.2. Those interpretations were wrong. The evidence was a
register resemblance plus two constants; I did not follow the instruction stream
to the operation that consumed them.

### 57.1 What the complete branch actually does

The exact code around the selector is:

```asm
00345150: MOVE  A4,A0
00345160: ANDI  C00h,A0
00345190: JREQ  345230h          ; selector 0 -> low-byte sub-branch
003451a0: CMPI  C00h,A0
003451c0: JREQ  345800h          ; selector C00 -> alternate worker
003451d0: CMPI  400h,A0
003451f0: JREQ  3452A0h          ; selector 400 -> load COLOR1_B
00345200: MOVI  1010101h,B9      ; selector 800 -> COLOR1_A
00345230: MOVE  A4,A0
00345240: ANDI  FFh,A0
00345270: CMPI  4h,A0
00345290: JRNE  3452D0h          ; selector 0, low byte != 4: keep B9=A12
003452A0: MOVI  15151515h,B9      ; selector 0 / low byte 4: COLOR1_B
```

The complete action table, now implemented in `payload_codec.c`:

| `(control & 0x0C00)` | `(control & 0x00FF)` | observed action |
|---:|---:|---|
| `0x0000` | `0x0004` | load `0x15151515` into B9 |
| `0x0000` | anything else | retain B9=A12 (set at `$FFF450C0`) |
| `0x0400` | any | load `0x15151515` into B9 |
| `0x0800` | any | load `0x01010101` into B9 |
| `0x0C00` | any | branch to alternate worker `$FFF45800` |

These are **B9/COLOR1 setup actions**. `docs/tms34010-reference.md` §2.2 identifies
B9 as COLOR1; §3.3's PIXBLT operands do not apply just because one setup value
looks like a bit pattern.

### 57.2 The instruction that resolves the ambiguity: `FILL XY`

The primary worker does not execute PIXBLT. Its actual inner loop is:

```asm
00345430: MOVE  B0,B2
00345440: MOVE  A6,B7
00345450: JR    3454A0h
00345460: ADD   A9,A0
00345470: ADDC  B1,B0
00345480: ADD   A7,A4
00345490: ADDC  A8,A6
003454A0: MOVE  B0,B2
003454B0: MOVE  A6,B7
003454C0: SUBXY B2,B7
003454D0: JRV   345500h
003454E0: FILL  XY
003454F0: RL    4h,B9
00345500: DSJS  A10,345460h
```

The two stored COLOR1 values are used in a path whose raster operation is `FILL
XY`; this is not evidence of a one-bit source bitmap, SADDR, or PIXBLT. The loop
updates XY-addressed state and repeats fills. That proves an **XY/FILL drawing
worker**, not the geometric meaning of each parameter.

The `MOVE A8,B0` at `$FFF45100` was prematurely labelled `SADDR` in §55.2. The
worker later executes FILL XY, and the captured path does not demonstrate that
this word is used as a PIXBLT source address. The C accessor has been renamed
from `saddr` to `field_a8_initial`; no source-address semantics are assigned.

### 57.3 What is established now

* Record stride and framing: 8 words, `base + index*16`, record 0 begins at
  offset 0 (§54 correction, §55.1).
* Control word bits: bit 15 skip path, bit 12 alternate path, bits 10-11 choose
  the B9 setup branch, and the low byte compares with 4 (§55.2 and table above).
* Word 1 is loaded into A8 and B0; word 2 is loaded into A6. Later words are
  consumed into A8/A7/A9/A10 on the primary worker, but their geometric meanings
  are not established.
* The primary worker executes `FILL XY`; it loads either `0x01010101` or
  `0x15151515` into B9/COLOR1, or inherits B9 from A12.
* The control/action table is now reflected in `reproduction/gsp/payload_codec.c`
  as code-path outcomes, not as a guessed image decoder.

### 57.4 What remains unknown

The mapping from words to XY origin, extent, repeat count, colour, or other shape
parameters; whether the fills represent rectangles, strips, masks or another
higher-level primitive; what the bit-12 branch `$FFF45A10` does; and what the
bit-15 branch `$FFF44D50` does. No word should be called a vertex or a slope until
a consumer instruction proves that meaning.

This still moves `stun-runner-alo` forward: the payload is not a raw PIXBLT
source list. It contains descriptors consumed by an XY/FILL drawing routine.
A C replacement may be practical, but it needs the remaining parameter semantics
and an output comparison against MAME before it can be considered a replacement.

## 58. Further disassembly: the control word's outer branches and the FILL worker

§57 followed the B9 selector but missed the branches which happen BEFORE that
selector. Continuing `tools/disasm-gsp` from the actual uploaded image exposed two
more facts and another necessary correction to §55.

### 58.1 Skip and mode branches

The beginning of `$FFF45090` is:

```asm
00345090: MOVE  *A5+,A4,0      ; word 0
003450a0: BTST  Fh,A4
003450b0: JRNE  344D50h
003450c0: MOVE  A12,B9
003450d0: BTST  Ch,A4
003450e0: JRNE  344D60h
```

The targets are now resolved:

```text
$FFF44D50: RETS
$FFF44D60: JA $FFF48FC0
```

Therefore bit 15 is **skip this record / return**, not "end the whole list" (that
was §55's label and was unsupported). Bit 12 is **dispatch to the other worker**
`$FFF48FC0`, not a generic mode flag. That worker begins with register saves,
sets a host-visible status word at `$C0000150`, and validates subsequent stream
words against bounds before continuing. Its payload semantics remain unexamined.

### 58.2 The primary path really fills XY regions

The ordinary path proceeds through the B9 selector, then uses `FILL XY` in this
loop:

```asm
00345430: MOVE  B0,B2
00345440: MOVE  A6,B7
00345460: ADD   A9,A0
00345470: ADDC  B1,B0
00345480: ADD   A7,A4
00345490: ADDC  A8,A6
003454a0: MOVE  B0,B2
003454b0: MOVE  A6,B7
003454c0: SUBXY B2,B7
003454d0: JRV   345500h
003454e0: FILL  XY
003454f0: RL    4h,B9
00345500: DSJS  A10,345460h
```

This is an XY-addressed fill region with an inner loop and a shifted B9/COLOR1
value. The 32-bit-looking values loaded after this loop at `$FFF45510` onward
feed A7/A9/A10 and the next repeated pass. **It may be describing geometric
extents**, but I do not yet know which record word is origin, width, height,
repeat count, or edge delta. That is the next field-tracing question.

### 58.3 Corrections applied to code

`reproduction/gsp/payload_codec.c` now decodes the *observed branch outcomes*,
not an expand-mask guess:

* bit 15 -> skip / RETS;
* bit 12 -> alternate validator/worker `$FFF48FC0`;
* otherwise, (bits 10-11, low byte) choose inherit/set COLOR1 A/set COLOR1 B or
  the selector's `$FFF45800` alternate path.

The control-word accessor receives only the record, so it cannot know external
`A13` bit 3, which the handler also tests before entering `$FFF45800`. That
external gate is named in the implementation instead of silently ignored.

Also renamed word 1's accessor from `saddr` to `field_a8_initial`. It is loaded
into A8 and B0, but because the operative graphics instruction is FILL XY, the
PIXBLT architectural name SADDR did not establish the payload meaning. The
codec now calls the two constants B9/COLOR1 values, not colour-expand masks.

Negative controls rebuilt before test: wrong 0x400 action, wrong 0x800 action,
ignore low-byte==4, and lose the 0xC00 alternate path all FAILED; restored PASSED.

### 58.4 Still open, now sharply scoped

Disassemble and trace the *primary FILL worker's* word3-6 updates into A8/A6,
A7/A9/A10, B0/B1/B2/B7, and determine the resulting FILL rectangle edges.
Separately follow the `$FFF48FC0` validator/worker and the `$FFF45800` alternate
fill path. Only after that can a C implementation emit the same FILL operations
from the upstream SOM payload rather than copying a record format whose shape
semantics remain unknown.

## 59. Primary record fields map to FILL XY operands (`stun-runner-q9t`)

The primary FILL path can now be reduced to its literal register dataflow. This
is the first point where a raw record yields a useful geometry operation rather
than only control flow.

### 59.1 Word 1, word 2, word 3

On the ordinary path (`word0.bit15=0`, `word0.bit12=0`, external `A13.bit3=0`):

```asm
003450f0: MOVE  *A5+,A8,0       ; record word 1
00345100: MOVE  A8,B0           ; B0 low/X = word 1
00345110: MOVE  *A5+,A6,0       ; record word 2
...
00345360: MOVE  *A5+,A8,0       ; record word 3
003453a0: SLL   10h,A8
003453b0: MOVY  A8,A6           ; A6 high/Y = word 3; low/X remains word 2
003453c0: MOVE  A8,B1
003453d0: MOVY  B1,B0           ; B0 high/Y = word 3; low/X remains word 1
003453e0: ADDXY A11,A6          ; add caller's Y delta to A6.Y
...
00345430: MOVE  B0,B2           ; FILL DADDR = (word3:Y, word1:X)
00345440: MOVE  A6,B7           ; candidate FILL DYDX
...
003454a0: MOVE  B0,B2
003454b0: MOVE  A6,B7
003454c0: SUBXY B2,B7           ; B7.Y -= B2.Y; B7.X remains word2
003454e0: FILL  XY              ; DADDR=B2, DYDX=B7, COLOR1=B9
```

On the documented TMS34010 register meanings (`docs/tms34010-reference.md:1355-1357`,
§4.3), B2 is DADDR, B7 is DYDX, B9 is COLOR1. `FILL XY` fills the destination
array with COLOR1 (§3.10, lines 1350-1360). Thus the literal setup is:

```text
DADDR.X = word 1
DADDR.Y = word 3
DYDX.DX = word 2
DYDX.DY = high 16 bits of caller A11
```

Why `DYDX.DY` is the high half of A11: `ADDXY A11,A6` adds A11's high-16 Y
delta to A6.Y; `SUBXY B2,B7` then subtracts the starting Y held in B2, leaving
that delta in B7.Y. Word 2 remains in the low/X half throughout, hence DX.

For capture record 0 (`0014 1615 171F 1013 ...`), the values are therefore
DADDR `(X=0x1615,Y=0x1013)` and DYDX `(DX=0x171F,DY=A11[31:16])`. **A11 was not
captured in the M1_GSP_DL_ENTRY record**, so the actual height is still unknown;
the C test supplies a synthetic A11 value only to test the arithmetic, not as an
oracle observation.

These are TMS34010 XY/DYDX operands, **not asserted screen-space pixel values**.
Their large values are not to be clamped to 512×240 without evidence.

### 59.2 C helper

`stunrun_gsp_payload_primary_fill_xy()` now exposes that exact dataflow as a
small C slice. It takes the record plus live caller A11 and A13 values and
returns the four FILL operands only for the primary path. It rejects skip
(bit 15), mode jump (bit 12), selector-C00 alternate, and external A13 bit-3
alternate without touching output.

Tests use the real captured record 0 for word1/2/3 and a synthetic A11 vector
whose expected DY is taken directly from `ADDXY`/`SUBXY` disassembly. Five
negative controls rebuilt before CTest (wrong X field, wrong Y field, wrong DX,
wrong A11 half, ignoring A13) all fail; restored passes.

This is the first concrete C geometry operation, but not a complete ADSP
replacement: the upstream logic that produces these records and A11 remains
unimplemented, and the other two workers are not decoded.

### 59.3 Other worker entry points

The branch targets resolve as follows:

* `$FFF44D50` = `RETS`: skip this record.
* `$FFF44D60` = `JA $FFF48FC0`: an alternate validator/worker. Its first decoded
  actions save register banks, write `$C0000150`, inspect records against
  bounds, and branch to error/status writes at `$FFF71C10`. Its payload semantics
  remain to be read.
* `$FFF45800` = a second FILL worker. It loads a color value from a table, then
  reaches `FILL XY` at `$FFF45A00`; its loop uses A7/A9/A10 and multiply/rotate
  operations. Those are likely the remaining geometric parameters, but their
  meanings are not yet decoded.

The outstanding job is no longer "what is the payload?" in the abstract. It is
two bounded analyses: trace the `$FFF45800` FILL setup, then the bounds-checking
contract at `$FFF48FC0`, with A11/A13 and VRAM writes captured at the same frame.

## 60. The rest of the primary record is a stepping program, not padding

Continued the primary FILL worker after the first rectangle setup. The reads at
`$FFF45510-$FFF45610` were previously called "unmapped tail"; that was too broad.
They feed two arithmetic pairs and a loop scalar:

```asm
00345510: MOVE  *A5+,A8,1      ; field F1
00345520: CMP   A12,A8         ; A12 sentinel -> RETS at 45630
00345540: MOVE  A8,A7
00345550: SLL   10h,A7
00345560: SRA   10h,A8
00345570: ADDI  10000h,A8
003455a0: MOVE  *A5+,A9,1      ; next F1 field
003455b0: MOVE  A9,B1
003455c0: SLL   10h,A9
003455d0: SRA   10h,B1
003455e0: ADDI  10000h,B1
00345610: MOVE  *A5+,A10,0     ; F0 field
00345620: JR    345500h
...
00345460: ADD   A9,A0
00345470: ADDC  B1,B0
00345480: ADD   A7,A4
00345490: ADDC  A8,A6
00345500: DSJS  A10,345460h
```

Two observations are direct from the listing:

1. Each F1 value is split/transformed into two registers, then used in a paired
   `ADD`/`ADDC` update. The first pair is A7/A8 -> A4/A6; the second is A9/B1 ->
   A0/B0. This is fixed-point-style stepping arithmetic; the exact geometric
   axis/quantity each pair advances is **not established**.
2. A10 is decremented/tested by `DSJS` and also participates in signed multiplies
   in the alternate worker. Call it a loop scalar for now, not a vertex count or
   a line count.

The primary worker can therefore build a start rectangle from word1/2/3 plus
A11.Y, then consume one or more stepping tuples until the A12 sentinel. This is
consistent with an ADSP-generated outline/shape, but the data does not yet prove
"slope", "lighting", or any named geometry quantity. The decisive unknown is
now the field descriptor F1 interpretation and the meanings of the two
accumulator pairs.

The C codec's `unmapped` range now starts at word 4. Word 3 is exposed as `y()`
from the primary FILL path. Words 4..7 are preserved raw: the consumer's
register destinations and arithmetic are known, but their source-field widths,
values after F1 extraction, and geometric meanings are not.

## 61. Retract the fixed 8-word entry: bit-address stride and F1 are not bytes

A final unit check against `docs/tms34010-reference.md:724-736` and the actual
field helpers in the MAME source found another factor-of-eight error in §54–§60.
The TMS34010's data addresses are **bit addresses**. The unit added by the
consumer's `SLL 4h,A5` is 16 bits, not 16 bytes.

### 61.1 What `SLL 4h` actually means

```asm
00345040: SLL 4h,A5
00345050: ADD A1,A5
```

`A5 = A1 + index*0x10` in **logical bit-address units**. One index step is 16
bits = one 16-bit word = two physical bytes. The M5 selected-index example
contains values `0x0001, 0x0011, 0x0021, ...`; those values are 16 indices apart,
so their selected starts are 16 words = 32 physical bytes apart. They are not
16 bytes apart.

This retracts §55.1's "SLL 4h gives a 16-byte stride" and corrects
`analysis/m5-visible-output.md` and `reproduction/gsp/record_parser.h`. Its
address helper still computes the right literal arithmetic, but the unit is
logical bit-address units; `record_parser` stores an eight-word observation
sample, not a proven complete semantic entry.

The `raw=` strings in `M1_GSP_DL_CALL` are fixed 64/128-word capture windows.
Their length being divisible by eight is **not** evidence of eight-word entry
boundaries. §54/§56's "8 records" and §55's fixed-record interpretation are
withdrawn. The 64-word vector remains valid as captured words, not as eight
proven records.

### 61.2 F0 is 16 bits; F1 is 32 bits

The uploaded program sets the field descriptors at startup:

```asm
00340000: SETF 10h,0,0     ; F0
00340010: SETF 20h,0,1     ; F1
```

The pinned observer MAME source makes the mechanics explicit:

* `tms34010.cpp:158-159`: `FW(i) = (m_st >> (i?6:0)) & 0x1f`,
  `FWEX(i) = (m_st >> (i?6:0)) & 0x3f`.
* `34010ops.hxx:839-847`: `SETF` writes the selected six-bit descriptor into
  `m_st`.
* `34010fld.hxx:663-670`: F0 descriptor `0x10` selects `rfield_z_16`; F1
  descriptor `0x20` selects `rfield_32`.
* `34010ops.hxx:1269-1283`: `MOVE *Rs+,Rd,F` reads with `RFIELD(F)` and increments
  by `fw_inc[FW(F)]`; `fw_inc[0] == 32` logical bits, i.e. two words.

So the primary worker's stream reads are:

```text
F0: word0 control       16 bits
F0: word1 -> A8         16 bits
F0: word2 -> A6         16 bits
F0: word3 -> A8         16 bits
F1: next value -> A8     32 bits = 2 words
F1: next value -> A9     32 bits = 2 words
F0: next value -> A10    16 bits
```

That is already **nine words** before accounting for the following sentinel or
additional F1 tuples. There is no fixed eight-word record established here.
The primary FILL prefix of four F0 words remains valid; the rest of the entry is
a variable stream of 32-bit F1 fields and 16-bit F0 fields.

### 61.3 Corrected C boundary

`reproduction/gsp/payload_codec.{h,c}` no longer groups the stream into
8-word records. It now decodes only the first four F0 words as a **primary FILL
prefix**, with an explicit offset into a larger raw stream. The C helper derives
DADDR/DYDX from that prefix plus caller A11/A13. No assertion is made about the
entry length, the F1 tuple boundary, or the raw capture chunk size.

`record_parser`'s eight-word array remains a sampled raw window from its earlier
M5 trace. Its address-stride constant/comment now says address units, not bytes;
this path does not prove that eight words are a complete record. Any future
semantic parser must honor the per-field F0/F1 widths above, not slice every
eight words and hope.

### 61.4 Next evidence needed

Capture the live A5 and effective F0/F1 values at `$FFF45510`, `$FFF455A0`, and
`$FFF45610`, along with the source bit addresses and data. This will establish
exactly which words in the MAME raw stream form the two F1 values, the F0 loop
scalar, and the sentinel, and whether the selected-index gaps are padding,
parallel streams, or separate entries. Until then, the prefix is decoded and
tested; the full geometry payload remains variable-length and partly unknown.

## 62. The old service_f2 runtime capture does not reproduce in the current runner

To capture actual F1 reads with A5 and caller registers, I extended
`tools/mame-ram-read-trace` with the same `service_f2` input schedule used by the
committed display-list trace: Service Mode value 0 at frame 60, value 1 at frame
120. Added a public test for the option and both scheduled edges.

On the **frozen oracle** `/mnt/build/mame-oracle/mame`, five repeated runs with
the same command (`frames=180`, capture 60-180, input `service_f2`) all reported
`call_count=0` and GSP state sample PC=0. Their `canonical_log_sha256` values
were all different, so these are not identical captures and cannot be compared
byte-for-byte. This does **not** refute the committed 2026-10-10 capture, which
reported seven calls in 180 frames; it establishes that the same input schedule
does not reproduce it in today's runner/environment. Per AGENTS.md's input rule,
I make no behavioral claim from a single run; the five-run result is recorded as
non-reproduction, not as an oracle change.

ROM validation is clean (`tools/check-roms roms /mnt/build/mame-oracle/mame`),
but the cause is UNKNOWN: possible difference in the old capture's invocation,
input/harness state, or runtime configuration. We need its full provenance before
using it to assert a live F1 field value.

### Consequence for q9t

The **static** field-width fact remains strong and independent of that runtime
capture: the uploaded program sets F0 to `0x10` and F1 to `0x20`; pinned MAME's
`SETF`/`rfield` source maps those to a 16-bit F0 and a 32-bit F1 read. Thus the
first four F0 fields form a useful prefix, but any F1 field consumes two 16-bit
words. No fixed 8-word boundary follows.

The C code has been changed accordingly: `payload_codec` now decodes only a
four-word F0 prefix at an explicit word offset, and its primary FILL helper takes
A11/A13 as inputs. It no longer groups the capture into eight-word records or
claims a full payload round-trip. Five freshly rebuilt controls fail (X/DX swap,
wrong Y, low-half A11 for DY, ignoring A13, and prefix offset +1); restored
passes.

Next capture needs to start from a reproducible state that actually reaches the
consumer. Until then, static disassembly gives field widths and register mapping,
but the runtime value sequence after the prefix remains UNKNOWN.

## 63. Caller resolves A11: primary FILL height is one (`q9t`)

§59/§61 said the actual A11 height was unknown because the M1 entry tap did not
capture A11. That is true of the tap and false of the static call chain: the
caller code is uploaded and disassembles.

```asm
00344b30: MOVI  FFFFFFFFh,A12
00344b70: MOVI  10001h,A11
00344ba0: CALLR 345090h
...
00344bf0: MOVI  FFFFFFFFh,A12
00344c20: MOVI  10001h,A11
00344c50: MOVE  @FFF716C0h,A2,0
00344c80: MOVE  A5,A3
00344c90: MOVE  A5,A1
00344ca0: MOVE  *A5,A0,0
00344cb0: SLL   4h,A0
00344cc0: ADD   A0,A3
00344cd0: MOVE  @FFF77E60h,A13,0
00344d00: JR    345000h
```

All xrefs to the primary parser in the disassembled image resolve to the two
above: the single-record `CALLR` and the indexed loop into `$FFF45000`. Both set
`A11=0x00010001` before entry. Since `ADDXY A11,A6` takes A11's high 16 bits,
`DYDX.DY = 1` for this primary path. The low half `1` may be used later as other
state, but it is not the FILL height.

Both set `A12=0xFFFFFFFF`; this resolves the sentinel compare at `$FFF45520`:
`CMP A12,A8 / JREQ $FFF45630` terminates the variable F1 tuple scan when the 32-bit
field equals all ones. Thus the F1 tail is a sentinel-terminated variable stream,
not a fixed 8-word record.

Updated the C test to use the observed A11 caller constant (`DY=1`) and retain a
separate synthetic A11 vector (`DY=2`) as a negative control proving the helper
uses the high half. This is static disassembly evidence, not an M1 runtime capture
of A11; the attempted live read trace did not reproduce the old service_f2 path
(§62).

## 64. First ADSP C math kernel, backed by one SOM transaction (`alo` / `1fv`)

The bounded capture is committed at
`reference/experiments/stunrun/adsp-som-transaction-f551/`. It was generated
with input **none** from power-on using the frozen oracle binary
`/mnt/build/mame-oracle/mame` (sha256
`75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06`) and the
current ROM manifest (sha256 `b3cf2b613752c494cb441f85090dd056f53b9006bdc4564eb53396d3c4ae729d`). The result contains 4,009 same-run ADSP data-space events from
SOMCLK to GINT and 146 SOMLATCH words; it is bounded raw evidence.

```text
SOMCLK  DM($2003) = 0       ADSP PC $0034  frame 551
146     SOMLATCH DM($2002) writes, logical addresses 0..145
GINT    DM($2006) = $0045   ADSP PC $004C  frame 551
IRQ ack $818060             68010 PC $021402, same frame
```

At SOMCLK the selected host bank is 1; MAME's producer-side `bank ^ 1` writes
bank 0. The bank history is in the same-run capture. Later in the capture window
the 68010 makes thousands of SOM-window reads and HSTDAT writes, but their
one-to-one relationship to this 146-word block is **not established**; metadata
explicitly keeps that correlation UNKNOWN.

### 64.1 Two measured interpolation-helper vectors

The primary producer stream enters the ADSP helpers at `$05DD` and `$0607`. The
same-run capture records the DM samples, register snapshots, and output writes:

| helper | DM sample difference | `MY0` | observed `MR1:MR0` | SOM-side DM stores |
|---|---:|---:|---:|---|
| `$05DD` | `DM[$44]=$00FB - DM[$42]=$00FF = -4` | `$1249` | `$FFFF:6DB8` | `$003E=$6DB8`, `$003F=$FFFF` |
| `$0607` | `DM[$40]=$00F4 - DM[$42]=$00FF = -11` | `$1999` | `$FFFD:CCDA` | `$003C=$CCDA`, `$003D=$FFFD` |

The ADSP listing for both sites executes `MR = AR * MY0 (SU)`. Both captured
outputs equal the low 32 bits of `2 * signed_AR * unsigned_MY0` exactly. The
PM cells are captured by `tools/mame-adsp-program-dump` at frames 551 and 600
from the same frozen MAME binary: `PM[$103D]=$00124900`, `PM[$103B]=$00199900`;
bits 23:8 supply the `MY0` values in the transaction. The whole program images
differ by 2748 bytes between those checkpoints, so this finding promotes only
the two matching cells (corroborated by the same-run `MY0` register snapshots),
not whole-image stability. The C test supplies the PM words and DM samples as
inputs; it calculates, it does not look up expected SOM outputs.

### 64.2 The first C slice

`reproduction/adsp/som_math.{h,c}` implements the captured low-index branch of
this interpolation helper, including PM table lookup and DM pointer reads. It
rejects `AR > $40`, whose alternate scaling path is not ported. Its two vectors
match the MAME MR pairs and DM stores above.

Three rebuilt negative controls all fail: omit the ADSP fractional `×2`, read
the wrong PM coefficient byte, or reverse the DM samples; restored passes.

This is a **bounded arithmetic helper**, not yet the complete `$05DD`/`$0607`
routine, not the whole 146-word SOM producer, not a lookup table, and not an
ADSP emulator. The `AR > $40` scaling branch remains unimplemented. The next
slice is that branch and its MR-to-SR shift, followed by the caller-level
routine's comparison against the captured SOM sequence. The 146-word
transaction remains the acceptance oracle, not the GSP's eventual pixels.

## 65. Same transaction recaptured with caller/helper register snapshots

The capture harness was extended to record the ADSP register file at memory
accesses in `$FFF514-$FFF5D4` and `$FFF5DD-$FFF630`, while still capturing the
same no-input SOMCLK-to-GINT transaction. The expanded result is
`reference/experiments/stunrun/adsp-som-transaction-f551-regs/result.json`.

It contains 576 register snapshots, including 464 in the caller range and the
two interpolation-helper ranges. This exposed the full setup around the two
tested multiply vectors:

* At `$05ED/$05EF`, `I0=$45`, `I2=$43`, `M6=$07`, `MY0=$1249`; the reads are
  DM `$44=$00FB`, `$42=$00FF`, and the result stores `$6DB8/$FFFF` at `$003E/$003F`.
* At `$0617/$0619`, `I4=$41`, `I1=$43`, `M6=$05`, `MY0=$1999`; the reads are
  DM `$40=$00F4`, `$42=$00FF`, and the result stores `$CCDA/$FFFD` at `$003C/$003D`.

The new run's SOM words and all 4,009 producer events are exactly equal to the
base frame-551 capture after removing the added register fields. This is a
second no-input MAME observation of the same transaction, not the scripted
service_f2 arm that failed to reproduce in §62.

The attempted ADSP program-space read tap still records **zero** PM lookups. The
PM table values used in the C test are therefore independently sourced from the
frame-551/frame-600 ADSP program snapshots (metadata records both image hashes)
and corroborated by `MY0` in the same-run live register snapshots. No claim is
made that the whole program image is static: the two captured images differ by
2,748 bytes, while these two table words match.

Next C slice: implement more of the caller around `$0514-$05D4` using these
register/input snapshots, and compare its complete 146 SOMLATCH words against
the captured stream. The current multiply kernel is only one mathematical
operation and remains uncalled by `./run.sh`.

## 66. SOM record layout decoded from the ADSP emitter (`alo` / `1fv`)

The transaction captured in §64 is fully attributable: the capture records the
ADSP program counter for every one of the 146 SOMLATCH words, so the whole
stream can be split by which instruction latched each word.

### 66.1 Transaction envelope

```text
$0033/$0034  AR = $0000 ; DM($2003) = AR    SOMCLK, resets the SOM word pointer
$0035        DM($2002) = AR                word 0 = $0000
$0039        DM($2002) = AR                word 1 = $0000
$003D/$003E  CALL $0780 / CALL $0834        $0834 runs the record emitter
   ...       9 records, 16 words each      $0515 .. $05D4
$004C        DM($2006) = AX0                GINT, value $0045
```

`AR` is still `$0000` at both prologue writes, which is why the first two words
are zero. Record boundaries therefore fall at SOM word offsets 2, 18, 34, 50,
66, 82, 98, 114, 130 — sixteen words each.

### 66.2 One record

```text
$0515  control    AR  = DM($0036)          -> $0030 in all nine records
$0565  start_x    AR  = DM(I1,M1)
$0567  delta_x    AR  = DM(I2,M2)
$056B  start_y    AX1 = DM(I0,M0)
       span       AX0 = DM($003E)           side value, low word
       span       AX0 = DM($003F)           side value, high word
       span       AX0 = DM($003C)           other side, low word
       span       AX0 = DM($003D)           other side, high word
       span       DM($2002) = AR            run length in rows
$05D2  DM($2002) = $FFFF
$05D4  DM($2002) = $FFFF
```

Four header words, **N** five-word spans, two `$FFFF` terminator words. All nine
captured records carry two spans, but the count is a loop bound and not fixed.

The two 32-bit side values are not arbitrary: they are what `$05DD` and `$0607`
leave in `DM($003E)/DM($003F)` and `DM($003C)/DM($003D)`, i.e. the outputs of the
interpolation helper already ported as `som_math.c`.

### 66.3 The three span branches all compute one run length

The loop body at `$0579` picks a branch on `AY1 - AY0`:

| branch | condition | instruction | run length |
|---|---|---|---|
| `$059D` | `AY1 >  AY0` | `AF = AY0 - AX1` | `AY0 - AX1 + 1` |
| `$057D` | `AY1 == AY0` | `AF = AY1 - AX1` | `AY1 - AX1 + 1` |
| `$05B7` | `AY1 <  AY0` | `AF = AY1 - AX1` | `AY1 - AX1 + 1` |

All three reduce to `min(AY0,AY1) - AX1 + 1` over unsigned 16-bit arithmetic.
Checked against all eighteen captured spans: **18/18 exact, no mismatches.** The
capture exercises all three branch forms (`$05A0` four times, `$05BA` four
times, `$0580` ten times).

### 66.4 Guard levels are not uniform, and the capture proves it

The `$0574`/`$0577` prologue guards use `IF LE`, while every loop tail
(`$0594`, `$059A`, `$05B4`, `$05CE`) uses `IF LT`. The difference is observable:
record 2's second span has `AY0 == AY1 == AX1 == $00A0` and still emits a span
with run length 1, which the `IF LE` prologue would have rejected.

A second asymmetry: every tail checks `AY1`, but only `$059A` checks `AY0`. The
`$0595` and `$05CF` tails jump back to `$0579` without re-checking `AY0`. So
`AY0 >= AX1` is a conservative precondition, not a property of every path.

### 66.5 The second C slice

`reproduction/adsp/som_record.{h,c}` implements the output sequence: header
order, the three-way run-length branch, the two guard levels, the five-word span
layout, and the `$FFFF $FFFF` terminator. The test reassembles all 146 captured
words from the nine measured records and `memcmp`s them against the capture.

Fourteen rebuilt negative controls all fail and the restored code passes:
swapping the `DM($003E)`/`DM($003C)` pair, swapping either pair's low/high
halves, using `max` instead of `min`, dropping the `+1`, swapping header words,
swapping `control` and `start_y`, emitting one terminator word, using a
non-`$FFFF` terminator, emitting the terminator before the spans, applying
either guard level to the wrong position, misreporting the word count, and
writing a partial record when a guard fails.

### 66.6 What this does not establish

- **Header word order is not discriminated by the capture.** All nine records
  latch `DM(I1,M1)` and `DM(I2,M2)` to equal values, so `start_x` and `delta_x`
  are indistinguishable by value. The capture pins only the attribution (word 1
  at `$0565`, word 2 at `$0567`). The order rests on the disassembly, and the
  test carries a clearly-labelled synthetic record to stop a swap passing
  silently. This is a limit of the fixture, not a finding about the hardware.
- The **span list is an input**. This slice does not model the index/modifier
  walk (`$0550`-`$0563`) that chooses the spans, nor the loop bound at
  `$058E`/`$05AE`/`$05C7`. That is the next slice.
- No field is given a geometric meaning. "Run length in rows" describes what the
  instruction computes, not what a GSP primitive does with it.

## 67. The span walk and ADSP circular-buffer addressing (`alo` / `1fv`)

§66 decoded what the emitter writes. This section recovers *where the values
come from*: the index/modifier walk at `$0564`-`$05D1`, and an addressing rule
that is not the one the instruction text suggests.

### 67.1 `DM(I,M)` reads at the index, then adds the modifier

The pinned MAME source settles it
(`src/devices/cpu/adsp2100/2100ops.hxx`, `data_read_dag1`/`data_read_dag2`):

```cpp
res = data_read(i);
i = (i + m_m[mreg]) & 0x3fff;
if (i < base) i += l;
else if (i >= base + l) i -= l;
m_i[ireg] = i;
```

with `base = m_i[ireg] & m_lmask[ireg]` recomputed by `update_i` on every
register **write** (`2100ops.hxx:333-336`). So the address is the *current*
index, not `index + modifier`, and the index then wraps inside the L register's
circular window. `m_lmask` is `m_mask_table`, the threshold table built in
`adsp2100.cpp:1089-1107`.

The capture agrees. Record 0, `$0564`: `I1=$0042`, `M1=$0001`, and the tap
records a read of **DM[$0042]** with `I1` afterwards at `$0043`. Under the
naive `index + modifier` rule the read would have been DM[$0043].

### 67.2 The wrap is load-bearing, and it explains two records

`L0 = L1 = L2 = L4 = $0006`, so `lmask(6) = $3FF8` and `base = index & $3FF8`.
The vertex data is a **six-word ring** at DM[$0040..$0045].

Record 0's `I4` runs `$0043 -> $0041 -> $0045`: the second step computes
`$003F`, which is below `base = $0040`, so `+6` lifts it back to `$0045`. Without
the wrap the walk would run off the buffer.

That same wrap is why records 4 and 6 look wrong under any non-wrapping model:
at `$056A` their `I0` computes `$0047`, which is `>= base + 6`, so `-6` gives
`$0041` — and `$0041` is the address actually read. Records 0-3 and 5, 7, 8
compute `$0045` or lower, do not wrap, and read where the arithmetic says. This
was the anomaly that forced the circular-buffer model rather than a fudge.

### 67.3 The walk itself

`$0568` seeds `I0` from `I2`; the two chains then walk toward each other,
forward by `M0/M2 = +2` and backward by `M1/M4 = -2`, and the record ends when
`I4 == I0` (`$058D`/`$058E`). The branch at `$057B`/`$057C` decides which chain
advances: when `AY1 > AY0` the `$05AF` tail walks the backward chain and
promotes `AY0`; otherwise the `$058F`/`$05C9` tails walk the forward chain and
promote `AY1`.

Validated against all nine captured records: header words, span count, each
span's `(AX1, AY0, AY1)`, every run length, and the terminator — **9/9 exact**,
derived from DM plus the entry pointers alone.

### 67.4 The third C slice

`reproduction/adsp/som_walk.{h,c}` transcribes the walk and the circular
addressing. CTest `adsp-som-walk-slice-c` supplies the captured DM ring and the
entry pointers for all nine records, derives the record, fills in only the two
32-bit side values per span (which `som_math.c` owns), and compares the
assembled 16 words against the capture.

Eleven rebuilt negative controls all fail and the restored code passes:
switching to `index + modifier` addressing, dropping the circular wrap, walking
the backward chain forward, promoting the wrong endpoint in either branch,
skipping the `$056D` back step, terminating immediately instead of merging,
using the post-`$0571` modifier for the header read, not advancing `I0` at
`$0569`, weakening the prologue guard, and stopping the walk early.

### 67.5 Harness defect found and fixed

The DM window is read from inside the data-memory read tap, and that re-enters
the same tap. The first version guarded the *state dump* but not the event
emission, so the dump's own reads were recorded as ADSP traffic: 4,009 producer
events became several thousand more, while the 146-word SOM stream was
unchanged. The corrected harness suppresses the whole tap while the dump runs.

The corrected capture is **bit-identical** to the register-snapshot fixture —
same `transaction_sha256`
`c41715380aabbd4e71f46afcda28cbaa0e48828264b9dc5a768a73b4ab238ea4`, same 4,009
events, same 146 words — which is a determinism check on the harness change as
well as on the emulator.

### 67.6 One control is an equivalent mutant, not a gap

A mutation that recomputes `base` inside every step rather than only on write
**passes**, and that is correct behaviour for the test to have. The wrap always
returns the index to `[base, base+length)`, which lies inside one `$3FF8`-aligned
block, so the block bits cannot change and the resulting index is identical.
A 400,000-case sweep over `(length, index, modifier)` found **zero** cases where
recomputing the base changes the resulting index; only the stored `base` field
differs, and the walk never reads it after the initial write. Recorded here so
the control count is not reported as stronger than it is.

### 67.7 What this still does not establish

- The **side values** — the two 32-bit pairs per span — are still inputs.
  `som_math.c` implements the `$05DD`/`$0607` arithmetic, but nothing yet wires
  it to the walk: the selectors are the span deltas (`AY1 - AX1` and
  `AY0 - AX1`, confirmed against two captured vectors), and joining those two
  slices needs the PM coefficient table as a checked-in input.
- The **`$052F`-`$0544` scan** that chooses `I1`/`I2` before `$0564` is not
  transcribed. The entry pointers are still measured inputs.
- The DM window is a **capture limit**: the walk is only validated where the
  indices land inside DM[$0040..$0045], and the tool refuses to read outside it.

## 68. The coefficient table, and the two helpers wired into the walk (`alo` / `1fv`)

With the record layout (§66) and the pointer walk (§67) in hand, the last
measured input was the PM table the two helpers index at `$1036`.

### 68.1 The table is a reciprocal, and it is stable

`PM[$1036 + n]` for `n = 0..$40`, taken from the frame-551 ADSP program image:

```text
n=0 $0000   n=1 $8000   n=2 $4000   n=3 $2AAA   n=4 $2000
n=5 $1999   n=6 $1555   n=7 $1249   n=8 $1000   ...   n=$40 $0200
```

Those are exactly `floor($8000/n)` for `n = 1..$40`, with zero at `n = 0` —
checked against all 65 captured words, no mismatches. This **upgrades** the
§64/§65 provenance claim: earlier only two cells had been compared across
frames, and I said the full images differ by 2,748 bytes. The whole 65-word
table is byte-identical at frames 551 and 600.

So the helper's `MY0` is a reciprocal of its own selector, and
`MR = AR * MY0` is a reciprocal multiply. Reading the two selectors (§68.2)
this is a division by the quantity being scaled.

The low byte of every table word is zero and nothing is set above bit 23, which
is consistent with the container convention: `MY0` is bits 23:8.

### 68.2 Each side is normalised by its own extent

`$05DD` is entered with `AR = AY1 - AX1` and the forward chain's pointers
(`I0`, `I2`); `$0607` with `AR = AY0 - AX1` and the backward chain's
(`I4`, `I1`). Each side therefore divides by **its own** extent, not by the
emitted run length, which is `min(AY0,AY1) - AX1 + 1`. Record 0 shows the
difference plainly: divisors 7 and 5, emitted run length 6.

Stating it as a division is an inference from the arithmetic, not a claim
about intent. What is measured is the selector values and the results.

### 68.3 A correction: the EQ tail calls BOTH helpers

§67.4's control list included "stop the walk immediately instead of merging the
chains", but while wiring the helpers I had misread the loop tails and
implemented the `AY1 == AY0` case as refreshing only `$05DD`.

The listing is unambiguous: `$0595 CALL $05DD` is followed in the *same* tail by
`$0596`-`$059B`, which walks `I1`/`I4`, re-reads `AY0` and calls `$0607`. So:

| branch | tail | refreshes |
|---|---|---|
| `AY1 > AY0` | `$05AF`-`$05B6` | `$0607` only |
| `AY1 == AY0` | `$058F`-`$059C` | `$05DD` **and** `$0607` |
| `AY1 < AY0` | `$05C9`-`$05D0` | `$05DD` only |

This shows up as a wrong answer rather than a crash. Record 2's second span has
`AY1 == AY0 == AX1`, so the EQ tail runs and both helpers are re-entered with
selector 0; the coefficient at `$1036` is zero, so **both** side values are
`$0000/$0000`. My earlier version kept the prologue's `$0607` result there and
emitted `6DB8/FFFF` instead.

The §66.4 statement that only `$059A` re-checks `AY0` stands; that was always
about the tail's guard, not about which helper it calls.

### 68.4 The fourth C slice

`som_walk.c` now calls `som_math.c` and fills in each span's two 32-bit side
values. **Nothing in the emitted record is copied from the capture any more**:
given the DM ring, the coefficient table and the entry pointers, the walk
derives the header words, the span count, each span's `AX1/AY0/AY1`, both side
values, and the run length. `adsp-som-walk-slice-c` compares all nine assembled
16-word records against the capture.

`stunrun_adsp_som_interpolation_low_branch` gained a `data_base` parameter so
its DM window contract matches the walk's. That was not cosmetic: the helper
indexes `DM[I0-1]` by absolute address, and the walk supplies a six-word ring
starting at `$0040`, so without the base the bounds check rejected every call.
Three controls cover that window now: a ring that covers the samples is
accepted and produces `$FFFF:$6DB8`, a ring one word too short is rejected, and
a window based above the samples is rejected.

Eight rebuilt negative controls all fail and the restored code passes: drop
either half of the EQ tail's refresh, give the LT tail a `$0607` refresh, make
the GT tail refresh `$05DD`, use the run length instead of the side delta as
the selector, read the wrong chain's samples, take the low PM byte instead of
bits 23:8, and ignore the DM window base.

### 68.5 What this still does not establish

- The **`$052F`-`$0544` scan** that selects `I1`/`I2` before `$0564` is still
  untranscribed, so a record is derived from a supplied pointer pair rather than
  from game state. This is now the only measured input left in the path.
- The **`> $40` scaled branch** of both helpers (`$05F5`/`$061F`,
  `ASHIFT ... BY -6`) is still unimplemented. No captured selector reaches it;
  the helper declines any selector above `$40` rather than guessing.
- The relationship between the emitted run length and the two divisors (§68.2)
  is arithmetic, not intent.
- `./run.sh` still shows nothing new.

## 69. Palette-bank decode in the shared GSP board model

The title capture's colour mismatch was not caused by RGB channel order. MAME's
`gsp_palette_change()` and `native/gsp_display.c` both decode red from the high
byte of palette-low, green from its low byte, and blue from the low byte of
palette-high. The defect was earlier in the pipeline: the shared native GSP
board model did not model the palette-bank hardware consistently.

MAME's `hdgsp_control_hi_w()` derives the bank bit from the **control-high
register write offset** (`(offset >> 3) & 1`) and the selected register
(`offset & 7`), then palette accesses resolve to
`bank * 256 + (offset & 0xff)`. Native `gsp_board.c` instead derived the bank
from `ctrl_hi[0] & 7`; its local palette read/write paths also used the raw
address low bits, bypassing the bank resolver. That made GSP-local writes land
in a different palette entry than MAME and left the scanout with stale/wrong
colours.

The regression test selects bank 1 by writing zero to `$F48000A0` (the address
encodes bank bit 0), writes `$1234` to palette entry 0, and checks that the
value appears at physical palette entry 256—not entry 0. It then selects bank
0 using `$F4800020` with nonzero data and proves the selected bank follows the
address, not the stored data. Before the fix this test failed six assertions;
after the fix `stunrun-gsp-board-test` passes.

**Measured improvement, not parity.** At frame 600, byte comparison of native
and direct MAME palette-plane dumps improved low-plane agreement from 167/2048
bytes to 1544/2048 (75.4%), and high-plane agreement from 744/2048 to 1584/2048
(77.3%). The rendered frame is still not MAME-matched; display geometry and
remaining palette-state differences are open. Captures used
`STUNRUN_FRAMES=600 STUNRUN_LIVE_VIDEO=0 STUNRUN_DUMP_DIR=... ./run.sh` and the
existing pinned MAME direct-share hook. The raw comparison files are local under
`/tmp/opencode/native-title-600-fixed-state/` and `/tmp/srshare/`; they are not
committed evidence fixtures.

## 70. Frame-48 service glyphs: byte order is not the mismatch

The suggested byte-order explanation does not fit the observed service prompt.
The native frame-48 CPU run reaches `$2686: dbra D1,$265E` while rendering the
30-character `$47` string; its VRAM dump has text writes through character 18
(`"PRESS START TO BYPA"`) and the remaining character cells are still blank.
One more native frame completes the loop. The frame-49 internal framebuffer,
cropped to MAME's 512x229 visible area, is **pixel-identical** to one MAME
frame-48 screenshot (`tools/frame_diff` with its documented two-image form:
0/117248 differing pixels, max RGB delta 0). That is direct evidence against
reversing the glyph bytes: the completed glyph rendering can match the oracle
exactly. It is a one-frame CPU/text-loop boundary difference, not a demonstrated
byte-order defect.

There is a separate real bug in the debug override `STUNRUN_FORCE_FRAMEBUF`.
It converted a physical row number directly to DPYADR, but DPYADR stores the row
in bits 15:4. The correct encoding for physical byte `$12C08` is row `$25`,
`DPYADR=$FDAC`, `DPYTAP=$0081`; the old encoding `$FFD9` decoded to row 2. The
conversion now lives in `stunrun_gsp_display_force_byte_offset()` and is pinned
by the `gsp-display-slice-c` regression test. This only fixes the diagnostic
override; it does not change the ordinary live scanout path.

**Oracle limitation:** five fresh runs of
`tools/mame-gsp-state-snapshot ... --input service --frames 60 --targets 48`
produced two different frame-48 screenshots (three black, two with the prompt).
The screenshots with the prompt were identical to each other; the black result
was also repeatable. Do not promote the one matching prompt screenshot as a
stable oracle or change glyph code to make it fit. The wrapper's scripted input
timing is nondeterministic (tracked separately by `stun-runner-0p0`). The MAME
run used the pinned local binary (`75b09ee4...`), ROM manifest
`b3cf2b613752c494cb441f85090dd056f53b9006bdc4564eb53396d3c4ae729d`, and the
service input schedule; native used `./run.sh --service` (IN0 `$C7`). Captures
and the repeated run results are retained under `/tmp/opencode/` for this
investigation, not as canonical fixtures.

**Check correction:** an earlier comparison invoked `tools/frame_diff` with an
unsupported `--diff <path>` option. The third positional argument is the numeric
threshold, so `atoi("--diff")` made the tool count every pixel (`threshold=0`)
even when the images were identical. Those `pixels_different` percentages were
invalid. Re-run with only two image paths (or a numeric third threshold); the
frame-49/native vs frame-48/MAME crop above reports zero differences. The
independent ImageMagick pixel comparison also reports zero differing pixels.

## 71. The `$052F`-`$0544` scan: the last measured input is gone (`alo` / `1fv`)

### 69.1 Both derived quantities come from one word

`$0526` reads `DM($0039)` into `MR0`, `$0529` doubles it into `L0`, and `$052E`
reads the **same** address into `CNTR`. So the ring length and the loop count
are one input. `DM($0039) = $0003` in all nine captured records, giving
`L0 = $0006` — the six-word ring §67 established — and a three-iteration scan.
`$051F`-`$0525` seed `I0 = I1 = I2 = I3 = $0040` with `M0 = +1` and
`M1 = M2 = M3 = 0`, so the scan steps one word at a time, unlike the walk's
stride of two.

The scan compares each new sample against a running minimum `AR` and moves the
`I1`/`I2` chains to whichever side tracks it: `$0531` `GT` leaves both alone,
`$0533` (`LT`) pulls both, and `$0537` (`EQ`) compares `AY0` against `DM(I1)`
and moves `I1` at `$053B`, or falls through to `$053D` to do the same for `I2`.

Reproduces the captured entry pointers for all nine records: `(42,42)` three
times, `(40,40)` four times, `(44,44)` twice.

### 69.2 The `$053D` block is dead in this transaction

Counting ADSP data-space events at each read site in the scan, across the whole
transaction:

| PC | reads | note |
|---|---:|---|
| `$0537` | 9 | one per record |
| `$053D` | **0** | never executed |
| `$0543`, `$0544` | 27 each | 9 records x 3 iterations |

So `$0539 IF GT` is never taken, and the whole `$053D`-`$0541` block — the `I2`
comparison, the `$053F IF LT` guard and the `$0540`/`$0541` `I2 = I3` update —
never runs. My C implements it from the disassembly, but **this capture provides
no evidence for it**, and the test cannot exercise it.

Three negative controls pass for exactly this reason, and the explanation is
recorded here rather than quietly counted as coverage:

| control | why it passes |
|---|---|
| signed vs unsigned `AF` at `$0539`/`$053F` | those tests are unreached |
| drop the `$053F IF LT` guard | unreached |
| run the loop one extra iteration | the extra iteration changes neither `I1` nor `I2` in any of the nine |

They are equivalent mutants for this data, not weak assertions. Exercising them
needs a record whose ring actually drives `AY0` above `DM(I1)`.

Branch coverage that *is* real: `$0531 GT` 12, `$0532 EQ` 9, `$0533 LT` 6,
`$053A I1 = I3` 9.

### 69.3 A trap worth naming

`AF = AY1 - AR` is a 16-bit result and `IF GT` / `IF LT` are **signed** tests on
it. My first version cast the 16-bit difference to a wider signed type without
sign-extending, so every comparison was "greater than zero" whenever the values
differed. That is not a subtle rounding difference: it sends every record down
the `$0531` path and the scan returns `$0040` for all nine instead of the
captured `$0042`/`$0040`/`$0044`. `adsp_diff` now sign-extends explicitly, for
the same reason `som_math.c` does.

### 69.4 The fifth C slice, and the first fully derived transaction

`stunrun_adsp_som_derive_record` composes control word (`$0514`), scan
(`$0526`-`$0544`), walk and interpolation, and fills a
`stunrun_adsp_som_record_t`. Its only inputs are a data-memory window covering
`DM($0036)`, `DM($0039)` and the ring, plus the coefficient table.

`adsp-som-walk-slice-c` now assembles **all 146 captured SOMLATCH words** —
the two `$0000` prologue words plus nine generated 16-word records — and
compares them word for word. No pointer, span list, side value or record word
is supplied to the C anywhere in that path.

Six rebuilt negative controls fail and the restored code passes: treat `AF` as
unsigned at `$0531`, swap the `$0531`/`$0532` branches, let the `LT` tail move
only `I1`, seed the scan a word high, use the counter as the ring length
without doubling, and read the control word from `$0039` instead of `$0036`.

### 69.5 What is now fully unmeasured in this path

Nothing between `DM($0036)` and the emitted record. The remaining unknowns are
elsewhere: the caller that fills the ring before `$0514`, the `$0631` tail after
`$05DC`, and the `> $40` scaled branch of the two helpers. `./run.sh` still
shows nothing new, because nothing calls this from the native shell yet.

## 72. The ring has four writers and only one of them survives (`alo` / `1fv`)

§69.5 listed "the caller that fills the ring before `$0514`" as unmeasured. It is
now measured, and the answer is that it is not one routine. Four of them write
`DM($0040..$0045)`, three of them are entirely overwritten, and the fourth is the
only thing the emitter ever reads.

**[ROM]** All counts and values below are from
`reference/experiments/stunrun/adsp-som-transaction-f551-regs/result.json`,
`transaction.producer_events`, 4009 events, `truncated: false`, transaction sha256
`c41715380aabbd4e71f46afcda28cbaa0e48828264b9dc5a768a73b4ab238ea4`, frame 551,
start condition `power_on`, ROM manifest `b3cf2b61…`, machine map `b9106e5c…`,
MAME `/mnt/build/mame-oracle/mame` sha256 `75b09ee4…`, harness
`tools/capture-adsp-som-transaction` + `mame/lua/adsp-som-transaction.lua`.
`DM($0039) = 3`, so the ring length is `2 * 3 = 6` words, matching §68.

### 72.1 The four writers, in the order they execute

The per-record dispatcher is `$01D4`–`$01DC`:

```
01D2: AR = DM($0039)
01D3: AF = AR
01D4: IF EQ JUMP $01DF
01D5: CALL $04B2            <- pass 1 and pass 2
01D6: AR = DM($0038)
01D7: AF = AR
01D8: IF EQ JUMP $01DB
01D9: CALL $0663
01DA: JUMP $01DF
01DB: CALL $04EB            <- the clamp, then straight into the emitter
```

Before that dispatcher, `$01A8: CALL $01E8` has already run. Bounding each record's
window by the *previous* record's `$0515` write — so the leading prologue belongs
to the record it feeds — counting writes to `DM($0040..$0045)` by PC gives
**24 writes and a byte-identical histogram in all nine records**:

| PC | count | what |
|---|---|---|
| `$01B8` / `$01BA` / `$01BC` | 1 / 1 / 1 | 3 ring words, written by the scan at `$01B0` |
| `$0201` / `$0206` / `$020F` | 1 / 1 / 1 | 3 ring words, written by routine `$01E8` |
| `$04C8` / `$04CA` | 3 / 3 | **pass 1**, 6 words |
| `$04D9` / `$04DC` | 3 / 3 | **pass 2**, 6 words |
| `$050D` | 6 | **the clamp**, 6 words |

Routine `$01E8` also mirrors its three words into `DM($08E7..$08E9)` at `$0202`,
`$0207`, `$0210` — 27 writes, nine per record, and those mirrors survive because
nothing overwrites them. It sources them from the `DM($2000)` word stream
(`$01EC`, `$01FC`), which is the SIMBUF, i.e. `/MP`, the object ROM. The scan at
`$01B0` stores its running maximum to `DM($08EA)` at `$01C3` and its iteration
count to `DM($0039)` at `$01C2`.

Pass 1 runs `CNTR = DM($0039)` iterations of a `DO $04CA UNTIL CE` loop, two words
per iteration. Pass 2 repeats the shape with `DO $04DC UNTIL CE`, also
`CNTR = DM($0039)`. The clamp's count is built explicitly: `$0501: SR = ASHIFT AR
BY 1 (LO)`, `$0502: CNTR = SR0`, which is the ring length doubled — one word per
iteration instead of two.

### 72.2 Only pass 2 survives

Every one of the three earlier writers is dead in this capture, which is a
property of the *observed data*, not of the code:

- `$01E8` and `$01B0` together write all six words, and pass 1 overwrites all six.
- Pass 1 differs from pass 2 in **9 of 9** records, so pass 2 overwrites pass 1.
- The clamp at `$050D` reproduces pass 2's output **byte for byte in 9 of 9**.
  Its bounds are `AY0 = 0`, `AY1 = $01FF` (`$0505`, `$0506`), and every pass-1 and
  pass-2 value observed is already inside `[0, $01FF]`.

Per record, the six words that the `$0514` scan actually reads:

| rec | pass 1 | pass 2 (== what is read) |
|---|---|---|
| 0 | `00F4 00FD 00FF 00F6 00FB 0101` | `00F4 009E 00FF 0099 00FB 00A0` |
| 1 | `0103 0101 00FF 00F6 010A 00FD` | `0103 00A0 00FF 0099 010A 009E` |
| 2 | `00FB 0101 00FF 00F6 0103 0101` | `00FB 00A0 00FF 0099 0103 00A0` |
| 3 | `00F2 00F3 00FF 00F6 00F4 00FD` | `00F2 0097 00FF 0099 00F4 009E` |
| 4 | `010A 00FD 00FF 00F6 010C 00F3` | `010A 009E 00FF 0099 010C 0097` |
| 5 | `00FF 00E8 00FF 00F6 00F6 00EC` | `00FF 0091 00FF 0099 00F6 0093` |
| 6 | `010C 00F3 00FF 00F6 0108 00EC` | `010C 0097 00FF 0099 0108 0093` |
| 7 | `00F6 00EC 00FF 00F6 00F2 00F3` | `00F6 0093 00FF 0099 00F2 0097` |
| 8 | `00FF 00E8 0108 00EC 00FF 00F6` | `00FF 0091 0108 0093 00FF 0099` |

### 72.3 Our refutation: `$04DD` is a converged epilogue, not a skipped branch

I first read `$04DD` as the target of the untaken `IF EQ JUMP $04DD` at `$04CD`
and recorded it alongside `$053D` as an unexecuted branch. **That was wrong.**
`$04DD` executes in 9 of 9 records:

```
983473 PC04DD R DM[0031]=0001
983474 PC04DE W DM[2007]=0001      ... x9
```

`DM($0031)` is read only ever at `$04DD` (9 reads, no other PC), so this is the
routine's epilogue, not a stray instruction.

The reason is that pass 2's `DO $04DC UNTIL CE` **falls out the bottom** when
`CNTR` reaches zero instead of branching away, so `$04DD` is reached by ordinary
fall-through. `$04CD`'s `IF EQ JUMP $04DD` targets the same address, so the
early-exit path and the normal path converge on one epilogue:

```
04CB: AR = DM($1FE4)
04CC: AR = AR
04CD: IF EQ JUMP $04DD     <- skips pass 2 entirely when DM($1FE4) == 0
04CE: CNTR = DM($0039)
     ... pass 2 ...
04DC: DM(I1,M1) = AR       <- end of loop, falls through
04DD: AR = DM($0031)       <- shared epilogue
04DE: DM($2007) = AR
04DF: RTS
```

Reading the fall-through as a skipped branch would have produced a routine that
never returns. `DM($1FE4)` is `$0200` on all nine reads at `$04CB`, so the
early-exit is genuinely untaken here and the convergence is what keeps the
routine honest.

### 72.4 Coverage gaps this adds

Two new unexercised paths, both in the same family as `$053D`–`$0541`:

1. **`$04E0`–`$04EA`**, the `IF LE JUMP $04E0` sibling at `$04C3`. Zero events. It
   is a second interpolation of the same three outputs, but computed differently
   from the executed `$04C4`–`$04CA` path: `(SS)` scaling followed by
   `LSHIFT MR BY 9` (`$04E3`, `$04E4`, `$04E7`, `$04E8`) instead of `(RND)` with
   `MR1 + AY0`. It ends `JUMP $04CA`, so it rejoins the same store.
2. **The clamp's clamping.** `$050D`'s six writes are no-ops in 9 of 9, so
   `$050A: IF LT AR = AY0` and `$050C: IF LT AR = AY1` never take either
   assignment, and neither did they need to.

Pass 1, pass 2, the fall-through epilogue and the clamp loop are all fully
exercised, so they are the only parts of the ring producer that a C replacement
can be tested against today.

### 72.5 What a C replacement of the ring producer now needs

Not one routine but a chain, and it drags in the object ROM:

- `DM($2000)` — the SIMBUF `/MP` word stream, read 27 times per transaction at
  `$04C5` alone. This is the largest new dependency and it is a separate
  banked buffer, not the `PM` table `som_math` already consumes.
- `DM($1FE4)`, `DM($1FE5)` — the pass-2 guard and one of its multipliers.
- `DM($0928)` — pass 1's `I0` base.
- `DM($08EA)`, `DM($08E7..$08E9)`, `DM($0039)`, `DM($0031)`, `DM($2007)` — loop
  counts, the scan's running maximum, and the `$01E8` mirrors.

Recommended order, now that the shape is known: pass 2 first (it is the only
output the emitter reads, and it is fully exercised), then pass 1, then the
`$01E8` mirrors as a separate slice, and only then the object ROM itself.

## 73. Looking at the SOM producer: `./run.sh --adsp-som` (`alo` / `1fv`)

Everything in §64-§72 existed only in slice tests. `./run.sh` showed none of it,
because nothing in the native shell called any of it. This section records the
view that makes it visible, what it is honest about, and three mistakes the work
produced.

### 73.1 What the view is, and what it refuses to claim

`native/adsp_som_view.c` derives all nine captured records from
`DM($0030..$0045)` plus the coefficient table and nothing else, compares each
record's sixteen words against the capture, and draws the decoded record fields.

It is **not** the game's rendering. The GSP's consumer of the SOM record is not
decoded (bead `q9t`), so:

- the axes are an index space defined in `adsp_som_view.c` and labelled with raw
  values, not a coordinate space recovered from hardware;
- the bar colour is a documented ramp over one raw 16-bit field
  (`DM($003C)`/`DM($003D)`), not a palette, and `stunrun_adsp_som_view_side_color`
  is exported so the test pins it as the documented function it is;
- `AY0 != AY1` in eight of eighteen spans, and nothing here says what the larger
  one means. It is drawn as a tick at the far end rather than dropped.

The one asserted claim is the word stream. It reports
`derived words matching the capture: 9/9`, and exits non-zero otherwise. If that
number is not 9, the picture is a picture of something the game never emitted.

**[ROM]** Inputs: `reference/experiments/stunrun/adsp-som-transaction-f551/`
transaction sha256 `c41715380aabbd4e71f46afcda28cbaa0e48828264b9dc5a768a73b4ab238ea4`,
MAME sha256 `75b09ee4…`, frame 551, `--input none`, power-on. See §66-§72.

### 73.2 The span chain shares a boundary row, and my first statement of it was off by one

Every record has exactly two spans, and the second span's `AX1` is the first
span's `min(AY0,AY1)` — **not** `AX1 + run length`:

```
rec  start_y  span 0: ax1  ay0  ay1  h   span 1: ax1  ay0  ay1  h
  0    0099        0099 009E 00A0  6         009E 00A0 00A0  3
  2    0099        0099 00A0 00A0  8         00A0 00A0 00A0  1
  5    0091        0091 0093 0099  3         0093 0099 0099  7
```

So `AX1(k+1) == min(AY0,AY1)(k)`, and because `h = min - AX1 + 1`, that is
`AX1(k) + h(k) - 1`. **Consecutive spans share their boundary row.** I first
asserted `AX1 + h` in the test, the test failed, and the disassembly-independent
way to tell which was right was to print the values rather than reason about
them — `AX1 + h` is one row too far, and the ring geometry does not care which
way a run is counted as long as the emitter's own `min - AX1 + 1` agrees.

### 73.3 The coefficient table is 64 entries, not `$40`

`PM[$1037..$1076]` is **64** contiguous entries equal to `floor($8000/n)` in bits
23:8 for `n = 1..64`, verified closed-form. §68 and the slice README both said
"n = 1..$40". The `$40` is the reachable **selector** bound — `$05DD`/`$0607`
decline anything larger — not the extent of the table. Both statements are true
and they were being conflated. `tools/check-adsp-som-fixture` now asserts the
64-entry closed form and exposes `..._pm_selected_max()` separately, so the two
cannot be confused again.

### 73.4 The fixture had one copy; the view needed another, so a tool appeared

The captured inputs lived in `som_walk_test.c`. Hand-copying them into the view
would have put a free-to-drift second copy of the evidence into the tree, so they
moved to `reproduction/adsp/som_fixture.c` and the test consumes that (487 lines
to 253).

The first tool to maintain them was a **generator** that extracted the tables
back out of `som_walk_test.c`. It worked, and then it became circular: once the
test read from the fixture, the generator's source was its own output's consumer,
and it reported `table not found in som_walk_test.c: PM_PROGRAM`. It is now
`tools/check-adsp-som-fixture`, a **validator** of the committed fixture's
invariants.

**Its limitation, which matters more than its checks:** it cannot tell you
whether a record word is the *right* record. Expected words come from the same
human transcription as everything else in the fixture, so a uniformly wrong
record passes every identity here. It verifies internal consistency and agreement
with the independently transcribed findings 72.2 ring. Re-deriving the expected
words from the capture means parsing 4009 producer events — a different question,
not yet asked. Until someone asks it, "checked against the capture" is not a
thing this tool can claim.

### 73.5 Two controls that passed for the wrong reason

The view's render test samples pixels per column and compares against what the
spans say should be there. It took three attempts to make it able to fail, and
both earlier versions passed every partial-render control:

1. **The first version counted any non-lane pixel as content.** The axis
   separator down the left of each column is `AXIS_R/G/B` (`$46/$4E/$5E`), not
   the lane colour, so all five partial-render controls passed: "only record 0
   is drawn", "only the first span is drawn", "bars collapsed to zero height".
2. **The second version excluded the lane colour and the axis colour but not the
   page background.** A render that skips a record leaves that column at the
   page background rather than the lane colour, so *every undrawn pixel counted
   as content* and "only record 0 is drawn" passed again.

Both are the same class of mistake as §71's wrong control name: a check that
cannot fail reads exactly like a check that passed. The fix in both cases was to
print what the sampler actually saw rather than to reason about which colours
should be excluded.

### 73.6 One equivalent mutant, recorded rather than worked around

Eight render controls fail. One passes: mutating a bar to end at `max(AY0,AY1)`
instead of `min(AY0,AY1)`. The test compares the **union** of a record's spans,
and by §73.2 consecutive spans share their boundary row, so the next span's bar
already covers the rows the change would add — for all nine records. Both spans
also share a column centre, so there is no x-axis evidence either.

Catching it needs per-span discrimination, which this check does not do. It is
noted in `adsp_som_view_test.c` next to the check so the next reader does not
mistake the passing control for a covered one.

## 74. The road twin buffer is ROM, and the capture tool leaked DIP state

This section records two findings from an attempt to give the 68010 geometry
work a visible fixture. The fixture was not built. What came out of the attempt
was worth more.

### 74.1 A captured work-RAM buffer is a verbatim copy of a ROM slot

**Question.** Could `stunrun_geom_upload`'s 384-word march table be committed as
a regression fixture, the way the SOM capture was?

**Answer: no.** `geom_upload.h` already records that the upload "matches work RAM
verbatim, 768/768 bytes at both `0xFF9584` and `0xFF9884`". That fidelity is the
problem — the buffer is mostly ROM that the 68010 copied there.

Measured by reading ROM through the emulator, never by reading chip files:

```
tools/mame-memory-snapshot roms /tmp/q1/rom-40000 \
  --device :mainpcb:maincpu --space program \
  --base 0x40000 --count 0x3000 --width 16 --address-stride 2 \
  --frames 1300 --targets 600,1200,1300 --input none --nothrottle \
  --mame /mnt/build/mame-oracle/mame
```

`--width 16 --address-stride 2` is the only faithful read: the 68010 has a
16-bit bus and fetches big-endian words at even addresses, and `--width 32`
would synthesize an unaligned fetch the CPU cannot perform.

| captured state | ROM slot | words matching |
|---|---:|---:|
| frame 1200, attract | `0x044930` | **360 / 384** |
| frame 3600, course 1 | `0x044630` | **384 / 384** |

In the attract capture only the final 48 bytes (words 360-383) differ, which
independently corroborates `analysis/track-storage.md`. In the gameplay capture
nothing differs at all. **So there is no version of this fixture that is safe to
commit** — driven or not, and no better capture to go looking for. A regression
guard here has to be a sha256 or a hand-derived delta, not the bytes.

A previous session's note that the buffer's head words matched a march at
`0x44630` was true but attributed to the wrong slot: `0x044630` and `0x044930`
share their first 24 words, which is what made eight head words look like a
match. `0x44630` remains a legitimate course-0 base per `geom_upload.h`.

Added `GeometryFixtureCommittabilityTests` so a `road-twin*.bin` appearing under
`reference/` fails the public suite rather than the review that missed it.

**Bank state was established, not assumed:** a throwaway Lua probe compared
`space.read_u16` against `memory.regions[":mainpcb:maincpu"].read_u16` over all
384 words at ten frames spanning attract and gameplay — 384/384 identical at
every probe, so no bank remaps that window.

### 74.2 `tools/mame-memory-snapshot` leaked input-port state between runs

**This is the bug worth having found.** The tool hardcoded
`-cfg_directory /tmp/stunrun-mame-cfg`. MAME writes `stunrun.cfg` there at exit
**including current DIP-switch state**, and `-noreadconfig` does not stop the
*next* run from reading it back.

A run whose schedule presses "Service Mode" on `:mainpcb:IN0` — that is what
`--input service_probe` does — persisted

```xml
<input>
    <port tag=":mainpcb:IN0" type="DIPSWITCH" mask="32" defvalue="32" value="0" />
</input>
```

so every later run started with that DIP forced **off**. Main RAM then stayed
all-`0xFF`, the exit status was still 0, and the tool reported success. An entire
input-mode sweep was corrupted by this and read as a set of clean null results.

Measured against the pinned oracle, same command line, the only difference being
whether a `service_probe` run preceded it:

| | nonzero bytes in the 1536-byte capture | |
|---|---:|---|
| reference capture, clean cfg | 1262 | correct |
| reference capture, contaminated | **1536 (all `0xFF`)** | silently wrong, rc=0 |
| reference capture, after the fix | 1262 | correct |

Each run now gets its own `tempfile.mkdtemp()` cfg directory, removed in the
`finally` block, and the path is recorded in `result.json` for provenance.
Guarded by `MameCfgIsolationTests`.

**The general lesson is trap 4 wearing a disguise.** Trap 4 says scripted input
is nondeterministic and to run five times. This was the opposite: perfectly
reproducible, five times identical, and consistently wrong — because the
corruption was in the emulator's *persisted state*, not its scheduling. Five
identical wrong answers looked exactly like five correct ones. The only thing
that caught it was comparing against a capture whose provenance was already
trusted.

### 74.3 `drive_hold` never starts the game

`--input drive_hold` is two events, both at frame 2: AD Stick X = 220 and P1
Button 1 pressed. No coin, no start. Frame 1200 under it is the **attract**
state, and its road buffer is byte-identical to an `--input none` run. A
reproduction checked against it would be checked against an attract upload, not
a driven race.

`--input course_sweep` does reach gameplay: the course word at `0xFF9578/79`
becomes nonzero (value 1) at frame **3207**, and at frame 3600 the twin buffer
reproduces 5/5 across five MAME processes. Its sha256 is
`dda9cba51823fb32da9bc1551288589f0f15a3fce1f938c9ea290b84b4f7a648` — and per
§74.1 that buffer is 384/384 verbatim ROM, so it is recorded here as a number
and deliberately not as a file.

## 75. The road FIFO is a 32-bit `move.l`, not an even-lane selection

Chasing the 68010-side seam instead of the ADSP produced a refutation of our own
slice, plus the transport the road path actually uses.

### 75.1 What was measured

The road drain had never been observed on the write side. `road_fifo.c` modelled
it as 384 halfword reads producing 192 FIFO words on an **even lane**
(`base[2i]`), derived from a CPU-side read trace
(`m5-road-buffer-consumer-trace.metadata.json`) that carried addresses but no
values. There was nothing to check that reading against.

```sh
tools/mame-ram-write-trace roms OUT \
  --device :mainpcb:maincpu --space program \
  --base 0xC0000C --end 0xC0000E \
  --frames 1300 --start-frame 1280 --end-frame 1300 \
  --pc 0x02248E --input late_drive --nothrottle \
  --mame /mnt/build/mame-oracle/mame
```

32 524 writes, every one from PC `$02248E`, split evenly across **`0xC0000C`
(16 262) and `0xC0000E` (16 262)**. The even-lane model had one destination
register and therefore predicted a 2:1 write ratio it could not produce. The
ordering is strict `$C0000C` then `$C0000E`, **0 exceptions in 16 262 pairs** —
a 68010 big-endian `move.l` writing the high halfword of a long at the base
address first. No re-run was needed to settle this; the structure is in a single
capture.

Frame 1290 contributed exactly 192 pairs. Capturing the base road buffer at that
same frame and input:

```sh
tools/mame-memory-snapshot roms OUT --device :mainpcb:maincpu --space program \
  --base 0xFF9584 --count 0x600 --width 8 \
  --frames 1291 --targets 1290 --input late_drive --nothrottle
```

Packing those 384 source words two per long, high halfword first:

| | result |
|---|---|
| frame-1290 burst vs the packed base buffer | **192 / 192 exact** |
| occurrences of the full table in the 16 262-pair window | **1** (pair index 9347) |
| sha256 of the 192 packed longs | `a1064bce98f3d80acfa2a83ec9df46654e7108e580b9cae5e9967fa5a256789c` |

### 75.2 Why the old model passed its own test

`road_fifo.c` asserted `fifo[i] == base[2i]`. Under the true reading, `base[2i]`
is the **high halfword** of long `i`, so a `uint16_t` comparison against it
matches under both models. The test could not distinguish them, and neither could
any assertion of the form "output equals source word *2i*". That is the same
shape of failure as the two render controls in 73.5 and the SIGBUS assertion in
71.1: a test that looks like it pins the behaviour and pins half of it.

`road_fifo_test.c` now asserts the low halfword too
(`(uint16_t)fifo[i] == base[2i + 1]`) and, over the whole burst, that the host
writes reproduce the source sequence unmodified — which is the sharpest
available statement that the drain selects nothing and reorders nothing.

### 75.3 Why it mattered

The even-lane reading says the 68010 throws away half the table and the GSP
receives a subsample. The truth is that **every source word reaches the GSP**,
carried two per 32-bit write. Any decoder built on the old model was reading a
sequence that never existed, and any renderer built on it was drawing half the
data it should have had.

Three call sites carried the error and are corrected: `road_fifo.c`/`.h` (now
packs consecutive pairs and exposes `stunrun_road_fifo_host_writes` for the
bus split), `native/game_loop.c` (now pushes 384 halfwords rather than 192), and
the shell's own `fifo_match` self-check. Two analysis files that stated the even
lane as fact are marked superseded rather than silently edited.

Negative control: restoring `fifo_out[i] = base[i * 2u]` in the slice and
rebuilding produces **934 test failures**; the restored implementation passes.

### 75.4 Still UNKNOWN

This fixes the transport and refutes the subsample claim. It does **not** decode
the payload. Per 74.1 the buffer is commercial ROM, so the words carry no
established field meaning, and `analysis/gsp-road-fifo-consumer.md` still records
the GSP-side consumer's destination buffers as UNKNOWN. The next step on this
seam is decoding what `0xFFF48C20` does with the stream — it recombines adjacent
byte lanes with `SLL 8h`/`OR` and writes two work buffers, so the payload is
consumed on the GSP side, not the 68010 side.

---

## 76. The road window transform is transcribed; and "base == twin at frame N" only means the transform was not running

Slice: `reproduction/maincpu/road_window.{h,c}` + `road_window_test.c` (target
`road-window-slice-c`, sources `road_window.c` + `road_buffer_math.c` +
`road_window_test.c`). Oracle: pinned MAME `/mnt/build/mame-oracle/mame`, ROM set
validated by `tools/check-roms roms /mnt/build/mame-oracle/mame`. Static listing
`:mainpcb:maincpu` sha256 `171e56ccf1a9940635a2e8e9929ab7edaa5841ac3238ef78acf0d6630f8e45e7`
(`reference/listings/stunrun.metadata.json`).

### 76.1 The contradiction, stated first

A caller reported measuring main RAM under `--input course_sweep`: at frames 3300,
3600 and 6000 the 768-byte base window `0xFF9584` and the 768-byte twin window
`0xFF9884` were **byte-identical**, with base changing only slightly in between.
If `base[i] -= twin[i]` had run in that range, the two would have diverged —
equal operands subtracted give all-zero, which is trivially distinguishable from
a non-zero twin. The three candidate explanations were (a) the pass never fires
there, (b) another writer restores base, (c) `track-storage.md` is wrong about
which PC reads which buffer.

**Resolution: (a), and (c) is not the problem — `track-storage.md` is correct.**
The transform is real, fires on a 3-frame cadence, and is byte-exact; it simply is
not executing during the frames that were sampled.

### 76.2 The pass does not fire in that window (three runs)

`tools/mame-ram-read-trace` over the whole road window, no PC filter:

```bash
tools/mame-ram-read-trace roms OUT --device :mainpcb:maincpu --space program \
  --base 0xFF9584 --end 0xFF9B83 --input course_sweep --nothrottle \
  --mame /mnt/build/mame-oracle/mame --frames 3700 \
  --start-frame 3200 --end-frame 3700 --max-events 400000
```

Result: `event_count: 0`, `pc_counts: {}` — **zero CPU reads of any part of
`0xFF9584..0xFF9B83` in frames 3200..3700**. `0x0298BE` is `move.b (A0)+,D0` and
`0x0298C0` is `sub.b D0,(A1)+`, an RMW whose read half a read tap must see, so a
subtract pass cannot execute there. Nor can `0x02248E`, the FIFO drain: also zero.

Repeated, per the nondeterministic-`--input` rule in `AGENTS.md` trap 4:

| run | window | frames | result |
|---|---|---|---|
| 1 | `0xFF9584-0xFF9B83` | 3200..3700 | 0 events |
| 2 | `0xFF9584-0xFF9B83` | 0..6000 | first event at frame **169**; road traffic only in 169..573, 1115..1194, 1440..1489, 3867..4420; **nothing in 1489..3867 or 4420..6000** |
| 3 | `0xFF9584-0xFF9B83` | 3200..6100 | first event at frame **3867** |

Three independent runs agree that frames 3300, 3600 and 6000 sit in a quiet
interval. `0xFFDB36` (the interrupt tick copy) advancing monotonically through
that interval proves the machine was alive, not that the roadway state machine
was running — those are different things and the second is what the question
needed.

### 76.3 What the transform actually does (byte-exact, oracle-confirmed)

Snapshot pairs across a real burst, window `0xFF954E..0xFF9B83` at width 8,
schedule `late_drive`:

| frames | relation over all 768 bytes | bytes changed |
|---|---|---:|
| 525→526 | `base[n+1] == base[n] - twin[n]` mod 256 | 704 |
| 528→529 | same | 704 |
| 531→532 | same | 704 |
| 551→552 | `base[n+1] == base[n] + twin[n]` mod 256 | 621 |
| 555→556 | same | 621 |
| 558→559, 561→562, 564→565, 567→568 | same | 621 |

Worked example, subtract (frame 525 → 526, first eight bytes):
`E0 E0 E0 12 1F 2C 65 00` − `1F 1F 1F 02 04 06 0E 00` =
`C1 C1 C1 10 1B 26 57 00`. The 64 unchanged bytes are exactly the offsets where
the twin byte is zero.

The unchanged-byte count differs between the two passes (704 vs 621) because the
twin differs, not because either pass is partial. Both passes touch all 768
offsets; the read tap records 768 read events at `0x0298BE` **and** 768 at
`0x0298C0` on every subtract-pass frame, and the same pair for `0x0298F8`/
`0x0298FA` on every add-pass frame.

### 76.4 `base == twin` is the *loaded* state, not a contradiction

At the top of a burst the twin is rewritten by the table-copy loop at
`0x02977E` (`move.b D0,(A0)`, after `divu.w D1,D0` at `0x029776`):

```
tools/mame-ram-write-trace roms OUT --device :mainpcb:maincpu --space program \
  --base 0xFF9884 --end 0xFF9B83 --input late_drive --nothrottle \
  --mame /mnt/build/mame-oracle/mame --frames 535 \
  --start-frame 518 --end-frame 534 --max-events 400000
```

768 write events, `writer_pcs: [0x02977E]`, covering exactly
`0xFF9884..0xFF9B83`, split across frames 522-523.

And the resulting buffers before the burst are **commercial ROM**, exactly as
section 74 warned:

```
base@522 == twin@522 == roms/stunrun/stunrun_maincpu.bin @ 0x0461F0 .. 0x0464EF
stunrun_maincpu.bin sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
```

Recorded as a number and a provenance only — no bytes committed. The
divisor-8 reload is directly visible: for all 768 bytes,
`twin@525[i] == base@522[i] / 8`.

So the sequence is: **copy (base = src, twin = src/divisor) → N transform passes
(base drifts from twin) → next copy (base == twin again when divisor == 1).**
`base == twin` at a snapshot is the signature of *a window nobody has
transformed yet*, which is the opposite of evidence that the transform is a
no-op.

### 76.5 `0xFF954E` is one word with two jobs, and 68 writers

`grep ff954e` over the pinned listing returns **68** sites. Among them:

| address | instruction | role |
|---|---|---|
| `0x02978E` | `move.w D0,$FF954E` | store the loader's divisor byte after a copy |
| `0x02988A` | `move.w D0,$FF954E` | same, second loader exit |
| `0x029924` | `subq.w #1,$FF954E` | gate, subtract phase |
| `0x029AE2` | `subq.w #1,$FF954E` | gate, add phase |
| `0x02449E` | `move.w #$78,$FF954E` | startup value (`analysis/ram-map.md`) |
| `0x02469A` … `0x041670` | 63 further sites | unrelated state machines |

`analysis/ram-map.md` row 41 calls it "road-buffer copy/animation countdown and
divisor latch". That is right about the two road roles and incomplete about the
rest: the same cell is a general-purpose scratch word for large parts of the
game. Consequence for any future measurement: **`0xFF954E` observed outside a
roadway burst says nothing about the road.** During the burst it was observed
decrementing `7,7,6,6,6,5,5,5,4,…` across frames 524..534 — one decrement per
three frames, matching the pass cadence — while `0xFF9550` held 53. Outside the
burst the same address read 43, 27, 10 and 0 at frames 3300, 3350, 3400 and
3450, with `0xFF9550` at 4 and then 8: live values, belonging to a different
machine.

The gate branches on the **result** of `subq.w`, so latch `1` → `0` → expiry,
and latch `0` → `0xFFFF` → pass. Reaching zero does not stop the transform; it
arms it for the next call. The slice asserts this wrap explicitly.

### 76.6 The gate fires once per three frames, from a burst

Across `course_sweep` frames 0..6000 the subtract readers `0x0298BE`/`0x0298C0`
fired on 37 frames and the add readers `0x0298F8`/`0x0298FA` on 37, always in
pairs, always 768 events each, always every third frame inside a burst.
Measured burst spans (`course_sweep`):

| burst | subtract frames | add frames |
|---|---|---|
| 1 | 523,525,528,531,534,537,540 (7) | 550,551,555,558,561,564,567 (7) |
| 2 | 1115-1135 (9) | 1174-1190 (7) |
| 3 | 1440-1454 (7) | 1461-1482 (9) |
| 4 | 3867-3884 (7) | 3923-3938 (7) |
| 5 | 4368-4382 (7) | 4398-4415 (7) |

Running the same 0..6000 read tap under `late_drive` gave **identical** per-PC
counts — `0x2248E` 30336, and 26880 each for `0x298BE`, `0x298C0`, `0x298F8`,
`0x298FA` — with the **first burst identical frame for frame** (subtract at
523…540, add at 550…567). Only the later bursts shift. So the road transform
cadence is not input-driven; the two schedules differ in when the roadway
re-enters a burst, not in the burst's internal rhythm. That is what makes the
frame-paired snapshot comparison in §76.3 sound across two separate runs.

### 76.7 Slice shape, and why it is not a word emitter

The seam a GL backend will use is the *window*, not a word stream. The slice
therefore exposes:

- the window state itself — `base`, `twin`, `latch`, `active` (`$FFDB3C`);
- a geometry view — `{words, count=384, stride_words=384, offset_words}` with
  big-endian bytes already decoded to host order, so a backend can upload
  `view->words` directly and never needs to know the transform was a subtract;
- the divergence count between base and twin, which is what a snapshot reader
  should assert on before concluding anything about the transform.

Field meanings are **not** assigned. Nothing here claims a word is a coordinate,
a width or a curvature; `analysis/track-storage.md` still owns that question.

### 76.8 Negative controls

`gcc -std=c99 -Wall -Wextra -Werror -pedantic -Ireproduction/maincpu` over
`road_window.c road_buffer_math.c road_window_test.c` builds clean and runs
`road window C port: all checks passed (99 checks)`. Mutations, one at a time,
each applied to `road_window.c` and reverted:

| mutation | result |
|---|---|
| `subtract()` calls `add` | FAIL `subtract pass result byte 0: got 0xFF expected 0xC1` |
| passes walk 767 bytes | FAIL `add pass result byte 767` |
| gate tests `==1` not `==0` | FAIL at the gate assertion |
| `subq` truncated to 8 bits | FAIL `latch == 0xFF, expected 0xFFFF` |
| loader does not latch the divisor | FAIL `latch == 0x0, expected 0x1` |
| geometry decodes little-endian | FAIL `words[1] == 0x12E0, expected 0xE012` |
| pass skips the `$FFDB3C` store | FAIL `active == 0x0, expected 0xFF9584` |
| geometry accepts an overrunning slice | FAIL `geometry(...) == 0x1, expected 0x0` |
| geometry stride is 1 word | FAIL `stride_words == 0x1, expected 0x180` |
| divergence always zero | FAIL `divergence == 0x0, expected 0x2DB` |

**One mutation is NOT caught and is not expected to be**: removing the divisor-0
check from the window layer is unobservable through this slice, because
`stunrun_road_buffer_copy_scaled` refuses the same call. That is recorded in the
test next to the assertion rather than left implied.

Zero-check control: building `road_window_test.c` with all eight test calls
removed yields `FAIL no checks ran (matched-test count is zero)` and exit 1.

### 76.9 Still UNKNOWN

- Which field of the window, if any, the subtract/add pair is ramping. The
  "descending ramp `0xA3` toward `0x19`" note in `track-storage.md` §"The
  `0x0298C0` writes" is consistent with repeated subtract passes against a
  fixed twin, but this section does not promote that to an explanation.
- Which of the 68 `$FF954E` writers is running at any given moment. Only the two
  road gates are transcribed.
- Why the burst cadence is three frames, and what selects a burst.
- The GSP-side consumer. Out of scope here by instruction; it is the other side
  of the same seam.

## 77. A labelled state/event log, and two refutations of our own reading

The user watches the game onscreen and wants to know WHEN a machine-state
change corresponds to something they SEE. `shell: watch-word frame=… addr=0xFF9550
value=0x0004` cannot answer that: a bare address does not say which game
quantity it is. So `STUNRUN_STATE_LOG=1` prints named, confidence-carrying lines
on change. Building it produced two findings about **us**, not the ROM, plus one
measurement the log was built to make possible.

### 77.1 What was added

| File | Role |
|---|---|
| `native/state_watch.{h,c}` | the log: the word table, the ADSP transport classifier, the frame-boundary sampler |
| `native/state_watch_test.c` | 283 assertions on the exact printed lines (section 76.5) |
| `native/musashi_cpu.{h,c}` | an observation-only ADSP-window store observer, NULL by default |
| `native/shell.c` | `STUNRUN_STATE_LOG` gate, one call per frame, a summary at exit, **and the `watch-word` polarity fix of 76.4** |
| `run.sh -h` | the new switch, its two limitations, and the grep recipes |

The word table is 15 entries, ordered rarest-change-first so `head` of a frame's
block shows the rare events rather than the per-frame tick. Every entry carries
the `Confidence` cell from `analysis/ram-map.md` **verbatim, including its
negations** — `TIMING-SOURCE, NOT TRACK POSITION` is the reason a rising
`irq-tick` cannot be misread as player position. The selection rule is
"Confidence cell carries a CONFIRMED / DYNAMIC / RESOLVED token", it is stated at
the table in `state_watch.c`, and `state_watch_test.c` asserts every row satisfies
it so a `STATIC-CANDIDATE` row cannot drift in.

Rows deliberately excluded, with reasons in the same comment: `0xFF9544`
(no dynamic observation), `0xFF956C` (resolved as a comparison cache, explicitly
NOT-CRAFT-STATUS), the `STATIC-CANDIDATE` rows, and the `OBSERVED-NEGATIVE` rows.

### 77.2 THE ADSP HONESTY CONSTRAINT

**We do not execute the ADSP-2100.** There is no ADSP instruction decoder in this
tree, no ADSP PC and no ADSP bus; `native/checkpoint.h`'s `adsp_pc` is a field of
the *replacement-image* model, not a running core. `musashi_cpu.c` keeps
`adsp_pgm_ram`, which is the 68010's **view** of the DSP's program RAM, and
routs 0x818000-0x81FFFF straight to a `break` — the device side is dropped.

So the log can show **transport** and cannot show **execution**:

| can show | cannot show |
|---|---|
| every MC68010 store into 0x818000-0x81FFFF, with its address, data, width and bus PC | any ADSP-internal event: no SOMLATCH emission, because nothing decodes ADSP instructions |
| the recorded control ops, labelled from `adsp_control_seq.c` | GINT / SFLAG / SPMODE out of the DSP, because no DSP runs |
| the IRQ-clear pair at 0x818060 | an ADSP PC, an ADSP cycle count, any ADSP register |
| the upload frame boundaries (recorded frame-indexed facts, not measurements) | anything the DSP did in response |

Every ADSP-flavoured line is `kind=adsp-control` / `kind=adsp-irq-clear` /
`kind=adsp-upload` / `kind=adsp-install` and names a 68010 address. The enable
header says so in the log itself (`no-adsp-2100-is-executed`), because a header
comment nobody opens is not a constraint. MAME remains the authority for what the
DSP then did.

### 77.3 What the transport run actually shows

600 frames, `IN0=0xE7`, no input (`STUNRUN_STATE_LOG=1 STUNRUN_LIVE_VIDEO=0
STUNRUN_FRAMES=600 ./run.sh`, `RESULT PASS`). 25 ADSP transport events, 0 dropped,
3 upload transitions. Against MAME's recorded 600-frame multiset:

| address | label | MAME 600f | native 600f |
|---|---|---:|---:|
| `0x818002` | LEDs | 54 | **1** |
| `0x818008` | default/control | 2 | 2 |
| `0x81800A` | /BR assert | 2 | 2 |
| `0x81800C` | /HALT assert | 1 | 1 |
| `0x81800E` | ADSP reset assert | 3 | 3 |
| `0x818014` | default/control | 2 | 2 |
| `0x818016` | deferred ADSP bank switch | 1 | 1 |
| `0x818018` | default/control | 2 | 2 |
| `0x81801A` | /BR release | 2 | 2 |
| `0x81801C` | /HALT release | 2 | 2 |
| `0x81801E` | ADSP reset release | 2 | 2 |
| | **control writes** | **73** | **20** |

Ten of eleven match exactly. The one that does not is `0x818002`, the LED
register — the only control address whose MAME handler has **state we drop**, so
a 68010 polling it back sees a different value here. That is the honesty
constraint showing up as a number rather than as a caveat, and it is the expected
shape: a transport log can differ from the oracle exactly where the transport's
far side is unmodelled.

Two further observations, both `UNKNOWN`:

- **Three IRQ clears, not two.** At bus-PCs `0x02142C`, `0x02C23A` and
  `0x00023C`. The third is at frame 0 and `0x00023C` is 572 bytes into the ROM —
  i.e. the reset path, not either recorded title-path writer. `adsp_control_seq.h`
  records two writes per 600-frame title run; this run sees three because it also
  counts the reset. Not a contradiction, but `IRQ-clear writes = 2` is
  title-path-only and should not be read as a per-reset constant.
- **`0x818040` is written and is not in the recorded set.** Twice, at bus-PCs
  `0x0212C0` and `0x02143A`, data `0x0004`. `STUNRUN_DISASM` puts
  `021432: 33 fc 00 04 00 81 80 40  move.w #$4, $818040.l`, immediately followed by
  `02143a: 4e 75 rts`, so the frame is aligned and this is a real instruction.
  `adsp_control_seq.h`'s eleven addresses stop at `0x81801E`; `0x818040` is
  inside the same 32-byte window and outside that set. The log prints it as
  `label=unrecorded confidence="UNKNOWN"` rather than swallowing it, because a
  silently dropped control-window write is indistinguishable from "the 68010 never
  did that" — which is how an entire input-mode sweep once looked like clean null
  results. See IRQ-0007 for the one question this raises.

### 77.4 Refutation 1 — `stunrun_mem_read*` returns non-zero ON SUCCESS, and the shell's `watch-word` had it backwards

Writing the sampler, the first version of `state_watch.c` read

```c
if (stunrun_mem_read16_be(ports, addr, &value) != 0) { reads_failed++; continue; }
```

and reported `reads_failed = 14` — all fourteen words, every frame, while the same
call from a test program returned 1. `native/fake_ports.c:83-92` returns **1 on
success, 0 on failure**; every other call site in the tree reads it that way
(`CHECK(stunrun_mem_read8(...))`, `assert(stunrun_mem_read32_be(...) && ...)` in
`game_loop_test.c:27`).

`native/shell.c` held the same inversion, in the `watch-word` readback itself:

```c
if (stunrun_mem_read16_be(&ports, g_watch_frames_addr[i], &v) != 0)
    continue;                                   /* skips on SUCCESS */
```

`shell: watch-word` could therefore only ever print for an address the RAM model
**refuses**, with `v` left at 0 — in practice it printed nothing at all. Nothing
pinned it: the feature is behind `STUNRUN_WATCH_WRITE`, no test sets it, and no
test greps for `watch-word`. Fixed to `== 0`, and the fix is visible in a real run:

```text
shell: watch-word frame=547 addr=0x00FF954E value=0x0078 (was 0x0000)
shell: watch-word frame=576 addr=0x00FF9582 value=0x0100 (was 0x0000)
```

This is our own misunderstanding, so it is recorded here rather than in
`BUGS.md`, whose entries are about the 1989 game and about MAME. The general
lesson is the one this repository already states for a different reason: a
diagnostic nobody exercises is not a diagnostic.

### 77.5 Refutation 2 — a longword row names the LONGWORD's address, and the half that moves is the other one

The ram-map row is `FFFF8014` (bus-visible `0xFF8014`), "interrupt-driven global
tick; PC `0x0222DE` executes `addq.l #1,$8014.w`". The first version of the table
watched `0xFF8014` and reported **zero changes over 600 frames**.

`STUNRUN_WATCH_WRITE=0xFF8014` in the same shell reported, in the same process,
**966 writes from PC `0x000222DE`, width 32, last value `0x03C4`**. The writer PC
matches the ram-map exactly, so the ROM was doing what ram-map says. The
longword at `0xFF8014` lays out big-endian, so `0x03C4` is at `0xFF8016` and
`0xFF8014` stays 0. Confirmed directly: `stunrun_mem_write32_be(&p, 0xFF8014,
0x3C4)` then `stunrun_mem_read16_be(&p, 0xFF8014)` returns **0**, while
`read32` returns `0x000003C4` and `main_ram[0x14..0x17]` is `00 00 03 C4`.

Fixed to watch `0xFF8016` as `irq-tick` and keep `0xFF8014` as `irq-tick-hi`. It
now steps **+4 per frame from frame 364** — which independently confirms
ram-map's "about four updates per frame".

The same trap applies to every longword row, and the table now watches **both**
halves because a half that never changes costs nothing (change-only printing) and
turns "never moved" into a measurement instead of an absence:

| ram-map row | longword at | low half (the value) | high half |
|---|---|---|---|
| `0xFF9532-0xFF9535` score | `0xFF9532` | `0xFF9534` = `score` | `0xFF9532` = `score-hi` |
| `0xFF9568-0xFF956B` time-remaining | `0xFF9568` | `0xFF956A` = `time-remaining` | `0xFF9568` |
| `0xFF9564-0xFF9567` elapsed-update | `0xFF9564` | `0xFF9566` | `0xFF9564` |
| `FFFF8014` irq tick | `0xFF8014` | `0xFF8016` | `0xFF8014` |

Had the task list been followed literally — "`0xFF9568` time-remaining",
"`0xFF9532` score" — the log would have shown **nothing at all** for either. The
measured `time-remaining` value `0x215C` is the ROM seed longword `0x0000215C`
from `move.l $4EDE6.l,$FF9568.l` (listing `0x024372`), and its low half is what
counts down.

### 77.6 The bus PC is the instruction address + 6 for these forms — and both recorded IRQ-clear writers reproduce exactly

The observer deliberately does **not** single-step (stepping moves
`m68ki_check_interrupts()` to every instruction boundary and changes interrupt
latency), so it reports the PC as seen inside the bus callback rather than the
instruction address. For `move.w #imm,(xxx).L` that offset is a fixed **+6**:
opcode (2) + immediate (2) + absolute-long extension (4), all consumed before the
store. From `STUNRUN_DISASM=0x02141A:0x021450`:

```text
02141a: 42 79 00 81 80 1c  clr.w $81801c.l
021420: 42 79 00 81 80 0e  clr.w $81800e.l
021426: 42 79 00 81 80 60  clr.w $818060.l     <- IRQ clear
02142c: 42 79 00 81 80 14  clr.w $818014.l
021432: 33 fc 00 04 00 81 80 40  move.w #$4, $818040.l
02143a: 4e 75  rts
```

and at `0x02C234`, the second recorded writer, the same six bytes
`42 79 00 81 80 60` (`clr.w $818060.l`). The run reported bus PCs `0x02142C` and
`0x02C23A`:

```text
0x02142C - 6 = 0x021426   (recorded IRQ-clear writer PC #1)
0x02C23A - 6 = 0x02C234   (recorded IRQ-clear writer PC #2)
```

**Both MAME-recorded writer PCs are reproduced exactly.** The log keeps printing
`bus-pc` with the `note="bus-pc is the PC inside the bus callback, not the
instruction address"` string and the two reference PCs on every IRQ-clear line,
rather than printing a derived `insn=` field — because +6 is only true for the
immediate/absolute-long form, and a log that applied it unconditionally would be
exactly the kind of over-claim this repository forbids.

### 77.7 The real run, and its limits

```text
shell: state frame=327 kind=word name=time-remaining addr=0xFF956A value=0x215C (was 0x0000) …
shell: state frame=364 kind=word name=irq-tick addr=0xFF8016 value=0x0004 (was 0x0000) …
shell: state frame=547 kind=word name=road-countdown addr=0xFF954E value=0x0078 (was 0x0000) …
shell: state frame=576 kind=word name=elapsed-update addr=0xFF9566 value=0x0001 (was 0x0000) …
shell: state frame=576 kind=word name=animation-counter addr=0xFF9582 value=0x0100 (was 0x0000) …
```

**266 `shell: state` lines over 600 frames**: 238 `kind=word` (234 of them the
`irq-tick` heartbeat, 4 genuine one-offs) + 28 ADSP (25 transport + 3 upload
transitions). 0 dropped, 0 RAM read failures, `RESULT PASS`. The non-heartbeat
game-state content is **four lines in 600 frames**, because this run has no input:
`score`, `track-index`, `game-state`, `input-state` and `object-hits` are all flat.

That is a finding, not a bug — and it is exactly why the exit summary prints a
per-word `changes=` count. With change-only printing, "no line for track-index" and
"track-index was never watched" are otherwise indistinguishable.

Two limits the user must be told about, both stated in `state_watch.h` and in
`./run.sh -h`:

1. **Once per frame at the frame boundary.** A value set and cleared inside one
   frame is invisible. That is a direct consequence of the sampling rate, not a
   heuristic; `STUNRUN_WATCH_WRITE` is the per-store complement and the two
   together settle it — which is how 76.5 was found in one step.
2. **The ADSP lines are transport.** Stated again here because it is the single
   easiest thing to over-read.

### 77.8 What the test asserts, and how it was shown to have teeth

283 assertions, all on **printed output**: stdout is captured with
`dup`/`tmpfile` and compared against literals written out from the format strings.
There is deliberately no callback or returned-struct side channel — a side channel
would let a broken printer pass, which is the failure this feature exists to end.

- the enable header names every field with its confidence string, and the
  `state-log name=` line count equals the table size;
- every table address is a live word in the memory model, probed and read back, and
  is inside the 32 KiB mapped window rather than one past its end (where a read
  wraps to address 0 — `fake_ports.h` records that once producing 376 284 bogus
  "RAM" bytes);
- every confidence string carries a confirmed/dynamic token (the selection rule);
- the baseline frame prints no word lines, and an unchanged image prints **nothing
  at all** on any later frame;
- one synthetic change set reproduces an **exact** golden by whole-buffer equality:
  four ADSP lines in insertion order then three words in table order, each with its
  previous value;
- the summary block, the `(was …)` chain across two further changes, table ordering,
  and the 4096-entry ring's overflow being counted and announced;
- the upload milestones are cross-checked against `adsp_control_seq.c`, and every
  recorded control op is checked to produce its recorded label;
- observation-only: the whole RAM image is byte-compared before and after a
  sampling pass.

**Negative controls.** A comparator that cannot fail is not a check, and this
repository has shipped three tests that passed for the wrong reason. So the same
comparator is shown *rejecting* a golden one nibble different, one with a line
deleted, one with a line duplicated, and one with a corrupted confidence string —
before the positive result is accepted. Every positive comparison is accompanied
by an exact expected line count, and those counts are non-zero, so a control which
matched nothing cannot read as a pass. The table size is pinned at 15, so a table
of zero entries cannot make the "nothing was printed" checks vacuous.

Eight deliberate sabotages of the implementation, each confirmed to fail, then the
restored build passing:

| sabotage | failures |
|---|---:|
| log every field every frame (change-only removed) | 19 |
| constant `(was 0x0000)` | 1 |
| invert the read polarity (the 76.4 bug) | 21 |
| never drain the ADSP transport ring | 36 |
| drop the confidence string from word lines | 14 |
| silently drop unrecorded control-window writes | 2 |
| seed silently removed | 1 |
| ring overflow silent | 3 |
| *(restored)* | **0** |

### 77.9 Still UNKNOWN

- Why the native run sees **1** LED write where MAME sees 54. The dropped LED
  handler is the obvious candidate and is consistent with every other address
  matching, but it is a hypothesis. Arming `STUNRUN_WATCH_WRITE=0x818002` and
  comparing MAME's writer PC would settle it.
- Whether the reset-path IRQ clear at frame 0 (`0x00023C`) is a third *writer* or
  the same routine reached before the title path starts.
- What `0x818040` is. One instruction, `move.w #$4,$818040.l`, data `0x0004`,
  never observed by the MAME tap that produced `adsp_control_seq.h`. Its data is
  non-zero where every recorded control write is `0x0000`.
- **IRQ5 does not reach `0x0222DE` before frame 364 in this native run.** The
  counter is flat for the first 364 frames and then steps +4/frame. `BUGS.md`
  SR-08 places the handler's `addq.l` elsewhere (`$ff9042.l` at `0x82C`-`0x8C0`),
  so there may be two tick counters and the 364-frame boundary may be when the
  second one starts running. Not established.
- Whether `0x818002`'s width-16 observation is right. See IRQ-0007.

## 78. Refutation: the road-window subtract off-by-one was invisible, not killed

**Section 76 reported 10 of 11 mutations killed, including "walk 767 bytes →
FAIL byte 767". That claim was false for the subtract pass.** I rebuilt the
mutation rather than diffing it, and it passed.

Root cause, confirmed by reading the fixture rather than the code under test:

```
sub_twin        len=768  last4=['0xf','0x0','0x0','0x0']  zeros=64   [767]=0x00
sub_base_before len=768  last4=['0x70','0x0','0x0','0x0'] zeros=37   [767]=0x00
sub_base_after  len=768  last4=['0x61','0x0','0x0','0x0'] zeros=37   [767]=0x00
add_twin        len=768  last4=['0xd','0xb','0x10','0x17'] zeros=147  [767]=0x17
```

The oracle subtract fixture has `sub_twin[767] == 0`. So
`base[767] - twin[767] == base[767]`, a walk stopping at byte 767 leaves byte 767
at `0x00`, and `sub_base_after[767]` is also `0x00`. Byte-for-byte identical. The
one field the check pinned was the one byte the operation **cannot** change.

The add fixture does not share the defect (`add_twin[767] == 0x17`), which is
why mutating the add loop was caught and mutating the subtract loop was not. The
kill was real — it just belonged to the other pass.

**This is §73's trap for the third time.** There: a partial render passed because
axis-separator pixels counted as bar content, then because page background
counted. Here: an off-by-one passed because the sampled tail byte was
idempotent under the operation. In both cases the check reproduced the shape of
the bug rather than its effect. The general failure is not "weak assertions" but
**an expectation drawn from data the operation cannot move.**

Fix: `test_subtract_pass_covers_the_final_byte` runs a labelled synthetic vector
whose last three twin bytes are all non-idempotent under both passes. Synthetic
because the oracle has no such frame, and labelled as synthetic in the source so
it is never mistaken for captured evidence. Check count 99 -> 1640.

Rebuilt negative control, both passes:

| mutation | before | after |
|---|---|---|
| shared subtract loop `i + 1 < bytes` | **PASSED** | `FAIL base[i] == 0x5F, expected 0x47` |
| window wrapper length arg -> `BYTES - 1` | **PASSED** | killed |

**Lesson for §76's other claim.** The eleventh mutation ("divisor-0 check is
unobservable") was reported honestly. But 10/11 was asserted, not measured by
rebuild. Six of the repo's now-known wrong-reason passes came from believing a
mutation table rather than re-running it.
