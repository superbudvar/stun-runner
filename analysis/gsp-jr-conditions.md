# The TMS34010 JR/JA condition table, complete

`analysis/gsp-program-upload.md:316-320` recorded a condition table that stopped
at `$600` and said so:

> All three reproduce the disassembler's printed targets exactly. Note
> `0xcb04`'s condition field is `0xb00`; the table above stops at `0x600` and
> the remaining codes were not read off `print_condition_code` before the end of
> this session — **the full condition list still needs transcribing**.

This file closes that gap. It answers the two questions the note left open,
separately, because they have different answers and different kinds of evidence:

- **(a) which condition codes this program uses** — measured, from the captured
  upload-boundary image;
- **(b) what each code means** — sourced to SPVU001A (S9), which was read for
  this work.

It also reports the two places where **MAME's disassembler and MAME's executor
disagree**, which is not the same as where they disagree about *conditions*: on
the conditions they agree completely, and the divergence is one level down, in
the *form* field of the same `switch`.

Everything is held by `tests/test_gsp_jr_conditions.py`, which needs neither a
ROM set nor a capture nor the manual online.

---

## Provenance

### MAME binaries

Three are in play (AGENTS.md trap 2 and the table added to it;
`stun-runner-w55` tracks the reconciliation). Both binaries named here were run,
and their sha256 are recorded because **a capture must never be compared across
binaries without saying which produced each side**.

| name | path | sha256 | mtime | used here for |
|---|---|---|---|---|
| `md12-fixed` | `/mnt/build/mame-ref/mame` | `55956f2b26c0d5caeb86f346e6247ded04428eae07465d35470dea77d6bf4045` | 2026-10-14 | **the capture this document decodes** |
| `stock-behaviour` | `/mnt/build/mame-oracle/mame` | `75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06` | 2026-10-09 | the original capture; identical image (below) |

The bead brief named the frozen oracle; AGENTS.md now makes `md12-fixed` the
default for new work, so **both were run**. They agree on the artifact that
matters here:

```console
$ sha256sum /mnt/build/mame-oracle/mame /mnt/build/mame-ref/mame
75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06  /mnt/build/mame-oracle/mame
55956f2b26c0d5caeb86f346e6247ded04428eae07465d35470dea77d6bf4045  /mnt/build/mame-ref/mame
```

```console
$ tools/capture-gsp-program-upload roms OUT --mame /mnt/build/mame-ref/mame
$ tools/capture-gsp-program-upload roms OUT --mame /mnt/build/mame-oracle/mame
# both, from result.json:
#   release  {"frame": 405, "seq": 631205, "pc": 180676, "hstctl_at_release": 30720}
#   frame 404 gsp_vram.bin  sha256 47e41d1b9d54055d965074ac2b6e9e0b844f024006e2555f6b8a17543d4a2715
#                                nonzero_words 81899, base 4286578688 (0xFF800000)
#   frame 405 gsp_vram.bin  sha256 51cc52c56e505ac76f27c05e3d4ee636e99df2b8ce7eb81109135f4f3e086dc8
#                                nonzero_words 83194
```

That reproduces findings **§85.1** exactly — "release at **frame 405**, `seq
631205`, HSTCTL `$7800`" and "**83,194 nonzero words** at frame 405". Note
`--frames`, not `--frame`: `tools/capture-gsp-program-upload` has no `--frame`
option, so §85.1's quoted command line is a shorthand, not something that
would run.

**The decoded image is the frame-404 one**: the GSP is still halted, the program
is intact, and nothing has run over it. The frame-600 share is the wrong corpus
(§85.1: only 113 real instructions by then).

### The decoder

`tools/disasm-gsp`, which compiles MAME's own `34010dsm.cpp`
(`/mnt/build/mame-ref/src/devices/cpu/tms34010/`) against a small shim, so the
mnemonics are MAME's. No MAME binary is involved in decoding; the binaries above
produced the bytes.

### Addresses

Two address spaces appear below and they are **not** interchangeable:

- **PC** — MAME's bit-addressed program counter. One instruction word advances it
  by 16 (`ROPCODE()` is `m_pc += 2 << 3`). This is the space
  `analysis/gsp-program-upload.md` prints and the space SPVU001A means by `PC`.
- **share byte** — offset into the 524,288-byte physical share
  (`m_gsp_vram`, 262,144 words). `share_byte = PC / 8`.

---

## (a) Which condition codes this program uses

**All sixteen.** In the frame-404 image, every one of `op[11:8]` = `$000`
… `$0f00` occurs at least once in a JR/JA encoding — 1,258 JR/JA-family
instructions in the whole share, of which 294 fall inside the program extent
`analysis/gsp-program-upload.md` records as PC `$340000`–`$35DC00`.

Counted by condition field:

| `op[11:8]` | disasm prints | ISA mnemonic | MAME executor predicate | SPVU001A meaning | S9 example flags | whole share | program extent | one example (PC) |
|---|---|---|---|---|---|---|---|---|
| `$000` | `JR` | UC | `1` | Unconditional; no conditions tested | `xxxx` | **61** | 59 | `$003410E0` |
| `$010` | `P` | P | `!N_FLAG() && !Z_FLAG()` | Positive | `0x0x` | **16** | 0 | `$002D2970` |
| `$020` | `LS` | LS | `C_FLAG() \|\| Z_FLAG()` | Lower or same | `xx1x x1xx` | **21** | 7 | `$00347B80` |
| `$030` | `HI` | HI | `!C_FLAG() && !Z_FLAG()` | Higher than | `x00x` | **60** | 5 | `$0034A550` |
| `$040` | `LT` | LT | `(N && !V) \|\| (!N && V)` | Less than (signed) | `0xx1 1xx0` | **30** | 7 | `$00340F90` |
| `$050` | GE | GE | `(N && V) \|\| (!N && !V)` | Greater or equal (signed) | `0xx0 1xx1` | **32** | 26 | `$00344030` |
| `$060` | LE | LE | `(N && !V) \|\| (!N && V) \|\| Z` | Less or equal (signed) | `0xx1 1xx0 xx1x` | **41** | 21 | `$0034D010` |
| `$070` | GT | GT | `(N && V && !Z) \|\| (!N && !V && !Z)` | Greater than (signed) | `0x00 1x01` | **24** | 11 | `$00348350` |
| `$080` | `C` | C | `C_FLAG()` | Carry / borrow set | `x1xx` | **112** | 10 | `$003464A0` |
| `$090` | NC | NC | `!C_FLAG()` | No carry / no borrow | `x0xx` | **116** | 16 | `$00345E60` |
| `$0a0` | EQ | EQ | `Z_FLAG()` | Equal / zero | `xx1x` | **174** | 56 | `$00341320` |
| `$0b0` | NE | NE | `!Z_FLAG()` | Not equal / nonzero | `xx0x` | **170** | 46 | `$003410A0` |
| `$0c0` | `V` | V | `V_FLAG()` | Overflow (also window clip) | `xxx1` | **180** | 6 | `$003454D0` |
| `$0d0` | NV | NV | `!V_FLAG()` | No overflow | `xxx0` | **98** | 7 | `$00345D30` |
| `$0e0` | `N` | N | `N_FLAG()` | Negative | `1xxx` | **60** | 11 | `$003435D0` |
| `$0f0` | NN | NN | `!N_FLAG()` | Nonnegative | `0xxx` | **63** | 6 | `$003434D0` |

The **executor predicates are quoted verbatim** from `34010ops.hxx`; the table
above abbreviates them to `N`, `C`, `Z`, `V` for width, and
`tests/test_gsp_jr_conditions.py` compares the unabbreviated text.

Two things in that table are worth stating plainly, because they are the two
places a reader is most likely to have been misled before:

- **`$000` prints nothing.** `print_condition_code` emits two spaces for the
  unconditional code, so the mnemonic is bare `JR` (or `JA`), and `UC` appears
  nowhere in the printed table — only in MAME's source comment
  `/* This is really UC (Unconditional) */`. `docs/open-questions-index.md` F8
  describes this function as "all sixteen entries, `0x0000` → `0x0f00`"; the
  sixteen entries are there, but the sixteenth has no name in the output.
- **`$010` is Positive and nothing else.** It is `!N && !Z` — *not* carry. This
  is the condition `docs/open-questions-index.md` correction 2 already settled,
  and it is now held by a test so it cannot be re-reported as a MAME bug.

### Rendered mnemonics: 18

Grouping by the two printed prefixes gives 18 distinct rendered names, and that
is where §85.1's "**all 18 JR condition variants appear**" lands:

| | whole share | | whole share | | whole share | | whole share |
|---|---|---|---|---|---|---|---|
| `JA` | 3 | `JR` | 58 | `JRGE` | 32 | `JRNV` | 98 |
| `JAHI` | 2 | `JRC` | 112 | `JRGT` | 24 | `JRP` | 16 |
| | | `JREQ` | 174 | `JRHI` | 58 | `JRV` | 180 |
| | | `JRLE` | 41 | `JRLS` | 21 | | |
| | | `JRLT` | 30 | `JRN` | 60 | | |
| | | `JRNC` | 116 | `JRNE` | 170 | | |
| | | `JRNN` | 63 | | | | |

1,258 total. `JA` and `JAHI` are the `JAcc` form — the one MAME's executor does
not implement, see below.

Totals: 1,258 whole share, **294 inside the program extent**. Inside the extent
the count is **17** distinct names and **15** of the 16 condition codes —
`JRP`/Positive (`$010`) occurs 16 times in the share and **not at all** between
`$340000` and `$35DC00`. `JA` and `JAHI` (the conditional absolute form) both
occur inside the extent, once and twice.

**Caveat that travels with every number above.** These are *linear-decoder*
counts. A linear disassembly of a data region produces plausible instructions
(`tools/disasm-gsp`'s own docstring demonstrates this on 9 of 18 words in a
1397-word window). So "the program uses condition `$080` ten times" means *the
decode of that address range contains ten such encodings*, not *the GSP
executed ten C-conditional branches*. Per findings **§85.5** the executor
settles what an encoding computes and cannot settle whether an address was
fetched; that is still `stun-runner-yuv`.

---

## (b) What each code means — SPVU001A (S9)

### Obtained, and how

`docs/sources.md` records S9 as **not held locally**. Two printings were fetched
to a temp directory, read, and **not committed**:

| printing | fetched from | sha256 | pages |
|---|---|---|---|
| **SPVU001A** | `https://vgamuseum.info/images/doc/texas_instruments/tms34010userguide_88.pdf` | `7a8ab8da7276da92a5eeb097de645dfc66be734740f76a0c50df68025cdd515f` | 667 |
| SPVU001 | `https://www.transputer.net/mtw/rg-750/doc/tms34010/t34010ug.pdf` and `https://theretroweb.com/chip/documentation/tms34010-users-guide-6600e59ecb47a698529836.pdf` (byte-identical) | `55e7f28a38bce1c050585abe682e464d8d612bc65bbe95440c92ce4ffdd6bef2` | 565 |

Both carry an OCR text layer, which was used for searching, and the four pages
the claims below rest on were **rendered as images and read directly**, so that
no claim here is an OCR artefact. Accessed 2026-10-17. Neither PDF is in this
repository and neither should be.

**Citation-location note.** findings §83, `analysis/gsp-program-upload.md` and
`the tools/disasm-gsp` docstring cite "SPVU001A Appendix A pp. A-13..A-16" for
the instruction-set summary, the `RS`/`RD` legend at "A-13" and `MOVE *Rs+,*Rd+,F`
as `1001 10FS SSSS DDDD` at "A-14". **Those page numbers are correct for
SPVU001A**: its Appendix A is the TMS34010 Data Sheet, and pages A-13..A-16 of
it carry the instruction-set summary chart, including `MOVE *Rs+,*Rd+,F` =
`1001 10FS SSSR DDDD` on A-14. In the earlier **SPVU001** printing the same
material is in **§12** — `Table 12-1. TMS34010 Instruction Set Symbol and
Abbreviation Definitions` p. 12-3 (`Rs` = "Source register", `Rd` =
"Destination register"), and `Table 12-7. TMS34010 Instruction Set Summary` pp.
12-14..12-20. Both were checked; neither reference is wrong, they are two
printings of one document.

### The encoding, from SPVU001A §12

All three forms, read off the page images:

| form | SPVU001A | printed page | MAME disassembler | MAME executor |
|---|---|---|---|---|
| `JAcc Address` | `1 1 0 0  code  1 0 0 0 0 0 0 0`, 3 words, `code` at `op[11:8]` | `12-92` | `(op & 0x00ff) == 0x80` → `JA` + `print_long_parm` | see "the JAcc gap" below |
| `JRcc Address`, ±127 words | `1 1 0 0  code  offset(8 bits)`, 1 word | `12-94` | default arm → `print_relative_8bit` | `j_<cc>_8` / `j_<cc>_x` |
| `JRcc Address`, ±32K words | `1 1 0 0  code  0 0 0 0 0 0 0 0` + a 16-bit offset word, 2 words | `12-96` | `(op & 0x00ff) == 0x00` → `print_relative` | `j_<cc>_0` |

SPVU001A p. 12-94 also settles a detail the disassembler's code could be read
either way on: "the assembler calculates the offset as `(Address - PC')/16` and
inserts the resulting **8-bit offset** into the opcode." So the short form's
displacement is bits `[7:0]` as a signed byte, which is what
`PARAM_REL8(O) = ((int8_t)(O))` (`tms34010.cpp:168`) already does. **No
divergence.**

### The Status Bits column, and the NCZV example column

Each of `JAcc`, `JRcc` ±127 and `JRcc` ±32K prints its own copy of the
condition table — six copies in total, and this is the useful redundancy: the
tables can be compared with each other.

Every table gives the predicate twice, in two independent forms:

- the **Status Bits** column, a boolean expression over `N`, `C`, `Z`, `V`
  (SPVU001A's legend: `+` logical OR, `·` logical AND, overbar logical NOT);
- the **Flags for Branch** example column, a set of four-character flag
  patterns headed `NCZV`.

The `NCZV` header reads **N, C, Z, V** left to right. That is not a guess: S9
**§5.2 Figure 5-4 and Table 5-2** (pp. 5-20/5-21) place `N` at bit 31, `C` at
bit 30, **`Z` at bit 29** and **`V` at bit 28**. Reading the header as N, C, V, Z
instead makes `JRZ` (`xx1x`) and `JALT` (`0xx1`, `1xx0`) disagree about what
the third character means, and the whole example table stops being
self-consistent. `tests/test_gsp_jr_conditions.py` holds that ordering.

Every condition was checked against **both** columns, by evaluating MAME's
predicate over all sixteen NCZV combinations and comparing with the manual's
patterns. **All sixteen agree**, in both printings, in every one of the six
copies of the table — with the exceptions in the next section, which are two
rows that contradict *SPVU001A itself* and which MAME follows the other way.

### Two rows in SPVU001A that contradict SPVU001 and SPVU001A itself

These are **printed defects in SPVU001A**, verified on the page images, and
**not** OCR artefacts. They are recorded because MAME disagrees with them and
someone will eventually notice.

| # | SPVU001A says | SPVU001A also says, elsewhere | SPVU001 says | MAME does |
|---|---|---|---|---|
| 1 | `JALO`/`JRLO` `(JAC)`/`(JRC)`, "Dst lower than Src", status `C`, code **`0001`** — pp. **12-92**, **12-94**, **12-96** (all three read from the page images) | `JAC`/`JRC`, "Carry set on result", status `C`, code **`1000`** — pp. **12-93**, **12-95**, **12-97**. And the table's own note: "A mnemonic code in parentheses is an alternate code for the preceding code", so the parenthesised `JRC` *must* share `JALO`'s code. | `JALO (JAC) … 1000` | `$080` → `C`, predicate `C_FLAG()` |
| 2 | `JAZ`/`JRZ` "Result = zero", status **`Z̄`** (barred), code **`0101`** — pp. **12-92**, **12-95**, **12-96**, "Compare to Zero" block | `JAEQ (JAZ)` "Dst = Src", status `Z`, code `1010` — same pages; and `JAZ` "Result is zero", status `Z`, code `1010` — pp. **12-93**, **12-95**, **12-97**, "General Arithmetic" block. `0101` is `JAGE`/`JRGE`'s code. | `JAZ … 1010` | `$0a0` → `EQ`, predicate `Z_FLAG()` |

The second is self-refuting on its face: the row that defines "Result = zero"
gives it a *barred* Z and the code for "greater or equal".

A third anomaly is in the **example** column rather than the code column and is
weaker evidence, because the same string appears in both printings:
`JAHS`/`JRHS` "higher or same as Src", status `C̄` (= no carry), is illustrated
with `x00x  xx1x` — which is *higher than*, or *equal* — rather than with
`x0xx` like its own alias `JANC`/`JRNC`. MAME's `!C_FLAG()` matches the status
bit, not the example. Not reported as a finding; recorded so the next reader does
not "discover" it.

**None of the three makes MAME wrong**, on the evidence available: for each,
MAME agrees with the other printing, with the consistent rows of the same
printing, and with the status-bit column. MAME's `34010tbl.hxx` and
`print_condition_code` treat `$080` and `$0a0` as carry and zero. What the real
silicon does is **`UNKNOWN`** — that needs the part, not the manual, and no
hardware is claimed here.

---

## Do MAME's disassembler and its executor disagree?

**On the conditions: no. Not one of the sixteen.** Both were read out of
`/mnt/build/mame-ref/src/devices/cpu/tms34010/` and compared:

- `34010dsm.cpp:132-153`, `print_condition_code`, switches on `op & 0x0f00` and
  prints sixteen entries, `$000` blank, in ISA order.
- `34010tbl.hxx:591-635`, rows `0xc000`..`0xcf00`, calls `j_<CC>_0` at column 0,
  `j_<CC>_8` at column 8 and `j_<CC>_x` at the other fourteen — so the row
  carries the condition and the column carries the form.
- `34010ops.hxx:1643-1834`, forty-eight functions `j_<CC>_{0,8,x}`, each a
  one-line `j_xx_<form>(<predicate>)`. For each of the sixteen conditions the
  three forms carry **the same predicate text**.

That is a real check rather than a copy: the disassembler's name, the opcode
table's row binding, and the executor's predicate are three separate artefacts,
and `tests/test_gsp_jr_conditions.py` fails if any of them is perturbed.

**On the form field: yes, there is one divergence, and it is in the executor.**

`j_xx_8`'s absolute arm is unreachable. The macro
(`34010ops.hxx:1577-1603`), abridged only in the cycle counts:

```c
#define j_xx_8(TAKE)                                            \
{                                                               \
    if (DSTREG(op))                                             \
    {                                                           \
        if (TAKE)                                               \
        {                                                       \
            m_pc += (PARAM_REL8(op) << 4);                      \
            COUNT_CYCLES(2);                                    \
        }                                                       \
        else                                                    \
            COUNT_CYCLES(1);                                    \
    }                                                           \
    else                                                        \
    {                                                           \
        if (TAKE)                                               \
        {                                                       \
            m_pc = PARAM_LONG_NO_INC();                         \
            CORRECT_ODD_PC("J_XX_8");                           \
            COUNT_CYCLES(3);                                    \
        }                                                       \
        else                                                    \
        {                                                       \
            SKIP_LONG();                                        \
            COUNT_CYCLES(4);                                    \
        }                                                       \
    }                                                           \
}
```

`DSTREG(O) = ((O) & 0x0f)` (`tms34010.cpp:163`), and `s_opcode_table` is indexed
`[op >> 4]` (`tms34010.cpp:1201`, `:1206`), so the *column* is `op & 0x0f`. Every
opcode dispatched to `j_xx_8` therefore has `op & 0x0f == 8`, which makes
`DSTREG(op)` **always true** in that function. The `else` arm — `m_pc =
PARAM_LONG_NO_INC()` and `SKIP_LONG()`, the only code in MAME that implements a
32-bit absolute jump target — can never run.

The `JAcc` encoding is exactly `op & 0x00ff == 0x80` (SPVU001A p. 12-92, read
from the page image above), so **`JAcc` is executed by MAME as a ±127 relative
branch with displacement `(int8_t)0x80 == -128`**: `m_pc += (-128 << 4)`, i.e.
128 words backwards, instead of to the 32-bit address in the two extension
words. The disassembler prints the same opcode as `JA FFF48FC0h`.

How this survived: `JAcc` costs three words where `JRcc` costs one, and `JUMP Rs`
is the register-indirect long jump, so an assembler emits `JAcc` only when the
target is known at assembly time and out of ±127 words — rare.

**What this does and does not establish.** It is a reading of the pinned source,
not a measurement: no run was made, and per findings §85.5 a source reading
settles what an encoding *computes*, never whether an address was ever *fetched*.
The dense sweep of the frame-404 image contains **five** `JAcc` encodings
(`$c080` ×3, `$c380` ×2), of which **three** — `JA FFF48FC0h` at PC `$344D60`,
`JAHI FFF40000h` at `$34A730` and `$34A7C0` — lie inside the documented program
extent. **Whether any of them was ever fetched is `UNKNOWN`.** No `BUGS.md`
entry has been written: this lane does not own that file, and a MAME-driver
entry gets reported upstream, where a false one costs more than silence.

Two adjacent things were checked and are **not** divergences:

- the two-word relative form. `op & 0x0f == 0` → `j_xx_0`, whose `DSTREG(op)` is
  always false, so it always takes the `else` arm —
  `m_pc += (PARAM_WORD_NO_INC()<<4)+0x10` when taken, `SKIP_WORD()` when not.
  That reads the extension word's displacement, matching SPVU001A p. 12-96 and
  matching the disassembler's `case 0x00`.
- the one-word form's displacement width. SPVU001A p. 12-94 says 8 bits; MAME
  uses `(int8_t)op`. Agreed.

---

## One number in §85.1 does not survive: the decode stride

Reproduced exactly: §85.1's `7,411` real instructions, `85` distinct
mnemonics, `24,182` `DW`, `0` unknown. All four come out of

```console
$ tools/disasm-gsp /tmp/.../gsp-vram-upload-last/gsp_vram.bin --at 0 --count 40000 --json
# 31,593 decoded slots, 85 distinct mnemonics, 24,182 DW, 0 unknown,
# 7,411 non-DW, 118 JR/JA-family instructions, 17 distinct rendered names
```

But that sweep reads the share with **16 bytes between instruction words**, and
the physical share it was given does not have that layout. The 524,288-byte
`m_gsp_vram` holds **262,144 words at two bytes each**, and `ROPCODE()` advancing
`m_pc` by `2 << 3` = 16 bits is exactly two bytes. The uploaded program sits at
**share bytes `0x68000`–`0x6B580`**, and reads cleanly at a two-byte stride:

```console
$ tools/disasm-gsp <same capture, words re-spaced at 16 bytes> --at 0 --count 24
  00000000: 00050050  SETF   10h,0,0
  00000010: 00070040  SETF   20h,0,1
  00000020: 000900c0  MOVI   0h,A0
  00000040: 00050080  MOVE   A0,@C0000110h,0
  00000070: 00180040  MOVK   2h,A0
  00000080: 00050080  MOVE   A0,@C0000150h,0
  ...
  00000260: 00050080  MOVE   A0,@C0000140h,0
```

That is `analysis/gsp-program-upload.md`'s entry listing, line for line,
including `@C0000110h`, `MOVI FFFCh,A0`, `@FFF6F5A0h`, `LMO A0,A0` and
`@C0000130h`. Two further independent anchors land on their marks:

```console
# harddriv.cpp:5092 hardcodes m_gsp_speedup_pc = 0xfff41070
PC 0x341070   000b0060  CMPI FFFFh,A0
PC 0x3410a0   00cb0004  JRNE 410F0h
# the three JR examples the note pins:
PC 0x340f90   00c400fb  JRLT 40F50h     (doc: JRLT 340F50h)
PC 0x3410a0   00cb0004  JRNE 410F0h     (doc: JRNE 3410F0h)
PC 0x3410e0   00c0000b  JR 411A0h       (doc: JR 3411A0h)
```

With the same bytes decoded at two bytes per word, the whole share gives
**253,231 slots, 98 distinct mnemonics, 192,655 `DW`, 60,576 non-`DW`, 1,258
JR/JA instructions, 18 distinct rendered JR/JA names, all 16 condition fields** —
and the program extent gives 294 JR/JA instructions over 17 rendered names and
15 condition codes.

So §85.1's *qualitative* conclusion ("all 18 JR condition variants appear") is
reproduced, and in fact only by the two-byte decode: the tool's 16-byte sweep
yields **17** distinct rendered names, one fewer. §85.1's *quantitative* figures
(7,411 / 85 / 24,182) belong to the 16-byte sweep and should be read as counts
over every eighth word.

`tools/disasm-gsp`'s `WORD_STRIDE = 16` is the thing to look at, and its
docstring's "one instruction word occupies one 16-byte cell, replicated eight
times" is the claim that does not hold of the 512 KiB capture. It plausibly did
hold of the 4 MiB window image the tool was originally written against
(`.mirror(0x0400000)` over a 512 KiB store replicates every byte 8×, which turns
a 2-byte cell into a 16-byte one); the tool was later changed to require the
512 KiB physical share and the stride was not changed with it.

**Not filed, and not fixed.** `tools/disasm-gsp`, `analysis/gsp-program-upload.md`,
`tools/gsp-program-map`, `docs/mc68010-boot-findings.md` and `BUGS.md` are all
outside this lane's write scope, and the executed-vs-decoded classification that
would say which of these 1,258 encodings are code is a concurrent agent's.
Recorded here so the parent can route it. **`tests/test_disasm_gsp.py` pins the
16-byte model**, so a fix has to change that file too.

---

## What is `UNKNOWN`

- Whether any of the five `JAcc` encodings in the frame-404 image is ever
  fetched, and therefore whether MAME's missing absolute arm affects any
  measurement in `reference/`. Source reading only; no run was made.
- What the real TMS34010 silicon does for condition `$080` and `$0a0`, given that
  SPVU001A prints contradictory codes for them (§ above). MAME, SPVU001 and the
  consistent rows of SPVU001A all agree; silicon is not in evidence.
- Whether `JRP`/Positive (`$010`) appears in code as opposed to data. It occurs
  16 times in the share, zero times inside the documented program extent.
- Which of the two remaining binaries' readers was used to write
  `analysis/gsp-program-upload.md`'s disassembly listings. Both current binaries
  produce the same uploaded image here, but that says nothing about the reading
  under a third binary — `stun-runner-w55` is open and this document does not
  settle it.

## Cited by, not read

- findings **§83**'s `Appendix A A-13`/`A-14` page numbers: confirmed to be
  SPVU001A's Appendix A (the Data Sheet) in the printing fetched, with the
  `MOVE *Rs+,*Rd+,F` encoding on A-14. The page images for A-13/A-14 were not
  read individually; the chart was located through the OCR text stream and the
  surrounding pages.
- **§8.7 Table 8-6** (illegal opcodes) and **§8.8** (level-0 vector fetch): not
  read for this bead. `tests/test_disasm_gsp.py` already holds Table 8-6.
- The full Appendix A instruction-set summary: not read line by line. Only the
  `MOVE *Rs+,*Rd+,F` row, in passing, was checked — and it agrees with §83.

---

## Reproducing

```console
# the capture
tools/capture-gsp-program-upload roms OUT --mame /mnt/build/mame-ref/mame

# the decode this document counts (two-byte words re-spaced for the tool)
python3 - <<'PY'
import struct
src = open('OUT/gsp-vram-upload-last/gsp_vram.bin','rb').read()
for c in range(len(src)//2 // 32768):
    out = bytearray(0x80000)
    for i in range(32768):
        struct.pack_into('<H', out, 16*i,
                         struct.unpack_from('<H', src, 2*(c*32768+i))[0])
    open('chunk%03d.bin' % c,'wb').write(bytes(out))
PY
for f in chunk*.bin; do tools/disasm-gsp "$f" --at 0 --count 40000 --json; done

# the checks
python3 -m unittest tests.test_gsp_jr_conditions -v
```

The re-spacing is information-preserving: each 16-bit word of the share appears
once, padded. It exists only because `tools/disasm-gsp` reads its offset as a
byte offset while `WORD_STRIDE` advances the cursor by eight words; see "the
decode stride" above. **It is a workaround for a bug in the tool, not a
correction to the data.**
---

## Independent re-measurement, 2026-10-17 (parent session)

Every claim above was re-derived from source and from a fresh capture rather
than accepted. Results: **the condition table is confirmed; two of the numeric
claims did not reproduce and are corrected here; and the defect is larger than
this document states.**

### The opcode set — `op & 0xf000 == 0xc000`, all 16 condition codes

This document does not state the mask it counted over, and the obvious guesses
are wrong. `34010dsm.cpp:1451-1458` lists `case 0xc000, 0xc200, 0xc400, 0xc600,
0xc800, 0xca00, 0xcc00, 0xce00` — **eight even condition codes only**, which is
easy to mistake for the whole family and undercounts by more than a third. The
executor table gives the real set: every row from `// 0xc000` to `// 0xcf0` in
`34010tbl.hxx` dispatches to a `j_*` handler, i.e. `op & 0xf000 == 0xc000` and
**all 16 condition codes**.

Worth noting for anyone reporting upstream: MAME's *disassembler* case list
covers only the even codes at this site. Whether odd-code jumps are decoded
elsewhere was not established and is **UNKNOWN**.

### Counts: two of this document's numbers do not reproduce

Re-captured the pristine upload-boundary image (release frame 405, so
`gsp-vram-upload-last` is frame 404 — the program before the GSP's first
output), at `md12-fixed`, sha256 `55956f2b…`, and scanned every word with
`op & 0xf000 == 0xc000`:

| form | this document | measured | status |
|---|---|---|---|
| distinct JA/JR names, whole share | 18 | **18** | confirmed |
| `op & 0xff == 0x80` (`JAcc`), whole share | 5 | **5** | confirmed |
| JR/JA-family words, whole share | 1,258 | **1,506** | **corrected** |
| of the `0x80` form, inside the documented extent | 3 | **unverifiable** | **UNKNOWN** |

The 1,258 vs 1,506 gap is **not an error**, it is two different questions, and
this document compares them. 1,506 is every word *matching the opcode pattern*;
1,258 is what `tools/disasm-gsp` actually *decoded*. The 248-word difference is
data that looks like jumps plus words the decoder will not name — pattern
matching over an image overcounts. This is the **same distinction as §85.1's
cell-versus-word views**, and it is now the second time in this file's history
that a count has been compared across two different denominators.

The "3 inside the extent" cannot be checked here. The documented extent is
stated in GSP addresses (`$340000`–`$35DC00`), and converting those to share word
offsets needs the address-to-offset fold, which is **unresolved** and is
`stun-runner-47h`. Measured directly as word offsets, the JR/JA-bearing region
spans `0x285c4`..`0x360bc`; asserting which of the 5 sit "inside" needs the fold,
so the claim stays **UNKNOWN** rather than being carried forward.

### The defect is larger than "the `JAcc` gap"

This document frames the divergence as `JAcc` (the `op & 0xff == 0x80` form) not
being implemented. That is the *rarer* half. Reading both files together:

| `op & 0x00ff` | ISA (SPVU001A) | MAME disassembler | MAME executor |
|---|---|---|---|
| `0x00` | ±32K relative | `print_relative` | **absolute — wrong** |
| `0x80` | absolute | `print_long_parm` / `JA` | **±127 relative — wrong** |
| other | ±127 relative | `print_relative_8bit` | relative — agrees |

The executor tests `DSTREG(op)` = `op & 0x0f`, so it takes the absolute arm
**only when the low nibble is zero** — which the ISA assigns to the *relative*
form. The two special cases are **swapped with respect to each other and to the
ISA**, and MAME's own disassembler has it right.

Pattern matches over the frame-404 image: **`0x00` → 161**, `0x80` → 5, other →
1,340. So the more numerous divergent form is the one this document does not
discuss. Filed as `BUGS.md` **MD-14**.

Also recorded there, because it is easy to misread as the bug: the `_0`/`_8`/`_x`
split is **not** a missing implementation. `34010tbl.hxx` binds `j_UC_0` at
column 0, `j_UC_8` at column 8 and `j_UC_x` at the other 14, so the dispatch has
already fixed `op & 0x0f`; one arm of each macro is dead by construction. The
defect is the `DSTREG` test, not the specialisation.
