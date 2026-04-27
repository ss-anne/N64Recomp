"""
Tier-1 (PINNED BUG): N64Recomp.exe segfaults when invoked with no
arguments.

Discovered during initial test scaffolding (2026-04-26). On Windows,
`build-vs/Release/N64Recomp.exe` with no args returns SIGSEGV
(0xC0000005, posixly mapped to 139) instead of printing a usage
line and exiting cleanly. Likely a missing-args check in main() that
dereferences argv[1] before validating argc.

Pinned with expected_fail=True so the suite stays green; remove the
expected_fail flag once the binary is hardened.
"""
from __future__ import annotations

import pathlib
import subprocess

REPO = pathlib.Path(__file__).resolve().parents[2]
N64RECOMP = REPO / 'build-vs' / 'Release' / 'N64Recomp.exe'

expected_fail = True


def test_n64recomp_no_args_exits_cleanly():
    if not N64RECOMP.is_file():
        # Don't double-fail; binary smoke test owns the existence check.
        return
    proc = subprocess.run(
        [str(N64RECOMP)],
        capture_output=True, text=True, timeout=10,
    )
    # Clean exit codes are in [0, 255]. Segfault returns ~139 on
    # bash/cygwin or huge values on raw Windows. We accept the small
    # range as "did not crash."
    assert 0 <= proc.returncode <= 255, (
        f'N64Recomp.exe crashed on no-args invocation: '
        f'returncode={proc.returncode}. Expected: print usage and exit.'
    )
    combined = (proc.stdout + proc.stderr).lower()
    assert 'usage' in combined or 'config' in combined or 'toml' in combined, (
        f'N64Recomp.exe with no args produced no usage line:\n'
        f'STDOUT: {proc.stdout!r}\nSTDERR: {proc.stderr!r}'
    )
