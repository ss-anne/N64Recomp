# N64Recomp engine tests

Engine-level regression tests. Per-game tests live in each game repo
(`PokemonStadiumRecomp/tests/`, `Zelda64Recomp/tests/`, ...). This
suite gates the **recompiler itself** — `N64Recomp.exe`, `RSPRecomp.exe`,
the librecomp/ultramodern runtime libs, and the ares-bridge.

(Directory is named `engine-tests/` instead of `tests/` because the
top-level `.gitignore` reserves `tests/` for local scratch work; do
not rename without removing that ignore.)

## Running

```
python engine-tests/run_tests.py            # everything
python engine-tests/run_tests.py --tier 1   # only Tier-1
python engine-tests/run_tests.py -k <pat>   # name substring match
```

Exit code 0 = all pass, 1 = any fail. Tests marked `expected_fail =
True` in their module are run but their failure does not change the
exit code — they pin known bugs that are not yet fixed.

## Tier model

Mirrors the layout that worked for snesrecomp / nesrecomp / gbarecomp.
Cheaper tiers run first and gate the next.

### Tier 1 — `engine-tests/unit/` (fast, no ROM, no oracle)

Pure-input/pure-output checks against the recompiler binaries and
source.

- Decoder coverage per opcode class
- Emit golden-file tests for representative basic blocks
- Regen idempotency: running `N64Recomp.exe` twice on the same toml
  produces byte-identical output
- Buffered-emit completeness: every emitted function has a closing
  brace and no truncated trailing line (regression for f4cf3eb era)
- Tolerant-emit fires when (and only when) expected; no silent
  storage-fallback paths (e.g. cop0_regs[]) reintroduced
- `load_overlays` bounds: DMA range fits inside one section without
  walking past the section table end
- RSPRecomp GPR persistence: emitted ucode references GPRs via
  persistent storage, NOT function-local zero-initialized locals

Tier 1 tests must complete in seconds and require only the built
recompiler binaries. No Ares, no real ROMs (synthetic ELFs / micro-tomls
are fine).

### Tier 2 — `engine-tests/synthetic/` (small ROMs, self-checking)

Hand-built MIPS / RSP programs exercising one feature each:

- delay slots, branch-likely
- `J`/`JAL`/`JR` direct + indirect
- `MFC0`/`MTC0`
- COP1 single + double precision
- RSP DMA roundtrip
- RSP GPR persistence across `run_task` calls (real-execution version
  of the Tier-1 emit-shape test)

Each synthetic program self-checks (writes a sentinel to a known
address; orchestrator reads it back). No external oracle required.

### Tier 3 — `engine-tests/l3/` (real ROMs vs. ares oracle)

Boot a known-good consumer (Zelda64Recomp + OoT v1.0) with verify
mode engaged against `ares-bridge`. Each frame, diff PC + RDRAM
block-hashes. Pass = no divergence over a recorded N-frame input
sequence.

Requires `WITH_ARES_BRIDGE=ON` and a working real bridge build.
Tier 3 tests are skipped (not failed) when `ares_bridge_is_real()`
returns 0.

### Tier 4 — reverse debugger (planned, not yet scaffolded)

Mirrors nesrecomp's REVERSE_DEBUGGER: when a Tier-3 divergence
fires, automatically rewind N frames via `ares_save_state` /
`ares_load_state` and step instruction-by-instruction to localize
the first divergent op. Lands once Tier 3 is real.

## Layout

```
engine-tests/
├── README.md                  (this file)
├── run_tests.py               (orchestrator — entry point)
├── unit/                      (Tier 1)
│   ├── test_ares_bridge_loud_aborts.py
│   ├── test_binary_smoke.py
│   ├── test_buffered_emit.py
│   ├── test_pinned_n64recomp_noargs_segfault.py  (expected_fail)
│   ├── test_rsp_gpr_persistence.py               (expected_fail)
│   └── test_tolerant_emit_decls.py
├── synthetic/                 (Tier 2 — placeholder)
├── l3/                        (Tier 3 — placeholder)
└── fixtures/                  (input toml/elf/data shared across tests)
```

## Adding a test

1. Write a `test_*.py` under `unit/`, `synthetic/`, or `l3/` with one
   or more `def test_*()` functions. Assertion failures are reported;
   uncaught exceptions are reported as errors.
2. The orchestrator auto-discovers any `test_*.py` in the tier dirs;
   no registration step.
3. If the test pins a known bug and is expected to fail until the
   bug is fixed, set `expected_fail = True` at module top level.
4. Tests must be deterministic and side-effect-free: no writes outside
   `engine-tests/fixtures/_scratch/` (which the orchestrator wipes
   between runs).
