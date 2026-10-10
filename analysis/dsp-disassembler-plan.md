# DSP Disassembler and Sleigh Specification Plan

## Context

The S.T.U.N. Runner reverse-engineering project uses MAME as the behavioral
reference implementation. Two custom DSPs are active processors in the system,
but neither has Ghidra processor support:

- **ADSP-2100** — Analog Devices fixed-point DSP, geometry/math co-processor
- **TMS34010 GSP** — Texas Instruments graphics system processor, handles road
  rendering, HUD, and display timing

MAME's source contains complete, production-tested implementations of both
processors (BSD-3-Clause licensed). The disassembly logic is fully documented
in `src/devices/cpu/adsp2100/2100dasm.cpp` and `src/devices/cpu/tms34010/34010dsm.cpp`.

This document covers two parallel workstreams:

1. **Standalone disassemblers** — portable C tools that decode ADSP-2100 and
   TMS34010 instruction words using MAME's proven opcode tables, for use
   against the actual STUN Runner ROM images
2. **Ghidra Sleigh specs** — `.sinc` language definitions that teach Ghidra
   to disassemble and decompile both processors natively

---

## Workstream A — Standalone Disassemblers

### Goal

Produce `adsp-dis` and `gsp-dis` command-line tools that:

- read raw ADSP/GSP program ROM images
- decode every instruction word
- output annotated disassembly with opcode, registers, and effective addresses
- require no MAME runtime (pure C, no external dependencies)

### Source reference

| File in `/tmp/mame-src/src/devices/cpu/` | Lines | Purpose |
|---|---|---|
| `adsp2100/2100dasm.cpp` | 568 | Complete ADSP-2100 disassembler |
| `adsp2100/2100dasm.h` | 43 | Disassembler interface |
| `tms34010/34010dsm.cpp` | 1693 | Complete TMS34010 disassembler |
| `tms34010/34010dsm.h` | 52 | Disassembler interface |

### ADSP-2100 disassembler (`adsp-dis`)

**Input:** raw program ROM file (e.g. `prog-hi.t20`, `prog-lo0.s21`),
64 KB each, 16-bit word-addressed, big-endian 24-bit instruction encoding
stored in 32-bit word containers.

**Implementation:**
1. Copy `2100dasm.cpp` disassembly logic into `tools/adsp-dis/adsp-dis.c`
   (BSD-3-Clause; attribution required)
2. Add a ROM loader that maps 32-bit big-endian reads → 24-bit instruction words
3. Wrap with a `main()` that reads a filename and emits disassembly to stdout
4. Use the same `BIT()` macro pattern as MAME for field extraction
5. Map PC values to ROM word offsets for display

**Instruction encoding reference** (from `2100dasm.cpp`):

```
24-bit instruction word layout:
  bits[23:16] = opcode class (0x00–0xff)
  bits[15:0]  = immediate / register fields

Key opcode classes:
  0x00       NOP
  0x02       IDLE, FLAG_MOD
  0x03       CALL/JUMP on flag
  0x04       Stack control
  0x09       Address register modify
  0x0a       Conditional return
  0x0b       Indirect conditional jump/call
  0x0c       Mode control
  0x0d       Internal data move
  0x0e–0x11 Shift ops
  0x14–0x17 DO UNTIL loops
  0x18–0x1f Conditional JUMP/CALL (immediate addr)
  0x20–0x2f Conditional ALU/MAC
  0x30–0xbf Load immediates, ALU/MAC with memory
  0xc0–0xdf Data memory write immediate
  0xe0–0xff ALU/MAC with dual memory read
```

**Register set** (from `2100dasm.h`):
- Data registers: AX0, AX1, MX0, MX1, AY0, AY1, MY0, MY1, SI, SE, AR, MR0,
  MR1, MR2, SR0, SR1
- Address registers: I0–I7, M0–M7, L0–L7
- System registers: ASTAT, MSTAT, SSTAT, IMASK, ICNTL, CNTR, SB, PX, RX0,
  TX0, RX1, TX1, IFC, OWRCNTR

**Output format:**

```
$0000  NOP
$0004  CALL $0780 [IF EQ]
$0005  CALL $0834 [IF NE]
$0006  RTS
...
```

**Deliverable:** `tools/adsp-dis/` directory with `Makefile` or CMakeLists.txt,
self-contained C file, and a man page. Add as a CMake `add_executable` target.

### TMS34010 disassembler (`gsp-dis`)

**Input:** raw GSP program ROM images from `roms/extracted/*.3401`, 64 KB each,
16-bit opcode words + optional parameter words.

**Implementation:**
1. Copy `34010dsm.cpp` into `tools/gsp-dis/gsp-dis.c` (BSD-3-Clause)
2. Add a `main()` ROM loader
3. The TMS34010 has two register files (A and B), selected by bit 4 of the
   opcode word; the disassembler tracks this via the `rf` member
4. Handle 16-bit opcode + parameter word pairs

**Instruction encoding reference** (from `34010dsm.cpp`):

```
16-bit opcode word:
  bits[15:9] = major opcode
  bits[8:5]  = sub-opcode / source register
  bits[4]    = register file select (A=0, B=1)
  bits[3:0]  = destination register

Major opcodes (top 9 bits):
  0x000–0x01e  Control: REV, IDLE, NOP, CLRC, DINT, ABS, NEG, NOT, etc.
  0x040/0x060  SEXT, ZEXT, SETF, MOVE (memory)
  0x080        TRAP, CALL, RETI, RETS, MMTM/MMFM, MOVI
  0x0a0        ADDI, CMPI, ANDI, ORI, XORI, SUBI
  0x0c0        ADDXYI, CALLR, CALLA, EINT, DSJ
  0x0e0        PIXBLT (graphics blit operations)
  0x100/0x120  ADDK, INC
  0x140/0x160  SUBK, DEC
  0x180/0x1a0  MOVK
  0x1c0/0x1e0  BTST (immediate)
  0x200–0x380  Shift ops: SLA, SLL, SRA, SRL, RL
  0x400–0x6e0  ALU: ADD, SUB, CMP, AND, OR, XOR, DIVS, DIVU, MPYS, MPYU, etc.
  0x800–0xbc0  Memory MOVE variants (indexed, pre/post inc/dec, indirect)
  0xc00–0xc80  JR/JA (jump relative/absolute)
  0xe00–0xee0  ADDXY, SUBXY, CMPXY, CPW, CVXYL, MOVX, MOVY
  0xf00–0xfc0  PIXT (pixel transfer)
```

**Register set:** A0–A15 (register file A), B0–B15 (register file B),
SP (stack pointer, encoded as register 0xf)

**Output format:**

```
$0000  NOP
$0010  MOVE   A0,*$12345678,L
$0020  CALL   A1
...
```

**Deliverable:** `tools/gsp-dis/` directory with self-contained C file and
CMake target.

### Verification

Run both disassemblers against the known-constant regions in the ROM images:

- ADSP: the already-decoded init image (`adsp_init_image.c`) contains known
  instruction words at known PC values — verify the disassembler reproduces
  the same mnemonics
- GSP: the MAME driver sets a reset vector; use `flatpak run org.mamedev.MAME`
  with the `stunrun` driver to capture a GSP PC sample and verify disassembly
  against MAME's built-in debugger disassembler output

---

## Workstream B — Ghidra Sleigh Specifications

### Goal

Produce Ghidra processor module specifications (`.sinc` files) for ADSP-2100 and
TMS34010 so that Ghidra can:

1. Disassemble ROM images natively
2. Perform full decompilation to C
3. Resolve cross-references and build call graphs

### Ghidra processor structure reference

Each Ghidra processor needs these files:

```
Processors/<PROCNAME>/
  data/
    languages/
      <procname>.sinc        # Main Sleigh specification
      <procname>_commmon.sinc # Shared definitions (if needed)
    <procname>.ldefs         # Processor definition / register list
    <procname>.cspec         # Compiler spec
    <procname>.pspec         # Processor spec (endianness, align)
    <procname>.sla           # Compiled SLEIGH output
    <procname>.slaspec
  lib/
    <procname>.jar           # Processor support library
  Module.manifest
```

The MSP430 processor (`TI_MSP430/`) is the closest analogue: a 16-bit
fixed-point DSP with Harvard architecture. Reference it as the primary template.

### ADSP-2100 Sleigh spec

**Location:** `Ghidra/Processors/ADSP2100/`

**Key Sleigh constructs needed:**

1. **Instruction encoding** — the 24-bit word is the primary unit; the opcode
   is in bits[23:16]. Use Sleigh's `:` operator for field slicing:

   ```
   define token opcode(24)
     op_class = (23..16)   # 8-bit opcode class
     op_field = (15..0)    # 16-bit immediate/register fields
   ```

2. **Sub-tables** — the `switch` pattern on `op_class` dispatches to
   instruction-family sub-tables, matching MAME's `case` structure in
   `2100dasm.cpp`

3. **Register definitions** (from `2100dasm.h`):
   - Data registers: r0=AX0, r1=AX1, r2=MX0, r3=MX1, r4=AY0, r5=AY1,
     r6=MY0, r7=MY1, r8=SI, r9=SE, r10=AR, r11=MR0, r12=MR1, r13=MR2,
     r14=SR0, r15=SR1
   - Address registers: i0–i7, m0–m7, l0–l7
   - System/Control: ASTAT, MSTAT, SSTAT, IMASK, ICNTL, CNTR, PC, SP

4. **Pcode generation** — each Sleigh instruction pattern emits Ghidra pcode
   (Ghidra's intermediate representation). The ADSP has:
   - ALU operations (add, subtract, multiply, divide, logical)
   - MAC (multiply-accumulate) operations
   - Shift/normalize operations
   - Memory load/store (DM = data memory, PM = program memory)
   - Control flow (JUMP, CALL, RTS, RTI, DO UNTIL)
   - DAG operations (indirect addressing via I/M registers)

5. **Addressing modes** — the ADSP uses a two-stage DAG (Data Address
   Generator) for both data memory (DM) and program memory (PM) access:
   - Direct: `DM($xxxx)` or `PM($xxxx)`
   - Indirect: `DM(Ix,Mx)` or `PM(Ix,Mx)` with optional post-increment
   - Immediate: `#xxxx`

6. **Boot ROM** — the ADSP reset vector at PC=0x0004 must be modeled;
   this is the entry point Ghidra will use

### TMS34010 Sleigh spec

**Location:** `Ghidra/Processors/TMS34010/`

**Key Sleigh constructs needed:**

1. **Instruction encoding** — 16-bit opcode words, optionally followed by
   16-bit parameter words. The register file selector (A or B) is bit 4:

   ```
   define token opcode(16)
     major = (15..9)
     src   = (8..5)
     rf    = (4..4)    # 0=A file, 1=B file
     dst   = (3..0)
   ```

2. **Register definitions:**
   - Register file A: A0–A15
   - Register file B: B0–B15
   - PC (program counter), SP (stack pointer)
   - Various system registers visible in the GSP memory map

3. **Memory organization** — the TMS34010 is a graphics processor with:
   - Program memory (on-chip, loaded from ROM)
   - VRAM (video RAM for frame buffer)
   - Host interface registers (to/from 68010 main CPU)
   - GSP-specific control registers mapped at specific addresses

4. **Key instruction patterns to model:**
   - `PIXBLT` — pixel block transfer (graphics fill/blit)
   - `LINE` — line drawing
   - `MOVE` — register/memory moves with complex addressing
   - `CALLR/CALLA` — subroutine calls
   - `ADDK/SUBK/MOVK` — immediate arithmetic
   - `PIXT` — pixel transfer operations

5. **Reset vector** — MAME sets the initial PC; establish this as the
   entry point

### Sleigh development workflow

1. Create processor directory under `Ghidra/Processors/`
2. Write the `.sinc` file incrementally:
   - Define tokens and instruction patterns for one instruction family
   - Compile with `SleighCompiler`
   - Test disassembly against MAME output for the same PC
   - Add one family at a time
3. Define registers in `.ldefs`
4. Configure endianness in `.pspec`
5. Define calling conventions in `.cspec` for decompiler support
6. Build the processor module and install into Ghidra's `Processors/` directory
7. Load a STUN Runner ROM image and verify disassembly matches MAME's debugger

### Dependencies and risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Ghidra Sleigh is complex; full decompilation may take weeks | Medium | Ship disassembler-only v1; decompilation is additive |
| ADSP-2100 has unusual Harvard architecture that Ghidra may not model well | Medium | Start with the TMS34010 (more conventional) |
| MAME opcode tables may have undocumented edge cases | Low | Use MAME's own test suite to validate |
| Sleigh compiler errors are hard to debug | Medium | Build one instruction family at a time; test incrementally |

---

## Resource references

| Resource | Location |
|---|---|
| MAME ADSP-2100 source | `/tmp/mame-src/src/devices/cpu/adsp2100/` |
| MAME TMS34010 source | `/tmp/mame-src/src/devices/cpu/tms34010/` |
| Ghidra Sleigh documentation | Ghidra installation: `docs/languages/html/SleighDeveloperGuide.html` |
| MSP430 Sleigh reference | `/mnt/build/ghidraStuff/ghidra_12.1.3_PUBLIC/Ghidra/Processors/TI_MSP430/` |
| MAME license (BSD-3-Clause) | `https://github.com/mamedev/mame/blob/master/LICENSE` |

---

## Priority ordering

1. **Immediate** — `adsp-dis`: port MAME's `2100dasm.cpp` to a standalone
   tool and verify it against the known-decoded init image
2. **Immediate** — `gsp-dis`: port MAME's `34010dsm.cpp` similarly; verify
   against MAME debugger output for a GSP PC sample
3. **Short-term** — Sleigh for TMS34010: the GSP is more conventional and
   the graphics ops are self-contained; start here for decompilation
4. **Medium-term** — Sleigh for ADSP-2100: the Harvard architecture and
   DAG addressing are complex; build after GSP experience is gained

---

*Plan derived from MAME source at `/tmp/mame-src/` (cloned 2026-01-XX),
Ghidra 12.1.3 processor structure at `/mnt/build/ghidraStuff/ghidra_12.1.3_PUBLIC/Ghidra/Processors/`,
and existing project artifacts.*
