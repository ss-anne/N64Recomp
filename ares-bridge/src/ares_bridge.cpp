/*
 * ares_bridge.cpp — stub implementation.
 *
 * Every entry point returns ARES_BRIDGE_NOT_IMPLEMENTED. This
 * compiles cleanly so consumer code can reference the API today;
 * it does NOT actually run Ares.
 *
 * Real implementation lands when WITH_ARES_BRIDGE=ON pulls in the
 * Ares submodule and ares_core_glue.cpp wires up the C++ core.
 * See DESIGN.md.
 */

#include "ares_bridge.h"

extern "C" {

ares_status_t ares_init(const char *) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

void ares_shutdown(void) {
    /* no-op in stub */
}

ares_status_t ares_reset(void) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_step_frame(void) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_step_instruction(void) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_read_cpu_register(int, uint64_t *) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_read_pc(uint32_t *) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_read_hi_lo(uint64_t *, uint64_t *) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_read_memory(uint32_t, void *, size_t) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_set_controller(int, const ares_input_t *) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_save_state(void *, size_t, size_t *out_len) {
    if (out_len) *out_len = 0;
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

ares_status_t ares_load_state(const void *, size_t) {
    return ARES_BRIDGE_NOT_IMPLEMENTED;
}

int ares_bridge_is_real(void) {
#ifdef N64RECOMP_ARES_BRIDGE_REAL
    return 1;
#else
    return 0;
#endif
}

const char *ares_bridge_version(void) {
#ifdef N64RECOMP_ARES_BRIDGE_REAL
    /* The real implementation will report the linked Ares git
     * SHA via a CMake-generated string. */
    return "ares-bridge: real but ares_core_glue.cpp not "
           "implemented yet";
#else
    return "stub";
#endif
}

} /* extern "C" */
