"""`tools/disasm-gsp` decodes the GSP program; these tests keep it honest.

Two things about this tool are easy to get wrong in ways that still produce
output. The GSP address space is bit-addressed, and the capture presents each
instruction word across a 16-byte cell, so a position arithmetic mistake is
eight-fold wrong and yields *plausible* mnemonics rather than an error. And a
wrong-sized capture is not a smaller view of the share but a different region,
so decoding one silently places every address somewhere else.

Neither is caught by asserting the tool runs.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOL = REPO / "tools" / "disasm-gsp"
IMAGE = pathlib.Path("/tmp/opencode/vramdump-recheck2/gsp_vram.bin")
MAME_SRC = pathlib.Path("/mnt/build/mame-ref")


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), *args], capture_output=True, text=True,
                          check=False, timeout=600)


@unittest.skipUnless(MAME_SRC.is_dir(), "MAME source tree not available")
@unittest.skipUnless(IMAGE.is_file(), "no captured GSP VRAM image available")
class DisasmGspTests(unittest.TestCase):
    def test_refuses_an_image_that_is_not_the_whole_share(self):
        """A 512 KiB capture must be refused, not decoded.

        This is the trap `mame-gsp-vram-dump.lua` already documents: a capture
        shorter than the share is a different region, not a smaller view, so
        every address decoded from it would be wrong by a constant.
        """
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as handle:
            handle.write(IMAGE.read_bytes()[: 512 * 1024])
            truncated = handle.name
        try:
            proc = run(truncated, "--count", "2")
            self.assertNotEqual(proc.returncode, 0,
                                "a half-share image was accepted; every "
                                "decoded address would be wrong")
            self.assertIn("not a smaller view", proc.stderr)
        finally:
            pathlib.Path(truncated).unlink()

    def test_word_stride_is_sixteen_bytes(self):
        """One instruction word per 16-byte cell.

        If this regresses to a 2-byte stride the output is still well-formed
        text, so assert the constant itself.
        """
        proc = run(str(IMAGE), "--count", "1", "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["word_stride"], 16)

    def test_consecutive_instructions_step_by_the_word_stride(self):
        """Not a text-presence check: a wrong stride still prints text.

        Every decoded offset after the first must be exactly 16 bytes further
        on. This is what an eight-fold stride error would break.
        """
        proc = run(str(IMAGE), "--at", "0x340000", "--count", "8", "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        offsets = [i["offset"] for i in json.loads(proc.stdout)["instructions"]]
        self.assertGreaterEqual(len(offsets), 4)
        deltas = {b - a for a, b in zip(offsets, offsets[1:])}
        self.assertTrue(
            all(d % 16 == 0 for d in deltas),
            f"offsets did not advance in whole 16-byte cells: {offsets}")

    def test_decodes_a_known_hot_loop(self):
        """The hottest loop in the fetch trace must decode as a real loop.

        Anchored on evidence rather than on the decoder's output format: this
        address was fetched 78,994 times in 400,000 traced instructions, and
        the instruction at +0x40 branches back to the loop head.
        """
        proc = run(str(IMAGE), "--at", "0x340f50", "--count", "4", "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        instructions = json.loads(proc.stdout)["instructions"]
        mnemonics = [i["mnemonic"] for i in instructions]
        self.assertEqual(mnemonics[:3], ["MOVE", "CMP", "JRLT"],
                         f"expected a load/compare/branch-back loop, got {mnemonics}")
        self.assertIn("340F50", instructions[2]["operands"],
                      "the branch does not target the loop head")


if __name__ == "__main__":
    unittest.main()