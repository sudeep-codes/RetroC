# RetroC

**RetroC** is an optimizing research compiler and execution engine for a pure, versioned dataflow language. It transforms batch dataflow pipelines into **retroactively editable computations**: pipelines where past inputs can be modified, stages inserted, or stages deleted without paying the prohibitive cost of a full rerun from scratch.

Unlike traditional incremental computation systems designed primarily for appending streaming data (such as differential dataflow or streaming engines), RetroC treats the pipeline’s *own developmental history* as a mutable, retroactive graph. By combining **record-level fine-grained memoization**, **dynamic topological rewrites**, **early cutoff propagation**, and **tree-based reduction**, RetroC is faster than a full rerun in all **35 measured scenarios** (1.08×–53,400×); a single-record edit is **525×–53,400× faster** from 10k to 1M records. It beats stage-level invalidation in 33 of 34 scenarios and is on par in the remaining one (W4 E-F, 0.98×), where both strategies do identical work.

---

## Key Features

1. **Pure Dataflow Language**: Strongly typed, deterministic dataflow language with support for integers, floats, booleans, strings, arrays, records, lambdas, and builtins (`map`, `filter`, `reduce`, `tree_reduce`, `join`).
2. **Versioned Retroactive IR**: A directed acyclic graph (DAG) where nodes track inputs, versions, content hashes, and per-record execution memo tables adhering to formal invariants (I1–I9).
3. **Three Atomic Retroactive Edit Kinds**:
   - **Edit A (`retro modify`)**: In-place modification of an input record with fine-grained, record-level delta propagation and early cutoff.
   - **Edit B (`retro insert`)**: Dynamic insertion of new transformation stages (`map`, `filter`) with automated Kahn's topological reordering.
   - **Edit C (`retro delete`)**: Elimination of arbitrary non-input/non-output stages with dependency bypass.
4. **Tree-Based Incremental Reduce (`tree_reduce`)**: Binary reduction tree memoization enabling $O(\log N)$ updates for associative combiners (e.g., recomputing $\le 10$ combiner steps for 1,024 elements).
5. **Atomic Transactional Rollback**: Guaranteed zero state corruption; any edit triggering a type error, cycle, or syntax violation is rejected atomically with rollback to the prior consistent snapshot.
6. **Provenance & Rebuild Plans (`explain`)**: Transparent accounting reporting exactly which records were evaluated vs reused, total operations saved, and the causal chain of edits affecting any output.
7. **Equivalence Verifier & Differential Testing**: Built-in verification testing tool ensuring that every retroactive edit execution produces byte-identical, structurally equivalent results compared against a from-scratch rerun of the rewritten program text.

---

## Project Structure

```
RetroC/
├── bench/                  # Workload generators (W1-W4) & benchmark runner (B0/B1/B2)
├── demo/                   # End-to-end interactive sensor-log demo
│   ├── pipeline.rc         # Sensor log batch pipeline
│   ├── edits.rcs           # Script with Modify, Insert, and Delete edits
│   ├── run_demo.bat        # Windows demo launcher
│   └── run_demo.sh         # Linux/macOS demo launcher
├── docs/                   # Formal language, IR, and architecture specifications
│   ├── ARCHITECTURE.md     # Module boundaries and layout
│   ├── BENCHMARKS.md       # Measured evaluation results on W1-W4
│   ├── EDIT_SEMANTICS.md   # Edit rules, rebuild plans, and propagation logic
│   ├── IR_SPEC.md          # DAG node structure and invariants
│   ├── LANGUAGE_SPEC.md    # Grammar, types, and error codes
│   └── PRD.md              # Project requirements document
├── examples/               # Example programs (ex1.rc - ex7.rc)
├── src/                    # Compiler and runtime source code
│   ├── bytecode/           # Stack bytecode compiler and VM
│   ├── cli/                # Command-line interface driver (main.cpp)
│   ├── common/             # Values, error reporting, source locations
│   ├── edit/               # Edit script parser and source rewriter
│   ├── ir/                 # IR DAG, builder, nodes, and canonical dumper
│   ├── lexer/              # Tokenizer
│   ├── parser/             # Recursive-descent AST parser
│   ├── reuse/              # Propagator, work-set planner, and explain engine
│   ├── runtime/            # Graph executor, CSV loader, memo tables
│   ├── sema/               # Typechecker, symbol table, scope validator
│   └── verify/             # Rerun verifier and structural equality checker
└── tests/                  # Unit, golden, and differential test suites
    ├── diff/               # Randomized differential tester (10,000 trials over 5 seeds)
    ├── golden/             # Stage-by-stage golden tests
    └── unit/               # Doctest unit test suites (65 test cases)
```

---

## Getting Started

### Prerequisites

- **C++ Compiler**: Modern C++17 compatible compiler (GCC 9+, Clang 10+, or MSVC 2019+).
- **Build System**: [CMake](https://cmake.org/) (3.20 or newer).
- **Optional**: Python 3 (for running golden test harnesses via CTest).

### Building

```bash
# 1. Configure the build directory
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build compiler, runtime, tests, and benchmarks
cmake --build build --config Release
```

The resulting binaries will be placed in `build/`:
- `retroc` (CLI compiler driver)
- `unit_tests` (Doctest test suite)
- `difftest` (Differential tester)
- `gen_workload` (Workload synthesizer)
- `bench_runner` (Benchmark harness)

---

## Quickstart: Running the Demonstrations

### 1. Professor Presentation Demo (Interactive & Detailed)
An interactive presentation demonstration designed for project evaluations and technical walkthroughs with full step-by-step theoretical commentary:

```cmd
demo\prof_demo.bat
```
*(Or via PowerShell: `powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1`)*

For an accompanying slide-by-slide oral talking script, anticipated questions, and rigorous technical answers, consult the **[Professor Walkthrough Guide](demo/DEMO_WALKTHROUGH.md)**.

### 2. Fast 5-Second Automated Demo
A quick automated demo running the sensor log pipeline, 3 edits, verification, and provenance in seconds:

#### On Windows:
```cmd
demo\run_demo.bat
```

#### On Linux / macOS:
```bash
chmod +x demo/run_demo.sh
./demo/run_demo.sh
```

---

## CLI Reference

RetroC provides a modular command-line interface through `retroc`:

### 1. Batch Execution
Execute a program from scratch and print its output stages:
```bash
retroc run examples/ex2.rc
```

### 2. Retroactive Editing
Apply an edit script (`.rcs`) to an existing pipeline, printing the stage rebuild plan and updated outputs:
```bash
retroc edit examples/ex2.rc tests/golden/edit/ex2_three_edits.rcs
```

### 3. Equivalence Verification
Verify that edit propagation produces byte-identical results to an end-to-end recompilation and rerun:
```bash
retroc verify examples/ex2.rc tests/golden/edit/ex2_three_edits.rcs
```

### 4. Output Provenance (`explain`)
Explain why an output value changed by listing the exact edits and stages responsible:
```bash
retroc explain examples/ex2.rc tests/golden/edit/ex2_three_edits.rcs avg
```

### 5. Inspecting AST, Tokens, and IR
```bash
retroc dump-tokens examples/ex1.rc
retroc dump-ast examples/ex1.rc
retroc dump-ir examples/ex1.rc --verbose
retroc check examples/ex1.rc
```

---

## Test Suites

RetroC enforces strict correctness and test coverage across the entire compiler stack. Run all 8 test suites via CTest:

```bash
ctest --test-dir build --output-on-failure
```

```
Test project C:/Projects/RetroC/build
    Start 1: unit_tests .......................   Passed    (65 test cases, 1,097 assertions)
    Start 2: golden_tests_lexer ...............   Passed
    Start 3: golden_tests_parser ..............   Passed
    Start 4: golden_tests_sema ................   Passed
    Start 5: golden_tests_ir ..................   Passed
    Start 6: golden_tests_run .................   Passed
    Start 7: golden_tests_edit ................   Passed
    Start 8: diff_tests .......................   Passed

100% tests passed out of 8
```

---

## Benchmark Evaluation

Measured on a 13th Gen Intel Core i7-13620H (Windows 11) across six workload sizes and six edit kinds, comparing **B0** (full from-scratch rerun), **B1** (coarse stage-level invalidation), and **B2** (RetroC fine-grained record memoization):

| Workload | Edit | B0 full rerun | B1 stage-level | B2 RetroC | vs full rerun | vs stage-level |
|---|---|---|---|---|---|---|
| W1 sensor pipeline, 10k | modify 1 record | 30.3 ms | 29.9 ms | **0.06 ms** | **525×** | 520× |
| W1 sensor pipeline, 100k | modify 1 record | 504.1 ms | 515.9 ms | **0.09 ms** | **5,444×** | 5,572× |
| W1 sensor pipeline, 1M | modify 1 record | 5,259.9 ms | 5,493.9 ms | **0.10 ms** | **53,400×** | 55,776× |
| W2 join (500 users + 2,000 orders) | modify 1 record | 8.47 ms | 9.15 ms | **0.05 ms** | **185×** | 199× |
| W3 12-stage pipeline, 100k | modify 1 record | 1,592.6 ms | 1,507.0 ms | **0.15 ms** | **10,617×** | 10,047× |
| W4 reduce-heavy, 1M | modify 1 record | 1,392.0 ms | 1,339.8 ms | **0.05 ms** | **27,348×** | 26,323× |
| W1 sensor pipeline, 100k | modify 100 records | 508.2 ms | 40,723.1 ms | **9.84 ms** | **51.6×** | 4,138× |
| W4 reduce-heavy, 1M | modify 100 records | 1,398.0 ms | 133,673.0 ms | **1.31 ms** | **1,065×** | 101,838× |
| W1 sensor pipeline, 100k | insert map near end | 611.4 ms | 156.9 ms | **139.8 ms** | **4.37×** | 1.12× |
| W3 12-stage pipeline, 100k | insert identity map | 1,908.6 ms | 312.4 ms | **243.2 ms** | **7.85×** | 1.28× |
| W1 sensor pipeline, 100k | insert map near start | 624.9 ms | 521.0 ms | **503.4 ms** | **1.24×** | 1.04× |

Protocol: pinned to one core, 3 warm-up runs and the median of 11 (runs over 10 s: 1 warm-up, median of 5), systems timed round-robin. Full table: [bench/published/2026-10-29/full_run.txt](bench/published/2026-10-29/full_run.txt).

- **First-run cost of recording history**: 18.9% (479.2 ms plain vs 569.7 ms with memo and history, W1-100k), repaid by the first edit.
- **Differential correctness**: 10,000 / 10,000 randomized programs and edit scripts matched a full rerun; the verifier passed on all 35 benchmark scenarios.
- **Tree Reduce ($O(\log N)$)**: Recomputes $\le 10$ combiner steps on 1,024 elements upon modify edit instead of 1,024 linear operations.

For complete benchmark methodology and analysis, refer to [docs/BENCHMARKS.md](file:///c:/Projects/RetroC/docs/BENCHMARKS.md).

---

## License

This project is developed as part of an advanced compiler design and incremental computing research curriculum.
