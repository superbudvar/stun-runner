"""Bounded MAME process lifecycle used by scripted experiments."""

from __future__ import annotations

import os
import signal
import shutil
import subprocess
from typing import IO, Mapping, Sequence


def run_mame(
    command: Sequence[str],
    *,
    env: Mapping[str, str] | None = None,
    stdout: int | IO[str] | None = None,
    stderr: int | IO[str] | None = None,
    timeout: float | None = None,
    isolated_display: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run MAME and always reap its process group.

    MAME may create helper processes (SDL, debugger, or a frontend). Starting
    a new session lets timeout and signal cleanup terminate those descendants
    as well as the emulator itself.
    """
    display_process = None
    process_env = dict(env) if env is not None else os.environ.copy()
    if isolated_display:
        xvfb = shutil.which("Xvfb")
        if xvfb is None:
            raise FileNotFoundError("Xvfb is required for an isolated MAME display")
        display_process = subprocess.Popen(
            [xvfb, "-displayfd", "1", "-screen", "0", "1024x768x24", "-nolisten", "tcp"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
            start_new_session=(os.name == "posix"),
        )
        display = display_process.stdout.readline().strip()
        if not display:
            _stop_process_group(display_process)
            raise RuntimeError("Xvfb did not provide a display number")
        process_env["DISPLAY"] = display
    process = subprocess.Popen(
        list(command),
        env=process_env,
        stdout=stdout,
        stderr=stderr,
        text=True,
        start_new_session=(os.name == "posix"),
    )
    try:
        captured_stdout, captured_stderr = process.communicate(timeout=timeout)
    except BaseException:
        _stop_process_group(process)
        raise
    finally:
        if display_process is not None:
            _stop_process_group(display_process)
    return subprocess.CompletedProcess(
        list(command), process.returncode, captured_stdout, captured_stderr
    )


def _stop_process_group(process: subprocess.Popen[str]) -> None:
    """Stop and reap a MAME process without leaving an orphan behind."""
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    else:
        process.terminate()
    try:
        process.wait(timeout=5)
        return
    except subprocess.TimeoutExpired:
        pass
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    else:
        process.kill()
    process.wait()
