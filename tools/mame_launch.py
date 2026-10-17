"""One MAME argv builder, so a run cannot inherit the previous run's state.

Twenty-five tools in this repository launch the pinned MAME against the
`stunrun` set, and each of them used to hand-roll the same isolation flags.
Measured consequences, both recorded in AGENTS.md trap 12 and findings §74:

  * A SHARED `-cfg_directory` makes runs contaminate each other in a way that
    reads as a fact about the game. MAME writes `<machine>.cfg` at exit
    including current input-port (DIP) state, and `-noreadconfig` does not
    stop the next run reading it. `tools/mame-ram-write-trace` was the last
    wrapper still hardcoding `/tmp/stunrun-mame-cfg`; five identical
    `--input service_f2` invocations then gave 0 / 45850 / 0 / 45850 / 0
    events -- a perfect alternation, i.e. a deterministic leak, not a race --
    while every run exited 0. Deleting the shared directory first gave
    0 / 0 / 0.
  * `-nonvram_save` WITHOUT `-nvram_directory` loads stale NVRAM and fails
    silently. In the pinned MAME, `machine.cpp` gates only the *write*:
    `if (options().nvram_save()) nvram_save();`, while the read is
    unconditional and is served from `options().nvram_directory()`. So the
    capture is the right size, just stale or zeroed. Nineteen tools did this.

Both are structural, not typos, so the fix is structural too: this module is
the only place a `stunrun` argv is assembled, and it cannot emit a shared
state path because it never writes one.

WHAT IT GETS WRONG -- read before trusting a capture it built:

  * It builds an argv. It does not RUN MAME, does not reap MAME and its
    descendants, and does not notice a failed `-autoboot_script`. MAME reports
    a Lua error as a line in its own output and then carries on exiting 0, so
    a run with a dead observer publishes a capture that stopped collecting.
    Callers still go through `mame_harness/process.run_mame` and
    `check_lua_errors`; nothing here does that work for them.
  * It does not check that the MAME binary or the ROM set exist. `mame` is not
    on `PATH` by default here (trap 2), so a bare `mame` fails inside the
    subprocess with a message that says nothing about isolation.
  * It does not deduplicate `extra`. A caller who passes `-video` in `extra`
    gets two `-video` flags, and MAME's behaviour on a repeated boolean or
    value option is last-wins rather than an error.
  * `frames -> seconds` is a WALL-CLOCK bound, not an emulated-frame count.
    MAME self-terminates when emulated time passes it, so a wall that is too
    slow truncates the run and a capture that looks complete is short. See
    `seconds_for_frames` for the arithmetic and for the three tools whose
    bound is NOT that formula -- they pass `seconds=` and must keep doing so.
  * Isolation covers MAME's own directories. It does not cover the ROM path
    (a caller that points two runs at different ROM sets still gets two
    different machines) and it does not cover observer environment variables.
  * `playback=` drops `-noreadconfig` and `-nowriteconfig` for record
    fidelity. Those two gate MAME's *standard INI files* only
    (`mame.cpp`: `if (m_options.read_config()) parse_standard_inis(...)`);
    the DIP state that actually leaks lives in `<machine>.cfg`, which
    `-cfg_directory` alone controls -- `config.cpp` reads and writes it
    through `options().cfg_directory()` and nothing else. This module
    therefore KEEPS a fresh per-run `-cfg_directory` on the playback path even
    though four of the tools dropped every config flag there. That is strictly
    more isolated, and it fixes a latent bug in `mame-ram-read-trace`/
    `mame-rom-read-trace`, which removed the `-cfg_directory` token and left
    its bare path value sitting in the argv as a positional argument -- which
    MAME reads as a second machine name.
  * `video=` exists only because two callers render a framebuffer
    (`tools/mame-checkpoint`, `tools/mame-replay` pass `-video soft` and call
    `machine.video:snapshot()`), and two more open a window on request
    (`tools/mame-gsp-state-snapshot`, `-mirror-probe`, `-vram-dump` with
    `--windowed`). The default is `"none"`, which is what every other tool
    wants; `-video none` is a headlessness choice, not an isolation flag, and
    forcing it would silently blank those captures.
  * `tools/mame-play-capture` and `tools/check-roms` are NOT migrated, on
    purpose. The first is a human-play session that must use the operator's
    own key map and NVRAM, and offers `--clean-cfg`/`--fresh-nvram` as
    deliberate opt-ins; the second is a CLI command (`-verifyroms`) dispatched
    from `clifront.cpp`'s command table, which never instantiates the machine
    and so never calls `load_settings()`, `save_settings()` or `nvram_load()`.
    Both are named in `tests/test_public_tools.py`'s allowlist, with reasons,
    so the guard states what it does not cover instead of implying it does.

CLEANUP: `/tmp` on this machine sits around 92% full, and a full disk has
already produced exit code 120 with *empty* output files -- which reads
exactly like a clean null result. Every directory this module creates is
removed in a `finally`, so an exception inside the `with` block (including
`SystemExit` from a Lua-error check) cannot leak one.
"""

from __future__ import annotations

import contextlib
import dataclasses
import os
import pathlib
import shutil
import tempfile
from typing import Iterator, Sequence

# The run bound every tool derives from a frame count. Evidence-affecting: it
# decides how long MAME is allowed to run, and therefore whether a capture
# collected everything it was asked for. Six tools use a different formula and
# pass `seconds=` explicitly; each is listed so the next reader does not
# "unify" one by accident:
#
#   tools/mame-adsp-init-listing   max(3, (frame + 59) // 60 + 1)
#   tools/mame-adsp-program-dump   max(3, (frame + 59) // 60 + 1)
#   tools/mame-replacement-image    max(3, (install + settle + 59) // 60 + 1)
#   tools/mame-checkpoint           max(2, ceil((frames + 60) / 60))
#   tools/mame-replay               max(2, ceil((frame + 1 or 120) / 60))
#   tools/mame-play-capture         max(60, frames // 60 + 60)  [not migrated]
#
# "Unifying" those to this one is not a cleanup; it changes how long every
# capture runs.
FRAMES_PER_SECOND = 60


def seconds_for_frames(frames: int) -> int:
    """The `-seconds_to_run` bound for `frames` emulated frames.

    `max(2, (frames + 59) // 60 + 1)`, byte-for-byte the arithmetic the tools
    spelled out inline. The `+59` rounds the frame count up to a whole second
    and the trailing `+1` is headroom; `max(2, ...)` matters for tiny frame
    counts, where `(frames + 59) // 60 + 1` would otherwise allow MAME a
    single second to reach the frame it was asked for. The floor of 2 is what
    makes a 1-frame run survivable at all, and `tools/mame-trace`'s bash
    `((frames + 59) / 60 + 1)` is the same value without the redundant clamp.
    """
    return max(2, (int(frames) + FRAMES_PER_SECOND - 1) // FRAMES_PER_SECOND + 1)


@dataclasses.dataclass(frozen=True)
class Launch:
    """The argv and the per-run directories a `mame_launch` block produced.

    `.cfg_directory` and `.nvram_directory` exist for the whole `with` block,
    which is the whole life of the MAME process it describes; after the block
    they are removed. A capture's `result.json` that wants to record which
    directories were used must read them from inside the block.
    """

    argv: list[str]
    cfg_directory: pathlib.Path
    nvram_directory: pathlib.Path
    state_directory: pathlib.Path | None
    seconds: int
    playback: pathlib.Path | None = None

    def command(self) -> str:
        """The argv as one shell-quoted line, for a diagnostic or a log."""
        return " ".join(_quote(part) for part in self.argv)


def _quote(part: str) -> str:
    return part if all(character.isalnum() or character in "-_./=:,%+@" for character in part) \
        else "'" + part.replace("'", "'\\''") + "'"


@contextlib.contextmanager
def mame_launch(
    *,
    mame: str | os.PathLike[str],
    rompath: str | os.PathLike[str],
    machine: str = "stunrun",
    seconds: int | None = None,
    frames: int | None = None,
    state_directory: str | os.PathLike[str] | None = None,
    statename: str | None = "%g",
    autoboot: str | os.PathLike[str] | None = None,
    extra: Sequence[str] = (),
    playback: str | os.PathLike[str] | None = None,
    video: str = "none",
    nvram_directory: str | os.PathLike[str] | None = None,
) -> Iterator[Launch]:
    """Build an isolated MAME argv for `machine`, and clean up after it.

    `mame` and `rompath` are required; everything else has a default that
    matches what the tools want. `mame_launch` MUST be used as a context
    manager -- the per-run directories only exist for its duration:

        with mame_launch(mame=exe, rompath=roms, frames=600,
                         autoboot=script) as launch:
            run_mame(launch.argv, ...)

    At least one of `seconds` or `frames` is required. `frames` goes through
    `seconds_for_frames`; `seconds` wins when both are given, which is how the
    `--load-state` tools ask for the long 86400-second allowance a
    pre-advanced state needs.

    `state_directory` names where MAME looks for save-state slots. The default
    is a fresh per-run directory, which is what kills the shared
    `/tmp/stunrun-state-dir` literal; pass a path you staged yourself when
    `--load-state` put `<dir>/<statename>/1.sta` there, and the caller keeps
    ownership of it (this module never removes a directory it did not create).
    `statename=None` omits `-statename`, which is only correct when nothing
    loads or saves a slot. There is deliberately no way to omit
    `-state_directory` altogether: a tool that does not need one gets a fresh
    empty one, which costs an `mkdir` and removes a decision.

    `nvram_directory` is the one escape from "fresh per run, discarded": pass a
    path you own when the capture is meant to CONTINUE from NVRAM rather than
    start from a clean boot. Naming an explicit directory also drops
    `-nonvram_save`, because a store you want to keep needs MAME to be able to
    write it. Left alone, `-nonvram_save` plus a fresh directory is what every
    evidence run wants.

    `extra` is appended verbatim, last. Put flags that must precede nothing in
    particular here: `-nothrottle`, `-debug -debugger none`, `-state 1`,
    `-snapname X -snapshot_directory Y`.

    `playback` names a recorded `.inp`. It adds `-input_directory`/`-playback`
    and drops `-noreadconfig`, `-nowriteconfig` and `-nonvram_save`. The three
    tools that also dropped `-cfg_directory` on that path
    (tools/mame-trace, tools/mame-ram-read-trace, tools/mame-rom-read-trace,
    and tools/mame-memory-snapshot before it) re-open the DIP leak, so the
    per-run cfg/nvram/state directories are kept here deliberately -- see this
    module's docstring for what the two removed flags actually gate.
    """
    if seconds is None and frames is None:
        raise ValueError(
            "mame_launch needs a run bound: pass frames= (which goes through "
            "seconds_for_frames) or seconds= verbatim")
    bound = int(seconds) if seconds is not None else seconds_for_frames(int(frames))

    owned: list[pathlib.Path] = []
    try:
        cfg_directory = _fresh("cfg", owned)
        nvram_path = (pathlib.Path(nvram_directory)
                      if nvram_directory is not None
                      else _fresh("nvram", owned))
        if state_directory is None:
            state_path: pathlib.Path | None = _fresh("state", owned)
        else:
            state_path = pathlib.Path(state_directory)

        argv: list[str] = [
            str(mame), "-rompath", str(rompath), machine,
            "-video", video, "-sound", "none", "-skip_gameinfo",
            "-noreadconfig", "-nowriteconfig",
            "-cfg_directory", str(cfg_directory),
            "-nvram_directory", str(nvram_path),
        ]
        if nvram_directory is None:
            # Only meaningful against a directory nobody else will read again;
            # MAME loads NVRAM unconditionally and writes it only under this
            # flag (machine.cpp), so a fresh directory makes the flag moot and
            # stating it keeps the argv self-describing.
            argv.append("-nonvram_save")
        if state_path is not None:
            argv += ["-state_directory", str(state_path)]
            if statename is not None:
                argv += ["-statename", statename]
        argv += ["-seconds_to_run", str(bound)]
        if autoboot is not None:
            argv += ["-autoboot_script", str(autoboot)]

        recording = None
        if playback is not None:
            recording = pathlib.Path(playback)
            # The recorded run's own ini settings are part of the record.
            for flag in ("-noreadconfig", "-nowriteconfig", "-nonvram_save"):
                argv.remove(flag)
            argv += ["-input_directory", str(recording.parent),
                     "-playback", recording.name]
        argv += [str(item) for item in extra]

        yield Launch(argv=argv, cfg_directory=cfg_directory,
                     nvram_directory=nvram_path, state_directory=state_path,
                     seconds=bound, playback=recording)
    finally:
        for path in owned:
            shutil.rmtree(path, ignore_errors=True)


def _fresh(label: str, owned: list[pathlib.Path]) -> pathlib.Path:
    path = pathlib.Path(tempfile.mkdtemp(prefix=f"stunrun-{label}-"))
    owned.append(path)
    return path