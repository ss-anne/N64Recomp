/*
 * ares_core_glue.cpp — real Ares N64 core integration.
 *
 * This file is compiled only when WITH_ARES_BRIDGE=ON. It overrides
 * the placeholder bodies from ares_bridge.cpp by providing real
 * implementations linked at static-library level.
 *
 * Phase 1 (current): ROM open + header validation + lifecycle stubs.
 * Phase 2:           Node tree construction, ROM mount via cartridge.
 * Phase 3:           Register/memory state read.
 * Phase 4:           Stepping (frame + instruction).
 * Phase 5:           Validation against Zelda64Recomp + OoT.
 * Phase 6:           Input plumbing.
 * Phase 7:           Save/load state.
 *
 * Symbol resolution: when WITH_ARES_BRIDGE=ON, both ares_bridge.cpp
 * and this file are compiled into the same static library. The linker
 * uses the strong definitions from this file and discards the
 * placeholder weak/non-weak definitions in ares_bridge.cpp via the
 * "first wins" rule for static libs — we must be careful that ONLY
 * ONE definition of each symbol is reachable per consumer build. To
 * avoid that hazard we use distinct symbol names internally and
 * #ifdef out the placeholder bodies when N64RECOMP_ARES_BRIDGE_REAL
 * is defined.
 *
 * That gating is enforced by ares_bridge.cpp checking
 * N64RECOMP_ARES_BRIDGE_REAL — when set, the placeholder file
 * compiles to an empty translation unit and this file owns the
 * symbols.
 */

#include "ares_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <vector>
#include <mutex>

namespace {

struct BridgeState {
    bool initialized = false;
    std::vector<uint8_t> rom_bytes;
    std::string rom_path;
};

BridgeState g_state;
std::mutex g_state_mutex;

[[noreturn]] void glue_unimplemented(const char *fn, const char *reason) {
    std::fprintf(stderr,
        "\nares-bridge (real): %s not yet implemented.\n"
        "                    %s\n"
        "                    See ares-bridge/DESIGN.md for phase plan.\n",
        fn, reason);
    std::fflush(stderr);
    std::abort();
}

/* Byteswap big-endian (.z64) ROM headers; .v64 / .n64 require a full
 * byteswap pass to canonicalize. We tolerate all three formats so the
 * oracle can consume whatever the test harness happens to have. */
bool detect_and_canonicalize_rom_format(std::vector<uint8_t> &bytes) {
    if (bytes.size() < 4) return false;
    uint32_t magic = (uint32_t(bytes[0]) << 24) |
                     (uint32_t(bytes[1]) << 16) |
                     (uint32_t(bytes[2]) <<  8) |
                     (uint32_t(bytes[3]) <<  0);
    switch (magic) {
        case 0x80371240u: /* .z64 — big-endian, native N64 layout */
            return true;
        case 0x37804012u: /* .v64 — byteswapped (16-bit) */
            for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
                std::swap(bytes[i], bytes[i + 1]);
            }
            return true;
        case 0x40123780u: /* .n64 — wordswapped (32-bit) */
            for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
                std::swap(bytes[i + 0], bytes[i + 3]);
                std::swap(bytes[i + 1], bytes[i + 2]);
            }
            return true;
        default:
            return false;
    }
}

} // namespace

extern "C" {

ares_status_t ares_init(const char *rom_path) {
    if (!rom_path || !*rom_path) {
        return ARES_BRIDGE_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_state.initialized) {
        return ARES_BRIDGE_ALREADY_INITIALIZED;
    }

    std::ifstream in(rom_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "ares-bridge: cannot open ROM '%s'\n", rom_path);
        return ARES_BRIDGE_ROM_LOAD_FAILED;
    }
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size <= 0) {
        return ARES_BRIDGE_ROM_LOAD_FAILED;
    }
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char *>(bytes.data()), size)) {
        return ARES_BRIDGE_ROM_LOAD_FAILED;
    }

    if (!detect_and_canonicalize_rom_format(bytes)) {
        std::fprintf(stderr,
            "ares-bridge: unrecognized ROM magic in '%s'\n", rom_path);
        return ARES_BRIDGE_ROM_LOAD_FAILED;
    }

    /* Phase 2 will construct the Ares Node tree and mount the cartridge.
     * For Phase 1 we just hold the ROM bytes and report success so the
     * lifecycle handshake is provable from a consumer test. */

    g_state.rom_bytes = std::move(bytes);
    g_state.rom_path = rom_path;
    g_state.initialized = true;
    return ARES_BRIDGE_OK;
}

void ares_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_state = {};
}

ares_status_t ares_reset(void) {
    glue_unimplemented("ares_reset",
        "Phase 2 — needs Node tree + cartridge mount before reset is "
        "well-defined.");
}

ares_status_t ares_step_frame(void) {
    glue_unimplemented("ares_step_frame",
        "Phase 4 — scheduler stepping not wired yet.");
}

ares_status_t ares_step_instruction(void) {
    glue_unimplemented("ares_step_instruction",
        "Phase 4 — single-step requires accuracy-mode scheduler swap.");
}

ares_status_t ares_read_cpu_register(int, uint64_t *) {
    glue_unimplemented("ares_read_cpu_register",
        "Phase 3 — needs R4300 register-file accessor.");
}

ares_status_t ares_read_pc(uint32_t *) {
    glue_unimplemented("ares_read_pc",
        "Phase 3 — needs R4300 PC accessor.");
}

ares_status_t ares_read_hi_lo(uint64_t *, uint64_t *) {
    glue_unimplemented("ares_read_hi_lo",
        "Phase 3 — needs R4300 HI/LO accessor.");
}

ares_status_t ares_read_memory(uint32_t, void *, size_t) {
    glue_unimplemented("ares_read_memory",
        "Phase 3 — needs RDRAM access + TLB translation hook.");
}

ares_status_t ares_set_controller(int, const ares_input_t *) {
    glue_unimplemented("ares_set_controller",
        "Phase 6 — controller plumbing not wired yet.");
}

ares_status_t ares_save_state(void *, size_t, size_t *out_len) {
    if (out_len) *out_len = 0;
    glue_unimplemented("ares_save_state",
        "Phase 7 — Ares serializer not wired yet.");
}

ares_status_t ares_load_state(const void *, size_t) {
    glue_unimplemented("ares_load_state",
        "Phase 7 — Ares serializer not wired yet.");
}

int ares_bridge_is_real(void) {
    return 1;
}

const char *ares_bridge_version(void) {
    /* TODO: replace with CMake-generated string carrying the actual
     * Ares submodule SHA. For now: a hand-pinned tag. */
    return "ares-bridge: real (Ares v147 / f533120df)";
}

} /* extern "C" */
