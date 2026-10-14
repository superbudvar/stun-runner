# The GSP program

The 68010 uploads a TMS34010 program into the GSP's VRAM before releasing it to
run. This is that program, decoded. Read with `tools/disasm-gsp`.

## What language it is

TMS34010 native machine code. Instruction words are 16 bits, each carrying two
10-bit operand fields; there is a separate graphics instruction set (PIXBLT,
VBLT, PLINE, FILL…) for the blitter. This is not assembly text and not a
high-level language. **What Atari wrote it in is not known and is not guessed at
here** — no cross-compiler has been identified, and "assembly" is an assumption
about the source language, not an observation about the artifact.

`stunrun` has **no GSP ROM region**. The ROM definitions are `maincpu` (68010),
`jsa:cpu`, `user1` (ADSP), `jsa:oki1`, `200e`, `210e`. VRAM is the only memory
the GSP can execute from, so everything below came out of the share the host
wrote.

## Where it is

| | |
|---|---|
| Share offsets | `0x340000` – `0x35DC00` |
| VRAM addresses | `$FFB40000` – `$FFB59DC0`, equivalently `$FFF40000` – `$FFF59DC0` by the 4 MiB mirror |
| Size | 6,595 instruction words (~13 KB) |
| Distinct PCs actually executed | 656 |
| Distinct opcodes executed | 309 |

Established from the `STUNRUN-FETCH` trace: 400,000 fetches, control flow in both
directions (+48, +16, −64, −48, −16 units), and 100% of traced opcodes present in
the share image.

## The address space, which will mislead you

`m_pc` on this part is **bit**-addressed. `ROPCODE()` advances it by `2 << 3`,
and MAME's own `34010dsm.cpp` advances its read position by 16 units per word.
Those 16 units are **bytes in the capture**: one instruction word occupies one
16-byte cell, replicated eight times, so a fetch at any alignment inside the cell
returns the same word.

```text
share offset = pc & 0x3fffff
```

This was derived empirically, from 179,601 fetch/opcode pairs whose opcode was
unique in the share: in every case `pc & 0x3fffff` landed exactly on the cell
holding that fetch's opcode. It is not a guess from the ISA manual.

The consequence is that **the share's uniform 16-byte cells are the expected
shape, not evidence that the data is not code.** See the correction in
`gsp-program-upload-boundary.md`; that inference was made here and refuted.

## The program

Program entry, share `0x340000`. It is display setup:

```text
00340000: 00050050  SETF   10h,0,0
00340010: 00070040  SETF   20h,0,1
00340020: 000900c0  MOVI   0h,A0
00340040: 00050080  MOVE   A0,@C0000110h,0
00340070: 00180040  MOVK   2h,A0
00340080: 00050080  MOVE   A0,@C0000150h,0
003400b0: 000900c0  MOVI   0h,A0
003400d0: 00050080  MOVE   A0,@C0000160h,0
00340100: 000900e0  MOVI   FFFCh,A0
00340130: 00050080  MOVE   A0,@C0000090h,0
00340160: 00050080  MOVE   A0,@FFF6F5A0h,0
00340190: 000900d8  MOVI   0h,B8
003401b0: 000900d1  MOVI   400h,B1
003401d0: 000900d3  MOVI   400h,B3
003401f0: 004e0030  MOVE   B1,A0
00340200: 006a0000  LMO    A0,A0
00340210: 00050080  MOVE   A0,@C0000130h,0
00340240: 004e0070  MOVE   B3,A0
00340250: 006a0000  LMO    A0,A0
00340260: 00050080  MOVE   A0,@C0000140h,0
```

`SETF` pairs at the top clear the interrupt status bits before anything else.
The `MOVI`/`MOVE A0,@C00001xx` pairs write the GSP's internal register file —
`internal_regs_map` covers `$C0000000`–`$C00001FF`. `LMO` is load-memory-offset,
the circular/stride addressing the plotter uses, and it is applied to the two
`$400` strides immediately before storing them.

**Those register writes do not go where they appear to.** `m_IOregs` holds 64
entries and `IOREG(reg)` is `m_IOregs[reg]` with no mask, so `@C0000110` is
offset 272 into a 64-element array. This is recorded as **MD-12** in `BUGS.md`.
It is also why nothing here is named semantically: it is not merely that MAME's
`REG_` enum names only 32 of the exposed addresses, it is that most of the
addresses the program actually writes are out of bounds in the first place. The
stores are labelled as raw addresses until that is settled.

`00340160` writes to `$FFF6F5A0`, which folds through the mirror to share offset
`0x36F5A0` — just past the end of the code. That is a data table the program
points at, not an instruction.

## The decode is checked against execution

A linear disassembly is only worth something if it agrees with what the processor
actually did. Cross-checking every executed address against the fetch trace:

```text
executed PCs: 656
sampled 120 executed PCs: 109 length-consistent, 11 not
all 11 exceptions: CALLA / JR / CALL A0 / RETS
```

The 11 are control-transfer instructions, where the next fetch is the branch
target rather than `pc + length`. `CALLA $FFF46E30` is followed by a fetch at
share offset `0x346E30` — the same address after the 4 MiB mirror fold. So every
sampled decode is consistent with execution, and the mirror fold is confirmed on
branch targets as well as on code.

Entry is genuinely executed: all 28 PCs in the first 1 KB were fetched, and the
image word at each matches the traced opcode exactly.

## The hottest loop is a handshake

`0x340F50` was fetched 78,994 times in 400,000 — the single hottest address:

```text
00340f50: 000700e0  MOVE   @FFF716A0h,A0
00340f80: 00480020  CMP    A1,A0
00340f90: 00c400fb  JRLT   340F50h
00340fa0: 00560000  CLR    A0
00340fb0: 000500e0  MOVB   A0,@FFF716A0h
00340fe0: 00050080  MOVE   A0,@FFF716C0h,0
00341010: 00050050  SETF   10h,0,0
00341020: 00070040  SETF   20h,0,1
00341030: 000900e3  MOVI   FFF9FC00h,A3
00341060: 00840060  MOVE   *A3,A0,0
00341070: 000b0060  CMPI   FFFFh,A0
003410a0: 00cb0004  JRNE   3410F0h
```

**`MOVI FFF9FC00h,A3` is the driver's own speed-up address.** In
`harddriv.cpp`:

```c
m_gsp_speedup_addr[0] = &m_gsp_vram[(0xfff9fc00 - 0xff800000) >> 4]; // Addresses are in bits. Really.
```

The GSP polls it (`MOVE *A3,A0` / `CMPI FFFF,A0` / `JRNE`) until the host writes
`$FFFF`. That is a textbook ready-handshake, and it independently confirms the
bit-addressed reading: the driver computes the cell index with `>> 4`, which is
16 bytes — the cell size this document derives from the other direction.

## Two known limits

**MAME's disassembler has no mnemonic for the repeat instructions.** `RS`, `RS1`,
`RS2`, `RI` and `RPT` decode as `MOVE`. A power-on delay loop therefore does not
look like one. This is a MAME disassembler gap, not a property of the data — it
is why an earlier reading of `$0x9920` as a reset vector was unsound.

**Nothing here has been named semantically.** The register writes are decoded as
raw addresses, not as `DPYSTRT` and friends. See MD-12 above for why: most of
the addresses the program writes are outside the array MAME backs them with, so
naming them would assert a mapping that has not been established.

## Reproducing

```sh
tools/disasm-gsp <image> --at 0x340000 --count 32
tools/disasm-gsp <image> --at 0x340f50 --count 12 --json
```

The tool compiles MAME's own `34010dsm.cpp` against a ~60-line shim on first use,
so the decoding is MAME's and not a reimplementation that could disagree with it.
It refuses an image that is not the whole 4 MiB share: a 512 KiB capture is a
different region, not a smaller view of it, and decoding one silently would place
every address wrong.

## Cross-check against prior art (2026-10-14)

Asked whether anyone outside this repository had already documented this, the
answer turned out to be yes, in two places, and both are worth recording — one
because it corroborates the decode, and one because it does not.

### The hardware designer's own account

Jed Margolin designed the board. His account states that the TMS34010 "also used
the VRAM memory for its program", that VRAM was built from 64Kx4 parts paired
into banks, and that the memory organisation was switched dynamically because
"Programs used one memory space, polygon creation used another, and Graphic
Display used a third."

That independently confirms the central finding here: the GSP program lives in
VRAM, alongside the data it draws, with no separate program memory. It also
explains, from the hardware side, why the share is so heavily replicated —
the part was abused for memory granularity, and the 16-byte cell structure we
measured is a consequence of that, not an artifact of the capture.

### MAME hardcodes a spin PC, and ours lands on it

`harddriv.cpp:5092` sets, for S.T.U.N. Runner:

```c
m_gsp_speedup_pc = 0xfff41070;
m_gsp_speedup_addr[0] = &m_gsp_vram[(0xfff9fc00 - 0xffc00000) >> 4];
```

Our disassembler, built independently from MAME's decoder, decodes share offset
`0x341070` — which is `$FFF41070` after the 4 MiB mirror fold — as:

```text
00341030: 000900e3  MOVI   FFF9FC00h,A3
00341060: 00840060  MOVE   *A3,A0,0
00341070: 000b0060  CMPI   FFFFh,A0
003410a0: 00cb0004  JRNE   3410F0h
```

`MOVI $FFF9FC00,A3` loads exactly the address in `m_gsp_speedup_addr[0]`, and
`CMPI $FFFF,A0` compares against exactly the `0xffff` that
`hdgsp_speedup_r` tests for. The PC matches to the instruction. That
corroborates the address mapping, the 16-byte cell size, the decode, and the
identification of the handshake, in one go.

### But the GSP does not spin there

This is where the cross-check stops paying off. Fetch counts over the 400,000
traced instructions:

```text
0xfff40f50   78994   MOVE @FFF716A0h,A0  / CMP A1,A0 / JRLT 340F50h
0xfff40f80   78994
0xfff40f90   78994
0xfff41060       44   MOVE  *A3,A0,0
0xfff41070       44   CMPI  FFFFh,A0
0xfff410a0       44   JRNE  3410F0h
```

MAME's `m_gsp_speedup_pc` points at an instruction the GSP passes through **44
times**, while the loop it actually spends its time in is at `0xFFF40F50` —
`CMP A1,A0 / JRLT`, waiting for `*$FFF716A0` to reach `A1`, a different handshake.

The consequences are bounded, and this is stated rather than overstated:

- **Performance, not correctness.** `hdgsp_speedup_r` only calls
  `spin_until_interrupt()` when `m_gsp->pc() == m_gsp_speedup_pc`. With the GSP
  at `0xFFF40F50`, that test fails, so MAME simulates the spin loop properly.
  Slower than it could be, and correct.
- **Whether MAME's watched address is right is UNKNOWN.** `m_gsp_speedup_addr` is
  a `uint16_t*`, and `(0xfff9fc00 - 0xffc00000) >> 4` uses `0xffc00000`, which is
  the *driver* board's base, while S.T.U.N. Runner is on *multisync*, mapped at
  `0xff800000`. But `>> 4` into a `uint16_t*` is not obviously a byte offset, and
  the mirrored 4 MiB share changes the arithmetic again. `init_harddriv` uses
  `0xff800000` where `init_harddrivc` uses `0xffc00000`, so the constants are not
  consistent across sibling boards either. **No claim of a defect is made here.**
  Establishing it needs the actual share index arithmetic worked out from
  MAME's region setup, not from reading the expression.

## Pinned encodings for the RTL work (2026-10-14)

The gaps blocking `tools/check-gsp-replay --boot` are now specified, taken from
`34010dsm.cpp` and **verified against this program** — each rule below reproduces
the exact targets the disassembler printed, by hand.

### JR family — missing from `tms34010_decode.sv`

The RTL has only `JUMP Rs` (`bits[15:5] == 0x00B`) and the `DSJ` family. The
conditional-jump family is absent, and it is in the hot loop.

Encoding, from `case 0xc000 … case 0xce00` at `34010dsm.cpp:1451`:

| field | bits | meaning |
|---|---|---|
| family | `[15:9]` = `0x60`–`0x67` | JR. Equivalently `op & 0xfe00` ∈ {`0xc000`, `0xc200`, …, `0xce00`} |
| condition | `[11:8]` | `0x000`=UC, `0x100`=P, `0x200`=LS, `0x300`=HI, `0x400`=LT, `0x500`=GE, `0x600`=LE, … |
| form select | `[7:0]` | `0x00` = two-word relative; `0x80` = `JA` absolute; anything else = one-word relative |

Target computation:

```c
/* bits[7:0] == 0x00 : two words. w is the following word, signed 16-bit. */
target = pc + 32 + (w << 4);

/* bits[7:0] == 0x80 : JA, a 32-bit absolute parameter. */
target = long_param;

/* otherwise : one word. The offset is the low 8 bits of the opcode, signed. */
target = pc + 16 + ((int8_t)op << 4);
```

`pc + 16` / `pc + 32` skip past the current one- or two-word instruction, and the
`<< 4` is the 16-byte cell this program's words live in — the same unit as
`share offset = pc & 0x3fffff`.

Verification against three instructions this program actually executes:

```text
0x340f90  0xc4fb  bits[7:0]=0xfb (-5)  cond LT  -> 0x340f90+0x10+(-5<<4) = 0x340f50  ✓ JRLT
0x3410a0  0xcb04  bits[7:0]=0x04 (+4)  cond NE  -> 0x3410a0+0x10+( 4<<4) = 0x3410f0  ✓ JRNE
0x3410e0  0xc00b  bits[7:0]=0x0b(+11)  cond UC  -> 0x3410e0+0x10+(11<<4) = 0x3411a0  ✓ JR
```

All three reproduce the disassembler's printed targets exactly. Note `0xcb04`'s
condition field is `0xb00`; the table above stops at `0x600` and the remaining
codes were not read off `print_condition_code` before the end of this session —
**the full condition list still needs transcribing**, though `LT`, `NE` and `UC`
are confirmed by these three.

### Repeat instructions — encoding not yet pinned

MAME's **disassembler** has no mnemonic for `RS`/`RS1`/`RS2`/`RI`/`RPT`, which is
recorded as MD-13. The *executor* was not reached for these before the end of
this session, so the encoding and semantics are **not** pinned here. Do not
implement from the disassembler's `MOVE *,*` rendering — that rendering is
MAME's fallback for an unhandled encoding, not a statement about the data.

Two routes to the answer, both cheap:

1. `execute_op()` in `tms34010.cpp` — the authoritative semantics, in the same
   file the fetch trace already comes from.
2. SPVU001A, which `tms34010_decode.sv` already cites for its other encodings.

### Scope of the remaining work

Running this program on our own core is gated on exactly two things:

1. the JR family above, which is now specified to the point of three verified
   examples; and
2. the repeat instructions, which are not yet pinned.

Nothing else in the decode is missing for the opcodes this program has executed
so far. `CLR` is *not* a gap — MAME emits it as `XOR` with source and destination
equal, and the RTL has `XOR`.

Once both are in, the run is `tools/check-gsp-replay --boot <stream> --vram-out
<vram>`, which already feeds the recorded host writes into the Verilated core and
dumps the resulting VRAM. The two open questions from above still stand and are
not blocked by this: whether the GSP *display* pipeline is modelled well enough to
compare a frame rather than only VRAM writes.
