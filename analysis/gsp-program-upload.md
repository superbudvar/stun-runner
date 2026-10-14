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
`internal_regs_map` covers `$C0000000`–`$C00001FF` — loading display parameters.
`LMO` is load-memory-offset, the circular/stride addressing the plotter uses, and
it is applied to the two `$400` strides immediately before storing them.

`00340160` writes to `$FFF6F5A0`, which folds through the mirror to share offset
`0x36F5A0` — just past the end of the code. That is a data table the program
points at, not an instruction.

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

**Nothing here has been named semantically.** The register writes at
`$C0000110`/`$C0000150`/`$C0000160` are decoded as addresses, not as `DPYSTRT`
and friends, because MAME's `REG_` enum names only 32 of the 128 entries the
register map exposes and the indexing was not pinned down. That is the next
piece of work, not a result.

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
