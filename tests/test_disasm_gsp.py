"""`tools/disasm-gsp` decodes the GSP program; these tests keep it honest.

The tool's failure modes are all of the kind AGENTS.md calls the worst: they
render something. A stride error is eight-fold wrong and still prints clean
mnemonics. A `DW` line means MAME has no decode, not that the region is data.
A linear disassembly of a table prints confident nonsense. So none of these
tests assert that the tool runs, and none assert that a plausible string is
present; they assert the decode of bytes whose correct answer is known from
somewhere other than the decoder.

Expectations come from three independent places, never from the tool:

  * SPVU001A, the TMS34010 User's Guide -- its Appendix A instruction-set
    summary (pp. A-13..A-16) is the ISA's own table of encodings, and section
    8.7 Table 8-6 is the ISA's own list of illegal opcodes. Where a test
    asserts a mnemonic it is asserting what that chart says the bits mean.
  * `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp` and
    `src/devices/cpu/tms34010/34010dsm.cpp` -- `init_stunrun()` hardcodes
    `m_gsp_speedup_pc = 0xfff41070` and indexes the share with
    `&m_gsp_vram[(0xfff9fc00 - 0xffc00000) >> 4]`; `r16()` advances its read
    position by 16 and `print_relative_8bit()` computes `pc + 16 + (ls << 4)`.
    Those are constants and formulas in the emulator's own source, written
    without reference to this tool, and the tests read them out of the files
    rather than hardcoding them so they cannot silently rot.

WHAT BROKE, AND WHY THIS FILE IS SPLIT IN TWO (findings 87.5)
-------------------------------------------------------------

`tools/disasm-gsp` used to read one word in eight and every count it printed
described a ninth of the program. Two earlier explanations were wrong:

  * "`WORD_STRIDE = 16` is wrong" -- it is not. MAME's TMS34010 has
    `addrshift = 3`, so one BYTE is 8 address units and one WORD is 16 units.
    `WORD_STRIDE` is units-per-word and is used only to render `[N word(s)]`.
  * "the shim's cursor needs `off += len / 8`" -- that fixes stepping and
    breaks operand fetching, because MAME's disassembler reads an instruction's
    operand words at 16-byte pitch: it is written for a 4 MiB window image with
    one word per 16-byte cell. Planted proof, reproducible with
    `tools/disasm-gsp` itself: three dense words `0580 0110 c000` at share
    byte offsets 0/2/4 render as

        00000000: 00050080  MOVE   A0,@C0000110h,0   [3 word(s)]

    but under `off += len/8` the same three words render
    `MOVE A0,@0h,0` -- the stepping is right and the operands are still wrong.

The real cause is narrower than either: the tool's input contract silently
changed. It was written for a 4 MiB image with one word per 16-byte cell, and
findings 84 changed the accepted size to the 512 KiB DENSE physical share
(262,144 words at 2 bytes each) without changing the cursor. So the fix is to
expand the dense share into the cell layout INTERNALLY, keep `off += len`, and
accept and report offsets in share BYTES at the boundary.

Because that fixes the tool and not the arithmetic behind it, the tests here
are split, and the split is the point:

  * **Decoding / ISA tests** use the transcribed words at DENSE, arbitrary,
    self-consistent offsets. They say what MAME's decoder does with a word.
    They make no claim about where the word lives.
  * **Addressing tests** assert the GSP-bit-address -> share-offset fold
    directly against MAME's own constants, with no fixture at all.

A test that lives in both halves is the failure this file is trying not to
repeat. Under the old model `tests/test_disasm_gsp.py` derived its probe
geometry from the same constant the tool did, so every assertion was
self-satisfying: `test_speedup_pc_decodes_as_the_compare_mame_hardcodes`
"passed" because the fixture had been laid out at the number that fold
produced. Four tests have now passed for the wrong reason in this repository
over its history; this file must not add a fifth.

NOTE ON THE TRANSCRIPTION, and on what is deliberately NOT claimed
------------------------------------------------------------------

`PROGRAM` below holds 85 instruction words transcribed from a
`tools/mame-gsp-vram-dump` capture of a 4 MiB image at frame 450
(`sha256 95d538ad...`). That image is a WINDOW image, not the share: findings
84 established the share is 512 KiB dense and findings 87's sweep showed a
2-byte sweep of the real capture returns exactly the `nonzero_words` its own
`result.json` records. The transcription was therefore taken one word per
16-byte cell of an image that does not exist, at labels that were never folded
into share offsets.

So: the WORDS are real words MAME's decoder really renders (these tests check
that), and the LABELS are transcription labels with no physical meaning (these
tests do not pretend otherwise). They are laid down densely and densely
compared; no test here asserts that `PROGRAM`'s labels are share addresses, and
the addressing tests compute every offset from the fold instead. Claiming the
labels were physical would be a new shared-wrong-model failure wearing a
different hat.

NOTE on the ground truth the brief lists: `$0002C1C4` / `$0002C1CC` are the
**68010** PCs at which the maincpu writes `$7800` to HSTCTL to release the
halted GSP, not GSP PCs (analysis/gsp-program-upload-boundary.md, findings
section 36.2). Nothing here depends on them, and nothing in this file calls
them GSP addresses.

NOTE on the withdrawn claim this file also pins: BUGS.md MD-13 asserted that
MAME mis-decodes repeat instructions as `MOVE`. SPVU001A's instruction-set
summary has no repeat instruction, and assigns `1001 10FS SSSS DDDD` to
`MOVE *Rs+,*Rd+,F`, which is exactly what MAME prints.
`test_no_word_decodes_to_a_repeat_mnemonic` and
`test_0x9920_is_the_move_the_isa_documents` hold both halves of that, so the
false claim cannot come back quietly.
"""

from __future__ import annotations

import json
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOL = REPO / "tools" / "disasm-gsp"

# ---------------------------------------------------------------------------
# The physical share and the window that maps onto it
# ---------------------------------------------------------------------------

# harddriv.cpp:628 -> multisync_gsp_map:
#     map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram")
# That is a 4 MiB address WINDOW with a mirror mask, over a 512 KiB physical
# share. MAME's direct m_gsp_vram dump reports 524288 bytes / 262144 words;
# Margolin (S3) counts 8 banks of 64Kx8 on the MultiSync board; SP-338 (S6) says
# "VRAM Array (16) 64K x 4".
SHARE_BASE = 0xFF800000
SHARE_BYTES = 0x80000
SHARE_WORDS = SHARE_BYTES // 2
GSP_WINDOW_BYTES = 0x400000

# The TMS34010 is `addrshift = 3` (tms34010.h): one ADDRESS UNIT is one BIT, so
# 8 units make a byte. These two functions are the ONLY place in this file that
# converts a GSP bit address to a place in the share.
GSP_ADDRSHIFT = 3


def share_byte_for_gsp_address(pc: int) -> int:
    """GSP bit address -> byte offset in the 512 KiB share.

    `>> 3` is the addrshift. `& (SHARE_BYTES - 1)` is the mirror: the window is
    eight times the share, so the top three bits of the byte offset alias.
    """
    return ((pc - SHARE_BASE) >> GSP_ADDRSHIFT) & (SHARE_BYTES - 1)


def share_word_for_gsp_address(pc: int) -> int:
    """GSP bit address -> WORD index in the share.

    A word is 16 address units (`>> 4`), and `m_gsp_vram` is a
    `optional_shared_ptr<uint16_t>` (harddriv.h:353), so MAME indexes it in
    words. `test_word_fold_is_mames_own_index_arithmetic` checks this function
    against the expression MAME itself writes in `init_stunrun()`; nothing else
    in this file is allowed to assume the fold.
    """
    return ((pc - SHARE_BASE) >> 4) & (SHARE_WORDS - 1)


# MAME's TMS34010 disassembler addresses ONE WORD PER 16-BYTE CELL -- it is
# written for the window image -- so it steps and fetches operands 16 units
# apart. `WORD_STRIDE` is UNITS PER WORD, not a byte pitch in the share.
# `tools/disasm-gsp` expands the dense share into that cell layout internally
# and keeps this number only to render `[N word(s)]` and to report
# `word_stride` in `--json`.
WORD_STRIDE = 16

# A word in the DENSE share is two bytes, and they are adjacent. This is the
# physical layout the tool's input is, and the layout every offset here is in.
WORD_BYTES = 2

MAME_SRC = pathlib.Path("/mnt/build/mame-ref")
HARDRIV = MAME_SRC / "src/mame/atari/harddriv.cpp"
DSM = MAME_SRC / "src/devices/cpu/tms34010/34010dsm.cpp"

# SPVU001A section 8.7, Table 8-6, "Illegal Opcodes Ranges" (p. 8-9).
ILLEGAL_RANGES = (
    (0x0200, 0x02FF), (0x0400, 0x04FF), (0x0800, 0x08FF),
    (0x0A00, 0x0AFF), (0x0C00, 0x0CFF), (0x0E00, 0x0EFF),
    (0x3400, 0x37FF), (0x7000, 0x7FFF), (0x9E00, 0x9FFF),
    (0xBE00, 0xBFFF), (0xD800, 0xDEFF), (0xFE00, 0xFFFF),
)

# 85 instruction words, transcribed as contiguous runs. Format:
#     name -> (label, "wwwwww..." little-endian words)
# The label is a TRANSCRIPTION LABEL, not a share offset -- see the module
# docstring. Nothing below asserts that it is one.
PROGRAM = {
    # Display setup. The three-word MOVE at label+8 carries @C0000110h, the
    # first of the seven out-of-bounds GSP register writes BUGS.md MD-12 is
    # about; its two operand words are the address, composed from the two words
    # after the opcode.
    "ENTRY": (0x40000, "0550074009c0000005800110c000184005800150c00009c0000005800160c000"
                      "09e0fffc000005800090c0000580f5a0fff609d8000009d1040009d304004e306a"
                      "0005800130c0004e706a000580"),
    # The hottest address in the 400,000-fetch trace (78,994 fetches) and the
    # ready-handshake that contains MAME's hardcoded speed-up PC. Words 15..19
    # are `MOVI FFF9FC00h,A3` / `MOVE *A3,A0,0` / `CMPI FFFFh,A0` -- see
    # `test_speedup_handshake_is_three_adjacent_instructions`.
    "HOTLOOP": (0x40f40, "fff707e016a0fff74820c4fb560005e016a0fff7058016c0fff7055007400"
                        "9e3fc00fff984600b600000ffffcb0409e5fc10fff9c00b09e3"),
    # GSP PCs sampled elsewhere in this project, each with the words that
    # follow so multi-word instructions get real operands.
    "S_342e50": (0x42e40, "04000fc009af"),
    "S_3414c0": (0x414b0, "fff707a01670"),
    "S_345330": (0x45320, "848005800000"),
    "S_3479e0": (0x479d0, "fff505990000"),
    "S_359a70": (0x59a60, "100b498bc8fb"),
    "S_359dd0": (0x59dc0, "0962098f0054"),
}

# The 18 distinct words in the window the STUNRUN first-output boundary defines
# (share bytes 446118..448912, findings section 36.3). Measured: nine of them
# decode as instructions. That is the point -- a linear disassembly of a data
# table renders instructions, so this file does not claim the tool can tell.
FILL_WORDS = {
    0x3535: "DW", 0x354B: "DW", 0x3565: "DW", 0x3595: "DW",
    0x4A4A: "BTST", 0x4A7B: "BTST", 0x4B4B: "BTST", 0x4B7B: "BTST",
    0x6565: "SRA",
    0x7B36: "DW", 0x7B4A: "DW", 0x7B4B: "DW", 0x7B7B: "DW", 0x7B95: "DW",
    0x957B: "MOVE", 0x9595: "MOVE", 0x95B1: "MOVE",
    0xB1B1: "MOVE",
}

# Post-release GSP PCs sampled elsewhere in this project, and the address span
# `$02FE68`/`$02FE6E` covered just before the release
# (analysis/gsp-program-upload-boundary.md, sections "The uploaded range is the
# one the GSP then executes" and "The upload is sparse").
SAMPLED_POST_RELEASE_PCS = (0xFFF59A70, 0xFFF59DD0, 0xFFF42E50, 0xFFF40F90,
                            0xFFF41060, 0xFFF414C0, 0xFFF45330, 0xFFF479E0)
UPLOAD_SPAN = (0xFFF40000, 0xFFFFFFE0)


def share(words_at: dict[int, int]) -> bytes:
    """A 512 KiB dense share image with `word` little-endian at each byte offset.

    An offset that is not word-aligned, or that does not fit, raises rather than
    being written: bytearray slice assignment past the end *appends*, which
    would silently produce an oversized image and then fail the tool's size
    guard for the wrong reason.
    """
    buf = bytearray(SHARE_BYTES)
    for offset, word in words_at.items():
        if offset % WORD_BYTES:
            raise ValueError(f"offset 0x{offset:x} is not word-aligned in a dense share")
        if not 0 <= offset <= SHARE_BYTES - WORD_BYTES:
            raise ValueError(
                f"word at 0x{offset:x} does not fit a {SHARE_BYTES}-byte share")
        buf[offset:offset + WORD_BYTES] = struct.pack("<H", word)
    return bytes(buf)


def program_words() -> dict[int, int]:
    """The transcribed words laid down DENSELY, at their transcription labels.

    Word i of a blob sits at `label + WORD_BYTES * i`. Dense is the physical
    layout of the share (findings 87.5); the labels themselves are not share
    offsets and nothing asserts that they are.
    """
    words: dict[int, int] = {}
    for label, blob in PROGRAM.values():
        for index in range(len(blob) // 4):
            words[label + WORD_BYTES * index] = int(blob[index * 4:index * 4 + 4], 16)
    return words


def run(*args: str, image: pathlib.Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), str(image), *args], capture_output=True,
                          text=True, check=False, timeout=900)


def decode(image: pathlib.Path, start: int, count: int) -> dict[int, dict]:
    """Decode `count` instructions from `start`; key the result by share offset.

    `start` and the keys are SHARE BYTE offsets, which is what `--at` takes and
    what `offset` reports. A branch target inside `operands` is different: MAME
    renders it in its own 16-byte CELL address space and the shim passes that
    through verbatim, so `cell_target()` below converts.
    """
    proc = run("--at", hex(start), "--count", str(count), "--json", image=image)
    if proc.returncode != 0:
        raise AssertionError(f"disasm-gsp failed: {proc.stderr.strip()}")
    return {i["offset"]: i for i in json.loads(proc.stdout)["instructions"]}


def jr_relative_displacement(word: int) -> int:
    """The signed low byte of a one-word relative JRcc, in WORDS.

    `34010dsm.cpp:112` `print_relative_8bit`:
    `util::stream_format(stream, "%Xh", pc + 16 + (ls << 4))` with
    `int8_t ls = (int8_t)op`. `pc` is the position the driver was handed, which
    for this shim is a CELL address, i.e. `word_index * WORD_STRIDE`. Every
    term is a multiple of 16, so the rendered target collapses to
    `(word_index + 1 + ls) * WORD_STRIDE` -- one word past the opcode, plus
    the signed low byte of the opcode.
    """
    displacement = word & 0xFF
    if displacement >= 0x80:
        displacement -= 0x100
    return displacement


class DisasmGspCase(unittest.TestCase):
    """Shared 512 KiB image holding the transcribed words, densely packed."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._dir = tempfile.TemporaryDirectory(prefix="disasm-gsp-test-")
        cls.image = pathlib.Path(cls._dir.name) / "gsp_vram.bin"
        cls.words = program_words()
        cls.image.write_bytes(share(cls.words))

    @classmethod
    def tearDownClass(cls) -> None:
        cls._dir.cleanup()

    def scratch(self, name: str) -> pathlib.Path:
        return pathlib.Path(self._dir.name) / name

    def text(self, offset: int, image: pathlib.Path | None = None) -> str:
        ins = decode(self.image if image is None else image, offset, 1)[offset]
        return f"{ins['mnemonic']} {ins['operands']}".strip()


# Where a batch of candidate words starts in the share. The pitch between
# candidates is set by `decode_word_list`.
PROBE_BASE = 0x10000


def decode_word_list(words: list[int], image: pathlib.Path) -> list[dict]:
    """Decode a batch of candidate words in one linear run, in the order given.

    A linear run visits every word, so a candidate can only be read at its own
    offset if the word before it cannot swallow it. The longest instruction in
    this ISA is five words (`MOVB @Saddress,@Daddress`, SPVU001A A-14), so each
    candidate gets a six-word pitch with four `DW` filler words behind it: a
    candidate of any length lands, and the run's own cursor is back on a word
    boundary before the next one. The filler is `$FFFF`, which is illegal per
    Table 8-6 and therefore decodes to exactly one word.

    The pitch is in SHARE BYTES (two per word), because the share is dense.
    Using MAME's 16-byte cell pitch here would leave seven zero words between
    candidates, which decode as instructions and swallow the candidate -- an
    error this helper reports by name rather than hiding.

    Every candidate offset must appear in the output. If the pitch were too
    small, or a candidate turned out to be six words, the candidate would be
    reported as swallowed rather than silently missing.
    """
    pitch = 6 * WORD_BYTES
    cells: dict[int, int] = {}
    offsets = []
    for index, word in enumerate(words):
        offset = PROBE_BASE + pitch * index
        offsets.append(offset)
        cells[offset] = word
        for filler in range(1, 5):
            cells[offset + WORD_BYTES * filler] = 0xFFFF
    path = image.parent / (image.stem + "-probe.bin")
    path.write_bytes(share(cells))
    got = decode(path, PROBE_BASE, 10 * len(words) + 16)
    missing = [offset for offset in offsets if offset not in got]
    if missing:
        raise AssertionError(
            f"{len(missing)} of {len(words)} candidate words were not decoded, "
            f"first at {missing[0]:#x}; a candidate was swallowed by a "
            f"neighbouring instruction")
    return [got[offset] for offset in offsets]


def harddriv_init_stunrun() -> str:
    """The body of `init_stunrun()` from the pinned MAME tree.

    Brace-matched rather than line-numbered so the constants cannot be read
    out of a neighbouring board's init by accident -- `m_gsp_speedup_pc` is set
    four times in this file, and only one of them is stunrun's.
    """
    text = HARDRIV.read_text(encoding="utf-8", errors="replace")
    start = text.index("void harddriv_state::init_stunrun()")
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError("unterminated init_stunrun() in harddriv.cpp")


def mame_share_word_index(expression: str) -> int:
    """Evaluate MAME's own `&m_gsp_vram[...]` index expression.

    stunrun's `init_stunrun()` writes

        m_gsp_speedup_addr[0] = &m_gsp_vram[(0xfff9fc00 - 0xffc00000) >> 4];

    and `m_gsp_vram` is a `optional_shared_ptr<uint16_t>` (harddriv.h:353), so
    that bracket expression IS a word index into the share. This returns the
    value MAME computes, from MAME's own constants and MAME's own shape of
    expression -- it is the oracle this file's fold is measured against.

    If MAME changes the shape of the expression, this RAISES rather than
    guessing: a silently-skipped check is the failure mode this file exists to
    prevent.
    """
    match = re.search(
        r"&m_gsp_vram\[\s*\(\s*(0x[0-9a-fA-F]+)\s*-\s*(0x[0-9a-fA-F]+)\s*\)"
        r"\s*>>\s*(\d+)\s*\]", expression)
    if match is None:
        raise AssertionError(
            "m_gsp_speedup_addr[] is no longer indexed by "
            "`&m_gsp_vram[(<addr> - <base>) >> <shift>]`; this file's fold needs "
            "re-deriving against the new expression rather than guessing:\n"
            + expression.strip())
    addr, base, shift = match.group(1), match.group(2), int(match.group(3))
    return (int(addr, 16) - int(base, 16)) >> shift


# ===========================================================================
# 1. DECODING / ISA. Transcribed words at dense, arbitrary, self-consistent
#    offsets. No claim is made about where those words live.
# ===========================================================================

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"),
                     "no C++ compiler to build MAME's disassembler with")
class DecodingTests(DisasmGspCase):

    def test_entry_sequence_matches_the_documented_display_setup(self):
        """The first instructions are the SETF/MOVI/MOVE display setup.

        SPVU001A A-15: `SETF FS,FE,F  0000 01F1 01 FS SSSS`. A-14:
        `MOVE Rd,@Daddress,F  0000 01F1 100R SSSS` (3 words) and
        `MOVE Rs,Rd  0100 11MS SSSR DDDD`; A-15: `LMO Rs,Rd  0110 101S SSSR
        DDDD`; A-14: `MOVB Rs,@Daddress  0000 0101 111R SSSS`.

        The mnemonic sequence is what makes this a decoding test rather than a
        transcription test: it only comes out in that order if each
        instruction consumed exactly the number of words SPVU001A says it does,
        which on a dense share is a step of 2 bytes per word and nowhere else.
        """
        got = decode(self.image, 0x40000, 20)
        self.assertEqual(
            [got[offset]["mnemonic"] for offset in sorted(got)],
            ["SETF", "SETF", "MOVI", "MOVE", "MOVK", "MOVE", "MOVI", "MOVE",
             "MOVI", "MOVE", "MOVE", "MOVI", "MOVI", "MOVI", "MOVE", "LMO",
             "MOVE", "MOVE", "LMO", "MOVE"])

        # Label+8 is the first of MD-12's seven register writes past the end of
        # MAME's 64-entry m_IOregs. The address is the two words after the
        # opcode, and MAME reads them 16 bytes apart -- which, on a dense
        # share, is why the tool expands the share into cells internally. If
        # that expansion were dropped, this operand renders `@0h` and fails.
        ins = got[0x40008]
        self.assertEqual(ins["mnemonic"], "MOVE")
        self.assertIn("@C0000110h", ins["operands"], ins["operands"])

    def test_hot_loop_branch_target_is_hand_derived(self):
        """Assert the target arithmetic, not the string MAME printed.

        MAME renders a one-word relative JRcc as `pc + 16 + (ls << 4)` in
        CELL addresses (`34010dsm.cpp:112`). Recomputed here from the bits and
        from the 16-units-per-word pitch, and compared both to what the tool
        printed and to where the word actually is in the dense share.
        """
        offset = 0x40f4A          # the `$C4FB` JRLT in the transcribed hot loop
        word = self.words[offset]
        self.assertEqual(word & 0xFE00, 0xC400, "not the JRcc family")
        self.assertNotIn(word & 0x00FF, (0x00, 0x80),
                         "this is a long/absolute form, not the one-word relative "
                         "form this test derives")
        # (word_index + 1 + ls) * WORD_STRIDE, and in the share that is
        # 2 * (word_index + 1 + ls) bytes.
        expect_cell = (offset // WORD_BYTES + 1 + jr_relative_displacement(word)) * WORD_STRIDE
        expect_byte = 2 * (offset // WORD_BYTES + 1 + jr_relative_displacement(word))

        ins = decode(self.image, offset, 1)[offset]
        self.assertEqual(ins["mnemonic"], "JRLT")
        self.assertEqual(int(ins["operands"].rstrip("h"), 16), expect_cell,
                         "the rendered branch target is not the one MAME's own "
                         "formula gives for these bits")
        # And the target really is the loop head: the word there must be the
        # `$07E0` the transcription puts three words earlier.
        self.assertEqual(self.words[expect_byte], 0x07E0,
                         "the computed target is not the hottest address")

    def test_opcode_field_is_two_byte_padded_fields_not_the_word(self):
        """Pin the `opcode` field's formatting, which downstream tools read.

        The driver prints byte[1] then byte[0], each with `setw(4)` and a zero
        fill, so the string is two 4-digit byte fields concatenated -- the high
        byte first. Read as one number that is NOT the instruction word:
        `int("00050050", 16)` is 0x50050, while the word is 0x0550. Getting this
        wrong makes every downstream tool decode a different instruction from
        the same number, silently.

        An earlier draft of this file got the relationship backwards and
        asserted the field *was* the word; the mutation and alignment controls
        are what caught it.
        """
        for offset in (0x40000, 0x40008, 0x40008 + 6, 0x40f4A):
            word = self.words[offset]
            ins = decode(self.image, offset, 1)[offset]
            self.assertEqual(ins["opcode"], f"{word >> 8:04x}{word & 0xFF:04x}",
                             f"at {offset:#x}")
            self.assertNotEqual(int(ins["opcode"], 16), word,
                                f"at {offset:#x}: the field now reads as the "
                                f"word, so this guard is no longer the right one")

    def test_speedup_handshake_is_three_adjacent_instructions(self):
        """The words MAME spins on are three instructions, densely adjacent.

        `init_stunrun()` sets `m_gsp_speedup_pc` and hands the GSP a pointer to
        `$FFF9FC00`; MAME's main loop spins while the GSP is at that PC, so the
        word there has to be the compare against `$FFFF`.

        The three-instruction geometry is not read back from the tool. It comes
        from two sources that agree and neither of which is this tool:

          * `third_party/tms34010_sv/rtl/core/tms34010_decode.sv:92` cites
            SPVU001A for `CMPI_IL_TOP11 = 11'b0000_1011_011` and says the
            32-bit immediate "follows in two 16-bit words (LO, HI)". `$0B60`'s
            top 11 bits are exactly `0000_1011_011`, so CMPI here is THREE
            words. `CMPI_IW_TOP11` (11 words, immediate in one) is
            `0000_1011_010` and does not match.
          * `34010dsm.cpp:774` reaches the same conclusion from its own table:
            `subop = op & 0x01e0` is `$0160` for `$0B60`, and `case 0x0160:`
            calls `print_long_parm_1s_comp`, a two-word read.

        So MOVI IL is three words, `MOVE *A3,A0,0` is one, and CMPI IL is
        three: the run is 7 words / 14 dense bytes. A cursor that stepped 16
        bytes per word would put the MOVI at +0, +48 and +64.
        """
        body = harddriv_init_stunrun()
        watched = int(re.search(
            r"m_gsp_speedup_addr\[0\]\s*=\s*[^;]*\(\s*(0x[0-9a-fA-F]+)",
            body).group(1), 16)
        pc = int(re.search(r"m_gsp_speedup_pc\s*=\s*(0x[0-9a-fA-F]+)",
                           body).group(1), 16)

        # The captured handshake, transcribed (HOTLOOP words 15..21).
        words = {0x50000: 0x09E3, 0x50002: 0xFC00, 0x50004: 0xFFF9,
                 0x50006: 0x8460, 0x50008: 0x0B60, 0x5000A: 0x0000,
                 0x5000C: 0xFFFF}
        self.assertEqual(words[0x50008] >> 5, 0x05B, "not the CMPI IL prefix")
        path = self.scratch("handshake.bin")
        path.write_bytes(share(words))
        got = decode(path, 0x50000, 3)

        self.assertEqual(got[0x50000]["mnemonic"], "MOVI")
        self.assertEqual(got[0x50000]["operands"], f"{watched:08X}h,A3")
        # SPVU001A: MOVI IL is three words (+6), `MOVE *A3,A0,0` is one (+2), so
        # the CMPI starts at +8 and runs to +14.
        self.assertEqual(sorted(got), [0x50000, 0x50006, 0x50008])
        self.assertEqual(got[0x50006]["text"], "MOVE *A3,A0,0")
        # MAME prints the immediate's 1's complement, and composes it LO then
        # HI: ~($FFFF0000) = $0000FFFF.
        self.assertEqual(got[0x50008]["text"], "CMPI FFFFh,A0")
        after_cmpi = decode(path, 0x50008, 2)
        self.assertEqual(sorted(after_cmpi), [0x50008, 0x5000E],
                         "CMPI IL is three words (opcode + LO + HI), so the "
                         "instruction after it must start six bytes later, not "
                         "two and not sixteen")

        # The driver only spins while the GSP sits at m_gsp_speedup_pc, and it
        # pairs that with the `$FFF9FC00` address. harddriv.cpp:628 maps
        # 0xff800000-0xffbfffff and mirrors it every 0x04000000, so the span
        # actually covered is 0xff800000-0xffffffff -- which is where stunrun's
        # constants live. Both must also sit on a word boundary in ADDRESS
        # UNITS, or the TMS34010 could not fetch there at all.
        mapped = (SHARE_BASE, SHARE_BASE + 2 * GSP_WINDOW_BYTES - 1)
        for name, value in (("m_gsp_speedup_pc", pc),
                            ("m_gsp_speedup_addr[0]", watched)):
            self.assertTrue(mapped[0] <= value <= mapped[1],
                            f"{name} {value:#x} is outside the mapped span")
            self.assertEqual(value & 0x0F, 0,
                             f"{name} {value:#x} is not on a word boundary in "
                             f"address units")

    def test_words_are_read_little_endian(self):
        """The discriminator against a big-endian read of the same two bytes.

        `$09E3` and `$E309` are the same two bytes; only one of them is
        `MOVI ... ,A3`. On a dense share they sit in ADJACENT two-byte slots,
        which is also the discriminator against the superseded 16-bytes-per-word
        model: under that model these two words would be eight slots apart and
        the pair would prove nothing.
        """
        word = 0x09E3   # MOVI long, Rd = A3 -- from the captured handshake
        swapped = int.from_bytes(struct.pack("<H", word), "big")
        self.assertNotEqual(word, swapped)
        path = self.scratch("endian.bin")
        path.write_bytes(share({0x21000: word, 0x21000 + WORD_BYTES: swapped}))
        first = decode(path, 0x21000, 1)[0x21000]
        second = decode(path, 0x21000 + WORD_BYTES, 1)[0x21000 + WORD_BYTES]
        self.assertEqual(first["opcode"], "000900e3")
        self.assertEqual(second["opcode"], "00e30009")
        self.assertNotEqual(first["mnemonic"], second["mnemonic"])


# ===========================================================================
# 2. ADDRESSING. The GSP-bit-address fold, asserted against MAME's own
#    constants with NO fixture. Nothing here decodes anything.
# ===========================================================================

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
class AddressingTests(unittest.TestCase):
    """The fold, measured. No image, no decode, no transcription."""

    def test_word_fold_is_mames_own_index_arithmetic(self):
        """`share_word_for_gsp_address` == the index MAME itself computes.

        This is the test the old suite was missing. Everything in the old
        `test_speedup_pc_decodes_as_the_compare_mame_hardcodes` was
        self-satisfying: it derived the probe offset with the same fold the tool
        used and then asserted the fixture it had just written. Here the
        expected value is MAME's own bracket expression, parsed out of
        `init_stunrun()` and evaluated with MAME's constants, and the actual is
        this file's fold. They cannot both be wrong in the same direction
        unless MAME changed.

        The shift matters and is easy to get wrong by one: a WORD is 16 address
        units (`addrshift = 3`, so 8 units per byte), not 8. `>> 3` yields a
        byte offset, and masking that with the share's 18-bit WORD count gives
        a number in a 256 KiB range -- a quarter of the share, and no way to
        notice from inside it.
        """
        body = harddriv_init_stunrun()
        for slot in (0, 1):
            expression = re.search(
                rf"m_gsp_speedup_addr\[{slot}\]\s*=\s*([^;]+);", body).group(1)
            mame_says = mame_share_word_index(expression)
            window_address = int(re.search(
                rf"m_gsp_speedup_addr\[{slot}\]\s*=\s*[^;]*\(\s*(0x[0-9a-fA-F]+)",
                body).group(1), 16)
            self.assertEqual(
                share_word_for_gsp_address(window_address), mame_says,
                f"slot {slot}: our fold and MAME's own index disagree for "
                f"{window_address:#x}")

    def test_byte_and_word_folds_agree_and_the_share_is_eight_times_over(self):
        """Two independent spellings of the fold, and the mirror's factor.

        `((pc - base) >> 3) & (SHARE_BYTES - 1)` and
        `((pc - base) >> 4) & (SHARE_WORDS - 1)` are different arithmetic, so
        their agreement is a real check on both. And because the window is
        eight times the share, eight window bytes alias each share byte and
        two words: shifting a window address up by one full share -- expressed
        in ADDRESS UNITS, which is what a GSP address is -- must leave the fold
        alone.
        """
        one_share_in_address_units = SHARE_BYTES * (1 << GSP_ADDRSHIFT)
        self.assertEqual(one_share_in_address_units, SHARE_BYTES << 3)
        for pc in SAMPLED_POST_RELEASE_PCS + UPLOAD_SPAN:
            byte = share_byte_for_gsp_address(pc)
            word = share_word_for_gsp_address(pc)
            self.assertEqual(byte, 2 * word, f"{pc:#x}: the two folds disagree")
            self.assertLess(byte, SHARE_BYTES, f"{pc:#x}: fold left the share")
            self.assertEqual(
                byte,
                share_byte_for_gsp_address(pc + one_share_in_address_units),
                f"{pc:#x}: one share later is a different place, so this is "
                f"not the mirror the map declares")
        self.assertEqual(GSP_WINDOW_BYTES // SHARE_BYTES, 8,
                         "the mirror factor changed; the alias check above is "
                         "no longer saying what it claims")

    def test_sampled_post_release_pcs_fold_inside_the_uploaded_span(self):
        """The eight sampled PCs land in the span the upload covered.

        `analysis/gsp-program-upload-boundary.md` records `$02FE68`/`$02FE6E`
        writing 6,091 words each into `$FFF40000`-`$FFFFFFE0` just before the
        release, and that every sampled post-release PC lies inside it. Folding
        both sets with the same function is the only way that claim survives a
        change of address model: the old version of this test folded the PCs
        but left the span in the superseded 4 MiB space, so it was comparing
        two coordinate systems and could not fail for the right reason.
        """
        low, high = (share_word_for_gsp_address(a) for a in UPLOAD_SPAN)
        self.assertLess(low, high, "the uploaded span does not fold to a range")
        for pc in SAMPLED_POST_RELEASE_PCS:
            word = share_word_for_gsp_address(pc)
            self.assertTrue(low <= word <= high,
                            f"{pc:#010x} folds to share word {word:#x}, outside "
                            f"the uploaded {low:#x}-{high:#x}")
            self.assertEqual(
                word,
                share_word_for_gsp_address(pc + SHARE_WORDS * (1 << 4)),
                f"{pc:#x}: the fold is not invariant across the share")

    def test_speedup_pc_is_inside_the_share_and_not_on_a_word_misalignment(self):
        """`m_gsp_speedup_pc` folds to a whole word, and to one that exists.

        The GSP bit address of an instruction is word-aligned by construction
        (a TMS34010 fetches whole words), so a fold that landed on an odd byte
        would mean the shift is wrong rather than that the PC is unusual.
        """
        pc = int(re.search(r"m_gsp_speedup_pc\s*=\s*(0x[0-9a-fA-F]+)",
                           harddriv_init_stunrun()).group(1), 16)
        self.assertEqual(share_byte_for_gsp_address(pc) % WORD_BYTES, 0, hex(pc))
        self.assertEqual(share_byte_for_gsp_address(pc),
                         2 * share_word_for_gsp_address(pc))
        self.assertEqual(share_word_for_gsp_address(pc), 0x34107,
                         "the hardcoded speed-up PC folded somewhere else; "
                         "MAME's driver or the share size changed")


# ===========================================================================
# 3. THE TOOL'S OWN ADDRESSING CONTRACT: share bytes in, share bytes out,
#    and a step of one word per word consumed.
# ===========================================================================

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"),
                     "no C++ compiler to build MAME's disassembler with")
class ToolAddressingTests(DisasmGspCase):

    def test_offsets_are_word_aligned_and_rise_by_whole_words(self):
        """Every reported offset is word-aligned and rises by >= one word.

        The INVERSE of what this test asserted for months. It used to require
        16-byte alignment and a >= 16-byte rise, which is right for a 4 MiB
        window image with one word per 16-byte cell and wrong for the dense
        share the tool actually reads -- and it passed, because the fixture
        packed words on the same 16-byte pitch the tool skipped.

        On a dense share the invariant is the same statement at the physical
        unit: offsets are multiples of `WORD_BYTES` and never go backwards or
        overlap. A tool that stepped 16 bytes per word would fail on the first;
        one that stepped 2 bytes per word but read operands at 2-byte pitch
        would fail on the decode tests above instead.
        """
        got = sorted(decode(self.image, 0x40000, 24))
        self.assertGreaterEqual(len(got), 8)
        for offset in got:
            self.assertEqual(offset % WORD_BYTES, 0, f"{offset:#x}")
        for before, after in zip(got, got[1:]):
            self.assertGreaterEqual(after - before, WORD_BYTES,
                                    f"{before:#x} -> {after:#x}")
            self.assertEqual((after - before) % WORD_BYTES, 0)

        proc = run("--at", "0x40000", "--count", "1", "--json", image=self.image)
        # WORD_STRIDE is MAME's ADDRESS UNITS PER WORD -- a byte is 8 units and
        # a word is 16 (addrshift = 3). It is not a byte pitch in the share and
        # must not be read as one; the share's pitch is WORD_BYTES.
        self.assertEqual(json.loads(proc.stdout)["word_stride"], WORD_STRIDE)

    def test_reported_offsets_are_share_bytes_not_window_bytes(self):
        """`offset` and `vram` speak share bytes, at a MAME-supplied address.

        The offset is built from `m_gsp_speedup_pc` through this file's fold --
        which test 2 pins to MAME's own index arithmetic -- and the words are
        the captured handshake. So the offset is right for a reason that is not
        "the fixture was laid out to match": if the tool's internal expansion
        disagreed with the fold, `--at` would read a different word and this
        would report `DW`.

        This does NOT claim the real capture holds a CMPI at that share offset.
        That is not testable here: the transcription's labels are not share
        offsets (module docstring), so the content is planted, and a planted
        word is not evidence about the board.
        """
        pc = int(re.search(r"m_gsp_speedup_pc\s*=\s*(0x[0-9a-fA-F]+)",
                           harddriv_init_stunrun()).group(1), 16)
        offset = share_byte_for_gsp_address(pc)
        # `$0B60` is CMPI IL: three words, 32-bit immediate LO then HI, printed
        # complemented. ~($FFFF0000) is $0000FFFF. See
        # `test_speedup_handshake_is_three_adjacent_instructions` for the two
        # sources of that encoding claim.
        path = self.scratch("speedup-at.bin")
        path.write_bytes(share({offset: 0x0B60, offset + 2: 0x0000,
                                offset + 4: 0xFFFF}))
        ins = decode(path, offset, 1)
        self.assertEqual(sorted(ins), [offset],
                         f"the tool reported an offset other than {offset:#x} "
                         f"for --at {offset:#x}")
        self.assertEqual(ins[offset]["text"], "CMPI FFFFh,A0")
        self.assertEqual(ins[offset]["vram"], SHARE_BASE + (offset << GSP_ADDRSHIFT))

    def test_default_start_is_the_folded_upload_entry(self):
        """Default --at is the upload entry folded into dense share bytes."""
        path = self.scratch("default-start.bin")
        path.write_bytes(share({}))
        proc = run("--json", image=path)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        result = json.loads(proc.stdout)
        self.assertEqual(result["start"], 0x68000)
        self.assertEqual(result["start_vram"], 0xFFB40000)
        self.assertEqual(result["instructions"][0]["offset"], 0x68000)

    def test_rejects_odd_or_out_of_share_start_offsets(self):
        """A byte address must identify a whole word inside the physical share."""
        path = self.scratch("bad-start.bin")
        path.write_bytes(share({}))
        for start in ("1", "0x80000", "-2"):
            with self.subTest(start=start):
                proc = run("--at", start, image=path)
                self.assertEqual(proc.returncode, 2, proc.stderr)
                self.assertIn("--at must be an even byte offset", proc.stderr)

    def test_rejects_nonpositive_instruction_count(self):
        path = self.scratch("bad-count.bin")
        path.write_bytes(share({}))
        proc = run("--at", "0", "--count", "0", image=path)
        self.assertEqual(proc.returncode, 2, proc.stderr)
        self.assertIn("--count must be positive", proc.stderr)

    def test_the_share_is_dense_so_adjacent_words_are_not_replicas(self):
        """Two words two bytes apart decode separately -- the old bug, pinned.

        The superseded model had one word replicated across 16 bytes, and
        `test_every_alignment_inside_a_replicated_cell_reads_the_same_word`
        asserted that as a feature. It is not a feature of this machine: the
        share is dense (findings 87.5, and a 2-byte sweep of the frame-404
        capture returns exactly the `nonzero_words` its own `result.json`
        records). Under the corrected model a word's 8 former aliases are 8
        DIFFERENT words, and a tool still walking 16 bytes per word visits one
        word in eight. So: three adjacent words, each decoded on its own, all
        three distinct.

        `$07E0` is three words long (`MOVE @Saddress,Rs,F`, SPVU001A A-14), so a
        single linear run would step over its neighbours; they are decoded one
        at a time deliberately.
        """
        trio = {0x20000: 0x4820, 0x20002: 0x07E0, 0x20004: 0x5600}  # CMP / MOVE / CLR
        path = self.scratch("dense.bin")
        path.write_bytes(share(trio))
        want = {0x20000: "CMP A1,A0", 0x20002: "MOVE", 0x20004: "CLR A0"}
        seen = set()
        for offset, text in want.items():
            ins = decode(path, offset, 1).get(offset)
            self.assertIsNotNone(ins, f"nothing decoded at {offset:#x}")
            self.assertTrue(ins["text"].startswith(text), ins["text"])
            seen.add(ins["text"])
        self.assertEqual(len(seen), 3,
                         f"adjacent share words decoded identically: {seen}")

    def test_operand_words_are_fetched_at_sixteen_byte_pitch(self):
        """The three-word MOVE reads its address from words 1 and 2.

        This is the regression guard for the diagnosis itself. MAME's
        disassembler reads operand words 16 address units apart; on a dense
        share that is 8 words apart unless the tool expands the share into the
        cell layout the disassembler expects. `$0580` renders `MOVE Rd,@Daddr,F`
        with the address in the two following words (SPVU001A A-14), so both
        halves are checkable from the operand text alone:

            words 0580 0110 c000   ->  MOVE A0,@C0000110h,0

        A tool that stepped 2 bytes per word while fetching operands 2 bytes
        apart renders `@0h` and still exits 0.
        """
        path = self.scratch("operands.bin")
        path.write_bytes(share({0: 0x0580, 2: 0x0110, 4: 0xC000}))
        proc = run("--at", "0x0", "--count", "1", image=path)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("MOVE   A0,@C0000110h,0", proc.stdout, proc.stdout)
        # And the same three words, read through --json, carry the same address.
        ins = decode(path, 0, 1)[0]
        self.assertIn("@C0000110h", ins["operands"], ins["operands"])


# ===========================================================================
# 4. The decoder must not invent a mnemonic, and must say so where it cannot.
# ===========================================================================

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"),
                     "no C++ compiler to build MAME's disassembler with")
class UndecidableTests(DisasmGspCase):

    def test_architecturally_illegal_opcodes_render_as_DW(self):
        """SPVU001A Table 8-6: the TMS34010 traps on these. Never a mnemonic.

        The ISA lists 9,984 illegal opcode words. If any of them decoded to a
        mnemonic, the tool would be rendering an instruction the processor
        cannot execute -- the exact failure MD-13 alleged, in the one region
        where it is actually checkable.
        """
        words: list[int] = []
        for low, high in ILLEGAL_RANGES:
            words += [c for c in (low, low + 0x01, low + 0x80, low + 0xFE, high)
                      if low <= c <= high]
        for word, ins in zip(words, decode_word_list(words, self.image)):
            self.assertEqual(ins["mnemonic"], "DW",
                             f"{word:#06x} is illegal per SPVU001A Table 8-6 but "
                             f"decoded as {ins['mnemonic']}")

    def test_every_word_of_an_illegal_range_is_sampled(self):
        """The sampling above must actually reach each range's extremes.

        Otherwise "no illegal word renders as a mnemonic" could be true simply
        because the probe never looked at the offending end of a range.
        """
        for low, high in ILLEGAL_RANGES:
            for word in (low, high):
                ins = decode_word_list([word], self.image)[0]
                self.assertEqual(ins["mnemonic"], "DW",
                                 f"{word:#06x} decoded as {ins['mnemonic']}")

    def test_no_word_decodes_to_a_repeat_mnemonic(self):
        """MD-13 said MAME renders repeats as MOVE. Sweep the families.

        Every word in the `1001 10FS` / `1001 11FS` families -- 1,024 words,
        the encodings MD-13 attributed to RS/RS1/RS2/RI/RPT. Nothing may decode
        to a repeat mnemonic, and nothing may be the `DW` fallback either:
        these are legal `MOVE`s and must say so.
        """
        words = list(range(0x9800, 0x9C00)) + [0x9920]
        for word, ins in zip(words, decode_word_list(words, self.image)):
            self.assertNotIn(ins["mnemonic"], ("RS", "RS1", "RS2", "RI", "RPT"),
                             f"{word:#06x} decoded as a repeat instruction")
            self.assertNotEqual(ins["mnemonic"], "DW",
                                f"{word:#06x} is a legal MOVE but was not decoded")

    def test_0x9920_is_the_move_the_isa_documents(self):
        """The word MD-13 called a mis-decode, pinned to the correct reading.

        SPVU001A A-14: `MOVE *Rs+,*Rd+,F  1001 10FS SSSR DDDD`. For `$9920`:
        F = 0, Rs = bits[8:5] = 9 -> A9, R = 0 (A file), Rd = 0 -> A0.
        """
        got = decode_word_list([0x9920], self.image)[0]
        self.assertEqual(got["mnemonic"], "MOVE")
        self.assertEqual(got["operands"], "*A9+,*A0+,0", got["operands"])

    def test_uniform_fill_data_still_decodes_to_plausible_instructions(self):
        """The reason `DW` cannot be read as "not code", asserted as behaviour.

        The 1,397-word window the first-output boundary defines (share bytes
        446118..448912) contains 18 distinct words, and nine of them decode as
        instructions -- `MOVE *B12+,B5,0`, `BTST A10,A11`, `SRA A11,A5`. The
        docstring says this; if MAME's decoder changed underneath it, this
        would fail instead of the docstring quietly becoming a lie.
        """
        words = sorted(FILL_WORDS)
        for word, ins in zip(words, decode_word_list(words, self.image)):
            self.assertEqual(ins["mnemonic"], FILL_WORDS[word],
                             f"{word:#06x} decoded as {ins['text']}")
        self.assertEqual(sum(1 for m in FILL_WORDS.values() if m != "DW"), 9,
                         "the measured claim that nine of eighteen fill words "
                         "render as instructions has changed")


# ===========================================================================
# 5. Negative controls: the assertions above must be able to fail.
# ===========================================================================

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"),
                     "no C++ compiler to build MAME's disassembler with")
class NegativeControlTests(DisasmGspCase):

    def test_mutating_one_byte_of_an_asserted_instruction_changes_the_decode(self):
        """If this passes vacuously, the tests above are reading nothing.

        Each transcribed word is flipped in bit 0 and in bit 8 -- one run of the
        word, not the whole instruction -- and the decode must change. A test
        that cannot distinguish the right bytes from the wrong ones is not a
        test, and this is the only thing here that would notice.

        Two of the 85 words are legitimately inert, and they are named rather
        than excluded, because "the control passed" must not mean "the control
        skipped some":

          * 0x4000A holds `$10C0`, an operand word of the three-word `MOVE` at
            0x40008. It is an operand, not an instruction; MAME renders it and
            the word with bit 0 set alike.
          * 0x42E42 holds `$0FC0`, which SPVU001A A-13 gives as
            `FILL L  0000 1111 1100 0000` -- bit 0 is not in the encoding, so
            `$0FC1` is the same instruction and *must* render identically.

        These are dense share offsets. Under the superseded 16-byte-per-word
        model the same two words sat at 0x40050 and 0x42E50.
        """
        inert = set()
        for offset, word in sorted(self.words.items()):
            for bit in (0, 8):
                path = self.scratch("mutate.bin")
                mutated = dict(self.words)
                mutated[offset] = word ^ (1 << bit)
                path.write_bytes(share(mutated))
                before = self.text(offset)
                after = self.text(offset, image=path)
                if before == after:
                    inert.add(offset)
        self.assertEqual(
            inert, {0x4000A, 0x42E42},
            "the set of words whose decode survives a one-bit mutation changed; "
            "if a new word is inert the mutation control no longer covers it, "
            "and if a known one is no longer inert MAME's decoder has changed")

    def test_the_oracles_own_capture_size_is_accepted(self):
        """The 512 KiB direct share capture MUST be accepted.

        An earlier revision of this tool demanded 4194304 bytes -- the GSP
        ADDRESS WINDOW from harddriv.cpp:628 -- and so refused
        `tools/mame-gsp-vram-dump.lua`'s own 524288-byte output, with a message
        claiming the 512 KiB form "is a different region". That was wrong: MAME's
        direct m_gsp_vram dump IS 524288 bytes / 262144 words, Margolin (S3)
        counts 8 banks of 64Kx8 on the MultiSync board, and SP-338 (S6) says
        "VRAM Array (16) 64K x 4".

        The regression this pins is narrow and deliberate: if someone reintroduces
        the window size as the accepted size, this fails.
        """
        path = self.scratch("oracle-shaped.bin")
        path.write_bytes(bytes(SHARE_BYTES))
        proc = run("--at", "0x40000", "--count", "2", image=path)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stderr, "", "the oracle-shaped capture produced a diagnostic")
        self.assertIn("DW", proc.stdout)

    def test_size_guard_refuses_every_wrong_sized_image(self):
        """A capture that is neither 512 KiB nor empty-of-content must be refused.

        Wrong sizes include the 4 MiB ADDRESS WINDOW: it is a different
        container from the 512 KiB physical share, and decoding one silently
        would place every address wrong by a constant -- and still print text.
        """
        for size in (0, SHARE_BYTES // 2, SHARE_BYTES + 4, GSP_WINDOW_BYTES):
            with self.subTest(size=size):
                path = self.scratch(f"size-{size}.bin")
                path.write_bytes(b"\x00" * size)
                proc = run("--at", "0x40000", "--count", "2", image=path)
                self.assertNotEqual(proc.returncode, 0,
                                    f"a {size}-byte image was decoded")
                # The message must name the physical size and must not let the
                # window size pass as the share.
                self.assertIn("524288", proc.stderr)
                self.assertIn("WINDOW", proc.stderr)

    def test_a_missing_image_is_a_diagnosis_not_a_traceback(self):
        proc = run("--count", "1", image=self.scratch("does-not-exist.bin"))
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("no image at", proc.stderr)
        self.assertNotIn("Traceback", proc.stderr)


if __name__ == "__main__":
    unittest.main()