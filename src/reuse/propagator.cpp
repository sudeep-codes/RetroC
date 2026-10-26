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
    for (auto& item : col) {
        if (item.id == edit.record_id) {
            target_item = &item;
            break;
        }
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

    // Prepare RebuildPlan
    RebuildPlan plan;
    plan.edit_id = current_edit_id;
    plan.edit_description = edit.to_string();

    // Check if new record value is structurally identical to old record value
    bool value_unchanged = (old_record_val == new_record_val);

    // Apply modification to target item
    target_item->value = new_record_val;

    // Recompute input node outHash
    uint64_t old_input_hash = input_node->outHash;
    uint64_t new_input_hash = it_res->second.as_value().hash();

    // Invariant I4: version increases iff outHash changes
    if (new_input_hash != old_input_hash) {
        input_node->version++;
        input_node->outHash = new_input_hash;
    }
    stage_values[input_node->name] = it_res->second.as_value();

    // Work set: per-node pending record set
    // Seed work set: {(input_stage, RecordId)}
    std::unordered_map<NodeId, std::unordered_set<RecordId>> pending_records;
    std::unordered_set<NodeId> dirty_nodes;

    if (!value_unchanged && new_input_hash != old_input_hash) {
        // Add consumers of input_node
        for (const auto& nid : graph_.order) {
            const auto* n = graph_.get_node(nid);
            if (!n) continue;
            for (const auto& in_id : n->inputs) {
                if (in_id == input_node->id) {
                    pending_records[nid].insert(edit.record_id);
                    dirty_nodes.insert(nid);
                }
            }
        }
    }

    HistoryEntry hist_entry;
    hist_entry.edit_id = current_edit_id;
    hist_entry.description = edit.to_string();
    hist_entry.nodes_touched.push_back(input_node->name);

    // 6. Propagation algorithm (topological order)
    for (const auto& node_id : graph_.order) {
        auto* node = graph_.get_node(node_id);
        if (!node) continue;
        if (node->id == input_node->id) {
            continue; // input node already updated
        }

        bool has_pending = (pending_records.find(node_id) != pending_records.end() &&
                            !pending_records[node_id].empty());
        bool is_dirty = (dirty_nodes.find(node_id) != dirty_nodes.end());

        if (!has_pending && !is_dirty) {
            // Node has no pending work and no dirty inputs: early cutoff / clean
            node->status = NodeStatus::CLEAN;
            continue;
        }

        hist_entry.nodes_touched.push_back(node->name);

        StagePlan sp;
        sp.stage_name = node->name;

        NodeExecutionResult& res = results[node_id];
        uint64_t prev_out_hash = node->outHash;

        switch (node->kind) {
            case NodeKind::INPUT:
                break;

            case NodeKind::MAP: {
                // node->inputs[0] is input collection
                const auto& in_res = results[node->inputs[0]];
                res.is_collection = true;
                sp.ops_total = in_res.collection_val.size();

                BytecodeCompiler compiler(node->fn->params);
                auto prog = compiler.compile(node->fn->body.get());

                Collection new_col;
                new_col.reserve(in_res.collection_val.size());

                std::unordered_set<RecordId> changed_records;

                for (const auto& item : in_res.collection_val) {
                    uint64_t in_hash = item.value.hash();
                    // Invariant I3: check memo entry
                    bool can_reuse = false;
                    if (executor_.enable_memo()) {
                        auto memo_it = node->memo.map_memo.find(item.id);
                        if (memo_it != node->memo.map_memo.end() &&
                            memo_it->second.inHash == in_hash &&
                            memo_it->second.fnHash == node->fnHash) {
                            can_reuse = true;
                            sp.ops_reused++;
                            hist_entry.ops_reused++;
                            new_col.push_back(CollectionItem{item.id, memo_it->second.out});
                        }
                    }

                    if (!can_reuse) {
                        sp.ops_evaluated++;
                        hist_entry.ops_recomputed++;
                        Value out_val = VM::execute(prog, {item.value}, stage_values);
                        if (executor_.enable_memo()) {
                            node->memo.map_memo[item.id] = MapMemoEntry{in_hash, node->fnHash, out_val};
                        }
                        changed_records.insert(item.id);
                        new_col.push_back(CollectionItem{item.id, std::move(out_val)});
                    }
                }

                res.collection_val = std::move(new_col);

                if (sp.ops_evaluated == 0) {
                    sp.status = StagePlanStatus::REUSED;
                } else if (sp.ops_evaluated == sp.ops_total) {
                    sp.status = StagePlanStatus::FULL;
                } else {
                    sp.status = StagePlanStatus::PARTIAL;
                    sp.note = "(early cutoff on unchanged inputs)";
                }

                // If output changed, mark downstream
                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    // Add changed records to downstream pending sets
                    for (const auto& down_id : graph_.order) {
                        const auto* down_node = graph_.get_node(down_id);
                        if (!down_node) continue;
                        for (const auto& in_id : down_node->inputs) {
                            if (in_id == node->id) {
                                dirty_nodes.insert(down_id);
                                for (auto r : changed_records) {
                                    pending_records[down_id].insert(r);
                                }
                            }
                        }
                    }
                }
                break;
            }

            case NodeKind::FILTER: {
                const auto& in_res = results[node->inputs[0]];
                res.is_collection = true;
                sp.ops_total = in_res.collection_val.size();

                BytecodeCompiler compiler(node->fn->params);
                auto prog = compiler.compile(node->fn->body.get());

                Collection new_col;
                std::unordered_set<RecordId> changed_records;

                for (const auto& item : in_res.collection_val) {
                    uint64_t in_hash = item.value.hash();
                    bool keep = false;
                    bool can_reuse = false;

                    if (executor_.enable_memo()) {
                        auto memo_it = node->memo.filter_memo.find(item.id);
                        if (memo_it != node->memo.filter_memo.end() &&
                            memo_it->second.inHash == in_hash &&
                            memo_it->second.fnHash == node->fnHash) {
                            can_reuse = true;
                            sp.ops_reused++;
                            hist_entry.ops_reused++;
                            keep = memo_it->second.keep;
                            if (keep) {
                                new_col.push_back(item);
                            }
                        }
                    }

                    if (!can_reuse) {
                        sp.ops_evaluated++;
                        hist_entry.ops_recomputed++;
                        Value keep_val = VM::execute(prog, {item.value}, stage_values);
                        keep = keep_val.bool_val;
                        if (executor_.enable_memo()) {
                            node->memo.filter_memo[item.id] = FilterMemoEntry{in_hash, node->fnHash, keep};
                        }
                        changed_records.insert(item.id);
                        if (keep) {
                            new_col.push_back(item);
                        }
                    }
                }

                res.collection_val = std::move(new_col);

                if (sp.ops_evaluated == 0) {
                    sp.status = StagePlanStatus::REUSED;
                } else if (sp.ops_evaluated == sp.ops_total) {
                    sp.status = StagePlanStatus::FULL;
                } else {
                    sp.status = StagePlanStatus::PARTIAL;
                }

                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    for (const auto& down_id : graph_.order) {
                        const auto* down_node = graph_.get_node(down_id);
                        if (!down_node) continue;
                        for (const auto& in_id : down_node->inputs) {
                            if (in_id == node->id) {
                                dirty_nodes.insert(down_id);
                                for (auto r : changed_records) {
                                    pending_records[down_id].insert(r);
                                }
                            }
                        }
                    }
                }
                break;
            }

            case NodeKind::REDUCE: {
                const auto& in_res = results[node->inputs[0]];
                res.is_collection = false;
                sp.ops_total = in_res.collection_val.size();

                uint64_t in_hash = in_res.as_value().hash();
                uint64_t init_hash = node->init ? hash_expr_ir(node->init.get()) : 0;

                if (executor_.enable_memo() && node->memo.reduce_memo.valid &&
                    node->memo.reduce_memo.inputHash == in_hash &&
                    node->memo.reduce_memo.fnHash == node->fnHash &&
                    node->memo.reduce_memo.initHash == init_hash) {
                    sp.ops_reused = sp.ops_total;
                    hist_entry.ops_reused += sp.ops_total;
                    sp.status = StagePlanStatus::REUSED;
                    res.scalar_val = node->memo.reduce_memo.result;
                } else {
                    sp.ops_evaluated = sp.ops_total;
                    hist_entry.ops_recomputed += sp.ops_total;
                    sp.status = StagePlanStatus::FULL;
                    sp.note = "(reduce is O(n) in v1)";

                    BytecodeCompiler init_comp;
                    auto init_prog = init_comp.compile(node->init.get());
                    Value acc = VM::execute(init_prog, {}, stage_values);

                    if (node->fn.has_value()) {
                        BytecodeCompiler fn_comp(node->fn->params);
                        auto fn_prog = fn_comp.compile(node->fn->body.get());
                        for (const auto& item : in_res.collection_val) {
                            acc = VM::execute(fn_prog, {acc, item.value}, stage_values);
                        }
                    }
                    res.scalar_val = acc;
                    if (executor_.enable_memo()) {
                        node->memo.reduce_memo = ReduceMemoEntry{in_hash, node->fnHash, init_hash, acc, true};
                    }
                }

                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    for (const auto& down_id : graph_.order) {
                        const auto* down_node = graph_.get_node(down_id);
                        if (!down_node) continue;
                        for (const auto& in_id : down_node->inputs) {
                            if (in_id == node->id) {
                                dirty_nodes.insert(down_id);
                            }
                        }
                    }
                }
                break;
            }

            case NodeKind::TREE_REDUCE: {
                const auto& in_res = results[node->inputs[0]];
                res.is_collection = false;
                sp.ops_total = in_res.collection_val.size();

                uint64_t in_hash = in_res.as_value().hash();
                uint64_t init_hash = node->init ? hash_expr_ir(node->init.get()) : 0;

                auto& memo = node->memo.tree_reduce_memo;
                const auto& changed_recs = pending_records[node_id];

                bool can_incremental = (executor_.enable_memo() && memo.valid &&
                                        memo.fnHash == node->fnHash && memo.initHash == init_hash &&
                                        memo.num_leaves == in_res.collection_val.size() &&
                                        !changed_recs.empty());

                if (can_incremental && in_res.collection_val.size() > 1) {
                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());

                    std::unordered_set<size_t> dirty_nodes_at_level;

                    for (auto rid : changed_recs) {
                        auto leaf_it = memo.record_to_leaf.find(rid);
                        if (leaf_it == memo.record_to_leaf.end()) continue;
                        size_t leaf_idx = leaf_it->second;

                        const Value* new_val_ptr = nullptr;
                        for (const auto& it : in_res.collection_val) {
                            if (it.id == rid) {
                                new_val_ptr = &it.value;
                                break;
                            }
                        }
                        if (new_val_ptr) {
                            memo.tree[leaf_idx] = *new_val_ptr;
                            if (leaf_idx > 1) {
                                dirty_nodes_at_level.insert(leaf_idx / 2);
                            }
                        }
                    }

                    while (!dirty_nodes_at_level.empty()) {
                        std::unordered_set<size_t> next_level;
                        for (size_t idx : dirty_nodes_at_level) {
                            size_t left_child = 2 * idx;
                            size_t right_child = 2 * idx + 1;
                            if (memo.active[left_child] && memo.active[right_child]) {
                                sp.ops_evaluated++;
                                hist_entry.ops_recomputed++;
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
                        dirty_nodes_at_level = std::move(next_level);
                    }

                    sp.ops_reused = sp.ops_total > sp.ops_evaluated ? (sp.ops_total - sp.ops_evaluated) : 0;
                    hist_entry.ops_reused += sp.ops_reused;
                    sp.status = StagePlanStatus::PARTIAL;
                    sp.note = "(tree_reduce O(log n) update)";
                    memo.result = memo.tree[1];
                    memo.inputHash = in_hash;
                    res.scalar_val = memo.tree[1];
                } else {
                    BytecodeCompiler init_comp;
                    auto init_prog = init_comp.compile(node->init.get());
                    Value init_val = VM::execute(init_prog, {}, stage_values);

                    const auto& col = in_res.collection_val;
                    size_t n = col.size();

                    if (n == 0) {
                        res.scalar_val = init_val;
                        sp.status = StagePlanStatus::FULL;
                    } else if (n == 1) {
                        sp.ops_evaluated = 1;
                        hist_entry.ops_recomputed += 1;
                        BytecodeCompiler fn_comp(node->fn->params);
                        auto fn_prog = fn_comp.compile(node->fn->body.get());
                        Value out = VM::execute(fn_prog, {init_val, col[0].value}, stage_values);
                        res.scalar_val = out;
                        sp.status = StagePlanStatus::FULL;
                    } else {
                        sp.ops_evaluated = n - 1;
                        hist_entry.ops_recomputed += n - 1;
                        sp.status = StagePlanStatus::FULL;

                        BytecodeCompiler fn_comp(node->fn->params);
                        auto fn_prog = fn_comp.compile(node->fn->body.get());

                        size_t m = 1;
                        while (m < n) m <<= 1;

                        memo.leaf_offset = m;
                        memo.num_leaves = n;
                        memo.tree.resize(2 * m);
                        memo.active.resize(2 * m, false);
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
                        memo.fnHash = node->fnHash;
                        memo.initHash = init_hash;
                        memo.valid = true;
                    }
                }

                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    for (const auto& down_id : graph_.order) {
                        const auto* down_node = graph_.get_node(down_id);
                        if (!down_node) continue;
                        for (const auto& in_id : down_node->inputs) {
                            if (in_id == node->id) {
                                dirty_nodes.insert(down_id);
                            }
                        }
                    }
                }
                break;
            }

            case NodeKind::JOIN: {
                const auto& left_res = results[node->inputs[0]];
                const auto& right_res = results[node->inputs[1]];
                res.is_collection = true;
                sp.ops_total = left_res.collection_val.size() + right_res.collection_val.size();

                uint64_t ka_hash = node->fn ? hash_lambda_ir(*node->fn) : 0;
                uint64_t kb_hash = node->join_fn_b ? hash_lambda_ir(*node->join_fn_b) : 0;

                BytecodeCompiler ka_comp(node->fn->params);
                auto ka_prog = ka_comp.compile(node->fn->body.get());

                BytecodeCompiler kb_comp(node->join_fn_b->params);
                auto kb_prog = kb_comp.compile(node->join_fn_b->body.get());

                std::vector<std::pair<Value, const CollectionItem*>> right_keys;
                right_keys.reserve(right_res.collection_val.size());
                for (const auto& r_item : right_res.collection_val) {
                    Value key = VM::execute(kb_prog, {r_item.value}, stage_values);
                    sp.ops_evaluated++;
                    hist_entry.ops_recomputed++;
                    right_keys.push_back({std::move(key), &r_item});
                }

                Collection new_col;
                RecordId out_id = 0;
                for (const auto& l_item : left_res.collection_val) {
                    Value l_key = VM::execute(ka_prog, {l_item.value}, stage_values);
                    sp.ops_evaluated++;
                    hist_entry.ops_recomputed++;
                    uint64_t l_hash = l_item.value.hash();

                    for (const auto& [r_key, r_item_ptr] : right_keys) {
                        JoinPairKey pair_key{l_item.id, r_item_ptr->id};
                        uint64_t r_hash = r_item_ptr->value.hash();

                        if (executor_.enable_memo()) {
                            auto memo_it = node->memo.join_memo.find(pair_key);
                            if (memo_it != node->memo.join_memo.end() &&
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
                        if (executor_.enable_memo()) {
                            node->memo.join_memo[pair_key] = JoinMemoEntry{
                                l_hash, r_hash, ka_hash, kb_hash, matched, std::move(out_record)
                            };
                        }
                    }
                }
                res.collection_val = std::move(new_col);
                sp.status = StagePlanStatus::FULL;

                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    for (const auto& down_id : graph_.order) {
                        const auto* down_node = graph_.get_node(down_id);
                        if (!down_node) continue;
                        for (const auto& in_id : down_node->inputs) {
                            if (in_id == node->id) {
                                dirty_nodes.insert(down_id);
                            }
                        }
                    }
                }
                break;
            }

            case NodeKind::SCALAR: {
                res.is_collection = false;
                sp.ops_total = 1;

                uint64_t inputs_hash = 14695981039346656037ULL;
                for (const auto& in_id : node->inputs) {
                    inputs_hash ^= results[in_id].as_value().hash();
                    inputs_hash *= 1099511628211ULL;
                }

                if (executor_.enable_memo() && node->memo.scalar_memo.valid &&
                    node->memo.scalar_memo.inputsHash == inputs_hash &&
                    node->memo.scalar_memo.fnHash == node->fnHash) {
                    sp.ops_reused = 1;
                    hist_entry.ops_reused += 1;
                    sp.status = StagePlanStatus::REUSED;
                    res.scalar_val = node->memo.scalar_memo.result;
                } else {
                    sp.ops_evaluated = 1;
                    hist_entry.ops_recomputed += 1;
                    sp.status = StagePlanStatus::FULL;

                    BytecodeCompiler comp;
                    auto prog = comp.compile(node->expr.get());
                    res.scalar_val = VM::execute(prog, {}, stage_values);
                    if (executor_.enable_memo()) {
                        node->memo.scalar_memo = ScalarMemoEntry{inputs_hash, node->fnHash, res.scalar_val, true};
                    }
                }

                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    for (const auto& down_id : graph_.order) {
                        const auto* down_node = graph_.get_node(down_id);
                        if (!down_node) continue;
                        for (const auto& in_id : down_node->inputs) {
                            if (in_id == node->id) {
                                dirty_nodes.insert(down_id);
                            }
                        }
                    }
                }
                break;
            }

            case NodeKind::OUTPUT: {
                const auto& in_res = results[node->inputs[0]];
                res = in_res;
                uint64_t new_out_hash = res.as_value().hash();
                if (new_out_hash != prev_out_hash) {
                    node->version++;
                    node->outHash = new_out_hash;
                    plan.updated_outputs.push_back(node->name);
                    hist_entry.outputs_changed.push_back(node->name);
                }
                break;
            }
        }

        node->status = NodeStatus::CLEAN;
        stage_values[node->name] = res.as_value();

        if (node->kind != NodeKind::OUTPUT && node->kind != NodeKind::INPUT) {
            plan.stages.push_back(std::move(sp));
        }
    }

    // Refresh executor outputs list
    auto& exec_outputs = executor_.mutable_outputs();
    exec_outputs.clear();
    for (const auto& out_id : graph_.outputs) {
        const auto* n = graph_.get_node(out_id);
        if (n) {
            exec_outputs.push_back({n->name, results[out_id].as_value().to_string()});
        }
    }

    // Tally op counts for plan
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

    hist_entry.ops_recomputed = plan.recomputed_ops;
    hist_entry.ops_reused = plan.reused_ops;
    hist_entry.total_memo_bytes = executor_.total_memo_bytes();
    executor_.get_history().add_entry(std::move(hist_entry));

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

    // Backup graph and executor state for atomic rollback
    Graph backup_graph = graph_.clone();
    auto backup_results = executor_.get_all_results();
    auto backup_stage_values = executor_.get_all_stage_values();
    auto backup_outputs = executor_.get_outputs();

    // 1. Create fresh NodeId
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

    // 2. Replace target_node.inputs[0] with new_nid
    target_node->inputs[0] = new_nid;
    graph_.nodes[new_nid] = std::move(new_node);

    // 3. Recompute topological order (I1)
    if (!graph_.recompute_topological_order()) {
        // Rollback
        graph_ = std::move(backup_graph);
        executor_.mutable_results() = std::move(backup_results);
        executor_.mutable_stage_values() = std::move(backup_stage_values);
        executor_.mutable_outputs() = std::move(backup_outputs);
        return CompileError{ErrorCode::E601, edit.loc, "insert failed: cycle detected during topological sort"};
    }

    // 4. Seed work set: evaluate new stage on all records of P
    EditId current_edit_id = executor_.get_history().next_edit_id++;
    RebuildPlan plan;
    plan.edit_id = current_edit_id;
    plan.edit_description = edit.to_string();

    HistoryEntry hist_entry;
    hist_entry.edit_id = current_edit_id;
    hist_entry.description = edit.to_string();

    auto& results = executor_.mutable_results();
    auto& stage_values = executor_.mutable_stage_values();

    // Work set tracking
    std::unordered_set<NodeId> dirty_nodes = {new_nid, target_node->id};
    std::unordered_map<NodeId, std::unordered_set<RecordId>> pending_records;

    // Propagate from new stage through all downstream stages in topological order
    for (const auto& node_id : graph_.order) {
        auto* node = graph_.get_node(node_id);
        if (!node) continue;

        bool is_new_stage = (node_id == new_nid);
        bool is_dirty = (dirty_nodes.find(node_id) != dirty_nodes.end());
        bool has_pending = (pending_records.find(node_id) != pending_records.end() &&
                            !pending_records[node_id].empty());

        if (!is_new_stage && !is_dirty && !has_pending) {
            node->status = NodeStatus::CLEAN;
            continue;
        }

        hist_entry.nodes_touched.push_back(node->name);
        StagePlan sp;
        sp.stage_name = node->name;

        NodeExecutionResult& res = results[node_id];
        uint64_t prev_out_hash = node->outHash;

        if (node->kind == NodeKind::MAP) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = true;
            sp.ops_total = in_res.collection_val.size();

            BytecodeCompiler compiler(node->fn->params);
            auto prog = compiler.compile(node->fn->body.get());

            Collection new_col;
            new_col.reserve(in_res.collection_val.size());
            std::unordered_set<RecordId> changed_records;

            for (const auto& item : in_res.collection_val) {
                uint64_t in_hash = item.value.hash();
                bool can_reuse = false;
                if (!is_new_stage && executor_.enable_memo()) {
                    auto memo_it = node->memo.map_memo.find(item.id);
                    if (memo_it != node->memo.map_memo.end() &&
                        memo_it->second.inHash == in_hash &&
                        memo_it->second.fnHash == node->fnHash) {
                        can_reuse = true;
                        sp.ops_reused++;
                        hist_entry.ops_reused++;
                        new_col.push_back(CollectionItem{item.id, memo_it->second.out});
                    }
                }

                if (!can_reuse) {
                    sp.ops_evaluated++;
                    hist_entry.ops_recomputed++;
                    Value out_val = VM::execute(prog, {item.value}, stage_values);
                    if (executor_.enable_memo()) {
                        node->memo.map_memo[item.id] = MapMemoEntry{in_hash, node->fnHash, out_val};
                    }
                    changed_records.insert(item.id);
                    new_col.push_back(CollectionItem{item.id, std::move(out_val)});
                }
            }

            res.collection_val = std::move(new_col);

            if (is_new_stage) {
                sp.status = StagePlanStatus::NEW;
            } else if (sp.ops_evaluated == 0) {
                sp.status = StagePlanStatus::REUSED;
            } else if (sp.ops_evaluated == sp.ops_total) {
                sp.status = StagePlanStatus::FULL;
            } else {
                sp.status = StagePlanStatus::PARTIAL;
                sp.note = "(early cutoff on unchanged inputs)";
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash || is_new_stage) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) {
                            dirty_nodes.insert(down_id);
                            for (auto r : changed_records) pending_records[down_id].insert(r);
                        }
                    }
                }
            }
        } else if (node->kind == NodeKind::FILTER) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = true;
            sp.ops_total = in_res.collection_val.size();

            BytecodeCompiler compiler(node->fn->params);
            auto prog = compiler.compile(node->fn->body.get());

            Collection new_col;
            std::unordered_set<RecordId> changed_records;

            for (const auto& item : in_res.collection_val) {
                uint64_t in_hash = item.value.hash();
                bool keep = false;
                bool can_reuse = false;

                if (!is_new_stage && executor_.enable_memo()) {
                    auto memo_it = node->memo.filter_memo.find(item.id);
                    if (memo_it != node->memo.filter_memo.end() &&
                        memo_it->second.inHash == in_hash &&
                        memo_it->second.fnHash == node->fnHash) {
                        can_reuse = true;
                        sp.ops_reused++;
                        hist_entry.ops_reused++;
                        keep = memo_it->second.keep;
                        if (keep) new_col.push_back(item);
                    }
                }

                if (!can_reuse) {
                    sp.ops_evaluated++;
                    hist_entry.ops_recomputed++;
                    Value keep_val = VM::execute(prog, {item.value}, stage_values);
                    keep = keep_val.bool_val;
                    if (executor_.enable_memo()) {
                        node->memo.filter_memo[item.id] = FilterMemoEntry{in_hash, node->fnHash, keep};
                    }
                    changed_records.insert(item.id);
                    if (keep) new_col.push_back(item);
                }
            }

            res.collection_val = std::move(new_col);

            if (is_new_stage) {
                sp.status = StagePlanStatus::NEW;
            } else if (sp.ops_evaluated == 0) {
                sp.status = StagePlanStatus::REUSED;
            } else if (sp.ops_evaluated == sp.ops_total) {
                sp.status = StagePlanStatus::FULL;
            } else {
                sp.status = StagePlanStatus::PARTIAL;
                sp.note = "(early cutoff on unchanged inputs)";
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash || is_new_stage) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) {
                            dirty_nodes.insert(down_id);
                            for (auto r : changed_records) pending_records[down_id].insert(r);
                        }
                    }
                }
            }
        } else if (node->kind == NodeKind::REDUCE) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = false;
            sp.ops_total = in_res.collection_val.size();

            uint64_t in_hash = in_res.as_value().hash();
            uint64_t init_hash = node->init ? hash_expr_ir(node->init.get()) : 0;

            if (executor_.enable_memo() && node->memo.reduce_memo.valid &&
                node->memo.reduce_memo.inputHash == in_hash &&
                node->memo.reduce_memo.fnHash == node->fnHash &&
                node->memo.reduce_memo.initHash == init_hash) {
                sp.ops_reused = sp.ops_total;
                hist_entry.ops_reused += sp.ops_total;
                sp.status = StagePlanStatus::REUSED;
                res.scalar_val = node->memo.reduce_memo.result;
            } else {
                sp.ops_evaluated = sp.ops_total;
                hist_entry.ops_recomputed += sp.ops_total;
                sp.status = StagePlanStatus::FULL;
                sp.note = "(reduce is O(n) in v1)";

                BytecodeCompiler init_comp;
                auto init_prog = init_comp.compile(node->init.get());
                Value acc = VM::execute(init_prog, {}, stage_values);

                if (node->fn.has_value()) {
                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());
                    for (const auto& item : in_res.collection_val) {
                        acc = VM::execute(fn_prog, {acc, item.value}, stage_values);
                    }
                }
                res.scalar_val = acc;
                if (executor_.enable_memo()) {
                    node->memo.reduce_memo = ReduceMemoEntry{in_hash, node->fnHash, init_hash, acc, true};
                }
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) dirty_nodes.insert(down_id);
                    }
                }
            }
        } else if (node->kind == NodeKind::TREE_REDUCE) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = false;
            sp.ops_total = in_res.collection_val.size();

            uint64_t in_hash = in_res.as_value().hash();
            uint64_t init_hash = node->init ? hash_expr_ir(node->init.get()) : 0;

            auto& memo = node->memo.tree_reduce_memo;
            const auto& changed_recs = pending_records[node_id];

            bool can_incremental = (executor_.enable_memo() && memo.valid &&
                                    memo.fnHash == node->fnHash && memo.initHash == init_hash &&
                                    memo.num_leaves == in_res.collection_val.size() &&
                                    !changed_recs.empty());

            if (can_incremental && in_res.collection_val.size() > 1) {
                BytecodeCompiler fn_comp(node->fn->params);
                auto fn_prog = fn_comp.compile(node->fn->body.get());

                std::unordered_set<size_t> dirty_nodes_at_level;

                for (auto rid : changed_recs) {
                    auto leaf_it = memo.record_to_leaf.find(rid);
                    if (leaf_it == memo.record_to_leaf.end()) continue;
                    size_t leaf_idx = leaf_it->second;

                    const Value* new_val_ptr = nullptr;
                    for (const auto& it : in_res.collection_val) {
                        if (it.id == rid) {
                            new_val_ptr = &it.value;
                            break;
                        }
                    }
                    if (new_val_ptr) {
                        memo.tree[leaf_idx] = *new_val_ptr;
                        if (leaf_idx > 1) {
                            dirty_nodes_at_level.insert(leaf_idx / 2);
                        }
                    }
                }

                while (!dirty_nodes_at_level.empty()) {
                    std::unordered_set<size_t> next_level;
                    for (size_t idx : dirty_nodes_at_level) {
                        size_t left_child = 2 * idx;
                        size_t right_child = 2 * idx + 1;
                        if (memo.active[left_child] && memo.active[right_child]) {
                            sp.ops_evaluated++;
                            hist_entry.ops_recomputed++;
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
                    dirty_nodes_at_level = std::move(next_level);
                }

                sp.ops_reused = sp.ops_total > sp.ops_evaluated ? (sp.ops_total - sp.ops_evaluated) : 0;
                hist_entry.ops_reused += sp.ops_reused;
                sp.status = StagePlanStatus::PARTIAL;
                sp.note = "(tree_reduce O(log n) update)";
                memo.result = memo.tree[1];
                memo.inputHash = in_hash;
                res.scalar_val = memo.tree[1];
            } else {
                BytecodeCompiler init_comp;
                auto init_prog = init_comp.compile(node->init.get());
                Value init_val = VM::execute(init_prog, {}, stage_values);

                const auto& col = in_res.collection_val;
                size_t n = col.size();

                if (n == 0) {
                    res.scalar_val = init_val;
                    sp.status = StagePlanStatus::FULL;
                } else if (n == 1) {
                    sp.ops_evaluated = 1;
                    hist_entry.ops_recomputed += 1;
                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());
                    Value out = VM::execute(fn_prog, {init_val, col[0].value}, stage_values);
                    res.scalar_val = out;
                    sp.status = StagePlanStatus::FULL;
                } else {
                    sp.ops_evaluated = n - 1;
                    hist_entry.ops_recomputed += n - 1;
                    sp.status = StagePlanStatus::FULL;

                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());

                    size_t m = 1;
                    while (m < n) m <<= 1;

                    memo.leaf_offset = m;
                    memo.num_leaves = n;
                    memo.tree.resize(2 * m);
                    memo.active.resize(2 * m, false);
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
                    memo.fnHash = node->fnHash;
                    memo.initHash = init_hash;
                    memo.valid = true;
                }
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) dirty_nodes.insert(down_id);
                    }
                }
            }
        } else if (node->kind == NodeKind::SCALAR) {
            res.is_collection = false;
            sp.ops_total = 1;

            uint64_t inputs_hash = 14695981039346656037ULL;
            for (const auto& in_id : node->inputs) {
                inputs_hash ^= results[in_id].as_value().hash();
                inputs_hash *= 1099511628211ULL;
            }

            if (executor_.enable_memo() && node->memo.scalar_memo.valid &&
                node->memo.scalar_memo.inputsHash == inputs_hash &&
                node->memo.scalar_memo.fnHash == node->fnHash) {
                sp.ops_reused = 1;
                hist_entry.ops_reused += 1;
                sp.status = StagePlanStatus::REUSED;
                res.scalar_val = node->memo.scalar_memo.result;
            } else {
                sp.ops_evaluated = 1;
                hist_entry.ops_recomputed += 1;
                sp.status = StagePlanStatus::FULL;

                BytecodeCompiler comp;
                auto prog = comp.compile(node->expr.get());
                res.scalar_val = VM::execute(prog, {}, stage_values);
                if (executor_.enable_memo()) {
                    node->memo.scalar_memo = ScalarMemoEntry{inputs_hash, node->fnHash, res.scalar_val, true};
                }
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) dirty_nodes.insert(down_id);
                    }
                }
            }
        } else if (node->kind == NodeKind::OUTPUT) {
            const auto& in_res = results[node->inputs[0]];
            res = in_res;
            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                plan.updated_outputs.push_back(node->name);
                hist_entry.outputs_changed.push_back(node->name);
            }
        }

        node->status = NodeStatus::CLEAN;
        stage_values[node->name] = res.as_value();
        if (node->kind != NodeKind::OUTPUT && node->kind != NodeKind::INPUT) {
            plan.stages.push_back(std::move(sp));
        }
    }

    // Refresh outputs
    auto& exec_outputs = executor_.mutable_outputs();
    exec_outputs.clear();
    for (const auto& out_id : graph_.outputs) {
        const auto* n = graph_.get_node(out_id);
        if (n) {
            exec_outputs.push_back({n->name, results[out_id].as_value().to_string()});
        }
    }

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

    hist_entry.ops_recomputed = plan.recomputed_ops;
    hist_entry.ops_reused = plan.reused_ops;
    hist_entry.total_memo_bytes = executor_.total_memo_bytes();
    executor_.get_history().add_entry(std::move(hist_entry));

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

    // Backup graph & executor state for atomic rollback
    Graph backup_graph = graph_.clone();
    auto backup_results = executor_.get_all_results();
    auto backup_stage_values = executor_.get_all_stage_values();
    auto backup_outputs = executor_.get_outputs();

    NodeId target_id = target_node->id;
    std::vector<NodeId> consumers;

    // 1. For every consumer C of target_node, replace reference with p_id
    for (auto& [id, n] : graph_.nodes) {
        bool is_consumer = false;
        for (auto& in_id : n.inputs) {
            if (in_id == target_id) {
                in_id = p_id;
                is_consumer = true;
            }
        }
        if (is_consumer) {
            consumers.push_back(id);
        }
    }

    // 2. Remove target_node from graph
    graph_.nodes.erase(target_id);

    // 3. Recompute topological order
    if (!graph_.recompute_topological_order()) {
        graph_ = std::move(backup_graph);
        executor_.mutable_results() = std::move(backup_results);
        executor_.mutable_stage_values() = std::move(backup_stage_values);
        executor_.mutable_outputs() = std::move(backup_outputs);
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
    auto& stage_values = executor_.mutable_stage_values();
    results.erase(target_id);
    stage_values.erase(edit.stage_name);

    std::unordered_set<NodeId> dirty_nodes(consumers.begin(), consumers.end());
    std::unordered_map<NodeId, std::unordered_set<RecordId>> pending_records;

    // Seed consumers with records from p_node
    const auto& p_res = results[p_id];
    if (p_res.is_collection) {
        for (const auto& c_id : consumers) {
            for (const auto& item : p_res.collection_val) {
                pending_records[c_id].insert(item.id);
            }
        }
    }

    // 4. Propagate topologically
    for (const auto& node_id : graph_.order) {
        auto* node = graph_.get_node(node_id);
        if (!node) continue;

        bool is_dirty = (dirty_nodes.find(node_id) != dirty_nodes.end());
        bool has_pending = (pending_records.find(node_id) != pending_records.end() &&
                            !pending_records[node_id].empty());

        if (!is_dirty && !has_pending) {
            node->status = NodeStatus::CLEAN;
            continue;
        }

        hist_entry.nodes_touched.push_back(node->name);
        StagePlan sp;
        sp.stage_name = node->name;

        NodeExecutionResult& res = results[node_id];
        uint64_t prev_out_hash = node->outHash;

        if (node->kind == NodeKind::MAP) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = true;
            sp.ops_total = in_res.collection_val.size();

            BytecodeCompiler compiler(node->fn->params);
            auto prog = compiler.compile(node->fn->body.get());

            Collection new_col;
            new_col.reserve(in_res.collection_val.size());
            std::unordered_set<RecordId> changed_records;

            for (const auto& item : in_res.collection_val) {
                uint64_t in_hash = item.value.hash();
                bool can_reuse = false;
                if (executor_.enable_memo()) {
                    auto memo_it = node->memo.map_memo.find(item.id);
                    if (memo_it != node->memo.map_memo.end() &&
                        memo_it->second.inHash == in_hash &&
                        memo_it->second.fnHash == node->fnHash) {
                        can_reuse = true;
                        sp.ops_reused++;
                        hist_entry.ops_reused++;
                        new_col.push_back(CollectionItem{item.id, memo_it->second.out});
                    }
                }

                if (!can_reuse) {
                    sp.ops_evaluated++;
                    hist_entry.ops_recomputed++;
                    Value out_val = VM::execute(prog, {item.value}, stage_values);
                    if (executor_.enable_memo()) {
                        node->memo.map_memo[item.id] = MapMemoEntry{in_hash, node->fnHash, out_val};
                    }
                    changed_records.insert(item.id);
                    new_col.push_back(CollectionItem{item.id, std::move(out_val)});
                }
            }

            res.collection_val = std::move(new_col);

            if (sp.ops_evaluated == 0) {
                sp.status = StagePlanStatus::REUSED;
            } else if (sp.ops_evaluated == sp.ops_total) {
                sp.status = StagePlanStatus::FULL;
            } else {
                sp.status = StagePlanStatus::PARTIAL;
                sp.note = "(early cutoff on unchanged inputs)";
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) {
                            dirty_nodes.insert(down_id);
                            for (auto r : changed_records) pending_records[down_id].insert(r);
                        }
                    }
                }
            }
        } else if (node->kind == NodeKind::FILTER) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = true;
            sp.ops_total = in_res.collection_val.size();

            BytecodeCompiler compiler(node->fn->params);
            auto prog = compiler.compile(node->fn->body.get());

            Collection new_col;
            std::unordered_set<RecordId> changed_records;

            for (const auto& item : in_res.collection_val) {
                uint64_t in_hash = item.value.hash();
                bool keep = false;
                bool can_reuse = false;

                if (executor_.enable_memo()) {
                    auto memo_it = node->memo.filter_memo.find(item.id);
                    if (memo_it != node->memo.filter_memo.end() &&
                        memo_it->second.inHash == in_hash &&
                        memo_it->second.fnHash == node->fnHash) {
                        can_reuse = true;
                        sp.ops_reused++;
                        hist_entry.ops_reused++;
                        keep = memo_it->second.keep;
                        if (keep) new_col.push_back(item);
                    }
                }

                if (!can_reuse) {
                    sp.ops_evaluated++;
                    hist_entry.ops_recomputed++;
                    Value keep_val = VM::execute(prog, {item.value}, stage_values);
                    keep = keep_val.bool_val;
                    if (executor_.enable_memo()) {
                        node->memo.filter_memo[item.id] = FilterMemoEntry{in_hash, node->fnHash, keep};
                    }
                    changed_records.insert(item.id);
                    if (keep) new_col.push_back(item);
                }
            }

            res.collection_val = std::move(new_col);

            if (sp.ops_evaluated == 0) {
                sp.status = StagePlanStatus::REUSED;
            } else if (sp.ops_evaluated == sp.ops_total) {
                sp.status = StagePlanStatus::FULL;
            } else {
                sp.status = StagePlanStatus::PARTIAL;
                sp.note = "(early cutoff on unchanged inputs)";
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) {
                            dirty_nodes.insert(down_id);
                            for (auto r : changed_records) pending_records[down_id].insert(r);
                        }
                    }
                }
            }
        } else if (node->kind == NodeKind::REDUCE) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = false;
            sp.ops_total = in_res.collection_val.size();

            uint64_t in_hash = in_res.as_value().hash();
            uint64_t init_hash = node->init ? hash_expr_ir(node->init.get()) : 0;

            if (executor_.enable_memo() && node->memo.reduce_memo.valid &&
                node->memo.reduce_memo.inputHash == in_hash &&
                node->memo.reduce_memo.fnHash == node->fnHash &&
                node->memo.reduce_memo.initHash == init_hash) {
                sp.ops_reused = sp.ops_total;
                hist_entry.ops_reused += sp.ops_total;
                sp.status = StagePlanStatus::REUSED;
                res.scalar_val = node->memo.reduce_memo.result;
            } else {
                sp.ops_evaluated = sp.ops_total;
                hist_entry.ops_recomputed += sp.ops_total;
                sp.status = StagePlanStatus::FULL;
                sp.note = "(reduce is O(n) in v1)";

                BytecodeCompiler init_comp;
                auto init_prog = init_comp.compile(node->init.get());
                Value acc = VM::execute(init_prog, {}, stage_values);

                if (node->fn.has_value()) {
                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());
                    for (const auto& item : in_res.collection_val) {
                        acc = VM::execute(fn_prog, {acc, item.value}, stage_values);
                    }
                }
                res.scalar_val = acc;
                if (executor_.enable_memo()) {
                    node->memo.reduce_memo = ReduceMemoEntry{in_hash, node->fnHash, init_hash, acc, true};
                }
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) dirty_nodes.insert(down_id);
                    }
                }
            }
        } else if (node->kind == NodeKind::TREE_REDUCE) {
            const auto& in_res = results[node->inputs[0]];
            res.is_collection = false;
            sp.ops_total = in_res.collection_val.size();

            uint64_t in_hash = in_res.as_value().hash();
            uint64_t init_hash = node->init ? hash_expr_ir(node->init.get()) : 0;

            auto& memo = node->memo.tree_reduce_memo;
            const auto& changed_recs = pending_records[node_id];

            bool can_incremental = (executor_.enable_memo() && memo.valid &&
                                    memo.fnHash == node->fnHash && memo.initHash == init_hash &&
                                    memo.num_leaves == in_res.collection_val.size() &&
                                    !changed_recs.empty());

            if (can_incremental && in_res.collection_val.size() > 1) {
                BytecodeCompiler fn_comp(node->fn->params);
                auto fn_prog = fn_comp.compile(node->fn->body.get());

                std::unordered_set<size_t> dirty_nodes_at_level;

                for (auto rid : changed_recs) {
                    auto leaf_it = memo.record_to_leaf.find(rid);
                    if (leaf_it == memo.record_to_leaf.end()) continue;
                    size_t leaf_idx = leaf_it->second;

                    const Value* new_val_ptr = nullptr;
                    for (const auto& it : in_res.collection_val) {
                        if (it.id == rid) {
                            new_val_ptr = &it.value;
                            break;
                        }
                    }
                    if (new_val_ptr) {
                        memo.tree[leaf_idx] = *new_val_ptr;
                        if (leaf_idx > 1) {
                            dirty_nodes_at_level.insert(leaf_idx / 2);
                        }
                    }
                }

                while (!dirty_nodes_at_level.empty()) {
                    std::unordered_set<size_t> next_level;
                    for (size_t idx : dirty_nodes_at_level) {
                        size_t left_child = 2 * idx;
                        size_t right_child = 2 * idx + 1;
                        if (memo.active[left_child] && memo.active[right_child]) {
                            sp.ops_evaluated++;
                            hist_entry.ops_recomputed++;
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
                    dirty_nodes_at_level = std::move(next_level);
                }

                sp.ops_reused = sp.ops_total > sp.ops_evaluated ? (sp.ops_total - sp.ops_evaluated) : 0;
                hist_entry.ops_reused += sp.ops_reused;
                sp.status = StagePlanStatus::PARTIAL;
                sp.note = "(tree_reduce O(log n) update)";
                memo.result = memo.tree[1];
                memo.inputHash = in_hash;
                res.scalar_val = memo.tree[1];
            } else {
                BytecodeCompiler init_comp;
                auto init_prog = init_comp.compile(node->init.get());
                Value init_val = VM::execute(init_prog, {}, stage_values);

                const auto& col = in_res.collection_val;
                size_t n = col.size();

                if (n == 0) {
                    res.scalar_val = init_val;
                    sp.status = StagePlanStatus::FULL;
                } else if (n == 1) {
                    sp.ops_evaluated = 1;
                    hist_entry.ops_recomputed += 1;
                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());
                    Value out = VM::execute(fn_prog, {init_val, col[0].value}, stage_values);
                    res.scalar_val = out;
                    sp.status = StagePlanStatus::FULL;
                } else {
                    sp.ops_evaluated = n - 1;
                    hist_entry.ops_recomputed += n - 1;
                    sp.status = StagePlanStatus::FULL;

                    BytecodeCompiler fn_comp(node->fn->params);
                    auto fn_prog = fn_comp.compile(node->fn->body.get());

                    size_t m = 1;
                    while (m < n) m <<= 1;

                    memo.leaf_offset = m;
                    memo.num_leaves = n;
                    memo.tree.resize(2 * m);
                    memo.active.resize(2 * m, false);
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
                    memo.fnHash = node->fnHash;
                    memo.initHash = init_hash;
                    memo.valid = true;
                }
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) dirty_nodes.insert(down_id);
                    }
                }
            }
        } else if (node->kind == NodeKind::SCALAR) {
            res.is_collection = false;
            sp.ops_total = 1;

            uint64_t inputs_hash = 14695981039346656037ULL;
            for (const auto& in_id : node->inputs) {
                inputs_hash ^= results[in_id].as_value().hash();
                inputs_hash *= 1099511628211ULL;
            }

            if (executor_.enable_memo() && node->memo.scalar_memo.valid &&
                node->memo.scalar_memo.inputsHash == inputs_hash &&
                node->memo.scalar_memo.fnHash == node->fnHash) {
                sp.ops_reused = 1;
                hist_entry.ops_reused += 1;
                sp.status = StagePlanStatus::REUSED;
                res.scalar_val = node->memo.scalar_memo.result;
            } else {
                sp.ops_evaluated = 1;
                hist_entry.ops_recomputed += 1;
                sp.status = StagePlanStatus::FULL;

                BytecodeCompiler comp;
                auto prog = comp.compile(node->expr.get());
                res.scalar_val = VM::execute(prog, {}, stage_values);
                if (executor_.enable_memo()) {
                    node->memo.scalar_memo = ScalarMemoEntry{inputs_hash, node->fnHash, res.scalar_val, true};
                }
            }

            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                for (const auto& down_id : graph_.order) {
                    const auto* down_node = graph_.get_node(down_id);
                    if (!down_node) continue;
                    for (const auto& in_id : down_node->inputs) {
                        if (in_id == node->id) dirty_nodes.insert(down_id);
                    }
                }
            }
        } else if (node->kind == NodeKind::OUTPUT) {
            const auto& in_res = results[node->inputs[0]];
            res = in_res;
            uint64_t new_out_hash = res.as_value().hash();
            if (new_out_hash != prev_out_hash) {
                node->version++;
                node->outHash = new_out_hash;
                plan.updated_outputs.push_back(node->name);
                hist_entry.outputs_changed.push_back(node->name);
            }
        }

        node->status = NodeStatus::CLEAN;
        stage_values[node->name] = res.as_value();
        if (node->kind != NodeKind::OUTPUT && node->kind != NodeKind::INPUT) {
            plan.stages.push_back(std::move(sp));
        }
    }

    auto& exec_outputs = executor_.mutable_outputs();
    exec_outputs.clear();
    for (const auto& out_id : graph_.outputs) {
        const auto* n = graph_.get_node(out_id);
        if (n) {
            exec_outputs.push_back({n->name, results[out_id].as_value().to_string()});
        }
    }

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

    hist_entry.ops_recomputed = plan.recomputed_ops;
    hist_entry.ops_reused = plan.reused_ops;
    hist_entry.total_memo_bytes = executor_.total_memo_bytes();
    executor_.get_history().add_entry(std::move(hist_entry));

    return plan;
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
