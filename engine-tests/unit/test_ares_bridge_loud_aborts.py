"""
Tier-1: ares-bridge placeholder bodies must abort, not silently return.

The placeholder build (no -DWITH_ARES_BRIDGE=ON) MUST cause any
functional bridge call to abort with a loud diagnostic. Returning
ARES_BRIDGE_NOT_IMPLEMENTED is forbidden — that's the failure mode
we hit on 2026-04-25 (commit 9e54110), where consumer code could
accept NOT_IMPLEMENTED as "fine" and silently drift past the broken
oracle.

The ONLY entry points allowed to return without aborting are
ares_bridge_is_real() (capability probe) and ares_bridge_version()
(string introspection). All others must call bridge_abort().

If anyone reverts the bridge to silent NOT_IMPLEMENTED returns,
this test fires.
"""
from __future__ import annotations

import pathlib

REPO = pathlib.Path(__file__).resolve().parents[2]
BRIDGE_CPP = REPO / 'ares-bridge' / 'src' / 'ares_bridge.cpp'

# Functions that must abort in placeholder mode.
MUST_ABORT = [
    'ares_init',
    'ares_shutdown',
    'ares_reset',
    'ares_step_frame',
    'ares_step_instruction',
    'ares_read_cpu_register',
    'ares_read_pc',
    'ares_read_hi_lo',
    'ares_read_memory',
    'ares_set_controller',
    'ares_save_state',
    'ares_load_state',
]

# Functions that must NOT abort (capability probes).
MUST_NOT_ABORT = [
    'ares_bridge_is_real',
    'ares_bridge_version',
]


def test_no_silent_not_implemented_returns():
    """
    The placeholder file must contain zero `return ARES_BRIDGE_NOT_IMPLEMENTED`
    statements. NOT_IMPLEMENTED is reserved for genuine sub-feature
    gaps inside the real build (an Ares-unsupported opcode, etc.) —
    never as a stand-in for "stub."
    """
    src = BRIDGE_CPP.read_text(encoding='utf-8')
    assert 'ARES_BRIDGE_NOT_IMPLEMENTED' not in src, (
        'ares_bridge.cpp placeholder uses ARES_BRIDGE_NOT_IMPLEMENTED — '
        'this is the silent-stub anti-pattern. Replace with bridge_abort().'
    )


def test_required_functions_call_bridge_abort():
    """
    Each must-abort function body must contain a bridge_abort() call.
    """
    src = BRIDGE_CPP.read_text(encoding='utf-8')
    import re

    failures = []
    for fn in MUST_ABORT:
        # Match the function definition + its body.
        pat = re.compile(
            r'\b' + re.escape(fn) + r'\s*\([^)]*\)\s*\{(.*?)\n\}',
            re.DOTALL,
        )
        m = pat.search(src)
        if not m:
            failures.append(f'{fn}: definition not found')
            continue
        body = m.group(1)
        if 'bridge_abort' not in body:
            failures.append(f'{fn}: body does not call bridge_abort()')

    assert not failures, '\n'.join(failures)


def test_capability_probes_do_not_abort():
    """
    is_real() and version() must NOT call bridge_abort — they are
    the documented escape hatch consumers use to detect placeholder
    builds.
    """
    src = BRIDGE_CPP.read_text(encoding='utf-8')
    import re

    failures = []
    for fn in MUST_NOT_ABORT:
        pat = re.compile(
            r'\b' + re.escape(fn) + r'\s*\([^)]*\)\s*\{(.*?)\n\}',
            re.DOTALL,
        )
        m = pat.search(src)
        if not m:
            failures.append(f'{fn}: definition not found')
            continue
        body = m.group(1)
        if 'bridge_abort' in body:
            failures.append(
                f'{fn}: capability probe must not abort — consumers rely '
                f'on it to detect placeholder builds without crashing.'
            )

    assert not failures, '\n'.join(failures)
