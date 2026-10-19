# AGENTS.md — Rules for the coding agent (read this first, every session)

## Project
RetroC: a compiler for a small pure dataflow language. It builds a versioned dataflow graph (the Retroactive IR) and a runtime that supports retroactive edits (modify input record, insert stage, delete stage) with minimal, fine-grained recomputation. Results after an edit must equal a full rerun of the edited program.

## Session start checklist (do this every time)
1. Read `MEMORY.md` (current phase, decisions, bugs, next actions).
2. Read `docs/PHASES.md` and find the current phase.
3. Read only the specs that phase needs (see "Spec map" below). Do not skim everything.
4. State in 3-5 lines what you will do this session, then do it.

## Spec map (source of truth, in priority order)
1. `docs/LANGUAGE_SPEC.md`: grammar, types, semantics, example programs.
2. `docs/IR_SPEC.md`: graph, node fields, invariants, dump format.
3. `docs/EDIT_SEMANTICS.md`: edit rules, propagation, rebuild plan, correctness statement.
4. `docs/ARCHITECTURE.md`: modules, folder layout, tech stack.
5. `docs/TEST_PLAN.md`, `docs/BENCHMARKS.md`, `docs/PRD.md`, `docs/PRIOR_WORK.md`.

If code and spec disagree, the spec wins. If the spec is wrong or ambiguous, STOP and ask; do not silently change it.

## Hard rules
- Work on ONE phase at a time. Never start the next phase without being told.
- Do not add features, syntax, node kinds, or edit types that are not in the specs.
- Never edit files in `docs/` unless the user asked for a spec change. Propose spec changes in `MEMORY.md` under "Open questions".
- Every new module ships with tests in the same change. No test, not done.
- Run the full test suite before saying a task is done. Paste the pass/fail summary.
- Never claim a speedup, a test result, or a benchmark number you did not just measure.
- No new third-party dependencies without asking (allowed: doctest, a CSV parser we write ourselves).
- Keep the language pure: no mutation, no I/O inside lambdas, no randomness, no recursion.
- Deterministic output: same input gives byte-identical output (sorted maps, fixed float formatting).

## Code conventions
- C++17, CMake, `-Wall -Wextra -Werror`, `-O2` for benchmarks, `-O0 -g -fsanitize=address,undefined` for tests.
- One module per folder under `src/` (see ARCHITECTURE.md). Public API in `*.hpp`, no logic in headers except templates.
- Errors: use the error codes in LANGUAGE_SPEC.md. Every user-facing error has a code, a line:col, and a message.
- Prefer small functions and plain structs. No clever template metaprogramming.
- Comment the "why" for invariants; reference the invariant id (e.g. `// I3`).

## Testing rules
- Golden tests live in `tests/golden/<stage>/`. Never "fix" a golden file to make a test pass unless the spec says the old output was wrong. Say so explicitly if you do.
- When the differential tester fails, save seed + program + edit script to `tests/diff/failures/` and fix the root cause. Do not special-case the failing input.

## When to stop and ask
- A spec is ambiguous or contradicts another spec.
- A phase's acceptance criteria cannot be met without changing a spec.
- You are about to delete or rewrite more than ~200 lines of existing working code.
- A test is flaky.

## Definition of done (for any task)
1. Code compiles with no warnings.
2. New + existing tests pass (paste the summary).
3. Acceptance criteria of the phase item are met, checked one by one.
4. `MEMORY.md` updated (phase status, decisions, bugs, next actions).
5. Short summary to the user: what changed, what is not done, what to review.

## End of session
Update `MEMORY.md`: status, decisions made (with the reason), bugs found, exact next step. Keep entries short.
