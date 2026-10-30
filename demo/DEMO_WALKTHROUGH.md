# RetroC: Pitch and Live Demo Walkthrough

This is the run sheet for presenting RetroC with the pitch deck. The full word-for-word script for every slide is in the deck's speaker notes; this file is the plan, the demo commands and the Q&A.

All numbers below come from `bench/published/2026-10-28/` and the test run of 2026-10-28 (see `MEMORY.md`). Do not quote older numbers (10.9×, 1,000 trials, 1.15× overhead): they are out of date.

---

## 1. Before the session (10 minutes)

1. Build: `cmake --build build` (with `C:\msys64\ucrt64\bin` on PATH).
2. Dry run once: `powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1 -Auto`.
3. Terminal: font size 18+ and a dark theme. Put the terminal and slides on separate windows and switch with Alt+Tab.
4. Record a backup video of the demo (Win+Alt+R). If the live demo fails, use the "recorded output" slide or the video.
5. Close other heavy apps if you plan to run the live benchmark.

---

## 2. Running order (about 14 minutes)

| Part | Slides | Time | Suggested speaker |
|---|---|---|---|
| Problem and idea | 1-4: cover, problem, existing tools, our idea | 2.5 min | Swaraj |
| How it works | 5-10: language, flow, IR, edit propagation, record-level reuse, hash and tree_reduce | 4.5 min | Johan |
| Live demo | 11 (12 is the backup) | 4 min | Sudeep |
| Results | 13-16: setup, headline, wins and losses, correctness | 2.5 min | Swaraj |
| Close | 17-19: contributions, limitations, questions | 1 min | Johan |

Change the speakers to suit your team; the point is that each person owns one block.

---

## 3. The live demo

Switch to the terminal on slide 11 and run:

```powershell
powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1
```

Press Enter between steps. Optional flags: `-Auto` (no pauses), `-Bench` (run the live benchmark without asking).

| Step | Command | What to point at |
|---|---|---|
| 1 | `retroc dump-ir` then `retroc run demo\pipeline.rc` | One IR node per stage. First result: `avg_temp_f = 73.17` |
| 2 | `retroc edit demo\pipeline.rc demo\edits.rcs` | Edit #1: only the filter runs, `1 / 5`, `Reuse: 80.0%`, `Updated outputs: (none)`: early cutoff. Edit #2: one record goes through the map; the plain `reduce` re-adds all 5. Edits #3 and #4 insert and delete a stage. |
| 3 | `retroc edit demo\tree_demo.rc demo\tree_demo.rcs` | `tree_reduce`: `evaluated 3 / 8`: one path to the root. |
| 4 | `retroc verify demo\pipeline.rc demo\edits.rcs` | `Verification OK: all outputs match full rerun exactly.` |
| 5 | `retroc explain ... avg_temp_f` | Edit #1 is not listed, because early cutoff proved it had no effect. |
| 6 (optional) | `bench_runner --only W1-10k` (about 50 s) | B0/B2 for "modify 1 record" is about 9.6×. Every row says PASS. |

If something fails, don't debug it live: go to slide 12 (the recorded output) and carry on.

---

## 4. Numbers you may quote

| Claim | Value | Source |
|---|---|---|
| One-record edit, W1 100k | 9.87× faster than a rerun (521 ms → 53 ms) | full protocol, B0/B2 |
| One-record edit, W1 1M | 10.05× (5.47 s → 0.54 s) | full protocol |
| One-record edit, W3 (12 stages) | 22.39× | full protocol |
| Before / after removing the rescan | 1.28× → 9.87× (W1 100k) | paper Fig. 6 |
| Losses | batches 0.03-0.23×, W2 joins 0.02-0.40×, insert near start 0.71-1.06× | full protocol |
| vs. stage-level invalidation (B1) | 8.8-20× on one-record edits for W1 and W3; structural edits 0.64-0.98× of B1 | full protocol |
| First run with memo tables | +42.7% | W1 100k |
| Memo memory | 36.18 MB for 1.73 MB of input | W1 100k |
| Tests | 52 cases, 687 assertions, 8/8 suites, 10,000 differential trials, planted bug caught at trial 16 | 2026-10-28 run |

---

## 5. Likely questions

**How is this different from Differential Dataflow?**
Differential Dataflow propagates record changes through a fixed dataflow graph. RetroC also inserts and deletes stages in the graph, as type-checked atomic edits, and checks every result against a full rerun. We have not yet benchmarked against it directly; that is listed as future work.

**Why are batches slower than a rerun?**
Each edit in a batch is propagated on its own, so 100 edits do 100 passes. Propagating a batch in one pass, plus a cost model that falls back to a rerun, is the first next step.

**Why are joins slow?**
A changed key forces matching pairs to be recomputed, and our join has no incremental index yet. An incremental hash join is future work.

**What is the memory overhead?**
High: 36.18 MB of memo tables for 1.73 MB of input, because every stage keeps the input hash and output of every record. The first run is 42.7% slower because it fills them. Compacting the memo is future work.

**How do you stop an insert from creating a cycle?**
After an insert, the graph is re-sorted with Kahn's algorithm. If not every node can be ordered, there is a cycle; the edit is rejected with a coded error and fully rolled back.

**Why is early cutoff safe?**
Lambdas are pure: no mutation, I/O, randomness or recursion. So the same input always gives the same output, and if a stage's output hash is unchanged, everything after it would compute the same values. Hash collisions are bounded by q²/2⁶⁵ after q hashes.

**Are there known bugs?**
Yes, one: a lambda that reads another stage (for example `map(xs, x => x + total)`) is not refreshed when `total` changes. Fixing it needs a change to the IR specification, which we have proposed.
