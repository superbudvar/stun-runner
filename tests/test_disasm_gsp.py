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
  * `/mnt/build/mame-ref/src/mame/atari/harddriv.cpp` -- `init_stunrun()`
    hardcodes `m_gsp_speedup_pc = 0xfff41070` and
    `m_gsp_speedup_addr[0] = &m_gsp_vram[(0xfff9fc00 - ...)]`. Those are
    constants in the driver, written without reference to this tool, and the
    test reads them out of the file rather than hardcoding them so they cannot
    silently rot.
  * The captured GSP share -- the words transcribed in `PROGRAM` below, taken
    from a `tools/mame-gsp-vram-dump` capture of the 4 MiB share at frame 450
    (`sha256 95d538ad8d3d1ae9a98f3f1b1daf8f4f4eb390f453f038c7a00c87c71a43e7ff`).
    Only 85 instruction words are transcribed, as contiguous blocks. Only the
    words are transcribed, not the raw bytes, so the eightfold replication each
    cell has in the capture cannot be re-checked from this file -- a test built
    out of the transcription would only assert the transcription. What *is*
    tested is the tool behaviour that replication is load-bearing for, in
    `AddressingTests.test_every_alignment_inside_a_replicated_cell_reads_the_same_word`.

NOTE on the ground truth the brief lists: `$0002C1C4` / `$0002C1CC` are the
**68010** PCs at which the maincpu writes `$7800` to HSTCTL to release the
halted GSP, not GSP PCs (analysis/gsp-program-upload-boundary.md, findings
section 36.2). Nothing here depends on them, and nothing in this file calls
them GSP addresses.

NOTE on the withdrawn claim this file also pins: BUGS.md MD-13 asserted that
MAME mis-decodes repeat instructions as `MOVE`. SPVU001A's instruction-set
summary has no repeat instruction, and assigns `1001 10FS SSSR DDDD` to
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

# harddriv.cpp -> multisync_gsp_map: map(0xff800000, 0xffbfffff)
#   .mirror(0x0400000).ram().share("gsp_vram")
SHARE_BASE = 0xFF800000
# The PHYSICAL SHARE is 512 KiB. harddriv.cpp:628 maps a 4 MiB address WINDOW
# with a mirror mask, and an earlier revision of this file called that window
# "the share" -- which is what made tools/disasm-gsp refuse the oracle's own
# 524288-byte capture. MAME's direct m_gsp_vram dump reports 262144 words;
# Margolin (S3) counts 8 banks of 64Kx8 on the MultiSync board; SP-338 (S6)
# says "VRAM Array (16) 64K x 4".
SHARE_BYTES = 0x80000
GSP_WINDOW_BYTES = 0x400000
# Window-space offsets folded into the 512 KiB physical share. These were
# written as share offsets in the old 4 MiB WINDOW space; they are now
# physical offsets (& 0x7FFFF). The GSP window address each came from is
# recoverable by adding 0xFF800000 plus the folded-out multiple of 0x80000.
# One instruction word per 16-byte cell, replicated 8x. See the tool's docstring.
WORD_STRIDE = 16

MAME_SRC = pathlib.Path("/mnt/build/mame-ref")
HARDRIV = MAME_SRC / "src/mame/atari/harddriv.cpp"

# SPVU001A section 8.7, Table 8-6, "Illegal Opcodes Ranges" (p. 8-9).
ILLEGAL_RANGES = (
    (0x0200, 0x02FF), (0x0400, 0x04FF), (0x0800, 0x08FF),
    (0x0A00, 0x0AFF), (0x0C00, 0x0CFF), (0x0E00, 0x0EFF),
    (0x3400, 0x37FF), (0x7000, 0x7FFF), (0x9E00, 0x9FFF),
    (0xBE00, 0xBFFF), (0xD800, 0xDEFF), (0xFE00, 0xFFFF),
)

# 85 instruction words transcribed from the capture, as contiguous 16-byte
# cells. Format: name -> (base share offset, "wwwwww..." little-endian words).
PROGRAM = {
    # Display setup. 0x40040 is the first of the seven out-of-bounds GSP
    # register writes that BUGS.md MD-12 is about; the two words after the
    # opcode are the @C0000110h address, high word first.
    "ENTRY": (0x40000, "0550074009c0000005800110c000184005800150c00009c0000005800160c000"
                      "09e0fffc000005800090c0000580f5a0fff609d8000009d1040009d304004e306a"
                      "0005800130c0004e706a000580"),
    # The hottest address in the 400,000-fetch trace (78,994 fetches) and the
    # ready-handshake that contains MAME's hardcoded speed-up PC.
    "HOTLOOP": (0x40f40, "fff707e016a0fff74820c4fb560005e016a0fff7058016c0fff7055007400"
                        "9e3fc00fff984600b600000ffffcb0409e5fc10fff9c00b09e3"),
    # Post-release GSP PCs sampled elsewhere in this project, each with the two
    # cells that follow so multi-word instructions get real operands.
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


def share(cells: dict[int, int]) -> bytes:
    """A 512 KiB share image with `word` written little-endian at each cell offset.

    An offset outside the share raises rather than being written: bytearray
    slice assignment past the end *appends*, which would silently produce an
    oversized image and then fail the tool's size guard for the wrong reason.
    """
    buf = bytearray(SHARE_BYTES)
    for offset, word in cells.items():
        if not 0 <= offset <= SHARE_BYTES - 2:
            raise ValueError(f"cell 0x{offset:x} does not fit a {SHARE_BYTES}-byte share")
        buf[offset:offset + 2] = struct.pack("<H", word)
    return bytes(buf)


def program_cells() -> dict[int, int]:
    cells: dict[int, int] = {}
    for base, blob in PROGRAM.values():
        for index in range(len(blob) // 4):
            cells[base + WORD_STRIDE * index] = int(blob[index * 4:index * 4 + 4], 16)
    return cells


def run(*args: str, image: pathlib.Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), str(image), *args], capture_output=True,
                          text=True, check=False, timeout=900)


def decode(image: pathlib.Path, start: int, count: int) -> dict[int, dict]:
    """Decode `count` instructions from `start`; key the result by share offset."""
    proc = run("--at", hex(start), "--count", str(count), "--json", image=image)
    if proc.returncode != 0:
        raise AssertionError(f"disasm-gsp failed: {proc.stderr.strip()}")
    return {i["offset"]: i for i in json.loads(proc.stdout)["instructions"]}


class DisasmGspCase(unittest.TestCase):
    """Shared 4 MiB image holding the transcribed program at its real offsets."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._dir = tempfile.TemporaryDirectory(prefix="disasm-gsp-test-")
        cls.image = pathlib.Path(cls._dir.name) / "gsp_vram.bin"
        cls.cells = program_cells()
        cls.image.write_bytes(share(cls.cells))

    @classmethod
    def tearDownClass(cls) -> None:
        cls._dir.cleanup()

    def text(self, offset: int, image: pathlib.Path | None = None) -> str:
        ins = decode(self.image if image is None else image, offset, 1)[offset]
        return f"{ins['mnemonic']} {ins['operands']}".strip()


# Where a batch of candidate words starts in the share. The pitch between
# candidates is set by `decode_word_list`.
PROBE_BASE = 0x10000


def decode_word_list(words: list[int], image: pathlib.Path) -> list[dict]:
    """Decode a batch of candidate words in one linear run, in the order given.

    A linear run visits every cell, so a candidate can only be read at its own
    offset if the cell before it cannot swallow it. The longest instruction in
    this ISA is five words (`MOVB @Saddress,@Daddress`, SPVU001A A-14), so each
    candidate gets a five-cell pitch with four `DW` filler words behind it: a
    candidate of any length lands, and the run's own cursor is back on a cell
    boundary before the next one. The filler is `$FFFF`, which is illegal per
    Table 8-6 and therefore decodes to exactly one word.

    Every candidate offset must appear in the output. If the pitch were too
    small, or a candidate turned out to be six words, the candidate would be
    reported as swallowed rather than silently missing.
    """
    pitch = 6 * WORD_STRIDE
    cells: dict[int, int] = {}
    offsets = []
    for index, word in enumerate(words):
        offset = PROBE_BASE + pitch * index
        offsets.append(offset)
        cells[offset] = word
        for filler in range(1, 5):
            cells[offset + WORD_STRIDE * filler] = 0xFFFF
    path = image.parent / (image.stem + "-probe.bin")
    path.write_bytes(share(cells))
    got = decode(path, PROBE_BASE, 10 * len(words) + 16)
    missing = [offset for offset in offsets if offset not in got]
    if missing:
        raise AssertionError(
            f"{len(missing)} of {len(words)} candidate cells were not decoded, "
            f"first at {missing[0]:#x}; a candidate was swallowed by a "
            f"neighbouring instruction")
    return [got[offset] for offset in offsets]


# --------------------------------------------------------------------------
# 1. The decoder is right: expectations from SPVU001A and from MAME's driver.
# --------------------------------------------------------------------------

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"),
                     "no C++ compiler to build MAME's disassembler with")
class DecodingTests(DisasmGspCase):

    def test_speedup_pc_decodes_as_the_compare_mame_hardcodes(self):
        """MAME's own `m_gsp_speedup_pc` must land on a real instruction.

        `init_stunrun()` in harddriv.cpp sets `m_gsp_speedup_pc = 0xfff41070`
        and `m_gsp_speedup_addr[0] = &m_gsp_vram[(0xfff9fc00 - ...)]`. The GSP
        polls that address until the host writes `$FFFF`, so the instruction at
        the hardcoded PC must be the compare against `$FFFF`, and the register
        load just before it must name `$FFF9FC00`. Three constants from the
        driver, none of which mention this tool, have to land on these words.
        """
        body = harddriv_init_stunrun()
        pc = re.search(r"m_gsp_speedup_pc\s*=\s*(0x[0-9a-f]+)", body).group(1)
        addr = re.search(r"m_gsp_speedup_addr\[0\]\s*=\s*[^;]*\(\s*(0x[0-9a-f]+)",
                         body).group(1)
        speedup_pc = int(pc, 16)
        speedup_addr = int(addr, 16)
        offset = (speedup_pc - SHARE_BASE) % SHARE_BYTES

        self.assertEqual(offset, 0x41070, "the hardcoded PC moved")
        ins = decode(self.image, offset, 1)[offset]
        self.assertEqual(ins["mnemonic"], "CMPI",
                         "MAME spins while the GSP is at m_gsp_speedup_pc, so "
                         "that address must be the handshake compare")
        # The compare value is the 0xffff hdgsp_speedup_r tests for, inverted
        # because CMPI takes the immediate's 1's complement.
        self.assertEqual(ins["operands"], "FFFFh,A0", ins["operands"])

        # And the load that puts the watched address in the pointer register.
        # MOVI is three words and is followed by a one-word MOVE, so it sits
        # four cells above the compare.
        load = self.text(offset - 4 * WORD_STRIDE)
        self.assertTrue(load.startswith("MOVI "), load)
        self.assertEqual(load.split()[1], f"{speedup_addr:08X}h,A3", load)

    def test_hot_loop_branch_target_is_hand_derived(self):
        """Assert the target arithmetic, not the string MAME printed.

        SPVU001A Appendix A: `JRcc Address  1100 code xxxx xxxx` (A-16), i.e.
        the short relative form, whose displacement is the low eight bits
        signed and whose target is `pc + 16 + (disp << 4)` in this program's
        16-byte cells. Computed here from the bits, not read back.
        """
        offset = 0x40f90
        word = self.cells[offset]
        self.assertEqual(word & 0xFE00, 0xC400, "not the JRcc family")
        displacement = word & 0xFF
        if displacement >= 0x80:
            displacement -= 0x100
        expect = offset + WORD_STRIDE + (displacement << 4)

        ins = decode(self.image, offset, 1)[offset]
        self.assertEqual(ins["mnemonic"], "JRLT")
        self.assertEqual(int(ins["operands"].rstrip("h"), 16), expect,
                         "the branch does not land where the ISA says it must")
        # And it really is the loop head: three instructions earlier.
        self.assertEqual(self.cells[expect], 0x07E0,
                         "the computed target is not the hottest address")

    def test_entry_sequence_matches_the_documented_display_setup(self):
        """The first instructions are the SETF/MOVI/MOVE display setup.

        SPVU001A A-15: `SETF FS,FE,F  0000 01F1 01 FS SSSS`. A-14:
        `MOVE Rd,@Daddress,F  0000 01F1 100R SSSS` (3 words) and
        `MOVE Rs,Rd  0100 11MS SSSR DDDD`; A-15: `LMO Rs,Rd  0110 101S SSSR
        DDDD`; A-14: `MOVB Rs,@Daddress  0000 0101 111R SSSS`.
        """
        got = decode(self.image, 0x40000, 20)
        self.assertEqual(
            [got[offset]["mnemonic"] for offset in sorted(got)],
            ["SETF", "SETF", "MOVI", "MOVE", "MOVK", "MOVE", "MOVI", "MOVE",
             "MOVI", "MOVE", "MOVE", "MOVI", "MOVI", "MOVI", "MOVE", "LMO",
             "MOVE", "MOVE", "LMO", "MOVE"])

        # 0x40040 is the first of MD-12's seven register writes past the end
        # of MAME's 64-entry m_IOregs. The address is the two words after the
        # opcode, high word first.
        ins = got[0x40040]
        self.assertEqual(ins["mnemonic"], "MOVE")
        self.assertIn("@C0000110h", ins["operands"], ins["operands"])

    def test_sampled_post_release_pcs_decode_inside_the_uploaded_range(self):
        """The eight PCs sampled elsewhere in this project, all decoded.

        Each share offset is (PC - SHARE_BASE) mod SHARE_BYTES, which is what
        the tool's own `vram` field inverts. The assertion is that the tool
        agrees on the mapping and produces an instruction -- not what the
        instruction is, because nothing independent fixes that.
        """
        pcs = (0xFFF59A70, 0xFFF59DD0, 0xFFF42E50, 0xFFF40F90, 0xFFF41060,
               0xFFF414C0, 0xFFF45330, 0xFFF479E0)
        for pc in pcs:
            offset = (pc - SHARE_BASE) % SHARE_BYTES
            self.assertTrue(0x40000 <= offset <= 0x5dc00,
                            f"{pc:#010x} -> {offset:#x} is outside the upload")
            ins = decode(self.image, offset, 1).get(offset)
            self.assertIsNotNone(ins, f"nothing decoded at {pc:#010x}")
            self.assertNotEqual(ins["mnemonic"], "DW",
                                f"{pc:#010x} decoded as an unhandled word")
            self.assertEqual(ins["vram"], SHARE_BASE + offset)

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
        for offset in (0x40000, 0x40070, 0x40f90, 0x41070):
            word = self.cells[offset]
            ins = decode(self.image, offset, 1)[offset]
            self.assertEqual(ins["opcode"], f"{word >> 8:04x}{word & 0xFF:04x}",
                             f"at {offset:#x}")
            self.assertNotEqual(int(ins["opcode"], 16), word,
                                f"at {offset:#x}: the field now reads as the "
                                f"word, so this guard is no longer the right one")


# --------------------------------------------------------------------------
# 2. The addressing is right -- and specifically not the `>> 3` bug.
# --------------------------------------------------------------------------

@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"),
                     "no C++ compiler to build MAME's disassembler with")
class AddressingTests(DisasmGspCase):

    def test_offsets_advance_in_whole_sixteen_byte_cells(self):
        """Every decoded offset is a multiple of 16 and rises by at least 16."""
        got = sorted(decode(self.image, 0x40000, 24))
        self.assertGreaterEqual(len(got), 8)
        for offset in got:
            self.assertEqual(offset % WORD_STRIDE, 0, f"{offset:#x}")
        for before, after in zip(got, got[1:]):
            self.assertGreaterEqual(after - before, WORD_STRIDE)
            self.assertEqual(after - before,
                             WORD_STRIDE * ((after - before) // WORD_STRIDE))
        proc = run("--at", "0x40000", "--count", "1", "--json", image=self.image)
        self.assertEqual(json.loads(proc.stdout)["word_stride"], WORD_STRIDE)

    def test_neighbouring_cells_are_distinguished(self):
        """The discriminator against a `>> 3` (divide-by-8) stride.

        A first version of this tool divided the read position by 8. That bug
        is invisible in a uniform region and inside a replicated cell, because
        both read the same word twice over. It is only visible across a cell
        boundary. So: two adjacent cells holding words that decode differently,
        each decoded on its own, and the tool must report both, at both offsets.

        Decoded one at a time deliberately: `$07E0` is `MOVE *Rs,Rd,F` with a
        32-bit address, three words long, so a single linear run would step
        over the neighbour instead of reading it.
        """
        left, right = 0x4820, 0x5600   # CMP A1,A0 / CLR A0, both one word
        path = pathlib.Path(self._dir.name) / "neighbour.bin"
        path.write_bytes(share({0x20000: left, 0x20000 + WORD_STRIDE: right}))
        for offset, mnemonic in ((0x20000, "CMP"), (0x20010, "CLR")):
            ins = decode(path, offset, 1).get(offset)
            self.assertIsNotNone(ins, f"nothing decoded at {offset:#x}")
            self.assertEqual(ins["mnemonic"], mnemonic, ins["text"])

    def test_words_are_read_little_endian(self):
        """The discriminator against a big-endian read of the same two bytes.

        Replicated cells make byte order *look* irrelevant, so it has to be
        pinned on bytes that are not palindromic. `$09E3` and `$E309` are the
        same two bytes; only one of them is `MOVI ... ,A3`.
        """
        word = 0x09E3   # MOVI long, Rd = A3 -- from the captured handshake
        swapped = int.from_bytes(struct.pack("<H", word), "big")
        self.assertNotEqual(word, swapped)
        path = pathlib.Path(self._dir.name) / "endian.bin"
        path.write_bytes(share({0x21000: word, 0x21000 + WORD_STRIDE: swapped}))
        for offset, opcode in ((0x21000, "000900e3"), (0x21010, "00e30009")):
            ins = decode(path, offset, 1)[offset]
            self.assertEqual(ins["opcode"], opcode)
        self.assertNotEqual(
            decode(path, 0x21000, 1)[0x21000]["mnemonic"],
            decode(path, 0x21010, 1)[0x21010]["mnemonic"])

    def test_every_alignment_inside_a_replicated_cell_reads_the_same_word(self):
        """The claim that makes the whole mapping usable, asserted as behaviour.

        The docstring says a fetch at any alignment inside the 16-byte cell
        returns the same word, because the capture replicates it eight times.
        Decode at all eight 2-byte alignments and require the same mnemonic and
        the same opcode bytes; if the mapping ever walked a 2-byte stride
        instead of a 16-byte one it could not.

        The *operands* are deliberately not compared: `$C4FB` is a `JRLT`, whose
        target is `pc + 16 + disp`, so the rendered target legitimately moves
        with the alignment. Only the word is replication-invariant.
        """
        word = 0xC4FB   # JRLT, from the captured hot loop
        cell = struct.pack("<H", word) * (WORD_STRIDE // 2)
        buf = bytearray(SHARE_BYTES)
        buf[0x22000:0x22000 + WORD_STRIDE] = cell
        path = pathlib.Path(self._dir.name) / "cell.bin"
        path.write_bytes(bytes(buf))
        seen = set()
        for index in range(WORD_STRIDE // 2):
            ins = next(iter(decode(path, 0x22000 + 2 * index, 1).values()))
            seen.add((ins["mnemonic"], ins["opcode"]))
        self.assertEqual(seen, {("JRLT", "00c400fb")},
                         f"the eight alignments inside one cell disagreed: {seen}")


# --------------------------------------------------------------------------
# 3. The decoder must not invent a mnemonic, and must say so where it cannot.
# --------------------------------------------------------------------------

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


# --------------------------------------------------------------------------
# 4. Negative controls: the assertions above must be able to fail.
# --------------------------------------------------------------------------

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

        Two of the 85 cells are legitimately inert, and they are named rather
        than excluded, because "the control passed" must not mean "the control
        skipped some":

          * 0x40050 holds `$0110`, the address word of the three-word `MOVE`
            at 0x40040. It is an operand, not an instruction; MAME renders it
            and the word with bit 0 set alike as `EMU`.
          * 0x42e50 holds `$0FC0`, which SPVU001A A-13 gives as
            `FILL L  0000 1111 1100 0000` -- bit 0 is not in the encoding, so
            `$0FC1` is the same instruction and *must* render identically.
        """
        inert = set()
        for offset, word in sorted(self.cells.items()):
            for bit in (0, 8):
                path = pathlib.Path(self._dir.name) / "mutate.bin"
                mutated = dict(self.cells)
                mutated[offset] = word ^ (1 << bit)
                path.write_bytes(share(mutated))
                before = self.text(offset)
                after = self.text(offset, image=path)
                if before == after:
                    inert.add(offset)
        self.assertEqual(
            inert, {0x40050, 0x42e50},
            "the set of cells whose decode survives a one-bit mutation changed; "
            "if a new cell is inert the mutation control no longer covers it, "
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
        path = pathlib.Path(self._dir.name) / "oracle-shaped.bin"
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
                path = pathlib.Path(self._dir.name) / f"size-{size}.bin"
                path.write_bytes(b"\x00" * size)
                proc = run("--at", "0x40000", "--count", "2", image=path)
                self.assertNotEqual(proc.returncode, 0,
                                    f"a {size}-byte image was decoded")
                # The message must name the physical size and must not let the
                # window size pass as the share.
                self.assertIn("524288", proc.stderr)
                self.assertIn("WINDOW", proc.stderr)

    def test_a_missing_image_is_a_diagnosis_not_a_traceback(self):
        proc = run("--count", "1",
                   image=pathlib.Path(self._dir.name) / "does-not-exist.bin")
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("no image at", proc.stderr)
        self.assertNotIn("Traceback", proc.stderr)


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


if __name__ == "__main__":
    unittest.main()