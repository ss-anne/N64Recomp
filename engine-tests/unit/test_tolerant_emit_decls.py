"""
Tier-1: tolerant-emit declarations and emit-site presence.

The tolerant-emit machinery (commits 512191b / ba044e3) is the engine's
ONLY acceptable fallback for instructions/branches the recompiler can't
translate at compile time. It must:

  (a) declare loud-abort runtime hooks in include/recomp.h
  (b) emit calls to those hooks from src/recompilation.cpp
  (c) keep (a) and (b) in lockstep — stale decls or unbacked emit
      sites are both bugs

All six hooks are load-bearing. There is no "store the bits in flat
storage and trust the game won't notice" fallback for any unmodeled
behavior, including unmodeled COP0 registers — that would be a stub
in violation of PRINCIPLES.md #12.
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / 'include' / 'recomp.h'
RECOMP_CPP = REPO / 'src' / 'recompilation.cpp'

# Hooks that MUST be both declared in the header AND actively emitted
# from recompilation.cpp.
ACTIVE_HOOKS = [
    'recomp_unhandled_branch',
    'recomp_unhandled_call',
    'recomp_unhandled_jalr',
    'recomp_unhandled_cop0_read',
    'recomp_unhandled_cop0_write',
    'recomp_unhandled_instruction',
]


def test_header_declares_all_active_hooks():
    header = HEADER.read_text(encoding='utf-8')
    missing = [h for h in ACTIVE_HOOKS if h not in header]
    assert not missing, f'include/recomp.h missing tolerant-emit decls: {missing}'


def test_recompilation_emits_all_active_hooks():
    src = RECOMP_CPP.read_text(encoding='utf-8')
    missing = [h for h in ACTIVE_HOOKS if h not in src]
    assert not missing, (
        f'src/recompilation.cpp does not emit calls to: {missing}. '
        'Tolerant-emit was either removed or replaced with a silent '
        'storage path; the engine would be dropping untranslatable '
        'instructions without a runtime abort.'
    )


def test_no_unknown_recomp_unhandled_emit_sites():
    """
    Inverse direction: every recomp_unhandled_* identifier emitted by
    recompilation.cpp must be declared in recomp.h. Catches typos and
    drift where a new emit site lands without the matching decl.
    """
    src = RECOMP_CPP.read_text(encoding='utf-8')
    header = HEADER.read_text(encoding='utf-8')

    emitted = set(re.findall(r'recomp_unhandled_[a-z0-9_]+', src))
    undeclared = sorted(h for h in emitted if h not in header)
    assert not undeclared, (
        f'src/recompilation.cpp emits calls to undeclared hooks: '
        f'{undeclared}. Add decls in include/recomp.h.'
    )


def test_no_cop0_flat_storage_fallback():
    """
    Guard against (re)introducing the ctx->cop0_regs[] flat-storage
    fallback for unmodeled COP0 registers. Reverted on 2026-04-26 as
    a stub-in-disguise: silently storing bits and reading them back
    masks side-effecting registers (Count/Compare/Cause). The correct
    fallback is the loud-abort tolerant-emit path.
    """
    header = HEADER.read_text(encoding='utf-8')
    src = RECOMP_CPP.read_text(encoding='utf-8')

    assert 'cop0_regs[' not in header, (
        'cop0_regs[] storage reintroduced in recomp_context. This is '
        'the stub-in-disguise fallback that silently masks Count/'
        'Compare/Cause side effects. Use recomp_unhandled_cop0_* '
        'tolerant-emit hooks instead.'
    )
    assert 'ctx->cop0_regs[' not in src, (
        'recompilation.cpp emits writes to ctx->cop0_regs[]. This is '
        'the stub-in-disguise fallback. Use recomp_unhandled_cop0_* '
        'tolerant-emit hooks instead.'
    )
