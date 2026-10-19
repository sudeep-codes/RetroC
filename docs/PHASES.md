# PHASES

Rule: one phase at a time. Start each session with: "Read AGENTS.md, MEMORY.md, docs/PHASES.md. Implement Phase N only. Update MEMORY.md when done."
Estimates are rough effort for one person with an AI agent; adjust to your deadline.

## Phase 0 — Setup and spec freeze (0.5 day)
Deliverables: repo skeleton per ARCHITECTURE.md, CMake builds an empty `retroc`, doctest wired, `ctest` runs one trivial test, specs reviewed by you.
Acceptance: clean build, `ctest` passes, MEMORY.md decisions D-001/D-002 confirmed or changed.
Out of scope: any compiler logic.

## Phase 1 — Lexer (0.5 day)
Deliverables: `src/lexer`, token dump command, golden tests.
Acceptance: all token kinds tested; E101/E102 with correct line:col; Ex1-Ex7 tokenize.

## Phase 2 — Parser + AST (1 day)
Deliverables: recursive-descent parser for the full grammar, AST printer, error recovery up to first error.
Acceptance: Ex1-Ex7 parse; precedence tests; lambda-vs-paren lookahead tests; E201 messages golden.

## Phase 3 — Semantic analysis (1.5 days)
Deliverables: symbol tables, type inference, lambda param inference, builtin signatures, recursion check, forward-ref check, typed AST.
Acceptance: Ex1-Ex4 typecheck; Ex5-Ex7 give E301/E401/E402 at the right line; every E3xx/E4xx code has a test.

## Phase 4 — IR builder + dump (1 day)
Deliverables: typed AST -> Graph (IR_SPEC.md), nested builtin splitting, `fn` inlining, `retroc dump-ir`.
Acceptance: IR dump for Ex1-Ex4 matches golden; invariants I1, I2 asserted.

## Phase 5 — Runtime: from-scratch execution (1.5 days)
Deliverables: Value type, expression bytecode compiler + VM, executor for all node kinds, CSV loader, printer, `retroc run`.
Acceptance: Ex1-Ex4 expected outputs exactly; runtime error E501 tests; determinism test.

## Phase 6 — History recording (1 day)
Deliverables: per-node memo tables filled during the first run, hashes, versions, history log struct; memory accounting.
Acceptance: run with memo equals run with empty memo (I8); memo sizes reported by `--verbose`; first-run overhead measured and noted in MEMORY.md.

## Phase 7 — Modify-input propagation (1.5 days)
Deliverables: edit script parser (modify only), work-set propagation, early cutoff, `retroc edit`, `retroc verify`, first differential tester (modify edits only).
Acceptance: Edit A tests pass; 500 differential trials pass; modify-to-same-value does zero recompute.

## Phase 8 — Insert and delete stage (2 days)
Deliverables: insert/delete rewrites, re-typecheck, atomic rollback, topological reorder.
Acceptance: Edit B/C tests pass; invalid edits rejected with unchanged state; metamorphic tests pass; differential tester extended to all three edit types, 1000 trials pass.

## Phase 9 — Reuse analyzer, rebuild plan, explain (1 day)
Deliverables: op counting, plan printer in the exact format of EDIT_SEMANTICS.md section 7, history log, `retroc explain`.
Acceptance: hand-computed op counts match for Ex2 modify and one insert case; plan format golden; explain golden for a 3-edit script.

## Phase 10 — Verifier hardening (0.5 day)
Deliverables: verifier mode with full-value comparison (no hash-only), failure artifact dump, invalid-edit generator pass.
Acceptance: 1000+ trials pass with seeds recorded; sanitizers clean.

## Phase 11 — Benchmarks (1.5 days)
Deliverables: workload generators W1-W4, runner, baselines B0/B1/B2 (see BENCHMARKS.md), results JSON + table.
Acceptance: results table filled with measured numbers (median of 11 runs); losses and overheads reported honestly.

## Phase 12 — Stretch (pick in this order, only after Phase 11)
1. Tree-based incremental REDUCE for associative combiners (`tree_reduce`): O(log n) per changed element.
2. Cost model: choose recompute vs patch for JOIN and REDUCE using node `cost` and changed-record fraction.
3. Edits on input records: insert/delete record.
4. Control-flow stages (`if` at stage level) with edits that flip a branch.
5. Standalone C++ emission of the initial execution.
6. Baseline B3: compare with Differential Dataflow or Adapton on modify-input.

## Final phase — Report and demo (1.5 days)
Deliverables: README, report (PRD section 9), demo script with three edits and plans, slides if required.
Acceptance: a fresh clone builds and the demo runs from the README in under 5 minutes.
