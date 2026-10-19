#pragma once

#include "ir/expr_ir.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

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

// Placeholder for per-record memo table (to be detailed in Phase 6)
struct Memo {
    size_t size() const { return 0; }
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
};

} // namespace retroc
