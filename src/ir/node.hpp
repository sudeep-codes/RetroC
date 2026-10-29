#pragma once

#include "ir/expr_ir.hpp"
#include "common/value.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <unordered_map>

namespace retroc {

using NodeId = std::string; // e.g. "n0", "n1", ...

enum class NodeKind {
    INPUT,
    MAP,
    FILTER,
    REDUCE,
    TREE_REDUCE,
    JOIN,
    SCALAR,
    OUTPUT
};

const char* node_kind_to_string(NodeKind kind);

enum class NodeStatus {
    CLEAN,
    DIRTY,
    UNKNOWN
};

const char* node_status_to_string(NodeStatus status);

// Memo structures per IR_SPEC section 4 and ARCHITECTURE.md section 5:
// MAP: RecordId -> {inHash, fnHash, out}
struct MapMemoEntry {
    uint64_t inHash = 0;
    uint64_t fnHash = 0;
    Value out;
};

// FILTER: RecordId -> {inHash, fnHash, keep}
struct FilterMemoEntry {
    uint64_t inHash = 0;
    uint64_t fnHash = 0;
    bool keep = false;
};

// REDUCE: {inputHash, fnHash, initHash, result}
struct ReduceMemoEntry {
    uint64_t inputHash = 0;
    uint64_t fnHash = 0;
    uint64_t initHash = 0;
    Value result;
    bool valid = false;
};

// TREE_REDUCE: complete binary reduction tree for associative combiners
struct TreeReduceMemo {
    std::vector<Value> tree; // 1-indexed, size 2*M, where tree[1] is root
    std::vector<bool> active; // active[node] indicates if node has value
    std::unordered_map<RecordId, size_t> record_to_leaf; // RecordId -> tree index (leaf)
    size_t leaf_offset = 0; // M
    size_t num_leaves = 0;
    uint64_t inputHash = 0;
    uint64_t fnHash = 0;
    uint64_t initHash = 0;
    Value result;
    bool valid = false;
};

// SCALAR: {inputsHash, fnHash, result}
struct ScalarMemoEntry {
    uint64_t inputsHash = 0;
    uint64_t fnHash = 0;
    Value result;
    bool valid = false;
};

// JOIN: key index on both sides & pair-level memo
struct JoinPairKey {
    RecordId leftId;
    RecordId rightId;

    bool operator==(const JoinPairKey& other) const {
        return leftId == other.leftId && rightId == other.rightId;
    }
};

struct JoinPairHash {
    size_t operator()(const JoinPairKey& k) const {
        return (static_cast<size_t>(k.leftId) << 32) ^ static_cast<size_t>(k.rightId);
    }
};

struct JoinMemoEntry {
    uint64_t leftHash = 0;
    uint64_t rightHash = 0;
    uint64_t kaHash = 0;
    uint64_t kbHash = 0;
    bool matched = false;
    Value out;
};

// JOIN key index: RecordId -> {inHash, key fn hash, key}, one table per side
struct JoinKeyEntry {
    uint64_t inHash = 0;
    uint64_t keyFnHash = 0;
    Value key;
    uint64_t layoutGen = 0;    // the join layout this record was part of
};

// Additive REDUCE (Phase 12.2): an int fold (a, x) => a + t(x) equals
// init + sum of t(x) with wraparound, so one changed record is patched by
// swapping its term. Stores each record's term t(x).
struct SumReduceMemo {
    // (RecordId, t(x)) sorted by RecordId, like the input collection (I7), so
    // a full fold fills it with push_back and a patch finds a record by
    // binary search
    std::vector<std::pair<RecordId, int64_t>> terms;
    uint64_t fnHash = 0;
    uint64_t initHash = 0;
    int64_t init = 0;
    uint64_t sum = 0;          // sum of terms mod 2^64
    bool valid = false;
};

// Unified Memo table for a node
struct Memo {
    std::unordered_map<RecordId, MapMemoEntry> map_memo;
    std::unordered_map<RecordId, FilterMemoEntry> filter_memo;
    ReduceMemoEntry reduce_memo;
    SumReduceMemo sum_memo;
    TreeReduceMemo tree_reduce_memo;
    ScalarMemoEntry scalar_memo;
    std::unordered_map<JoinPairKey, JoinMemoEntry, JoinPairHash> join_memo;
    std::unordered_map<RecordId, JoinKeyEntry> join_left_keys;
    std::unordered_map<RecordId, JoinKeyEntry> join_right_keys;
    // JOIN output layout: the (left, right) RecordIds at each output position
    // and, per input record, the positions it appears in. Lets an edit that
    // keeps every key patch the affected output records in place.
    std::vector<std::pair<RecordId, RecordId>> join_pairs;
    std::unordered_map<RecordId, std::vector<size_t>> join_left_pos;
    std::unordered_map<RecordId, std::vector<size_t>> join_right_pos;
    bool join_layout_valid = false;
    uint64_t join_layout_gen = 0;  // bumped by every full join

    // Number of recorded entries
    size_t size() const {
        if (!map_memo.empty()) return map_memo.size();
        if (!filter_memo.empty()) return filter_memo.size();
        if (!join_left_keys.empty() || !join_right_keys.empty()) {
            return join_left_keys.size() + join_right_keys.size() + join_memo.size();
        }
        if (tree_reduce_memo.valid) return tree_reduce_memo.tree.size();
        if (sum_memo.valid) return sum_memo.terms.size();
        if (reduce_memo.valid) return 1;
        if (scalar_memo.valid) return 1;
        return 0;
    }

    void clear() {
        map_memo.clear();
        filter_memo.clear();
        reduce_memo = ReduceMemoEntry{};
        sum_memo = SumReduceMemo{};
        tree_reduce_memo = TreeReduceMemo{};
        scalar_memo = ScalarMemoEntry{};
        join_memo.clear();
        join_left_keys.clear();
        join_right_keys.clear();
        join_pairs.clear();
        join_left_pos.clear();
        join_right_pos.clear();
        join_layout_valid = false;
    }

    // Memory accounting in bytes
    size_t bytes() const;
};

struct Node {
    NodeId id;                    // unique, never reused, even after deletion
    std::string name;             // stage name from source; inserted stages get names from edit
    NodeKind kind;                // INPUT | MAP | FILTER | REDUCE | JOIN | SCALAR | OUTPUT
    std::vector<NodeId> inputs;   // ordered input NodeIds
    
    // For MAP, FILTER: fn is the single lambda (r => ...)
    // For REDUCE: fn is the fold lambda ((a, x) => ...)
    // For JOIN: fn is left key lambda, join_fn_b is right key lambda
    std::optional<LambdaIR> fn;
    std::optional<LambdaIR> join_fn_b; // for JOIN right key lambda: o => o.uid
    
    ExprIRPtr init;               // REDUCE initial value
    ExprIRPtr expr;               // SCALAR expression, or INPUT inline value
    std::string from_file;        // INPUT CSV filename if from file

    TypePtr type;                 // result type of this stage
    uint64_t fnHash = 0;          // structural hash of fn/init/expr
    uint64_t version = 0;         // incremented each time this node's output value changes
    uint64_t outHash = 0;         // hash of current output value
    NodeStatus status = NodeStatus::CLEAN;
    float cost = 1.0f;            // estimated cost units per record evaluation
    Memo memo;                    // per-record table
    // I3 memo key for the current run: fnHash mixed with the output hashes of
    // the stages its expressions read by name (inputs past the collection
    // inputs). Set by the executor/propagator right before the node runs.
    uint64_t memoKey = 0;

    Node clone() const {
        Node n;
        n.id = id;
        n.name = name;
        n.kind = kind;
        n.inputs = inputs;
        if (fn) n.fn = fn->clone();
        if (join_fn_b) n.join_fn_b = join_fn_b->clone();
        if (init) n.init = init->clone();
        if (expr) n.expr = expr->clone();
        n.from_file = from_file;
        n.type = type;
        n.fnHash = fnHash;
        n.version = version;
        n.outHash = outHash;
        n.status = status;
        n.cost = cost;
        n.memo = memo;
        n.memoKey = memoKey;
        return n;
    }
};

// Number of leading inputs that are collections the node iterates over
// (MAP/FILTER/REDUCE/TREE_REDUCE: 1, JOIN: 2, OUTPUT: 1). Later inputs are
// stages read by name inside fn/init/expr (IR_SPEC section 4); every input of
// a SCALAR is such a reference.
size_t collection_arity(NodeKind kind);

// True if consumer reads producer by name (a reference input, not an
// iterated collection). A change to such a producer can change every record.
bool reads_by_name(const Node& consumer, const NodeId& producer);

// For a REDUCE whose fold is (a, x) => a + t(x) or t(x) + a over ints, with t
// not reading `a` or any stage, returns t; otherwise nullptr.
const ExprIR* additive_reduce_term(const Node& node);

} // namespace retroc
