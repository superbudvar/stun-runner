"""ROM-free checks for the GSP program-upload boundary capture and image diff.

The traps these pin down are all ones this repository has already paid for:

* MAME hands a write tap the FULL address, not a range-relative offset. Getting
  that wrong records nothing while the run still reports success.
* Retaining per-event Lua tables, or writing files, from inside a memory-tap
  callback segfaults this MAME build; only a preallocated ring is safe.
* Reading the shared VRAM from a frame-done callback SILENTLY stops those taps.
  A tool that also dumps VRAM and then reports "no host-port traffic" would be
  reporting its own instrumentation failure as a fact about the board.
* The boundary is an HSTCTL halt release that FOLLOWS host data writes, not the
  /GSPRES release, which happens once at power-on before the upload.
* Neither image is "the GSP program", and their difference is the GSP's first
  output rather than the upload.
"""

import importlib.machinery
import importlib.util
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "tools" / "capture-gsp-program-upload"
DIFF = ROOT / "tools" / "diff-gsp-upload-images"
LUA = ROOT / "mame" / "lua" / "gsp-program-upload.lua"


def load_tool(path, name):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def sample_log(**overrides):
    fields = {
        "release": "M1_GSP_UPLOAD_RELEASE frame=405 seq=631205 pc=0002C144 "
                   "hstctl=00007800 hstdat=627943",
        "taps": "M1_GSP_UPLOAD_TAPS frame=1 seq=0",
        "resetline": "M1_GSP_UPLOAD_RESETLINE frame=362 seq=511798 pc=0002237A "
                     "line=release hstdat=511761",
        "sample": "M1_GSP_UPLOAD_SAMPLE frame=406 pc=FFF4FFF0 st=00220010",
        "pcstat": "M1_GSP_UPLOAD_PC kind=HSTDAT pc=0002C144 count=627943 "
                  "first_frame=31 last_frame=405 data_first=0000FFFF "
                  "dest_first=FF800000 data_last=0000FFFF dest_last=FFBFFFF0",
        "done": "M1_GSP_UPLOAD_DONE frames=600 seq=701877 stored=400000 "
                "dropped=301877 release_seq=631205 release_frame=405 "
                "hstdat_writes=697202 dest_lo=00000000 dest_hi=FF800000 "
                "tap_installs=1",
    }
    fields.update(overrides)
    return "\n".join(value for value in fields.values() if value) + "\n"


class ProgramUploadCaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool(CAPTURE, "capture_gsp_program_upload")

    def test_parses_boundary_writers_and_tap_evidence(self):
        parsed = self.tool.parse_log(sample_log())
        self.assertEqual(parsed["release"]["frame"], 405)
        self.assertEqual(parsed["release"]["hstdat_writes_before_release"], 627943)
        self.assertEqual(parsed["release"]["hstctl_at_release"], 0x7800)
        self.assertEqual(parsed["done"]["dropped"], 301877)
        # Overflow must stay visible: a truncated stream looks identical to a
        # matching one unless the loss is reported.
        self.assertEqual(parsed["done"]["tap_installs"], 1)
        self.assertEqual(parsed["taps_installed"]["seq_at_install"], 0)
        self.assertEqual(parsed["writer_pc_stats"][0]["kind"], "HSTDAT")
        self.assertEqual(parsed["writer_pc_stats"][0]["destination_first"], 0xFF800000)
        self.assertEqual(parsed["post_release_samples"][0]["pc"], 0xFFF4FFF0)

    def test_absent_release_is_not_a_boundary(self):
        parsed = self.tool.parse_log(sample_log(release=""))
        self.assertIsNone(parsed["release"])

    def test_lua_error_invalidates_capture(self):
        self.assertEqual(self.tool.find_lua_errors(
            "[LUA ERROR] install_execute_tap\n"),
            ["[LUA ERROR] install_execute_tap"])
        self.assertEqual(self.tool.find_lua_errors(sample_log()), [])

    def test_tap_addresses_are_full_addresses_not_range_offsets(self):
        # MAME passes the absolute address to the callback. Comparing against
        # 0x02/0x04/0x08/0x0c records nothing and still looks like a clean run.
        text = LUA.read_text(encoding="utf-8")
        self.assertIn("HSTADRH_ADDR = 0xc00002", text)
        self.assertIn("HSTADRL_ADDR = 0xc00004", text)
        self.assertIn("HSTCTL_ADDR = 0xc00008", text)
        self.assertIn("HSTDAT_ADDR = 0xc0000c", text)
        for wrong in ("address == 0x02", "address == 0x04",
                      "address == 0x08", "address == 0x0c"):
            self.assertNotIn(wrong, text)

    def test_destination_applies_the_post_increment_not_the_raw_latch(self):
        # Reading the HSTADR latch reports one repeated address for a whole
        # contiguous upload, because the driver moves the latch by +0x10 after
        # each store when HSTCTL 0x0800 is set. That makes a "which range did
        # this writer fill" question unanswerable.
        text = LUA.read_text(encoding="utf-8")
        self.assertIn("local effective = hstadr & 0xfffffff0", text)
        self.assertIn("if (hstctl & 0x0800) ~= 0 then hstadr = hstadr + 0x10 end", text)
        self.assertIn("hstadr = (data << 16) | (hstadr & 0xffff)", text)
        self.assertIn("hstadr = (hstadr & 0xffff0000) | data", text)
        self.assertNotIn("((hstadr_hi << 16) | hstadr_lo) & 0xfffffff0", text)

    def test_boundary_is_halt_release_after_data_writes(self):
        # /GSPRES is released once at power-on, before the upload. Capturing
        # there yields an all-zero image that looks like a valid capture.
        text = LUA.read_text(encoding="utf-8")
        self.assertIn("was_halted and not halts and hstdat_writes > 0", text)
        # And this script must not read VRAM at all: reading the share drops the
        # pass-through handler, silently, so the tap would report nothing and the
        # run would still look clean.
        self.assertNotIn("read_vram", text)
        self.assertNotIn("gsp_space:read_u16", text)

    def test_events_use_a_preallocated_ring_not_growth(self):
        text = LUA.read_text(encoding="utf-8")
        self.assertIn("for index = 1, cap * WIDTH do buf[index] = 0 end", text)
        # `emit` is the per-event path. Pass-through callbacks are not coroutines
        # and fire once per access, so anything per-event is a hazard by
        # construction. Extract the WHOLE body: stopping at the first `end` would
        # silently inspect only the overflow check.
        body = text.split("local function emit(")[1]
        emit = body.split("\nlocal function ")[0]
        self.assertNotIn("string.format", emit)
        self.assertNotIn("file:write", emit)
        self.assertNotIn("io.open", emit)
        # No table constructor either: growing or allocating is the failure.
        self.assertNotIn("{", emit)
        self.assertIn("buf[b + 11] = a2.value", emit)
        # The once-per-run boundary print is fine; it must not be per event.
        self.assertIn("dropped = dropped + 1", emit)
        self.assertIn("if stored >= cap then", emit)

    def test_tap_drop_is_documented_not_a_mame_defect(self):
        # These were filed as BUGS.md MD-10/MD-11 and withdrawn. The symptom is
        # real but MAME documents the cause and the remedy, so claiming a defect
        # would file a false upstream report.
        text = LUA.read_text(encoding="utf-8")
        self.assertIn("ref-mem.rst:197-200", text)
        self.assertIn("passthrough:reinstall()", text)
        self.assertIn("is not an emulator defect", text)
        bugs = (ROOT / "BUGS.md").read_text(encoding="utf-8")
        self.assertNotIn("### MD-10", bugs)
        self.assertNotIn("### MD-11", bugs)
        # The withdrawal is recorded in place, not deleted: BUGS.md entries are
        # the ones that get reported upstream.
        self.assertIn("Withdrawn 2026-10-13", bugs)

    def test_tool_refuses_a_silent_capture(self):
        # A run that installs taps and then records nothing is this tool's own
        # failure mode, not evidence that the 68010 never used the host port.
        tool = CAPTURE.read_text(encoding="utf-8")
        self.assertIn('parsed["done"]["stored"] == 0', tool)
        self.assertIn("no HSTCTL halt release following host data writes", tool)
        self.assertIn("find_lua_errors(text)", tool)
        # Phase 2 must go through the proven consecutive-share reader rather
        # than re-implementing a VRAM read inside the tap script.
        self.assertIn('with_name("mame-gsp-vram-dump")', tool)
        self.assertIn('"-nvram_directory"', tool)

    def test_boundary_beyond_the_ring_is_reported_not_truncated_away(self):
        # The ring keeps the FIRST cap events. A 600-frame run needs 701877 of
        # them, so a cap of 400000 puts the release (event 631205) outside the
        # retained window: the log then stops before the boundary and still looks
        # like a complete capture.
        tool = CAPTURE.read_text(encoding="utf-8")
        self.assertIn("boundary_beyond_ring", tool)
        self.assertIn("the event log stops before the boundary", tool)
        self.assertIn("M1_GSP_UPLOAD_TRUNCATED", LUA.read_text(encoding="utf-8"))
        parsed = self.tool.parse_log(sample_log(
            done="M1_GSP_UPLOAD_TRUNCATED release_seq=631205 stored=400000"))
        self.assertEqual(parsed["boundary_beyond_ring"],
                         {"release_seq": 631205, "stored": 400000})
        # A capture that retained the boundary reports nothing here.
        self.assertIsNone(self.tool.parse_log(sample_log())["boundary_beyond_ring"])

    def test_lua_compiles(self):
        luac = shutil.which("luac")
        if luac is None:
            self.skipTest("luac not installed")
        proc = subprocess.run([luac, "-p", str(LUA)], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)


class UploadImageDiffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool(DIFF, "diff_gsp_upload_images")

    def write_image(self, directory, name, words):
        path = directory / name
        path.write_bytes(b"".join(word.to_bytes(2, "little") for word in words))
        return path

    def test_reports_first_output_as_ranges_and_never_as_the_upload(self):
        before = [0] * 16
        after = list(before)
        after[4:8] = [0x7F00] * 4          # one contiguous fill run
        after[12] = 0x0001                # a lone word elsewhere
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            summary = self.tool.summarize(
                self.write_image(temp, "before.bin", before),
                self.write_image(temp, "after.bin", after))
        self.assertEqual(summary["changed_words"], 5)
        self.assertEqual(summary["changed_range_count"], 2)
        self.assertEqual(summary["words_newly_nonzero"], 5)
        self.assertEqual(summary["words_modified"], 0)
        first = summary["changed_ranges"][0]
        self.assertEqual(first["first_word_offset"], 4)
        self.assertEqual(first["last_word_offset"], 7)
        self.assertEqual(first["word_count"], 4)
        self.assertEqual(first["before_sample"], [0, 0, 0, 0])
        self.assertEqual(first["after_sample"], [0x7F00] * 4)
        # Both address forms, so a reader does not redo the arithmetic wrong.
        self.assertEqual(first["vram_address_range"],
                         [0xFF800000 + 8, 0xFF800000 + 16])
        self.assertEqual(first["logical_bit_address_range"], [64, 128])
        self.assertIn("not the upload", summary["interpretation_limits"]["meaning"])
        self.assertIn("pre-upload /GSPRES release",
                      summary["interpretation_limits"]["before_image"])

    def test_separates_fills_from_modifications(self):
        before = [0x1111, 0x2222, 0x3333]
        after = [0x1111, 0x0000, 0x4444]
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            summary = self.tool.summarize(
                self.write_image(temp, "before.bin", before),
                self.write_image(temp, "after.bin", after))
        self.assertEqual(summary["words_newly_zero"], 1)
        self.assertEqual(summary["words_modified"], 1)
        self.assertEqual(summary["changed_words"], 2)

    def test_identical_images_report_nothing_changed(self):
        words = [0, 1, 2, 3]
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            summary = self.tool.summarize(
                self.write_image(temp, "a.bin", words),
                self.write_image(temp, "b.bin", words))
        self.assertEqual(summary["changed_words"], 0)
        self.assertEqual(summary["changed_ranges"], [])
        self.assertEqual(summary["unchanged_words"], 4)

    def test_mismatched_sizes_fail_loudly(self):
        # Comparing a 512 KiB page against a 4 MiB share would "succeed" and
        # report 512 KiB of deletions. That is how the retired fixture went
        # unnoticed.
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            with self.assertRaises(SystemExit):
                self.tool.summarize(self.write_image(temp, "a.bin", [0] * 4),
                                    self.write_image(temp, "b.bin", [0] * 8))

    def test_odd_byte_length_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            odd = temp / "odd.bin"
            odd.write_bytes(b"\x00\x00\x01")
            with self.assertRaises(SystemExit):
                self.tool.load_words(odd)


if __name__ == "__main__":
    unittest.main()