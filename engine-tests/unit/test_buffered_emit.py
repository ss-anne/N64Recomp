"""
Tier-1: per-function output buffering.

Regression for f4cf3eb. Before that fix, recompile_function called
output_file.clear() on failure — which only resets stream FLAGS, not
buffered TEXT — leaving partial function output (signature + open
braces, no body) in shared multi-function output files. The fix
buffers each function into an ostringstream and only commits to the
real output_file on success.

If anyone reverts to writing directly into output_file from
recompile_function, this test fires.
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]


def test_recompile_function_uses_ostringstream_buffer():
    src = (REPO / 'src' / 'recompilation.cpp').read_text(encoding='utf-8')

    # Locate the recompile_function definition (the public wrapper, not
    # _impl). It must contain an ostringstream buffer and gate the
    # write to output_file on success.
    body = re.search(
        r'bool\s+N64Recomp::recompile_function\s*\([^)]*\)\s*\{(.*?)\n\}',
        src, re.DOTALL,
    )
    assert body, 'recompile_function definition not found in recompilation.cpp'
    body_text = body.group(1)

    assert 'ostringstream' in body_text, (
        'recompile_function must buffer emission into an ostringstream '
        'before committing to output_file (regression for f4cf3eb)'
    )
    assert 'if (ok)' in body_text or 'if(ok)' in body_text, (
        'recompile_function must gate the output_file write on success — '
        'otherwise partial-function output can leak into multi-function '
        'files (regression for f4cf3eb)'
    )


def _strip_cpp_comments(s: str) -> str:
    # Remove /* ... */ block comments (DOTALL) and // ... line comments.
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.DOTALL)
    s = re.sub(r'//[^\n]*', '', s)
    return s


def test_no_direct_clear_on_output_file():
    """
    The historical bug was `output_file.clear()` — which only resets
    stream flags, not text. If someone reintroduces it inside
    recompile_function or recompile_function_impl, that's the bug
    coming back.

    We strip comments before scanning so the historical reference to
    the bug in the docstring of recompile_function doesn't trigger.
    """
    src = (REPO / 'src' / 'recompilation.cpp').read_text(encoding='utf-8')
    code_only = _strip_cpp_comments(src)
    bad = re.search(r'\boutput_file\s*\.\s*clear\s*\(\s*\)', code_only)
    assert not bad, (
        'output_file.clear() reintroduced — this only resets stream '
        'flags, not buffered text. Use the ostringstream buffer pattern '
        'in recompile_function instead. See f4cf3eb.'
    )
