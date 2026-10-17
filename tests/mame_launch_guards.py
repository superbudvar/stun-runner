"""Find the tools that launch MAME, and decide whether they isolate their runs.

Shared by tests/test_public_tools.py and tests/test_mame_trace_wrapper.py rather
than living inside either of them. It is a module and not a test file on
purpose: unittest discovery puts this directory on `sys.path`, so the guard
tests can import the same checkers, and nothing here is collected as a test.

Why it is a module with functions rather than assertions: a guard has to be
pointable at something. `MameCfgIsolationTests` runs it over `tools/`; the
controls run it over deliberately broken copies, which is the only way to show
it can fail. A guard that can only be run against the repository cannot be
falsified without breaking the repository.

The question it answers: "is there any tool here that starts the pinned MAME
against `stunrun` without a per-run cfg, nvram and state directory?"

WHAT IT GETS WRONG -- read before trusting a clean result:

  * Discovery is lexical. A launcher that calls neither `mame_launch()` nor
    `run_mame()` and names none of the MAME flags below is not found. Nothing
    in tools/ is like that, and such a tool would be broken in ways this does
    not claim to cover. The signal list is written down above rather than left
    implicit.
  * It reads SOURCE. It cannot tell that a tool passes the right directory to
    the wrong thing, that it deletes the directory before MAME reads it, or
    that two tools share one. Those are behavioural and need a real MAME run,
    which the ROM-free suite does not do.
  * `shared_state_literals` permits a `mktemp`/`mkdtemp` line. That is right
    for the two shell tools here and would be wrong for a tool that spelled a
    fixed path next to the word mktemp.
  * `MAME_LAUNCH_ALLOWLIST` and `MAME_META_TOOLS` are the only hand-maintained
    names in the guard, and both are checked against this discovery, so neither
    can become a place a new tool is quietly excused.
"""

import ast
import io
import pathlib
import re
import tokenize


ROOT = pathlib.Path(__file__).resolve().parents[1]
# --------------------------------------------------------------------------
# Discovering the MAME-launching tools, and the shared launcher they must use.
# --------------------------------------------------------------------------

MAME_LAUNCH_MODULE = ROOT / "tools" / "mame_launch.py"

# A shebang does not make a tool shell: every Python tool in tools/ has one
# too, so the bash interpreter is what is looked for.
_BASH_SHEBANG = "#!/usr/bin/env bash"

# Discovery is two-tier; see the module docstring. Each signal is matched as a
# WHOLE TOKEN, so `-rompath` is not found inside `--rompath` -- which is
# tools/compare-frame's own CLI option.
_MACHINE_TOKEN = re.compile(r'"stunrun"|\'stunrun\'|\bstunrun\b')
# Two tiers, because after migration the tools no longer name the machine: the
# launcher owns that default. A tier-1 signal is unambiguous on its own; a
# tier-2 flag is only a signal together with the machine name, because
# `--playback` and `--state-directory` are also these tools' own CLI options.
#
# `subprocess.run(` was tried here and had to come out again: with the word
# "stunrun" in a tempfile prefix or a schema name it swept in
# tools/native-coverage and tools/run-native-visible-bridge, neither of which
# starts an emulator.
_CALL_SIGNALS = ("mame_launch(", "run_mame(")
_FLAG_SIGNALS = (
    "-rompath", "-seconds_to_run", "-cfg_directory", "-nvram_directory",
    "-state_directory", "-statename", "-autoboot_script", "-noreadconfig",
    "-nowriteconfig", "-nonvram_save", "-playback", "-verifyroms",
)
# LIMIT, stated rather than hidden: a launcher that calls none of the shared
# runner and names none of these flags would not be discovered. Nothing in
# tools/ is like that, and such a tool would be broken in ways this guard does
# not claim to cover.
_SIGNAL_TOKEN = re.compile(
    r"(?<![\w-])(?:%s)"
    % "|".join(re.escape(signal) for signal in _CALL_SIGNALS + _FLAG_SIGNALS))
_FLAG_TOKEN = re.compile(
    r"(?<![\w-])(?:%s)" % "|".join(re.escape(signal) for signal in _FLAG_SIGNALS))
_CALL_TOKEN = re.compile(
    r"(?<![\w-])(?:%s)" % "|".join(re.escape(signal) for signal in _CALL_SIGNALS))


def _strip_comments_and_docstrings(text):
    """Executable source: no `#` comments and no docstrings, prose kept.

    Comments and docstrings are how this repository records WHY a literal was
    retired, so leaving them in would make the guard fail on its own history
    notes. Prose inside ordinary string literals is deliberately NOT stripped:
    a path pasted into a result document or a note is still the kind of thing a
    reader would copy back into an argv.
    """
    if text.startswith(_BASH_SHEBANG):
        # Shell: drop whole-line `#` comments. A `#` inside a quoted heredoc
        # body is left alone -- the heredoc IS a program, and those are its
        # comments, removed when the heredoc is parsed as Python below.
        return "\n".join(line for line in text.splitlines()
                         if not line.lstrip().startswith("#"))
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return text
    lines = text.splitlines()
    docstring_lines = set()
    for node in ast.walk(tree):
        if not isinstance(node, (ast.Module, ast.FunctionDef,
                                 ast.AsyncFunctionDef, ast.ClassDef)):
            continue
        body = getattr(node, "body", None)
        if body and isinstance(body[0], ast.Expr) and \
                isinstance(body[0].value, ast.Constant) and \
                isinstance(body[0].value.value, str):
            docstring_lines.update(range(body[0].lineno,
                                         body[0].end_lineno + 1))
    out = []
    for number, line in enumerate(lines, 1):
        if number in docstring_lines:
            continue
        out.append(line.split("#", 1)[0])
    return "\n".join(out)


def _python_heredocs(text):
    """The bodies of `python3 - <<'PY' ... PY` blocks in a shell tool.

    A shell wrapper's program IS its heredoc, so its comments and docstrings
    have to be stripped by parsing it as Python, not by line prefix.
    """
    return re.findall(r"<<'PY'\n(.*?)\nPY\n", text, re.DOTALL)


def _live_source(path):
    """Everything in `path` that could become runtime behaviour.

    For a Python tool that is the file minus comments and docstrings. For a
    shell tool it is the shell minus whole-line comments, plus each embedded
    Python heredoc minus its own comments and docstrings.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return ""
    if not text.startswith(_BASH_SHEBANG):
        return _strip_comments_and_docstrings(text)
    parts = [_strip_comments_and_docstrings(text)]
    parts += [_strip_comments_and_docstrings(block)
              for block in _python_heredocs(text)]
    return "\n".join(parts)


def discover_mame_launching_tools(directory):
    """Every executable tool in `directory` that reaches the pinned MAME.

    Deliberately not a list: a hand-maintained list is how the previous version
    of this guard passed while twenty tools were broken, because it only ever
    inspected the one tool whose bug had been found. `MAME_LAUNCH_ALLOWLIST`
    below is the only place a tool is named by hand, and each entry there
    carries a reason and is checked against this discovery.

    Candidate files must carry a `#!` shebang and decode as UTF-8, which is what
    separates a tool from `tools/diff.png` and `tools/frame_diff` -- two binary
    blobs that happen to sit in the same directory without a suffix.
    """
    found = []
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.suffix not in ("", ".sh"):
            continue
        if path.name == MAME_LAUNCH_MODULE.name:
            continue  # the launcher itself is the thing being required
        try:
            head = path.read_text(encoding="utf-8")[:2]
        except UnicodeDecodeError:
            continue
        if head != "#!":
            continue
        code = _live_source(path)
        if not _SIGNAL_TOKEN.search(code):
            continue
        if _CALL_TOKEN.search(code) or \
                (_MACHINE_TOKEN.search(code) and _FLAG_TOKEN.search(code)):
            found.append(path)
    return found


# Tools that are discovered but must not route through the launcher, each with
# the reason. `test_allowlist_is_not_stale` fails if an entry no longer matches
# discovery, so this cannot quietly become a place to hide a new tool.
MAME_LAUNCH_ALLOWLIST = {
    "mame-play-capture": (
        "A human-play session, not a capture: it must use the operator's own "
        "key map and NVRAM, and offers --clean-cfg/--fresh-nvram as explicit "
        "opt-ins for the cases where those are wrong. Forcing per-run temp "
        "directories would silently discard the operator's key bindings."
    ),
    "check-roms": (
        "-verifyroms is a CLI command dispatched from clifront.cpp's command "
        "table, before mame.cpp creates a machine, so load_settings() / "
        "save_settings() / nvram_load() never run and there is no per-run state "
        "to isolate."
    ),
}

# Tools that spawn ANOTHER tool rather than MAME. They are not allowlisted out
# of discovery -- each is asserted to delegate to a tool that is itself
# launcher-routed, which closes the gap instead of hiding it.
MAME_META_TOOLS = {
    "compare-frame": "mame-gsp-state-snapshot",
    "mame-ram-read-write-trace": "mame-ram-write-trace",
}

# A fixed absolute path in /tmp is the shape of the bug. `mktemp`/`mkdtemp`
# templates are permitted because they are unique by construction -- the `X`s
# are what make them so.
_FIXED_TMP_PATH = re.compile(r"""(?<![\w-])(/tmp/[\w./-]*)""")


def shared_state_literals(path):
    """Fixed `/tmp/...` literals in a tool's executable source.

    A `mktemp`/`mkdtemp` on the same line is not an offender: those are unique
    by construction, which is the `X`s' whole job.
    """
    offenders = []
    for line in _live_source(path).splitlines():
        stripped = line.strip()
        if "mktemp" in stripped or "mkdtemp" in stripped:
            continue
        for match in _FIXED_TMP_PATH.finditer(line):
            offenders.append("%s: %s" % (path.name, stripped[:120]))
    return offenders


def imports_the_launcher(path):
    """True when the file actually imports `mame_launch` in the body that uses it.

    Calling a name that was never imported raises NameError on the first run and
    is invisible to every other check here -- `routes_through_launcher` sees the
    call, the isolation flags are all present, and the shared-path scan is
    empty. tools/mame-gsp-vram-dump shipped that way during this migration and
    only a stub-MAME smoke run caught it.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return True
    bodies = ([text] if not text.startswith(_BASH_SHEBANG)
              else _python_heredocs(text) or [text])
    for body in bodies:
        if "mame_launch" not in body:
            continue
        try:
            tree = ast.parse(body)
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.ImportFrom) and node.module == "mame_launch":
                return True
            if isinstance(node, ast.Import):
                if any(alias.name == "mame_launch" for alias in node.names):
                    return True
    return False


def routes_through_launcher(path):
    """True when `path` actually CALLS `mame_launch`, not merely names it."""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return False
    bodies = ([text] if not text.startswith(_BASH_SHEBANG)
              else _python_heredocs(text) or [text])
    for body in bodies:
        try:
            tree = ast.parse(body)
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.With):
                continue
            for item in node.items:
                call = item.context_expr
                if isinstance(call, ast.Call) and \
                        isinstance(call.func, ast.Name) and \
                        call.func.id == "mame_launch":
                    return True
    return False


def launcher_binding_is_used(path):
    """True when every `with mame_launch(...)` binds its result and reads it.

    This is a whole class of mistake, and it shipped once while this work was
    being done: editing a call site's keyword list dropped `as launch` while the
    body kept using `launch.argv`, so the tool raised NameError at run time and
    every other source-level guard here still passed. Binding a name and using
    it are separate edits, and only the second one is visible to a name search.

    `ast.With.items` holds one entry per `with` CLAUSE, and the bound name is
    that entry's `optional_vars` -- so `with A as a, B as b:` has two items, and
    a bare `with A:` has one with `optional_vars is None`. That `None` is the
    case worth noticing: it means the argv was built and then thrown away.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return True
    bodies = ([text] if not text.startswith(_BASH_SHEBANG)
              else _python_heredocs(text) or [text])
    blocks = 0
    for body in bodies:
        try:
            tree = ast.parse(body)
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.With):
                continue
            for item in node.items:
                call = item.context_expr
                if not (isinstance(call, ast.Call)
                        and isinstance(call.func, ast.Name)
                        and call.func.id == "mame_launch"):
                    continue
                blocks += 1
                bound = item.optional_vars
                if not isinstance(bound, ast.Name):
                    return False
                if not any(isinstance(child, ast.Name) and child.id == bound.id
                           and child is not bound for child in ast.walk(node)):
                    return False
    return blocks > 0
