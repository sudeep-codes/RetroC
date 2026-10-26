#include "runtime/executor.hpp"
#include "runtime/csv.hpp"
#include "bytecode/bytecode.hpp"
#include <iostream>
#include <filesystem>

namespace retroc {

size_t GraphExecutor::total_memo_bytes() const {
    size_t total = 0;
    for (const auto& [id, node] : graph_.nodes) {
        total += node.memo.bytes();
    }
    return total;
}

void GraphExecutor::run(std::ostream* output_stream) {
    results_.clear();
    stage_values_.clear();
    outputs_.clear();

    HistoryEntry entry;
    entry.edit_id = 0; // 0 represents initial run
    entry.description = "initial run";

    // Invariant I1: graph_.order is a valid topological order
    for (const auto& node_id : graph_.order) {
        auto it = graph_.nodes.find(node_id);
        if (it == graph_.nodes.end()) {
            throw RuntimeError("Node not found: " + node_id);
        }
        execute_node(it->second, entry);
    }

    entry.total_memo_bytes = total_memo_bytes();
    history_.add_entry(std::move(entry));

    if (output_stream) {
        for (const auto& [name, str_val] : outputs_) {
            *output_stream << name << " = " << str_val << "\n";
        }
    }
}

const NodeExecutionResult& GraphExecutor::get_result(const NodeId& id) const {
    auto it = results_.find(id);
    if (it == results_.end()) {
        throw RuntimeError("No execution result for node: " + id);
    }
    return it->second;
}

std::vector<std::pair<std::string, std::string>> GraphExecutor::get_outputs() const {
    return outputs_;
}

std::vector<std::pair<std::string, Value>> GraphExecutor::get_output_values() const {
    std::vector<std::pair<std::string, Value>> out_vals;
    out_vals.reserve(graph_.outputs.size());
    for (const auto& out_id : graph_.outputs) {
        const auto* n = graph_.get_node(out_id);
        if (n) {
            auto it = results_.find(out_id);
            if (it != results_.end()) {
                out_vals.push_back({n->name, it->second.as_value()});
            }
        }
    }
    return out_vals;
}

void GraphExecutor::execute_node(Node& node, HistoryEntry& entry) {
    NodeExecutionResult res;
    entry.nodes_touched.push_back(node.name);

    if (!enable_memo_) {
        node.memo.clear();
    }

    switch (node.kind) {
        case NodeKind::INPUT: {
            if (!node.from_file.empty()) {
                // Load from CSV file
                std::filesystem::path csv_path(node.from_file);
                if (csv_path.is_relative()) {
                    csv_path = std::filesystem::path(source_dir_) / csv_path;
                }
                TypePtr elem_ty = (node.type && node.type->kind == TypeKind::ARRAY) ? node.type->element_type : nullptr;
                res.is_collection = true;
                auto& cache = csv_cache();
                auto it = cache.find(csv_path.string());
                if (it != cache.end()) {
                    res.collection_val = it->second;
                } else {
                    res.collection_val = load_csv(csv_path.string(), elem_ty);
                    cache[csv_path.string()] = res.collection_val;
                }
                entry.ops_recomputed += res.collection_val.size();
            } else if (node.expr) {
                // Inline data: compile and evaluate expr
                BytecodeCompiler compiler;
                auto prog = compiler.compile(node.expr.get());
                Value val = VM::execute(prog, {}, stage_values_);
                entry.ops_recomputed += 1;

                if (node.type && node.type->kind == TypeKind::ARRAY) {
                    res.is_collection = true;
                    res.collection_val.reserve(val.array_val.size());
                    for (size_t i = 0; i < val.array_val.size(); ++i) {
                        res.collection_val.push_back(CollectionItem{static_cast<RecordId>(i), std::move(val.array_val[i])});
                    }
                } else {
                    res.is_collection = false;
                    res.scalar_val = std::move(val);
                }
            }
            break;
        }

        case NodeKind::MAP: {
            // MAP: 1 input collection. Invariant I7: keep order and RecordIds
            const auto& input_res = results_[node.inputs[0]];
            res.is_collection = true;
            if (node.fn.has_value()) {
                BytecodeCompiler compiler(node.fn->params);
                auto prog = compiler.compile(node.fn->body.get());
                res.collection_val.reserve(input_res.collection_val.size());
                for (const auto& item : input_res.collection_val) {
                    uint64_t in_hash = item.value.hash();
                    // Invariant I3: check memo entry matches inHash AND fnHash
                    if (enable_memo_) {
                        auto memo_it = node.memo.map_memo.find(item.id);
                        if (memo_it != node.memo.map_memo.end() &&
                            memo_it->second.inHash == in_hash &&
                            memo_it->second.fnHash == node.fnHash) {
                            // Reused
                            entry.ops_reused += 1;
                            res.collection_val.push_back(CollectionItem{item.id, memo_it->second.out});
                            continue;
                        }
                    }

                    // Recomputed
                    entry.ops_recomputed += 1;
                    Value out_val = VM::execute(prog, {item.value}, stage_values_);
                    if (enable_memo_) {
                        node.memo.map_memo[item.id] = MapMemoEntry{in_hash, node.fnHash, out_val};
                    }
                    res.collection_val.push_back(CollectionItem{item.id, std::move(out_val)});
                }
            }
            break;
        }

        case NodeKind::FILTER: {
            // FILTER: 1 input collection. Invariant I7: keep order and RecordIds
            const auto& input_res = results_[node.inputs[0]];
            res.is_collection = true;
            if (node.fn.has_value()) {
                BytecodeCompiler compiler(node.fn->params);
                auto prog = compiler.compile(node.fn->body.get());
                for (const auto& item : input_res.collection_val) {
                    uint64_t in_hash = item.value.hash();
                    bool keep = false;
                    // Invariant I3: check memo
                    if (enable_memo_) {
                        auto memo_it = node.memo.filter_memo.find(item.id);
                        if (memo_it != node.memo.filter_memo.end() &&
                            memo_it->second.inHash == in_hash &&
                            memo_it->second.fnHash == node.fnHash) {
                            entry.ops_reused += 1;
                            keep = memo_it->second.keep;
                            if (keep) {
                                res.collection_val.push_back(item);
                            }
                            continue;
                        }
                    }

                    // Recomputed
                    entry.ops_recomputed += 1;
                    Value keep_val = VM::execute(prog, {item.value}, stage_values_);
                    keep = keep_val.bool_val;
                    if (enable_memo_) {
                        node.memo.filter_memo[item.id] = FilterMemoEntry{in_hash, node.fnHash, keep};
                    }
                    if (keep) {
                        res.collection_val.push_back(item);
                    }
                }
            }
            break;
        }

        case NodeKind::REDUCE: {
            // REDUCE: 1 input collection, initial expr, fold lambda (a, x) => ...
            const auto& input_res = results_[node.inputs[0]];
            res.is_collection = false;

            uint64_t input_hash = input_res.as_value().hash();
            uint64_t init_hash = node.init ? hash_expr_ir(node.init.get()) : 0;

            // Invariant I3: check memo for REDUCE
            if (enable_memo_ && node.memo.reduce_memo.valid &&
                node.memo.reduce_memo.inputHash == input_hash &&
                node.memo.reduce_memo.fnHash == node.fnHash &&
                node.memo.reduce_memo.initHash == init_hash) {
                entry.ops_reused += input_res.collection_val.size();
                res.scalar_val = node.memo.reduce_memo.result;
            } else {
                // Evaluate init expr
                BytecodeCompiler init_comp;
                auto init_prog = init_comp.compile(node.init.get());
                Value acc = VM::execute(init_prog, {}, stage_values_);

                if (node.fn.has_value()) {
                    BytecodeCompiler fn_comp(node.fn->params);
                    auto fn_prog = fn_comp.compile(node.fn->body.get());
                    for (const auto& item : input_res.collection_val) {
                        entry.ops_recomputed += 1;
                        acc = VM::execute(fn_prog, {acc, item.value}, stage_values_);
                    }
                }
                res.scalar_val = acc;
                if (enable_memo_) {
                    node.memo.reduce_memo = ReduceMemoEntry{input_hash, node.fnHash, init_hash, acc, true};
                }
            }
            break;
        }

        case NodeKind::TREE_REDUCE: {
            const auto& input_res = results_[node.inputs[0]];
            res.is_collection = false;

            uint64_t input_hash = input_res.as_value().hash();
            uint64_t init_hash = node.init ? hash_expr_ir(node.init.get()) : 0;

            if (enable_memo_ && node.memo.tree_reduce_memo.valid &&
                node.memo.tree_reduce_memo.inputHash == input_hash &&
                node.memo.tree_reduce_memo.fnHash == node.fnHash &&
                node.memo.tree_reduce_memo.initHash == init_hash) {
                entry.ops_reused += node.memo.tree_reduce_memo.num_leaves > 0 ? (node.memo.tree_reduce_memo.num_leaves - 1) : 1;
                res.scalar_val = node.memo.tree_reduce_memo.result;
                break;
            }

            BytecodeCompiler init_comp;
            auto init_prog = init_comp.compile(node.init.get());
            Value init_val = VM::execute(init_prog, {}, stage_values_);

            const auto& col = input_res.collection_val;
            size_t n = col.size();

            if (n == 0) {
                res.scalar_val = init_val;
                if (enable_memo_) {
                    TreeReduceMemo memo;
                    memo.inputHash = input_hash;
                    memo.fnHash = node.fnHash;
                    memo.initHash = init_hash;
                    memo.result = init_val;
                    memo.valid = true;
                    node.memo.tree_reduce_memo = std::move(memo);
                }
                break;
            }

            BytecodeCompiler fn_comp(node.fn->params);
            auto fn_prog = fn_comp.compile(node.fn->body.get());

            if (n == 1) {
                entry.ops_recomputed += 1;
                Value out = VM::execute(fn_prog, {init_val, col[0].value}, stage_values_);
                res.scalar_val = out;
                if (enable_memo_) {
                    TreeReduceMemo memo;
                    memo.tree = {Value(), out};
                    memo.active = {false, true};
                    memo.record_to_leaf[col[0].id] = 1;
                    memo.leaf_offset = 1;
                    memo.num_leaves = 1;
                    memo.inputHash = input_hash;
                    memo.fnHash = node.fnHash;
                    memo.initHash = init_hash;
                    memo.result = out;
                    memo.valid = true;
                    node.memo.tree_reduce_memo = std::move(memo);
                }
                break;
            }

            size_t m = 1;
            while (m < n) m <<= 1;

            TreeReduceMemo memo;
            memo.leaf_offset = m;
            memo.num_leaves = n;
            memo.tree.resize(2 * m);
            memo.active.resize(2 * m, false);

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
                    entry.ops_recomputed += 1;
                    memo.tree[i] = VM::execute(fn_prog, {memo.tree[left_child], memo.tree[right_child]}, stage_values_);
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
            if (enable_memo_) {
                memo.inputHash = input_hash;
                memo.fnHash = node.fnHash;
                memo.initHash = init_hash;
                memo.result = memo.tree[1];
                memo.valid = true;
                node.memo.tree_reduce_memo = std::move(memo);
            }
            break;
        }

        case NodeKind::JOIN: {
            // JOIN: 2 input collections, left key lambda, right key lambda
            // Output ordered by left order, then right order
            // Record fields: { left: A, right: B }
            const auto& left_res = results_[node.inputs[0]];
            const auto& right_res = results_[node.inputs[1]];
            res.is_collection = true;

            uint64_t ka_hash = node.fn ? hash_lambda_ir(*node.fn) : 0;
            uint64_t kb_hash = node.join_fn_b ? hash_lambda_ir(*node.join_fn_b) : 0;

            BytecodeCompiler ka_comp(node.fn->params);
            auto ka_prog = ka_comp.compile(node.fn->body.get());

            BytecodeCompiler kb_comp(node.join_fn_b->params);
            auto kb_prog = kb_comp.compile(node.join_fn_b->body.get());

            // Build index on right side keys
            std::vector<std::pair<Value, const CollectionItem*>> right_keys;
            right_keys.reserve(right_res.collection_val.size());
            for (const auto& r_item : right_res.collection_val) {
                Value key = VM::execute(kb_prog, {r_item.value}, stage_values_);
                entry.ops_recomputed += 1;
                right_keys.push_back({std::move(key), &r_item});
            }

            RecordId out_id = 0;
            for (const auto& l_item : left_res.collection_val) {
                Value l_key = VM::execute(ka_prog, {l_item.value}, stage_values_);
                entry.ops_recomputed += 1;
                uint64_t l_hash = l_item.value.hash();

                for (const auto& [r_key, r_item_ptr] : right_keys) {
                    JoinPairKey pair_key{l_item.id, r_item_ptr->id};
                    uint64_t r_hash = r_item_ptr->value.hash();

                    if (enable_memo_) {
                        auto memo_it = node.memo.join_memo.find(pair_key);
                        if (memo_it != node.memo.join_memo.end() &&
                            memo_it->second.leftHash == l_hash &&
                            memo_it->second.rightHash == r_hash &&
                            memo_it->second.kaHash == ka_hash &&
                            memo_it->second.kbHash == kb_hash) {
                            if (memo_it->second.matched) {
                                res.collection_val.push_back(CollectionItem{out_id++, memo_it->second.out});
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
                        res.collection_val.push_back(CollectionItem{out_id++, out_record});
                    }
                    if (enable_memo_) {
                        node.memo.join_memo[pair_key] = JoinMemoEntry{
                            l_hash, r_hash, ka_hash, kb_hash, matched, std::move(out_record)
                        };
                    }
                }
            }
            break;
        }

        case NodeKind::SCALAR: {
            res.is_collection = false;
            // Compute combined hash of input stage values
            uint64_t inputs_hash = 14695981039346656037ULL;
            for (const auto& in_id : node.inputs) {
                inputs_hash ^= results_[in_id].as_value().hash();
                inputs_hash *= 1099511628211ULL;
            }

            if (enable_memo_ && node.memo.scalar_memo.valid &&
                node.memo.scalar_memo.inputsHash == inputs_hash &&
                node.memo.scalar_memo.fnHash == node.fnHash) {
                entry.ops_reused += 1;
                res.scalar_val = node.memo.scalar_memo.result;
            } else {
                entry.ops_recomputed += 1;
                BytecodeCompiler comp;
                auto prog = comp.compile(node.expr.get());
                res.scalar_val = VM::execute(prog, {}, stage_values_);
                if (enable_memo_) {
                    node.memo.scalar_memo = ScalarMemoEntry{inputs_hash, node.fnHash, res.scalar_val, true};
                }
            }
            break;
        }

        case NodeKind::OUTPUT: {
            // OUTPUT: 1 input stage
            const auto& in_res = results_[node.inputs[0]];
            res = in_res;
            std::string out_str = res.as_value().to_string();
            outputs_.push_back({node.name, out_str});
            entry.outputs_changed.push_back(node.name);
            break;
        }
    }

    // Invariant I4: version increases iff outHash changes
    uint64_t new_out_hash = res.as_value().hash();
    if (new_out_hash != node.outHash || node.version == 0) {
        node.version += 1;
        node.outHash = new_out_hash;
    }
    // Invariant I5: after execution/propagation, status is CLEAN
    node.status = NodeStatus::CLEAN;

    // Save stage value for downstream stages that refer to this stage by name
    stage_values_[node.name] = res.as_value();
    results_[node.id] = std::move(res);
}

} // namespace retroc
