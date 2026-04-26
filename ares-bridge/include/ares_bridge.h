/*
 * ares_bridge.h — generic N64Recomp oracle bridge to Ares.
 *
 * Stable C API for embedding the Ares N64 core as a divergence
 * oracle. Header is intended to be consumed by any N64Recomp
 * runner regardless of the underlying game.
 *
 * Status: API frozen, implementation pending. All entry points
 * currently return ARES_BRIDGE_NOT_IMPLEMENTED.
 */

#ifndef N64RECOMP_ARES_BRIDGE_H
#define N64RECOMP_ARES_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Status codes returned by every API call                            */
/* ------------------------------------------------------------------ */

typedef enum {
    ARES_BRIDGE_OK                  = 0,
    ARES_BRIDGE_NOT_IMPLEMENTED     = 1,
    ARES_BRIDGE_NOT_INITIALIZED     = 2,
    ARES_BRIDGE_ALREADY_INITIALIZED = 3,
    ARES_BRIDGE_INVALID_ARGUMENT    = 4,
    ARES_BRIDGE_ROM_LOAD_FAILED     = 5,
    ARES_BRIDGE_STATE_BUFFER_TOO_SMALL = 6,
    ARES_BRIDGE_INTERNAL_ERROR      = 7,
} ares_status_t;

/* ------------------------------------------------------------------ */
/* Controller input (minimal N64 layout)                              */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Buttons — bit-packed, layout matches N64 controller register */
    uint16_t buttons;        /* A, B, Z, Start, D-pad, L/R, C-pad */
    int8_t   stick_x;        /* analog stick, signed -128..127    */
    int8_t   stick_y;
    /* Reserved for rumble pak / mempak / transfer pak signaling   */
    uint16_t flags;
} ares_input_t;

/* ------------------------------------------------------------------ */
/* CPU register IDs (matches MIPS R4300 conventions)                  */
/* ------------------------------------------------------------------ */

enum {
    ARES_REG_R0 = 0, ARES_REG_AT, ARES_REG_V0, ARES_REG_V1,
    ARES_REG_A0, ARES_REG_A1, ARES_REG_A2, ARES_REG_A3,
    ARES_REG_T0, ARES_REG_T1, ARES_REG_T2, ARES_REG_T3,
    ARES_REG_T4, ARES_REG_T5, ARES_REG_T6, ARES_REG_T7,
    ARES_REG_S0, ARES_REG_S1, ARES_REG_S2, ARES_REG_S3,
    ARES_REG_S4, ARES_REG_S5, ARES_REG_S6, ARES_REG_S7,
    ARES_REG_T8, ARES_REG_T9, ARES_REG_K0, ARES_REG_K1,
    ARES_REG_GP, ARES_REG_SP, ARES_REG_FP, ARES_REG_RA,
    ARES_REG_COUNT_GPR
};

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/* Initialize the embedded Ares N64 core and load `rom_path`. Returns
 * ARES_BRIDGE_ALREADY_INITIALIZED if called twice without an
 * intervening ares_shutdown(). */
ares_status_t ares_init(const char *rom_path);

/* Free all resources. Safe to call multiple times. */
void ares_shutdown(void);

/* Reset to power-on state. ROM remains loaded. */
ares_status_t ares_reset(void);

/* ------------------------------------------------------------------ */
/* Stepping                                                           */
/* ------------------------------------------------------------------ */

/* Advance one frame (until next VI vsync). Suitable for coarse
 * frame-by-frame divergence checks. */
ares_status_t ares_step_frame(void);

/* Advance one CPU instruction. Suitable for fine-grained
 * instruction-level divergence checks once a frame-level
 * divergence has been localized. */
ares_status_t ares_step_instruction(void);

/* ------------------------------------------------------------------ */
/* State read                                                         */
/* ------------------------------------------------------------------ */

/* Read a general-purpose register. `reg` is one of ARES_REG_*. */
ares_status_t ares_read_cpu_register(int reg, uint64_t *out);

/* Read the program counter. */
ares_status_t ares_read_pc(uint32_t *out);

/* Read the HI/LO multiply-divide registers. */
ares_status_t ares_read_hi_lo(uint64_t *hi, uint64_t *lo);

/* Read N bytes of RDRAM at virtual address `vaddr`. Performs MIPS
 * TLB translation if necessary. Returns ARES_BRIDGE_INVALID_ARGUMENT
 * for unmapped addresses. */
ares_status_t ares_read_memory(uint32_t vaddr, void *buf, size_t len);

/* ------------------------------------------------------------------ */
/* Input                                                              */
/* ------------------------------------------------------------------ */

/* Set controller state for the given port (0-3). Persists until
 * the next ares_set_controller() call for that port. */
ares_status_t ares_set_controller(int port, const ares_input_t *in);

/* ------------------------------------------------------------------ */
/* State save / load (for rewind debugging)                           */
/* ------------------------------------------------------------------ */

/* Serialize complete emulator state into `buf`. If `buf` is NULL or
 * `buf_len` is too small, sets `*out_len` to the required size and
 * returns ARES_BRIDGE_STATE_BUFFER_TOO_SMALL. Otherwise writes the
 * state and sets `*out_len` to the bytes written. */
ares_status_t ares_save_state(void *buf, size_t buf_len, size_t *out_len);

/* Restore state previously serialized by ares_save_state. The ROM
 * must already be loaded; ares_load_state does not change the
 * loaded ROM. */
ares_status_t ares_load_state(const void *buf, size_t buf_len);

/* ------------------------------------------------------------------ */
/* Build / capability introspection                                   */
/* ------------------------------------------------------------------ */

/* Returns 1 if the bridge was compiled with WITH_ARES_BRIDGE=ON
 * (i.e., real Ares is linked in), 0 if this is a stub-only build. */
int ares_bridge_is_real(void);

/* Returns a const string identifying the linked Ares version, or
 * "stub" if ares_bridge_is_real() returns 0. */
const char *ares_bridge_version(void);

#ifdef __cplusplus
}
#endif

#endif /* N64RECOMP_ARES_BRIDGE_H */
