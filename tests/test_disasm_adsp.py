"""`tools/disasm-adsp` decodes the uploaded ADSP-2100 program; these keep it honest.

Three things about this tool fail silently rather than loudly.

1. The address arithmetic. An ADSP program address is a 32-bit bus access that
   holds exactly one 24-bit instruction, so the mapping from file byte offset
   to instruction is 4 bytes, big-endian, and the 24-bit instruction lives in
   bits 23:0 of that word. A wrong factor here — 2 bytes, or a
   little-endian word, or treating the address as a byte offset — still prints
   well-formed mnemonics; it just decodes the wrong instructions.

2. Where the extension bits of a 24-bit instruction come from. MAME reads one
   `r32(pc)` and puts the opcode class in bits 23:16, i.e. the *second* byte
   of the word, not the first. A reader who assumes big-endian instruction
   layout rather than big-endian *container* layout gets a confident wrong
   answer. `test_mutation_changes_the_decode` exists for exactly this.

3. The capture contract. A capture of the wrong length is not a smaller view
   of the ADSP program RAM, it is a different container, and decoding it
   anyway would place every address somewhere else. The upload's own record
   structure has an explicit destination gap
   (`reference/experiments/stunrun/adsp-program-upload.metadata.json`), so a
   short image means "only this much was written", not "the rest is zero".

None of these is caught by asserting the tool runs and prints something.
"""

from __future__ import annotations

import json
import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOL = REPO / "tools" / "disasm-adsp"
MAME_SRC = pathlib.Path("/mnt/build/mame-ref")
COMPILER = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")

# harddriv.cpp:680-685 maps ADSP program addresses 0x0000-0x1fff to internal RAM
# and 0x2000-0x3fff to nopr(); tools/mame-adsp-program-dump writes one 32-bit
# word per address, big-endian, via struct.pack(">I", space:read_u32(addr)).
PROGRAM_ADDRESSES = 0x2000
IMAGE_BYTES = PROGRAM_ADDRESSES * 4

# Words taken from records this repository already holds. These are the
# fixtures, not the expectations -- the expectations are MAME's own recorded
# instruction text, quoted in the tests below.
#
#   reference/experiments/stunrun/m3-adsp-program-image.metadata.json
#     readback_first_word  = 0x000A001F  (program address 0)
#     readback_entry_word  = 0x001C780F  (program address 0x0004, entry)
#   tools/build-replacement-image  adsp_init_prefix()
#     words[0x0005] = 0x001C834F
WORD_FIRST = 0x000A001F
WORD_ENTRY = 0x001C780F
WORD_SECOND_CALL = 0x001C834F


def image(words: dict[int, int]) -> bytes:
    """A whole-share capture carrying `words` at their program addresses."""
    table = [0] * PROGRAM_ADDRESSES
    for address, word in words.items():
        table[address] = word
    return b"".join(struct.pack(">I", word) for word in table)


def landmark_image() -> bytes:
    """The recorded reset-vector neighbourhood, at its recorded addresses."""
    return image({
        0x0000: WORD_FIRST,
        0x0004: WORD_ENTRY,
        0x0005: WORD_SECOND_CALL,
        0x0006: 0x0018006F,   # JUMP $0006
    })


@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(COMPILER, "no C++ compiler available")
class DisasmAdspTests(unittest.TestCase):
    """`tests/test_disasm_gsp.py` is the precedent; the same three traps, same shape."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix="disasm-adsp-tests-")
        cls.landmarks = pathlib.Path(cls.dir.name) / "landmarks.bin"
        cls.landmarks.write_bytes(landmark_image())

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def write(self, name: str, payload: bytes) -> pathlib.Path:
        path = pathlib.Path(self.dir.name) / name
        path.write_bytes(payload)
        return path

    def run_tool(self, *args: str) -> subprocess.CompletedProcess:
        proc = subprocess.run([str(TOOL), *args], capture_output=True,
                              text=True, check=False, timeout=600)
        return proc

    def decode(self, payload: bytes, *args: str) -> dict:
        """Run the tool over `payload` and return the parsed --json document."""
        path = self.write("case.bin", payload)
        proc = self.run_tool(str(path), *args, "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return json.loads(proc.stdout)

    # -- the recorded landmarks ------------------------------------------------

    def test_decodes_the_recorded_reset_entry(self):
        """Program addresses 0x0004 and 0x0005 decode to `CALL $0780`, `CALL $0834`.

        The expectations are MAME's own instruction text, recorded independently
        of this tool: `reference/experiments/stunrun/processor-trace-600.metadata.json`
        records the first non-NOP ADSP instruction of a 600-frame run as
        `first_non_nop_pc: "0x0004"` / `first_non_nop_instruction: "CALL $0780"`,
        produced by MAME's debugger disassembler over the live chip, and
        `analysis/m3-adsp-reset-entry.md` records `0x0004: CALL $0780` followed
        by `0x0005: CALL $0834` from a bounded trace. The dwords fed in here are
        the captured image's own record of those addresses,
        `readback_entry_word: "0x001C780F"` in
        `m3-adsp-program-image.metadata.json`, and the encoding
        `tools/build-replacement-image`'s `init-prefix` fixture writes at 0x0005.

        The sweep runs for two instructions on purpose. A single-instruction
        sweep would still pass if the tool advanced the program address by the
        wrong amount; asking for the pair pins the advance to one address per
        instruction as well.

        If this fails, the addressing is wrong. Do not relax the expectation.
        """
        document = self.decode(landmark_image(), "--at", "0x4", "--count", "2")
        instructions = document["instructions"]
        self.assertEqual(len(instructions), 2)
        self.assertEqual([i["pc"] for i in instructions], [0x0004, 0x0005])
        self.assertEqual([i["text"] for i in instructions],
                         ["CALL $0780", "CALL $0834"])
        entry = instructions[0]
        self.assertEqual(entry["word"], WORD_ENTRY)
        self.assertEqual(entry["instruction"], WORD_ENTRY & 0xFFFFFF)
        self.assertEqual(entry["byte_offset"], 0x10)

    def test_decodes_the_recorded_second_reset_call(self):
        """Program address 0x0005 decodes to `CALL $0834`.

        `analysis/m3-adsp-reset-entry.md` and `analysis/interconnect.md` record
        the retained startup landmarks as `0x0004: CALL $0780` followed by
        `0x0005: CALL $0834`, from a bounded MAME trace of the original code.
        The dword is the one `tools/build-replacement-image`'s `init-prefix`
        fixture encodes for that address, so the *word* here is repo-authored;
        the *expected text* is the recorded trace. Asserting the text is what
        makes this a test of the decoder rather than of the encoder.
        """
        document = self.decode(landmark_image(), "--at", "0x5", "--count", "1")
        self.assertEqual(document["instructions"][0]["text"], "CALL $0834")

    def test_word_zero_is_a_return_not_the_reset_entry(self):
        """Program address 0x0000 is `RTI`, so the entry is 0x0004 and not 0.

        `STATUS.md` and `analysis/interconnect.md` both record the reset entry as
        0x0004; `m3-adsp-program-image.metadata.json` records the captured word
        at address 0 as `readback_first_word: "0x000A001F"`. A tool that
        started its sweep at 0 and treated the first word as the entry would
        report the reset path as an `RTI`, which is still plausible text.
        """
        document = self.decode(landmark_image(), "--at", "0x0", "--count", "1")
        self.assertEqual(document["instructions"][0]["text"], "RTI")
        # And the same sweep must reach the calls without being told to.
        document = self.decode(landmark_image(), "--at", "0x0", "--count", "8")
        texts = [i["pc"] for i in document["instructions"]]
        self.assertEqual(texts, list(range(8)))
        self.assertEqual([i["text"] for i in document["instructions"][4:6]],
                         ["CALL $0780", "CALL $0834"])

    def test_zero_word_decodes_as_nop(self):
        """An all-zero capture is all NOP, which is the frame-136 boundary.

        `m3-adsp-program-image.metadata.json` records frame 136 as
        `nonzero_words: 0` and keeps it as the "program RAM is still all zero"
        negative boundary. A sweep over it must say so rather than error.
        """
        document = self.decode(image({}), "--at", "0x0", "--count", "4")
        self.assertEqual([i["text"] for i in document["instructions"]],
                         ["NOP"] * 4)

    # -- the address arithmetic -----------------------------------------------

    def test_byte_offset_is_four_times_the_program_address(self):
        """Program address A lives at file byte offset 4*A, big-endian.

        Stated rather than assumed, because this is the correspondence the
        recorded `readback_entry_word` / `destination_word_index` numbers use.
        A 2-byte or byte-granular reading of the container is the exact mistake
        the GSP counterpart of this tool already made once.
        """
        document = self.decode(landmark_image(), "--at", "0x4", "--count", "4")
        for instruction in document["instructions"]:
            self.assertEqual(instruction["byte_offset"],
                             instruction["pc"] * 4)
        # The captured word at address 4 is at file offset 16..19, i.e. the
        # bytes 00 1c 78 0f. If the container were little-endian this would
        # read 0f782c00 and decode to something else entirely.
        self.assertEqual(document["instructions"][0]["word"], WORD_ENTRY)

    def test_one_program_address_is_one_instruction(self):
        """Two different words at 4 and 5 must decode to two different texts.

        A stride or address-arithmetic error would decode the same word twice
        and still print text, so this asserts the *decodes differ*, not that
        output exists.
        """
        document = self.decode(landmark_image(), "--at", "0x4", "--count", "2")
        instructions = document["instructions"]
        self.assertEqual([i["pc"] for i in instructions], [0x0004, 0x0005])
        self.assertEqual([i["text"] for i in instructions],
                         ["CALL $0780", "CALL $0834"])
        self.assertNotEqual(instructions[0]["word"], instructions[1]["word"])

    def test_opcode_alignment_is_one(self):
        """MAME's `opcode_alignment()` is 1, reported rather than asserted here.

        `2100dasm.cpp:565-568` returns 1. It matters because MAME's debug
        disassembly buffer picks its layout from it
        (`src/emu/debug/debugbuf.cpp:372-379`): a 32-bit-granular space with
        alignment 1 gets one `u32` per program address, which is what this
        tool's membuf provides. Had it been 2 or 4 the buffer would be filled
        and read differently and every address here would be wrong.
        """
        document = self.decode(landmark_image(), "--at", "0x4", "--count", "1")
        self.assertEqual(document["opcode_alignment"], 1)

    def test_length_is_mame_own_value(self):
        """The reported `length` is 1, straight out of MAME's return value.

        `2100dasm.cpp:562` returns `1 | dasmflags | SUPPORTED` unconditionally,
        so there is no instruction-length annotation to read on this part. The
        JSON reports MAME's number rather than a constant so a future MAME that
        made it variable would show up as a change here.
        """
        document = self.decode(landmark_image(), "--at", "0x4", "--count", "3")
        self.assertEqual([i["length"] for i in document["instructions"]], [1, 1, 1])
        self.assertEqual(document["instructions"][0]["flags"],
                         ["supported", "step_over"])

    def test_sweep_stops_at_the_end_of_program_ram(self):
        """A count that runs past 0x1fff stops there; it does not wrap or invent."""
        document = self.decode(landmark_image(), "--at", "0x1ffe", "--count", "8")
        self.assertEqual([i["pc"] for i in document["instructions"]],
                         [0x1FFE, 0x1FFF])

    # -- negative controls -----------------------------------------------------

    def test_mutation_changes_the_decode(self):
        """Each byte of the instruction must matter, and change the text.

        This is the control that makes the assertions above meaningful. Three
        single-byte mutations of the word at program address 0x0004:

          file offset 19 (bits  3..0, the condition field)  -> `IF NOT CE` prefix
          file offset 17 (bits 23..16, the opcode class)    -> CALL becomes JUMP
          file offset 18 (bits 15..8, part of the target)   -> target becomes $0000

        If mutating bytes leaves the decoded text unchanged, the membuf is not
        reading what this test thinks it is and every landmark assertion above
        is passing for the wrong reason. Each mutation must also leave the
        *neighbouring* instruction alone, so a shifted read cannot hide behind a
        wholesale change.
        """
        base = landmark_image()
        reference = self.decode(base, "--at", "0x4", "--count", "2")
        self.assertEqual([i["text"] for i in reference["instructions"]],
                         ["CALL $0780", "CALL $0834"])

        cases = {
            19: (0x0E, "IF NOT CE CALL $0780",
                 "bits 3..0 are the ADSP condition field; 15 is 'always' and "
                 "14 is 'IF NOT CE', so this must acquire a condition prefix"),
            17: (0x18, "JUMP $0780",
                 "bits 23..16 are the opcode class and 0x1c is a call while "
                 "0x18 is a jump; the same target must now decode as JUMP"),
            18: (0x00, "CALL $0000",
                 "bits 15..8 carry the low half of the 14-bit target, so "
                 "clearing this byte must move the target, not the opcode"),
        }
        for offset, (replacement, expected, why) in cases.items():
            with self.subTest(file_offset=offset):
                mutated = bytearray(base)
                mutated[offset] = replacement
                document = self.decode(bytes(mutated), "--at", "0x4", "--count", "2")
                instructions = document["instructions"]
                self.assertEqual(instructions[0]["text"], expected, why)
                # The neighbour is untouched: a mis-sized read would move it too.
                self.assertEqual(instructions[1]["text"], "CALL $0834")
                self.assertEqual(instructions[1]["word"], WORD_SECOND_CALL)

    def test_conditional_prefix_survives_json(self):
        """MAME emits `IF`/`DO` as leading words; `text` must keep them intact.

        There is deliberately no `mnemonic` field: a first-word split would
        read `IF` or `DO` on exactly the instructions worth reading. The tool's
        own docstring quotes two of these shapes as its examples, so they are
        asserted here rather than left as prose: a conditional CALL
        (`2100dasm.cpp:450`), a `DO ... UNTIL` (`:444`), and a conditional
        ALU/MAC whose condition is a leading word on its own (`:460`). A naive
        whitespace split would mangle all three differently.
        """
        payload = image({
            0x0000: 0x001C780E,   # condition field 14 -> "IF NOT CE"
            0x0001: 0x0014123F,   # opcode class 0x14 -> DO ... UNTIL FOREVER
            0x0002: 0x00202000,   # opcode class 0x20 -> conditional ALU/MAC
        })
        document = self.decode(payload, "--at", "0x0", "--count", "3")
        self.assertEqual([i["text"] for i in document["instructions"]], [
            "IF NOT CE CALL $0780",
            "DO $0123 UNTIL FOREVER",
            "IF EQ MR = MX0 * MY0 (RND)",
        ])
        # step_cond is set only on the conditional CALL; a DO loop is not
        # reported as conditionally transferring control by this decoder.
        self.assertIn("step_cond", document["instructions"][0]["flags"])
        self.assertNotIn("step_cond", document["instructions"][1]["flags"])
        self.assertNotIn("step_cond", document["instructions"][2]["flags"])

    # -- the capture contract --------------------------------------------------

    def test_refuses_a_truncated_capture(self):
        """A capture one word short must be refused, not decoded."""
        path = self.write("short.bin", landmark_image()[:-4])
        proc = self.run_tool(str(path), "--count", "2")
        self.assertNotEqual(proc.returncode, 0,
                            "a capture one program address short was accepted; "
                            "the upload's destination gap means a short image "
                            "means 'only this much was written', not 'the rest "
                            "is zero'")
        self.assertIn("expected a 32768-byte image", proc.stderr)
        self.assertEqual(proc.stdout, "", "a refused capture still produced output")

    def test_refuses_an_oversized_capture(self):
        """A capture one word long must also be refused."""
        path = self.write("long.bin", landmark_image() + b"\0" * 4)
        proc = self.run_tool(str(path), "--count", "2")
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("expected a 32768-byte image", proc.stderr)

    def test_refuses_a_capture_that_is_not_whole_words(self):
        """A file that is not a whole number of 32-bit words gets its own message."""
        path = self.write("ragged.bin", landmark_image() + b"\0")
        proc = self.run_tool(str(path), "--count", "2")
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("not a multiple of 4", proc.stderr)

    def test_refuses_a_missing_capture(self):
        proc = self.run_tool(str(pathlib.Path(self.dir.name) / "absent.bin"))
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("no image at", proc.stderr)

    def test_image_size_matches_the_share_the_driver_maps(self):
        """The accepted size is 8192 addresses x 4 bytes, stated in the document.

        `harddriv.cpp:683` maps 0x0000-0x1fff to the `adsp_pgm_memory` share
        and `:684` maps 0x2000-0x3fff to `nopr()`, so 8192 addresses is the
        whole decodable region, not an arbitrary round number. The document
        carries the size it actually read so a reader does not have to take the
        constant on trust.
        """
        document = self.decode(landmark_image(), "--at", "0x0", "--count", "1")
        self.assertEqual(document["image_bytes"], IMAGE_BYTES)
        self.assertEqual(document["program_addresses"], PROGRAM_ADDRESSES)
        self.assertEqual(document["bytes_per_program_address"], 4)
        self.assertEqual(document["schema"], "stunrun-disasm-adsp-v1")


if __name__ == "__main__":
    unittest.main()