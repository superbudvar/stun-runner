"""Tests for bounded MAME process lifecycle and display options."""

import pathlib
import shutil
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HARNESS = ROOT / "mame_harness"


class MameProcessTests(unittest.TestCase):
    def test_timeout_reaps_process_group(self):
        sys.path.insert(0, str(HARNESS))
        from process import run_mame

        with self.assertRaises(subprocess.TimeoutExpired):
            run_mame([sys.executable, "-c", "import time; time.sleep(60)"], timeout=0.01)

    @unittest.skipUnless(shutil.which("Xvfb") and shutil.which("xdpyinfo"),
                         "Xvfb and xdpyinfo are required")
    def test_isolated_display_uses_valid_display_name(self):
        sys.path.insert(0, str(HARNESS))
        from process import run_mame

        result = run_mame(
            [shutil.which("xdpyinfo")], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=10, isolated_display=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_snapshot_exposes_windowed_mode(self):
        tool = (ROOT / "tools" / "mame-gsp-state-snapshot").read_text(encoding="utf-8")
        self.assertIn("--windowed", tool)
        self.assertIn('video_args = ["-video", "sdl", "-window"]', tool)
        self.assertIn("--screen-dir", tool)
        self.assertIn("--isolated-display", tool)
        self.assertIn('"service"', tool)


if __name__ == "__main__":
    unittest.main()
