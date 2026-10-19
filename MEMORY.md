# MEMORY.md — Running state of the project (update at the end of every session)

## Current status
- Current phase: Phase 1 (complete) -> Ready for Phase 2 (Parser + AST)
- Last session: 2026-10-19 — Phase 1 completed (lexer, dump-tokens command, unit tests, golden tests Ex1-Ex7)
- Build: clean (GCC 15.2.0 / MinGW UCRT64, CMake 4.4.4)
- Tests: 2 passed / 0 failed (100% pass across doctest unit tests and python golden tests)

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
- [ ] Phase 2 parser + AST
- [ ] Phase 3 semantic analysis
- [ ] Phase 4 IR builder + dump
- [ ] Phase 5 runtime (from-scratch execution)
- [ ] Phase 6 history recording
- [ ] Phase 7 modify-input propagation
- [ ] Phase 8 insert/delete stage
- [ ] Phase 9 reuse analyzer + rebuild plan + explain
- [ ] Phase 10 verifier + differential testing
- [ ] Phase 11 benchmarks
- [ ] Phase 12 stretch (tree reduce, cost model, record edits, control flow)

## Next actions (exact, small)
1. Phase 2: Implement AST data structures and recursive-descent parser in `src/parser`.
2. Implement AST printer and golden tests for parser (`tests/golden/parser/`).

## Session log (newest first, 2-3 lines each)
- 2026-10-19: Phase 1 complete. Implemented source locations and error formatting (`src/common`), token kinds and lexer (`src/lexer`), `retroc dump-tokens` CLI subcommand, comprehensive doctest unit tests (all token kinds, string escapes, comments, int vs float, E101/E102), and golden tests for Ex1-Ex7. CTest passed 2/2 tests.
- 2026-10-19: Phase 0 complete. Built repo structure, CMakeLists.txt, CLI stub in src/cli/main.cpp, and doctest sanity test in tests/unit/test_sanity.cpp. Configured build with MinGW/GCC 15.2 and CMake 4.4.4; ctest passed 1/1.


