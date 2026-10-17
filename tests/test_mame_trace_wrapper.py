"""Static checks for the saved-state instruction-trace wrapper."""

import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from mame_launch_guards import _python_heredocs, routes_through_launcher  # noqa: E402

TOOL = ROOT / "tools" / "mame-trace"


class MameTraceWrapperTests(unittest.TestCase):
    def test_shell_syntax(self):
        result = subprocess.run(["bash", "-n", str(TOOL)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_documents_saved_state_option(self):
        text = TOOL.read_text(encoding="utf-8")
        self.assertIn("--load-state STATE", text)
        # The staged state directory and the `-state 1` that loads from it moved
        # into the embedded python3 heredoc, which is where the argv is built now.
        self.assertIn("state_directory=", text)
        self.assertIn("state_directory=state_directory,", text)
        self.assertIn('("-state", "1") if state_directory else ()', text)
        self.assertIn("seconds_to_run=86400", text)

    def test_supports_saved_state_visible_forks(self):
        text = TOOL.read_text(encoding="utf-8")
        self.assertIn('"fork_hold_left"', text)
        self.assertIn('"fork_center"', text)

    def test_supports_recorded_input_playback(self):
        text = TOOL.read_text(encoding="utf-8")
        self.assertIn("--playback INP", text)
        self.assertIn('STUNRUN_TRACE_PLAYBACK="$playback"', text)
        # -input_directory/-playback are added by tools/mame_launch.py, reached
        # through the playback= keyword. Asserting the bash array that used to
        # hold them would fail the moment the wrapper stopped hand-rolling the
        # argv, which is what the launcher is for.
        self.assertIn("playback=playback,", text)
        self.assertTrue(routes_through_launcher(TOOL))

    def test_the_heredoc_is_where_the_launch_happens(self):
        """A shell tool has no argv of its own; the heredoc is the program.

        This is the same mistake the previous version of the isolation guard
        made: it looked only where it already knew to look, so a wrapper that
        stopped calling the shared runner entirely would have gone unnoticed.
        """
        heredocs = _python_heredocs(TOOL.read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(heredocs), 1)
        self.assertTrue(any("mame_launch(" in block for block in heredocs))


if __name__ == "__main__":
    unittest.main()
