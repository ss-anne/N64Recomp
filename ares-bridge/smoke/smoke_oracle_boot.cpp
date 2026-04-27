/*
 * smoke_oracle_boot.cpp — Stage A end-to-end smoke test.
 *
 * Exercises the Phase 2 lifecycle: ares_init → ares_reset →
 * ares_step_frame. Validates that:
 *
 *   1. ares-bridge links cleanly against ares.lib + dependencies
 *      (no unresolved-external-symbol errors).
 *   2. ares::Nintendo64::system constructs without crashing when
 *      handed our OraclePlatform's hand-crafted Stadium pak.
 *   3. system.run() returns after one frame (i.e. CPU::main()'s
 *      vi.refreshed exit condition fires).
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

    /* Step a single frame. cpu.main() runs until vi.refreshed sets,
     * which is one VI vsync (~1/60s of emulated time). On real
     * hardware this is ~16.6ms; in our HLE-mode oracle it should be
     * faster but bounded by the actual emulated work. */
    CHECK(ares_step_frame(), ARES_BRIDGE_OK);
    std::printf("[3/3] ares_step_frame OK — first frame completed\n");

    ares_shutdown();
    std::printf("smoke ok\n");
    return 0;
}
