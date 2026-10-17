"""`tools/gsp-program-map` turns a disassembly into a function map.

Two of its assertions are about not inventing structure, which is the failure
mode that matters here. A linear decode runs happily past the end of the program
producing confident instructions, and a call target outside the mapped share can
be folded into an address that looks real and is not. Both produce output, and
neither raises.

SELF-CONTAINED, and that is the point
-------------------------------------
An earlier revision of this file pointed at absolute `/tmp/opencode/...` paths
for a 4 MiB capture and a 19 MB fetch trace, and guarded every test with
`skipUnless(IMAGE.is_file())`. That combination is worse than no test:

  * it reported as present while asserting nothing on every machine but the one
    whose /tmp still had the files;
  * the "fixture" was ROM-derived capture data living in a temp directory, which
    this repository does not treat as evidence;
  * and it pinned the WRONG SIZE. harddriv.cpp:628 maps a 4 MiB address WINDOW
    with a mirror mask; the physical share is 512 KiB (MAME's direct m_gsp_vram
    dump is 524288 bytes / 262144 words; Margolin S3 counts 8 banks of 64Kx8 on
    the MultiSync board; SP-338 S6 says "VRAM Array (16) 64K x 4"). Both this
    tool and `tools/disasm-gsp` were built on the window and so refused the
    oracle's own output.

So the fixture is synthesised here, in a temp dir, with no ROM data at all, and
the size regression is asserted directly.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOL = REPO / "tools" / "gsp-program-map"

# harddriv.cpp -> multisync_gsp_map:
#   map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram")
# The WINDOW is 4 MiB. The physical SHARE MAME allocates is 512 KiB.
SHARE_BASE = 0xFF800000
SHARE_BYTES = 0x80000
GSP_WINDOW_BYTES = 0x400000
SHARE_TOP = SHARE_BASE + SHARE_BYTES - 2      # a word, not a byte


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), *args], capture_output=True, text=True,
                          check=False, timeout=900)


class GspProgramMapTests(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._dir.cleanup)
        self.share = pathlib.Path(self._dir.name) / "gsp_vram.bin"
        self.share.write_bytes(bytes(SHARE_BYTES))

    def path(self, name: str) -> str:
        return str(pathlib.Path(self._dir.name) / name)

    # -- the regression this file exists for -------------------------------

    def test_accepts_the_512_kib_physical_share(self):
        """The oracle's own capture size must be accepted, with no complaint.

        Both this tool and `tools/disasm-gsp` previously demanded 4194304 bytes
        -- the address WINDOW -- and so refused every real capture this project
        produces, with a message claiming the 512 KiB form was "a different
        region". It is not: it is the share.
        """
        proc = run(str(self.share), "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stderr, "", "a correctly sized share produced a diagnostic")
        document = json.loads(proc.stdout)
        # The tool decodes a bounded window by default (--count), not the whole
        # share, so assert it decoded rather than pinning the count here.
        self.assertGreater(document["decoded_instructions"], 0)
        # An all-zero share is not a call graph: inventing targets here would be
        # the failure mode the tool exists to avoid.
        self.assertEqual(document["distinct_targets"], 0)
        self.assertEqual(document["out_of_share_calls"], {})

    def test_refuses_the_window_size_and_other_wrong_sizes(self):
        """4 MiB is the window, not the share, and must still be refused."""
        for size in (0, SHARE_BYTES // 2, SHARE_BYTES + 4, GSP_WINDOW_BYTES):
            with self.subTest(size=size):
                path = self.path(f"size-{size}.bin")
                pathlib.Path(path).write_bytes(bytes(size))
                proc = run(path, "--json")
                self.assertNotEqual(proc.returncode, 0,
                                    f"a {size}-byte image was accepted as the share")
                self.assertIn("524288", proc.stderr)

    # -- not inventing structure ------------------------------------------

    def test_reports_a_code_boundary_from_the_trace(self):
        """Without a trace the tool must not claim to know where code ends."""
        proc = run(str(self.share), "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        document = json.loads(proc.stdout)
        self.assertIsNone(document["last_fetched_offset"])
        self.assertIsNone(document["trace"])

    def test_every_reported_function_is_inside_the_512_kib_share(self):
        """No reported VRAM label may fall outside the physical share.

        A CALLA target below 0xff800000 used to be masked into the share and
        printed with a VRAM label outside the mapped range -- a plausible
        address that does not exist. The bound is the 512 KiB SHARE, not the
        4 MiB window.
        """
        trace = self.path("fetch.txt")
        pathlib.Path(trace).write_text(
            "# index time pc_bits pc_byteaddr opcode\n"
            "0 1000 00040000 400000 0000\n")
        proc = run(str(self.share), "--fetch-trace", trace, "--json")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        document = json.loads(proc.stdout)
        for function in document["functions"]:
            with self.subTest(entry=hex(function["entry"])):
                self.assertTrue(
                    SHARE_BASE <= function["vram"] <= SHARE_TOP,
                    f"{function['entry']:#x} was labelled VRAM "
                    f"{function['vram']:#x}, outside the {SHARE_BYTES}-byte "
                    f"physical share")

    def test_refuses_a_file_that_is_not_a_fetch_trace(self):
        """A trace reader that accepts the wrong file reports an empty executed
        set, which reads as "the GSP never ran" rather than "bad input"."""
        bogus = self.path("bogus.txt")
        pathlib.Path(bogus).write_text("this is not a trace\nneither is this\n")
        proc = run(str(self.share), "--fetch-trace", bogus)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("no fetch records", proc.stderr)


if __name__ == "__main__":
    unittest.main()