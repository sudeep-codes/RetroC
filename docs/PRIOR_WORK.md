# PRIOR_WORK and novelty claims

Verify every reference below (authors, venue, year) in the original source before citing it in the report. Add the real URLs/DOIs here as you read them.

## 1. Related work (read at least the abstracts)
| Area | Work | Why it matters to RetroC |
|---|---|---|
| Retroactive data structures | Demaine, Iacono, Langerman, "Retroactive Data Structures" (SODA 2004; ACM TALG 2007) | Source of the term "retroactive": editing past operations of a data structure. Do not claim to have invented the idea of retroactivity. |
| Self-adjusting computation | Acar, Blelloch, Harper, "Adaptive Functional Programming" (POPL 2002); Acar's thesis "Self-Adjusting Computation" (CMU 2005) | Dependency tracing + change propagation; closest foundation for our propagation. |
| Selective memoization | Acar, Blelloch, Harper, "Selective Memoization" (POPL 2003) | Memo keyed by inputs actually used; relates to our per-record memo. |
| Demand-driven incremental | Hammer et al., "Adapton: Composable, Demand-Driven Incremental Computation" (PLDI 2014) | Lazy recomputation; a baseline to compare conceptually. |
| Incremental dataflow | McSherry, Murray, Isaacs, Isard, "Differential Dataflow" (CIDR 2013); Murray et al., "Naiad" (SOSP 2013) | Handles input insert/delete at scale; the strongest practical competitor for modify-input. |
| Incremental lambda calculus | Cai, Giarrusso, Rendel, Ostermann, "A Theory of Changes for Higher-Order Languages" (PLDI 2014) | Compiler-style derivation of incremental programs via change structures. |
| Build systems | Mokhov, Mitchell, Peyton Jones, "Build Systems a la Carte" (ICFP 2018) | Early cutoff, minimality, suspending vs restarting schedulers; vocabulary we reuse. |
| Query-based compilers | Salsa / rustc incremental compilation | Real-world memoized queries with early cutoff. |
| Reactive notebooks | Observable, Pluto.jl | Cell re-execution on change; related use case. |

## 2. What is NOT new (do not claim)
- Dependency-graph change propagation, memoization, early cutoff, incremental recomputation after an input changes.
- The word "retroactive" applied to editing history (data-structure literature).

## 3. What we claim (state precisely, with scope)
| Claim | Strength | Evidence we must produce |
|---|---|---|
| C1: Stage insert/delete/modify are first-class compiler-level edits on a versioned IR of a pure dataflow language | Moderate (integration/design) | IR_SPEC + EDIT_SEMANTICS + implementation |
| C2: Record-level memoization lets inserted/deleted stages reuse downstream work via hash-based early cutoff | Moderate | Reuse % vs B1 (stage-level) on E-C/E-D/E-E |
| C3: Rebuild plan and provenance (`explain`) tie outputs to historical edits | Modest, practical | Plan format + explain golden tests |
| C4: Edit-then-propagate equals full rerun | Correctness claim | Differential tester, >= 1000 trials, plus proof sketch |

## 4. Honest novelty assessment
Plain framing ("incremental compiler with history edits") sits around 6.5-7/10 for an undergraduate project: the mechanics are well studied. A defensible way to argue higher, only if delivered and measured:
1. Fine-grained reuse across **structural** edits (insert/delete stage) with a measured gain over stage-level invalidation (C2).
2. A cost model that decides recompute vs patch (Phase 12.2).
3. Tree-based incremental reduce (Phase 12.1).
4. Control-flow edits (Phase 12.4).
5. A baseline comparison with an established system (B3).
6. A written correctness argument plus large-scale differential testing (C4).
Present the contribution as "a compiler-level edit model and IR for editable pipelines with measured fine-grained reuse", not as a new concept.

## 5. Claim-check before the review/viva
- Can I point to a sentence in the related work for every "unlike X" statement?
- Does each claim have a test or benchmark behind it?
- Have I listed limitations (EDIT_SEMANTICS section 11)?
