// Benchmark runner for docs/BENCHMARKS.md.
//
// Protocol: 3 warm-up runs, then median of 11 timed runs, min and max reported.
// A system whose single run takes longer than kSlowRunMs drops to 1 warm-up +
// kSlowRuns timed runs; the table marks those rows with the run count so the
// deviation is visible. Timing excludes parsing, compiling, data loading and
// graph cloning (setup happens outside the timed region).
//
// Usage: bench_runner [data_dir] [results_dir] [--machine "CPU, RAM, OS"] [--only W1]
//                     [--cpu N]  pin to logical CPU N (hybrid CPUs: pick a performance core)
#include "edit/edit.hpp"
#include "edit/source_rewrite.hpp"
#include "ir/builder.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "reuse/propagator.hpp"
#include "runtime/executor.hpp"
#include "sema/sema.hpp"
#include "verify/verifier.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace retroc;

static constexpr int kWarmup = 3;
static constexpr int kRuns = 11;
static constexpr double kSlowRunMs = 10000.0;
static constexpr int kSlowRuns = 5;

struct TimingStats {
  double median_ms = 0.0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  int runs = 0;
  bool skipped = false;
};

static TimingStats compute_stats(std::vector<double> times) {
  TimingStats stats;
  if (times.empty())
    return stats;
  std::sort(times.begin(), times.end());
  stats.min_ms = times.front();
  stats.max_ms = times.back();
  size_t mid = times.size() / 2;
  stats.median_ms = (times.size() % 2 == 0) ? (times[mid - 1] + times[mid]) / 2.0
                                            : times[mid];
  stats.runs = static_cast<int>(times.size());
  return stats;
}

// run_once performs its own setup and returns only the timed portion in ms.
static TimingStats measure_one(const std::function<double()> &run_once) {
  double first = run_once();
  int warmups = kWarmup;
  int runs = kRuns;
  if (first > kSlowRunMs) {
    warmups = 1;
    runs = kSlowRuns;
  }
  for (int i = 1; i < warmups; ++i)
    run_once();
  std::vector<double> times;
  for (int i = 0; i < runs; ++i)
    times.push_back(run_once());
  return compute_stats(std::move(times));
}

// Measures several systems round-robin (one run of each per round) so slow
// drift such as thermal throttling affects all of them alike and the speedup
// ratios stay comparable. Each system gets its own run count from its first
// warm-up, as in measure().
static std::vector<TimingStats>
measure_interleaved(const std::vector<std::function<double()>> &systems) {
  size_t k = systems.size();
  std::vector<int> warmups(k, kWarmup), runs(k, kRuns);
  for (size_t s = 0; s < k; ++s) {
    if (systems[s]() > kSlowRunMs) {
      warmups[s] = 1;
      runs[s] = kSlowRuns;
    }
  }
  for (int i = 1; i < kWarmup; ++i)
    for (size_t s = 0; s < k; ++s)
      if (i < warmups[s])
        systems[s]();
  std::vector<std::vector<double>> times(k);
  for (int i = 0; i < kRuns; ++i)
    for (size_t s = 0; s < k; ++s)
      if (i < runs[s])
        times[s].push_back(systems[s]());
  std::vector<TimingStats> stats;
  for (auto &t : times)
    stats.push_back(compute_stats(std::move(t)));
  return stats;
}

static double elapsed_ms(std::chrono::high_resolution_clock::time_point t0) {
  auto t1 = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

static std::variant<std::pair<Graph, TypedProgram>, CompileError>
build_ast_and_ir(const std::string &src) {
  Lexer lexer(src);
  auto lex_res = lexer.tokenize();
  if (std::holds_alternative<CompileError>(lex_res))
    return std::get<CompileError>(lex_res);

  Parser parser(std::get<std::vector<Token>>(lex_res));
  auto parse_res = parser.parse();
  if (std::holds_alternative<CompileError>(parse_res))
    return std::get<CompileError>(parse_res);

  SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
  auto sema_res = sema.analyze();
  if (std::holds_alternative<CompileError>(sema_res))
    return std::get<CompileError>(sema_res);

  auto typed_prog = std::get<TypedProgram>(std::move(sema_res));
  IRBuilder builder(typed_prog);
  Graph g = builder.build();
  return std::make_pair(std::move(g), std::move(typed_prog));
}

static Graph build_graph(const std::string &src) {
  auto res = build_ast_and_ir(src);
  if (std::holds_alternative<CompileError>(res)) {
    std::cerr << "compile error: " << std::get<CompileError>(res).message << "\n";
    std::exit(1);
  }
  return std::move(std::get<std::pair<Graph, TypedProgram>>(res).first);
}

static std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open())
    return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

struct Scenario {
  std::string workload; // e.g. "W1-100k"
  std::string rc_file;
  std::string edit;     // E-A .. E-F
  std::string detail;   // human-readable description of the edit
  EditScript script;
  bool skip_b1 = false; // estimated to take far too long; reported as skipped
};

struct BenchResultRow {
  std::string workload;
  std::string edit;
  std::string detail;
  TimingStats b0, b1, b2;
  double reuse_pct = 0.0;
  size_t ops_evaluated = 0;
  size_t ops_reused = 0;
  size_t full_rerun_ops = 0;
  bool verify_ok = false;
};

struct OverheadRow {
  std::string workload;
  TimingStats plain;
  TimingStats history;
  double overhead_pct = 0.0;
  double memo_mb = 0.0;
  double input_mb = 0.0;
};

// Applies every edit; when totals is given, sums op counts over the script.
static void apply_all(Propagator &prop, const EditScript &script, RebuildPlan *totals) {
  if (totals)
    *totals = RebuildPlan{};
  for (const auto &ed : script.edits) {
    std::variant<RebuildPlan, CompileError> pres;
    if (ed->kind() == EditKind::MODIFY)
      pres = prop.apply_modify(static_cast<const ModifyEdit &>(*ed));
    else if (ed->kind() == EditKind::INSERT)
      pres = prop.apply_insert(static_cast<const InsertEdit &>(*ed));
    else
      pres = prop.apply_delete(static_cast<const DeleteEdit &>(*ed));
    if (std::holds_alternative<CompileError>(pres)) {
      std::cerr << "edit rejected: " << std::get<CompileError>(pres).message << "\n";
      std::exit(2);
    }
    if (totals) {
      const auto &plan = std::get<RebuildPlan>(pres);
      totals->recomputed_ops += plan.recomputed_ops;
      totals->reused_ops += plan.reused_ops;
      totals->full_rerun_ops += plan.full_rerun_ops;
    }
  }
}

// Source text of the edited program, used for the B0 full rerun. Modify edits
// on CSV inputs are applied by editing the cached CSV collection instead.
static std::string edited_source(const std::string &original_source,
                                 const std::string &source_dir,
                                 const EditScript &script,
                                 std::vector<const ModifyEdit *> &csv_modifies) {
  std::string current = original_source;
  Graph graph = build_graph(original_source);
  GraphExecutor ex(graph, source_dir, true);
  ex.run();
  Propagator prop(graph, ex);
  for (const auto &ed : script.edits) {
    if (ed->kind() == EditKind::MODIFY) {
      const auto &me = static_cast<const ModifyEdit &>(*ed);
      const auto *in_n = graph.find_node_by_name(me.input_stage);
      if (in_n && !in_n->from_file.empty()) {
        csv_modifies.push_back(&me);
      } else {
        current = std::get<std::string>(apply_modify_to_source(current, me));
      }
    } else if (ed->kind() == EditKind::INSERT) {
      const auto &ie = static_cast<const InsertEdit &>(*ed);
      prop.apply_insert(ie);
      current = std::get<std::string>(apply_insert_to_source(current, ie, ie.new_stage_name));
    } else {
      const auto &de = static_cast<const DeleteEdit &>(*ed);
      const auto *dn = graph.find_node_by_name(de.stage_name);
      std::string in_name = dn && !dn->inputs.empty() ? graph.get_node(dn->inputs[0])->name : "";
      prop.apply_delete(de);
      current = std::get<std::string>(apply_delete_to_source(current, de, in_name));
    }
  }
  return current;
}

// Applies CSV-input modifies to a copy of the cached CSV collection so B0
// reruns the edited data, then restores the cache.
struct CsvPatch {
  std::string path;
  Collection original;
};

static std::vector<CsvPatch> patch_csv_cache(const Graph &graph, const std::string &source_dir,
                                             const std::vector<const ModifyEdit *> &mods) {
  std::vector<CsvPatch> patches;
  auto &cache = GraphExecutor::csv_cache();
  for (const auto *me : mods) {
    const auto *n = graph.find_node_by_name(me->input_stage);
    fs::path p = n->from_file;
    if (p.is_relative())
      p = fs::path(source_dir) / p;
    auto it = cache.find(p.string());
    if (it == cache.end())
      continue;
    bool saved = false;
    for (const auto &pp : patches)
      saved = saved || pp.path == p.string();
    if (!saved)
      patches.push_back({p.string(), it->second});
    auto &item = it->second[me->record_id];
    for (auto &rf : item.value.record_val) {
      if (rf.name == me->field_name)
        rf.value = std::make_shared<Value>(me->new_value);
    }
  }
  return patches;
}

static void restore_csv_cache(std::vector<CsvPatch> &patches) {
  auto &cache = GraphExecutor::csv_cache();
  for (auto &p : patches)
    cache[p.path] = std::move(p.original);
}

static BenchResultRow benchmark_scenario(const Scenario &sc, const std::string &source_dir) {
  BenchResultRow row;
  row.workload = sc.workload;
  row.edit = sc.edit;
  row.detail = sc.detail;
  std::cout << "Benchmarking " << sc.workload << " " << sc.edit << " (" << sc.detail << ")..."
            << std::endl;

  std::string original_source = read_file(sc.rc_file);

  // 1. Correctness first: propagated result must equal a full rerun
  VerificationResult v_res = verify_edits(original_source, sc.script, source_dir);
  row.verify_ok = v_res.ok;
  if (!v_res.ok) {
    std::cerr << "VERIFICATION FAILED for " << sc.workload << " " << sc.edit << ": "
              << v_res.mismatch_details << std::endl;
  }

  // 2. B1/B2 start from the same executed graph; only propagation is timed.
  // Built before the CSV cache is patched for B0, so it sees the original data.
  Graph base_graph = build_graph(original_source);
  GraphExecutor base_ex(base_graph, source_dir, true);
  base_ex.run();

  auto run_edits = [&](bool stage_invalidation, RebuildPlan *plan) {
    Graph g = base_graph.clone();
    GraphExecutor ex(g, base_ex);
    Propagator prop(g, ex, stage_invalidation);
    auto t0 = std::chrono::high_resolution_clock::now();
    apply_all(prop, sc.script, plan);
    return elapsed_ms(t0);
  };

  // 3. B0: full rerun of the edited program from an empty memo
  std::vector<const ModifyEdit *> csv_mods;
  std::string b0_source = edited_source(original_source, source_dir, sc.script, csv_mods);
  Graph b0_template = build_graph(b0_source);
  auto patches = patch_csv_cache(b0_template, source_dir, csv_mods);
  auto run_b0 = [&] {
    Graph g = b0_template.clone();
    GraphExecutor ex(g, source_dir, false);
    auto t0 = std::chrono::high_resolution_clock::now();
    ex.run();
    return elapsed_ms(t0);
  };

  RebuildPlan totals;
  std::vector<std::function<double()>> systems = {run_b0, [&] { return run_edits(false, &totals); }};
  if (!sc.skip_b1)
    systems.push_back([&] { return run_edits(true, nullptr); });
  auto stats = measure_interleaved(systems);
  restore_csv_cache(patches);

  row.b0 = stats[0];
  row.b2 = stats[1];
  if (sc.skip_b1) {
    row.b1.skipped = true;
  } else {
    row.b1 = stats[2];
  }
  row.ops_evaluated = totals.recomputed_ops;
  row.ops_reused = totals.reused_ops;
  row.full_rerun_ops = totals.full_rerun_ops;
  size_t ops = totals.recomputed_ops + totals.reused_ops;
  row.reuse_pct = ops > 0 ? 100.0 * static_cast<double>(totals.reused_ops) / static_cast<double>(ops) : 0.0;
  return row;
}

static OverheadRow measure_first_run_overhead(const std::string &workload,
                                              const std::string &rc_file,
                                              const std::string &source_dir) {
  OverheadRow row;
  row.workload = workload;
  std::cout << "Measuring first-run overhead for " << workload << "..." << std::endl;
  Graph graph = build_graph(read_file(rc_file));

  size_t input_bytes = 0;
  for (const auto &[nid, n] : graph.nodes) {
    if (n.kind == NodeKind::INPUT && !n.from_file.empty()) {
      fs::path p = n.from_file;
      if (p.is_relative())
        p = fs::path(source_dir) / p;
      if (fs::exists(p))
        input_bytes += fs::file_size(p);
    }
  }
  row.input_mb = static_cast<double>(input_bytes) / (1024.0 * 1024.0);

  auto run = [&](bool memo) {
    Graph g = graph.clone();
    GraphExecutor ex(g, source_dir, memo);
    auto t0 = std::chrono::high_resolution_clock::now();
    ex.run();
    double ms = elapsed_ms(t0);
    if (memo)
      row.memo_mb = static_cast<double>(ex.total_memo_bytes()) / (1024.0 * 1024.0);
    return ms;
  };
  row.plain = measure_one([&] { return run(false); });
  row.history = measure_one([&] { return run(true); });
  row.overhead_pct = (row.history.median_ms - row.plain.median_ms) / row.plain.median_ms * 100.0;
  return row;
}

// --- Scenario construction --------------------------------------------------

static std::shared_ptr<ModifyEdit> modify_int(const std::string &stage, RecordId id,
                                              const std::string &field, int64_t v) {
  auto m = std::make_shared<ModifyEdit>();
  m->input_stage = stage;
  m->record_id = id;
  m->field_name = field;
  m->new_value = Value::make_int(v);
  m->raw_literal_text = std::to_string(v);
  return m;
}

static std::shared_ptr<InsertEdit> insert(const std::string &before, const std::string &name,
                                          const std::string &builtin, const std::string &lambda) {
  auto ins = std::make_shared<InsertEdit>();
  ins->before_stage = before;
  ins->new_stage_name = name;
  ins->builtin_name = builtin;
  ins->raw_lambda_text = lambda;
  return ins;
}

static std::shared_ptr<DeleteEdit> remove_stage(const std::string &name) {
  auto del = std::make_shared<DeleteEdit>();
  del->stage_name = name;
  return del;
}

static Scenario one_edit(const std::string &workload, const std::string &rc, const std::string &edit,
                         const std::string &detail, std::shared_ptr<Edit> e) {
  Scenario sc{workload, rc, edit, detail, {}, false};
  sc.script.edits.push_back(std::move(e));
  return sc;
}

// E-B: modify 1% of records, spread evenly, capped at kMaxBatch edits because
// edits apply one at a time and B1 recomputes the whole pipeline per edit.
static constexpr size_t kMaxBatch = 100;

static Scenario batch_modify(const std::string &workload, const std::string &rc,
                             const std::string &stage, const std::string &field, size_t n,
                             int64_t value) {
  size_t count = std::max<size_t>(1, std::min(n / 100, kMaxBatch));
  std::ostringstream detail;
  detail << "modify " << count << " records (" << std::fixed << std::setprecision(2)
         << (100.0 * static_cast<double>(count) / static_cast<double>(n)) << "%)";
  Scenario sc{workload, rc, "E-B", detail.str(), {}, false};
  for (size_t i = 0; i < count; ++i) {
    sc.script.edits.push_back(modify_int(stage, i * (n / count), field, value));
  }
  return sc;
}

static std::vector<Scenario> w1_scenarios(const std::string &data_dir, size_t n,
                                          const std::string &label) {
  std::string rc = (fs::path(data_dir) / ("w1_" + std::to_string(n) + ".rc")).string();
  std::vector<Scenario> s;
  s.push_back(one_edit(label, rc, "E-A", "modify 1 record", modify_int("raw", 500, "temp", 85)));
  s.push_back(batch_modify(label, rc, "raw", "temp", n, 75));
  s.push_back(one_edit(label, rc, "E-C", "insert map near start",
                       insert("normalized", "shifted", "map", "r => { id: r.id, temp: r.temp + 1 }")));
  s.push_back(one_edit(label, rc, "E-D", "insert map near end",
                       insert("total", "bumped", "map", "r => { id: r.id, temp: r.temp + 1 }")));
  s.push_back(one_edit(label, rc, "E-E", "delete middle stage", remove_stage("scaled")));
  s.push_back(one_edit(label, rc, "E-F", "insert identity map near start",
                       insert("normalized", "same", "map", "r => r")));
  return s;
}

static std::vector<Scenario> w2_scenarios(const std::string &data_dir, size_t n_orders) {
  std::string rc = (fs::path(data_dir) / "w2.rc").string();
  std::vector<Scenario> s;
  s.push_back(one_edit("W2", rc, "E-A", "modify 1 order", modify_int("orders", 100, "amt", 120)));
  s.push_back(batch_modify("W2", rc, "orders", "amt", n_orders, 99));
  s.push_back(one_edit("W2", rc, "E-C", "insert map before join",
                       insert("joined", "all_active", "map", "u => { uid: u.uid, active: true }")));
  s.push_back(one_edit("W2", rc, "E-D", "insert map near end",
                       insert("total_amt", "bumped", "map", "a => a + 1")));
  s.push_back(one_edit("W2", rc, "E-E", "delete middle stage", remove_stage("filtered")));
  s.push_back(one_edit("W2", rc, "E-F", "insert identity map",
                       insert("filtered", "same", "map", "a => a")));
  return s;
}

static std::vector<Scenario> w3_scenarios(const std::string &data_dir, size_t n) {
  std::string rc = (fs::path(data_dir) / "w3.rc").string();
  std::vector<Scenario> s;
  s.push_back(one_edit("W3", rc, "E-A", "modify 1 record", modify_int("raw", 500, "val", 50)));
  s.push_back(batch_modify("W3", rc, "raw", "val", n, 42));
  s.push_back(one_edit("W3", rc, "E-C", "insert map near start",
                       insert("s2", "boost", "map", "x => { id: x.id, val: x.val + 1 }")));
  s.push_back(one_edit("W3", rc, "E-D", "insert map near end",
                       insert("total", "bumped", "map", "x => { id: x.id, val: x.val + 1 }")));
  s.push_back(one_edit("W3", rc, "E-E", "delete middle stage", remove_stage("s5")));
  s.push_back(one_edit("W3", rc, "E-F", "insert identity map near start",
                       insert("s2", "same", "map", "x => x")));
  return s;
}

static std::vector<Scenario> w4_scenarios(const std::string &data_dir, size_t n) {
  std::string rc = (fs::path(data_dir) / "w4.rc").string();
  std::vector<Scenario> s;
  s.push_back(one_edit("W4", rc, "E-A", "modify 1 record", modify_int("raw", n / 2, "val", 999)));
  s.push_back(batch_modify("W4", rc, "raw", "val", n, 7));
  s.push_back(one_edit("W4", rc, "E-C", "insert map near start",
                       insert("mapped", "shifted", "map", "x => { id: x.id, val: x.val + 1 }")));
  s.push_back(one_edit("W4", rc, "E-D", "insert map near end",
                       insert("total", "bumped", "map", "x => x + 1")));
  // E-E: W4 has no deletable middle stage (deleting `mapped` changes the type)
  s.push_back(one_edit("W4", rc, "E-F", "insert identity map near end",
                       insert("total", "same", "map", "x => x")));
  return s;
}

// --- Output ------------------------------------------------------------------

static std::string fmt_ms(const TimingStats &t) {
  if (t.skipped)
    return "skipped";
  std::ostringstream o;
  o << std::fixed << std::setprecision(1) << t.median_ms;
  if (t.runs != kRuns)
    o << " (n=" << t.runs << ")";
  return o.str();
}

static std::string fmt_ratio(const TimingStats &a, const TimingStats &b) {
  if (a.skipped || b.skipped || b.median_ms <= 0.0)
    return "-";
  std::ostringstream o;
  o << std::fixed << std::setprecision(2) << (a.median_ms / b.median_ms) << "x";
  return o.str();
}

static void json_timing(std::ostream &jf, const char *name, const TimingStats &t, bool last) {
  jf << "      \"" << name << "\": ";
  if (t.skipped) {
    jf << "null";
  } else {
    jf << "{\"median_ms\": " << t.median_ms << ", \"min_ms\": " << t.min_ms
       << ", \"max_ms\": " << t.max_ms << ", \"runs\": " << t.runs << "}";
  }
  jf << (last ? "\n" : ",\n");
}

static std::string json_escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out;
}

int main(int argc, char **argv) {
  std::string data_dir = "bench/data";
  std::string results_dir = "bench/results";
  std::string machine = "unspecified";
  std::string only;
  int cpu = -1;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--machine" && i + 1 < argc) {
      machine = argv[++i];
    } else if (arg == "--cpu" && i + 1 < argc) {
      cpu = std::stoi(argv[++i]);
    } else if (arg == "--only" && i + 1 < argc) {
      only = argv[++i];
    } else {
      positional.push_back(arg);
    }
  }
  if (positional.size() > 0)
    data_dir = positional[0];
  if (positional.size() > 1)
    results_dir = positional[1];
  fs::create_directories(results_dir);
  if (cpu >= 0) {
#ifdef _WIN32
    // Hybrid CPUs mix fast and slow cores; letting the OS migrate the runner
    // between them adds large timing noise.
    SetProcessAffinityMask(GetCurrentProcess(), DWORD_PTR(1) << cpu);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
    machine += " [pinned to logical CPU " + std::to_string(cpu) + "]";
  }

  std::vector<Scenario> scenarios;
  for (auto &s : w1_scenarios(data_dir, 10000, "W1-10k"))
    scenarios.push_back(std::move(s));
  for (auto &s : w1_scenarios(data_dir, 100000, "W1-100k"))
    scenarios.push_back(std::move(s));
  for (auto &s : w1_scenarios(data_dir, 1000000, "W1-1M")) {
    // 100 sequential edits, each a full-pipeline recompute over 1M records
    // under B1: well over an hour per measurement on this machine.
    if (s.edit == "E-B")
      s.skip_b1 = true;
    scenarios.push_back(std::move(s));
  }
  for (auto &s : w2_scenarios(data_dir, 2000))
    scenarios.push_back(std::move(s));
  for (auto &s : w3_scenarios(data_dir, 100000))
    scenarios.push_back(std::move(s));
  for (auto &s : w4_scenarios(data_dir, 1000000))
    scenarios.push_back(std::move(s));

  std::vector<BenchResultRow> rows;
  auto started = std::chrono::high_resolution_clock::now();
  for (const auto &sc : scenarios) {
    if (!only.empty() && sc.workload.rfind(only, 0) != 0)
      continue;
    rows.push_back(benchmark_scenario(sc, data_dir));
  }

  std::vector<OverheadRow> overheads;
  if (only.empty() || std::string("W1").rfind(only, 0) == 0) {
    overheads.push_back(measure_first_run_overhead(
        "W1-100k", (fs::path(data_dir) / "w1_100000.rc").string(), data_dir));
  }
  double total_s = elapsed_ms(started) / 1000.0;

  std::cout << "\nProtocol: " << kWarmup << " warm-up + median of " << kRuns
            << " timed runs (n=" << kSlowRuns << " after 1 warm-up when a run exceeds "
            << kSlowRunMs / 1000.0 << " s)\n";
  std::cout << "Op counts and reuse are summed over all edits of the scenario (B2).\n";
  std::cout << "| Workload | Edit | Detail | B0 ms | B1 ms | B2 ms | B0/B2 | B1/B2 | "
               "Recomputed ops | Reused ops | Reuse % | Verify |\n";
  std::cout << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  for (const auto &r : rows) {
    std::cout << "| " << r.workload << " | " << r.edit << " | " << r.detail << " | "
              << fmt_ms(r.b0) << " | " << fmt_ms(r.b1) << " | " << fmt_ms(r.b2) << " | "
              << fmt_ratio(r.b0, r.b2) << " | " << fmt_ratio(r.b1, r.b2) << " | "
              << r.ops_evaluated << " | " << r.ops_reused << " | " << std::fixed
              << std::setprecision(1) << r.reuse_pct << "% | "
              << (r.verify_ok ? "PASS" : "FAIL") << " |\n";
  }

  std::cout << "\n| Workload | First run plain ms | First run with history ms | Overhead | "
               "Memo MB | Input MB |\n";
  std::cout << "|---|---|---|---|---|---|\n";
  for (const auto &o : overheads) {
    std::cout << "| " << o.workload << " | " << fmt_ms(o.plain) << " | " << fmt_ms(o.history)
              << " | " << std::fixed << std::setprecision(1) << o.overhead_pct << "% | "
              << std::setprecision(2) << o.memo_mb << " | " << o.input_mb << " |\n";
  }
  std::cout << "\nTotal benchmark time: " << std::fixed << std::setprecision(0) << total_s
            << " s\n";

  std::string json_path = (fs::path(results_dir) / "benchmark_results.json").string();
  std::ofstream jf(json_path);
  jf << "{\n";
  jf << "  \"system\": {\n";
  jf << "    \"machine\": \"" << json_escape(machine) << "\",\n";
  jf << "    \"compiler\": \"GCC " << __VERSION__ << "\",\n";
  jf << "    \"flags\": \"-O2 -DNDEBUG\"\n";
  jf << "  },\n";
  jf << "  \"protocol\": {\"warmup\": " << kWarmup << ", \"runs\": " << kRuns
     << ", \"slow_run_ms\": " << kSlowRunMs << ", \"slow_runs\": " << kSlowRuns << "},\n";
  jf << "  \"scenarios\": [\n";
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto &r = rows[i];
    jf << "    {\n";
    jf << "      \"workload\": \"" << r.workload << "\",\n";
    jf << "      \"edit\": \"" << r.edit << "\",\n";
    jf << "      \"detail\": \"" << json_escape(r.detail) << "\",\n";
    json_timing(jf, "b0", r.b0, false);
    json_timing(jf, "b1", r.b1, false);
    json_timing(jf, "b2", r.b2, false);
    jf << "      \"ops_recomputed\": " << r.ops_evaluated << ",\n";
    jf << "      \"ops_reused\": " << r.ops_reused << ",\n";
    jf << "      \"full_rerun_ops\": " << r.full_rerun_ops << ",\n";
    jf << "      \"reuse_percent\": " << r.reuse_pct << ",\n";
    jf << "      \"verify_ok\": " << (r.verify_ok ? "true" : "false") << "\n";
    jf << "    }" << (i + 1 < rows.size() ? "," : "") << "\n";
  }
  jf << "  ],\n";
  jf << "  \"first_run_overhead\": [\n";
  for (size_t i = 0; i < overheads.size(); ++i) {
    const auto &o = overheads[i];
    jf << "    {\n";
    jf << "      \"workload\": \"" << o.workload << "\",\n";
    json_timing(jf, "plain", o.plain, false);
    json_timing(jf, "with_history", o.history, false);
    jf << "      \"overhead_percent\": " << o.overhead_pct << ",\n";
    jf << "      \"memo_mb\": " << o.memo_mb << ",\n";
    jf << "      \"input_mb\": " << o.input_mb << "\n";
    jf << "    }" << (i + 1 < overheads.size() ? "," : "") << "\n";
  }
  jf << "  ]\n";
  jf << "}\n";
  std::cout << "Results saved to: " << json_path << std::endl;
  return 0;
}
