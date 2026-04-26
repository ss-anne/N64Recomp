# ares-bridge — generic N64Recomp Ares oracle integration
#
# Two build modes:
#   * Stub mode (default).  WITH_ARES_BRIDGE=OFF.  Compiles only
#     src/ares_bridge.cpp; every API call returns
#     ARES_BRIDGE_NOT_IMPLEMENTED. Header-stable for consumer
#     compilation.
#   * Real mode.  WITH_ARES_BRIDGE=ON.  Pulls in Ares from
#     third_party/ares/, compiles src/ares_core_glue.cpp, links a
#     working bridge.
#
# Consumers (Zelda64Recomp, MM:R, PokemonStadiumRecomp, etc.) link
# the `ares_bridge` target unconditionally and use
# ares_bridge_is_real() at runtime to decide whether to engage the
# oracle.

option(WITH_ARES_BRIDGE
    "Compile the Ares oracle bridge against a real Ares checkout. \
Default OFF — the stub library is always built."
    OFF)

# ---- Always-built stub library ---------------------------------------------

add_library(ares_bridge STATIC
    src/ares_bridge.cpp
)

target_include_directories(ares_bridge PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(ares_bridge PUBLIC cxx_std_17)

# ---- Real-mode build (opt-in) ----------------------------------------------

if(WITH_ARES_BRIDGE)
    set(ARES_SUBMODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ares")

    if(NOT EXISTS "${ARES_SUBMODULE_PATH}/.git"
       AND NOT EXISTS "${ARES_SUBMODULE_PATH}/.gitmodule_marker")
        message(FATAL_ERROR
            "WITH_ARES_BRIDGE=ON but Ares submodule not present at\n"
            "  ${ARES_SUBMODULE_PATH}\n"
            "Add the submodule (one-time):\n"
            "  git -C ${CMAKE_SOURCE_DIR} submodule add \\\n"
            "    https://github.com/ares-emulator/ares.git \\\n"
            "    ares-bridge/third_party/ares\n"
            "Then re-configure.")
    endif()

    target_compile_definitions(ares_bridge PUBLIC
        N64RECOMP_ARES_BRIDGE_REAL=1
    )

    # The real glue. Currently a placeholder source file — when
    # implemented it pulls in the minimum subset of Ares's N64
    # core. See DESIGN.md for the dependency map.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ares_core_glue.cpp")
        target_sources(ares_bridge PRIVATE
            src/ares_core_glue.cpp
        )
    else()
        message(WARNING
            "WITH_ARES_BRIDGE=ON but src/ares_core_glue.cpp does not "
            "exist yet. Building stubs anyway. See DESIGN.md.")
    endif()

    # Ares include paths — actual include hierarchy TBD once we
    # carve out the N64 core.
    # target_include_directories(ares_bridge PRIVATE
    #     "${ARES_SUBMODULE_PATH}/ares"
    #     "${ARES_SUBMODULE_PATH}/nall"
    # )

    message(STATUS "ares-bridge: REAL mode (Ares at ${ARES_SUBMODULE_PATH})")
else()
    message(STATUS "ares-bridge: stub mode (set -DWITH_ARES_BRIDGE=ON to "
                   "enable real Ares integration)")
endif()
