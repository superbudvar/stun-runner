"""ROM-free checks that the MAME wrappers refuse to publish a dead observer.

The failure this guards against is specific and was observed live: MAME reports
a failed `-autoboot_script` as a line in its own output and then carries on,
exiting 0. A wrapper that only checks the return code therefore hands back a
capture that stopped collecting the moment the script threw -- and an empty
field reads as "the hardware did nothing" rather than "we stopped watching".

`tools/mame-gsp-boundary` did exactly this: `mame/lua/gsp-boundary.lua` called
`install_execute_tap`, which this build does not expose on the GSP program space,
so the script died before installing its GSP taps and the wrapper still exited 0
with `execution_landmarks: []`.
"""

import importlib.machinery
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

sys_path_entry = str(ROOT)


def load_tool(path, name):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class LuaErrorDetectionTests(unittest.TestCase):
    def test_find_lua_errors(self):
        from mame_harness.process import find_lua_errors
        self.assertEqual(
            find_lua_errors("[LUA ERROR] attempt to call a nil value\n"),
            ["[LUA ERROR] attempt to call a nil value"])
        self.assertEqual(
            find_lua_errors("noise\n[LUA ERROR] in execute_function: boom\nmore\n"),
            ["[LUA ERROR] in execute_function: boom"])
        self.assertEqual(find_lua_errors("M1_GSP_BOUNDARY_DONE frames=200\n"), [])

    def test_detects_errors_after_a_clean_header(self):
        # MAME prints its banner and the script's own output first, so the error
        # is rarely the first line, and it carries a useful source location that
        # a non-greedy match would truncate.
        from mame_harness.process import find_lua_errors
        text = ("ALSA lib ... open /dev/snd/seq failed\n"
                "M1_GSP_RESET_WRITE frame=1 addr=0060400C\n"
                "[LUA ERROR] in execute_function: gsp-boundary.lua:108: nil\n"
                "M1_GSP_BOUNDARY_DONE frames=200\n")
        found = find_lua_errors(text)
        self.assertEqual(len(found), 1)
        # The whole line, so the failure names the script and line number.
        self.assertIn("gsp-boundary.lua:108", found[0])


class GspBoundaryScriptTests(unittest.TestCase):
    LUA = ROOT / "mame" / "lua" / "gsp-boundary.lua"
    WRAPPER = ROOT / "tools" / "mame-gsp-boundary"

    def test_execute_tap_is_guarded_and_its_status_reported(self):
        text = self.LUA.read_text(encoding="utf-8")
        # An unguarded call aborts install_tap() before the GSP taps exist.
        self.assertIn("type(gsp_space.install_execute_tap) == 'function'", text)
        self.assertIn("gsp_execute_tap_status = 'unavailable'", text)
        self.assertIn("M1_GSP_EXECUTE_TAP_STATUS status=", text)
        # Guard must come before the calls, not merely appear somewhere.
        guard = text.index("type(gsp_space.install_execute_tap) == 'function'")
        first_call = text.index("gsp_space:install_execute_tap(0xfff59920")
        self.assertLess(guard, first_call)

    def test_wrapper_refuses_a_dead_observer(self):
        wrapper = self.WRAPPER.read_text(encoding="utf-8")
        self.assertIn("find_lua_errors", wrapper)
        self.assertIn("check_lua_errors", wrapper)
        self.assertIn("M1_GSP_EXECUTE_TAP_STATUS", wrapper)

    def test_observer_missing_is_decided_by_behaviour_not_by_text(self):
        # A presence check on the error message passes even when the branch is
        # dead, so the decision is a function and is tested as one.
        tool = load_tool(self.WRAPPER, "mame_gsp_boundary")
        # No tap installed and nothing seen: refuse, this is the live bug.
        self.assertTrue(tool.observer_missing("unavailable", []))
        self.assertTrue(tool.observer_missing("unknown", []))
        # Tap installed but genuinely saw nothing: that IS a result.
        self.assertFalse(tool.observer_missing("observed", []))
        # Tap installed and it fired: a result.
        self.assertFalse(tool.observer_missing("observed", [{"frame": 1, "pc": 2}]))
        # Tap missing but landmarks present: inconsistent, but not our failure
        # to refuse -- there is something to report.
        self.assertFalse(tool.observer_missing("unavailable", [{"frame": 1, "pc": 2}]))

    def test_wrapper_calls_the_decision_helper(self):
        wrapper = self.WRAPPER.read_text(encoding="utf-8")
        # A wrapper that reimplements the condition inline can drift from the
        # tested one, so require it to call the helper.
        self.assertIn("if observer_missing(", wrapper)
        self.assertNotIn(
            'if result["gsp_execute_tap_status"] != "observed" and not execution_first:',
            wrapper)

    def test_wrapper_parses_the_status_marker(self):
        tool = load_tool(self.WRAPPER, "mame_gsp_boundary")
        # A log with no marker at all must not be read as "observed".
        self.assertIsNone(tool.EXECUTE_TAP_STATUS.search("M1_GSP_BOUNDARY_DONE\n"))
        match = tool.EXECUTE_TAP_STATUS.search(
            "M1_GSP_EXECUTE_TAP_STATUS status=unavailable\n")
        self.assertEqual(match.group(1), "unavailable")


if __name__ == "__main__":
    unittest.main()