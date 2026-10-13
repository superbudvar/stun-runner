"""The core MAME wrappers must refuse to publish a capture from a dead observer.

MAME reports a failed `-autoboot_script` as a line in its own output and then
carries on, exiting 0. A wrapper that only checks the process return code
therefore hands back a capture that stopped collecting the moment its script
threw, and an absent observation reads as a fact about the machine rather than
about the observer. This was found live in tools/mame-gsp-boundary, which
exited 0 with `execution_landmarks: []` while its script had thrown before
installing the GSP taps.

The decision lives in mame_harness/process.py (`find_lua_errors` /
`check_lua_errors`). Every wrapper below must call it, and must record what it
found. These tests cover the eight wrappers that drive MAME through an
autoboot script and publish their own result, and nothing else.

The static half asserts the contract exists in each source; the behavioural
half drives each wrapper with a stub MAME that exits 0 and prints a `[LUA
ERROR]` line, which is the only way to show the check is live rather than
merely present. A presence assertion passes just as happily for a check that
can never fire, and this repository has already shipped tests that passed for
the wrong reason.
"""

import ast
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]

# The eight wrappers in this lane's scope.
TOOLS = (
    "mame-inventory",
    "mame-listings",
    "mame-checkpoint",
    "mame-replay",
    "mame-replacement-image",
    "emit-proof",
    "mame-play-capture",
    "mame-gsp-mirror-probe",
)

# Shaped like the line the live failure produced, source location and a quoted
# symbol included: a message that does not name the script, the line and the
# missing symbol cannot be acted on, and the quote is what proves the recorded
# copy survives being embedded in JSON.
LUA_ERROR = ('[LUA ERROR] in execute_function: stub.lua:12: '
             'attempt to call a nil value "install_write_tap"')

CHECKPOINT_JSON = ('MAME_CHECKPOINT_JSON {"schema":"stunrun-checkpoint/v1",'
                   '"frame":600,"processors":{}}')
REPLAY_JSON = ('MAME_REPLAY_JSON {"schema":"arcade-replay-result/v1","frame":30,'
               '"events_applied":0,"expected_events":0}')
REPLACEMENT_RESULT = (
    "M2_REPLACEMENT_RESULT frame=142 installed=true first=00000000 "
    "entry_word=00000000 mismatches=0 final_pc=0001 main_pc=00000025E8 "
    "main_sr=00002000 main_sp=00FFFBD6 gsp_pc=FFF59920 gsp_st=00200000 "
    "sound_pc=0040 sound_p=70")
INVENTORY_JSON = ('MAME_INVENTORY_JSON {"schema":"mame-runtime-inventory/v1",'
                  '"devices":[]}')

# The four listings each of emit-proof and mame-listings expects to exist. The
# stub writes them so the wrappers' own completeness checks pass and the Lua
# check is the only thing standing between a throw and a published result.
LISTINGS = {
    "maincpu-68010": "00000000: 4E71          NOP\n",
    "gsp-tms34010": "00000000: 0300          NOP\n",
    "adsp2100": "00000000: 00000000      NOP\n",
    "sound-6502": "00000000: EA            NOP\n",
}


def tool_source(name: str) -> str:
    return (ROOT / "tools" / name).read_text(encoding="utf-8")


def tool_modules(name: str) -> list:
    """Parse a tool as Python, or as the Python heredoc a shell tool embeds.

    The wrappers here are a mix: some are Python, and two are shell scripts
    that shell out to `python3 - <<PY` because they have no Python of their own.
    Comments survive in the text of both and satisfy any substring assertion, so
    the contract is checked against the parsed tree instead -- a commented-out
    call must not count as a call.
    """
    text = tool_source(name)
    if text.startswith("#!/usr/bin/env bash"):
        blocks = re.findall(r"<<'PY'\n(.*?)\nPY\n", text, re.DOTALL)
        if not blocks:
            raise AssertionError(f"{name}: no python3 heredoc to check")
        return [ast.parse(block) for block in blocks]
    return [ast.parse(text)]


def calls_to(name: str, function: str) -> list:
    """Every real call to `function` in the tool, wherever it is written."""
    found = []
    for module in tool_modules(name):
        for node in ast.walk(module):
            if (isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Name)
                    and node.func.id == function):
                found.append(node)
    return found


def refused_unconditionally(name: str, function: str) -> bool:
    """True when at least one call is a statement no branch can skip.

    A call that sits inside `if False:` still parses as a call, and a wrapper
    holding one of those passes any assertion that only looks for the name --
    so the call has to be a statement on the path that always runs.
    """
    def visit(node, conditional=False):
        for child in ast.iter_child_nodes(node):
            skipped = conditional or isinstance(child, ast.If)
            if (isinstance(child, ast.Expr) and isinstance(child.value, ast.Call)
                    and isinstance(child.value.func, ast.Name)
                    and child.value.func.id == function and not skipped):
                return True
            if visit(child, skipped):
                return True
        return False

    return any(visit(module) for module in tool_modules(name))


def imports_from(name: str, modules: set, symbol: str) -> bool:
    for module in tool_modules(name):
        for node in ast.walk(module):
            if (isinstance(node, ast.ImportFrom)
                    and node.module in modules
                    and any(alias.name == symbol for alias in node.names)):
                return True
    return False


def names_the_record(name: str) -> bool:
    """True when the parsed source mentions lua_errors outside any comment."""
    for module in tool_modules(name):
        for node in ast.walk(module):
            if isinstance(node, ast.Name) and node.id == "lua_errors":
                return True
            if (isinstance(node, ast.Constant)
                    and isinstance(node.value, str)
                    and node.value.startswith("lua_errors")):
                return True
    return False


def write_stub(path: pathlib.Path, body: str) -> pathlib.Path:
    """A stand-in for the emulator: same contract, no MAME and no ROMs."""
    path.write_text("#!/bin/sh\n" + body + "\n", encoding="utf-8")
    path.chmod(0o755)
    return path


def run_tool(name: str, *args, cwd=ROOT):
    return subprocess.run([str(ROOT / "tools" / name), *[str(a) for a in args]],
                          capture_output=True, text=True, cwd=cwd, check=False)


def listings_stub(extra: str = "") -> str:
    lines = [f'printf "{text}" > "$DIR/{name}.lst"' for name, text in LISTINGS.items()]
    return 'DIR=$STUNRUN_EMIT_VALIDATION_DIR\n: "${DIR:=$STUNRUN_LISTING_DIR}"\n' \
        + "\n".join(lines) + "\n" + extra


class SharedCheckIsUsedTests(unittest.TestCase):
    """Each wrapper imports the shared decision, records what it found, calls it."""

    def test_wrapper_imports_the_shared_check(self):
        for name in TOOLS:
            with self.subTest(tool=name):
                # Both spellings are in use: `mame_harness.process` from the
                # repo root, and the flat `process` import that predates it.
                self.assertTrue(
                    imports_from(name, {"mame_harness.process", "process"},
                                 "check_lua_errors"),
                    f"{name} does not import check_lua_errors")
                self.assertTrue(
                    imports_from(name, {"mame_harness.process", "process"},
                                 "find_lua_errors"),
                    f"{name} does not import find_lua_errors")

    def test_wrapper_records_the_errors_it_found(self):
        for name in TOOLS:
            with self.subTest(tool=name):
                # The errors are read out of the log text MAME produced, not
                # out of an argument someone passed in.
                reads = calls_to(name, "find_lua_errors")
                self.assertTrue(reads, f"{name} never calls find_lua_errors")
                for call in reads:
                    self.assertEqual([a.id for a in call.args if isinstance(a, ast.Name)],
                                     ["text"],
                                     f"{name}: find_lua_errors is not fed the log text")
                # Where they are recorded depends on what the wrapper already
                # publishes: a JSON key in the result document, a local that
                # goes into one, or a summary line where the document belongs to
                # the emulator or the operator.
                self.assertTrue(names_the_record(name),
                                f"{name} computes the errors and drops them")

    def test_wrapper_calls_the_check(self):
        for name in TOOLS:
            with self.subTest(tool=name):
                calls = calls_to(name, "check_lua_errors")
                self.assertTrue(calls,
                                f"{name} computes the errors but never refuses on them")
                for call in calls:
                    self.assertEqual(len(call.args), 2,
                                     f"{name}: check_lua_errors needs the log "
                                     "text and the log to cite")
                self.assertTrue(refused_unconditionally(name, "check_lua_errors"),
                                f"{name}: check_lua_errors is only called inside a "
                                "branch, so a branch can skip it")


class WrapperRefusesADeadObserverTests(unittest.TestCase):
    """Drive each wrapper with a stub MAME that exits 0 and throws in Lua."""

    def assert_refuses(self, result, artifact=None):
        """A wrapper handed a Lua error must not report success."""
        self.assertNotEqual(result.returncode, 0,
                            "wrapper exited 0 after a Lua error:\n"
                            + result.stdout + result.stderr)
        self.assertIn("Lua", result.stdout + result.stderr,
                      "the failure must say the observer died, not MAME alone")
        if artifact is not None:
            # Placed after the artifact is written so the diagnosis has one.
            self.assertTrue(artifact.is_file(),
                            f"{artifact} missing, so the failure cites nothing")

    # ---- tools/mame-inventory --------------------------------------------

    def test_mame_inventory_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            clean = run_tool("mame-inventory", ROOT / "roms",
                             write_stub(temp / "mame-clean",
                                        f"echo '{INVENTORY_JSON}'"))
            self.assertEqual(clean.returncode, 0, clean.stderr)
            # stdout is the inventory document; the observer health goes to
            # stderr so the JSON stays machine-readable.
            self.assertEqual(clean.stdout.strip(), INVENTORY_JSON.split(" ", 1)[1])
            self.assertIn("lua_errors: []", clean.stderr)

            broken = run_tool("mame-inventory", ROOT / "roms",
                              write_stub(temp / "mame-lua",
                                         f"echo '{INVENTORY_JSON}'\n"
                                         f"echo '{LUA_ERROR}'"))
            self.assert_refuses(broken)
            self.assertIn("inventory is incomplete", broken.stderr)
            # The log is a temp file removed on exit, so the wrapper dumps it.
            self.assertIn(LUA_ERROR, broken.stderr)

    # ---- tools/mame-listings ---------------------------------------------

    def test_mame_listings_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            for label, extra, expects_failure in (
                    ("clean", "", False),
                    ("lua", f"echo '{LUA_ERROR}'", True)):
                output = temp / f"out-{label}"
                result = run_tool("mame-listings", ROOT / "roms", output,
                                  write_stub(temp / f"mame-{label}",
                                             listings_stub(extra)))
                if expects_failure:
                    # Every listing exists and decodes: only the Lua error
                    # distinguishes this run from a successful one.
                    self.assert_refuses(result, output / "mame.log")
                    for name in LISTINGS:
                        self.assertTrue((output / f"{name}.lst").is_file())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("lua_errors: []", result.stdout)
                    self.assertIn("static listings generated:", result.stdout)

    # ---- tools/mame-checkpoint -------------------------------------------

    def test_mame_checkpoint_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            output = temp / "out"
            result = run_tool("mame-checkpoint", ROOT / "roms", output,
                              "--mame", write_stub(
                                  temp / "mame-lua",
                                  f"echo '{CHECKPOINT_JSON}'\necho '{LUA_ERROR}'"))
            self.assert_refuses(result, output / "state.json")
            self.assertIn(f"lua_errors: [{json.dumps(LUA_ERROR)}]", result.stdout)
            # state.json is the `stunrun-checkpoint/v1` document the native
            # emitter is compared against byte for byte and published as oracle
            # evidence, so observer health is recorded on the summary line
            # rather than by extending a schema that is not ours.
            state = json.loads((output / "state.json").read_text(encoding="utf-8"))
            self.assertEqual(state, {"schema": "stunrun-checkpoint/v1",
                                     "frame": 600, "processors": {}})

    # ---- tools/mame-replay ------------------------------------------------

    def test_mame_replay_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            experiment = temp / "experiment.json"
            experiment.write_text(json.dumps({
                "schema": "arcade-experiment/v1", "id": "stub",
                "start": "power_on", "events": [],
                "expect": {"kind": "frame", "frame": 30},
            }), encoding="utf-8")
            output = temp / "out"
            # The result line is printed before the throw, so the existing
            # completeness check passes: this is the false success.
            result = run_tool("mame-replay", experiment, "--rompath", ROOT / "roms",
                              "--output", output, "--mame", write_stub(
                                  temp / "mame-lua",
                                  f"echo '{REPLAY_JSON}'\necho '{LUA_ERROR}'"))
            self.assert_refuses(result, output / "result.json")
            recorded = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertEqual(recorded["lua_errors"], [LUA_ERROR])

    # ---- tools/mame-replacement-image -------------------------------------

    def test_mame_replacement_image_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            image_dir = temp / "image"
            image_dir.mkdir()
            payload = bytes(20)
            (image_dir / "adsp2100.bin").write_bytes(payload)
            (image_dir / "image.json").write_text(json.dumps({
                "schema": "stunrun-replacement-image/v1",
                "processor": "adsp2100", "fixture": "nop", "origin": "0x0",
                "entry": "0x4", "length": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "binary": "adsp2100.bin",
            }), encoding="utf-8")
            output = temp / "out"
            # The RESULT line is printed before machine:exit(), so a throw
            # after it would be published as a completed installation.
            result = run_tool("mame-replacement-image", image_dir / "image.json",
                              "--rompath", ROOT / "roms", "--output", output,
                              "--mame", write_stub(
                                  temp / "mame-lua",
                                  f"echo '{REPLACEMENT_RESULT}'\necho '{LUA_ERROR}'"))
            self.assert_refuses(result, output / "result.json")
            recorded = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertEqual(recorded["lua_errors"], [LUA_ERROR])
            # Nothing observed changes; only the refusal is new.
            self.assertTrue(recorded["installed"])
            self.assertEqual(recorded["readback_mismatches"], 0)

    # ---- tools/emit-proof -------------------------------------------------

    def test_emit_proof_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            output = temp / "out"
            # The proof is that MAME decodes what we emit, so all four listings
            # are present and correct here: the decode checks all pass.
            result = run_tool("emit-proof", "--rompath", ROOT / "roms",
                              "--output", output, "--mame", write_stub(
                                  temp / "mame-lua",
                                  listings_stub(f"echo '{LUA_ERROR}'")))
            self.assert_refuses(result, output / "report.json")
            report = json.loads((output / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["lua_errors"], [LUA_ERROR])
            self.assertEqual(set(report["decoded"]), set(LISTINGS))

    # ---- tools/mame-play-capture ------------------------------------------

    def test_mame_play_capture_refuses_a_dead_script(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            roms = temp / "roms"
            (roms / "stunrun").mkdir(parents=True)
            output = temp / "out"
            result = run_tool("mame-play-capture", roms, output, "--mame",
                              write_stub(temp / "mame-lua",
                                         f"echo 'MAME_PLAY_START frame=1 course=0 score=0'\n"
                                         f"echo '{LUA_ERROR}'"),
                              "--frames", "200", "--every", "100")
            self.assert_refuses(result, output / "mame.log")
            # The operator is told to watch this terminal for MAME_PLAY_*
            # lines; capturing the log must not take them away.
            self.assertIn("MAME_PLAY_START", result.stdout)
            self.assertIn(f"lua_errors: [{json.dumps(LUA_ERROR)}]", result.stdout)
            self.assertIn(LUA_ERROR,
                          (output / "mame.log").read_text(encoding="utf-8"))

    # ---- tools/mame-gsp-mirror-probe --------------------------------------

    MIRROR_PROBE_LOG = (ROOT / "reference" / "experiments" / "stunrun"
                        / "gsp-vram-mirror-probe-600" / "mame.log")

    def test_mirror_probe_refuses_a_dead_script(self):
        recorded = self.MIRROR_PROBE_LOG.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            log = temp / "probe.log"
            log.write_text(recorded + LUA_ERROR + "\n", encoding="utf-8")
            output = temp / "out"
            result = run_tool("mame-gsp-mirror-probe", ROOT / "roms", output,
                              "--mame", write_stub(temp / "mame-lua",
                                                   f"cat '{log}'"),
                              "--frames", "600", "--target", "600")
            self.assert_refuses(result, output / "result.json")
            parsed = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertEqual(parsed["lua_errors"], [LUA_ERROR])
            # The measurement itself is unchanged: a recorded probe log plus a
            # trailing error is the same measurement, and the only difference
            # the wrapper makes is refusing to publish it as complete.
            self.assertEqual(parsed["share"]["bytes"], 4194304)


class WrapperStillSucceedsWithoutErrorsTests(unittest.TestCase):
    """The added check must not turn a healthy capture into a failure."""

    def test_checkpoint_succeeds_and_publishes_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            output = temp / "out"
            result = run_tool("mame-checkpoint", ROOT / "roms", output,
                              "--mame", write_stub(temp / "mame-clean",
                                                   f"echo '{CHECKPOINT_JSON}'"))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("lua_errors: []", result.stdout)
            # The checkpoint document stays the last line of stdout.
            self.assertEqual(json.loads(result.stdout.splitlines()[-1]),
                             {"schema": "stunrun-checkpoint/v1", "frame": 600,
                              "processors": {}})

    def test_replay_succeeds_and_prints_mames_line_verbatim(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            experiment = temp / "experiment.json"
            experiment.write_text(json.dumps({
                "schema": "arcade-experiment/v1", "id": "stub",
                "start": "power_on", "events": [],
                "expect": {"kind": "frame", "frame": 30},
            }), encoding="utf-8")
            output = temp / "out"
            result = run_tool("mame-replay", experiment, "--rompath", ROOT / "roms",
                              "--output", output, "--mame", write_stub(
                                  temp / "mame-clean", f"echo '{REPLAY_JSON}'"))
            self.assertEqual(result.returncode, 0, result.stderr)
            # stdout is MAME's own result line, unchanged: the artifacts under
            # reference/experiments are built from it.
            self.assertEqual(result.stdout.strip(), REPLAY_JSON.split(" ", 1)[1])
            self.assertEqual(
                json.loads((output / "result.json").read_text(encoding="utf-8"))["lua_errors"],
                [])


if __name__ == "__main__":
    unittest.main()
