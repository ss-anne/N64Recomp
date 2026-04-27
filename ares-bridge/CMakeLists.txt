# ares-bridge — generic N64Recomp Ares oracle integration
#
# Build modes:
#   * Placeholder (default).  WITH_ARES_BRIDGE=OFF.  Compiles only
#     src/ares_bridge.cpp; every functional API call abort()s with a
#     diagnostic. Header-stable for consumer compilation.
#   * Real (WITH_ARES_BRIDGE=ON).  Pulls in Ares from
#     third_party/ares/, compiles src/ares_core_glue.cpp, links a
#     working oracle.
#
# Real-mode build assumes Ares has been pre-built into
# ${ARES_BUILD_DIR} (default: ${CMAKE_CURRENT_SOURCE_DIR}/build/ares).
# To prep Ares (one-time per checkout):
#
#   cmake -S ares-bridge/third_party/ares \
#         -B ares-bridge/build/ares \
#         -G "Visual Studio 17 2022" -A x64 \
#         -DARES_CORES=n64\;gb \
#         -DARES_BUILD_OPTIONAL_TARGETS=OFF \
#         -DARES_PRECOMPILE_HEADERS=OFF \
#         -DARES_UNITY_CORES=OFF
#   cmake --build ares-bridge/build/ares --target ares --config Release
#
# Doing it externally (rather than add_subdirectory) keeps Ares' large
# CMake state from leaking into N64Recomp's build.

option(WITH_ARES_BRIDGE
    "Compile the Ares oracle bridge against a real Ares checkout. \
Default OFF — the placeholder library is always built."
    OFF)

# ---- Always-built placeholder library --------------------------------------

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
    set(ARES_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/ares"
        CACHE PATH "Directory containing a built Ares (ares.lib, nall.lib, ...)")

    if(NOT EXISTS "${ARES_SUBMODULE_PATH}/ares/ares/ares.hpp")
        message(FATAL_ERROR
            "WITH_ARES_BRIDGE=ON but Ares submodule not present at\n"
            "  ${ARES_SUBMODULE_PATH}\n"
            "Add the submodule (one-time):\n"
            "  git submodule update --init --recursive\n"
            "or initially:\n"
            "  git submodule add https://github.com/ares-emulator/ares.git \\\n"
            "    ares-bridge/third_party/ares")
    endif()

    set(_ares_lib       "${ARES_BUILD_DIR}/ares/Release/ares.lib")
    set(_nall_lib       "${ARES_BUILD_DIR}/nall/nall/nall.dir/Release/nall.lib")
    set(_libco_lib      "${ARES_BUILD_DIR}/libco/Release/libco.lib")
    set(_chdr_lib       "${ARES_BUILD_DIR}/thirdparty/Release/chdr-static.lib")
    set(_sljit_lib      "${ARES_BUILD_DIR}/thirdparty/Release/sljit.lib")
    set(_lzma_lib       "${ARES_BUILD_DIR}/thirdparty/libchdr/deps/lzma-24.05/Release/lzma.lib")
    set(_zlib_lib       "${ARES_BUILD_DIR}/thirdparty/libchdr/deps/zlib-1.3.1/Release/zlibstatic.lib")
    set(_zstd_lib       "${ARES_BUILD_DIR}/thirdparty/libchdr/deps/zstd-1.5.6/build/cmake/lib/Release/zstd_static.lib")

    foreach(_lib IN ITEMS
        "${_ares_lib}" "${_nall_lib}" "${_libco_lib}"
        "${_chdr_lib}" "${_sljit_lib}"
        "${_lzma_lib}" "${_zlib_lib}" "${_zstd_lib}")
        if(NOT EXISTS "${_lib}")
            message(FATAL_ERROR
                "WITH_ARES_BRIDGE=ON but Ares lib not built: ${_lib}\n"
                "Build Ares first — see the comment at the top of "
                "ares-bridge/CMakeLists.txt for the cmake invocations.")
        endif()
    endforeach()

    target_compile_definitions(ares_bridge PUBLIC
        N64RECOMP_ARES_BRIDGE_REAL=1
    )

    # Ares uses many compile definitions in its own build; when our
    # glue includes ares headers we must supply matching ones.
    target_compile_definitions(ares_bridge PRIVATE
        PROFILE_PERFORMANCE
        BUILD_LOCAL
        ARES_ENABLE_CHD
        CORE_GB
    )

    # Ares header search paths. Order matters: more-specific first.
    # Ares uses #include <ares/ares.hpp>, <nall/...>, <n64/...>, etc.
    # Those map to subdirs under ares/, nall/, ares/n64/ — but with the
    # repo's "ares" appearing twice (third_party/ares/ares/ares/ares.hpp).
    target_include_directories(ares_bridge SYSTEM PRIVATE
        "${ARES_SUBMODULE_PATH}/ares"
        "${ARES_SUBMODULE_PATH}/nall"
        "${ARES_SUBMODULE_PATH}/ares/ares"
        "${ARES_SUBMODULE_PATH}/ares/n64"
        "${ARES_SUBMODULE_PATH}/libco"
        "${ARES_SUBMODULE_PATH}/thirdparty"
    )

    # The real glue.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ares_core_glue.cpp")
        target_sources(ares_bridge PRIVATE
            src/ares_core_glue.cpp
        )
    else()
        message(WARNING
            "WITH_ARES_BRIDGE=ON but src/ares_core_glue.cpp does not "
            "exist yet. Bridge will link with placeholder bodies only. "
            "See DESIGN.md.")
    endif()

    # Link the pre-built Ares stack. Order: ares first (consumer), then
    # the libs ares depends on.
    target_link_libraries(ares_bridge PRIVATE
        "${_ares_lib}"
        "${_nall_lib}"
        "${_libco_lib}"
        "${_chdr_lib}"
        "${_sljit_lib}"
        "${_lzma_lib}"
        "${_zlib_lib}"
        "${_zstd_lib}"
    )

    # Ares pulls in winmm + Threads via its own CMake; replicate.
    if(WIN32)
        target_link_libraries(ares_bridge PRIVATE winmm)
    endif()
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads)
    if(TARGET Threads::Threads)
        target_link_libraries(ares_bridge PRIVATE Threads::Threads)
    endif()

    target_compile_features(ares_bridge PRIVATE cxx_std_20)

    message(STATUS "ares-bridge: REAL mode enabled")
    message(STATUS "  Ares submodule: ${ARES_SUBMODULE_PATH}")
    message(STATUS "  Ares build:     ${ARES_BUILD_DIR}")
else()
    message(STATUS "ares-bridge: placeholder mode (set -DWITH_ARES_BRIDGE=ON to "
                   "enable real Ares integration)")
endif()
