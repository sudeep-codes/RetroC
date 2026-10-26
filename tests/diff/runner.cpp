#include "verify/verifier.hpp"
#include "edit/edit.hpp"
#include "reuse/propagator.hpp"
#include "ir/builder.hpp"
#include "sema/sema.hpp"
#include "parser/parser.hpp"
#include "lexer/lexer.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace retroc;

struct DiffTestConfig {
    size_t trials = 1000;
    uint32_t seed = 1;
};

static void dump_failure_artifacts(uint32_t seed, size_t trial_idx,
                                   const std::string& program_source,
                                   const EditScript& script,
                                   const std::string& mismatch_details) {
    try {
        std::filesystem::path dir = "tests/diff/failures/" + std::to_string(seed);
        std::filesystem::create_directories(dir);

        {
            std::ofstream f(dir / "seed.txt");
            f << "seed: " << seed << "\ntrial: " << trial_idx << "\n";
            f << "mismatch:\n" << mismatch_details << "\n";
        }
        {
            std::ofstream f(dir / "program.rc");
            f << program_source;
        }
        {
            std::ofstream f(dir / "edits.rcs");
            for (const auto& ed : script.edits) {
                f << ed->to_string() << "\n";
            }
        }
        std::cerr << "Saved failure artifacts to " << dir.string() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error dumping failure artifacts: " << e.what() << std::endl;
    }
}

// Tests an invalid edit on a valid running execution and verifies that:
// (a) the invalid edit is rejected with error E601;
// (b) the executor/graph state is unchanged (atomic rollback);
// (c) output values match the pre-edit execution exactly.
static bool test_invalid_edit(size_t trial_idx, std::mt19937& rng,
                              const std::string& program_source,
                              size_t num_records) {
    Lexer lexer(program_source);
    auto lex_res = lexer.tokenize();
    if (std::holds_alternative<CompileError>(lex_res)) return false;

    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    if (std::holds_alternative<CompileError>(parse_res)) return false;

    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    if (std::holds_alternative<CompileError>(sema_res)) return false;

    IRBuilder builder(std::get<TypedProgram>(sema_res));
    Graph graph = builder.build();

    GraphExecutor executor(graph, ".", /*enable_memo=*/true);
    try {
        executor.run();
    } catch (...) {
        return false;
    }

    auto outputs_before = executor.get_output_values();
    size_t nodes_count_before = graph.nodes.size();

    Propagator propagator(graph, executor);

    // Pick a kind of invalid edit:
    // 0: modify invalid RecordId (out of range)
    // 1: modify nonexistent field
    // 2: modify with wrong type
    // 3: insert before INPUT stage
    // 4: insert with lambda type error
    // 5: delete INPUT stage
    // 6: delete last producer of OUTPUT stage
    int invalid_kind = rng() % 7;
    std::variant<RebuildPlan, CompileError> res;

    if (invalid_kind == 0) {
        ModifyEdit bad_mod;
        bad_mod.input_stage = "rows";
        bad_mod.record_id = num_records + 100 + (rng() % 100);
        bad_mod.field_name = "ok";
        bad_mod.new_value = Value::make_bool(true);
        bad_mod.raw_literal_text = "true";
        res = propagator.apply_modify(bad_mod);
    } else if (invalid_kind == 1) {
        ModifyEdit bad_mod;
        bad_mod.input_stage = "rows";
        bad_mod.record_id = rng() % num_records;
        bad_mod.field_name = "nonexistent_field_" + std::to_string(rng() % 1000);
        bad_mod.new_value = Value::make_int(123);
        bad_mod.raw_literal_text = "123";
        res = propagator.apply_modify(bad_mod);
    } else if (invalid_kind == 2) {
        ModifyEdit bad_mod;
        bad_mod.input_stage = "rows";
        bad_mod.record_id = rng() % num_records;
        bad_mod.field_name = "ok";
        bad_mod.new_value = Value::make_string("not_a_bool");
        bad_mod.raw_literal_text = "\"not_a_bool\"";
        res = propagator.apply_modify(bad_mod);
    } else if (invalid_kind == 3) {
        InsertEdit bad_ins;
        bad_ins.before_stage = "rows"; // cannot insert before INPUT
        bad_ins.builtin_name = "map";
        bad_ins.raw_lambda_text = "r => r";
        res = propagator.apply_insert(bad_ins);
    } else if (invalid_kind == 4) {
        InsertEdit bad_ins;
        // Find a valid non-input stage
        std::string target_stage;
        for (const auto& nid : graph.order) {
            const auto* n = graph.get_node(nid);
            if (n && n->kind != NodeKind::INPUT && n->kind != NodeKind::OUTPUT) {
                target_stage = n->name;
                break;
            }
        }
        if (!target_stage.empty()) {
            bad_ins.before_stage = target_stage;
            bad_ins.builtin_name = "map";
            bad_ins.raw_lambda_text = "x => x.nonexistent_field + 99"; // Typecheck error
            res = propagator.apply_insert(bad_ins);
        } else {
            return true;
        }
    } else if (invalid_kind == 5) {
        DeleteEdit bad_del;
        bad_del.stage_name = "rows"; // cannot delete INPUT
        res = propagator.apply_delete(bad_del);
    } else {
        DeleteEdit bad_del;
        // Find direct source of output
        std::string out_source;
        for (const auto& out_id : graph.outputs) {
            const auto* out_node = graph.get_node(out_id);
            if (out_node && !out_node->inputs.empty()) {
                const auto* src_node = graph.get_node(out_node->inputs[0]);
                if (src_node) {
                    out_source = src_node->name;
                    break;
                }
            }
        }
        if (!out_source.empty()) {
            bad_del.stage_name = out_source;
            res = propagator.apply_delete(bad_del);
        } else {
            return true;
        }
    }

    // Must be rejected with CompileError (E601)
    if (!std::holds_alternative<CompileError>(res)) {
        std::cerr << "TRIAL #" << trial_idx << ": Invalid edit was NOT rejected!\n";
        return false;
    }

    const auto& err = std::get<CompileError>(res);
    if (err.code != ErrorCode::E601) {
        std::cerr << "TRIAL #" << trial_idx << ": Invalid edit returned code "
                  << static_cast<int>(err.code) << " instead of E601\n";
        return false;
    }

    // Check atomic rollback: graph nodes count unchanged
    if (graph.nodes.size() != nodes_count_before) {
        std::cerr << "TRIAL #" << trial_idx << ": Graph node count changed after rejected edit!\n";
        return false;
    }

    // Check outputs unchanged
    auto outputs_after = executor.get_output_values();
    if (outputs_before.size() != outputs_after.size()) {
        std::cerr << "TRIAL #" << trial_idx << ": Output count changed after rejected edit!\n";
        return false;
    }
    for (size_t i = 0; i < outputs_before.size(); ++i) {
        if (outputs_before[i].first != outputs_after[i].first ||
            !(outputs_before[i].second == outputs_after[i].second)) {
            std::cerr << "TRIAL #" << trial_idx << ": Output value changed after rejected edit on "
                      << outputs_before[i].first << "!\n";
            return false;
        }
    }

    return true;
}

// Generates a valid program with random records and random modify edits,
// runs verify_edits (propagated vs rerun from scratch), and validates equality.
static bool run_trial(size_t trial_idx, uint32_t seed, std::mt19937& rng) {
    // Generate 1-2 inputs
    size_t num_records = 5 + (rng() % 20); // 5 to 24 records

    std::uniform_real_distribution<double> val_dist(-10.0, 10.0);
    std::uniform_int_distribution<int> bool_dist(0, 1);
    std::uniform_int_distribution<int> int_dist(1, 100);

    auto format_float = [](double v) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << v;
        return oss.str();
    };

    std::ostringstream src;
    src << "input rows: [{ id: int, v: float, ok: bool }] = [\n";
    for (size_t r = 0; r < num_records; ++r) {
        double f_val = static_cast<int>(val_dist(rng) * 10.0) / 10.0;
        src << "  { id: " << (r + 1)
            << ", v: " << format_float(f_val)
            << ", ok: " << (bool_dist(rng) ? "true" : "false") << " }";
        if (r + 1 < num_records) src << ",";
        src << "\n";
    }
    src << "];\n";

    // Random choice of pipeline structure
    int pipeline_type = rng() % 3;
    if (pipeline_type == 0) {
        // filter -> map -> reduce -> scalar -> output
        src << "let clean = filter(rows, r => r.ok);\n";
        src << "let vals = map(clean, r => r.v);\n";
        src << "let sum = reduce(vals, 0.0, (a, x) => a + x);\n";
        src << "let avg = if length(vals) > 0 then sum / to_float(length(vals)) else 0.0;\n";
        src << "output avg;\n";
    } else if (pipeline_type == 1) {
        // map -> filter -> reduce -> output
        src << "let scaled = map(rows, r => { id: r.id, score: r.v * 2.0, ok: r.ok });\n";
        src << "let passed = filter(scaled, r => r.ok);\n";
        src << "let total = reduce(passed, 0.0, (acc, r) => acc + r.score);\n";
        src << "output total;\n";
    } else {
        // filter -> map -> output
        src << "let clean = filter(rows, r => r.ok);\n";
        src << "let ids = map(clean, r => r.id * 10);\n";
        src << "output ids;\n";
    }

    std::string program_source = src.str();

    // Track stages present in the program for generating valid insert / delete / modify edits
    struct StageInfo {
        std::string name;
        std::string in_elem_type;  // element type consumed by this stage
        std::string out_elem_type; // element type produced by this stage
        bool can_delete;
    };

    std::vector<StageInfo> stages;
    if (pipeline_type == 0) {
        stages.push_back({"clean", "row", "row", true});
        stages.push_back({"vals", "row", "float", false}); // sum depends on vals
    } else if (pipeline_type == 1) {
        stages.push_back({"scaled", "row", "score_row", false}); // produces {id, score, ok}
        stages.push_back({"passed", "score_row", "score_row", false}); // total depends on passed
    } else {
        stages.push_back({"clean", "row", "row", true});
        stages.push_back({"ids", "row", "int", false});
    }

    // Generate 1 to 4 random edits (modify, insert, delete)
    size_t num_edits = 1 + (rng() % 4);
    EditScript script;
    int gen_counter = 1;

    for (size_t e = 0; e < num_edits; ++e) {
        int edit_kind = rng() % 100; // 0..59: modify (60%), 60..79: insert (20%), 80..99: delete (20%)

        if (edit_kind < 60) {
            // Modify input record
            auto mod = std::make_shared<ModifyEdit>();
            mod->input_stage = "rows";
            mod->record_id = rng() % num_records;

            int field_choice = rng() % 3;
            if (field_choice == 0) {
                mod->field_name = "ok";
                bool new_b = (bool_dist(rng) == 1);
                mod->new_value = Value::make_bool(new_b);
                mod->raw_literal_text = new_b ? "true" : "false";
            } else if (field_choice == 1) {
                mod->field_name = "v";
                double new_v = static_cast<int>(val_dist(rng) * 10.0) / 10.0;
                mod->new_value = Value::make_float(new_v);
                mod->raw_literal_text = format_float(new_v);
            } else {
                mod->field_name = "id";
                int new_id = int_dist(rng);
                mod->new_value = Value::make_int(new_id);
                mod->raw_literal_text = std::to_string(new_id);
            }
            script.edits.push_back(mod);
        } else if (edit_kind < 80) {
            // Insert stage before target_stage
            if (!stages.empty()) {
                size_t target_idx = rng() % stages.size();
                auto target_stage = stages[target_idx];

                auto ins = std::make_shared<InsertEdit>();
                ins->before_stage = target_stage.name;
                std::string new_name = "ins_" + std::to_string(trial_idx) + "_" + std::to_string(gen_counter++);
                ins->new_stage_name = new_name;

                if (target_stage.in_elem_type == "row") {
                    if (rng() % 2 == 0) {
                        ins->builtin_name = "filter";
                        ins->raw_lambda_text = "x => x.id >= 0";
                    } else {
                        ins->builtin_name = "map";
                        ins->raw_lambda_text = "x => { id: x.id + 1, v: x.v, ok: x.ok }";
                    }
                    stages.insert(stages.begin() + target_idx, {new_name, "row", "row", true});
                } else if (target_stage.in_elem_type == "score_row") {
                    if (rng() % 2 == 0) {
                        ins->builtin_name = "filter";
                        ins->raw_lambda_text = "x => x.score >= -100.0";
                    } else {
                        ins->builtin_name = "map";
                        ins->raw_lambda_text = "x => { id: x.id, score: x.score + 0.0, ok: x.ok }";
                    }
                    stages.insert(stages.begin() + target_idx, {new_name, "score_row", "score_row", true});
                }
                script.edits.push_back(ins);
            }
        } else {
            // Delete stage (only if can_delete is true)
            std::vector<size_t> deletable_indices;
            for (size_t i = 0; i < stages.size(); ++i) {
                if (stages[i].can_delete) {
                    deletable_indices.push_back(i);
                }
            }
            if (!deletable_indices.empty()) {
                size_t chosen_idx = deletable_indices[rng() % deletable_indices.size()];
                auto del = std::make_shared<DeleteEdit>();
                del->stage_name = stages[chosen_idx].name;
                stages.erase(stages.begin() + chosen_idx);
                script.edits.push_back(del);
            }
        }
    }

    auto res = verify_edits(program_source, script);
    if (!res.ok) {
        std::cerr << "TRIAL #" << trial_idx << " FAILED!\n";
        std::cerr << "Mismatch details:\n" << res.mismatch_details << "\n";
        dump_failure_artifacts(seed, trial_idx, program_source, script, res.mismatch_details);
        return false;
    }

    // Also run an invalid-edit test on this generated program to ensure rejection + zero side effects
    if (!test_invalid_edit(trial_idx, rng, program_source, num_records)) {
        dump_failure_artifacts(seed, trial_idx, program_source, script, "Invalid edit verification failed");
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    DiffTestConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--trials" && i + 1 < argc) {
            config.trials = std::stoul(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoul(argv[++i]);
        }
    }

    std::cout << "Running differential tests: " << config.trials
              << " trials (seed=" << config.seed << ")..." << std::endl;

    std::mt19937 rng(config.seed);
    size_t passed = 0;

    for (size_t i = 0; i < config.trials; ++i) {
        if (!run_trial(i + 1, config.seed, rng)) {
            return 3; // Exit code 3: verification mismatch per ARCHITECTURE.md
        }
        passed++;
        if ((i + 1) % 100 == 0 || (i + 1) == config.trials) {
            std::cout << "  Passed " << (i + 1) << " / " << config.trials << " trials." << std::endl;
        }
    }

    std::cout << "All " << passed << " differential trials PASSED.\n";
    return 0;
}
