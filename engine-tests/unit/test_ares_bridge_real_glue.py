"""
Tier-1: ares_core_glue.cpp invariants for Phase-1 real bridge.

Pins:
  - ares_core_glue.cpp exists (Phase 1 deliverable)
  - It is gated symmetrically with ares_bridge.cpp via the
    N64RECOMP_ARES_BRIDGE_REAL define so we can't get duplicate
    symbol definitions
  - It implements ares_init() with real ROM-load behavior, not
    a glue_unimplemented() placeholder (Phase 1 contract: lifecycle
    handshake works)
  - All other functional entry points still abort loudly via
    glue_unimplemented() — no silent stubs in real-mode glue either

When a phase lands (e.g., Phase 3 wires ares_read_pc), update the
PHASE_1_DONE list to include the newly-real entry point so this test
keeps catching the next regression.
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
GLUE_CPP = REPO / 'ares-bridge' / 'src' / 'ares_core_glue.cpp'
PLACEHOLDER_CPP = REPO / 'ares-bridge' / 'src' / 'ares_bridge.cpp'

# Entry points that are real (not glue_unimplemented) at the end of
# Phase 1.
PHASE_1_DONE = ['ares_init', 'ares_shutdown']

# Entry points that must still abort via glue_unimplemented because
# their phases are pending.
STILL_PENDING = [
    'ares_reset',                # Phase 2
    'ares_step_frame',           # Phase 4
    'ares_step_instruction',     # Phase 4
    'ares_read_cpu_register',    # Phase 3
    'ares_read_pc',              # Phase 3
    'ares_read_hi_lo',           # Phase 3
    'ares_read_memory',          # Phase 3
    'ares_set_controller',       # Phase 6
    'ares_save_state',           # Phase 7
    'ares_load_state',           # Phase 7
]


def test_glue_file_exists():
    assert GLUE_CPP.is_file(), (
        f'{GLUE_CPP} missing — Phase 1 deliverable was a real glue.'
    )


def test_placeholder_is_gated_by_real_define():
    """
    Without this gate, building with WITH_ARES_BRIDGE=ON pulls both
    files into the same static lib and the linker rejects duplicate
    definitions of every entry point.
    """
    src = PLACEHOLDER_CPP.read_text(encoding='utf-8')
    assert '#ifndef N64RECOMP_ARES_BRIDGE_REAL' in src, (
        'ares_bridge.cpp must guard its body with '
        '#ifndef N64RECOMP_ARES_BRIDGE_REAL so the real glue can own '
        'the symbols when WITH_ARES_BRIDGE=ON.'
    )
    assert '#endif /* !N64RECOMP_ARES_BRIDGE_REAL */' in src, (
        'ares_bridge.cpp missing the closing #endif marker for the '
        'N64RECOMP_ARES_BRIDGE_REAL gate.'
    )


def _glue_function_body(fn: str) -> str:
    src = GLUE_CPP.read_text(encoding='utf-8')
    pat = re.compile(
        r'\b' + re.escape(fn) + r'\s*\([^)]*\)\s*\{(.*?)\n\}',
        re.DOTALL,
    )
    m = pat.search(src)
    assert m, f'{fn}: definition not found in ares_core_glue.cpp'
    return m.group(1)


def test_phase_1_entries_are_real():
    """
    PHASE_1_DONE entries must NOT call glue_unimplemented — they must
    do actual work. Today: ares_init reads the ROM, ares_shutdown
    clears state.
    """
    for fn in PHASE_1_DONE:
        body = _glue_function_body(fn)
        assert 'glue_unimplemented' not in body, (
            f'{fn}: marked as Phase-1 done but still calls '
            f'glue_unimplemented. Either move it to STILL_PENDING in '
            f'this test, or implement it.'
        )


def test_pending_entries_abort_loudly():
    """
    STILL_PENDING entries must call glue_unimplemented() — the
    no-stubs rule applies to the real glue too. A pending entry that
    quietly returns a status code is a stub.
    """
    for fn in STILL_PENDING:
        body = _glue_function_body(fn)
        assert 'glue_unimplemented' in body, (
            f'{fn}: must call glue_unimplemented() until its phase '
            f'lands. Quietly returning a status code is a stub. If '
            f'this entry was actually implemented, move it to '
            f'PHASE_1_DONE in this test.'
        )
