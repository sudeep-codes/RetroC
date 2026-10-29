# IR_SPEC — Retroactive IR

## 1. Overview
The IR is a **versioned dataflow DAG**. One node per stage. Edges are data dependencies. Each node carries enough bookkeeping to reuse its previous results after an edit.

## 2. Node
```
Node {
  id        : NodeId        // unique, never reused, even after deletion
  name      : string        // stage name from source; inserted stages get names from the edit
  kind      : INPUT | MAP | FILTER | REDUCE | JOIN | SCALAR | OUTPUT
  inputs    : [NodeId]      // ordered; first the iterated collections (MAP/FILTER/REDUCE/TREE_REDUCE: 1; JOIN: 2; SCALAR: 0),
                            // then every stage that fn/init/expr reads by name (e.g. `total` in `x => x + total`)
  fn        : ExprIR*       // lambda body (MAP/FILTER/REDUCE/JOIN keys); null for INPUT/OUTPUT
  init      : ExprIR*       // REDUCE initial value
  expr      : ExprIR*       // SCALAR expression
  type      : IRType        // result type of this stage
  fnHash    : u64           // structural hash of fn/init/expr (and of fns they call)
  version   : u64           // incremented each time this node's output value changes
  outHash   : u64           // hash of the current output value
  status    : CLEAN | DIRTY | UNKNOWN
  cost      : float         // estimated cost units per record evaluation (static estimate)
  memo      : Memo          // per-record table (see ARCHITECTURE.md)
}
```
`ExprIR`: typed expression tree: `Const, Var(param|stage), Field, Binary(op), Unary(op), If, Call(builtin|fn), RecordLit, ArrayLit`. User `fn` calls are inlined at IR build time (no recursion, so this terminates).

## 3. Graph
```
Graph { nodes: map<NodeId, Node>, order: [NodeId] (topological), outputs: [NodeId] }
```
`order` is recomputed after every structural edit. Source order of stages is a valid topological order initially.

## 4. Node kinds
| Kind | Inputs | Per-record behavior | Memo |
|---|---|---|---|
| INPUT | 0 | holds records with RecordIds | none |
| MAP | 1 collection | out[r] = fn(in[r]) | `RecordId -> {inHash, fnHash, out}` |
| FILTER | 1 collection | keep r if fn(in[r]) | `RecordId -> {inHash, fnHash, keep}` |
| JOIN | 2 collections | equi-join on key fns (hash join; output in left order, then right order) | key index for both sides (`RecordId -> {inHash, keyFnHash, key}`); pair-level memo for matched pairs |
| REDUCE | 1 collection | left fold | `{inputHash, fnHash, initHash, result}`; additive int folds (`(a, x) => a + t(x)`, t reads neither `a` nor a stage) also keep `RecordId -> t(x)` and the term sum |
| SCALAR | stages | evaluate expr | `{inputsHash, fnHash, result}` |
| OUTPUT | 1 stage | print | none |

## 5. Invariants (reference these in code comments)
- I1: The graph is a DAG; `order` is a valid topological order.
- I2: NodeIds are unique and never reused.
- I3: A memo entry may be reused only if its `inHash` AND `fnHash` match the current ones. For a node that reads stages by name, the memo key is `fnHash` combined with the `outHash` of each such stage, so a change to a referenced stage invalidates every entry of the node.
- I4: `version` increases iff `outHash` changes.
- I5: After propagation, every node has status CLEAN and its output equals the pure function of its current inputs.
- I6: Every node type-checks against its inputs (checked after every edit; failure rejects the edit).
- I7: Collections preserve the order of their source records; RecordIds are stable through MAP/FILTER.
- I8: Outputs are unaffected by memo state: running with an empty memo gives identical results.

## 6. Canonical dump format (golden tests compare this exactly)
Columns: node id, kind, name, `: type`, `<-` input ids, then fn/init/expr text.
For Ex2 in LANGUAGE_SPEC.md:
```
n0 INPUT  rows  : [{id:int,v:float,ok:bool}]
n1 FILTER clean : [{id:int,v:float,ok:bool}] <- n0  fn: r => r.ok
n2 MAP    vals  : [float] <- n1  fn: r => r.v
n3 REDUCE sum   : float <- n2  init: 0.0  fn: (a, x) => a + x
n4 SCALAR avg   : float <- n3,n2  expr: sum / to_float(length(vals))
n5 OUTPUT avg   : float <- n4
```
Rules: one node per line, two spaces before `fn:`/`init:`/`expr:`, expression text uses fully parenthesized form only where precedence requires it.
With `--verbose`, also print `fnHash`, `status`, `version`, and memo sizes.

## 6.1 Source-to-IR mapping
- `input` -> INPUT. `let s = filter/map/reduce/join(...)` -> FILTER/MAP/REDUCE/JOIN. Any other `let` -> SCALAR. `output s;` -> OUTPUT.
- A `let` whose right side nests builtins (e.g. `map(filter(xs, f), g)`) is split into hidden stages named `s$1, s$2, ...` so every builtin application is one node.

## 7. Status and versions
- `DIRTY`: must recompute (at least some records). `UNKNOWN`: depends on a DIRTY node, resolved by early cutoff (if the dirty ancestor's output hash is unchanged, becomes CLEAN with no work). `CLEAN`: memo valid.
- Per-record dirtiness is tracked in the propagator's work set `(NodeId, RecordId)`, not stored on the node, to keep nodes small.
