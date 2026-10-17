"""`tools/check-gsp-executed` partitions the GSP share by EXECUTION, not decode.

Two of its assertions are about not inventing a classification, which is the
failure mode that matters here. "MAME prints DW" is not a statement about
whether an address is an instruction, and "MAME prints a mnemonic" is not a
statement that it is one either -- a data word that happens to be a legal
encoding prints just as confidently as code. The only thing that settles it is
the address having been FETCHED, and that is the tool's whole input. A tool
that classified by decoding would report a comfortable number and be wrong in
the one direction that costs us a decoder.

THE NEGATIVE CONTROLS
---------------------
The two tests named `*_until_it_is_fetched` are the point of this file. Each
picks one synthetic word address and asks the tool about it twice: once with a
trace that does not fetch it, once with a trace that does. The address must
move between buckets, and it must move because of EXECUTION -- the share bytes
never change between the two runs. If either test could pass without the fetch
bit changing, the tool would be classifying by decoding and the whole exercise
would be MD-13 again.

`test_a_word_mame_cannot_decode_is_bucket_d_until_it_is_fetched` doubles as the
reachability control for bucket B: the measured corpus reports B empty, so a
tool that hard-coded B to zero would pass everything else here.

SELF-CONTAINED, and that is the point
--------------------------------------
No ROM data, no capture, no MAME run. The share is synthesised in a temp dir
and the fetch trace is written by hand, so the expected answers come from the
trace and the share bytes rather than from anything this repository recorded.
The 4 MiB size regression is asserted directly, because confusing the GSP
address window with the physical share is what made `tools/disasm-gsp` refuse
the oracle's own capture once already (harddriv.cpp:628 maps a 4 MiB WINDOW
with a mirror mask; the share MAME allocates is 512 KiB).
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOL = REPO / "tools" / "check-gsp-executed"

# harddriv.cpp -> multisync_gsp_map:
#   map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram")
# The WINDOW is 4 MiB. The physical SHARE MAME allocates is 512 KiB, which is
# also what tools/mame-gsp-vram-dump writes and what MAME's own direct
# m_gsp_vram dump reports (524288 bytes / 262144 words).
SHARE_BASE = 0xFF800000
SHARE_BYTES = 0x80000
GSP_WINDOW_BYTES = 0x400000
WORDS = SHARE_BYTES // 2

# Two synthetic words, chosen so their decode is checked rather than assumed:
# MAME's TMS34010 disassembler, driven by tools/disasm-gsp, renders word 0x8484
# as `MOVE *A4,A4,0` and word 0x0000 as `DW`. tests/test_disasm_gsp.py pins
# MAME's decoder against S9 [SPVU001A]; this file only needs the two to differ,
# and it needs the second to be the decoder's own "I have nothing" marker.
DECODING_WORD = 0x8484      # MAME renders a MOVE
UNDECODABLE_WORD = 0x0000   # MAME renders DW
CODE_OFFSET = 0             # holds DECODING_WORD
GAP_OFFSET = 2              # holds UNDECODABLE_WORD

# Word 0xB4B4 is a TWO-word MOVE in MAME's decoder
# (`MOVE *B5(9797h),B4,0`), so a linear sweep steps 32 bytes past it and leaves
# its parameter words undecoded. Placing it proves the tool recovers those by
# decoding them individually rather than inheriting a neighbour's answer.
TWO_WORD_OFFSET = 0x100
TWO_WORD_WORD = 0xB4B4


def run(*args: str, timeout: int = 900) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), *args], capture_output=True, text=True,
                          check=False, timeout=timeout)


def pc_bits(share_byte_offset: int) -> int:
    """The GSP bit address that folds onto a share byte offset.

    The program space is datawidth 16 / addrshift 3, so one address unit is one
    bit; the share is reached at ((pc - 0xff800000) >> 3) & 0x7ffff. The trace
    records the core's own BIT address, so a hand-written trace has to invert
    that or the tool folds it to a different word than the author meant.
    """
    return SHARE_BASE + (share_byte_offset << 3)


class GspExecutedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._dir = tempfile.TemporaryDirectory()
        cls.work = pathlib.Path(cls._dir.name)
        image = bytearray(SHARE_BYTES)
        image[CODE_OFFSET:CODE_OFFSET + 2] = DECODING_WORD.to_bytes(2, "little")
        image[GAP_OFFSET:GAP_OFFSET + 2] = UNDECODABLE_WORD.to_bytes(2, "little")
        image[TWO_WORD_OFFSET:TWO_WORD_OFFSET + 2] = TWO_WORD_WORD.to_bytes(2, "little")
        cls.share = cls.work / "gsp_vram.bin"
        cls.share.write_bytes(bytes(image))

    @classmethod
    def tearDownClass(cls) -> None:
        cls._dir.cleanup()

    def path(self, name: str) -> pathlib.Path:
        return self.work / name

    def write_trace(self, name: str, offsets: list[int],
                    opcodes: dict[int, int] | None = None,
                    marker: str | None = None) -> pathlib.Path:
        """A STUNRUN-FETCH trace, in the 5-field form the observer hook writes."""
        lines = [] if marker is None else [f"# {marker}"]
        for index, offset in enumerate(offsets):
            opcode = ((opcodes or {}).get(offset,
                                          (DECODING_WORD if offset == CODE_OFFSET
                                           else UNDECODABLE_WORD
                                           if offset == GAP_OFFSET
                                           else TWO_WORD_WORD)))
            bits = pc_bits(offset)
            lines.append(f"{index} 0 {bits:08x} {bits >> 3:08x} {opcode:04x}")
        target = self.path(name)
        target.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return target

    def document(self, *args: str) -> dict:
        proc = run(*args, "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return json.loads(proc.stdout)

    def bucket_of(self, document: dict, offset: int) -> str:
        """The single bucket a word address lands in, asserting it is exactly one."""
        found = [name for name in ("A", "B", "C", "D")
                 if offset in document[f"bucket_{name}"]["words"]]
        self.assertEqual(len(found), 1,
                         f"word 0x{offset:05x} landed in {found or 'no bucket'}")
        return found[0]

    # -- the shape of the answer ------------------------------------------

    def test_accepts_the_512_kib_physical_share(self):
        """The oracle's own capture size must be accepted, with no complaint."""
        document = self.document(str(self.share))
        self.assertEqual(document["schema"], "stunrun-gsp-executed-v1")
        self.assertEqual(document["image"]["bytes"], SHARE_BYTES)
        self.assertEqual(document["image"]["words"], WORDS)
        self.assertEqual(document["buckets_total"], WORDS)

    def test_refuses_the_window_size_and_other_wrong_sizes(self):
        """4 MiB is the window, not the share, and must still be refused."""
        for size in (0, SHARE_BYTES // 2, SHARE_BYTES + 4, GSP_WINDOW_BYTES):
            with self.subTest(size=size):
                target = self.path(f"size-{size}.bin")
                target.write_bytes(bytes(size))
                proc = run(str(target), "--json")
                self.assertNotEqual(proc.returncode, 0,
                                    f"a {size}-byte image was accepted as the share")
                self.assertIn("524288", proc.stderr)
                self.assertIn("WINDOW", proc.stderr)

    def test_refuses_a_file_that_is_not_a_fetch_trace(self):
        """An empty executed set reads as "the GSP never ran" rather than "bad input"."""
        bogus = self.path("bogus.txt")
        bogus.write_text("this is not a trace\nneither is this\n")
        proc = run(str(self.share), "--fetch-trace", str(bogus))
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("no fetch records", proc.stderr)

    def test_buckets_partition_every_word_address_exactly_once(self):
        """No word may be in two buckets, and none may be in none."""
        document = self.document(str(self.share))
        self.assertEqual(sum(document["buckets"].values()), WORDS)
        counted = sum(document[f"bucket_{name}"]["count"]
                      for name in ("A", "B", "C", "D"))
        self.assertEqual(counted, WORDS)
        self.assertEqual(document["decode"]["words"], WORDS)

    def test_multi_word_instruction_parameter_words_are_decoded_individually(self):
        """A linear sweep steps over parameter words; they still get a verdict.

        Skipping them would silently leave them in whichever bucket a
        pass happened to default to, and a skipped word is exactly what a
        'nothing is unknown' claim is usually made of.
        """
        document = self.document(str(self.share))
        self.assertGreater(document["decode"]["words_decoded_individually"], 0,
                           "no word needed an individual decode, so the fill-in "
                           "path was never exercised")
        self.assertEqual(document["decode"]["words_decoded_individually"]
                         + document["decode"]["results_from_linear_sweeps"],
                         WORDS)

    # -- THE NEGATIVE CONTROLS --------------------------------------------

    def test_a_word_that_decodes_is_bucket_c_until_it_is_fetched(self):
        """The share bytes do not change. Only the trace does. The bucket must.

        This is the whole thesis: a mnemonic is not a classification, and the
        classification has to move when EXECUTION moves.
        """
        fetched = self.write_trace("fetched-code.txt", [CODE_OFFSET])
        # A VALID trace that simply does not fetch this address. An empty file
        # would not do: the tool rejects a trace with no records, and rightly
        # so -- "the GSP never ran" and "this is not a trace" must not look the
        # same.
        absent = self.write_trace("absent-code.txt", [TWO_WORD_OFFSET])

        with_fetch = self.document(str(self.share), "--fetch-trace", str(fetched))
        without_fetch = self.document(str(self.share), "--fetch-trace", str(absent))

        self.assertIn(CODE_OFFSET, with_fetch["bucket_A"]["words"],
                      "a fetched word that MAME decodes is not bucket A")
        self.assertNotIn(CODE_OFFSET, with_fetch["bucket_C"]["words"])
        self.assertIn(CODE_OFFSET, without_fetch["bucket_C"]["words"],
                      "a word MAME decodes but the GSP never fetched is not bucket C")
        self.assertNotIn(CODE_OFFSET, without_fetch["bucket_A"]["words"])
        self.assertEqual(self.bucket_of(with_fetch, CODE_OFFSET), "A")
        self.assertEqual(self.bucket_of(without_fetch, CODE_OFFSET), "C")

    def test_a_word_mame_cannot_decode_is_bucket_d_until_it_is_fetched(self):
        """The D -> B flip. Bucket B is the one that changes what we implement.

        The measured corpus reports B empty, so without this test a tool that
        never produced a B would pass everything else here.
        """
        fetched = self.write_trace("fetched-gap.txt", [GAP_OFFSET])
        absent = self.write_trace("absent-gap.txt", [TWO_WORD_OFFSET])

        with_fetch = self.document(str(self.share), "--fetch-trace", str(fetched))
        without_fetch = self.document(str(self.share), "--fetch-trace", str(absent))

        self.assertIn(GAP_OFFSET, with_fetch["bucket_B"]["words"],
                      "a fetched word MAME cannot decode is not bucket B")
        self.assertNotIn(GAP_OFFSET, with_fetch["bucket_D"]["words"])
        self.assertIn(GAP_OFFSET, without_fetch["bucket_D"]["words"],
                      "an undecodable word the GSP never fetched is not bucket D")
        self.assertEqual(with_fetch["buckets"]["B"], 1)
        self.assertEqual(without_fetch["buckets"]["B"], 0)
        self.assertEqual(self.bucket_of(with_fetch, GAP_OFFSET), "B")
        self.assertEqual(self.bucket_of(without_fetch, GAP_OFFSET), "D")

    def test_bucket_b_reports_an_address_list_and_contiguous_ranges(self):
        """B's addresses must be readable without re-running the tool."""
        fetched = self.write_trace("fetched-gap.txt", [GAP_OFFSET])
        document = self.document(str(self.share), "--fetch-trace", str(fetched))
        self.assertEqual(document["bucket_B"]["words"], [GAP_OFFSET])
        self.assertEqual(document["bucket_B"]["ranges"], [[GAP_OFFSET, GAP_OFFSET + 2]])
        self.assertEqual(document["bucket_B"]["words_omitted"], 0)

    # -- the address fold, and refusing to be wrong quietly ----------------

    def test_the_fold_is_verified_against_the_trace_opcodes(self):
        """A fold that is off by a factor of eight must not pass silently.

        Every record's opcode is compared with the word in the image at the
        folded offset. That is what distinguishes 'this fold is right' from
        'this fold produced addresses', which a decoder will happily do.
        """
        fetched = self.write_trace("fetched-gap.txt", [GAP_OFFSET])
        document = self.document(str(self.share), "--fetch-trace", str(fetched))
        fold = document["address_fold"]
        self.assertTrue(fold["verified"], fold)
        self.assertEqual(fold["opcode_mismatch"], 0)
        self.assertEqual(fold["opcode_match"], 1)
        self.assertIn(">> 3", fold["rule"])

    def test_a_trace_whose_opcodes_disagree_with_the_image_is_flagged(self):
        """A trace and an image from different runs are not a pair. Say so."""
        fetched = self.write_trace("bad-opcode.txt", [GAP_OFFSET],
                                   opcodes={GAP_OFFSET: 0x1234})
        document = self.document(str(self.share), "--fetch-trace", str(fetched))
        fold = document["address_fold"]
        self.assertFalse(fold["verified"], "a mismatching trace was accepted as verified")
        self.assertEqual(fold["opcode_mismatch"], 1)
        self.assertTrue(fold["mismatch_examples"])

    def test_the_hocks_truncation_marker_is_reported(self):
        """A truncated trace makes C and D lower bounds, and the tool must say so.

        Without this, a bounded window reads as 'the GSP never fetched these',
        which is a claim about the machine made from a claim about the capture.
        """
        bounded = self.write_trace("bounded.txt", [GAP_OFFSET],
                                   marker="TRUNCATED at 1 fetches")
        document = self.document(str(self.share), "--fetch-trace", str(bounded))
        self.assertTrue(document["trace"]["truncated_by_hook"])
        self.assertIsNotNone(document["trace"]["truncation_note"])
        complete = self.write_trace("complete.txt", [GAP_OFFSET])
        document = self.document(str(self.share), "--fetch-trace", str(complete))
        self.assertFalse(document["trace"]["truncated_by_hook"])
        self.assertIsNone(document["trace"]["truncation_note"])

    def test_a_trace_time_column_that_never_moves_is_counted_not_believed(self):
        """The hook's timestamp is frozen in every trace measured so far.

        A tool that windowed a trace by time would therefore silently window it
        at a single instant. The count is reported so the limitation is visible
        in the document rather than in a docstring nobody reads.
        """
        fetched = self.write_trace("fetched-gap.txt", [GAP_OFFSET])
        document = self.document(str(self.share), "--fetch-trace", str(fetched))
        self.assertEqual(document["trace"]["distinct_time_values"], 1)
        self.assertEqual(document["trace"]["unaligned_records"], 0)


if __name__ == "__main__":
    unittest.main()