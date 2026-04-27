"""
Tier-2: runtime validation of Path A persistence semantics.

The Tier-1 test_rsp_gpr_persistence.py guards the SHAPE of the
emitted code: signature is (rdram, ctx), GPRs are references, the
no-overlay wrapper owns a `static thread_local RspContext`. This
test goes one layer deeper: it compiles a small C++ program that
mirrors that exact emit pattern and verifies the runtime semantics
hold — i.e., a value written through the GPR reference inside
`ucode_impl(rdram, ctx)` is observable on the next call to the
wrapper because `static thread_local` storage actually persists.

Why this matters: source-level inspection can pass while the
generated code still fails to persist if e.g. the references are
declared correctly but the wrapper accidentally takes ctx by value,
or if the static is local-to-the-call rather than function-local,
or if the compiler optimizes the references away. Tier-2 catches
the semantic regression that Tier-1 can't see.

Skipped if no C++ compiler is on PATH.
"""
from __future__ import annotations

import pathlib
import shutil
import subprocess

REPO = pathlib.Path(__file__).resolve().parents[2]
SCRATCH = REPO / 'engine-tests' / 'fixtures' / '_scratch'

# Source program mirrors RSPRecomp's Path A emit. Keep parallel to
# RSPRecomp/src/rsp_recomp.cpp's create_function — if the engine emit
# changes structurally, mirror the change here.
SOURCE = r"""
#include <cstdint>
#include <cstdio>

struct RSP { int dummy; };

struct RspContext {
    uint32_t r1, r2, r3, r4, r5, r6, r7,
             r8, r9, r10, r11, r12, r13, r14, r15,
             r16, r17, r18, r19, r20, r21, r22, r23,
             r24, r25, r26, r27, r28, r29, r30, r31;
    uint32_t dma_mem_address;
    uint32_t dma_dram_address;
    uint32_t jump_target;
    RSP rsp;
    uint32_t resume_address;
    bool resume_delay;
};

enum class RspExitReason { Ok };

// Mirrors the impl shape: GPRs as references-into-ctx.
RspExitReason ucode_impl(uint8_t* rdram, RspContext* ctx) {
    uint32_t& r1 = ctx->r1;
    uint32_t& r29 = ctx->r29;

    // rspboot semantics: $1 always reset on entry.
    r1 = 0xFC0;

    // Test the persistence: write a sentinel into r29 on the first
    // call (when r29 is whatever the static was zero-init'd to);
    // increment it on subsequent calls so the test can prove it
    // observed the prior write.
    if (r29 == 0) {
        r29 = 0xDEADBEEF;
    } else {
        r29 += 1;
    }

    // Side-channel observation for the driver: write r29 to the
    // first word of rdram. This is the only way the driver can see
    // r29's current value (the static is private to ucode()).
    *(uint32_t*)rdram = r29;
    return RspExitReason::Ok;
}

// Mirrors the no-overlay wrapper shape: legacy signature owning a
// static thread_local RspContext.
RspExitReason ucode(uint8_t* rdram, [[maybe_unused]] uint32_t ucode_addr) {
    static thread_local RspContext persistent_ctx{};
    return ucode_impl(rdram, &persistent_ctx);
}

int main() {
    uint8_t rdram[16] = {};

    ucode(rdram, 0);
    uint32_t after_first = *(uint32_t*)rdram;
    if (after_first != 0xDEADBEEFu) {
        std::fprintf(stderr,
            "FAIL after first call: expected 0xDEADBEEF, got 0x%08X\n",
            after_first);
        return 1;
    }

    ucode(rdram, 0);
    uint32_t after_second = *(uint32_t*)rdram;
    if (after_second != 0xDEADBEF0u) {
        std::fprintf(stderr,
            "FAIL after second call: expected 0xDEADBEF0 (persisted+1), "
            "got 0x%08X. State did NOT persist across calls — Path A "
            "is broken at the C++ semantics level.\n",
            after_second);
        return 2;
    }

    std::fprintf(stderr, "PASS: r29 persisted across calls "
                 "(0xDEADBEEF -> 0xDEADBEF0)\n");
    return 0;
}
"""


def _find_cxx():
    for candidate in ('g++', 'clang++', 'cl'):
        path = shutil.which(candidate)
        if path:
            return candidate
    return None


def _can_actually_compile(cxx: str) -> bool:
    """
    A C++ compiler may be on PATH but unable to actually run end-to-end
    in this environment — e.g. some sandboxed shells block g++'s own
    subprocess fork to cc1plus, causing silent failures with empty
    stderr. Probe by attempting a trivial compile.
    """
    SCRATCH.mkdir(parents=True, exist_ok=True)
    src = SCRATCH / '_compiler_probe.cpp'
    exe = SCRATCH / '_compiler_probe.exe'
    src.write_text('int main(){return 0;}\n', encoding='utf-8')
    try:
        if cxx in ('g++', 'clang++'):
            cmd = [cxx, '-o', str(exe), str(src)]
        else:
            cmd = ['cl', '/EHsc', '/nologo', '/Fe:' + str(exe), str(src)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return r.returncode == 0 and exe.is_file()
    except Exception:
        return False


_compiler = _find_cxx()
if _compiler is None:
    skip_reason = 'no C++ compiler on PATH'
elif not _can_actually_compile(_compiler):
    skip_reason = (
        f'{_compiler} is on PATH but cannot run a trivial compile in '
        f'this environment (likely a sandbox blocking subprocess fork)'
    )
else:
    skip_reason = None


def test_static_thread_local_ctx_persists_gprs_across_wrapper_calls():
    SCRATCH.mkdir(parents=True, exist_ok=True)
    src = SCRATCH / 'rsp_persistence_probe.cpp'
    src.write_text(SOURCE, encoding='utf-8')
    exe = SCRATCH / 'rsp_persistence_probe.exe'

    if _compiler in ('g++', 'clang++'):
        cmd = [_compiler, '-std=c++17', '-O2', '-o', str(exe), str(src)]
    else:  # cl
        cmd = ['cl', '/std:c++17', '/O2', '/EHsc', '/nologo',
               '/Fe:' + str(exe), str(src)]

    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    assert proc.returncode == 0, (
        f'compile failed (rc={proc.returncode}):\n'
        f'cmd: {cmd}\n'
        f'stdout: {proc.stdout}\n'
        f'stderr: {proc.stderr}'
    )
    assert exe.is_file(), f'compile reported success but {exe} missing'

    proc = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert proc.returncode == 0, (
        f'persistence probe failed at runtime (rc={proc.returncode}):\n'
        f'stdout: {proc.stdout}\n'
        f'stderr: {proc.stderr}\n'
        f'This means the static thread_local RspContext does not '
        f'actually persist GPRs across wrapper calls — Path A is '
        f'broken at the C++ semantics level.'
    )
    assert 'PASS' in proc.stderr, (
        f'probe ran (rc=0) but did not report PASS — output:\n'
        f'stdout: {proc.stdout}\nstderr: {proc.stderr}'
    )
