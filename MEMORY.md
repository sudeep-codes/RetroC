# MEMORY.md — Running state of the project (update at the end of every session)

## Current status
- Current phase: Phase 0 (setup + spec freeze)
- Last session: (date) — (one line)
- Build: not started
- Tests: not started

## Decisions (append only; include the reason)
| ID | Date | Decision | Reason |
|----|------|----------|--------|
| D-001 | | Implementation language: C++17 + CMake + doctest (PROPOSED, confirm or change) | Fast runtime for honest benchmarks; familiar |
| D-002 | | Backend v1: lambda bodies compiled to a small stack bytecode; C++ runtime executes the graph with memo tables (PROPOSED) | Edits (insert/delete stage) need runtime graph changes; emitting standalone C++ is a stretch goal |
| D-003 | | Record-level (fine-grained) memoization for MAP/FILTER from the start | Main novelty lever vs stage-level invalidation |
| D-004 | | Language is pure, no recursion, no `for` loops in v1 | Keeps correctness proof and scope manageable |

## Open questions (for the human)
- (none yet)

## Known bugs
- (none yet)

## Phase checklist
- [ ] Phase 0 setup + spec freeze
- [ ] Phase 1 lexer
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
1. 

## Session log (newest first, 2-3 lines each)
- 
