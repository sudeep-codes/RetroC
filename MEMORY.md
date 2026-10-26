# MEMORY.md — Running state of the project (update at the end of every session)

## Current status
- Current phase: Phase 10 complete / Phase 11 (benchmarks harness and generators created, tested)
- Last session: 2026-10-26 — Phase 11 harness & generators implemented, W1–W4 generated, git repository clean & verified with 100% test pass
- Build: clean (GCC 15.2.0 / MinGW UCRT64, CMake 4.4.4)
- Tests: 8 passed / 0 failed (100% pass across doctest unit tests (42 cases, 511 assertions), python golden tests, and differential testing runner with 1000 trials)

## Decisions (append only; include the reason)
| ID | Date | Decision | Reason |
|----|------|----------|--------|
| D-001 | 2026-10-19 | Implementation language: C++17 + CMake + doctest (CONFIRMED) | Fast runtime for honest benchmarks; familiar |
| D-002 | 2026-10-19 | Backend v1: lambda bodies compiled to a small stack bytecode; C++ runtime executes the graph with memo tables (CONFIRMED) | Edits (insert/delete stage) need runtime graph changes; emitting standalone C++ is a stretch goal |
| D-003 | | Record-level (fine-grained) memoization for MAP/FILTER from the start | Main novelty lever vs stage-level invalidation |
| D-004 | | Language is pure, no recursion, no `for` loops in v1 | Keeps correctness proof and scope manageable |
| D-005 | 2026-10-21 | First-run overhead of memoization is negligible (~13 ms process startup + execution on Ex1, <2 KB memo table memory per pipeline) | Hash-based record memo and history recording introduce sub-millisecond in-process overhead while enabling fine-grained reuse |
| D-006 | 2026-10-21 | Differential testing verifies propagated execution vs from-scratch rerun on rewritten source text | Guarantees correctness statement (section 8) holds end-to-end on arbitrary valid modify edits |
| D-007 | 2026-10-21 | Atomic rollback restores complete graph and execution state snapshots on typecheck error or cycle | Enforces Invariant that failed/invalid edits leave system state strictly unchanged (zero side effects) |
| D-008 | 2026-10-24 | Explain provenance chains report each edit touching the output and the first affected stage | Accurately explains provenance per EDIT_SEMANTICS.md section 12 |
| D-009 | 2026-10-24 | Differential tester verifies both valid edit convergence (full-value structural equality) and invalid-edit atomic rejection per trial | Ensures complete robustness against erroneous edits without state corruption |

## Open questions (for the human)
- (none yet)

## Known bugs
- (none yet)

## Phase checklist
- [x] Phase 0 setup + spec freeze
- [x] Phase 1 lexer
- [x] Phase 2 parser + AST
- [x] Phase 3 semantic analysis
- [x] Phase 4 IR builder + dump
- [x] Phase 5 runtime (from-scratch execution)
- [x] Phase 6 history recording
- [x] Phase 7 modify-input propagation
- [x] Phase 8 insert/delete stage
- [x] Phase 9 reuse analyzer + rebuild plan + explain
- [x] Phase 10 verifier + differential testing
- [ ] Phase 11 benchmarks
- [ ] Phase 12 stretch (tree reduce, cost model, record edits, control flow)

## Next actions (exact, small)
1. Phase 11: Implement workload generators W1-W4 per `docs/BENCHMARKS.md`.
2. Implement runner and baselines B0 (full rerun from scratch), B1 (stage-level invalidation), B2 (RetroC fine-grained reuse).
3. Generate benchmark results JSON and comparison table (median of 11 runs).

## Session log (newest first, 2-3 lines each)
- 2026-10-24: Phase 10 complete. Hardened verifier mode (`verify_edits`) with full-value structural comparison (`Value::operator==`) in addition to string representations. Added failure artifact dumping (`tests/diff/failures/<seed>/` with `seed.txt`, `program.rc`, `edits.rcs`). Added an invalid-edit generator pass into each trial of `difftest` verifying rejection (`E601`) and atomic rollback with unchanged outputs. All 8 CTest suites passed (42 doctest cases, 511 assertions; 1000/1000 diff trials passed).
- 2026-10-24: Phase 9 complete. Implemented `retroc explain <file.rc> <script.rcs> <output>` CLI subcommand and provenance chain tracking (`Propagator::explain_output`) in `src/reuse/propagator.*`. Verified exact rebuild plan formatting matching `EDIT_SEMANTICS.md` section 7. Added unit tests for hand-computed op counts matching Ex2 modify and insert cases, and added golden tests for a 3-edit script (`ex2_three_edits.rc`, `ex2_three_edits.rcs`, `ex2_three_edits.expected`, `ex2_three_edits.explain.expected`). All 8 CTest suites passed (41 doctest cases, 507 assertions; 1000/1000 diff trials passed).
- 2026-10-21: Phase 8 complete. Implemented stage insertion (Edit B) and stage deletion (Edit C) with source rewriting in `src/edit/source_rewrite.*` and graph propagation in `src/reuse/propagator.*`. Added Kahn's topological reordering algorithm (`Graph::recompute_topological_order()`) and atomic snapshot/rollback on cycles or type errors. Implemented metamorphic tests (identity map preserves outputs) and extended differential tester to all three edit types (modify, insert, delete) running 1000 randomized trials. All 8 CTest suites passed (39 unit test cases, 481 assertions; 1000/1000 diff trials passed).
- 2026-10-21: Phase 7 complete. Implemented edit script parser (`src/edit/edit.*`), source rewriting (`src/edit/source_rewrite.*`), fine-grained work-set propagation with early cutoff (`src/reuse/propagator.*`), and verifier (`src/verify/verifier.*`). Added CLI subcommands `retroc edit <file.rc> <script.rcs>` and `retroc verify <file.rc> <script.rcs>`. Added differential testing runner executable `difftest` testing 500 randomized modify trials against full-rerun from scratch. All 8 test suites passed in CTest (35 unit test cases, 453 assertions; 500/500 diff trials passed).
- 2026-10-21: Phase 7 complete. Implemented edit script parser (`src/edit/edit.*`), source rewriting (`src/edit/source_rewrite.*`), fine-grained work-set propagation with early cutoff (`src/reuse/propagator.*`), and verifier (`src/verify/verifier.*`). Added CLI subcommands `retroc edit <file.rc> <script.rcs>` and `retroc verify <file.rc> <script.rcs>`. Added differential testing runner executable `difftest` testing 500 randomized modify trials against full-rerun from scratch. All 8 test suites passed in CTest (35 unit test cases, 453 assertions; 500/500 diff trials passed).
- 2026-10-21: Phase 6 complete. Implemented per-node `Memo` tables for MAP (`RecordId -> {inHash, fnHash, out}`), FILTER (`RecordId -> {inHash, fnHash, keep}`), REDUCE (`{inputHash, fnHash, initHash, result}`), JOIN (pair key `(leftId, rightId)` memo), and SCALAR in `src/ir/node.*`. Implemented `HistoryLog`, `HistoryEntry`, and memory accounting in `src/runtime/history.hpp`. Integrated memoization into `GraphExecutor`, verified Invariant I8 (empty-memo run == memoized run), added `--verbose` execution and memo statistics to `retroc run`. First-run execution overhead measured at ~13 ms median per run. CTest passed 6/6 tests (29 unit test cases, 403 assertions).
- 2026-10-21: Phase 5 complete. Implemented `Value` tagged representation with structural equality, 64-bit hashing, and deterministic formatting in `src/common/value.*`. Implemented bytecode instructions, compiler, and stack VM in `src/bytecode/bytecode.*`. Implemented CSV loader and graph executor supporting all node kinds (INPUT, MAP, FILTER, REDUCE, JOIN, SCALAR, OUTPUT) in `src/runtime/`. Added `retroc run <file.rc>` CLI subcommand (returning code 4 on runtime errors). Added doctest unit tests and golden tests for Ex1-Ex4, E501 errors, and determinism. CTest passed 6/6 tests (27 unit test cases, 374 assertions).
- 2026-10-20: Phase 4 complete. Implemented IR graph, node, and expression data structures in `src/ir/expr_ir.*`, `src/ir/node.*`, `src/ir/graph.*`. Implemented IR builder lowering from `TypedProgram` to `Graph` in `src/ir/builder.*` with user function inlining, nested collection builtin splitting into hidden stages (`s$1, s$2`), and invariant checks (I1 DAG topological order, I2 unique NodeIds). Implemented canonical IR dumper in `src/ir/dump.*` and `retroc dump-ir [--verbose]` CLI command. Added doctest unit tests and golden tests for Ex1-Ex4. CTest passed 5/5 tests (18 unit test cases, 327 assertions).
- 2026-10-20: Phase 3 complete. Implemented semantic analyzer in `src/sema/sema.hpp` / `src/sema/sema.cpp` and `src/sema/types.hpp` / `src/sema/types.cpp` with symbol table, sequential stage scoping, recursion rejection (E402), forward reference checks (E401), duplicate / shadowing checks (E403), lambda parameter inference, builtin signatures, and type checking for all operations. Added `retroc check` CLI command, doctest unit tests covering Ex1-Ex4 and all E3xx/E4xx codes, and golden tests for Ex1-Ex7. CTest passed 4/4 tests (15 unit test cases, 283 assertions).
- 2026-10-20: Phase 2 complete. Implemented AST nodes and printer in `src/parser/ast.hpp` / `src/parser/ast.cpp`, recursive-descent parser with lambda-vs-parenthesized lookahead and operator precedence in `src/parser/parser.hpp` / `src/parser/parser.cpp`, `retroc dump-ast` CLI subcommand, comprehensive doctest unit tests (precedence, associativity, lookahead, error codes), and golden tests for Ex1-Ex7 and syntax errors. CTest passed 3/3 tests (12 doctest test cases, 211 assertions).

- 2026-10-19: Phase 1 complete. Implemented source locations and error formatting (`src/common`), token kinds and lexer (`src/lexer`), `retroc dump-tokens` CLI subcommand, comprehensive doctest unit tests (all token kinds, string escapes, comments, int vs float, E101/E102), and golden tests for Ex1-Ex7. CTest passed 2/2 tests.
- 2026-10-19: Phase 0 complete. Built repo structure, CMakeLists.txt, CLI stub in src/cli/main.cpp, and doctest sanity test in tests/unit/test_sanity.cpp. Configured build with MinGW/GCC 15.2 and CMake 4.4.4; ctest passed 1/1.


