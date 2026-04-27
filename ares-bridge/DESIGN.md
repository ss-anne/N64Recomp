# ares-bridge — implementation design

How we go from the current stub to a working oracle. Read after
README.md.

## North star

A consumer's `verify_mode.c` should look roughly like:

```c
#include "ares_bridge.h"

void verify_one_frame(uint32_t recomp_pc, uint64_t *recomp_regs,
                      const uint8_t *recomp_rdram) {
    if (!ares_bridge_is_real()) return;   // stub build, skip

    ares_step_frame();

    uint32_t ares_pc;
    ares_read_pc(&ares_pc);
    if (ares_pc != recomp_pc) {
        WATCHDOG_FIRE("PC divergence at frame %d: recomp=%x ares=%x",
                      frame, recomp_pc, ares_pc);
    }

    uint8_t ares_rdram[8 * 1024 * 1024];
    ares_read_memory(0x80000000, ares_rdram, sizeof(ares_rdram));
    if (memcmp(recomp_rdram, ares_rdram, sizeof(ares_rdram)) != 0) {
        // bisect to find first divergent address
        WATCHDOG_FIRE("RDRAM divergence at frame %d", frame);
    }
}
```

Everything in this design exists to make that snippet real.

## Phase 1 — Carve out the N64 core  ✅ DONE (2026-04-26)

**Goal:** a buildable, headless Ares N64 core with a C entry point.

**Outcome:** completed faster than the original "1-2 weeks" budget by
not carving at all — Ares' own CMake supports a narrowed build via
`ARES_CORES=n64;gb` and tolerates the absence of `desktop-ui/mia/hiro`
when those subdirs are not added by a top-level project. We pre-build
Ares externally into `ares-bridge/build/ares/` and link the resulting
static libs from our `ares_bridge` target.

Pinned upstream: **Ares v147** (`f533120df`, 2025-12-18).

**Build sequence (one-time):**

```sh
git submodule update --init --recursive   # fetches third_party/ares
cmake -S ares-bridge/third_party/ares \
      -B ares-bridge/build/ares \
      -G "Visual Studio 17 2022" -A x64 \
      -DARES_CORES=n64\;gb \
      -DARES_BUILD_OPTIONAL_TARGETS=OFF \
      -DARES_PRECOMPILE_HEADERS=OFF \
      -DARES_UNITY_CORES=OFF
cmake --build ares-bridge/build/ares --target ares --config Release
```

This produces:

- `ares.lib`              — the core engine (N64 + GB cores + framework)
- `nall.lib`              — Ares' utility lib
- `libco.lib`             — coroutine context-switch primitive
- `chdr-static.lib`       — CHD compressed disk format
- `sljit.lib`             — SLJIT runtime asm helper
- `lzma.lib`/`zlib`/`zstd` — supporting compression libs

**Bridge build:**

```sh
cmake -S . -B build-vs -DWITH_ARES_BRIDGE=ON
cmake --build build-vs --target ares_bridge --config Release
```

`ares_bridge.lib` now links `ares.lib` and friends. `ares_init()` opens
the ROM file, byteswaps any of `.z64/.v64/.n64`, and holds the bytes;
all other functional entry points abort loudly with a phase-tagged
diagnostic so callers can't silently consume them.

**What still needs Phase-2 work to actually drive Ares:**

- `ares::Nintendo64::load(node, "Nintendo 64")` is callable but needs
  a `Node::System` constructed first.
- `ares::platform` is a global pointer to a video/audio/input sink;
  must be implemented (no-op video, no-op audio, dummy input) before
  the Node tree can be torn up.
- ROM is currently held in a `std::vector<uint8_t>`; needs to be
  routed through `mia::Pak` or wired into the cartridge slot
  directly to be visible to the emulator.

**Notes / gotchas captured during the carve:**

- Repo nests as `third_party/ares/ares/ares/ares.hpp` — three "ares"
  segments. Include search paths must be tuned accordingly.
- Vulkan/parallel-RDP is built into `ares.lib` even when we don't
  want a renderer. The `#if defined(VULKAN)` guards in `n64.hpp`/
  `vi.cpp` mean the headless code path is still exercised at run
  time so long as we don't construct the VI's Vulkan device. If
  symbol bloat becomes a problem we can revisit by editing
  `ares/n64/CMakeLists.txt` to drop the parallel-RDP source list.
- Ares' build defines `PROFILE_PERFORMANCE`, `BUILD_LOCAL`,
  `ARES_ENABLE_CHD`, `CORE_GB` at compile time. The bridge mirrors
  these so its glue's `#include <ares/ares.hpp>` matches.

## Phase 2 — Lifecycle (init / shutdown / reset)

**Goal:** `ares_init("baserom.z64")` loads the ROM and initializes
the core; `ares_shutdown()` tears it down without leaks.

Subtasks:

1. Wrap Ares's `Cartridge` loader in `ares_init`. Validate ROM
   header, allocate emulator state.
2. Implement `ares_shutdown` as the inverse — free resources.
   Must be idempotent.
3. Implement `ares_reset` — call into Ares's `Reset` machinery.
4. Bridge unit test: load ROM, run zero frames, exit cleanly.
   No assertions about behavior yet — just don't crash.

**Validation:** call `ares_init` 100 times in a row with
intervening `ares_shutdown`s. Memory usage must stay flat.

## Phase 3 — State read

**Goal:** consumer code can introspect Ares's state at any
quiescent point (frame boundary, post-step).

Subtasks:

1. `ares_read_cpu_register(reg, &out)` — wrap Ares's R4300
   register file access.
2. `ares_read_pc(&out)`, `ares_read_hi_lo(&hi, &lo)` — same.
3. `ares_read_memory(vaddr, buf, len)` — translate vaddr
   through Ares's TLB, copy bytes out. Return
   ARES_BRIDGE_INVALID_ARGUMENT for unmapped vaddrs.

**Gotcha:** Ares may store registers at a different bit-width or
endianness than the recomp's runtime expects. Define the bridge
API as canonical (uint64 little-endian on the host, vaddrs are
N64 virtual addresses) and have the bridge convert.

**Validation:** boot Pokémon Stadium up to the title screen on
both sides (via Phase 4 below), dump RDRAM from each, diff. They
need not match yet — we just need the dump format to work.

## Phase 4 — Stepping

**Goal:** advance Ares's state by one frame or one instruction.

Subtasks:

1. `ares_step_frame` — call Ares's per-frame loop until VI
   interrupt. Return only when the next vsync would fire.
2. `ares_step_instruction` — single-step the R4300. This may
   require flipping Ares into a different scheduling mode; some
   accuracy emulators don't expose instruction-level granularity
   directly.
3. Determinism: ensure no wall-clock dependency, no thread races
   that affect output, no PRNG seeded from `time(NULL)`.

**Critical determinism check:** run `ares_step_frame` 1000 times
with the same input on a fresh boot, dump RDRAM after each frame,
hash. The hashes must be identical across runs on the same host.
Also: identical across different hosts (Linux/Mac/Windows) ideally.
If they're not, isolate the source — usually scheduler or
floating-point.

## Phase 5 — Validation against Zelda64Recomp

**Goal:** prove the bridge is correct against a known-good
consumer before relying on it for our half-baked Pokémon Stadium
recomp.

Subtasks:

1. Clone Zelda64Recomp as a sister project. Junction its
   `n64recomp/` to this engine on the
   `pokestadium-integration` branch.
2. Build Zelda64Recomp with `-DWITH_ARES_BRIDGE=ON`.
3. Add a `verify_mode.c` to Zelda64Recomp's runner (mirror the
   nesrecomp/Faxanadu pattern). Engage the bridge: each frame,
   step both, diff state.
4. Boot OoT to title screen with verify mode on. **It must
   succeed without firing the watchdog.** If state diverges, the
   bridge is wrong (because the Zelda64Recomp recomp is known
   correct against Ares).
5. Run for 60 frames of in-game gameplay (a known input
   sequence). Identical state at frame 60 across runs is the
   pass criterion.

**Why Zelda64Recomp specifically:** longest-running production
consumer, biggest contributor base, most likely to have
documented its own behavior precisely. MM:R is a fine alternate
if OoT v1.0 is unobtainable.

## Phase 6 — Input

**Goal:** drive Ares with the same controller state as the recomp.

Subtasks:

1. `ares_set_controller(port, &in)` — set Ares's input register.
2. Plumb in the consumer's recorded inputs (a frame-keyed log of
   button states is the standard format).
3. Add a `--replay <input.log>` flag in consumer runners.

This is straightforward once phases 4-5 are real.

## Phase 7 — Save / load state (optional)

**Goal:** rewind support, à la nesrecomp's Tier-4 reverse
debugger.

`ares_save_state` serializes Ares's complete state into a buffer.
`ares_load_state` restores from one. Combined with the recomp's
own snapshotting, a divergence-detected watchdog can trigger an
automatic rewind to N frames before the divergence and step
forward instruction-by-instruction.

Defer until phases 1-6 are stable. Worth it for diagnostic flow.

## What we are NOT doing

- **GUI.** Ares's UI is irrelevant to the bridge.
- **Audio.** The bridge does not read or compare audio output.
  Audio is a known-hard divergence source and isn't load-bearing
  for correctness-of-game-logic.
- **Video.** Same as audio. The recomp + Ares may render
  differently due to RDP timing differences without that being a
  real bug.
- **Multi-system support.** Ares supports many consoles. We only
  need N64. Strip everything else.
- **Multiple controllers.** Phase 6 supports 1 controller. Add
  more if/when needed.
- **Save state portability across Ares versions.** Each Ares
  bump may break state format. Bridge users should snapshot the
  Ares submodule SHA in their state file headers.

## Open questions

- Will Ares's R4300 step-instruction granularity produce *exactly*
  the same instruction sequence as the recomp? If Ares uses a
  cycle-accurate timing model and the recomp is purely
  functional, instruction order at frame boundaries may differ
  in non-load-bearing ways. Need to scope the comparison to
  observable state, not instruction count.
- Determinism of TLB exception handling. The R4300's TLB miss
  handler is partly software (kernel code in the ROM). If Ares
  takes a different number of cycles to service a TLB miss, the
  observable RAM state at frame boundaries is still the same,
  but mid-frame state isn't comparable.
- Hash format for state diffs. SHA-256 of RDRAM is too slow at
  60 fps. Block-level hashes (one per 4KB) let us bisect to the
  divergent block and only diff that. Implement once we have
  a real divergence to debug.
