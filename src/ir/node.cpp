#include "ir/node.hpp"

namespace retroc {

const char* node_kind_to_string(NodeKind kind) {
    switch (kind) {
        case NodeKind::INPUT:  return "INPUT";
        case NodeKind::MAP:    return "MAP";
        case NodeKind::FILTER: return "FILTER";
        case NodeKind::REDUCE:      return "REDUCE";
        case NodeKind::TREE_REDUCE: return "TREE_REDUCE";
        case NodeKind::JOIN:        return "JOIN";
        case NodeKind::SCALAR: return "SCALAR";
        case NodeKind::OUTPUT: return "OUTPUT";
    }
    return "?";
}

const char* node_status_to_string(NodeStatus status) {
    switch (status) {
        case NodeStatus::CLEAN:   return "CLEAN";
        case NodeStatus::DIRTY:   return "DIRTY";
        case NodeStatus::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}

size_t Memo::bytes() const {
    size_t total = 0;
    // Map entries: RecordId (8) + MapMemoEntry (~32-48 bytes + hash table node overhead)
    total += map_memo.size() * (sizeof(RecordId) + sizeof(MapMemoEntry) + 32);
    // Filter entries: RecordId (8) + FilterMemoEntry (~24 bytes + hash table node overhead)
    total += filter_memo.size() * (sizeof(RecordId) + sizeof(FilterMemoEntry) + 32);
    // Join entries: JoinPairKey (16) + JoinMemoEntry (~56 bytes + hash table node overhead)
    total += join_memo.size() * (sizeof(JoinPairKey) + sizeof(JoinMemoEntry) + 32);
    if (reduce_memo.valid) total += sizeof(ReduceMemoEntry);
    if (tree_reduce_memo.valid) {
        total += sizeof(TreeReduceMemo) +
                 tree_reduce_memo.tree.capacity() * sizeof(Value) +
                 tree_reduce_memo.record_to_leaf.size() * (sizeof(RecordId) + sizeof(size_t) + 32);
    }
    if (scalar_memo.valid) total += sizeof(ScalarMemoEntry);
    if (sum_memo.valid) {
        total += sizeof(SumReduceMemo) + sum_memo.terms.size() * (sizeof(RecordId) + sizeof(int64_t) + 32);
    }
    total += (join_left_keys.size() + join_right_keys.size()) *
             (sizeof(RecordId) + sizeof(JoinKeyEntry) + 32);
    total += join_pairs.size() * (sizeof(std::pair<RecordId, RecordId>) + 2 * sizeof(size_t)) +
             (join_left_pos.size() + join_right_pos.size()) * (sizeof(RecordId) + 32);
    return total;
}

namespace {

bool mentions_param_or_stage(const ExprIR* e, const std::string& param) {
    if (!e) return false;
    switch (e->kind) {
        case ExprIRKind::CONST:
            return false;
        case ExprIRKind::VAR_PARAM:
            return static_cast<const VarParamExprIR*>(e)->name == param;
        case ExprIRKind::VAR_STAGE:
            return true;
        case ExprIRKind::FIELD:
            return mentions_param_or_stage(static_cast<const FieldExprIR*>(e)->object.get(), param);
        case ExprIRKind::BINARY: {
            auto b = static_cast<const BinaryExprIR*>(e);
            return mentions_param_or_stage(b->left.get(), param) ||
                   mentions_param_or_stage(b->right.get(), param);
        }
        case ExprIRKind::UNARY:
            return mentions_param_or_stage(static_cast<const UnaryExprIR*>(e)->operand.get(), param);
        case ExprIRKind::IF: {
            auto i = static_cast<const IfExprIR*>(e);
            return mentions_param_or_stage(i->cond.get(), param) ||
                   mentions_param_or_stage(i->then_branch.get(), param) ||
                   mentions_param_or_stage(i->else_branch.get(), param);
        }
        case ExprIRKind::CALL_BUILTIN:
            for (const auto& a : static_cast<const CallBuiltinExprIR*>(e)->args) {
                if (mentions_param_or_stage(a.get(), param)) return true;
            }
            return false;
        case ExprIRKind::RECORD_LIT:
            for (const auto& f : static_cast<const RecordLitExprIR*>(e)->fields) {
                if (mentions_param_or_stage(f.value.get(), param)) return true;
            }
            return false;
        case ExprIRKind::ARRAY_LIT:
            for (const auto& el : static_cast<const ArrayLitExprIR*>(e)->elements) {
                if (mentions_param_or_stage(el.get(), param)) return true;
            }
            return false;
    }
    return true;
}

bool is_param(const ExprIR* e, const std::string& name) {
    return e && e->kind == ExprIRKind::VAR_PARAM &&
           static_cast<const VarParamExprIR*>(e)->name == name;
}

} // namespace

size_t collection_arity(NodeKind kind) {
    switch (kind) {
        case NodeKind::MAP:
        case NodeKind::FILTER:
        case NodeKind::REDUCE:
        case NodeKind::TREE_REDUCE:
        case NodeKind::OUTPUT:
            return 1;
        case NodeKind::JOIN:
            return 2;
        case NodeKind::INPUT:
        case NodeKind::SCALAR:
            return 0;
    }
    return 0;
}

bool reads_by_name(const Node& consumer, const NodeId& producer) {
    for (size_t i = collection_arity(consumer.kind); i < consumer.inputs.size(); ++i) {
        if (consumer.inputs[i] == producer) return true;
    }
    return false;
}

const ExprIR* additive_reduce_term(const Node& node) {
    if (node.kind != NodeKind::REDUCE || !node.fn || !node.init) return nullptr;
    const LambdaIR& fn = *node.fn;
    if (fn.params.size() != 2 || !fn.body || fn.body->kind != ExprIRKind::BINARY) return nullptr;
    // Integer addition wraps (LANGUAGE_SPEC), so it is associative, commutative
    // and invertible; float addition is not, and is never patched.
    if (!fn.body->type || fn.body->type->kind != TypeKind::INT) return nullptr;
    if (!node.init->type || node.init->type->kind != TypeKind::INT) return nullptr;
    auto body = static_cast<const BinaryExprIR*>(fn.body.get());
    if (body->op != BinaryOp::ADD) return nullptr;
    const std::string& acc = fn.params[0];
    const ExprIR* term = nullptr;
    if (is_param(body->left.get(), acc)) {
        term = body->right.get();
    } else if (is_param(body->right.get(), acc)) {
        term = body->left.get();
    } else {
        return nullptr;
    }
    // The term must depend on the record alone. Stage references are excluded
    // because a change to the referenced stage would not reach the stored terms.
    if (mentions_param_or_stage(term, acc)) return nullptr;
    return term;
}

} // namespace retroc
