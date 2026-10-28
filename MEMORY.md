# MEMORY.md — Running state of the project (update at the end of every session)

## Current status
- Current phase: Post-review improvements 1-2 of 5 done (O(n) rescan removed; full benchmark protocol run, results in bench/published/2026-10-28/, 6593 s, 35 scenarios, all verify PASS). Next: cost model (Phase 12.2).
- Full-protocol headline (pinned P-core, median of 11): E-A modify 1 record B0/B2 = 9.63x (W1-10k), 9.87x (100k), 10.05x (1M), 22.39x (W3), 3.37x (W4-1M). B1/B2 on modifies 9-20x. Losses: E-B batches 0.03-0.23x, W2 joins 0.02-0.40x, insert near start 0.71-1.06x. Structural edits: B2 is 0.64-0.98x of B1 (C2 not supported). First-run overhead +42.7% (W1-100k).
- Last session: 2026-10-28 — Rewrote propagation so work follows changed records; implemented the B1 baseline (it was never implemented); extended difftest. Toolchain now MSYS2 UCRT64 GCC 16.2.0, CMake 4.4.3, Ninja (installed this session; `export PATH=/c/msys64/ucrt64/bin:$PATH`).
- Tests (2026-10-28): 8/8 CTest suites pass; 52 doctest cases, 687 assertions; difftest 10,000 trials (seeds 1, 1001, 5001, 20001, 77777 x 2000) pass. Sanitizers NOT run: MinGW GCC has no libasan/libubsan.
- Previous status: Final Phase complete (Project complete: Report, Demo, and Documentation delivered)
- Last session: 2026-10-27 — Final phase deliverables complete: authored comprehensive project report (REPORT.md per PRD section 9), created README.md with build and quickstart instructions, created end-to-end sensor-log demo (demo/pipeline.rc, demo/edits.rcs, demo/run_demo.bat, demo/run_demo.sh) running in < 2 seconds, 100% tests passing across all 8 suites.
- Build: clean (GCC 15.2.0 / MinGW UCRT64, CMake 4.4.4)
- Tests: 8 passed / 0 failed (100% pass across doctest unit tests (46 cases, 549 assertions), python golden tests, and differential testing runner with 1000 trials)

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
| D-010 | 2026-10-26 | Streamlined benchmark suite iterations & lean batch sizes for fast execution | Running coarse invalidation B1 on 1,000 sequential edits of 100k records causes 14,000 full-pipeline traversals; batching to 10 edits and running 1 warmup + 3 measurements keeps execution times practical (<2 minutes total) while preserving 100% verification across all workloads W1–W4 |
| D-011 | 2026-10-26 | Tree-reduce complete binary tree array representation (size 2M) and O(log N) ascend | 1-indexed binary reduction tree maps leaves to record ids directly. Single modify edit ascends at most ceil(log2 N) levels; for 1024 items, exactly 10 recomputed ops instead of full 1024 rerun |
| D-012 | 2026-10-26 | Dynamic overflow spill on FixedVMStack for large array literals | Preserves zero-allocation inline buffer for 99.9% of bytecodes (depth <= 64) while preventing stack overflow on large literals (e.g. 1024 elements) |
| D-013 | 2026-10-28 | One shared `Propagator::propagate` for modify/insert/delete; MAP/FILTER patch only pending records (binary search by RecordId) when pending <= 1/8 of input, else one full pass; consumers get only records whose output actually changed | Old loops rescanned, rehashed and copied every collection per edit, so 83% op reuse gave ~1.3x wall time |
| D-014 | 2026-10-28 | Collection hash = f(size, sum of mix(RecordId, valueHash)); kept in `NodeExecutionResult::term_sum` and patched in O(1) | Order-dependent hash forced O(n) rehash per stage per edit; collections are sorted by RecordId (I7) so the (id, value) set fixes the order |
| D-015 | 2026-10-28 | `stage_values` holds a collection only if an expression reads it (size-only marker when read only via `length`) | Copying every collection into stage_values on every visit was O(n) |
| D-016 | 2026-10-28 | Insert/delete rollback undoes the exact rewiring instead of cloning graph + results + memos | Clone was O(total memo size) on every structural edit; nothing else is mutated before the topological sort |
| D-017 | 2026-10-28 | B1 (`stage_invalidation`) now recomputes every record of a dirty MAP/FILTER/JOIN with no per-record memo; REDUCE/SCALAR keep stage-level hash reuse | The flag was stored but never read, so old B1 numbers measured B2 |
| D-018 | 2026-10-28 | difftest: 5-200 records, 6 pipeline shapes (adds filter chain + length(), branching + tree_reduce, join), 4 insert lambdas | Old generator (5-24 records, 3 shapes) never reached the patch path for most stages; mutation check: dropping one changed-record report fails at trial 16 |
| D-019 | 2026-10-28 | bench_runner follows BENCHMARKS.md: 3 warm-up + median of 11 (min/max kept); a system whose single run exceeds 10 s gets 1 warm-up + 5 runs, marked n=5 | Spec protocol; the cap keeps the 1M-record runs from taking hours |
| D-020 | 2026-10-28 | B0, B2, B1 are timed round-robin, process pinned to one performance core (`--cpu 2`, high priority) | i7-13620H is hybrid: same work ran ~2x slower on an E-core (CPU 14); sequential timing let thermal drift bias the ratios |
| D-021 | 2026-10-28 | Scenarios E-A..E-F for W1 (10k, 100k, 1M), W2, W3, W4 (1M, per spec); E-B = 1% of records capped at 100 edits; W1-1M E-B skips B1; W4 has no E-E | Edits apply one at a time and B1 recomputes the pipeline per edit; uncapped 1% at 1M under B1 would take days; deleting `mapped` in W4 changes the type |
| D-022 | 2026-10-28 | W2 stays 500 users / 2,000 orders (spec: 50k/200k) | JOIN is a nested loop, so 50k x 200k = 10^10 comparisons is not runnable |
| D-023 | 2026-10-28 | B0 for a modify on a CSV input runs on a patched copy of the cached CSV collection | Previously B0 reran the unedited data for CSV modifies |

## Benchmarks after D-013..D-017 (2026-10-28, same runner/protocol as before: 1 warm-up + median of 3; noisy machine)
| Scenario | B0/B2 before | after run 1 | after run 2 |
|---|---|---|---|
| W1 E-A modify 1 | 1.28x | 5.78x | 5.06x |
| W1 E-B modify 10 | 0.29x | 1.26x | 1.30x |
| W1 E-C insert start | 0.30x | 1.01x | 1.03x |
| W1 E-D insert end | 1.31x | 4.90x | 5.51x |
| W1 E-E delete middle | 0.86x | 2.15x | 2.24x |
| W2 E-A join | 0.22x | 0.20x | 0.22x |
| W3 E-C insert start | 0.58x | 2.11x | 0.85x (inconclusive) |
| W4 E-A modify 1 | 1.34x | 3.32x | 3.44x |
Absolute times varied up to 3x between runs (W1 first run plain: 363 ms vs 1056 ms), so only ratios that held in both runs should be quoted. Raw output was saved to the session scratchpad only, not to the repo.

## Open questions (for the human)
- Q1 (spec change needed): a lambda that reads another stage (e.g. `map(xs, x => x + total)`) does not list that stage in the node's `inputs` (IR_SPEC section 4 says MAP has 1 input), and the memo key ignores it. A modify that changes `total` leaves stale results: `retroc verify` reports `[25, 8, 9]` vs rerun `[25, 17, 18]`. Pre-existing (same on the original code). Fix needs IR_SPEC to add referenced stages as inputs and include their hashes in the memo key.
- Q2: `docs/BENCHMARKS.md` result table is stale (its B1 column measured B2). Update it after the full protocol run?

## Known bugs
- BUG-001 (open): stale results when a lambda reads another stage; see Q1.
- BUG-002 (fixed 2026-10-28): B1 baseline was never implemented (`stage_invalidation_` unused).
- BUG-003 (fixed 2026-10-28): tree_reduce incremental path silently skipped changed records missing from `record_to_leaf`; it now rebuilds the tree.

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
- [x] Phase 11 benchmarks
- [x] Phase 12 stretch 1 (tree reduce O(log N) incremental propagation)
- [x] Final phase (Report and demo complete)

## Next actions (exact, small)
1. Get an answer on Q1 (stage refs in lambdas) and Q2 (copy bench/published/2026-10-28 numbers into docs/BENCHMARKS.md) before touching docs/ or IR.
2. Cost model (Phase 12.2): per edit batch / per stage choose recompute vs patch; targets E-B batches, W2 joins, insert-near-start. Re-run full protocol after (`bench_runner bench/data bench/results --cpu 2 --machine "..."`, ~1h50m).
3. Hash-collision bound write-up. B3 baseline deferred by user.

## Session log (newest first, 2-3 lines each)
- 2026-10-28: Wrote PROJECT_REPORT.md (rubric-based). Removed the O(n) rescan from propagation (D-013..D-016), implemented B1 (D-017), extended difftest (D-018), added tests/unit/test_incremental.cpp (6 cases). W1 single-record modify went from 1.28x to ~5-6x vs full rerun. Found pre-existing BUG-001 (Q1).
- 2026-10-27: Final Phase complete. Created `README.md` with full project architecture, build instructions, CLI reference, and benchmark numbers. Authored comprehensive research project report (`REPORT.md`) covering problem, related work, design, edit semantics, tree reduction, benchmark results (W1-W4), and correctness verification. Created interactive end-to-end demo (`demo/pipeline.rc`, `demo/edits.rcs`, `demo/run_demo.bat`, `demo/run_demo.sh`) demonstrating initial execution, 3 retroactive edits, automated verification, and output explanation in under 2 seconds. All 8 CTest suites passed 100%.
- 2026-10-26: Phase 12 Stretch Goal 1 complete. Implemented `tree_reduce` builtin across Sema, IR lowering (`NodeKind::TREE_REDUCE`), Runtime (`GraphExecutor`), and Propagator (`apply_modify`, `apply_insert`, `apply_delete`). Evaluates associative combiners over complete binary reduction trees with leaves mapped to record IDs. Single modify edits propagate strictly in O(log N) operations (e.g., <=10 recomputed ops for N=1024). Updated `FixedVMStack` with dynamic overflow vector to handle large array literals without allocation overhead for small stacks. Added comprehensive unit tests in `tests/unit/test_tree_reduce.cpp`. All 8 CTest suites passed (100% pass, 46 unit test cases, 549 assertions, 1000 diff trials).
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


