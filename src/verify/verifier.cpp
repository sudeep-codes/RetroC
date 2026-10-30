#include "verify/verifier.hpp"
#include <algorithm>
#include "edit/source_rewrite.hpp"
#include "reuse/propagator.hpp"
#include "bytecode/bytecode.hpp"
#include "runtime/csv.hpp"
#include "ir/builder.hpp"
#include "sema/sema.hpp"
#include "parser/parser.hpp"
#include "lexer/lexer.hpp"
#include <sstream>
#include <fstream>
#include <functional>
#include <filesystem>

namespace retroc {

namespace {

// Position of a record in its (RecordId-sorted) input collection, which is
// also its position in the source array literal: deletes keep the relative
// order and inserts append.
size_t record_position(const Collection& col, RecordId id) {
    auto it = std::lower_bound(col.begin(), col.end(), id,
                               [](const CollectionItem& item, RecordId r) { return item.id < r; });
    return static_cast<size_t>(it - col.begin());
}

} // namespace

namespace {

std::variant<std::pair<Graph, TypedProgram>, CompileError> build_from_source(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    if (std::holds_alternative<CompileError>(lex_res)) return std::get<CompileError>(lex_res);

    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    if (std::holds_alternative<CompileError>(parse_res)) return std::get<CompileError>(parse_res);

    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    if (std::holds_alternative<CompileError>(sema_res)) return std::get<CompileError>(sema_res);

    auto typed_prog = std::get<TypedProgram>(std::move(sema_res));
    IRBuilder builder(typed_prog);
    Graph g = builder.build();
    return std::make_pair(std::move(g), std::move(typed_prog));
}

} // namespace

VerificationResult verify_edits(const std::string& original_source,
                                const EditScript& script,
                                const std::string& source_dir) {
    VerificationResult result;

    // 1. Initial run of original program
    auto build_res = build_from_source(original_source);
    if (std::holds_alternative<CompileError>(build_res)) {
        result.ok = false;
        result.mismatch_details = "Compile error in original source: " + std::get<CompileError>(build_res).format();
        return result;
    }

    auto [graph, typed_prog] = std::move(std::get<std::pair<Graph, TypedProgram>>(build_res));
    GraphExecutor executor(graph, source_dir, /*enable_memo=*/true);
    try {
        executor.run();
    } catch (const RuntimeError& err) {
        result.ok = false;
        result.mismatch_details = "Runtime error in original source: " + err.message;
        return result;
    }

    // Backup any CSV files used by input nodes so we can restore original disk contents
    std::unordered_map<std::string, std::string> csv_backups;
    for (const auto& [nid, n] : graph.nodes) {
        if (n.kind == NodeKind::INPUT && !n.from_file.empty()) {
            std::filesystem::path cp(n.from_file);
            if (cp.is_relative()) cp = std::filesystem::path(source_dir) / cp;
            if (std::filesystem::exists(cp)) {
                std::ifstream f(cp, std::ios::binary);
                std::ostringstream ss;
                ss << f.rdbuf();
                csv_backups[cp.string()] = ss.str();
            }
        }
    }

    auto restore_csvs = [&]() {
        for (const auto& [path, content] : csv_backups) {
            std::ofstream f(path, std::ios::binary);
            f << content;
            GraphExecutor::csv_cache().erase(path);
        }
    };

    struct ScopedRestorer {
        std::function<void()> fn;
        ~ScopedRestorer() { if (fn) fn(); }
    } restorer{restore_csvs};

    // 2. Propagate each edit in sequence
    Propagator propagator(graph, executor);
    std::string current_source = original_source;

    for (const auto& edit_ptr : script.edits) {
        if (edit_ptr->kind() == EditKind::MODIFY) {
            const auto& mod_edit = static_cast<const ModifyEdit&>(*edit_ptr);
            auto plan_res = propagator.apply_modify(mod_edit);
            if (std::holds_alternative<CompileError>(plan_res)) {
                result.ok = false;
                result.mismatch_details = "Propagation failed: " + std::get<CompileError>(plan_res).format();
                return result;
            }

            const auto* inp_node = graph.find_node_by_name(mod_edit.input_stage);
            if (inp_node && !inp_node->from_file.empty()) {
                // Input comes from CSV file: will save at the end of edit application
            } else {
                // After record deletes, RecordIds are no longer positions in the
                // source literal: rewrite the element at the record's position.
                ModifyEdit positioned = mod_edit;
                if (inp_node) {
                    positioned.record_id = static_cast<RecordId>(
                        record_position(executor.get_result(inp_node->id).collection_val, mod_edit.record_id));
                }
                auto rewrite_res = apply_modify_to_source(current_source, positioned);
                if (std::holds_alternative<CompileError>(rewrite_res)) {
                    result.ok = false;
                    result.mismatch_details = "Source rewrite failed: " + std::get<CompileError>(rewrite_res).format();
                    return result;
                }
                current_source = std::get<std::string>(rewrite_res);
            }
        } else if (edit_ptr->kind() == EditKind::INSERT_RECORD ||
                   edit_ptr->kind() == EditKind::DELETE_RECORD) {
            // Phase 12.3. A CSV input is written back to its file at the end;
            // an inline input has its array literal rewritten.
            bool is_insert = edit_ptr->kind() == EditKind::INSERT_RECORD;
            const std::string& input_name = is_insert
                ? static_cast<const InsertRecordEdit&>(*edit_ptr).input_stage
                : static_cast<const DeleteRecordEdit&>(*edit_ptr).input_stage;
            const auto* inp_node = graph.find_node_by_name(input_name);
            size_t position = 0;
            if (!is_insert && inp_node && inp_node->kind == NodeKind::INPUT) {
                position = record_position(executor.get_result(inp_node->id).collection_val,
                                           static_cast<const DeleteRecordEdit&>(*edit_ptr).record_id);
            }
            auto plan_res = propagator.apply_edit(*edit_ptr);
            if (std::holds_alternative<CompileError>(plan_res)) {
                result.ok = false;
                result.mismatch_details = "Record edit failed: " + std::get<CompileError>(plan_res).format();
                return result;
            }
            if (inp_node && inp_node->from_file.empty()) {
                auto rewrite_res = is_insert
                    ? apply_insert_record_to_source(current_source, static_cast<const InsertRecordEdit&>(*edit_ptr))
                    : apply_delete_record_to_source(current_source, input_name, position, edit_ptr->loc);
                if (std::holds_alternative<CompileError>(rewrite_res)) {
                    result.ok = false;
                    result.mismatch_details = "Source rewrite for record edit failed: " +
                                              std::get<CompileError>(rewrite_res).format();
                    return result;
                }
                current_source = std::get<std::string>(rewrite_res);
            }
        } else if (edit_ptr->kind() == EditKind::INSERT) {
            const auto& ins_edit = static_cast<const InsertEdit&>(*edit_ptr);
            auto plan_res = propagator.apply_insert(ins_edit);
            if (std::holds_alternative<CompileError>(plan_res)) {
                result.ok = false;
                result.mismatch_details = "Insert propagation failed: " + std::get<CompileError>(plan_res).format();
                return result;
            }

            const auto& plan = std::get<RebuildPlan>(plan_res);
            // In the plan, find the newly added stage name
            std::string actual_name = ins_edit.new_stage_name;
            if (actual_name.empty()) {
                for (const auto& sp : plan.stages) {
                    if (sp.status == StagePlanStatus::NEW) {
                        actual_name = sp.stage_name;
                        break;
                    }
                }
            }

            auto rewrite_res = apply_insert_to_source(current_source, ins_edit, actual_name);
            if (std::holds_alternative<CompileError>(rewrite_res)) {
                result.ok = false;
                result.mismatch_details = "Source rewrite for insert failed: " + std::get<CompileError>(rewrite_res).format();
                return result;
            }
            current_source = std::get<std::string>(rewrite_res);
        } else if (edit_ptr->kind() == EditKind::DELETE) {
            const auto& del_edit = static_cast<const DeleteEdit&>(*edit_ptr);

            // Find input stage name of the stage being deleted
            const auto* del_node = graph.find_node_by_name(del_edit.stage_name);
            std::string in_stage_name;
            if (del_node && !del_node->inputs.empty()) {
                const auto* in_node = graph.get_node(del_node->inputs[0]);
                if (in_node) in_stage_name = in_node->name;
            }

            auto plan_res = propagator.apply_delete(del_edit);
            if (std::holds_alternative<CompileError>(plan_res)) {
                result.ok = false;
                result.mismatch_details = "Delete propagation failed: " + std::get<CompileError>(plan_res).format();
                return result;
            }

            auto rewrite_res = apply_delete_to_source(current_source, del_edit, in_stage_name);
            if (std::holds_alternative<CompileError>(rewrite_res)) {
                result.ok = false;
                result.mismatch_details = "Source rewrite for delete failed: " + std::get<CompileError>(rewrite_res).format();
                return result;
            }
            current_source = std::get<std::string>(rewrite_res);
        }
    }

    // Sync any modified CSV collections to disk and cache for rerun
    for (const auto& [nid, n] : graph.nodes) {
        if (n.kind == NodeKind::INPUT && !n.from_file.empty()) {
            std::filesystem::path csv_path(n.from_file);
            if (csv_path.is_relative()) {
                csv_path = std::filesystem::path(source_dir) / csv_path;
            }
            const auto& col = executor.get_result(nid).collection_val;
            TypePtr elem_ty = (n.type && n.type->kind == TypeKind::ARRAY) ? n.type->element_type : nullptr;
            save_csv(csv_path.string(), col, elem_ty);
            GraphExecutor::csv_cache()[csv_path.string()] = col;
        }
    }

    result.propagated_outputs = executor.get_outputs();
    result.propagated_output_values = executor.get_output_values();

    // 3. Full rerun of rewritten source from scratch with empty memo
    auto rerun_build_res = build_from_source(current_source);
    if (std::holds_alternative<CompileError>(rerun_build_res)) {
        result.ok = false;
        result.mismatch_details = "Compile error in rewritten source: " + std::get<CompileError>(rerun_build_res).format();
        return result;
    }

    auto [rerun_graph, rerun_typed_prog] = std::move(std::get<std::pair<Graph, TypedProgram>>(rerun_build_res));
    GraphExecutor rerun_executor(rerun_graph, source_dir, /*enable_memo=*/false);
    try {
        rerun_executor.run();
    } catch (const RuntimeError& err) {
        result.ok = false;
        result.mismatch_details = "Runtime error during rerun: " + err.message;
        return result;
    }

    result.rerun_outputs = rerun_executor.get_outputs();
    result.rerun_output_values = rerun_executor.get_output_values();

    // 4. Compare outputs with full-value structural comparison (not hash-only)
    if (result.propagated_output_values.size() != result.rerun_output_values.size()) {
        result.ok = false;
        std::ostringstream oss;
        oss << "Output count mismatch: propagated has " << result.propagated_output_values.size()
            << ", rerun has " << result.rerun_output_values.size();
        result.mismatch_details = oss.str();
        return result;
    }

    for (size_t i = 0; i < result.propagated_output_values.size(); ++i) {
        const auto& [prop_name, prop_val] = result.propagated_output_values[i];
        const auto& [rerun_name, rerun_val] = result.rerun_output_values[i];

        if (prop_name != rerun_name || !(prop_val == rerun_val)) {
            result.ok = false;
            std::ostringstream oss;
            oss << "Full-value output mismatch on stage '" << prop_name << "':\n"
                << "  Propagated: " << prop_val.to_string() << "\n"
                << "  Rerun:      " << rerun_val.to_string() << "\n";
            result.mismatch_details = oss.str();
            return result;
        }

        // Also check string formatting matches
        if (i < result.propagated_outputs.size() && i < result.rerun_outputs.size()) {
            if (result.propagated_outputs[i].second != result.rerun_outputs[i].second) {
                result.ok = false;
                std::ostringstream oss;
                oss << "Output string formatting mismatch on stage '" << prop_name << "':\n"
                    << "  Propagated: " << result.propagated_outputs[i].second << "\n"
                    << "  Rerun:      " << result.rerun_outputs[i].second << "\n";
                result.mismatch_details = oss.str();
                return result;
            }
        }
    }

    result.ok = true;
    return result;
}

} // namespace retroc
