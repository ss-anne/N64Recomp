"""
Tier-1 (PINNED BUG): RSP GPR persistence on initial-entry ucode.

Real RSP hardware retains GPRs across task switches. rspboot only
writes $1/$2/$3/$4/$7 before jumping to the loaded ucode; the rest
of the register file is whatever the previous task left behind.

RSPRecomp's `is_permutation = true` branch (overlay-resume entry)
already loads all 32 GPRs from RspContext storage — that is the
correct shape. The `is_permutation = false` branch (initial-entry,
non-overlay-resume cold call) zero-initializes every GPR except
r1=0xFC0.

This asymmetry is the root cause of the Pokémon Stadium audio task
hang: libultra aspMain reads $29 on its first dispatch iteration,
expecting it to inherit from a prior task. Under the current emit,
$29 is always 0 on cold entry, the dispatch reads garbage from
DMEM[0], and the ucode loops forever.

This test PINS the bug. Today it XFAILs. When Path A (persistent
RSP GPRs across all entry shapes) lands, this test starts passing —
without the test, that fix would have nothing in CI to keep it
from regressing later.
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
RSP_RECOMP = REPO / 'RSPRecomp' / 'src' / 'rsp_recomp.cpp'

# Mark every test in this module as expected-fail until the bug is
# resolved. The runner counts XFAILs separately and does NOT include
# them in the exit code, so the suite stays green.
expected_fail = True


def _initial_entry_emit_block() -> str:
    """
    Extract the source-text block that emits the is_permutation=false
    initial-entry function header. We anchor on the unique signature
    string used in that branch and read forward to the closing
    fmt::print of that block.
    """
    src = RSP_RECOMP.read_text(encoding='utf-8')
    # Anchor: the signature string only appears in the else branch.
    anchor = '"RspExitReason {}(uint8_t* rdram, [[maybe_unused]] uint32_t ucode_addr)'
    fallback = '"RspExitReason {}(uint8_t* rdram, '  # in case ucode_addr param is renamed
    idx = src.find(anchor)
    if idx < 0:
        idx = src.find(fallback)
    assert idx >= 0, (
        'initial-entry signature anchor not found in rsp_recomp.cpp — '
        'has the structure changed? Update the anchor in this test.'
    )
    # Read forward until the next "    r1 = 0xFC0;\n", function_name);
    # which terminates the fmt::print for this branch.
    end_marker = 'function_name);'
    end_idx = src.find(end_marker, idx)
    assert end_idx > idx, 'cannot find end of initial-entry emit block'
    return src[idx : end_idx + len(end_marker)]


def test_initial_entry_loads_gprs_from_context():
    """
    The is_permutation=false branch must load r2..r31 from a
    persistent RspContext (ctx->rN), matching real hardware GPR
    retention across task switches. Today it zero-inits — that is
    the bug.
    """
    block = _initial_entry_emit_block()

    # Bug shape: literal "r2 = 0" through "r31 = 0" assignments.
    # Fixed shape: "r2 = ctx->r2" etc.
    has_zero_init = bool(re.search(r'\br2\s*=\s*0\b', block))
    has_ctx_load = 'ctx->r2' in block

    assert not has_zero_init, (
        'is_permutation=false (initial-entry) ucode still zero-inits '
        'GPRs. This breaks any ucode that depends on GPRs inherited '
        'from a prior task — see Pokémon Stadium aspMain $29.'
    )
    assert has_ctx_load, (
        'is_permutation=false branch must load GPRs from ctx->rN '
        'just like is_permutation=true. Otherwise rspboot semantics '
        '(only $1/$2/$3/$4/$7 reset) are violated.'
    )


def test_initial_entry_takes_rspcontext():
    """
    For the initial-entry function to load from persistent storage,
    its signature must take an RspContext* — not just rdram + ucode_addr.
    Today the signature is `(uint8_t* rdram, uint32_t ucode_addr)`,
    which is structurally incapable of expressing GPR persistence.
    """
    block = _initial_entry_emit_block()

    # The bug-shape signature includes "ucode_addr" with no RspContext.
    # The fixed-shape signature must take an RspContext* (named ctx by
    # convention, matching the is_permutation=true branch).
    has_rspcontext_param = bool(re.search(r'RspContext\s*\*\s*ctx', block))
    assert has_rspcontext_param, (
        'initial-entry signature must accept RspContext* ctx like the '
        'is_permutation=true branch. Today it takes only '
        '(uint8_t* rdram, uint32_t ucode_addr) which is structurally '
        'incapable of carrying GPRs across task boundaries.'
    )
