"""
Tier-1: built-binary smoke tests.

Just check that the recompiler binaries exist where we expect them
and respond to invocation without segfaulting. This catches the
trivial regression where someone breaks the build or moves the
output path. It does NOT validate any behavior beyond "runs at all."
"""
from __future__ import annotations

import pathlib
import subprocess

REPO = pathlib.Path(__file__).resolve().parents[2]

# Release-config build paths used in this repo's CMake setup.
N64RECOMP = REPO / 'build-vs' / 'Release' / 'N64Recomp.exe'
RSPRECOMP = REPO / 'build-vs' / 'Release' / 'RSPRecomp.exe'


def _run(exe: pathlib.Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(exe)],
        capture_output=True, text=True, timeout=10,
    )


def test_n64recomp_exists():
    assert N64RECOMP.is_file(), f'expected built binary at {N64RECOMP}'


def test_rsprecomp_exists():
    assert RSPRECOMP.is_file(), f'expected built binary at {RSPRECOMP}'


def test_rsprecomp_runs_without_segfault():
    proc = _run(RSPRECOMP)
    assert -1 <= proc.returncode <= 255, (
        f'RSPRecomp.exe likely crashed: returncode={proc.returncode}'
    )
    combined = (proc.stdout + proc.stderr).lower()
    assert 'usage' in combined or 'config' in combined, (
        f'RSPRecomp.exe with no args produced unexpected output:\n'
        f'STDOUT: {proc.stdout!r}\nSTDERR: {proc.stderr!r}'
    )
