# PRD — RetroC

## 1. One-line pitch
A compiler that turns a normal batch dataflow program into a retroactively editable computation: you can modify an earlier input, insert a stage, or delete a stage, and only the affected part of the computation is redone.

## 2. Problem
Data pipelines are re-run from scratch whenever an early assumption changes (a mislabeled row, a missing normalization step, a stage that should not be there). Incremental systems handle "new input arrives" well, but treating the pipeline's own history (inputs and stages) as editable is awkward or unsupported.

## 3. Goals
- G1: Complete compiler front end for a small pure dataflow language (lexer, parser, semantic analysis).
- G2: A versioned dataflow graph IR (the "Retroactive IR") that records dependencies, versions, and per-record memo tables.
- G3: Three retroactive edit operations as graph rewrites: modify input record, insert stage, delete stage.
- G4: Fine-grained recomputation: reuse per-record results; early cutoff when a recomputed value is unchanged.
- G5: A verifier proving (by testing) that edit-then-propagate equals a full rerun of the edited program.
- G6: Measured benefit vs full rerun and vs stage-level invalidation on defined workloads.
- G7: A rebuild-plan report: what was recomputed, what was reused, why.

## 4. Non-goals (do not build)
- Pointers, mutation, concurrency, I/O inside lambdas, recursion, user-defined types beyond records.
- A general-purpose language, an optimizing backend, a GUI, distributed execution.
- Competing with production systems (Differential Dataflow, Spark). We compare against them only as a reference.

## 5. Users and use cases
- Primary user: a student or analyst running a data pipeline (sensor logs, sales data) who revises early steps.
- U1: Fix one bad input record, see updated outputs fast.
- U2: Insert a cleaning stage in the middle of a pipeline and see downstream results.
- U3: Delete a stage and see what changes.
- U4: Ask "why did output X change?" and get the list of edits and stages responsible.

## 6. Functional requirements
- FR1: Compile `.rc` source per LANGUAGE_SPEC.md; report errors with code and line:col.
- FR2: `retroc run prog.rc` executes from scratch and records history.
- FR3: `retroc dump-ir prog.rc` prints the IR in the canonical format (IR_SPEC.md).
- FR4: `retroc edit prog.rc edits.rcs` applies an edit script to the recorded history and prints outputs + rebuild plan.
- FR5: `retroc verify prog.rc edits.rcs` runs both paths and compares outputs.
- FR6: `retroc explain <output>` lists the edits/stages that changed it.
- FR7: Invalid edits are rejected atomically (state unchanged) with an error code.

## 7. Non-functional requirements
- Deterministic output (byte-identical across runs).
- Correctness first: 100% match with full rerun in differential testing.
- Memory overhead of history reported and bounded (target: under 5x input size for the benchmark workloads; measure, do not assume).
- Builds with no warnings; tests run under sanitizers.

## 8. Success metrics (fill with real numbers later; never invent)
| Metric | Target | Measured |
|---|---|---|
| Differential tests matching full rerun | 100% of >= 1000 trials | |
| Speedup, modify 1 record, W1 (100k rows) | >= 10x vs full rerun | |
| Reuse % on insert-near-end edit | >= 80% | |
| History-recording overhead on first run | <= 2.5x plain run | |
| Spec coverage by tests | every grammar rule + every edit rule | |

## 9. Course deliverables (adjust to your rubric)
- Working compiler + runtime, source repo, README with build/run steps.
- Report: problem, related work (PRIOR_WORK.md), design (IR + edit semantics), evaluation (BENCHMARKS.md), limitations.
- Demo script: one sensor-log pipeline, three edits, printed rebuild plans, speedup table.

## 10. Risks
| Risk | Mitigation |
|---|---|
| Scope creep | Non-goals list; one phase at a time; AGENTS.md rules |
| Insert/delete correctness bugs | Differential tester from Phase 7 onward |
| Novelty challenged as "just incremental computation" | PRIOR_WORK.md claim table; fine-grained reuse + baselines + honest framing |
| Memo tables blow up memory | Measure early (Phase 6); cap or evict as stretch |
| Hash collisions give wrong reuse | 64-bit hash + full-equality check in verifier mode |

## 11. Open questions
- Final project name and title wording.
- Whether to emit standalone C++ (stretch) or ship runtime + bytecode only.
