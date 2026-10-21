#include "verify/verifier.hpp"
#include "edit/source_rewrite.hpp"
#include "reuse/propagator.hpp"
#include "bytecode/bytecode.hpp"
#include "ir/builder.hpp"
#include "sema/sema.hpp"
#include "parser/parser.hpp"
#include "lexer/lexer.hpp"
#include <sstream>

namespace retroc {

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

            // Also rewrite the source for full rerun
            auto rewrite_res = apply_modify_to_source(current_source, mod_edit);
            if (std::holds_alternative<CompileError>(rewrite_res)) {
                result.ok = false;
                result.mismatch_details = "Source rewrite failed: " + std::get<CompileError>(rewrite_res).format();
                return result;
            }
            current_source = std::get<std::string>(rewrite_res);
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

    result.propagated_outputs = executor.get_outputs();

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

    // 4. Compare outputs (full value strings)
    if (result.propagated_outputs.size() != result.rerun_outputs.size()) {
        result.ok = false;
        std::ostringstream oss;
        oss << "Output count mismatch: propagated has " << result.propagated_outputs.size()
            << ", rerun has " << result.rerun_outputs.size();
        result.mismatch_details = oss.str();
        return result;
    }

    for (size_t i = 0; i < result.propagated_outputs.size(); ++i) {
        if (result.propagated_outputs[i].first != result.rerun_outputs[i].first ||
            result.propagated_outputs[i].second != result.rerun_outputs[i].second) {
            result.ok = false;
            std::ostringstream oss;
            oss << "Output mismatch on stage '" << result.propagated_outputs[i].first << "':\n"
                << "  Propagated: " << result.propagated_outputs[i].second << "\n"
                << "  Rerun:      " << result.rerun_outputs[i].second << "\n";
            result.mismatch_details = oss.str();
            return result;
        }
    }

    result.ok = true;
    return result;
}

} // namespace retroc
