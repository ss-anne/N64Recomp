/*
 * smoke_oracle_boot.cpp — Stages A + B end-to-end smoke test.
 *
 * Exercises the Phase 2 lifecycle and the always-on RSP trace ring:
 *
 *   1. ares-bridge links cleanly against ares.lib + dependencies
 *      (no unresolved-external-symbol errors).
 *   2. ares::Nintendo64::system constructs without crashing when
 *      handed our OraclePlatform's hand-crafted Stadium pak.
 *   3. system.run() returns after one frame (i.e. CPU::main()'s
 *      vi.refreshed exit condition fires).
 *   4. The RSP per-instruction trace hook fires and records events
 *      into the ring as the boot trampoline runs IPL3.
 *
 * Usage:
 *
 *   smoke_oracle_boot <rom_path>
 *
 * Exit code 0 on success, nonzero on any failure (with diagnostic to
 * stderr). Designed to be cheap to invoke from the engine-tests
 * harness or a developer's hand.
 */

#include "ares_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const char *status_name(ares_status_t s) {
    switch (s) {
        case ARES_BRIDGE_OK:                  return "OK";
        case ARES_BRIDGE_NOT_IMPLEMENTED:     return "NOT_IMPLEMENTED";
        case ARES_BRIDGE_NOT_INITIALIZED:     return "NOT_INITIALIZED";
        case ARES_BRIDGE_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case ARES_BRIDGE_INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
        case ARES_BRIDGE_ROM_LOAD_FAILED:     return "ROM_LOAD_FAILED";
        case ARES_BRIDGE_STATE_BUFFER_TOO_SMALL: return "STATE_BUFFER_TOO_SMALL";
        case ARES_BRIDGE_INTERNAL_ERROR:      return "INTERNAL_ERROR";
        default: return "UNKNOWN";
    }
}

#define CHECK(call, expected) do {                                       \
    ares_status_t _s = (call);                                           \
    if (_s != (expected)) {                                              \
        std::fprintf(stderr,                                             \
            "SMOKE FAIL: %s returned %s, expected %s\n",                 \
            #call, status_name(_s), status_name(expected));              \
        return 2;                                                        \
    }                                                                    \
} while (0)

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <rom_path>\n", argv[0]);
        return 1;
    }

    if (!ares_bridge_is_real()) {
        std::fprintf(stderr,
            "SMOKE FAIL: ares_bridge_is_real() returned 0 — bridge "
            "was built without WITH_ARES_BRIDGE=ON. The smoke test "
            "is meaningless against a placeholder.\n");
        return 1;
    }

    std::printf("ares-bridge version: %s\n", ares_bridge_version());

    /* Lifecycle */
    CHECK(ares_init(argv[1]), ARES_BRIDGE_OK);
    std::printf("[1/3] ares_init OK\n");

    CHECK(ares_reset(), ARES_BRIDGE_OK);
    std::printf("[2/3] ares_reset OK — N64 system constructed and powered\n");

    /* Confirm the Stage B trace hook is wired before stepping. If the
     * static initializer in ares_rsp_ring.cpp ran, the ring is enabled
     * by default. */
    std::printf("[3/6] ares_rsp_trace_is_enabled = %d (expected 1)\n",
                ares_rsp_trace_is_enabled());

    /* Run 60 frames (~1 second of emulated time). Stadium's boot does
     * a fair amount of CPU-side work (CIC handshake, IPL3 trampoline,
     * RDRAM init) before kicking the RSP, so a single frame may show
     * zero RSP activity. */
    for (int i = 0; i < 60; i++) {
        CHECK(ares_step_frame(), ARES_BRIDGE_OK);
    }
    std::printf("[4/6] ares_step_frame x 60 OK\n");

    uint64_t trace_count = ares_rsp_trace_count();
    std::printf("[5/6] ares_rsp_trace_count = %llu events\n",
                (unsigned long long)trace_count);

    if (trace_count == 0) {
        std::fprintf(stderr,
            "SMOKE FAIL: trace_count is still zero after 60 frames. "
            "Either the hook isn't installed (linker GC'd "
            "ares_rsp_external_instruction_hook's static initializer), "
            "or Stadium's boot keeps the RSP halted longer than "
            "1s of emulated time. Investigate by inspecting the "
            "ares.lib symbol table for "
            "ares_rsp_external_instruction_hook.\n");
        return 3;
    }

    /* Inspect the most recent event to validate field plumbing. */
    ares_rsp_trace_event_t ev = {};
    if (ares_rsp_trace_get(trace_count - 1, &ev)) {
        std::printf("    last event: pc=0x%03X r1=0x%08X r3=0x%08X "
                    "r29=0x%08X r31=0x%08X dma_mem=0x%08X "
                    "status=0x%08X\n",
                    ev.pc, ev.gpr[1], ev.gpr[3],
                    ev.gpr[29], ev.gpr[31], ev.dma_mem_addr,
                    ev.status);
    }

    /* And boot snapshot: should have captured the very first events. */
    std::printf("[6/6] boot snapshot: %u events captured\n",
                ares_rsp_trace_boot_count());
    ares_rsp_trace_event_t boot_ev = {};
    if (ares_rsp_trace_boot_get(0, &boot_ev)) {
        std::printf("    first event: pc=0x%03X seq=%llu\n",
                    boot_ev.pc, (unsigned long long)boot_ev.seq);
    }

    ares_shutdown();
    std::printf("smoke ok\n");
    return 0;
}
