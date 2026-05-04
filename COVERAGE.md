# N64 Recompiler Coverage Audit

Date: 2026-05-03

Scope: CPU recompiler behavior only. This excludes the RSP recompiler, ares bridge, mod packaging, and runtime integration except where the generated CPU code depends on runtime helpers.

## References

- NEC uPD30200/uPD30210 VR4300/VR4305/VR4310 data sheet, document U10116EJ7V0DS00: https://www.bitsavers.org/components/nec/mips/Vr4300-ds_200011.pdf
- NEC VR4300/VR4305/VR4310 User's Manual, document U10504EJ7V0UM00: https://hack64.net/docs/VR43XX.pdf
- Nintendo 64 programming manual CPU page: https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro03/03-05.html

The data sheet identifies the VR4300 family as a 64-bit MIPS architecture processor with a 32-bit system interface bus and instruction set compatibility with the VR4000 series / MIPS I, II, and III. The Nintendo manual confirms the N64 uses an R4300-family CPU, runs game code in kernel mode with 32-bit addressing, and still has 64-bit integer operations available.

## Current Coverage Shape

The CPU recompiler's main coverage surface is concentrated in three places:

- `src/operations.cpp`: table-driven unary, binary, conditional branch, and store instruction mappings.
- `src/recompilation.cpp`: special handling for CP0 moves, multiply/divide, direct and indirect control flow, syscall/break, CFC1/CTC1, delay slots, branch-likely nullification, relocations, and runtime fallback emission.
- `src/analysis.cpp`: jump-table detection and runtime-fragment function-bound discovery.

A quick Rabbitizer CPU ID comparison found 234 generic CPU instruction IDs and 177 IDs referenced by the current static recompiler path. That leaves 57 Rabbitizer IDs not directly handled. Some are aliases or non-N64-relevant generic coprocessor IDs, but several are real VR4300/MIPS III instructions or behavior gaps.

## Covered Instruction Families

The recompiler has broad coverage for the common compiler-generated N64 subset:

- Integer arithmetic/logic/shifts/comparisons: `ADD`, `ADDU`, `ADDI`, `ADDIU`, `SUB`, `SUBU`, `DADD`, `DADDU`, `DADDI`, `DADDIU`, `DSUB`, `DSUBU`, `AND`, `OR`, `XOR`, `NOR`, `SLT`, `SLTU`, `SLTI`, `SLTIU`, 32-bit shifts, and 64-bit shifts.
- HI/LO and multiply/divide: `MFHI`, `MFLO`, `MTHI`, `MTLO`, `MULT`, `MULTU`, `DMULT`, `DMULTU`, `DIV`, `DIVU`, `DDIV`, `DDIVU`.
- Loads/stores: byte, halfword, word, doubleword, unsigned word, left/right unaligned forms, and CP1 word/doubleword loads and stores.
- Control flow: `J`, `JAL`, `JR`, `JALR`, `BEQ`, `BNE`, zero-comparison branches, REGIMM branches, branch-and-link forms, branch-likely variants, and CP1 branches.
- CP1/FPU common paths: `MFC1`, `MTC1`, `DMFC1`, `DMTC1`, S/D arithmetic, S/D square root/abs/move/negate, S/D/W/L conversions except long rounding forms, and S/D comparisons.
- System-ish operations: `SYSCALL` and `BREAK` emit runtime helper calls; `MFC0`/`MTC0` handle `Status` specially and otherwise use `ctx->cop0_regs[]` for non-reserved CP0 registers.

## Highest-Value Gaps

### 1. Real VR4300 Instructions That Fall To Runtime Unhandled

These are architecturally meaningful and currently land in the generic unhandled-instruction path at `src/recompilation.cpp:858`.

- `SYNC`: the VR4300 manual says the processor handles `SYNC` as a NOP because load/store instructions execute in program order. This should be a low-risk explicit NOP mapping.
- `CACHE`: this is a real VR4300 cache-management instruction. For the current HLE memory model it can probably become an explicit no-op or named runtime helper, but it should not be a generic unhandled opcode.
- `LL`, `LLD`, `SC`, `SCD`: the manual says VR4300 processes LL/SC compatibility instructions correctly even without a multiprocessor environment. A single-threaded HLE model could track LLbit/address enough for common locks.
- Trap instructions: `TEQ`, `TEQI`, `TGE`, `TGEI`, `TGEU`, `TGEIU`, `TLT`, `TLTI`, `TLTU`, `TLTIU`, `TNE`, `TNEI`. These should either emit a trap helper with condition logic or be documented intentional aborts.
- TLB/exception-return operations: `TLBP`, `TLBR`, `TLBWI`, `TLBWR`, `ERET`. These matter for boot/OS/TLB-mapped code, even if many static recomp targets avoid them.
- CP0 doubleword moves: `DMFC0`, `DMTC0`. The current CP0 path only handles `MFC0` and `MTC0`.
- FPU long fixed-point rounding conversions: `ROUND.L.S`, `ROUND.L.D`, `TRUNC.L.S`, `TRUNC.L.D`, `CEIL.L.S`, `CEIL.L.D`, `FLOOR.L.S`, `FLOOR.L.D`. The enum and C generator already have long conversion operation types, but `src/operations.cpp` does not map these opcodes.

### 2. Rabbitizer Aliases That May Surface As Unique IDs

Some missing IDs are assembler aliases rather than separate VR4300 opcodes, but they can still become recompiler coverage holes if Rabbitizer returns the alias ID:

- `BEQZ`, `BNEZ`, `BAL`, `MOVE`, `NOT`.
- `B` is already handled, and `NEGU` is mapped.
- `PREF`, `SN64_DIV`, and `SN64_DIVU` look generic/Rabbitizer-specific rather than confirmed VR4300 gaps and need classification before treating them as N64 CPU coverage work.

Recommendation: normalize aliases to base operations before dispatch, or add explicit table entries that route aliases to the same emit paths.

### 3. CP1 Control Register Coverage Is Too Narrow

`CFC1` and `CTC1` currently return `false` unless the control register is 31 (`src/recompilation.cpp:685` and `src/recompilation.cpp:693`). The VR4300 manual defines FCR0 as the implementation/revision register and FCR31 as control/status; it also says FCR31 and FCR0 can be read with `CFC1`, while FCR0 is read-only.

Immediate improvement:

- Allow `CFC1 rt, $0` and return the VR4300 FCR0 value.
- Keep `CTC1 $31` for rounding/status updates.
- Treat `CTC1 $0` and FCR1-FCR30 deliberately, not through a generic early return.

### 4. CP0 Is Storage-HLE, Not Full CPU Semantics

The deliberate `ctx->cop0_regs[]` model in `include/recomp.h` is reasonable for many game-code targets, but it is not full VR4300 behavior:

- `Count`, `Compare`, `Cause`, `EPC`, TLB registers, cache tag registers, watch registers, and error registers have hardware side effects or hardware-written values on real VR4300.
- Reserved CP0 registers currently emit loud runtime aborts.
- `ERET` and TLB instructions are absent.

This is acceptable if the supported target is "libultra-style game code after boot," but it is a clear coverage boundary for boot, exception handlers, TLB-mapped code, and code that polls CP0 timers or interrupt cause state.

### 5. Trapping Semantics Are Mostly Flattened

Several instructions with architecturally visible exceptions are translated like their non-trapping versions:

- `ADD` and `ADDU` share `Add32`; `SUB` and `SUBU` share `Sub32`; `DADD`/`DADDU` and `DSUB`/`DSUBU` are similarly collapsed in `src/operations.cpp`.
- `ADDI` and `ADDIU`, plus `DADDI` and `DADDIU`, are collapsed.
- Alignment, address error, TLB miss, coprocessor unusable, overflow, trap, and FPU exceptions are generally not modeled.

This may be pragmatic for compiler-generated N64 game code, but it should be explicit in coverage docs and tests. Any target relying on CPU exception behavior will not be faithfully recompiled.

### 6. FPU Comparison And FCSR Semantics Are Approximate

`src/operations.cpp` has a local TODO for float ordered/unordered comparison behavior. The current mapping collapses many unordered and signaling predicates to simple false/equal/less/less-equal comparisons. The VR4300 manual's FPU predicate table distinguishes unordered cases and invalid-exception behavior.

Additional FPU gaps:

- FCSR cause, enable, and flag fields are not modeled; `CFC1`/`CTC1` mostly expose host rounding mode.
- `NAN_CHECK` asserts instead of modeling FPU invalid-operation behavior.
- The C generator comments that round-to-word helpers should use banker's rounding, but currently use `lround`/`lroundf` for explicit `ROUND.W.*`.
- Host C divide/conversion behavior is not always VR4300 exception/flag behavior.

### 7. Divide Edge Cases Need Guards

`DDIV` and `DDIVU` helpers handle signed overflow for `INT64_MIN / -1` but not divide-by-zero. The 32-bit `DIV`/`DIVU` paths emit direct C division/modulo, which is host-undefined for zero divisors and can crash. The VR4300 manual documents specific zero-division LO/HI behavior for integer division, so this should be modeled deliberately instead of inheriting host C behavior.

## Control-Flow Coverage Gaps

### Direct Calls And Branches

- Unknown `JAL` targets emit `recomp_unhandled_call` instead of failing the function (`src/recompilation.cpp:342`).
- Direct branches outside the current function that are not known function starts emit `recomp_unhandled_branch` (`src/recompilation.cpp:424` and `src/recompilation.cpp:620`).
- Branches to known functions are treated as tail calls, but static-function handling is still marked FIXME.
- `JALR` with a link register other than `$ra` emits `recomp_unhandled_jalr`; real MIPS allows arbitrary `rd`.

### Indirect Jumps

- `JR $ra` returns.
- `JR <reg>` with a recognized jump table becomes a switch.
- `JR <reg>` without a recognized jump table is treated as an indirect tail call via `LOOKUP_FUNC(reg)`, with a TODO noting that not all indirect jumps are tail calls.

This is a practical static-recomp compromise, but it is a behavior gap for computed intra-function jumps, dispatch stubs, hand-written assembly, or unusual compiler output.

### Jump-Table Analysis

The analyzer recognizes a narrow register-state pattern around `LUI`/`ADDIU`/`ADDU`/`LW`/`JR` plus a GOT variant. Current failure points:

- Stack-state tracking returns `false` on unaligned or negative `SW`/`LW` stack offsets (`src/analysis.cpp:156` and `src/analysis.cpp:176`).
- Jump-table data is assumed to be in the same section as the function (`src/analysis.cpp:323`).
- A detected jump table with zero valid entries fails analysis (`src/analysis.cpp:349`).
- Runtime function-bound discovery fails on an indirect `JR` when the local register-state simulator does not detect a jump-table pattern (`src/analysis.cpp:506`) or when the jump table has no valid entries (`src/analysis.cpp:523`).
- `discover_function_bounds` intentionally does not follow direct `J` targets, so genuine intra-function unconditional jumps can be under-bounded.

### Delay Slots

Normal delay slots and branch-likely nullification are explicitly handled by duplicating delay-slot instructions. That is a good fit for static C output. Edge cases that still need coverage tests:

- Branch or jump in a delay slot.
- Delay slot that has relocation metadata.
- Delay slot that is also a branch target label.
- Likely branches whose delay slot has side effects and whose target is outside the function.

## Priority Plan

### P0: Make Coverage Measurable

- Add a generated opcode inventory test that enumerates expected VR4300 instruction IDs from Rabbitizer, filters known aliases/non-N64 IDs, and classifies each as concrete emit, intentional HLE/no-op, runtime trap helper, or unsupported.
- Add synthetic one-instruction recompilation fixtures that assert no generic `recomp_unhandled_instruction` appears for the supported set.
- Add alias fixtures for `B`, `BEQZ`, `BNEZ`, `BAL`, `MOVE`, `NOT`, and `NEGU`.
- Add a CI-friendly report that counts emitted `recomp_unhandled_*` calls per target binary/function.

### P1: Fill Low-Risk Opcode Holes

- Map `SYNC` to explicit NOP.
- Decide `CACHE` policy and implement explicit no-op or named runtime helper.
- Classify `PREF`; only add it to N64 coverage if target binaries can emit it or another source proves it belongs to the supported CPU profile.
- Add FPU long fixed-point conversion mappings already supported by `UnaryOpType` and `CGenerator`.
- Add `CFC1 FCR0` read support and stricter documented behavior for invalid FCR accesses.
- Normalize alias IDs to existing operations.
- Add guarded divide-by-zero behavior for `DIV`, `DIVU`, `DDIV`, and `DDIVU`.

### P1: Replace Generic Unhandled With Architectural Helpers

- Add conditional trap helpers for trap instructions.
- Add LL/SC/LLD/SCD support with an HLE LLbit/address model.
- Add CP0 `DMFC0`/`DMTC0` storage handling at least equivalent to the 32-bit CP0 path.
- If boot/OS code is in scope, add explicit helpers for `ERET` and TLB operations instead of generic unhandled.

### P2: Improve Semantic Fidelity

- Decide whether trapping integer overflow should remain intentionally unmodeled. If not, split trapping and non-trapping arithmetic emit paths.
- Implement FCSR flags/cause/enables and ordered/unordered comparison semantics.
- Replace NaN asserts with FPU-status behavior or named runtime traps.
- Add address/alignment exception policy for memory macros, even if the first step is a loud helper.

### P2: Strengthen Control Flow

- Replace linear jump-table discovery with block-aware register-state propagation.
- Support jump tables in separate sections or data sections.
- Classify indirect `JR` as jump table, function pointer tail call, or unsupported computed branch with diagnostics.
- Teach function-bound discovery to follow safe intra-function `J` targets when symbols or CFG evidence show they are not neighboring tail calls.

## Suggested Test Matrix

- Instruction emit tests: one test per supported opcode family, with explicit checks for generated helper/macro names.
- Alias tests: encoded instructions that Rabbitizer reports as aliases should route to the same emit as their base instruction.
- Delay-slot tests: taken/not-taken conditional branch, branch-likely taken/not-taken, `JAL`, `JR`, and delay slots with memory/FPU operations.
- Control-flow tests: local branch, tail-call branch, unknown branch, known `JAL`, unknown `JAL`, `JALR $ra`, `JALR non-$ra`, jump table, and non-jump-table `JR`.
- Semantic tests against an interpreter/oracle: trapping arithmetic, divide edge cases, LL/SC, FPU comparisons with NaNs, FCR0/FCR31 reads/writes, and CP0 Count/Compare/Cause if those become supported.

## Bottom Line

The recompiler covers the common statically compiled N64 game-code subset well: integer ALU, normal loads/stores, common branches, delay slots, common CP1 arithmetic/conversions, and basic CP0 status handling. The most important coverage holes are not obscure opcodes; they are architectural side effects and control-flow classification:

- LL/SC, SYNC/CACHE, trap, TLB/ERET, CP0 doubleword moves.
- FPU long rounding conversions and FCR0/FCSR behavior.
- Alias IDs that may bypass base-op mappings.
- Unknown direct branches/calls and optimistic indirect `JR` tail-call handling.
- Analyzer early failures around jump tables and function bounds.

The fastest path to better coverage is to first make the supported/unsupported opcode matrix executable, then fill the low-risk explicit mappings before tackling exception/TLB/FCSR fidelity.
