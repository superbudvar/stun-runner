# BUGS.md — defects in the *original* S.T.U.N. Runner, not in our port

Every entry here is a fact about the 1989 Atari game: its ROM code, its use of
the hardware, or the MAME 0.289 driver that models the board. **Nothing in this
file is a bug in our reimplementation.** Bugs in our code are ordinary defects —
fix them, cite them in the commit or bead, and do not add them here. If a line
here is a *misunderstanding* we had rather than a defect in the original, it
belongs in `docs/mc68010-boot-findings.md`, which keeps our refutations of our own
earlier notes.

## How to add an entry

Copy the template. Fill in every field. Do not weaken a `Confidence` line to make
an entry feel more solid — an unknown is a useful entry; a wrong one is not.

```
### SR-nn — one-line title

- **Kind:** ROM code | ROM data | hardware abuse | driver defect | driver gap
- **Where:** `136070-2102.200r` @ `0x67A` (ROM addr), or `mame-ref/src/...cpp:123`
- **Symptom:** what goes wrong, observed as an emulator artefact or as data
- **Evidence:** the exact instruction, byte pattern, or driver line
- **Confidence:** confirmed by MAME | confirmed by measurement | inferred
- **Impact on us:** why this matters for the C++ conversion, or "none, curiosity"
```

`Kind: driver defect` and `driver gap` are for the MAME driver specifically — keep
them in a separate section (§ MAME driver) because they change how the oracle
behaves and must be reported upstream.

---

## ROM code

### SR-01 — The RAM march test never tests the last word

- **Kind:** ROM code
- **Where:** `136070-2102.200r` @ `0x59D8` (fill), kernels `0x59E6`/`0x5A32`/`0x5A7E`/`0x5AD4`
- **Symptom:** the power-on RAM diagnostic writes 16,383 of the 16,384 words in
  the 32 KiB work RAM. The word at `0xFFFFFFFE` is never written, never read back,
  and never validated.
- **Evidence:** `lea (-$2,A1),A0` / `addq.w #2,A0` / `move.w D0,(A0)` /
  `cmpa.l A2,A0` / `bcs`, with `A1 = $FFFF8000` and `A2 = $FFFFFFFE`. `A2` is the
  exclusive end, so the loop stops with `A0 == A2` having last stored at `A2-2`
  = `0xFFFFFFFC`.
- **Confidence:** confirmed by measurement (our transcription
  `native/ram_march.c` writes exactly 16,383 words and returns `0xFFFFFFFE`)
- **Impact on us:** the conversion must reproduce the off-by-one if we want to
  match the diagnostic screen's behaviour, and must **not** "fix" it into a full
  sweep. `native/ram_march_test.c` asserts it explicitly.

### SR-02 — Boot can re-enter itself forever on the service bit

- **Kind:** ROM code
- **Where:** `0x254` (test) → `0x29E` (`jmp (A1)`)
- **Symptom:** with IN0 bit 5 (the service switch) asserted, `beq $2a0` at `0x25C`
  is not taken, so the ROM runs the whole GSP init and then `jmp (A1)` returns to
  `$20004`, which holds `0x216` — the top of boot. The board re-initialises
  continuously and never reaches the game.
- **Evidence:** `0x29E: jmp (A1)`; ROM `0x20004 = 0x000212AE` when the vector is
  loaded from `0x20000` (`A0 = $20000` at `0x286`). Note this is a *different*
  reset vector from the power-on `PC = 0x00000216` in the exception table — the
  self-reload deliberately jumps past the `stop #$2700` prologue.
- **Confidence:** confirmed by measurement (with IN0 bit 5 set we produced 14 init
  passes and 3.8M GSP host writes in 600 frames where MAME produces one pass)
- **Impact on us:** the port's IN0 default must have bit 5 **set** to match MAME's
  `PORT_SERVICE(0x20, IP_ACTIVE_LOW)`. Getting this backwards sends the ROM down
  the self-test path and costs hours. See `docs/mc68010-boot-findings.md` §1.

### SR-03 — The message-writer reads a word at an odd address and keeps only its low byte

- **Kind:** ROM code
- **Where:** `0x2326`, the `move.b $801B` in the palette/colour-word setup
- **Symptom:** a byte load from an odd address on a 16-bit bus reads the *word* at
  that address and discards the high byte. The retained byte is simultaneously the
  low byte of the blue ramp word at `0x801C`.
- **Evidence:** `move.b` at `$801B`; the neighbouring blue word is written at
  `$801C`. Discovered while transcribing the routine — the first draft of
  `native/gsp_boot.c` treated the two as independent.
- **Confidence:** inferred (byte-width behaviour is certain; the ROM's intent is not)
- **Impact on us:** harmless if we compute the palette cube directly, but the C++
  must not read `$801B` as an independent input.

### SR-10 — The display-list interpreter's only exit is a hard SSP reset

- **Kind:** ROM code
- **Where:** `136070-2102.200r` @ `0x225E4`-`0x22748` (the interpreter),
  `0x225CE` (the exit), `0x21286` (the SSP reset entry)
- **Symptom:** the routine compiles a byte stream into 16-bit GSP FIFO words and
  **never returns normally**. Three of its four terminators — a nonzero first
  byte, opcode `0x80` and opcode `0xC0` — branch to `0x225CE`, whose first
  instruction is `jsr $21286`. `0x21286` is `stop #$2700` / reset SSP / VBR=0 /
  `jmp (A0)` through `$4.w`: it never comes back. A display list that runs off its
  end therefore **reboots the machine** instead of returning to its caller. The
  epilogue's own flush (`0x225D4`-`0x225DA`: `tst.w D2` / `clr.b D3` /
  `move.w D3,(A2)`) is dead code, because it sits *after* the `jsr`.
- **Evidence:** `0x225CE: jsr $21286.l` / `0x225D4: tst.w D2` / `0x225D8: clr.b D3`
  / `0x225DA: move.w D3,(A2)` / `0x225DC: move.l A0,D0` / `0x225DE: movem.l
  (A7)+,D2-D5/A2-A3` / `0x225E2: rts`. Branch targets: `0x22602 bne $225ce`,
  `0x226D4 beq $225ce` (opcode `0x80`), `0x22704 beq $225ce` (opcode `0xC0`).
  The single exception is opcode `0x40`: `$22674 bmi $225D4` **skips over** the
  `jsr`, so `0x40` really does `rts` — with the flush skipped as well.
- **Confidence:** confirmed by disassembly (full listing in
  `docs/mc68010-boot-findings.md` §11.2)
- **Impact on us:** the C slice `stunrun_gsp_display_list_run()` cannot return
  "success" the way the ROM means it. It reports a `stunrun_gsp_display_list_end_t`
  (`STUNRUN_GSP_DL_END_RESET` vs `STUNRUN_GSP_DL_END_OPCODE_40`) instead of
  emulating the reset, and **must not** "fix" the dead flush into a live one.

### SR-11 — Six dead loads in the display-list interpreter, left behind by an optimiser

- **Kind:** ROM code
- **Where:** `0x2268C`, `0x226A4`, `0x226C2`, `0x22722`, `0x2273A` (dead word
  loads), `0x2263E` (dead shift)
- **Symptom:** five `move.w (-$20,A1,Dn.w),D3` instructions load a half-written
  word into `D3` and are then **immediately overwritten** by a `move.b …,D3`,
  which clears bits 31..8 of `D3`. A sixth instruction, `lsl.w #8,D3` at `0x2263E`,
  is likewise destroyed by the `move.b (A0)+,D3` that follows it. The observable
  effect of the odd-pointer copy loop is therefore to emit **the second byte only**
  (`0x00bb`), discarding the first byte the shift was supposed to keep.
- **Evidence:** `0x2268C: move.w (-$20,A1,D4.w),D3` /
  `0x22690: move.b ($1,A1,D5.w),D3`; `0x2263C: move.b (A0)+,D3` /
  `0x2263E: lsl.w #8,D3` / `0x22640: move.b (A0)+,D3`. Only the high half of the
  word at `$FF900A+2i` was ever written (`move.b D0,(-$20,A1)` at `0x225BC`), so
  even if these loads were live they would read a never-initialised low byte.
- **Confidence:** confirmed by disassembly
- **Impact on us:** a first draft of `native/gsp_display_list.c` "helpfully"
  preserved the high byte and emitted `0x5A5B` where the ROM emits `0x005B`; the
  test caught it. **Do not** write the shift as a real byte pack.

### SR-12 — An odd-length copy from an even pointer reads a word but advances one byte

- **Kind:** ROM code
- **Where:** `0x2266A`-`0x2266C`, with the parity test at `0x2262C`
- **Symptom:** the copy opcode splits on the *stream pointer's* low bit — even
  pointer copies words, odd pointer copies bytes. On the even path an odd count
  falls through to `move.w (A0),D3` (no post-increment) followed by
  `addq.l #1,A0`. Two bytes are therefore consumed for the trailing read but the
  pointer advances only one, leaving it **misaligned by one byte**. The *next*
  copy opcode's parity test reads that misaligned pointer and picks the opposite
  half of the copy loop, so the damage propagates into the following opcode
  rather than self-correcting.
- **Evidence:** `0x2266A: move.w (A0),D3` / `0x2266C: addq.l #1,A0` /
  `0x2266E: not.w D2`; parity test `0x2262C: move.w A0,D1` /
  `0x2262E: andi.w #$1,D1` / `0x22632: beq $22656`.
- **Confidence:** confirmed by disassembly
- **Impact on us:** stream-pointer parity must be a real, tested input to the C
  slice. `native/gsp_display_list_test.c` pins `(uintptr_t)stream & 1`, and
  `test_stream_parity` walks three flush/parity transitions to keep this honest.

### SR-13 — The "parked half-word" is never emitted; the flag also never clears

- **Kind:** ROM code
- **Where:** `0x2261E`-`0x2262A` (copy), `0x226D8`-`0x226E0` (fill), `0x22678`
  and `0x22718` (the two `D2 != 0` table paths)
- **Symptom:** `D2` is the interpreter's "an odd copy left a half-word behind"
  flag. The mechanism that should emit that parked word **does not work**.
  * On the copy path, `$22622` *reads a fresh byte from the stream* into `D3` and
    emits that, so the parked byte is silently dropped.
  * On the fill path, `$226D8` has already loaded `D3` with the fill's own operand
    byte **before** the `D2` test, so `$226DE` re-emits the operand. The parked
    word is dropped there too, and the operand is emitted one extra time.
  * On the `0x41`-`0x7F` and `0xC1`-`0xFF` paths there is no `$226E0`-style
    `moveq #0,D2` at all, so once `D2` is set it **stays set for the rest of the
    list**, permanently switching every later opcode onto its alternate branch
    (and, for the table opcodes, onto the other nibble of the operand byte).
- **Evidence:** `0x22622: move.b (A0)+,D3` / `0x22624: move.w D3,(A2)` /
  `0x22626: moveq #0,D2`; `0x226D8: move.b (A0)+,D3` / `0x226DA: tst.w D2` /
  `0x226DC: beq $226E8` / `0x226DE: move.w D3,(A2)`; no `D2` clear anywhere
  between `0x226A8` and `0x226C6` or between `0x2271C` and `0x22736`.
- **Confidence:** confirmed by disassembly
- **Impact on us:** reproduced exactly. `test_stream_parity` asserts that the
  parked leftovers `0x9B43`, `0x0055` and `0x006C` **never** appear in the FIFO
  log, and that `D2` survives a table-run opcode. Do not "complete" the flush.

### SR-16 — The OKI stop handler matches a sample number, not a sound index

- **Kind:** ROM code
- **Where:** `136070-2123.10c` @ `0x5C6F` (JSA-II 6502 sound ROM)
- **Symptom:** the only "stop this sound" command, `$66`, stops the wrong sound.
  Every other class-19 command takes its argument from `$616D` and uses it as a
  **sound index** into the three 53-byte tables at `$6532` (sample), `$6567`
  (voice+volume) and `$659C` (priority). Command `$66` reaches `$5C6F` with
  argument 48, and `$5C6F` then compares that raw argument against a *sample
  number*. `$6532[48]` is `$2A`, but the handler never indexes `$6532` with the
  argument; it scans the four voices and compares `$6532[current sound]` with
  the argument itself. Only `$6532[46]` holds the value 48, so `$66` stops sound
  index **46** (sample `$30`, voice 2) — not sound index 48 (sample `$2A`,
  voice 3) that a literal reading of "argument 48" suggests.
- **Evidence:** `0x5C6F: 08` `php` / `0x5C70: 78` `sei` / `0x5C71: A2 03` `ldx #$03`
  / `0x5C73: B4 52` `ldy $52,x` / `0x5C75: D9 32` `cmp $6532,y` /
  `0x5C78: 65 F0 05` `beq $5C7F` / `0x5C7A: CA` `dex` / `0x5C7B: 10 F6` `bpl $5C73`
  / `0x5C7F: BD 6B 5C` `lda $5c6b,x` / `0x5C82: 8D 00 2A` `sta $2a00`.
  The accumulator still holds the argument loaded by the dispatcher at
  `0x4386: BD 6D 61` `lda $616d,y`, because no handler code before `0x5C75`
  writes to A. `$52,x` is the per-voice current sound index and `$6532` is the
  sample table, so the comparison is `argument == sample_number`.
- **Confidence:** confirmed by disassembly and by measurement (all 53 `$6532`
  entries decoded from the ROM; `sample_table[46] == 0x30 == 48` is the unique
  match for `arg == 48`).
- **Impact on us:** reproduced exactly in
  `stun-runner/reproduction/sound/oki_cmdmap.c:translate_stop()`, which compares
  `jsa_sample_table[cur] != argument` and pre-computes **no** `target`. The CTest
  `reproduction/sound/oki_cmdmap_test.c` asserts that `$66` emits one `$20`
  (voice-2 stop) write when sound index 46 is playing, and emits nothing when
  only sound index 48 is playing. Do not "fix" the handler to index by sound.

### SR-18 — A duplicate-record hit silently consumes a course milestone

- **Kind:** ROM code
- **Where:** `136070-2102.200r` @ `0x3A294` (dedupe) and `0x3A2C6` (threshold compare)
- **Symptom:** each course has a hit count at which a milestone event fires. The
  milestone requires the counter to land on that value by *equality*. The dedupe at
  `0x3A294` returns early **without** incrementing the counter when the same record
  is hit twice in a row. So a repeated hit that straddles the threshold consumes the
  threshold permanently: the counter skips the exact value and the milestone never
  fires for that course.
- **Evidence:** `0x3A294: beq $03A314` jumps past both `0x3A298: addq.w #$1,$FFDD02`
  and `0x3A2EC: add.l d2,$FF9532`. `0x3A2C6: cmp.w $FFDD02,D0` followed by
  `0x3A2CC: bne $03A2DA` is equality, not `>=`. Thresholds live in a 33-byte ROM table
  at `0x4EE4E` indexed by the course word at `0xFF9578`; course 2 wants hit 11,
  course 5 wants hit 28, course 9 wants hit 70, course 16 wants hit 36.
- **Confidence:** confirmed by static reading; the runtime consequence is inferred
  and needs a course-2 or course-5 run that collides with the same record on two
  consecutive frames.
- **Impact on us:** `stunrun_mc68010_collision_award()` in `native/mc68010_physics.c`
  reproduces both the dedupe and the equality compare, and
  `test_award_duplicate_is_deduped` plus `test_award_milestone_requires_exact_match`
  pin them separately. Do not relax `==` to `>=` to "fix" the lost milestone.

### SR-19 — The event staging queue drops entries silently when a lane is full

- **Kind:** ROM code
- **Where:** `136070-2102.200r` @ `0x301E6` (queue), callers `0x3A2D2` / `0x3A30C`
- **Symptom:** `$00301CA` stages event bytes into a 32-lane, 32-deep byte buffer. When
  a lane already holds 32 entries the routine returns immediately with no flag, no
  counter bump, and no error. Callers unconditionally pop their argument and continue,
  so a full lane loses the event with nothing observable. The object-hit path pushes
  `0x16` (milestone) and `0x5F`/`0x6C` (per-course hit) through this queue.
- **Evidence:** `0x301E6: cmpi.b #$20,(A0,D0.W)` / `0x301EC: bcc` returns before any
  store. Lane index is the byte at `0xFFDBC8`, count is the byte at `0xFFDBC4`, and the
  payload lands at `0xFFDB64 + lane*32 + count`.
- **Confidence:** confirmed by static reading; the consequence is inferred.
- **Impact on us:** any slice that models event dispatch must decide explicitly whether
  it drops on full. `native/mc68010_physics.c` models the queue producer's decision
  (`milestone_queued`, `event_queued`) but not the queue itself; the drop-on-full
  behaviour must not be "fixed" to block or to wrap.

## ROM data

### SR-04 — The palette cube writes the high bank before the low bank

- **Kind:** ROM data
- **Where:** `0x6EE` (palette cube routine)
- **Symptom:** the routine fills `palette_hi` from `0x801C` **first**, then
  `palette_lo`. A faithful transcription that assumes low-then-high will produce a
  visibly different intermediate frame.
- **Evidence:** address order in the `addi.w` walk at `0x6EE`; also reproduced in
  `native/gsp_boot.c` with a comment marking the asymmetry deliberate.
- **Confidence:** confirmed by measurement (transcribed and unit-tested)
- **Impact on us:** the asymmetry is observable only if we render a frame mid-cube,
  which the conversion will do. Keep it.

### SR-05 — The palette cube's blue ramp is skewed; only odd blocks are non-black

- **Kind:** ROM data
- **Where:** `0x570`, `write_ramp_single` for the blue component
- **Symptom:** the eight blue steps are `0,8,0,8,0,8,0,8`, so blocks 0, 2, 4, 6
  stay black. The red and green pairs use `(0,0),(0,0),(0,8),(0,8),(8,0),(8,0),
  (8,8),(8,8)`, which is not the same skew.
- **Evidence:** step tables in the routine; the transcriber initially expected
  blocks 2,3,6,7 to be non-black and the test caught it.
- **Confidence:** confirmed by measurement (test expectation corrected against the
  disassembly, then locked in `native/gsp_boot_test.c`)
- **Impact on us:** the cube is the game's boot palette, so every colour the player
  sees at frame 0 derives from it. Get it right or the whole game is tinted wrong.

### SR-06 — `0x548E` is a second copy of the VRAM fill with no static references

- **Kind:** ROM code
- **Where:** `0x548E`-`0x54C0`
- **Symptom:** a near-clone of the `0x67A` fill (same `ori.w #$1800` / `lea
  $c0000c` / `move.l #$ff800000` / 900x256 `dbra` nest, ending in `jmp (A0)`), but
  **zero static code references point at it**. It is reachable only through the
  `jmp (A0)` trampoline chain at `0x5072`/`0x507A`.
- **Evidence:** a scan of the whole 1 MiB image finds no branch or `jsr` to
  `0x548E`; the disassembly shows `0x50A0: bra $566a` and `0x50AC: bra $56e6`
  around it. `0x54C0: jmp (A0)`.
- **Confidence:** confirmed by measurement (full-image static scan)
- **Impact on us:** dead or version-variant code. **Do not convert it.** It cost us
  a frame-budget investigation: with IN0 bit 5 clear we executed this fill 14 times
  and MAME executed it zero times, which made the CPU look hung when it was merely
  4x behind on cycles.

### SR-07 — `0x300`-`0x348` disassembles as nonsense

- **Kind:** ROM data
- **Where:** `0x300`-`0x348`
- **Symptom:** linear disassembly of this range yields invalid/implausible
  instructions, which means it is data (or the entry alignment is wrong), not code.
- **Evidence:** every attempt at a coherent decode fails; the transcriber skipped
  the range rather than guess.
- **Confidence:** inferred
- **Impact on us:** do not attempt to convert it; keep it as a raw blob until
  something proves otherwise.

### SR-17 — The OKI sample table is not a permutation: sample `$01` is listed twice

- **Kind:** ROM data
- **Where:** `136070-2123.10c` @ `0x6532` (53 bytes, JSA-II 6502 sound ROM)
- **Symptom:** `$6532` reads as "the 53 sound effects, one per OKI sample", so the
  obvious assumption is that it is a permutation of the sample numbers the chip
  actually holds. It is not: there are 53 entries but only 52 distinct values.
  Sample `$01` appears at index 0 **and** index 1, so sound commands `$05` and
  `$38` play the identical sample. Nothing is missing — the table just costs one
  duplicate.
- **Evidence:** `0x6532` = `01 01 02 03 04 05 06 07 08 20 22 21 23 09 0A 0B
  0C 0D 0E 0F 10 ... 2B`. Decoded over the whole 53-byte range: 52 distinct
  values, `{0x01: 2 occurrences}`, 0 missing. Sound index 0 is the command
  `$05` with argument 0 and sound index 1 is command `$38` with argument 1; both
  emit OKI byte1 `$81` and byte2 `$11` / `$10` respectively.
- **Confidence:** confirmed by measurement (every byte decoded from the ROM and
  cross-checked against `roms/stunrun/136070-2124.1fh` headers).
- **Impact on us:** `jsa_sample_table[]` in
  `stun-runner/reproduction/sound/oki_cmdmap.c` is transcribed verbatim, duplicate
  included; do not "fix" it to a permutation. The duplicate is what makes
  SR-16's argument-as-sample-number quirk land on sound index 46 rather than on
  a round index 48.

## Hardware abuse

*(none recorded yet — this section exists so the distinction stays visible)*

## Curiosity

*(ROM quirks with no impact on the conversion — kept here so they are not
rediscovered from scratch)*

### SR-08 — The IRQ5 handler's entire net effect is a tick counter

- **Kind:** ROM code
- **Where:** `0x82C`-`0x8C0`
- **Symptom:** the periodic-interrupt service routine saves registers, does
  `addq.l #1,$ff9042.l`, samples the ADCs, acknowledges IRQ5 with `clr.w
  $60c000.l`, and returns — **without raising SR**. The ADC sampling is real work,
  but the counter is the only persistent effect.
- **Evidence:** full disassembly of the ISR and its `bsr $8c2` helper.
- **Confidence:** confirmed by disassembly
- **Impact on us:** it tells us the video interrupt is *not* a frame callback; the
  main loop's own vblank spin at `0x13EC` is what paces frames. Useful for
  justifying the frame-slice timing in the native port.

### SR-09 — `0x13EC` spins on the vblank edge rather than taking an interrupt

- **Kind:** ROM code
- **Where:** `0x13EC`
- **Symptom:** the main loop polls IN0 bit 2 (`/VSYNCB`) in a `btst`/`beq` pair,
  waiting for the line to go low and then high again — a software frame sync on an
  active-low vertical sync, not a hardware interrupt.
- **Evidence:** `clr.w $608000` / `btst #$2,$60c001.l` / `beq $13f2` /
  `btst #$2,$60c001.l` / `bne $13fc`.
- **Confidence:** confirmed by measurement — 71% of all retired instructions are
  this four-instruction spin, which is what forced us to slice the emulated frame
  by scanline rather than run it in one call.
- **Impact on us:** the conversion's timing model must expose a rastersynchronised
  frame boundary, not a fixed 1/60s timer.

### SR-14 — `0x44518` is a clone of the GSP bootstrap with its `A1` prologue removed

- **Kind:** ROM code
- **Where:** `136070-2102.200r` @ `0x44518`-`0x44550`
- **Symptom:** 56 bytes sitting in a data island that are the body of `0x530`'s
  bootstrap **without** its first instruction, `lea $c00002.l,A1`. So the clone
  writes through `A1` on the assumption that it already holds `$C00002` — an
  assumption nothing on any path establishes. It is not byte-identical to `0x530`
  and must never be described that way.
- **Evidence:** `0x44518 + $38 = 0x44550` and `0x536 + $38 = 0x56E`, and the 56
  bytes between those two ends are identical. The bytes around it disassemble as
  data: `0x44552: ori.b #$ff,D0`, `0x44556: move.l D0,D5`, then
  `0x44558: addx.b -(A1),-(A3)` and `dc.w $ff57` / `dc.w $9dff` / `ILI` /
  `dc.w $003f`. A branch-target scan for `bsr`, `bsr.b`, `bra`, `jsr` and `jmp`
  finds **zero** references to `0x44518`, against exactly one `bsr.w` for `0x530`
  (from `0x62C`, inside the init stub at `0x618`).
- **Confidence:** confirmed by disassembly
- **Impact on us:** none — nothing runs it, so there is nothing to transcribe. It
  is the reason §11.10 of `docs/mc68010-boot-findings.md` points at
  `stunrun_gsp_boot_latch_draw_parameters()` in `native/gsp_boot.c` instead of
  adding a second C function for `0x530`.

### SR-15 — `dbra D2,$c06` and `dbra D4,$c06` share a target on purpose (NOT a bug)

- **Kind:** ROM code
- **Where:** `0x0C06`-`0x0C1C` (the routine starts at `0x0BC4`, not `0x0BF4`)
- **Symptom:** the nested sparse fill has two `dbra` instructions branching to the
  *same* address `$C06`, which looks like the classic off-by-one-in-the-branch
  bug. It is the opposite: `$C06` **is** the innermost body, and reusing it as the
  row-loop entry is the normal 68k idiom for one body under two loops. The block
  `$C06`-`$C1C` runs 8 times (`D4 = 7`, `dbra` gives 8), each pass filling
  `2 * (columns + 1)` `$FFFF` words and then relatching `D0 + $400`. The result is
  8 rows strided `$400` apart — coherent, and what the game's sparse tiles need.
  Recorded here so nobody "fixes" it.
- **Evidence:** `0x0C02: move.w #$7,D4` / `0x0C06: move.w D3,(A1)` /
  `0x0C08: move.w D3,(A1)` / `0x0C0A: dbra D2,$c06` / `0x0C0E: addi.l #$400,D0` /
  `0x0C14: move.l D0,$c00002.l` / `0x0C1A: move.w D1,D2` / `0x0C1C: dbra D4,$c06`.
  Cross-checked against the only caller's data table at `0xB9A`: a count word
  `0x0004` plus five 8-byte records occupy `0xB9C`-`0xBC3`, i.e. exactly
  `5 * 8` bytes ending at `0xBC4`, the routine's first byte — which pins both the
  `dbra`-gives-N+1 count and the 8-byte stride.
- **Confidence:** confirmed by measurement (our transcription
  `stunrun_gsp_display_list_sparse_fill()` emits 50 address-latch pairs and 805
  FIFO words for the real `0xB9A` table, and `native/gsp_display_list_test.c`
  asserts both)
- **Impact on us:** none to correct — but the refutation must be preserved. The
  one genuine oddity here is cosmetic: `$0C1C`'s final pass latches a base one row
  past the last row it fills (`$0200C100` for the first record) and `$0C22` moves
  on without writing there. Observable on the port, dead on screen, and left
  as-is.

---

## MAME driver

Defects and gaps in MAME 0.289's `atari/harddriv.cpp` / `harddriv_m.cpp` /
`harddriv_v.cpp`. **These are not STUN Runner bugs** — they are bugs in the model
we are using as an oracle. Report them upstream; until then, compensate locally and
record the compensation.

### MD-01 — `m:snapshot()` produces nothing under `-autoboot_script`

- **Kind:** driver gap
- **Where:** Lua API surface, `machine.video` / `manager.machine`
- **Symptom:** `manager.machine.video:save_snapshot()` and `m:snapshot()` both
  return cleanly and write **zero files**, even with `-snapshot_directory` pointing
  at a writable path and even with a display attached. Both `pcall` wrappers report
  success.
- **Evidence:** runs with and without `-snapshot_directory`; the directory stays
  empty. Worked around by grabbing the X11 root window from outside the emulator
  (`import -window root`), which produced
  `stun-runner/docs/mame-frame600-attract.png`.
- **Confidence:** confirmed by measurement
- **Impact on us:** the oracle harness cannot self-capture frames. Any future
  pixel-level comparison against the original needs the external grab, or a patched
  driver.

### MD-02 — `emu.register_periodic` never fires its callback

- **Kind:** driver defect
- **Where:** Lua `emu` object
- **Symptom:** `emu.register_periodic(ms/1000.0, fn)` returns success under `pcall`
  but the callback is **never invoked**. `emu.register_frame_done` in the same
  script works normally.
- **Evidence:** isolated probe `/tmp/t1.lua` printed frame-callback lines and the
  `periodic register ok=true` line, and nothing else, across a full run.
- **Confidence:** confirmed by measurement
- **Impact on us:** sub-frame instrumentation is impossible from stock Lua. This is
  why the boot investigation had to use per-frame PC logs and a PC histogram rather
  than a cycle-accurate trace. **Do not retry this path**; it costs a round each time.

### MD-03 — The gdbstub debugger has no 68010 register description

- **Kind:** driver gap
- **Where:** `-debugger gdbstub`
- **Symptom:** `gdbstub: cpuname m68010 not found in gdb stub descriptions`,
  followed by a fatal error. No 68010 GDB register XML ships with MAME 0.289.
- **Evidence:** verbatim error from the debugger launch. Also tried and failed:
  `-debugger none` (disables the debugger so the debugscript never runs),
  `-debugger imgui` (requires the BGFX renderer), `-debugger qt` (needs a display).
- **Confidence:** confirmed by measurement
- **Impact on us:** no remote-debugging path for the 68010. Our own GDB RSP stub in
  `native/musashi_cpu.c` (env `STUNRUN_GDB_PORT`) exists for this reason and is
  still unwired and untested.

### MD-04 — GSP display registers are unreachable from Lua

- **Kind:** driver gap
- **Where:** `tms34010` device state
- **Symptom:** `DPYADR`, `DPYSTRT`, `DPYTAP`, `DPYCTL`, `HEBLNK`, `HSBLNK` and
  friends live in the GSP's private `m_IOregs` and are **not exposed**:
  `device.ior` is `nil` and `device.state` contains only 16 AREGs, 15 BREGs,
  `PC`, `GENPC`, `CURPC`, `SP`, `ST`, `CURFLAGS`.
- **Evidence:** `34010ops.h:25-26` (`#define IOREG(reg) (m_IOregs[reg])`) plus an
  exhaustive dump of `device.state` entries. Tried by name; all `nil`.
- **Confidence:** confirmed by measurement
- **Impact on us:** we cannot read the display window the GSP is scanning out, so
  `stun-runner/tools/gsp_render.py` has to **sweep** row-base × palette-bank and
  score candidates against a reference screenshot. Acceptable for the C++ port
  (we own the scan-out) but it makes pixel-perfect oracle comparison expensive.
## MAME driver (continued)

### MD-05 — Lua cannot read the board's `gsp_vram` share at all

`gsp.spaces["program"]:read_u8(0xff800000 + i)` returns **zero for every byte**
of the Atari board's 512 KiB `gsp_vram` share, at every frame sampled (600,
1450). Reads of the palette windows at `0xf5000000`/`0xf5800000` from the same
space *do* return data, so the space is live — the share is simply not mapped
into it. `m_gsp_vram` is a `shared_ptr_finder` (`src/emu/devfind.h:1183`) that
resolves against the *board*, not the chip.

Consequence: an oracle that dumps the frame via Lua silently produces an empty
picture while everything else looks healthy. Evidence: the Lua dump of the share
was uniformly `0x00`, while MAME's own dump of the same frame has 256 distinct
byte values and byte `0x01e000` = `27 27 … 26 26 …`, matching the `first10`
words printed by the rasteriser probe.

Workaround in use: MAME writes the share itself from `scanline_multisync()`,
which does hold both `m_gsp_vram` and the screen. `vram_base - m_gsp_vram.begin()`
gives the resolved word offset, and `m_gsp_vram.length()` gives the true size —
both should be preferred over re-deriving the shift by hand.

### MD-06 — Lua tables filled from index 0 silently truncate and shift a dump

`for i = 0, 0x7ff do t[i] = v end` produces a table whose `#t` is **2047**, not
2048: Lua's length operator returns the largest index `n` with `t[n] ~= nil`, and
`t[0]` is invisible to it. Writing `table.unpack(t, 1, #t)` then emits 2047
bytes and drops the first one.

Worse, `f:write(table.unpack(...))` in 4096-byte chunks produced a **4271-byte**
`gsp_pal_lo.bin` for a 2048-byte device — a 108% overrun. Every palette reading
taken from those dumps was wrong.

Fix: fill from index 1, and build each chunk with `string.char(table.unpack(...))`
rather than passing bare numbers. Then assert the written size against the device
size in the script, so a bad write path fails loudly instead of quietly.

### MD-07 — `sizeof()` on a `memory_share_creator` is not the share size

`harddriv.h:356-357` declares

```c
memory_share_creator<uint16_t> m_gsp_paletteram_lo;
memory_share_creator<uint16_t> m_gsp_paletteram_hi;
```

so `sizeof(m_gsp_paletteram_lo)` is **72 bytes** (the finder object), not the
2048 bytes of palette data. Dumping with `fwrite(m_gsp_paletteram_lo, 1,
sizeof(m_gsp_paletteram_lo), f)` writes 72 bytes and loses 97% of the palette
silently. Use `m_gsp_paletteram_lo.target()` (or the implicit cast to
`uint16_t *`) and size the write from `m_palette->entries() * sizeof(uint16_t)`.

### MD-08 — `scanline_multisync()` does not run headless, but `get_display_params()` does

With `-video none`, or `-video soft` under an X server with no window manager,
the scanline callback fires at most once per run. `get_display_params()` is
driven by the scanline *timer* and runs every line regardless, so display-register
dumps belong there. Two consequences:

* Anything added to `scanline_multisync()` (a VRAM dump, a `dest[]` sample) needs
  `-video soft -window` on a real display and is still unreliable — it may run
  once. Do not build a harness on it.
* Anything needing `m_cache`/`m_vram` cannot go in `get_display_params()` at all:
  those are members of the derived `tms34010_device` (`tms34010.h:991`), not of
  the base `tms340x0_device` whose method this is (`tms34010.h:112`, `:168`).

### MD-09 — Service mode "BAD GSP VRAM" in MAME

- **Kind:** driver defect **or** original ROM behavior — **unresolved**
- **Where:** MAME `stunrun` service mode, and/or ROM `136070-2102.200r` @$004F82
- **Symptom:** MAME's service mode (IN0 bit 5 held) displays "BAD GSP VRAM" text
  rendered by the GSP, indicating a GSP VRAM diagnostic failure. The MC68010
  diagnostic at `$004F82` fills `$FF945E` with eight longwords of `0x01010101`
  (confirmed by live Musashi trace, §13.1) then reads 16 words back via the GSP
  host port — all 16 words return `0xFFFF` instead of the expected non-zero
  values, causing the test to fail.
- **Evidence:**
  - GSP PC = `0x00000000`, ST = `0x00000010` throughout service mode frames
    60–130 (halted, confirmed by MAME debugger). The halt is set by the GSP boot
    ROM at `$0216`: `stop #$2700`.
  - GSP program space has a `noprw()` hole at `0x00000000–0x0000200f` in
    `multisync_gsp_map` (`harddriv.cpp:106`), confirming the self-test window.
  - `host_r()` in `tms34010.cpp:1931–1986` reads via
    `TMS34010_RDMEM_WORD(addr & 0xfffffff0)` using `m_program.read_word(A)`:
    it does **not** check the GSP halt state.
  - The GSP reset at `$003F8` (called from `$004F88` and `$005008`) writes
    `0xB800` to GSP HSTCTLH (halt line asserted) and the GSP halts.
  - MAME VRAM mapping: `map(0xff800000, 0xffbfffff).mirror(0x0400000).ram()`
    in both GSP and 68k program spaces, both sharing `"gsp_vram"`.
  - The MC68010 `movea.l #$FF945E, A1` instruction at `$004F96` has a
    non-canonical encoding (`0x2279 00FF945E`) that does not match standard
    `movea.l #<long>`.L encoding (`0x2079 <long>`). The MAME disassembler
    decodes it as `movea.l #$FF945E, A1`, but the byte pattern suggests
    `movea.w #$FF945E, A1` (word, sign-extended → `$FFFF945E`), which would
    write to a different address. The boot findings claim the fill correctly
    reaches `$FF945E`, implying either the encoding is a special longword form
    or the listing transcription is inaccurate.
- **Confidence:** partially confirmed by measurement (GSP halt state, host port
  path, VRAM mapping confirmed); MC68010 instruction encoding is inferred from
  byte pattern and contradicts the listing.
- **Impact on us:** if this is a MAME driver defect, the TMS34010 host port
  does not correctly relay MC68010 writes into GSP VRAM when the GSP is halted.
  The MC68010 diagnostic writes through the GSP host port, but VRAM at
  `$FF945E` reads back as `0xFFFF`, meaning the write never reaches the
  expected location. Possible fix: verify the GSP program space `host_w` path
  correctly dispatches to the shared VRAM share when the GSP is halted, and
  confirm the MC68010 `movea.l` encoding actually sets A1 to `$FF945E` (not
  `$FFFF945E`). If it is a ROM behavior, the GSP NAND tree test (which runs
  during the GSP reset sequence) is expected to fail in MAME because the GSP
  is halted before VRAM is populated.
- **Blocked by:** a live capture of the GSP VRAM diagnostic run at frames 1–59
  (before service mode is pressed), which would show whether the fill actually
  reaches `$FF945E` and whether the subsequent read returns `0x0101` or `0xFFFF`.
  Also blocked by verification of the actual bytes at `$004F96` in the ROM and
  a confirmatory live trace of the MC68010 writing to `$FF945E` during the
  diagnostic call from `$009B80`.
