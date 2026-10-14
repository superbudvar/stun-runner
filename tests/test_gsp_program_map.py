"""`tools/gsp-program-map` turns a disassembly into a function map.

Two of its assertions are about not inventing structure, which is the failure
mode that matters here. A linear decode runs happily past the end of the program
producing confident instructions, and a call target outside the mapped share can
be folded into an address that looks real and is not. Both produce output, and
neither raises.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOL = REPO / "tools" / "gsp-program-map"
IMAGE = pathlib.Path("/tmp/opencode/vramdump-recheck2/gsp_vram.bin")
TRACE = pathlib.Path("/tmp/opencode/fetch1/fetch.txt")


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), *args], capture_output=True, text=True,
                          check=False, timeout=900)


@unittest.skipUnless(IMAGE.is_file(), "no captured GSP VRAM image available")
class GspProgramMapTests(unittest.TestCase):
    def test_reports_a_code_boundary_from_the_trace(self):
        """Without a trace the tool must not claim to know where code ends."""
        proc = run(str(IMAGE), "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        document = json.loads(proc.stdout)
        self.assertIsNone(document["last_fetched_offset"])
        self.assertIsNone(document["trace"])

    @unittest.skipUnless(TRACE.is_file(), "no fetch trace available")
    def test_folded_targets_all_land_inside_the_share(self):
        """Every reported function must be a real share address.

        A CALLA target below 0xff800000 used to be masked into the share and
        printed with a VRAM label outside the mapped range -- a plausible
        address that does not exist.
        """
        proc = run(str(IMAGE), "--fetch-trace", str(TRACE), "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        document = json.loads(proc.stdout)
        for function in document["functions"]:
            with self.subTest(entry=hex(function["entry"])):
                self.assertTrue(
                    0xFF800000 <= function["vram"] <= 0xFFBFFFFC,
                    f"{function['entry']:#x} was labelled VRAM "
                    f"{function['vram']:#x}, outside the mapped share")
        self.assertTrue(
            document["out_of_share_calls"],
            "this program's CALLA E060DA4h is unresolvable and should be "
            "reported as out-of-share rather than folded")

    def test_refuses_a_file_that_is_not_a_fetch_trace(self):
        """A trace reader that accepts the wrong file reports an empty executed
        set, which reads as "the GSP never ran" rather than "bad input"."""
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
            handle.write("this is not a trace\nneither is this\n")
            bogus = handle.name
        try:
            proc = run(str(IMAGE), "--fetch-trace", bogus)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("no fetch records", proc.stderr)
        finally:
            pathlib.Path(bogus).unlink()


if __name__ == "__main__":
    unittest.main()