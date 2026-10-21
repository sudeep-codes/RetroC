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

// Unified Memo table for a node
struct Memo {
    std::unordered_map<RecordId, MapMemoEntry> map_memo;
    std::unordered_map<RecordId, FilterMemoEntry> filter_memo;
    ReduceMemoEntry reduce_memo;
    ScalarMemoEntry scalar_memo;
    std::unordered_map<JoinPairKey, JoinMemoEntry, JoinPairHash> join_memo;

    // Number of recorded entries
    size_t size() const {
        if (!map_memo.empty()) return map_memo.size();
        if (!filter_memo.empty()) return filter_memo.size();
        if (!join_memo.empty()) return join_memo.size();
        if (reduce_memo.valid) return 1;
        if (scalar_memo.valid) return 1;
        return 0;
    }

    void clear() {
        map_memo.clear();
        filter_memo.clear();
        reduce_memo = ReduceMemoEntry{};
        scalar_memo = ScalarMemoEntry{};
        join_memo.clear();
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
        return n;
    }
};

} // namespace retroc
