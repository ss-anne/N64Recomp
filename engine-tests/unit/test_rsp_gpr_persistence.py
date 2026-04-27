"""
Tier-1: RSP GPR persistence on initial-entry ucode (Path A landed).

Real RSP hardware retains GPRs across task switches. rspboot only
writes $1/$2/$3/$4/$7 before jumping to the loaded ucode; the rest
of the register file is whatever the previous task left behind.

Pinned by Path A in RSPRecomp/src/rsp_recomp.cpp:

  - All GPRs (and dma_*, jump_target, rsp) are emitted as C++
    references into *ctx — writes auto-persist through to the
    backing RspContext, no manual store-back needed.
  - The no-overlay case emits an `_impl(rdram, ctx)` function plus
    a legacy-ABI wrapper `(rdram, ucode_addr)` that owns a
    `static thread_local RspContext` so GPRs persist across
    run_task calls without requiring a librecomp ABI change.
  - The overlay-swap function's ctx was promoted to
    static thread_local for the same reason.
  - write_overlay_swap_return no longer hand-flushes ctx fields —
    references handle it.

This test guards against regression of any of those properties.
"""
from __future__ import annotations

import pathlib

REPO = pathlib.Path(__file__).resolve().parents[2]
RSP_RECOMP = (REPO / 'RSPRecomp' / 'src' / 'rsp_recomp.cpp').read_text(encoding='utf-8')


def test_no_overlay_path_emits_impl_with_ctx():
    """
    The is_permutation=false branch (no-overlay case) must emit the
    work function as `<name>_impl(rdram, RspContext* ctx)`. The
    impl_function_name local in create_function carries the suffix.
    """
    assert 'function_name + "_impl"' in RSP_RECOMP, (
        'create_function no longer suffixes the no-overlay impl with '
        '_impl. Path A regressed: the wrapper / impl split is what '
        'preserves the runtime ABI while delivering GPR persistence.'
    )


def test_no_overlay_path_emits_legacy_wrapper():
    """
    After the impl, a wrapper with the legacy `(rdram, ucode_addr)`
    signature must be emitted owning a static thread_local
    RspContext. That's the persistent storage for cross-run_task
    GPR retention.
    """
    assert 'static thread_local RspContext persistent_ctx' in RSP_RECOMP, (
        'no-overlay wrapper missing static thread_local RspContext. '
        'Without it, GPRs reset to zero on every run_task call and '
        'rspboot semantics ($1/$2/$3/$4/$7-only reset) are violated.'
    )
    assert '_impl(rdram, &persistent_ctx)' in RSP_RECOMP, (
        'no-overlay wrapper does not call <name>_impl with the '
        'persistent ctx pointer.'
    )


def test_gprs_emitted_as_references_into_ctx():
    """
    Both is_permutation=true and is_permutation=false branches must
    emit GPRs as references (`uint32_t& r1 = ctx->r1`) rather than
    value copies (`uint32_t r1 = ctx->r1`). References make every
    write auto-persist through to ctx, eliminating the need for
    store-back at exit points and removing the entire class of
    "exit X forgot to flush state Y" bugs.
    """
    assert 'uint32_t&                 r1 = ctx->r1' in RSP_RECOMP, (
        'GPR locals are not declared as references into ctx. Path A '
        'regressed: value copies require manual store-back at every '
        'exit, which is fragile and was the original bug.'
    )
    assert 'RSP& rsp = ctx->rsp' in RSP_RECOMP, (
        'rsp local is not a reference into ctx — RSP state writes '
        'will not persist across exits.'
    )
    # Bug shape — must NOT be present (value-copy GPR declaration).
    assert 'uint32_t                 r1 = ctx->r1,   r2 = ctx->r2' not in RSP_RECOMP, (
        'value-copy GPR declaration reintroduced. Use references.'
    )
    # Bug shape — zero-init initial-entry (the original Stadium bug).
    assert 'uint32_t           r1 = 0,  r2 = 0' not in RSP_RECOMP, (
        'zero-init GPR declaration reintroduced — initial-entry will '
        'lose state inherited from prior tasks (Pokémon Stadium '
        'aspMain $29 bug).'
    )


def test_overlay_swap_function_owns_persistent_ctx():
    """
    The overlay-swap wrapper (create_overlay_swap_function) must
    own a static thread_local RspContext for the same reason as the
    no-overlay wrapper. Stack-local ctx loses state every call.
    """
    assert 'static thread_local RspContext ctx' in RSP_RECOMP, (
        'overlay-swap function uses stack-local RspContext. State is '
        'lost on every entry, breaking cross-task GPR retention for '
        'overlay-using ucodes (Zelda-style aspMain).'
    )


def test_overlay_swap_return_no_longer_stores_back():
    """
    With references, write_overlay_swap_return no longer needs to
    flush ctx->rN = rN, ctx->dma_* = dma_*, etc. — those are aliases.
    The function should be reduced to just the label + return.
    """
    assert 'ctx->r1 = r1;   ctx->r2 = r2' not in RSP_RECOMP, (
        'write_overlay_swap_return still emits manual GPR store-back. '
        'With references-into-ctx that is dead code at best and a '
        'self-assignment-with-tearing bug at worst.'
    )
