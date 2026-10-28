#include "reuse/propagator.hpp"
#include "bytecode/bytecode.hpp"
#include "ir/expr_ir.hpp"
#include "ir/builder.hpp"
#include "parser/parser.hpp"
#include "lexer/lexer.hpp"
#include "sema/sema.hpp"
#include "sema/types.hpp"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace retroc {

std::string RebuildPlan::to_string() const {
    std::ostringstream oss;
    oss << "Edit #" << edit_id << ": " << edit_description << "\n";
    oss << "Plan:\n";
    for (const auto& sp : stages) {
        oss << "  " << std::left << std::setw(10) << sp.stage_name << " ";
        std::string status_str;
        switch (sp.status) {
            case StagePlanStatus::NEW:     status_str = "NEW"; break;
            case StagePlanStatus::FULL:    status_str = "FULL"; break;
            case StagePlanStatus::PARTIAL: status_str = "PARTIAL"; break;
            case StagePlanStatus::REUSED:  status_str = "REUSED"; break;
            case StagePlanStatus::CLEAN:   status_str = "CLEAN"; break;
        }
        oss << std::left << std::setw(8) << status_str;
        oss << " evaluated " << std::right << std::setw(6) << sp.ops_evaluated
            << " / " << std::left << std::setw(6) << sp.ops_total;
        if (sp.ops_reused > 0) {
            oss << "  reused " << sp.ops_reused;
        }
        if (!sp.note.empty()) {
            oss << "  " << sp.note;
        }
        oss << "\n";
    }

    oss << "Recomputed: " << recomputed_ops << " ops   "
        << "Reused: " << reused_ops << " ops   "
        << "Full-rerun cost: " << full_rerun_ops << " ops\n";

    oss << std::fixed << std::setprecision(1) << "Reuse: " << reuse_percent << "%";
    oss << "   Updated outputs:";
    if (updated_outputs.empty()) {
        oss << " (none)";
    } else {
        for (size_t i = 0; i < updated_outputs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << " " << updated_outputs[i];
        }
    }
    oss << "\n";
    return oss.str();
}
namespace {

std::unordered_map<NodeId, std::vector<NodeId>> build_consumer_index(const Graph& graph) {
    std::unordered_map<NodeId, std::vector<NodeId>> consumers;
    for (const auto& node_id : graph.order) {
        const Node* n = graph.get_node(node_id);
        if (!n) continue;
        for (const auto& in_id : n->inputs) {
            auto& list = consumers[in_id];
            if (list.empty() || list.back() != node_id) {
                list.push_back(node_id);
            }
        }
    }
    return consumers;
}

// Collections are sorted by RecordId: inputs number records by position, MAP and
// FILTER keep ids and order (I7), JOIN numbers its output in order. So a record
// can be found by binary search instead of a scan.
bool id_less(const CollectionItem& item, RecordId id) {
    return item.id < id;
}

Collection::iterator lower_bound_id(Collection& col, RecordId id) {
    return std::lower_bound(col.begin(), col.end(), id, id_less);
}

const CollectionItem* find_record(const Collection& col, RecordId id) {
    auto it = std::lower_bound(col.begin(), col.end(), id, id_less);
    return (it != col.end() && it->id == id) ? &*it : nullptr;
}

// Kinds that can consume a set of changed RecordIds instead of a full rescan
bool tracks_records(NodeKind kind) {
    return kind == NodeKind::MAP || kind == NodeKind::FILTER || kind == NodeKind::TREE_REDUCE;
}

} // namespace


std::variant<RebuildPlan, CompileError> Propagator::apply_modify(const ModifyEdit& edit) {
    // 1. Precondition checks per EDIT_SEMANTICS.md section 3:
    // "input stage exists, RecordId exists, field exists, literal has the field's type."
    auto* input_node = const_cast<Node*>(graph_.find_node_by_name(edit.input_stage));
    if (!input_node) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "modify failed: stage '" + edit.input_stage + "' not found"};
    }
    if (input_node->kind != NodeKind::INPUT) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "modify failed: stage '" + edit.input_stage + "' is not an INPUT stage"};
    }

    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();
    auto it_res = results.find(input_node->id);
    if (it_res == results.end() || !it_res->second.is_collection) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "modify failed: stage '" + edit.input_stage + "' has no collection data"};
    }

    auto& col = it_res->second.collection_val;
    CollectionItem* target_item = nullptr;
    auto target_it = lower_bound_id(col, edit.record_id);
    if (target_it != col.end() && target_it->id == edit.record_id) {
        target_item = &*target_it;
    }

    if (!target_item) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "modify failed: RecordId " + std::to_string(edit.record_id) +
                            " not found in input stage '" + edit.input_stage + "'"};
    }

    // Check field and type
    Value old_record_val = target_item->value;
    Value new_record_val;

    if (!edit.field_name.empty()) {
        if (target_item->value.kind != ValueKind::RECORD) {
            return CompileError{ErrorCode::E601, edit.loc,
                                "modify failed: cannot access field '" + edit.field_name + "' on non-record value"};
        }
        bool field_found = false;
        std::vector<RecordField> new_fields;
        new_fields.reserve(target_item->value.record_val.size());

        for (const auto& rf : target_item->value.record_val) {
            if (rf.name == edit.field_name) {
                field_found = true;
                // Check literal type matches existing field type
                if (rf.value) {
                    if (rf.value->kind != edit.new_value.kind) {
                        return CompileError{ErrorCode::E601, edit.loc,
                                            "modify failed: type mismatch for field '" + edit.field_name +
                                            "', expected " + std::to_string(static_cast<int>(rf.value->kind)) +
                                            " but found " + std::to_string(static_cast<int>(edit.new_value.kind))};
                    }
                }
                new_fields.push_back(RecordField{rf.name, std::make_shared<Value>(edit.new_value)});
            } else {
                new_fields.push_back(rf);
            }
        }

        if (!field_found) {
            return CompileError{ErrorCode::E601, edit.loc,
                                "modify failed: field '" + edit.field_name + "' not found in record"};
        }
        new_record_val = Value::make_record(std::move(new_fields));
    } else {
        // Direct modification of element value (e.g. primitive array)
        if (target_item->value.kind != edit.new_value.kind) {
            return CompileError{ErrorCode::E601, edit.loc,
                                "modify failed: type mismatch, expected " +
                                std::to_string(static_cast<int>(target_item->value.kind)) +
                                " but found " + std::to_string(static_cast<int>(edit.new_value.kind))};
        }
        new_record_val = edit.new_value;
    }

    // EditId allocation
    EditId current_edit_id = executor_.get_history().next_edit_id++;

    RebuildPlan plan;
    plan.edit_id = current_edit_id;
    plan.edit_description = edit.to_string();

    HistoryEntry hist_entry;
    hist_entry.edit_id = current_edit_id;
    hist_entry.description = edit.to_string();
    hist_entry.nodes_touched.push_back(input_node->name);

    // Patch the record and the collection hash in O(1) instead of rehashing
    // the whole input.
    NodeExecutionResult& input_res = it_res->second;
    bool value_unchanged = (old_record_val == new_record_val);
    input_res.term_sum -= collection_item_term(edit.record_id, old_record_val.hash());
    input_res.term_sum += collection_item_term(edit.record_id, new_record_val.hash());
    target_item->value = std::move(new_record_val);

    // Invariant I4: version increases iff outHash changes
    uint64_t new_input_hash = input_res.hash();
    bool input_changed = (new_input_hash != input_node->outHash);
    if (input_changed) {
        input_node->version++;
        input_node->outHash = new_input_hash;
    }
    stage_refs_ = analyze_stage_refs(graph_);
    store_stage_value(stage_values, stage_refs_, input_node->name, input_res);

    WorkSet ws;
    ws.edit_kind = EditKind::MODIFY;
    if (!value_unchanged && input_changed) {
        // Seed work set: {(input_stage, RecordId)}
        std::vector<RecordId> seed{edit.record_id};
        mark_consumers(*input_node, build_consumer_index(graph_), ws, &seed);
    }

    propagate(ws, plan, hist_entry);
    return plan;
}

std::variant<RebuildPlan, CompileError> Propagator::apply_insert(const InsertEdit& edit) {
    // Edit B preconditions (EDIT_SEMANTICS.md section 4):
    // "<stage> exists and is not an INPUT; its first input P is a collection;
    // the new lambda type-checks against P's element type;
    // <stage>'s own function still type-checks against the new stage's output element type."

    auto* target_node = graph_.find_node_by_name(edit.before_stage);
    if (!target_node) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: stage '" + edit.before_stage + "' not found"};
    }
    if (target_node->kind == NodeKind::INPUT) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: cannot insert before an INPUT stage"};
    }
    if (target_node->inputs.empty()) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: target stage '" + edit.before_stage + "' has no inputs"};
    }

    NodeId p_id = target_node->inputs[0];
    auto* p_node = graph_.get_node(p_id);
    if (!p_node || !p_node->type || p_node->type->kind != TypeKind::ARRAY) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: first input of '" + edit.before_stage + "' is not a collection"};
    }

    // Determine new stage name: <before_stage>$ins<k> or edit.new_stage_name
    std::string new_name = edit.new_stage_name;
    if (new_name.empty()) {
        static int ins_counter = 1;
        new_name = edit.before_stage + "$ins" + std::to_string(ins_counter++);
    }

    // Check if new_name already exists in graph
    if (graph_.find_node_by_name(new_name) != nullptr) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: stage name '" + new_name + "' already exists"};
    }

    // Parse lambda expression: parse string "r => ..."
    Lexer lam_lexer(edit.raw_lambda_text);
    auto lam_toks = lam_lexer.tokenize();
    if (std::holds_alternative<CompileError>(lam_toks)) {
        return std::get<CompileError>(lam_toks);
    }
    Parser lam_parser(std::get<std::vector<Token>>(lam_toks));
    // To parse the lambda cleanly with our expression parser, wrap it in a dummy let:
    // "let _dummy = <builtin>(<p_node->name>, <edit.raw_lambda_text>);"
    std::string dummy_prog_src = "input " + p_node->name + ": " + p_node->type->to_string() + " = [];\n" +
                                 "let " + new_name + " = " + edit.builtin_name +
                                 "(" + p_node->name + ", " + edit.raw_lambda_text + ");\n" +
                                 "output " + new_name + ";\n";
    Lexer dummy_lexer(dummy_prog_src);
    auto dummy_toks = dummy_lexer.tokenize();
    if (std::holds_alternative<CompileError>(dummy_toks)) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: invalid lambda syntax: " + std::get<CompileError>(dummy_toks).message};
    }
    Parser dummy_parser(std::get<std::vector<Token>>(dummy_toks));
    auto dummy_parse_res = dummy_parser.parse();
    if (std::holds_alternative<CompileError>(dummy_parse_res)) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: invalid lambda: " + std::get<CompileError>(dummy_parse_res).message};
    }
    SemanticAnalyzer dummy_sema(std::get<Program>(std::move(dummy_parse_res)));
    auto dummy_sema_res = dummy_sema.analyze();
    if (std::holds_alternative<CompileError>(dummy_sema_res)) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "insert failed: lambda type error: " + std::get<CompileError>(dummy_sema_res).message};
    }

    auto& typed_dummy = std::get<TypedProgram>(dummy_sema_res);
    IRBuilder dummy_builder(typed_dummy);
    Graph dummy_graph = dummy_builder.build();
    const Node* inserted_dummy_node = dummy_graph.find_node_by_name(new_name);
    if (!inserted_dummy_node) {
        return CompileError{ErrorCode::E601, edit.loc, "insert failed: could not build IR for new stage"};
    }

    // Type check that <stage>'s own function type-checks against the new stage's output element type
    TypePtr new_output_type = inserted_dummy_node->type;
    TypePtr new_elem_type = new_output_type->element_type;

    if (target_node->kind == NodeKind::MAP || target_node->kind == NodeKind::FILTER) {
        // If target_node is MAP or FILTER, verify its lambda works with new_elem_type
        std::string check_src = "input " + new_name + ": " + new_output_type->to_string() + " = [];\n";
        if (target_node->kind == NodeKind::MAP) {
            check_src += "let " + target_node->name + " = map(" + new_name + ", " + format_lambda_ir(*target_node->fn) + ");\n";
        } else {
            check_src += "let " + target_node->name + " = filter(" + new_name + ", " + format_lambda_ir(*target_node->fn) + ");\n";
        }
        check_src += "output " + target_node->name + ";\n";

        Lexer check_lex(check_src);
        auto check_toks = check_lex.tokenize();
        if (std::holds_alternative<CompileError>(check_toks)) {
            return CompileError{ErrorCode::E601, edit.loc, "insert failed: downstream typecheck error"};
        }
        Parser check_par(std::get<std::vector<Token>>(check_toks));
        auto check_p_res = check_par.parse();
        if (std::holds_alternative<CompileError>(check_p_res)) {
            return CompileError{ErrorCode::E601, edit.loc, "insert failed: downstream typecheck error"};
        }
        SemanticAnalyzer check_sema(std::get<Program>(std::move(check_p_res)));
        auto check_s_res = check_sema.analyze();
        if (std::holds_alternative<CompileError>(check_s_res)) {
            return CompileError{ErrorCode::E601, edit.loc,
                                "insert failed: " + target_node->name + " fails typecheck with new input type: " +
                                std::get<CompileError>(check_s_res).message};
        }
    }

    // 1. Create fresh NodeId (I2)
    static uint32_t insert_node_counter = 1000;
    NodeId new_nid = "n$" + std::to_string(insert_node_counter++);
    while (graph_.nodes.find(new_nid) != graph_.nodes.end()) {
        new_nid = "n$" + std::to_string(insert_node_counter++);
    }

    Node new_node;
    new_node.id = new_nid;
    new_node.name = new_name;
    new_node.kind = (edit.builtin_name == "filter") ? NodeKind::FILTER : NodeKind::MAP;
    new_node.inputs = {p_id};
    if (inserted_dummy_node->fn) {
        new_node.fn = inserted_dummy_node->fn->clone();
    }
    new_node.fnHash = inserted_dummy_node->fnHash;
    new_node.type = new_output_type;
    new_node.status = NodeStatus::DIRTY;

    // 2. Replace target_node.inputs[0] with new_nid. Nothing else is touched
    // before the topological sort, so undoing these two changes restores the
    // prior state exactly (atomic edit) without copying results or memos.
    NodeId target_id = target_node->id;
    std::vector<NodeId> old_order = graph_.order;
    target_node->inputs[0] = new_nid;
    graph_.nodes[new_nid] = std::move(new_node);

    // 3. Recompute topological order (I1)
    if (!graph_.recompute_topological_order()) {
        graph_.get_node(target_id)->inputs[0] = p_id;
        graph_.nodes.erase(new_nid);
        graph_.order = std::move(old_order);
        return CompileError{ErrorCode::E601, edit.loc, "insert failed: cycle detected during topological sort"};
    }

    EditId current_edit_id = executor_.get_history().next_edit_id++;
    RebuildPlan plan;
    plan.edit_id = current_edit_id;
    plan.edit_description = edit.to_string();

    HistoryEntry hist_entry;
    hist_entry.edit_id = current_edit_id;
    hist_entry.description = edit.to_string();

    // 4. Seed work set: the new stage is evaluated for every record of P
    WorkSet ws;
    ws.edit_kind = EditKind::INSERT;
    ws.new_stage = new_nid;
    ws.dirty.insert(new_nid);
    ws.dirty.insert(target_id);

    propagate(ws, plan, hist_entry);
    return plan;
}

std::variant<RebuildPlan, CompileError> Propagator::apply_delete(const DeleteEdit& edit) {
    // Edit C preconditions (EDIT_SEMANTICS.md section 5):
    // "<stage> is MAP or FILTER whose output element type equals its input element type
    // (so consumers still type-check), or removal otherwise type-checks (I6).
    // INPUT, REDUCE, JOIN, SCALAR stages cannot be deleted in v1.
    // Deleting a stage that is the only source of an OUTPUT is an error (E601)."

    auto* target_node = graph_.find_node_by_name(edit.stage_name);
    if (!target_node) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "delete failed: stage '" + edit.stage_name + "' not found"};
    }
    if (target_node->kind != NodeKind::MAP && target_node->kind != NodeKind::FILTER) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "delete failed: only MAP and FILTER stages can be deleted in v1"};
    }
    if (target_node->inputs.empty()) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "delete failed: stage '" + edit.stage_name + "' has no inputs"};
    }

    NodeId p_id = target_node->inputs[0];
    auto* p_node = graph_.get_node(p_id);
    if (!p_node) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "delete failed: input stage not found"};
    }

    // Check if target_node is the only source of an OUTPUT
    for (const auto& out_id : graph_.outputs) {
        const auto* out_node = graph_.get_node(out_id);
        if (out_node && !out_node->inputs.empty() && out_node->inputs[0] == target_node->id) {
            return CompileError{ErrorCode::E601, edit.loc,
                                "delete failed: cannot delete stage '" + edit.stage_name + "' which is the source of an output"};
        }
    }

    // Precondition check: type compatibility (I6)
    // If MAP changed the element type, deleting it may break consumers unless p_node's element type matches
    if (!types_equal(target_node->type, p_node->type)) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "delete failed: output type of '" + edit.stage_name + "' differs from input type and breaks consumers"};
    }

    // Record exactly what we rewire so a failed topological sort can be undone
    // without snapshotting every result and memo table (atomic edit).
    NodeId target_id = target_node->id;
    std::string target_name = target_node->name;
    std::vector<NodeId> old_order = graph_.order;
    std::vector<std::pair<NodeId, size_t>> rewired; // (consumer, input slot)
    std::vector<NodeId> consumers;

    // 1. For every consumer C of target_node, replace reference with p_id
    for (auto& [id, n] : graph_.nodes) {
        bool is_consumer = false;
        for (size_t i = 0; i < n.inputs.size(); ++i) {
            if (n.inputs[i] == target_id) {
                n.inputs[i] = p_id;
                rewired.push_back({id, i});
                is_consumer = true;
            }
        }
        if (is_consumer) {
            consumers.push_back(id);
        }
    }

    // 2. Remove target_node from graph (NodeId is never reused, I2)
    Node removed = std::move(graph_.nodes[target_id]);
    graph_.nodes.erase(target_id);

    // 3. Recompute topological order
    if (!graph_.recompute_topological_order()) {
        graph_.nodes[target_id] = std::move(removed);
        for (const auto& [cid, slot] : rewired) {
            graph_.get_node(cid)->inputs[slot] = target_id;
        }
        graph_.order = std::move(old_order);
        return CompileError{ErrorCode::E601, edit.loc, "delete failed: cycle detected during topological sort"};
    }

    EditId current_edit_id = executor_.get_history().next_edit_id++;
    RebuildPlan plan;
    plan.edit_id = current_edit_id;
    plan.edit_description = edit.to_string();

    HistoryEntry hist_entry;
    hist_entry.edit_id = current_edit_id;
    hist_entry.description = edit.to_string();

    auto& results = executor_.mutable_results();
    results.erase(target_id);
    executor_.mutable_stage_values().erase(target_name);

    // 4. Seed consumers with every record of P: each consumer reuses memo
    // entries whose input hash is unchanged (early cutoff).
    WorkSet ws;
    ws.edit_kind = EditKind::DELETE;
    std::vector<RecordId> seed;
    const auto& p_res = results[p_id];
    if (p_res.is_collection) {
        seed.reserve(p_res.collection_val.size());
        for (const auto& item : p_res.collection_val) {
            seed.push_back(item.id);
        }
    }
    for (const auto& c_id : consumers) {
        seed_consumer(c_id, ws, p_res.is_collection ? &seed : nullptr);
    }

    propagate(ws, plan, hist_entry);
    return plan;
}

void Propagator::seed_consumer(const NodeId& consumer, WorkSet& ws,
                               const std::vector<RecordId>* records) {
    ws.dirty.insert(consumer);
    const Node* cn = graph_.get_node(consumer);
    if (records && !stage_invalidation_ && cn && tracks_records(cn->kind)) {
        ws.pending[consumer].insert(records->begin(), records->end());
    } else {
        ws.full_scan.insert(consumer);
    }
}

void Propagator::mark_consumers(const Node& node, const ConsumerIndex& consumers, WorkSet& ws,
                                const std::vector<RecordId>* changed_out) {
    auto it = consumers.find(node.id);
    if (it == consumers.end()) return;
    for (const auto& c : it->second) {
        seed_consumer(c, ws, changed_out);
    }
}

void Propagator::refresh_stage_values() {
    auto& stage_values = executor_.mutable_stage_values();
    const auto& results = executor_.get_all_results();
    for (const auto& [name, use] : stage_refs_) {
        if (use == StageRefUse::NONE) continue;
        auto sv = stage_values.find(name);
        bool missing = (sv == stage_values.end());
        bool size_only = !missing && use == StageRefUse::FULL &&
                         sv->second.kind == ValueKind::ARRAY && sv->second.int_val != 0;
        if (!missing && !size_only) continue;
        for (const auto& [id, n] : graph_.nodes) {
            if (n.name != name || n.kind == NodeKind::OUTPUT) continue;
            auto r = results.find(id);
            if (r != results.end()) {
                store_stage_value(stage_values, stage_refs_, name, r->second);
            }
            break;
        }
    }
}

void Propagator::propagate(WorkSet& ws, RebuildPlan& plan, HistoryEntry& hist) {
    stage_refs_ = analyze_stage_refs(graph_);
    refresh_stage_values();
    ConsumerIndex consumers = build_consumer_index(graph_);
    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();

    // Propagation algorithm (EDIT_SEMANTICS.md section 6), topological order (I1)
    for (const auto& node_id : graph_.order) {
        Node* node = graph_.get_node(node_id);
        if (!node) continue;
        if (node->kind == NodeKind::INPUT || ws.dirty.find(node_id) == ws.dirty.end()) {
            // No pending work and no dirty inputs: early cutoff / clean
            node->status = NodeStatus::CLEAN;
            continue;
        }

        hist.nodes_touched.push_back(node->name);
        uint64_t prev_out_hash = node->outHash;

        if (node->kind == NodeKind::OUTPUT) {
            NodeExecutionResult& res = results[node_id];
            res = results[node->inputs[0]];
            uint64_t new_out_hash = res.hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                plan.updated_outputs.push_back(node->name);
                hist.outputs_changed.push_back(node->name);
            }
            node->status = NodeStatus::CLEAN;
            continue;
        }

        StagePlan sp;
        sp.stage_name = node->name;
        std::vector<RecordId> changed_out;
        bool tracked = false;

        switch (node->kind) {
            case NodeKind::MAP:
            case NodeKind::FILTER:
                process_map_filter(*node, ws, sp, changed_out);
                tracked = !stage_invalidation_;
                break;
            case NodeKind::REDUCE:
                process_reduce(*node, sp);
                break;
            case NodeKind::TREE_REDUCE:
                process_tree_reduce(*node, ws, sp);
                break;
            case NodeKind::JOIN:
                process_join(*node, sp);
                break;
            case NodeKind::SCALAR:
                process_scalar(*node, sp);
                break;
            case NodeKind::INPUT:
            case NodeKind::OUTPUT:
                break;
        }

        const NodeExecutionResult& res = results[node_id];
        uint64_t new_out_hash = res.hash();
        bool is_new = (node_id == ws.new_stage);
        // Invariant I4; an unchanged output hash is the early cutoff
        if (new_out_hash != prev_out_hash || is_new) {
            node->version++;
            node->outHash = new_out_hash;
            mark_consumers(*node, consumers, ws, tracked ? &changed_out : nullptr);
        }
        node->status = NodeStatus::CLEAN;
        store_stage_value(stage_values, stage_refs_, node->name, res);
        plan.stages.push_back(std::move(sp));
    }

    // Refresh executor outputs; only changed outputs are printed again
    auto& exec_outputs = executor_.mutable_outputs();
    std::unordered_map<std::string, std::string> old_strings;
    for (auto& [name, str] : exec_outputs) {
        old_strings[name] = std::move(str);
    }
    std::unordered_set<std::string> changed(plan.updated_outputs.begin(), plan.updated_outputs.end());
    exec_outputs.clear();
    for (const auto& out_id : graph_.outputs) {
        const auto* n = graph_.get_node(out_id);
        if (!n) continue;
        auto old = old_strings.find(n->name);
        if (old != old_strings.end() && changed.find(n->name) == changed.end()) {
            exec_outputs.push_back({n->name, std::move(old->second)});
        } else {
            exec_outputs.push_back({n->name, results[out_id].as_value().to_string()});
        }
    }

    finish_plan(plan, hist);
}

void Propagator::process_map_filter(Node& node, WorkSet& ws, StagePlan& sp,
                                    std::vector<RecordId>& changed_out) {
    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();
    NodeExecutionResult& res = results[node.id];
    const Collection& in = results[node.inputs[0]].collection_val;

    const bool is_map = (node.kind == NodeKind::MAP);
    const bool is_new = (node.id == ws.new_stage);
    const bool lookup_memo = use_record_memo() && !is_new;
    const bool store_memo = use_record_memo();

    res.is_collection = true;
    sp.ops_total = in.size();

    BytecodeCompiler compiler(node.fn->params);
    auto prog = compiler.compile(node.fn->body.get());

    // Evaluates one record. Returns true if fn ran (memo miss); sets present
    // (record is in the output) and out (its output value).
    auto eval = [&](const CollectionItem& item, bool& present, Value& out) -> bool {
        uint64_t in_hash = item.value.hash();
        if (lookup_memo) {
            // Invariant I3: reuse only if inHash AND fnHash match
            if (is_map) {
                auto it = node.memo.map_memo.find(item.id);
                if (it != node.memo.map_memo.end() && it->second.inHash == in_hash &&
                    it->second.fnHash == node.fnHash) {
                    present = true;
                    out = it->second.out;
                    return false;
                }
            } else {
                auto it = node.memo.filter_memo.find(item.id);
                if (it != node.memo.filter_memo.end() && it->second.inHash == in_hash &&
                    it->second.fnHash == node.fnHash) {
                    present = it->second.keep;
                    if (present) out = item.value;
                    return false;
                }
            }
        }
        sp.ops_evaluated++;
        Value v = VM::execute(prog, {item.value}, stage_values);
        if (is_map) {
            if (store_memo) node.memo.map_memo[item.id] = MapMemoEntry{in_hash, node.fnHash, v};
            present = true;
            out = std::move(v);
        } else {
            present = v.bool_val;
            if (store_memo) node.memo.filter_memo[item.id] = FilterMemoEntry{in_hash, node.fnHash, present};
            if (present) out = item.value;
        }
        return true;
    };

    auto pend_it = ws.pending.find(node.id);
    size_t n_pending = (pend_it == ws.pending.end()) ? 0 : pend_it->second.size();
    // Patch in place when few records changed; when many changed, one sequential
    // pass is cheaper than many binary searches and vector inserts.
    bool full = is_new || !lookup_memo || ws.full_scan.count(node.id) > 0 ||
                n_pending * 8 > in.size();

    if (full) {
        Collection old = std::move(res.collection_val);
        Collection out_col;
        out_col.reserve(is_map ? in.size() : old.size());
        uint64_t sum = 0;
        // B1 consumers rescan everything, so they need no changed-record set
        const bool track = !stage_invalidation_;
        size_t oi = 0;
        for (const auto& item : in) {
            bool present = false;
            Value v;
            bool reevaluated = eval(item, present, v);
            if (track) {
                while (oi < old.size() && old[oi].id < item.id) {
                    changed_out.push_back(old[oi++].id); // dropped upstream
                }
                bool had = (oi < old.size() && old[oi].id == item.id);
                if (had) {
                    // A memo hit reproduces the previous output for this record,
                    // so only re-evaluated records can differ.
                    if (!present || (reevaluated && old[oi].value != v)) {
                        changed_out.push_back(item.id);
                    }
                    oi++;
                } else if (present) {
                    changed_out.push_back(item.id);
                }
            }
            if (present) {
                sum += collection_item_term(item.id, v.hash());
                out_col.push_back(CollectionItem{item.id, std::move(v)});
            }
        }
        if (track) {
            while (oi < old.size()) {
                changed_out.push_back(old[oi++].id);
            }
        }
        res.collection_val = std::move(out_col);
        res.term_sum = sum;
    } else if (n_pending > 0) {
        std::vector<RecordId> ids(pend_it->second.begin(), pend_it->second.end());
        std::sort(ids.begin(), ids.end());
        Collection& col = res.collection_val;
        for (RecordId rid : ids) {
            const CollectionItem* in_item = find_record(in, rid);
            auto pos = lower_bound_id(col, rid);
            bool had = (pos != col.end() && pos->id == rid);
            bool present = false;
            bool reevaluated = false;
            Value v;
            if (in_item) {
                reevaluated = eval(*in_item, present, v);
            }
            if (had && present) {
                if (reevaluated && pos->value != v) {
                    res.term_sum -= collection_item_term(rid, pos->value.hash());
                    res.term_sum += collection_item_term(rid, v.hash());
                    pos->value = std::move(v);
                    changed_out.push_back(rid);
                }
            } else if (had) {
                res.term_sum -= collection_item_term(rid, pos->value.hash());
                col.erase(pos);
                changed_out.push_back(rid);
            } else if (present) {
                res.term_sum += collection_item_term(rid, v.hash());
                col.insert(pos, CollectionItem{rid, std::move(v)});
                changed_out.push_back(rid);
            }
        }
    }

    if (is_new) {
        // Consumers used to read P directly, so every record of P may differ
        // from what they saw before.
        changed_out.clear();
        for (const auto& item : in) {
            changed_out.push_back(item.id);
        }
    }

    // Records not evaluated were served from the memo (or were untouched).
    sp.ops_reused = sp.ops_total - sp.ops_evaluated;
    if (is_new) {
        sp.status = StagePlanStatus::NEW;
    } else if (sp.ops_evaluated == 0) {
        sp.status = StagePlanStatus::REUSED;
    } else if (sp.ops_evaluated == sp.ops_total) {
        sp.status = StagePlanStatus::FULL;
    } else {
        sp.status = StagePlanStatus::PARTIAL;
        if (is_map || ws.edit_kind != EditKind::MODIFY) {
            sp.note = "(early cutoff on unchanged inputs)";
        }
    }
}

void Propagator::process_reduce(Node& node, StagePlan& sp) {
    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();
    NodeExecutionResult& res = results[node.id];
    const NodeExecutionResult& in_res = results[node.inputs[0]];
    res.is_collection = false;
    sp.ops_total = in_res.collection_val.size();

    uint64_t in_hash = in_res.hash();
    uint64_t init_hash = node.init ? hash_expr_ir(node.init.get()) : 0;

    if (executor_.enable_memo() && node.memo.reduce_memo.valid &&
        node.memo.reduce_memo.inputHash == in_hash &&
        node.memo.reduce_memo.fnHash == node.fnHash &&
        node.memo.reduce_memo.initHash == init_hash) {
        sp.ops_reused = sp.ops_total;
        sp.status = StagePlanStatus::REUSED;
        res.scalar_val = node.memo.reduce_memo.result;
        return;
    }

    sp.ops_evaluated = sp.ops_total;
    sp.status = StagePlanStatus::FULL;
    sp.note = "(reduce is O(n) in v1)";

    BytecodeCompiler init_comp;
    auto init_prog = init_comp.compile(node.init.get());
    Value acc = VM::execute(init_prog, {}, stage_values);
    if (node.fn.has_value()) {
        BytecodeCompiler fn_comp(node.fn->params);
        auto fn_prog = fn_comp.compile(node.fn->body.get());
        for (const auto& item : in_res.collection_val) {
            acc = VM::execute(fn_prog, {acc, item.value}, stage_values);
        }
    }
    res.scalar_val = acc;
    if (executor_.enable_memo()) {
        node.memo.reduce_memo = ReduceMemoEntry{in_hash, node.fnHash, init_hash, acc, true};
    }
}

void Propagator::process_tree_reduce(Node& node, WorkSet& ws, StagePlan& sp) {
    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();
    NodeExecutionResult& res = results[node.id];
    const NodeExecutionResult& in_res = results[node.inputs[0]];
    const Collection& col = in_res.collection_val;
    res.is_collection = false;
    sp.ops_total = col.size();

    uint64_t in_hash = in_res.hash();
    uint64_t init_hash = node.init ? hash_expr_ir(node.init.get()) : 0;
    auto& memo = node.memo.tree_reduce_memo;

    static const std::unordered_set<RecordId> no_records;
    auto pend_it = ws.pending.find(node.id);
    const auto& changed_recs = (pend_it == ws.pending.end()) ? no_records : pend_it->second;

    bool can_incremental = (use_record_memo() && memo.valid &&
                            ws.full_scan.count(node.id) == 0 &&
                            memo.fnHash == node.fnHash && memo.initHash == init_hash &&
                            memo.num_leaves == col.size() && !changed_recs.empty() &&
                            col.size() > 1);
    // Every changed record must still map to the same leaf; otherwise the leaf
    // layout is stale and the tree is rebuilt.
    if (can_incremental) {
        for (auto rid : changed_recs) {
            if (memo.record_to_leaf.find(rid) == memo.record_to_leaf.end() ||
                find_record(col, rid) == nullptr) {
                can_incremental = false;
                break;
            }
        }
    }

    if (can_incremental) {
        BytecodeCompiler fn_comp(node.fn->params);
        auto fn_prog = fn_comp.compile(node.fn->body.get());

        std::unordered_set<size_t> dirty_at_level;
        for (auto rid : changed_recs) {
            size_t leaf_idx = memo.record_to_leaf[rid];
            memo.tree[leaf_idx] = find_record(col, rid)->value;
            if (leaf_idx > 1) {
                dirty_at_level.insert(leaf_idx / 2);
            }
        }

        // Ascend to the root: at most ceil(log2 n) combiner steps per change
        while (!dirty_at_level.empty()) {
            std::unordered_set<size_t> next_level;
            for (size_t idx : dirty_at_level) {
                size_t left_child = 2 * idx;
                size_t right_child = 2 * idx + 1;
                if (memo.active[left_child] && memo.active[right_child]) {
                    sp.ops_evaluated++;
                    memo.tree[idx] = VM::execute(fn_prog, {memo.tree[left_child], memo.tree[right_child]}, stage_values);
                    memo.active[idx] = true;
                } else if (memo.active[left_child]) {
                    memo.tree[idx] = memo.tree[left_child];
                    memo.active[idx] = true;
                } else if (memo.active[right_child]) {
                    memo.tree[idx] = memo.tree[right_child];
                    memo.active[idx] = true;
                }
                if (idx > 1) {
                    next_level.insert(idx / 2);
                }
            }
            dirty_at_level = std::move(next_level);
        }

        sp.ops_reused = sp.ops_total > sp.ops_evaluated ? (sp.ops_total - sp.ops_evaluated) : 0;
        sp.status = StagePlanStatus::PARTIAL;
        sp.note = "(tree_reduce O(log n) update)";
        memo.result = memo.tree[1];
        memo.inputHash = in_hash;
        res.scalar_val = memo.tree[1];
        return;
    }

    BytecodeCompiler init_comp;
    auto init_prog = init_comp.compile(node.init.get());
    Value init_val = VM::execute(init_prog, {}, stage_values);
    size_t n = col.size();
    sp.status = StagePlanStatus::FULL;

    if (n == 0) {
        res.scalar_val = init_val;
        return;
    }

    BytecodeCompiler fn_comp(node.fn->params);
    auto fn_prog = fn_comp.compile(node.fn->body.get());
    if (n == 1) {
        sp.ops_evaluated = 1;
        res.scalar_val = VM::execute(fn_prog, {init_val, col[0].value}, stage_values);
        return;
    }

    sp.ops_evaluated = n - 1;
    size_t m = 1;
    while (m < n) m <<= 1;

    memo.leaf_offset = m;
    memo.num_leaves = n;
    memo.tree.assign(2 * m, Value());
    memo.active.assign(2 * m, false);
    memo.record_to_leaf.clear();

    for (size_t i = 0; i < n; ++i) {
        size_t leaf_idx = m + i;
        memo.tree[leaf_idx] = col[i].value;
        memo.active[leaf_idx] = true;
        memo.record_to_leaf[col[i].id] = leaf_idx;
    }

    for (size_t i = m - 1; i >= 1; --i) {
        size_t left_child = 2 * i;
        size_t right_child = 2 * i + 1;
        if (memo.active[left_child] && memo.active[right_child]) {
            memo.tree[i] = VM::execute(fn_prog, {memo.tree[left_child], memo.tree[right_child]}, stage_values);
            memo.active[i] = true;
        } else if (memo.active[left_child]) {
            memo.tree[i] = memo.tree[left_child];
            memo.active[i] = true;
        } else if (memo.active[right_child]) {
            memo.tree[i] = memo.tree[right_child];
            memo.active[i] = true;
        } else {
            memo.active[i] = false;
        }
    }

    res.scalar_val = memo.tree[1];
    memo.result = memo.tree[1];
    memo.inputHash = in_hash;
    memo.fnHash = node.fnHash;
    memo.initHash = init_hash;
    memo.valid = true;
}

void Propagator::process_join(Node& node, StagePlan& sp) {
    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();
    NodeExecutionResult& res = results[node.id];
    const auto& left_res = results[node.inputs[0]];
    const auto& right_res = results[node.inputs[1]];
    res.is_collection = true;
    sp.ops_total = left_res.collection_val.size() + right_res.collection_val.size();
    sp.status = StagePlanStatus::FULL;

    uint64_t ka_hash = node.fn ? hash_lambda_ir(*node.fn) : 0;
    uint64_t kb_hash = node.join_fn_b ? hash_lambda_ir(*node.join_fn_b) : 0;

    BytecodeCompiler ka_comp(node.fn->params);
    auto ka_prog = ka_comp.compile(node.fn->body.get());
    BytecodeCompiler kb_comp(node.join_fn_b->params);
    auto kb_prog = kb_comp.compile(node.join_fn_b->body.get());

    std::vector<std::pair<Value, const CollectionItem*>> right_keys;
    right_keys.reserve(right_res.collection_val.size());
    for (const auto& r_item : right_res.collection_val) {
        Value key = VM::execute(kb_prog, {r_item.value}, stage_values);
        sp.ops_evaluated++;
        right_keys.push_back({std::move(key), &r_item});
    }

    Collection new_col;
    RecordId out_id = 0;
    for (const auto& l_item : left_res.collection_val) {
        Value l_key = VM::execute(ka_prog, {l_item.value}, stage_values);
        sp.ops_evaluated++;
        uint64_t l_hash = l_item.value.hash();

        for (const auto& [r_key, r_item_ptr] : right_keys) {
            JoinPairKey pair_key{l_item.id, r_item_ptr->id};
            uint64_t r_hash = r_item_ptr->value.hash();

            if (use_record_memo()) {
                auto memo_it = node.memo.join_memo.find(pair_key);
                if (memo_it != node.memo.join_memo.end() &&
                    memo_it->second.leftHash == l_hash &&
                    memo_it->second.rightHash == r_hash &&
                    memo_it->second.kaHash == ka_hash &&
                    memo_it->second.kbHash == kb_hash) {
                    if (memo_it->second.matched) {
                        new_col.push_back(CollectionItem{out_id++, memo_it->second.out});
                    }
                    continue;
                }
            }

            bool matched = (l_key == r_key);
            Value out_record;
            if (matched) {
                std::vector<RecordField> fields = {
                    {"left", std::make_shared<Value>(l_item.value)},
                    {"right", std::make_shared<Value>(r_item_ptr->value)}
                };
                out_record = Value::make_record(std::move(fields));
                new_col.push_back(CollectionItem{out_id++, out_record});
            }
            if (use_record_memo()) {
                node.memo.join_memo[pair_key] = JoinMemoEntry{
                    l_hash, r_hash, ka_hash, kb_hash, matched, std::move(out_record)
                };
            }
        }
    }
    res.collection_val = std::move(new_col);
    res.rehash_collection();
}

void Propagator::process_scalar(Node& node, StagePlan& sp) {
    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();
    NodeExecutionResult& res = results[node.id];
    res.is_collection = false;
    sp.ops_total = 1;

    uint64_t inputs_hash = 14695981039346656037ULL;
    for (const auto& in_id : node.inputs) {
        inputs_hash ^= results[in_id].hash();
        inputs_hash *= 1099511628211ULL;
    }

    if (executor_.enable_memo() && node.memo.scalar_memo.valid &&
        node.memo.scalar_memo.inputsHash == inputs_hash &&
        node.memo.scalar_memo.fnHash == node.fnHash) {
        sp.ops_reused = 1;
        sp.status = StagePlanStatus::REUSED;
        res.scalar_val = node.memo.scalar_memo.result;
        return;
    }

    sp.ops_evaluated = 1;
    sp.status = StagePlanStatus::FULL;
    BytecodeCompiler comp;
    auto prog = comp.compile(node.expr.get());
    res.scalar_val = VM::execute(prog, {}, stage_values);
    if (executor_.enable_memo()) {
        node.memo.scalar_memo = ScalarMemoEntry{inputs_hash, node.fnHash, res.scalar_val, true};
    }
}

void Propagator::finish_plan(RebuildPlan& plan, HistoryEntry& hist) {
    for (const auto& sp : plan.stages) {
        plan.recomputed_ops += sp.ops_evaluated;
        plan.reused_ops += sp.ops_reused;
        plan.full_rerun_ops += sp.ops_total;
    }
    if (plan.recomputed_ops + plan.reused_ops > 0) {
        plan.reuse_percent = 100.0 * static_cast<double>(plan.reused_ops) /
                             static_cast<double>(plan.recomputed_ops + plan.reused_ops);
    } else {
        plan.reuse_percent = 0.0;
    }

    hist.ops_recomputed = plan.recomputed_ops;
    hist.ops_reused = plan.reused_ops;
    hist.total_memo_bytes = executor_.total_memo_bytes();
    executor_.get_history().add_entry(std::move(hist));
}

std::string Propagator::explain_output(const std::string& output_name) const {
    const auto& history = executor_.get_history();
    std::ostringstream oss;
    oss << "Explain " << output_name << ":\n";

    size_t count = 0;
    for (const auto& entry : history.entries) {
        if (entry.edit_id == 0) {
            continue; // Skip initial run
        }
        bool changed = false;
        for (const auto& out : entry.outputs_changed) {
            if (out == output_name) {
                changed = true;
                break;
            }
        }
        if (changed) {
            count++;
            std::string first_stage = entry.nodes_touched.empty() ? "(unknown)" : entry.nodes_touched.front();
            oss << "  Edit #" << entry.edit_id << ": " << entry.description << "\n";
            oss << "    first stage: " << first_stage << "\n";
            oss << "    recomputed:  " << entry.ops_recomputed << " ops, reused: " << entry.ops_reused << " ops\n";
        }
    }

    if (count == 0) {
        oss << "  (no edits changed output '" << output_name << "')\n";
    }

    return oss.str();
}

} // namespace retroc
