"""The trace/snapshot wrappers refuse to publish a dead observer.

The failure this guards against is not hypothetical. MAME reports a failed
`-autoboot_script` as a `[LUA ERROR]` line in its own output and then carries
on, exiting 0. A wrapper that only inspects the return code therefore hands back
a capture that stopped collecting the moment the script threw -- and an empty
event list reads as "the hardware did nothing" rather than "we stopped
watching". `tools/mame-gsp-boundary` did exactly that: its Lua script died
before installing its GSP taps and the wrapper still exited 0 with
`execution_landmarks: []`.

The shared decision lives in `mame_harness.process.check_lua_errors`. These
tests cover the seven capture wrappers that had no detection at all:

    tools/mame-ram-read-trace      tools/mame-sound-boundary
    tools/mame-ram-write-trace     tools/mame-landmark-probe
    tools/mame-rom-read-trace      tools/mame-trace
    tools/mame-gsp-state-snapshot

Each is checked twice. The source-shape test pins the import, the recorded
`lua_errors` key and the call, and the ordering -- the check must run after the
result file is written so the failure has an artifact to point at. The
behaviour test drives the wrapper with a stub MAME whose log is a *complete,
DONE-marked, Lua-error-free* capture with one `[LUA ERROR]` line appended after
the marker. Placing the error last is the point: every pre-existing
completeness check still passes, so only the Lua check can turn the exit code
non-zero. A check that merely fired because the DONE marker went missing would
not be testing this.

The clean half of each pair runs the same wrapper on the same log without the
error line and asserts exit 0. Without it a wrapper that failed unconditionally
would pass every failure assertion above.
"""

import json
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"

# The error is appended *after* the DONE marker on purpose; see the module
# docstring. The script and line are fictional but shaped like MAME's output, so
# the recorded message is the one a real failure would produce.
LUA_ERROR = ("[LUA ERROR] in execute_function: trace.lua:126: "
             "attempt to index a nil value (field 'command')\n")

CLEAN_LOGS = {
    "mame-ram-read-trace": (
        "M1_RAM_READ frame=700 pc=000025E8 addr=00FF9584 data=0001 mask=000F\n"
        "M1_RAM_READ_DONE frames=1800 events=1 truncated=false\n"
    ),
    "mame-ram-write-trace": (
        "M1_RAM_WRITE frame=700 pc=000025E8 addr=FF896040 data=1234 mask=FFFF\n"
        "M1_RAM_WRITE_DONE frames=705 events=1 truncated=false output=/tmp/x\n"
    ),
    "mame-rom-read-trace": (
        "M1_ROM_READ frame=700 pc=00029760 addr=00044630 data=0001 mask=FFFF\n"
        "M1_ROM_READ_DONE frames=1800 events=1 truncated=false\n"
    ),
    "mame-sound-boundary": (
        "M1_SOUND_WRITE label=main_command frame=100 pc=00003000 "
        "addr=FFFE0000 data=0001 mask=00FF\n"
        "M1_SOUND_BOUNDARY_DONE frames=600\n"
    ),
    "mame-landmark-probe": (
        "M1_LANDMARK_HIT pc=0000B228\n"
        "M1_LANDMARK_DONE cpu=:mainpcb:maincpu frames=600 output=/tmp/x\n"
    ),
    "mame-gsp-state-snapshot": (
        "M1_GSP_STATE frame=600 pc=FFF46590 st=00000001 "
        "lo_sum=1 hi_sum=2 low_sum=3 high_sum=4\n"
        "M1_GSP_STATE frame=1800 pc=FFF46590 st=00000001 "
        "lo_sum=1 hi_sum=2 low_sum=3 high_sum=4\n"
    ),
    "mame-trace": (
        "MAME_TRACE_CONFIG start=1 cpu=:mainpcb:maincpu output=/tmp/x\n"
        "MAME_TRACE_DONE cpu=:mainpcb:maincpu frames=5 output=/tmp/x\n"
    ),
}

# The token that writes the tool's own result file, used to pin ordering.
RESULT_WRITE = {
    "mame-ram-read-trace": '(args.output / "result.json").write_text(',
    "mame-ram-write-trace": '(args.output / "result.json").write_text(',
    "mame-rom-read-trace": '(args.output / "result.json").write_text(',
    "mame-sound-boundary": '(args.output / "result.json").write_text(',
    "mame-landmark-probe": '(args.output / "result.json").write_text(',
    "mame-gsp-state-snapshot": '(args.output / "result.json").write_text(',
    "mame-trace": 'pathlib.Path(sys.argv[3]).write_text(json.dumps({',
}


class LuaErrorSourceTests(unittest.TestCase):
    """The wrapper names the shared helper, records it, and calls it late."""

    def source(self, name):
        return (TOOLS / name).read_text(encoding="utf-8")

    def test_wrapper_imports_the_shared_helper(self):
        for name in RESULT_WRITE:
            with self.subTest(tool=name):
                source = self.source(name)
                self.assertIn("from mame_harness.process import", source)
                # find_lua_errors is what fills the recorded list; a wrapper that
                # only calls check_lua_errors cannot report what it found.
                self.assertRegex(
                    source,
                    r"from mame_harness\.process import [^\n]*\bcheck_lua_errors\b")
                self.assertRegex(
                    source,
                    r"from mame_harness\.process import [^\n]*\bfind_lua_errors\b")

    def test_wrapper_records_lua_errors(self):
        for name in RESULT_WRITE:
            with self.subTest(tool=name):
                source = self.source(name)
                self.assertRegex(source, r'"lua_errors":\s*lua_errors')
                # The value has to come from the captured log, not a literal.
                self.assertRegex(source, r"lua_errors\s*=\s*find_lua_errors\(text\)")

    def test_wrapper_calls_the_check(self):
        for name in RESULT_WRITE:
            with self.subTest(tool=name):
                self.assertIn("check_lua_errors(text, log)", self.source(name))

    def test_check_runs_after_the_result_file_is_written(self):
        # Otherwise a failure names a log and a result that were never produced,
        # and the capture cannot be diagnosed after the fact.
        for name, token in RESULT_WRITE.items():
            with self.subTest(tool=name):
                source = self.source(name)
                self.assertLess(source.index(token),
                                source.index("check_lua_errors(text, log)"))

    def test_pre_existing_completeness_checks_survive(self):
        # The Lua check is additive. A DONE/completeness guard that quietly
        # disappeared would make a truncated capture look complete.
        expected = {
            "mame-ram-read-trace": "if completed.returncode or not done:",
            "mame-ram-write-trace": "if completed.returncode or not done:",
            "mame-rom-read-trace": "if completed.returncode or not done:",
            "mame-sound-boundary": "if completed.returncode:",
            "mame-landmark-probe": "if completed.returncode:",
            "mame-gsp-state-snapshot":
                "if completed.returncode or len(snapshots) != len(args.targets.split(',')):",
            "mame-trace": '[[ -e "$trace" ]] ||',
        }
        for name, guard in expected.items():
            with self.subTest(tool=name):
                self.assertIn(guard, self.source(name))


class LuaErrorBehaviourTests(unittest.TestCase):
    """Drive each wrapper with a stub MAME and read the real exit code."""

    def stub_mame(self, temp, log, name):
        stub = temp / f"stub-mame-{name}"
        if name == "mame-trace":
            # The trace wrapper also requires the trace file itself, so the stub
            # writes it the way trace.lua's `trace` command would.
            stub.write_text(
                "#!/bin/sh\n"
                'printf "000000: nop\\n" > "$STUNRUN_TRACE_OUT"\n'
                f"cat {log}\n", encoding="utf-8")
        else:
            stub.write_text(f"#!/bin/sh\ncat {log}\n", encoding="utf-8")
        stub.chmod(0o755)
        return stub

    def run_tool(self, temp, name, stub, dead):
        if name == "mame-trace":
            output = temp / f"out-{name}-{'dead' if dead else 'clean'}"
            output.mkdir(parents=True, exist_ok=True)
            command = [str(TOOLS / name), str(temp / "roms"), str(output),
                       ":mainpcb:maincpu", "5", str(stub), "none", "1"]
        else:
            output = temp / f"out-{name}-{'dead' if dead else 'clean'}"
            command = [str(TOOLS / name), str(temp / "roms"), str(output),
                       "--mame", str(stub), *self.tool_args(name)]
        return subprocess.run(command, cwd=ROOT, capture_output=True, text=True)

    def tool_args(self, name):
        if name == "mame-ram-read-trace":
            return ["--base", "0xFF9584", "--end", "0xFF95FF", "--frames", "1800",
                    "--start-frame", "600", "--end-frame", "1800"]
        if name == "mame-ram-write-trace":
            return ["--device", ":mainpcb:maincpu", "--base", "0xFF896040",
                    "--end", "0xFF896080", "--frames", "705",
                    "--start-frame", "680", "--end-frame", "705"]
        if name == "mame-rom-read-trace":
            return ["--pc-range", "0x29760-0x29770", "--frames", "1800",
                    "--start-frame", "600", "--end-frame", "1800"]
        if name == "mame-sound-boundary":
            return ["--frames", "600"]
        if name == "mame-landmark-probe":
            # The PCs are positional and precede the options.
            return ["0xB228", "--frames", "600"]
        if name == "mame-gsp-state-snapshot":
            return ["--frames", "1800", "--targets", "600,1800"]
        raise AssertionError(name)

    def run_with_log(self, name, dead):
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            (temp / "roms").mkdir()
            log = temp / "recorded.log"
            clean = CLEAN_LOGS[name]
            log.write_text(clean + (LUA_ERROR if dead else ""), encoding="utf-8")
            stub = self.stub_mame(temp, log, name)
            result = self.run_tool(temp, name, stub, dead)
            artifact = None
            produced = temp / f"out-{name}-{'dead' if dead else 'clean'}"
            candidate = produced / "result.json"
            if candidate.is_file():
                artifact = json.loads(candidate.read_text(encoding="utf-8"))
            return result, artifact

    def test_dead_script_is_a_failure_not_an_empty_capture(self):
        for name in CLEAN_LOGS:
            with self.subTest(tool=name):
                result, artifact = self.run_with_log(name, dead=True)
                self.assertNotEqual(result.returncode, 0,
                                    f"{name} exited 0 on a dead script: "
                                    f"{result.stdout}")
                self.assertIn("[LUA ERROR]", result.stderr)
                # The diagnosis names the log an operator has to go read.
                self.assertIn("mame.log", result.stderr)

    def test_result_file_is_written_before_the_failure(self):
        # The artifact has to survive the refusal; otherwise the only record of
        # the partial capture is stderr.
        for name in CLEAN_LOGS:
            with self.subTest(tool=name):
                _, artifact = self.run_with_log(name, dead=True)
                self.assertIsNotNone(artifact, f"{name} published no result.json")
                self.assertEqual(artifact["lua_errors"],
                                 [LUA_ERROR.rstrip("\n")])

    def test_clean_capture_still_succeeds(self):
        for name in CLEAN_LOGS:
            with self.subTest(tool=name):
                result, artifact = self.run_with_log(name, dead=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(artifact["lua_errors"], [])

    def test_trace_wrapper_still_prints_the_trace_path(self):
        # mame-trace's stdout contract is the path to the trace file; the Lua
        # check must not turn that into a JSON blob or swallow it.
        result, _ = self.run_with_log("mame-trace", dead=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("maincpu-68010.trace", result.stdout)


if __name__ == "__main__":
    unittest.main()
