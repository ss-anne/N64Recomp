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

## Phase 1 — Carve out the N64 core

**Goal:** a buildable, headless Ares N64 core with a C entry point.

Ares upstream (`ares-emulator/ares`) ships as one big project that
produces a SDL/Qt GUI app. The N64 core lives at `ares/n64/` and
nominally does not need the UI — but it has implicit deps on
`nall/` (utility headers), the audio subsystem, the input
abstraction, and the scheduler in `ares/component/processor/`.

Subtasks:

1. Stand up an out-of-source CMake target that compiles just
   `ares/n64/`'s sources. Use a placeholder `main()` that does
   nothing. The build will fail on missing symbols — that's the
   dependency map.
2. Walk the missing-symbol list. For each: include the source
   file, or stub it out, or rewrite the call site to bypass it.
3. End state: an `ares_n64_core.a` static library that exports
   nothing externally except the C++ entry points we care about
   (load ROM, step, read state).

**Risk:** Ares's N64 core uses C++20 concepts, expression-template
DSLs in `nall/`, and aggressive header-only patterns. Carving may
require tracking 3-5 levels of header dependency. Budget 1-2
weeks just for this phase.

**Mitigation:** track which Ares HEAD we cut against. Don't
update Ares mid-Phase-1.

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
