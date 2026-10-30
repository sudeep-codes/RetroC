#include "runtime/executor.hpp"
#include "runtime/csv.hpp"
#include "bytecode/bytecode.hpp"
#include <iostream>
#include <filesystem>

namespace retroc {

static void scan_stage_refs(const ExprIR* expr, const std::vector<std::string>& params,
                            StageRefMap& refs) {
    if (!expr) return;
    BytecodeCompiler compiler(params);
    BytecodeProgram prog = compiler.compile(expr);
    for (size_t i = 0; i < prog.code.size(); ++i) {
        if (prog.code[i].op != Opcode::LOAD_STAGE) continue;
        const std::string& name = prog.stage_names[prog.code[i].arg];
        bool length_only = (i + 1 < prog.code.size() &&
                            prog.code[i + 1].op == Opcode::BUILTIN_LENGTH_ARRAY);
        auto& use = refs[name];
        if (!length_only) {
            use = StageRefUse::FULL;
        } else if (use == StageRefUse::NONE) {
            use = StageRefUse::LENGTH_ONLY;
        }
    }
}

StageRefMap analyze_stage_refs(const Graph& graph) {
    StageRefMap refs;
    for (const auto& [id, node] : graph.nodes) {
        if (node.fn) scan_stage_refs(node.fn->body.get(), node.fn->params, refs);
        if (node.join_fn_b) scan_stage_refs(node.join_fn_b->body.get(), node.join_fn_b->params, refs);
        scan_stage_refs(node.init.get(), {}, refs);
        scan_stage_refs(node.expr.get(), {}, refs);
    }
    return refs;
}

void store_stage_value(std::unordered_map<std::string, Value>& stage_values,
                       const StageRefMap& refs, const std::string& name,
                       const NodeExecutionResult& res) {
    if (!res.is_collection) {
        stage_values[name] = res.scalar_val;
        return;
    }
    auto it = refs.find(name);
    if (it == refs.end() || it->second == StageRefUse::NONE) {
        return;
    }
    // LENGTH_ONLY: BUILTIN_LENGTH_ARRAY reads the size from int_val, so we
    // avoid copying every record just to answer length(c).
    stage_values[name] = (it->second == StageRefUse::FULL) ? res.as_value() : res.as_stage_value();
}

namespace {

// Key of one input record: from the key index when its input hash and key
// function are unchanged (I3), otherwise by running the key lambda.
Value join_key(const BytecodeProgram& prog, const CollectionItem& item, uint64_t key_fn_hash,
               std::unordered_map<RecordId, JoinKeyEntry>& index, const StageValues& stage_values,
               bool use_memo, uint64_t gen, size_t& evals) {
    uint64_t in_hash = item.value.hash();
    if (use_memo) {
        auto it = index.find(item.id);
        if (it != index.end() && it->second.inHash == in_hash && it->second.keyFnHash == key_fn_hash) {
            it->second.layoutGen = gen;
            return it->second.key;
        }
    }
    evals++;
    Value key = VM::execute(prog, &item.value, 1, stage_values);
    if (use_memo) {
        index[item.id] = JoinKeyEntry{in_hash, key_fn_hash, key, gen};
    }
    return key;
}

} // namespace

uint64_t compute_memo_key(const Node& node,
                          const std::unordered_map<NodeId, NodeExecutionResult>& results) {
    uint64_t key = node.fnHash;
    for (size_t i = collection_arity(node.kind); i < node.inputs.size(); ++i) {
        auto it = results.find(node.inputs[i]);
        uint64_t h = (it == results.end()) ? 0 : it->second.hash();
        key ^= h + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2);
    }
    return key;
}

size_t execute_join(Node& node, const Collection& left, const Collection& right,
                    NodeExecutionResult& res, const StageValues& stage_values, bool use_memo) {
    // Stages the key lambdas read by name are part of every key's memo key
    uint64_t refs = node.memoKey ^ node.fnHash;
    uint64_t ka_hash = (node.fn ? hash_lambda_ir(*node.fn) : 0) ^ refs;
    uint64_t kb_hash = (node.join_fn_b ? hash_lambda_ir(*node.join_fn_b) : 0) ^ refs;
    BytecodeCompiler ka_comp(node.fn->params);
    auto ka_prog = ka_comp.compile(node.fn->body.get());
    BytecodeCompiler kb_comp(node.join_fn_b->params);
    auto kb_prog = kb_comp.compile(node.join_fn_b->body.get());

    size_t evals = 0;
    // Key entries stamped with this generation are the records of this layout
    uint64_t gen = use_memo ? ++node.memo.join_layout_gen : 0;
    std::vector<Value> right_keys;
    right_keys.reserve(right.size());
    // Buckets keep right order, so matches come out in right order per left record
    std::unordered_map<uint64_t, std::vector<size_t>> buckets;
    buckets.reserve(right.size());
    for (size_t j = 0; j < right.size(); ++j) {
        right_keys.push_back(join_key(kb_prog, right[j], kb_hash, node.memo.join_right_keys,
                                      stage_values, use_memo, gen, evals));
        buckets[right_keys.back().equality_hash()].push_back(j);
    }

    Collection out;
    RecordId out_id = 0;
    auto& memo = node.memo;
    if (use_memo) {
        memo.join_pairs.clear();
        memo.join_left_pos.clear();
        memo.join_right_pos.clear();
    }
    // Records the layout of the output record about to be appended
    auto note_pair = [&](RecordId l, RecordId r) {
        if (!use_memo) return;
        memo.join_left_pos[l].push_back(memo.join_pairs.size());
        memo.join_right_pos[r].push_back(memo.join_pairs.size());
        memo.join_pairs.emplace_back(l, r);
    };
    for (const auto& l_item : left) {
        Value l_key = join_key(ka_prog, l_item, ka_hash, node.memo.join_left_keys,
                               stage_values, use_memo, gen, evals);
        auto b = buckets.find(l_key.equality_hash());
        if (b == buckets.end()) continue;
        uint64_t l_hash = l_item.value.hash();
        for (size_t j : b->second) {
            if (!(l_key == right_keys[j])) continue; // hash collision
            const CollectionItem& r_item = right[j];
            uint64_t r_hash = r_item.value.hash();
            JoinPairKey pair_key{l_item.id, r_item.id};
            if (use_memo) {
                auto m = node.memo.join_memo.find(pair_key);
                if (m != node.memo.join_memo.end() && m->second.leftHash == l_hash &&
                    m->second.rightHash == r_hash && m->second.kaHash == ka_hash &&
                    m->second.kbHash == kb_hash) {
                    note_pair(l_item.id, r_item.id);
                    out.push_back(CollectionItem{out_id++, m->second.out});
                    continue;
                }
            }
            std::vector<RecordField> fields = {
                {"left", std::make_shared<Value>(l_item.value)},
                {"right", std::make_shared<Value>(r_item.value)}
            };
            Value record = Value::make_record(std::move(fields));
            if (use_memo) {
                // Only matched pairs are stored: unmatched pairs need no output
                node.memo.join_memo[pair_key] = JoinMemoEntry{l_hash, r_hash, ka_hash, kb_hash, true, record};
            }
            note_pair(l_item.id, r_item.id);
            out.push_back(CollectionItem{out_id++, std::move(record)});
        }
    }
    memo.join_layout_valid = use_memo;
    res.is_collection = true;
    res.collection_val = std::move(out);
    res.rehash_collection();
    return evals;
}

size_t execute_additive_reduce(Node& node, const ExprIR* term, const Collection& in,
                               NodeExecutionResult& res, const StageValues& stage_values,
                               bool store_memo) {
    BytecodeCompiler init_comp;
    auto init_prog = init_comp.compile(node.init.get());
    int64_t init = VM::execute(init_prog, {}, stage_values).int_val;
    BytecodeCompiler term_comp(node.fn->params);
    auto term_prog = term_comp.compile(term);

    SumReduceMemo memo;
    if (store_memo) memo.terms.reserve(in.size());
    uint64_t sum = 0;
    // The term never reads the accumulator, so its slot is a placeholder
    Value args[2] = {Value::make_int(0), Value()};
    for (const auto& item : in) {
        args[1] = item.value;
        int64_t t = VM::execute(term_prog, args, 2, stage_values).int_val;
        sum += static_cast<uint64_t>(t); // wraparound, as int addition (LANGUAGE_SPEC)
        if (store_memo) memo.terms.emplace_back(item.id, t);
    }
    res.is_collection = false;
    res.scalar_val = Value::make_int(static_cast<int64_t>(static_cast<uint64_t>(init) + sum));
    if (store_memo) {
        memo.fnHash = node.memoKey;
        memo.initHash = hash_expr_ir(node.init.get());
        memo.init = init;
        memo.sum = sum;
        memo.valid = true;
        node.memo.sum_memo = std::move(memo);
    }
    return in.size();
}

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
    stage_refs_ = analyze_stage_refs(graph_);

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
    node.memoKey = compute_memo_key(node, results_);

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
                            memo_it->second.fnHash == node.memoKey) {
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
                        node.memo.map_memo[item.id] = MapMemoEntry{in_hash, node.memoKey, out_val};
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
                            memo_it->second.fnHash == node.memoKey) {
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
                        node.memo.filter_memo[item.id] = FilterMemoEntry{in_hash, node.memoKey, keep};
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

            uint64_t input_hash = input_res.hash();
            uint64_t init_hash = node.init ? hash_expr_ir(node.init.get()) : 0;

            // Invariant I3: check memo for REDUCE
            if (enable_memo_ && node.memo.reduce_memo.valid &&
                node.memo.reduce_memo.inputHash == input_hash &&
                node.memo.reduce_memo.fnHash == node.memoKey &&
                node.memo.reduce_memo.initHash == init_hash) {
                entry.ops_reused += input_res.collection_val.size();
                res.scalar_val = node.memo.reduce_memo.result;
            } else if (const ExprIR* term = additive_reduce_term(node)) {
                // Phase 12.2: record per-record terms so edits can patch the sum
                entry.ops_recomputed += execute_additive_reduce(node, term, input_res.collection_val,
                                                                res, stage_values_, enable_memo_);
                if (enable_memo_) {
                    node.memo.reduce_memo = ReduceMemoEntry{input_hash, node.memoKey, init_hash,
                                                            res.scalar_val, true};
                }
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
                    node.memo.reduce_memo = ReduceMemoEntry{input_hash, node.memoKey, init_hash, acc, true};
                }
            }
            break;
        }

        case NodeKind::TREE_REDUCE: {
            const auto& input_res = results_[node.inputs[0]];
            res.is_collection = false;

            uint64_t input_hash = input_res.hash();
            uint64_t init_hash = node.init ? hash_expr_ir(node.init.get()) : 0;

            if (enable_memo_ && node.memo.tree_reduce_memo.valid &&
                node.memo.tree_reduce_memo.inputHash == input_hash &&
                node.memo.tree_reduce_memo.fnHash == node.memoKey &&
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
                    memo.fnHash = node.memoKey;
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
                    memo.fnHash = node.memoKey;
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
                memo.fnHash = node.memoKey;
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
            entry.ops_recomputed += execute_join(node, left_res.collection_val, right_res.collection_val,
                                                 res, stage_values_, enable_memo_);
            break;
        }

        case NodeKind::SCALAR: {
            res.is_collection = false;
            // Compute combined hash of input stage values
            uint64_t inputs_hash = 14695981039346656037ULL;
            for (const auto& in_id : node.inputs) {
                inputs_hash ^= results_[in_id].hash();
                inputs_hash *= 1099511628211ULL;
            }

            if (enable_memo_ && node.memo.scalar_memo.valid &&
                node.memo.scalar_memo.inputsHash == inputs_hash &&
                node.memo.scalar_memo.fnHash == node.memoKey) {
                entry.ops_reused += 1;
                res.scalar_val = node.memo.scalar_memo.result;
            } else {
                entry.ops_recomputed += 1;
                BytecodeCompiler comp;
                auto prog = comp.compile(node.expr.get());
                res.scalar_val = VM::execute(prog, {}, stage_values_);
                if (enable_memo_) {
                    node.memo.scalar_memo = ScalarMemoEntry{inputs_hash, node.memoKey, res.scalar_val, true};
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

    if (res.is_collection) {
        res.rehash_collection();
    }

    // Invariant I4: version increases iff outHash changes
    uint64_t new_out_hash = res.hash();
    if (new_out_hash != node.outHash || node.version == 0) {
        node.version += 1;
        node.outHash = new_out_hash;
    }
    // Invariant I5: after execution/propagation, status is CLEAN
    node.status = NodeStatus::CLEAN;

    // Save stage value for downstream stages that refer to this stage by name.
    // OUTPUT nodes share their source stage's name and value, so skip them.
    if (node.kind != NodeKind::OUTPUT) {
        store_stage_value(stage_values_, stage_refs_, node.name, res);
    }
    results_[node.id] = std::move(res);
}

} // namespace retroc
