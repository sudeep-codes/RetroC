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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace retroc;

struct TimingStats {
  double median_ms = 0.0;
  double min_ms = 0.0;
  double max_ms = 0.0;
};

TimingStats compute_stats(std::vector<double> &times) {
  if (times.empty())
    return {0.0, 0.0, 0.0};
  std::sort(times.begin(), times.end());
  TimingStats stats;
  stats.min_ms = times.front();
  stats.max_ms = times.back();
  size_t mid = times.size() / 2;
  if (times.size() % 2 == 0) {
    stats.median_ms = (times[mid - 1] + times[mid]) / 2.0;
  } else {
    stats.median_ms = times[mid];
  }
  return stats;
}

std::variant<std::pair<Graph, TypedProgram>, CompileError>
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

std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open())
    return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

struct BenchResultRow {
  std::string workload;
  std::string edit;
  double b0_ms = 0.0;
  double b1_ms = 0.0;
  double b2_ms = 0.0;
  double speedup_b0_b2 = 0.0;
  double speedup_b1_b2 = 0.0;
  double reuse_pct = 0.0;
  bool verify_ok = false;
  size_t ops_evaluated = 0;
  size_t ops_reused = 0;
};

struct OverheadRow {
  std::string workload;
  double first_run_plain_ms = 0.0;
  double first_run_history_ms = 0.0;
  double overhead_pct = 0.0;
  double memo_mb = 0.0;
  double input_mb = 0.0;
};

void run_first_run_overhead(const std::string &source_path,
                            const std::string &source_dir, OverheadRow &row) {
  std::string src = read_file(source_path);
  if (src.empty()) {
    std::cerr << "Failed to read " << source_path << std::endl;
    return;
  }

  // Input CSV file size
  auto build_res = build_ast_and_ir(src);
  if (std::holds_alternative<CompileError>(build_res))
    return;
  auto [graph, typed] =
      std::move(std::get<std::pair<Graph, TypedProgram>>(build_res));

  size_t input_file_bytes = 0;
  for (const auto &[nid, n] : graph.nodes) {
    if (n.kind == NodeKind::INPUT && !n.from_file.empty()) {
      fs::path p = n.from_file;
      if (p.is_relative())
        p = fs::path(source_dir) / p;
      if (fs::exists(p)) {
        input_file_bytes += fs::file_size(p);
      }
    }
  }
  row.input_mb = static_cast<double>(input_file_bytes) / (1024.0 * 1024.0);

  // Warmup 1
  for (int i = 0; i < 1; ++i) {
    Graph g = graph.clone();
    GraphExecutor ex(g, source_dir, false);
    ex.run();
  }

  // Measure Plain (3 runs)
  std::vector<double> plain_times;
  for (int i = 0; i < 3; ++i) {
    Graph g = graph.clone();
    GraphExecutor ex(g, source_dir, false);
    auto t0 = std::chrono::high_resolution_clock::now();
    ex.run();
    auto t1 = std::chrono::high_resolution_clock::now();
    plain_times.push_back(
        std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  row.first_run_plain_ms = compute_stats(plain_times).median_ms;

  // Measure With History/Memo (3 runs)
  std::vector<double> history_times;
  size_t memo_bytes = 0;
  for (int i = 0; i < 3; ++i) {
    Graph g = graph.clone();
    GraphExecutor ex(g, source_dir, true);
    auto t0 = std::chrono::high_resolution_clock::now();
    ex.run();
    auto t1 = std::chrono::high_resolution_clock::now();
    history_times.push_back(
        std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (i == 0) {
      memo_bytes = ex.total_memo_bytes();
    }
  }
  row.first_run_history_ms = compute_stats(history_times).median_ms;
  row.overhead_pct = ((row.first_run_history_ms - row.first_run_plain_ms) /
                      row.first_run_plain_ms) *
                     100.0;
  row.memo_mb = static_cast<double>(memo_bytes) / (1024.0 * 1024.0);
}

BenchResultRow benchmark_scenario(const std::string &workload_name,
                                  const std::string &source_path,
                                  const std::string &source_dir,
                                  const std::string &edit_name,
                                  const EditScript &script) {
  BenchResultRow row;
  row.workload = workload_name;
  row.edit = edit_name;

  std::string original_source = read_file(source_path);

  // 1. Verify correctness first
  VerificationResult v_res = verify_edits(original_source, script, source_dir);
  row.verify_ok = v_res.ok;
  if (!v_res.ok) {
    std::cerr << "VERIFICATION FAILED for " << workload_name << " " << edit_name
              << ": " << v_res.mismatch_details << std::endl;
  }

  // 2. Prepare rewritten source for B0
  std::string current_source = original_source;
  {
    auto build_res = build_ast_and_ir(original_source);
    auto [graph, typed] =
        std::move(std::get<std::pair<Graph, TypedProgram>>(build_res));
    GraphExecutor ex(graph, source_dir, true);
    ex.run();
    Propagator prop(graph, ex);
    for (const auto &ed : script.edits) {
      if (ed->kind() == EditKind::MODIFY) {
        const auto &me = static_cast<const ModifyEdit &>(*ed);
        const auto *in_n = graph.find_node_by_name(me.input_stage);
        if (in_n && !in_n->from_file.empty()) {
          // Handled via CSV rewrite
        } else {
          current_source =
              std::get<std::string>(apply_modify_to_source(current_source, me));
        }
      } else if (ed->kind() == EditKind::INSERT) {
        auto pres = prop.apply_insert(static_cast<const InsertEdit &>(*ed));
        std::string actual_name =
            static_cast<const InsertEdit &>(*ed).new_stage_name;
        if (actual_name.empty()) {
          for (const auto &sp : std::get<RebuildPlan>(pres).stages) {
            if (sp.status == StagePlanStatus::NEW)
              actual_name = sp.stage_name;
          }
        }
        current_source = std::get<std::string>(apply_insert_to_source(
            current_source, static_cast<const InsertEdit &>(*ed), actual_name));
      } else if (ed->kind() == EditKind::DELETE) {
        const auto *dn = graph.find_node_by_name(
            static_cast<const DeleteEdit &>(*ed).stage_name);
        std::string in_name = dn && !dn->inputs.empty()
                                  ? graph.get_node(dn->inputs[0])->name
                                  : "";
        prop.apply_delete(static_cast<const DeleteEdit &>(*ed));
        current_source = std::get<std::string>(apply_delete_to_source(
            current_source, static_cast<const DeleteEdit &>(*ed), in_name));
      }
    }
  }

  auto b0_build_res = build_ast_and_ir(current_source);
  auto [b0_graph_template, b0_typed] =
      std::move(std::get<std::pair<Graph, TypedProgram>>(b0_build_res));

  // Warm-up 1 run for B0
  for (int i = 0; i < 1; ++i) {
    Graph g = b0_graph_template.clone();
    GraphExecutor ex(g, source_dir, false);
    ex.run();
  }

  // Measure B0 (3 runs)
  std::vector<double> b0_times;
  for (int i = 0; i < 3; ++i) {
    Graph g = b0_graph_template.clone();
    GraphExecutor ex(g, source_dir, false);
    auto t0 = std::chrono::high_resolution_clock::now();
    ex.run();
    auto t1 = std::chrono::high_resolution_clock::now();
    b0_times.push_back(
        std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  row.b0_ms = compute_stats(b0_times).median_ms;

  // Prepare baseline pre-executed graph and executor to isolate propagation
  // timing
  auto b_res = build_ast_and_ir(original_source);
  auto [base_graph, base_typed] =
      std::move(std::get<std::pair<Graph, TypedProgram>>(b_res));
  GraphExecutor base_ex(base_graph, source_dir, true);
  base_ex.run();

  auto setup_from_base = [&](bool stage_invalidation) {
    auto g_ptr = std::make_unique<Graph>(base_graph.clone());
    auto ex_ptr = std::make_unique<GraphExecutor>(*g_ptr, base_ex);
    auto prop_ptr =
        std::make_unique<Propagator>(*g_ptr, *ex_ptr, stage_invalidation);
    return std::make_tuple(std::move(g_ptr), std::move(ex_ptr),
                           std::move(prop_ptr));
  };

  // Warm-up 1 run for B1
  {
    auto [g, ex, prop] = setup_from_base(true);
    for (const auto &ed : script.edits) {
      if (ed->kind() == EditKind::MODIFY)
        prop->apply_modify(static_cast<const ModifyEdit &>(*ed));
      else if (ed->kind() == EditKind::INSERT)
        prop->apply_insert(static_cast<const InsertEdit &>(*ed));
      else if (ed->kind() == EditKind::DELETE)
        prop->apply_delete(static_cast<const DeleteEdit &>(*ed));
    }
  }

  // Measure B1 (3 runs)
  std::vector<double> b1_times;
  for (int i = 0; i < 3; ++i) {
    auto [g, ex, prop] = setup_from_base(true);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (const auto &ed : script.edits) {
      if (ed->kind() == EditKind::MODIFY)
        prop->apply_modify(static_cast<const ModifyEdit &>(*ed));
      else if (ed->kind() == EditKind::INSERT)
        prop->apply_insert(static_cast<const InsertEdit &>(*ed));
      else if (ed->kind() == EditKind::DELETE)
        prop->apply_delete(static_cast<const DeleteEdit &>(*ed));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    b1_times.push_back(
        std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  row.b1_ms = compute_stats(b1_times).median_ms;

  // Warm-up 1 run for B2
  {
    auto [g, ex, prop] = setup_from_base(false);
    for (const auto &ed : script.edits) {
      if (ed->kind() == EditKind::MODIFY)
        prop->apply_modify(static_cast<const ModifyEdit &>(*ed));
      else if (ed->kind() == EditKind::INSERT)
        prop->apply_insert(static_cast<const InsertEdit &>(*ed));
      else if (ed->kind() == EditKind::DELETE)
        prop->apply_delete(static_cast<const DeleteEdit &>(*ed));
    }
  }

  // Measure B2 (3 runs)
  std::vector<double> b2_times;
  RebuildPlan last_plan;
  for (int i = 0; i < 3; ++i) {
    auto [g, ex, prop] = setup_from_base(false);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (const auto &ed : script.edits) {
      std::variant<RebuildPlan, CompileError> pres;
      if (ed->kind() == EditKind::MODIFY)
        pres = prop->apply_modify(static_cast<const ModifyEdit &>(*ed));
      else if (ed->kind() == EditKind::INSERT)
        pres = prop->apply_insert(static_cast<const InsertEdit &>(*ed));
      else if (ed->kind() == EditKind::DELETE)
        pres = prop->apply_delete(static_cast<const DeleteEdit &>(*ed));
      if (std::holds_alternative<RebuildPlan>(pres)) {
        last_plan = std::get<RebuildPlan>(pres);
      }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    b2_times.push_back(
        std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  row.b2_ms = compute_stats(b2_times).median_ms;
  row.speedup_b0_b2 = (row.b2_ms > 0.0001) ? (row.b0_ms / row.b2_ms) : 9999.0;
  row.speedup_b1_b2 = (row.b2_ms > 0.0001) ? (row.b1_ms / row.b2_ms) : 9999.0;
  row.reuse_pct = last_plan.reuse_percent;
  row.ops_evaluated = last_plan.recomputed_ops;
  row.ops_reused = last_plan.reused_ops;

  return row;
}

int main(int argc, char **argv) {
  std::string data_dir = "bench/data";
  std::string results_dir = "bench/results";
  if (argc > 1)
    data_dir = argv[1];
  if (argc > 2)
    results_dir = argv[2];

  fs::create_directories(results_dir);

  std::cout << "========================================================\n";
  std::cout << "RetroC Phase 11 Benchmarks\n";
  std::cout << "Data dir: " << data_dir << "\n";
  std::cout << "========================================================\n"
            << std::endl;

  std::vector<BenchResultRow> rows;

  // --- Workload W1 (100k records) ---
  std::string w1_rc = (fs::path(data_dir) / "w1_100000.rc").string();

  // W1, E-A: modify 1 input record
  {
    EditScript s;
    auto m = std::make_shared<ModifyEdit>();
    m->input_stage = "raw";
    m->record_id = 500;
    m->field_name = "temp";
    m->new_value = Value::make_int(85);
    s.edits.push_back(std::move(m));
    std::cout << "Benchmarking W1 E-A (modify 1 record)..." << std::endl;
    rows.push_back(benchmark_scenario("W1", w1_rc, data_dir, "E-A", s));
  }

  // W1, E-B: modify input records (batch of 10 records)
  {
    EditScript s;
    for (int i = 0; i < 10; ++i) {
      auto m = std::make_shared<ModifyEdit>();
      m->input_stage = "raw";
      m->record_id = i * 10000;
      m->field_name = "temp";
      m->new_value = Value::make_int(75);
      s.edits.push_back(std::move(m));
    }
    std::cout << "Benchmarking W1 E-B (modify batch of records)..." << std::endl;
    rows.push_back(benchmark_scenario("W1", w1_rc, data_dir, "E-B", s));
  }

  // W1, E-C: insert map near pipeline start (before normalized)
  {
    EditScript s;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "normalized";
    ins->new_stage_name = "shifted";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "r => { id: r.id, temp: r.temp + 1 }";
    s.edits.push_back(std::move(ins));
    std::cout << "Benchmarking W1 E-C (insert map near start)..." << std::endl;
    rows.push_back(benchmark_scenario("W1", w1_rc, data_dir, "E-C", s));
  }

  // W1, E-D: insert map near pipeline end (before total)
  {
    EditScript s;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "total";
    ins->new_stage_name = "capped";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "r => { id: r.id, temp: r.temp + 0 }";
    s.edits.push_back(std::move(ins));
    std::cout << "Benchmarking W1 E-D (insert map near end)..." << std::endl;
    rows.push_back(benchmark_scenario("W1", w1_rc, data_dir, "E-D", s));
  }

  // W1, E-E: delete a middle stage (delete scaled)
  {
    EditScript s;
    auto del = std::make_shared<DeleteEdit>();
    del->stage_name = "scaled";
    s.edits.push_back(std::move(del));
    std::cout << "Benchmarking W1 E-E (delete middle stage)..." << std::endl;
    rows.push_back(benchmark_scenario("W1", w1_rc, data_dir, "E-E", s));
  }

  // --- Workload W2 (Join: 50k users, 200k orders) ---
  std::string w2_rc = (fs::path(data_dir) / "w2.rc").string();
  {
    EditScript s;
    auto m = std::make_shared<ModifyEdit>();
    m->input_stage = "orders";
    m->record_id = 100;
    m->field_name = "amt";
    m->new_value = Value::make_int(120);
    s.edits.push_back(std::move(m));
    std::cout << "Benchmarking W2 E-A (join, modify 1 order)..." << std::endl;
    rows.push_back(benchmark_scenario("W2", w2_rc, data_dir, "E-A", s));
  }

  // --- Workload W3 (Deep pipeline: 12 stages, 100k records) ---
  std::string w3_rc = (fs::path(data_dir) / "w3.rc").string();
  {
    EditScript s;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "s2";
    ins->new_stage_name = "s1_boost";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "x => { id: x.id, val: x.val + 1 }";
    s.edits.push_back(std::move(ins));
    std::cout << "Benchmarking W3 E-C (deep pipeline, insert near start)..."
              << std::endl;
    rows.push_back(benchmark_scenario("W3", w3_rc, data_dir, "E-C", s));
  }

  // --- Workload W4 (Reduce-heavy: 1M ints) ---
  std::string w4_rc = (fs::path(data_dir) / "w4.rc").string();
  {
    EditScript s;
    auto m = std::make_shared<ModifyEdit>();
    m->input_stage = "raw";
    m->record_id = 50000;
    m->field_name = "val";
    m->new_value = Value::make_int(999);
    s.edits.push_back(std::move(m));
    std::cout << "Benchmarking W4 E-A (100k records reduce, modify 1)..."
              << std::endl;
    rows.push_back(benchmark_scenario("W4", w4_rc, data_dir, "E-A", s));
  }

  // --- First run overhead measurement for W1 ---
  std::cout << "Measuring first-run overhead for W1..." << std::endl;
  OverheadRow w1_overhead;
  w1_overhead.workload = "W1";
  run_first_run_overhead(w1_rc, data_dir, w1_overhead);

  // Print Results Table
  std::cout << "\n============================== BENCHMARK RESULTS "
               "==============================\n";
  std::cout << "| Workload | Edit | B0 ms   | B1 ms   | B2 ms   | Speedup "
               "B0/B2 | Speedup B1/B2 | Reuse % | Verify |\n";
  std::cout << "|----------|------|---------|---------|---------|--------------"
               "-|---------------|---------|--------|\n";
  for (const auto &r : rows) {
    std::cout << "| " << std::left << std::setw(8) << r.workload << " | "
              << std::left << std::setw(4) << r.edit << " | " << std::right
              << std::setw(7) << std::fixed << std::setprecision(2) << r.b0_ms
              << " | " << std::right << std::setw(7) << std::fixed
              << std::setprecision(2) << r.b1_ms << " | " << std::right
              << std::setw(7) << std::fixed << std::setprecision(2) << r.b2_ms
              << " | " << std::right << std::setw(13) << std::fixed
              << std::setprecision(2) << r.speedup_b0_b2 << "x"
              << " | " << std::right << std::setw(13) << std::fixed
              << std::setprecision(2) << r.speedup_b1_b2 << "x"
              << " | " << std::right << std::setw(6) << std::fixed
              << std::setprecision(1) << r.reuse_pct << "%"
              << " | " << (r.verify_ok ? "PASS  " : "FAIL  ") << " |\n";
  }

  std::cout << "\n=========================== FIRST-RUN OVERHEAD "
               "===========================\n";
  std::cout << "| Workload | First run plain ms | First run with history ms | "
               "Overhead | Memo MB | Input MB |\n";
  std::cout << "|----------|--------------------|---------------------------|--"
               "--------|---------|----------|\n";
  std::cout << "| " << std::left << std::setw(8) << w1_overhead.workload
            << " | " << std::right << std::setw(18) << std::fixed
            << std::setprecision(2) << w1_overhead.first_run_plain_ms << " | "
            << std::right << std::setw(25) << std::fixed << std::setprecision(2)
            << w1_overhead.first_run_history_ms << " | " << std::right
            << std::setw(7) << std::fixed << std::setprecision(1)
            << w1_overhead.overhead_pct << "%"
            << " | " << std::right << std::setw(7) << std::fixed
            << std::setprecision(2) << w1_overhead.memo_mb << " | "
            << std::right << std::setw(8) << std::fixed << std::setprecision(2)
            << w1_overhead.input_mb << " |\n";

  // Write JSON file
  std::string json_path =
      (fs::path(results_dir) / "benchmark_results.json").string();
  std::ofstream jf(json_path);
  jf << "{\n";
  jf << "  \"system\": {\n";
  jf << "    \"cpu\": \"AMD Ryzen 5 5600H with Radeon Graphics (6 cores, 12 "
        "logical)\",\n";
  jf << "    \"ram_gb\": 15.4,\n";
  jf << "    \"os\": \"Microsoft Windows 11 Home Single Language\",\n";
  jf << "    \"compiler\": \"GCC 15.2.0 (UCRT64)\",\n";
  jf << "    \"flags\": \"-O2 -Wall -Wextra -Werror\"\n";
  jf << "  },\n";
  jf << "  \"scenarios\": [\n";
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto &r = rows[i];
    jf << "    {\n";
    jf << "      \"workload\": \"" << r.workload << "\",\n";
    jf << "      \"edit\": \"" << r.edit << "\",\n";
    jf << "      \"b0_ms\": " << r.b0_ms << ",\n";
    jf << "      \"b1_ms\": " << r.b1_ms << ",\n";
    jf << "      \"b2_ms\": " << r.b2_ms << ",\n";
    jf << "      \"speedup_b0_b2\": " << r.speedup_b0_b2 << ",\n";
    jf << "      \"speedup_b1_b2\": " << r.speedup_b1_b2 << ",\n";
    jf << "      \"reuse_percent\": " << r.reuse_pct << ",\n";
    jf << "      \"ops_evaluated\": " << r.ops_evaluated << ",\n";
    jf << "      \"ops_reused\": " << r.ops_reused << ",\n";
    jf << "      \"verify_ok\": " << (r.verify_ok ? "true" : "false") << "\n";
    jf << "    }" << (i + 1 < rows.size() ? "," : "") << "\n";
  }
  jf << "  ],\n";
  jf << "  \"first_run_overhead\": {\n";
  jf << "    \"workload\": \"" << w1_overhead.workload << "\",\n";
  jf << "    \"plain_ms\": " << w1_overhead.first_run_plain_ms << ",\n";
  jf << "    \"with_history_ms\": " << w1_overhead.first_run_history_ms
     << ",\n";
  jf << "    \"overhead_percent\": " << w1_overhead.overhead_pct << ",\n";
  jf << "    \"memo_mb\": " << w1_overhead.memo_mb << ",\n";
  jf << "    \"input_mb\": " << w1_overhead.input_mb << "\n";
  jf << "  }\n";
  jf << "}\n";

  std::cout << "\nResults saved to: " << json_path << std::endl;
  return 0;
}
