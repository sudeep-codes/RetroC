# ARCHITECTURE — RetroC

## 1. Tech stack (PROPOSED: confirm in MEMORY.md D-001/D-002)
- C++17, CMake >= 3.16, doctest (single header) for unit tests, plain shell/Python scripts for golden + differential runners.
- Sanitizers (ASan + UBSan) on in test builds.

## 2. Pipeline
```
source.rc
  -> Lexer           (tokens, E1xx)
  -> Parser          (AST, E2xx)
  -> Sema            (types, scopes, purity, E3xx/E4xx)  -> typed AST
  -> IR Builder      (typed AST -> Retroactive IR graph)
  -> Bytecode Gen    (lambda/expr bodies -> stack bytecode)
  -> Runtime         (executes graph, records history/memo)       <- first run
  -> Edit Engine     (applies edits as graph rewrites)
  -> Reuse Analyzer  (marks DIRTY/CLEAN, early cutoff, rebuild plan)
  -> Propagator      (recomputes only the plan)                    <- after edits
  -> Verifier        (full rerun vs propagated, compares outputs)
```

## 3. Folder layout
```
retroc/
  AGENTS.md  MEMORY.md  README.md  CMakeLists.txt
  docs/                      (all specs)
  src/
    common/                  (errors, source locations, hashing, value type, printer)
    lexer/  parser/  sema/
    ir/                      (graph, node, dump)
    bytecode/                (expr compiler + VM)
    runtime/                 (executor, memo tables, history)
    edit/                    (edit script parser, rewrites, validation)
    reuse/                   (dirty marking, early cutoff, rebuild plan, explain)
    verify/                  (full-rerun comparator)
    cli/                     (main.cpp, subcommands)
  tests/
    unit/                    (doctest)
    golden/<stage>/*.rc + *.expected
    diff/                    (random program + edit generator, runner)
  examples/                  (the Ex1..Ex7 programs + sensor demo)
  bench/                     (workload generators, runner, results/)
```

## 4. Module boundaries (who may include whom)
`common` <- `lexer` <- `parser` <- `sema` <- `ir` <- `bytecode` <- `runtime` <- `edit` <- `reuse` <- `verify` <- `cli`.
A module may include only modules to its left. No cycles. `runtime` knows nothing about the edit script syntax; `edit` produces graph rewrites that `runtime`/`reuse` consume.

## 5. Key data structures
- `Value`: tagged union (int, float, bool, string, record, array). Immutable; hashed structurally (64-bit) with cached hash.
- `Collection`: vector of `(RecordId, Value)` in order.
- `Node`: see IR_SPEC.md. `Graph`: nodes by stable NodeId + topological order + output list.
- `Memo`: per-node table. MAP/FILTER: `RecordId -> {inHash, fnHash, out}`. JOIN: hash index on keys. REDUCE: last accumulated value + input hash (tree is stretch).
- `History`: edit log (EditId, edit, affected nodes, stats) + per-node version counters.

## 6. CLI
```
retroc run      prog.rc                 # execute from scratch, record history, print outputs
retroc dump-ir  prog.rc                 # canonical IR dump
retroc edit     prog.rc edits.rcs       # run, then apply edits one by one, print outputs + rebuild plan per edit
retroc verify   prog.rc edits.rcs       # compare propagated result with full rerun of edited program
retroc explain  prog.rc edits.rcs NAME  # which edits/stages changed output NAME
retroc bench    --workload W1 ...       # see BENCHMARKS.md
```
Exit codes: 0 ok, 1 compile error, 2 invalid edit, 3 verification mismatch, 4 runtime error.

## 7. Design decisions and rationale
- Graph is rebuilt/rewired in memory by edits; the source file is not re-parsed for stage edits (edit script has its own small parser reusing the expression parser).
- Memo key includes `fnHash` (structural hash of the lambda IR) so a changed stage never reuses stale results.
- Early cutoff compares output hashes, not timestamps.
- Single-threaded, deterministic.

## 8. Extension points (stretch only)
Tree-based incremental reduce, cost model for recompute-vs-patch, record insert/delete on inputs, standalone C++ emission.
