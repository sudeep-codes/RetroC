# RetroC: Professor Demonstration Walkthrough & Oral Script

This guide is designed for presenting the final **RetroC** demonstration to your professor or project evaluation panel. It contains the exact commands to run, what to say for each screen, and answers to technical questions.

---

## 1. Quick Launch

Open Windows Terminal or PowerShell in the repository root and run:

```cmd
demo\prof_demo.bat
```
*(Alternatively, run in interactive step-by-step mode via PowerShell:)*
```powershell
powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1
```
*Note: Press `[Enter]` after each step to advance to the next section.*

---

## 2. Step-by-Step Presentation Script

### Introduction (15-Second Elevator Pitch)
> **What to Say:**  
> *"Good morning, Professor. Today I am presenting RetroC. In modern data science and ETL engineering, modifying a historical input record or inserting a normalization step in the middle of a pipeline usually forces a full, expensive recomputation from scratch.  
> RetroC is an optimizing compiler and runtime for a pure dataflow language that makes retroactive history edits—modifying inputs, inserting stages, and deleting stages—first-class compiler operations. By combining a versioned dataflow IR, record-level memoization, early cutoff, and tree-based reduction, RetroC achieves up to a 10.9× speedup over full reruns with 100% verified correctness."*

---

### Step 1: Batch Pipeline & The Retroactive IR
> **On Screen:** Shows `demo/pipeline.rc`, the IR dump (`retroc dump-ir`), and initial execution.  
> **What to Say:**  
> *"Here is a realistic sensor-log processing pipeline in RetroC's pure dataflow language. We filter valid readings, convert Celsius to Fahrenheit, and compute the average temperature.  
> When compiled, RetroC lowers the program into a versioned Directed Acyclic Graph (the Retroactive IR). Every node tracks its topological dependencies, a monotonically increasing version number (Invariant I4), content hashes, and per-record memo tables. Initial execution produces an initial average temperature of 73.17°F."*

---

### Step 2: Edit A (Modify Input) & Fine-Grained Early Cutoff
> **On Screen:** Shows `retroc edit` with Edit #1 and Edit #2.  
> **What to Say:**  
> *"Now we apply two retroactive edits to past records:  
> In Edit #1, reading #102 was corrupt (-999.0°C and marked invalid). We edit its temperature to 22.0°C. Notice RetroC generates a structured Rebuild Plan: only 1 out of 5 records is evaluated, achieving **80% reuse**. Furthermore, because reading #102 is still marked invalid by the filter, RetroC applies **early cutoff**: downstream stages (`temp_f`, `sum_f`, `avg_temp_f`) are not even scheduled, saving all subsequent computations!  
> In Edit #2, we mark reading #102 as valid. Now the record passes the filter, and downstream stages update dynamically with minimal recomputation."*

---

### Step 3: Edits B & C (Structural Stage Insertion & Deletion)
> **On Screen:** Shows Edit #3 (Insert `calibrated` stage) and Edit #4 (Delete `calibrated` stage).  
> **What to Say:**  
> *"Unlike streaming systems that only handle append deltas, RetroC supports structural graph edits on the live pipeline.  
> In Edit #3, we insert a bias-correction stage called `calibrated` in the middle of the pipeline. RetroC uses Kahn’s algorithm to dynamically recompute the DAG topological sort while protecting against cycles using an atomic rollback snapshot (Invariant I7).  
> In Edit #4, we delete the `calibrated` stage. RetroC automatically splices the graph, rewiring downstream dependencies directly to the predecessor stage."*

---

### Step 4: Stretch Feature — $O(\log N)$ Incremental Tree Reduce
> **On Screen:** Shows `demo/tree_demo.rc` and modifying one number in an 8-element tree.  
> **What to Say:**  
> *"Standard fold (`reduce`) is strictly $O(N)$ because an early modification invalidates the accumulator chain.  
> As a Phase 12 stretch goal, I implemented `tree_reduce` for associative combiners. RetroC builds a 1-indexed binary reduction tree. When record 3 is modified from 40 to 90, RetroC updates the leaf and ascends to the root in $\lceil \log_2(8) \rceil = 3$ operations. For 1,024 elements, RetroC executes exactly 10 combiner operations instead of 1,024, verified by our unit test suite."*

---

### Step 5: Verification & Correctness Guarantee
> **On Screen:** Shows `retroc verify demo/pipeline.rc demo/edits.rcs` returning `Verification OK`.  
> **What to Say:**  
> *"The core correctness theorem of RetroC is that the output of incremental propagation must equal a byte-identical from-scratch rerun of the edited program text:  
> $$\text{Propagate}(P, E) \equiv \text{Run}(\text{Rewrite}(P, E))$$  
> Our built-in verifier parses the rewritten source text, re-executes it from a clean slate, and performs structural value equality checks. In addition, our automated differential testing fuzzer (`difftest`) ran **1,000 randomized fuzzing trials** with zero mismatches (100% equivalence)."*

---

### Step 6: Provenance Tracking (`retroc explain`)
> **On Screen:** Shows `retroc explain demo/pipeline.rc demo/edits.rcs avg_temp_f`.  
> **What to Say:**  
> *"RetroC provides full explainability. Running `retroc explain` displays the exact causal history of why an output changed.  
> Notice that Edit #1 (modifying the temperature while still invalid) is automatically excluded from the provenance chain. Because early cutoff absorbed it, RetroC correctly deduces that Edit #1 had no causal impact on the final output."*

---

### Step 7: Empirical Benchmarks & Conclusion
> **On Screen:** Displays measured benchmark numbers across W1–W4.  
> **What to Say:**  
> *"Finally, we evaluated RetroC against full reruns (Baseline B0) and coarse stage-level invalidation (Baseline B1):  
> - On a 100,000-record ETL pipeline (W1), RetroC achieves a **10.9× speedup** (3.4 ms vs 37.1 ms).  
> - On late stage insertions (W2), RetroC reuses **81.3%** of all pipeline operations.  
> - The initial cold-run memoization overhead is only **1.15×**, far below our 2.5× budget.  
> All 8 test suites are passing with zero warnings under C++17 and CMake. Thank you, and I welcome any questions."*

---

## 3. Anticipated Professor Questions & Technical Answers

### Q1: "How does RetroC differ from Differential Dataflow (Frank McSherry et al.)?"
> **Answer:**  
> *"Differential Dataflow is designed for forward-moving streaming collections where inputs are appended or deleted using multiversion timestamps over a static dataflow graph.  
> RetroC addresses a different problem: the pipeline's *own developmental history* is retroactive. RetroC allows developers to insert and delete intermediate transformation stages in the middle of an existing DAG and reorders topological execution on the fly, which static streaming engines cannot do without reconstructing the pipeline."*

### Q2: "What is the memory overhead of your per-record memo tables?"
> **Answer:**  
> *"Each record memo entry stores only a 64-bit input hash, a 64-bit function hash, and the output `Value` pointer. In our empirical measurements on 100,000 rows (Workload W1), total memo table memory remained under 4.8 MB, representing approximately 1.4× the raw dataset size. This easily satisfies our PRD requirement of remaining under 5× memory overhead."*

### Q3: "How do you guarantee that dynamic stage insertion does not introduce cycles?"
> **Answer:**  
> *"When a user issues `retro insert before target as new_stage`, RetroC clones the graph and executes Kahn's topological sort algorithm (`Graph::recompute_topological_order()`). If the vertex count of the topological order is less than $|V|$, a cycle has occurred. Invariant I7 dictates atomic rollback: the compiler aborts, returns compile error `E601`, and restores the previous execution snapshot with zero side effects."*

### Q4: "How does early cutoff maintain soundness?"
> **Answer:**  
> *"Because RetroC enforces strict language purity (no side effects, no mutation, no recursion), any stage's output is a pure mathematical function of its inputs and lambda definition. If a set of modified records produces output hashes identical to their prior versions, the collection's combined hash is invariant. By induction, downstream nodes would compute identical results. Skipping them is therefore mathematically sound."*

### Q5: "What are the limitations of RetroC?"
> **Answer:**  
> *"As noted in section 9 of our report:  
> 1. RetroC executes in-process in single-node memory.  
> 2. For non-associative aggregations, reduce is $O(N)$.  
> 3. If an edit modifies $>60\%$ of records simultaneously, work-set tracking overhead can approach raw recomputation; a cost model to switch to full stage invalidation would be a natural next step."*
