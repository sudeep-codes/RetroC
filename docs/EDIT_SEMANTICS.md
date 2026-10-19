# EDIT_SEMANTICS — Retroactive edits

## 1. Edit script (`.rcs`)
One edit per line, `//` comments allowed. Stage names refer to stages in the current graph.
```
retro modify  <input>[<RecordId>].<field> = <literal>
retro insert  before <stage> : map(_, <lambda>)        // or filter(_, <lambda>)
retro delete  <stage>
```
- `_` denotes the first input of `<stage>` (the collection the new stage will read).
- Inserted stages are named `<stage>$ins<k>` unless `as <name>` is given: `retro insert before scaled as clamped : map(_, r => ...)`.
- Edits apply in order. Each edit gets an `EditId` (1, 2, 3, ...).

## 2. Common rules
- **Atomic:** an edit either fully applies or leaves graph, memo, and history unchanged. Failures return E601 with a reason.
- **Typed:** after the rewrite, re-run type checking on the whole graph (I6). Any type error rejects the edit.
- **Purity:** inserted lambdas obey the language rules (no recursion, pure builtins only).
- **Provenance:** every recomputation records the `EditId` that caused it (used by `explain`).

## 3. Edit A — modify input record
Precondition: input stage exists, RecordId exists, field exists, literal has the field's type.
Rewrite: replace the field value in the INPUT record; recompute its hash.
Seed work set: `{(input, RecordId)}`.

## 4. Edit B — insert stage
Precondition: `<stage>` exists and is not an INPUT; its first input P is a collection; the new lambda type-checks against P's element type; `<stage>`'s own function still type-checks against the new stage's output element type.
Rewrite:
1. Create node N (kind MAP or FILTER, fresh NodeId) with `inputs = [P]`.
2. Replace `<stage>.inputs[0]` with N.
3. Recompute topological order (I1).
4. Seed work set: `{(N, r) for every record r of P}`. N has an empty memo, so N is evaluated for every record. This is unavoidable O(|P|) lambda evaluations.
5. Downstream reuse comes from the next rule (early cutoff, section 6): `<stage>` reuses its memo for every record whose new input hash equals the hash it was previously computed with.

## 5. Edit C — delete stage
Precondition: `<stage>` is MAP or FILTER whose output element type equals its input element type (so consumers still type-check), or removal otherwise type-checks (I6). INPUT, REDUCE, JOIN, SCALAR stages cannot be deleted in v1.
Rewrite:
1. For every consumer C of `<stage>`, replace the reference with `<stage>.inputs[0]`.
2. Remove the node from the graph (its memo is kept in the history log for `explain`; NodeId is never reused, I2).
3. Recompute topological order.
4. Seed work set: `{(C, r) for each consumer C, each record r}`; each consumer reuses memo entries whose input hash is unchanged (early cutoff).
Deleting a stage that is the only source of an OUTPUT is an error (E601).

## 6. Propagation algorithm (shared)
```
propagate(workSet):
  for node in graph.order:                      # topological
    if node has no pending work and no dirty inputs: continue
    switch node.kind:
      MAP/FILTER:
        for each record r in node's input (only pending records if input changed set is known, else all):
          key = (hash(in[r]), node.fnHash)
          if memo[r].matches(key): reuse           # I3
          else: evaluate fn, store in memo[r], count as recomputed
      JOIN:   re-evaluate keys for pending records; recompute only affected key groups
      REDUCE: if input hash unchanged and fnHash/initHash unchanged: reuse
              else recompute fold (O(n) in v1)
      SCALAR: reuse if inputsHash+fnHash unchanged, else evaluate
    newOutHash = hash(output)
    if newOutHash == node.outHash:  EARLY CUTOFF: mark downstream nodes UNKNOWN->CLEAN, no work
    else: node.outHash = newOutHash; node.version++; add changed records to downstream pending sets
    node.status = CLEAN
```
Records dropped or added by FILTER appear as removed/new entries in the downstream collection; downstream memo entries for removed RecordIds are garbage-collected after propagation.

## 7. Rebuild plan (output of each edit)
Counted in "operations" = lambda/expression evaluations (a REDUCE counts one per element folded).
```
Edit #2: insert before scaled as clamped : map(_, r => clamp(r, -1.0, 1.0))
Plan:
  clamped   NEW     evaluated 100000 / 100000
  scaled    PARTIAL evaluated   3112 / 100000  reused 96888  (early cutoff on unchanged inputs)
  sum       FULL    evaluated 100000 / 100000  (reduce is O(n) in v1)
  avg       FULL    evaluated      1 / 1
Recomputed: 203113 ops   Reused: 96888 ops   Full-rerun cost: 300001 ops
Reuse: 32.3%   Updated outputs: avg
```
Reuse % = reused / (recomputed + reused), over operations. Always print the full-rerun op count so the reader can judge.

## 8. Correctness statement (what the verifier checks)
For any well-typed program P, input data I, and valid edit e:
> `Propagate(History(P, I), e)` produces exactly the same outputs as `Run(Apply(e, P), Apply(e, I))` from an empty memo.

Proof sketch (by induction on topological order): the pending set contains every record whose input value or whose stage function changed. A node reuses a memo entry only if its input hash and `fnHash` match (I3); because stages are pure, equal inputs under equal function imply equal outputs. Nodes not reached by the work set have unchanged inputs and function, so their outputs are unchanged. Hash collisions are the only gap; the verifier mode compares full values, not just hashes.

## 9. Worked example (modify)
Ex2 with `retro modify rows[1].ok = true`:
- seed `(rows, 1)`; `clean` re-evaluates record 1 only (1 op), now kept; `vals` evaluates record 1 (1 op); output hash changed; `sum` recomputes fold over 3 elements (3 ops); `avg` 1 op.
- New outputs: `avg = (2.0+5.0+4.0)/3.0 = 3.6666666666666665`.

## 10. Error cases (all E601 unless noted)
Unknown stage / RecordId / field; literal type mismatch; insert before an INPUT; lambda type error (E301 inside the edit); delete creates a type error; delete the last producer of an OUTPUT; edit script syntax error (E201).

## 11. Limitations (state these in the report)
v1 REDUCE is O(n) when any element changes; stages cannot be replaced (only inserted/deleted); no record insert/delete on inputs; no control-flow edits. Stretch ideas are in PHASES.md Phase 12.

## 12. History and explain
History log entry: `{EditId, edit text, nodes touched, ops recomputed, ops reused, outputs changed}`. `retroc explain NAME` prints the chain of edits whose propagation changed output NAME, with the first stage where each change entered the graph.
