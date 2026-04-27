"""
Tier-1: N64Recomp.exe handles no-args invocation cleanly.

History: originally pinned as expected_fail on 2026-04-26 because
the binary segfaulted (0xC0000005, posix 139) when invoked with no
args — src/main.cpp dereferenced argv[1] before validating argc.
Fixed the same day by adding an `argc < 2` guard that prints a
usage line to stderr and exits with EXIT_FAILURE.

Test stays in the suite to guard against the guard ever being
removed in a future refactor.
"""
from __future__ import annotations

import pathlib
import subprocess

REPO = pathlib.Path(__file__).resolve().parents[2]
N64RECOMP = REPO / 'build-vs' / 'Release' / 'N64Recomp.exe'


def test_n64recomp_no_args_exits_cleanly():
    if not N64RECOMP.is_file():
        # Existence is asserted by test_binary_smoke; if missing here
        # we don't double-fail.
        return
    proc = subprocess.run(
        [str(N64RECOMP)],
        capture_output=True, text=True, timeout=10,
    )
    # Clean exit codes are in [0, 255]; segfault is ~139 on bash/
    # cygwin or huge values on raw Windows.
    assert 0 <= proc.returncode <= 255, (
        f'N64Recomp.exe crashed on no-args invocation: '
        f'returncode={proc.returncode}. Expected a usage line + clean '
        f'exit. Has the argc < 2 guard in src/main.cpp regressed?'
    )
    combined = (proc.stdout + proc.stderr).lower()
    assert 'usage' in combined or 'config' in combined or 'toml' in combined, (
        f'N64Recomp.exe with no args produced no usage line:\n'
        f'STDOUT: {proc.stdout!r}\nSTDERR: {proc.stderr!r}'
    )
    # Non-zero is the right convention for "you didn't give me what I
    # need." Zero would imply success.
    assert proc.returncode != 0, (
        f'N64Recomp.exe with no args exited 0 — should report a usage '
        f'error with non-zero exit so scripts can detect misuse.'
    )
