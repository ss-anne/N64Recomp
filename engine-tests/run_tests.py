#!/usr/bin/env python3
"""
Engine regression test runner for N64Recomp.

Tests live under tests/{unit,synthetic,l3}/ as test_*.py modules. Each
module exposes one or more `def test_*()` functions; the runner imports
the module, calls each, and treats AssertionError as a controlled fail
and any other exception as an error.

A module may set `expected_fail = True` at top level to mark every test
in it as expected-to-fail. Such tests are still executed; their results
are reported with [XFAIL] / [XPASS] markers but do not change the exit
code.

Exit code: 0 if no unexpected failures, 1 otherwise.

Usage:
    python tests/run_tests.py
    python tests/run_tests.py --tier 1
    python tests/run_tests.py -k <substring>
"""
from __future__ import annotations

import argparse
import importlib
import importlib.util
import pathlib
import shutil
import sys
import traceback
from typing import Callable

TESTS_DIR = pathlib.Path(__file__).parent.resolve()
REPO_ROOT = TESTS_DIR.parent
SCRATCH_DIR = TESTS_DIR / 'fixtures' / '_scratch'

TIER_DIRS = {
    1: TESTS_DIR / 'unit',
    2: TESTS_DIR / 'synthetic',
    3: TESTS_DIR / 'l3',
}


def discover(tier: int) -> list[pathlib.Path]:
    d = TIER_DIRS[tier]
    if not d.is_dir():
        return []
    return sorted(p for p in d.glob('test_*.py'))


def load_module(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise ImportError(f'cannot load {path}')
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def reset_scratch() -> None:
    if SCRATCH_DIR.exists():
        shutil.rmtree(SCRATCH_DIR)
    SCRATCH_DIR.mkdir(parents=True, exist_ok=True)


def run() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--tier', type=int, choices=[1, 2, 3], action='append',
                        help='Run only the given tier(s). May repeat.')
    parser.add_argument('-k', dest='match', default=None,
                        help='Only run tests whose label contains substring.')
    args = parser.parse_args()

    tiers = sorted(set(args.tier or [1, 2, 3]))

    reset_scratch()

    passed = 0
    failed = 0  # genuine failures (counted toward exit code)
    xfailed = 0  # expected failures (not counted)
    xpassed = 0  # expected-to-fail but passed (NOT counted toward exit
                 # code — pinned bugs may be silently fixed and we want
                 # the suite green; orchestrator just flags it visibly)
    errors = 0
    skipped = 0
    fail_log: list[tuple[str, str]] = []

    for tier in tiers:
        modules = discover(tier)
        if not modules:
            print(f'\n=== Tier {tier} ===  (no tests yet)')
            continue
        print(f'\n=== Tier {tier} ===')
        for mod_path in modules:
            try:
                mod = load_module(mod_path)
            except Exception:
                print(f'  ERR   {mod_path.name} (import)')
                fail_log.append((mod_path.name, traceback.format_exc()))
                errors += 1
                continue

            xfail = bool(getattr(mod, 'expected_fail', False))
            skip_reason = getattr(mod, 'skip_reason', None)
            tests: list[tuple[str, Callable[[], None]]] = [
                (n, getattr(mod, n)) for n in dir(mod) if n.startswith('test_')
                and callable(getattr(mod, n))
            ]
            for name, fn in tests:
                label = f'{mod_path.stem}.{name}'
                if args.match and args.match not in label:
                    continue
                if skip_reason:
                    print(f'  SKIP  {label}: {skip_reason}')
                    skipped += 1
                    continue
                try:
                    fn()
                    if xfail:
                        print(f'  XPASS {label}  (expected_fail set but test passed)')
                        xpassed += 1
                    else:
                        print(f'  PASS  {label}')
                        passed += 1
                except AssertionError as e:
                    if xfail:
                        print(f'  XFAIL {label}: {e}')
                        xfailed += 1
                    else:
                        print(f'  FAIL  {label}: {e}')
                        fail_log.append((label, str(e)))
                        failed += 1
                except Exception:
                    tb = traceback.format_exc()
                    print(f'  ERR   {label}')
                    fail_log.append((label, tb))
                    errors += 1

    print()
    summary = (
        f'{passed} passed, {failed} failed, {errors} errored, '
        f'{xfailed} xfailed, {xpassed} xpassed, {skipped} skipped'
    )
    print(summary)

    if fail_log:
        print()
        for label, msg in fail_log:
            print(f'--- {label} ---')
            print(msg)

    return 0 if (failed == 0 and errors == 0) else 1


if __name__ == '__main__':
    sys.exit(run())
