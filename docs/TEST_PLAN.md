# TEST_PLAN

## 1. Principles
- Every phase has tests before it is "done". Specs define expected behavior; tests encode it.
- Golden tests (exact output), unit tests (doctest), differential tests (random edits vs full rerun), metamorphic tests, error-case tests, sanitizer runs.

## 2. Layout and commands
```
tests/unit/        doctest files per module
tests/golden/lexer|parser|sema|ir|run|edit/   <name>.rc (+ <name>.rcs) and <name>.expected
tests/diff/        generator.cpp, runner.cpp, failures/
```
```
cmake -B build-test -DCMAKE_BUILD_TYPE=Debug -DRC_SANITIZE=ON && cmake --build build-test
ctest --test-dir build-test --output-on-failure
./build-test/tests/difftest --trials 1000 --seed 1
```
Golden runner: run `retroc <cmd>` on each `.rc`, diff stdout against `.expected`. Updating a golden file requires a stated reason in the commit/summary.

## 3. Tests per stage
| Stage | Unit/golden coverage |
|---|---|
| Lexer | every token kind; comments; float vs int; string escapes; E101, E102 with correct line:col |
| Parser | every grammar rule; precedence table; lambda vs paren lookahead; AST dump golden; E201 variants |
| Sema | Ex1-Ex7; every E3xx/E4xx code; inference of lambda param types; join key types; no forward refs |
| IR builder | IR dump golden for Ex1-Ex4; nested builtin splitting (`s$1`); user `fn` inlining; I1, I2 asserted |
| Runtime | Ex1-Ex4 expected output; empty collections; division by zero E501; float formatting; deterministic output |
| History | memo sizes after first run; version counters; empty-memo run equals memoized run (I8) |
| Edit A | modify single record; modify causing filter flip; modify to same value (zero recompute); bad RecordId (E601) |
| Edit B | insert map; insert filter; insert before multi-consumer stage; type-breaking insert rejected atomically |
| Edit C | delete map; delete filter; delete that breaks types rejected; delete last producer of OUTPUT rejected |
| Reuse/plan | op counts match hand-computed values for Ex2 modify; early cutoff triggers; plan format golden |
| Explain | provenance chain for a 3-edit script |

## 4. Differential tester (the main correctness evidence)
Generator (seeded, deterministic):
- Random program: 3-8 stages from {filter, map, reduce, join, scalar}, random int/float lambdas, well-typed by construction.
- Random data: 20-2000 records, includes duplicates and edge values (0, negatives, empty).
- Random edit script: 1-6 edits; 60% modify, 20% insert, 20% delete; only valid edits are generated, plus a separate pass generating invalid edits that must be rejected.
Check per trial: (a) propagated outputs == full-rerun outputs, with full-value equality (not only hashes); (b) invalid edits leave state byte-identical; (c) all nodes CLEAN, I1-I8 hold.
On failure: write `seed`, program, data, edit script to `tests/diff/failures/<seed>/` and exit 3.
Acceptance: 1000 trials with seeds 1..1000 pass; report the number in the paper.

## 5. Metamorphic tests
- Apply edit then its inverse (modify back, delete the inserted stage, re-insert a deleted stage) => outputs equal the original.
- Apply edits in two different valid orders that commute (modify distinct records) => equal outputs.
- Insert an identity map (`x => x`) => outputs unchanged, 100% reuse downstream.

## 6. Non-functional
- Sanitizers (ASan, UBSan) clean on the whole suite.
- Determinism: run each golden twice, byte-compare.
- Performance regression: `bench` smoke run on W1 small size must not slow down more than 2x versus the recorded baseline in `bench/results/baseline.json`.

## 7. Phase exit rule
A phase is complete only when: all tests listed for that phase pass, no earlier tests regressed, and sanitizers are clean.
