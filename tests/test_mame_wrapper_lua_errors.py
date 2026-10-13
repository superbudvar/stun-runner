"""The ADSP/main-state/memory wrappers must refuse a capture whose observer died.

MAME reports a failed `-autoboot_script` as a `[LUA ERROR]` line in its own
output and then carries on, exiting 0. A wrapper that only inspects the return
code therefore publishes a capture that stopped collecting the moment the
script threw, and an empty observation list reads as "the hardware did nothing"
rather than "we stopped watching". This was found live in
`tools/mame-gsp-boundary`, which exited 0 with `execution_landmarks: []`.

These tests cover the six wrappers that write a `mame.log` and read it back:

    tools/mame-adsp-init-listing
    tools/mame-adsp-program-dump
    tools/mame-adsp-program-snapshot
    tools/mame-adsp-window
    tools/mame-main-state-snapshot
    tools/mame-memory-snapshot

Each is checked twice over. The source assertions pin the shape of the fix --
import `check_lua_errors`, record `lua_errors`, call `check_lua_errors(`. The
behaviour tests drive each tool against a stub MAME (every one of these tools
takes `--mame`, so the emulator binary can be replaced and MAME and ROMs are
not needed) that emits a log containing a `[LUA ERROR]` line and exits 0. That
is the exact shape of the live bug, and it is the assertion that a grep cannot
make: `if False: check_lua_errors(text, log)` leaves `check_lua_errors(` in the
source and still publishes a dead capture, so the call is required to sit
directly in a function body rather than under a branch.

Nothing here needs ROMs, a MAME binary, or an X display.
"""

import ast
import json
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"

# The text a stub MAME prints to reproduce a Lua failure. MAME prints its own
# output, then the script's, and only sometimes stops collecting, so the error
# line is placed after a marker that the wrapper would otherwise accept.
LUA_ERROR_LINE = ("[LUA ERROR] in execute_function: "
                  "window.lua:12: attempt to call a nil value (global 'x')")


def stub_mame(directory, body):
    """Write an executable that ignores MAME's arguments and prints `body`."""
    path = pathlib.Path(directory) / "fake-mame"
    path.write_text("#!/bin/sh\n" + body, encoding="utf-8")
    path.chmod(0o755)
    return path


def run_tool(name, *arguments):
    return subprocess.run(
        [str(TOOLS / name), *map(str, arguments)],
        cwd=ROOT, capture_output=True, text=True)


def unconditional_calls(source):
    """Names called as a direct statement of some function body.

    A call guarded by `if False:` is a child of an `ast.If`, not of the
    function's own body, so it is deliberately not counted here.
    """
    tree = ast.parse(source)
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            for statement in node.body:
                if not isinstance(statement, ast.Expr):
                    continue
                call = statement.value
                if isinstance(call, ast.Call) and isinstance(call.func, ast.Name):
                    names.add(call.func.id)
    return names


class WrapperSourceTests(unittest.TestCase):
    """The source shape of the fix, per tool."""

    NAMES = (
        "mame-adsp-init-listing",
        "mame-adsp-program-dump",
        "mame-adsp-program-snapshot",
        "mame-adsp-window",
        "mame-main-state-snapshot",
        "mame-memory-snapshot",
    )

    def source(self, name):
        return (TOOLS / name).read_text(encoding="utf-8")

    def assert_guarded(self, name):
        source = self.source(name)
        # Imports the shared helper rather than re-deriving the regex: the
        # reason it lives in mame_harness/process.py is that this same miss
        # has already been made twice.
        self.assertIn("from mame_harness.process import", source, name)
        self.assertRegex(source,
                         r"from mame_harness\.process import [^\n]*\bcheck_lua_errors\b",
                         name)
        self.assertIn("sys.path.insert", source, name)
        # Records the errors in the published artifact, so a reader can see
        # that the observer was alive (or that it was not).
        self.assertIn("find_lua_errors", source, name)
        self.assertIn('"lua_errors": lua_errors', source, name)
        # And refuses to report success when there were any.
        self.assertIn("check_lua_errors(", source, name)
        self.assertIn("check_lua_errors", unconditional_calls(source), name)

    def test_mame_adsp_init_listing_guards_lua_errors(self):
        self.assert_guarded("mame-adsp-init-listing")

    def test_mame_adsp_program_dump_guards_lua_errors(self):
        self.assert_guarded("mame-adsp-program-dump")

    def test_mame_adsp_program_snapshot_guards_lua_errors(self):
        self.assert_guarded("mame-adsp-program-snapshot")

    def test_mame_adsp_window_guards_lua_errors(self):
        self.assert_guarded("mame-adsp-window")

    def test_mame_main_state_snapshot_guards_lua_errors(self):
        self.assert_guarded("mame-main-state-snapshot")

    def test_mame_memory_snapshot_guards_lua_errors(self):
        self.assert_guarded("mame-memory-snapshot")

    def test_every_listed_tool_is_covered_by_this_file(self):
        # A seventh wrapper is added to the list above without a matching
        # source or behaviour test when someone forgets this line.
        for name in self.NAMES:
            self.assertTrue((TOOLS / name).is_file(), name)
        self.assertEqual(len(self.NAMES), 6)


class AdspInitListingTests(unittest.TestCase):
    """MAME wrote the listing marker, then the script threw. Refuse anyway."""

    CLEAN = 'printf "stub listing\\n" > "$STUNRUN_ADSP_LISTING_OUT"\n' \
            'echo "M3_ADSP_LISTING frame=412 output=$STUNRUN_ADSP_LISTING_OUT"\n'

    def run_with(self, temp, body):
        return run_tool("mame-adsp-init-listing", ROOT / "roms", temp / "out",
                        "--mame", stub_mame(temp, body))

    def test_refuses_a_log_with_a_lua_error(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.CLEAN + 'echo "' + LUA_ERROR_LINE + '"\n')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Lua script failed", result.stderr)
            # The artifact still exists, so the failure can be diagnosed.
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [LUA_ERROR_LINE])

    def test_accepts_a_clean_log_and_records_no_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.CLEAN)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [])
            self.assertEqual(published["frame"], 412)


class AdspProgramDumpTests(unittest.TestCase):
    """A complete 0x2000-word dump plus a Lua error is still a dead observer."""

    CLEAN = ("awk 'BEGIN{for(i=0;i<8192;i++)"
             " printf \"M3_ADSP_WORD address=%04X value=%04X\\n\", i, i}'\n"
             'echo "M3_ADSP_DUMP frame=136 pc=0123"\n')

    def run_with(self, temp, body):
        return run_tool("mame-adsp-program-dump", ROOT / "roms", temp / "out",
                        "--mame", stub_mame(temp, body))

    def test_refuses_a_log_with_a_lua_error(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.CLEAN + 'echo "' + LUA_ERROR_LINE + '"\n')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Lua script failed", result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [LUA_ERROR_LINE])

    def test_accepts_a_clean_log_and_records_no_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.CLEAN)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [])
            self.assertEqual(published["capture_pc"], "0x0123")


class AdspProgramSnapshotTests(unittest.TestCase):
    """This wrapper checks only the return code, so the Lua error was its
    sole remaining way to notice that `snapshots: []` meant a dead observer."""

    MARKER = ("echo \"M1_ADSP_PROGRAM_SNAPSHOT frame=1 pc=0123 mstat=0000"
              " nonzero=10 first=0001,0002\"\n")

    def run_with(self, temp, body):
        return run_tool("mame-adsp-program-snapshot", ROOT / "roms", temp / "out",
                        "--frames", "10", "--targets", "1",
                        "--mame", stub_mame(temp, body))

    def test_refuses_a_log_with_a_lua_error(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKER + 'echo "' + LUA_ERROR_LINE + '"\n')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Lua script failed", result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [LUA_ERROR_LINE])

    def test_accepts_a_clean_log_and_records_no_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKER)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [])
            self.assertEqual(len(published["snapshots"]), 1)


class AdspWindowTests(unittest.TestCase):
    """`observations: 0` from a dead observer reads as "no host writes here"."""

    MARKER = 'echo "M1_ADSP_WRITE kind=upload pc=000025E8 addr=00800000 data=0001"\n'

    def run_with(self, temp, body):
        return run_tool("mame-adsp-window", ROOT / "roms", temp / "out",
                        "--frames", "10", "--mame", stub_mame(temp, body))

    def test_refuses_a_log_with_a_lua_error(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKER + 'echo "' + LUA_ERROR_LINE + '"\n')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Lua script failed", result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [LUA_ERROR_LINE])
            # The observations it did collect are still published, because a
            # partial capture is worth more than a refusal with no artifact.
            self.assertEqual(published["observations"], 1)

    def test_accepts_a_clean_log_and_records_no_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKER)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [])
            self.assertEqual(published["observations"], 1)


class MainStateSnapshotTests(unittest.TestCase):
    MARKERS = ('echo "M1_MAIN_STATE frame=600 pc=000025E8 r=00010203"'
               ' "aux=00000000 counter=0007 flag=01"\n'
               'echo "M1_MAIN_STATE frame=750 pc=000025E8 r=00010203"'
               ' "aux=00000000 counter=0008 flag=01"\n')

    def run_with(self, temp, body):
        return run_tool("mame-main-state-snapshot", ROOT / "roms", temp / "out",
                        "--frames", "800", "--targets", "600,750",
                        "--mame", stub_mame(temp, body))

    def test_refuses_a_log_with_a_lua_error(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKERS + 'echo "' + LUA_ERROR_LINE + '"\n')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Lua script failed", result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [LUA_ERROR_LINE])

    def test_accepts_a_clean_log_and_records_no_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKERS)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [])
            self.assertEqual(len(published["snapshots"]), 2)


class MemorySnapshotTests(unittest.TestCase):
    MARKER = ('printf \'{"frame":600,"nonzero":1}\' > "$STUNRUN_MEMORY_OUT/s.json"\n'
              'echo "MAME_MEMORY_SNAPSHOT frame=600'
              ' path=$STUNRUN_MEMORY_OUT/s.json nonzero=1"\n')

    def run_with(self, temp, body):
        return run_tool("mame-memory-snapshot", ROOT / "roms", temp / "out",
                        "--device", ":mainpcb:maincpu", "--count", "4",
                        "--frames", "600", "--targets", "600",
                        "--mame", stub_mame(temp, body))

    def test_refuses_a_log_with_a_lua_error(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKER + 'echo "' + LUA_ERROR_LINE + '"\n')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Lua script failed", result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [LUA_ERROR_LINE])

    def test_accepts_a_clean_log_and_records_no_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            result = self.run_with(temp, self.MARKER)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((temp / "out" / "result.json").read_text())
            self.assertEqual(published["lua_errors"], [])
            self.assertEqual([row["frame"] for row in published["snapshots"]], [600])


if __name__ == "__main__":
    unittest.main()