# ares-bridge/

Integration of [Ares](https://ares-emu.net/) as a divergence-checking
oracle for PokemonStadiumRecomp.

> **Status: scaffolded, not implemented.** API surface is declared
> in `ares_bridge.h`. All entry points return
> `ARES_BRIDGE_NOT_IMPLEMENTED`. Work happens on the
> `pokestadium-integration` branch in the N64Recomp engine.

## What this is for

The recomp pipeline produces native code from N64 MIPS. To know
whether the recomp is correct frame-by-frame, we need a reference
that we trust. Real hardware is impractical for automated checks;
high-accuracy emulation is the practical alternative.

Ares is the highest-accuracy open-source N64 emulator currently
maintained (the chosen oracle in this project's design questions).
Pattern matches what nesrecomp does with Nestopia and what
segagenesisrecomp does with clownmdemu — embed the oracle in our
runner, run it parallel to the recomp, diff state at frame
boundaries, fail fast on divergence.

## Why this is hard

Ares is **not designed as an embeddable oracle**. It's a
multi-system emulator with a UI, an audio pipeline, an input
abstraction, and a per-system core that sits inside an
application framework. Two real obstacles:

1. **Extracting the N64 core.** The N64 emulator lives at
   `ares/n64/` in the upstream tree but has implicit dependencies
   on the higher-level "platform" framework (input, audio,
   scheduling). We need a thin wrapper that compiles only the
   N64 core + its minimum deps, exposing a C API.
2. **Deterministic execution.** For state-diffing to be
   meaningful, both the recomp and Ares must execute the same
   ROM with the same controller inputs and produce identical
   side effects. RDP timing, audio DMA, and RNG seeding all
   need to be deterministic across both sides.

Neither problem has a packaged solution. Plan on this being a
multi-week effort once it gets prioritized.

## Layout (intended)

```
ares-bridge/
├── README.md              (this file)
├── DESIGN.md              integration design notes
├── CMakeLists.txt         no-op until WITH_ARES=1
├── include/
│   └── ares_bridge.h      C API surface
├── src/
│   ├── ares_bridge.cpp    bridge implementation (stubs today)
│   └── ares_core_glue.cpp glue to Ares's C++ N64 core
└── third_party/
    └── ares/              upstream Ares submodule (added on opt-in)
```

The `third_party/ares/` slot stays empty until `WITH_ARES=1
setup.sh` is run. That clones the upstream Ares repo, checks out a
known-good SHA, and the bridge build picks it up.

## API design (see ares_bridge.h)

A minimal C API. Implemented in C++ underneath, but the surface is
C so the recomp's runner code (likely C) can call it directly.

```c
ares_status_t ares_init(const char *rom_path);
ares_status_t ares_reset(void);
ares_status_t ares_step_frame(void);
ares_status_t ares_step_instruction(void);

ares_status_t ares_read_cpu_register(int reg, uint64_t *out);
ares_status_t ares_read_memory(uint32_t vaddr, void *buf, size_t len);
ares_status_t ares_set_controller(int port, const ares_input_t *in);

ares_status_t ares_save_state(void *buf, size_t buf_len, size_t *out_len);
ares_status_t ares_load_state(const void *buf, size_t buf_len);

void ares_shutdown(void);
```

When implemented, the recomp's `verify_mode.c` (TODO) drives both
sides in lockstep:
1. Boot recomp + Ares with same ROM.
2. Each frame: feed identical input, step both, diff RDRAM +
   register state, fail on first divergence.

## Why we ship the slot now (not later)

Three reasons even though it's a stub:

1. **Header in version control** means downstream code can
   reference the API without circular waiting. `verify_mode.c`
   can include `ares_bridge.h` and compile against stubs.
2. **Compile-flag gating in CMake** means accidentally enabling
   `WITH_ARES=1` before the bridge is real produces a clear
   "not implemented" error, not a silent miscompile.
3. **Forces the API design conversation upstream.** Defining the
   surface now beats discovering 18 months in that the surface
   is wrong.

## Sequencing

When prioritized:

1. Extract minimum N64 core build from upstream Ares — see
   `DESIGN.md` for the dependency map. (Hardest step.)
2. Implement `ares_init` / `ares_shutdown` against the extracted
   core. First milestone: load a ROM, run 100 frames, exit
   cleanly without crashing.
3. Implement `ares_read_memory` + `ares_read_cpu_register`. State
   visibility is the foundation for everything that follows.
4. Implement `ares_step_frame` deterministically. Verify two
   independent runs produce identical RDRAM dumps.
5. Wire `verify_mode.c` against the bridge. First useful diff:
   recomp boot vs. Ares boot, find first frame of divergence.
6. Implement `ares_save_state` / `ares_load_state` for rewind
   debugging (Tier-4 reverse debugger pattern from nesrecomp).

Each step is a real effort. Don't combine.
