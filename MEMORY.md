# MEMORY.md — Running state of the project (update at the end of every session)

## Current status
- Current phase: Phase 4 (complete) -> Ready for Phase 5 (runtime: from-scratch execution)
- Last session: 2026-10-20 — Phase 4 completed (typed AST -> Graph, node and graph IR data structures, fn inlining, nested builtin splitting into s$1/s$2, canonical IR dumper and CLI dump-ir subcommand, invariants I1/I2 assertions, doctest unit tests and golden tests)
- Build: clean (GCC 15.2.0 / MinGW UCRT64, CMake 4.4.4)
- Tests: 5 passed / 0 failed (100% pass across doctest unit tests and python golden tests)

## Decisions (append only; include the reason)
| ID | Date | Decision | Reason |
|----|------|----------|--------|
| D-001 | 2026-10-19 | Implementation language: C++17 + CMake + doctest (CONFIRMED) | Fast runtime for honest benchmarks; familiar |
| D-002 | 2026-10-19 | Backend v1: lambda bodies compiled to a small stack bytecode; C++ runtime executes the graph with memo tables (CONFIRMED) | Edits (insert/delete stage) need runtime graph changes; emitting standalone C++ is a stretch goal |
| D-003 | | Record-level (fine-grained) memoization for MAP/FILTER from the start | Main novelty lever vs stage-level invalidation |
| D-004 | | Language is pure, no recursion, no `for` loops in v1 | Keeps correctness proof and scope manageable |

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
- [ ] Phase 5 runtime (from-scratch execution)
- [ ] Phase 6 history recording
- [ ] Phase 7 modify-input propagation
- [ ] Phase 8 insert/delete stage
- [ ] Phase 9 reuse analyzer + rebuild plan + explain
- [ ] Phase 10 verifier + differential testing
- [ ] Phase 11 benchmarks
- [ ] Phase 12 stretch (tree reduce, cost model, record edits, control flow)

## Next actions (exact, small)
1. Phase 5: Implement `Value` representation and expression bytecode compiler + VM under `src/bytecode/`.
2. Implement graph executor for all node kinds (INPUT, MAP, FILTER, REDUCE, JOIN, SCALAR, OUTPUT) and CSV loader under `src/runtime/`.
3. Add `retroc run` CLI subcommand, verify exact output on Ex1-Ex4, test runtime error E501 and determinism.

## Session log (newest first, 2-3 lines each)
- 2026-10-20: Phase 4 complete. Implemented IR graph, node, and expression data structures in `src/ir/expr_ir.*`, `src/ir/node.*`, `src/ir/graph.*`. Implemented IR builder lowering from `TypedProgram` to `Graph` in `src/ir/builder.*` with user function inlining, nested collection builtin splitting into hidden stages (`s$1, s$2`), and invariant checks (I1 DAG topological order, I2 unique NodeIds). Implemented canonical IR dumper in `src/ir/dump.*` and `retroc dump-ir [--verbose]` CLI command. Added doctest unit tests and golden tests for Ex1-Ex4. CTest passed 5/5 tests (18 unit test cases, 327 assertions).
- 2026-10-20: Phase 3 complete. Implemented semantic analyzer in `src/sema/sema.hpp` / `src/sema/sema.cpp` and `src/sema/types.hpp` / `src/sema/types.cpp` with symbol table, sequential stage scoping, recursion rejection (E402), forward reference checks (E401), duplicate / shadowing checks (E403), lambda parameter inference, builtin signatures, and type checking for all operations. Added `retroc check` CLI command, doctest unit tests covering Ex1-Ex4 and all E3xx/E4xx codes, and golden tests for Ex1-Ex7. CTest passed 4/4 tests (15 unit test cases, 283 assertions).
- 2026-10-20: Phase 2 complete. Implemented AST nodes and printer in `src/parser/ast.hpp` / `src/parser/ast.cpp`, recursive-descent parser with lambda-vs-parenthesized lookahead and operator precedence in `src/parser/parser.hpp` / `src/parser/parser.cpp`, `retroc dump-ast` CLI subcommand, comprehensive doctest unit tests (precedence, associativity, lookahead, error codes), and golden tests for Ex1-Ex7 and syntax errors. CTest passed 3/3 tests (12 doctest test cases, 211 assertions).

- 2026-10-19: Phase 1 complete. Implemented source locations and error formatting (`src/common`), token kinds and lexer (`src/lexer`), `retroc dump-tokens` CLI subcommand, comprehensive doctest unit tests (all token kinds, string escapes, comments, int vs float, E101/E102), and golden tests for Ex1-Ex7. CTest passed 2/2 tests.
- 2026-10-19: Phase 0 complete. Built repo structure, CMakeLists.txt, CLI stub in src/cli/main.cpp, and doctest sanity test in tests/unit/test_sanity.cpp. Configured build with MinGW/GCC 15.2 and CMake 4.4.4; ctest passed 1/1.


