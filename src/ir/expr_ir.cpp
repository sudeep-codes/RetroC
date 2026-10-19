#include "ir/expr_ir.hpp"
#include <sstream>
#include <iomanip>

namespace retroc {

namespace {

// FNV-1a 64-bit hash
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

void hash_combine_u64(uint64_t& h, uint64_t val) {
    h ^= val;
    h *= FNV_PRIME;
}

void hash_combine_str(uint64_t& h, const std::string& str) {
    for (char c : str) {
        h ^= static_cast<uint8_t>(c);
        h *= FNV_PRIME;
    }
}

void hash_type(uint64_t& h, const Type* ty) {
    if (!ty) {
        hash_combine_u64(h, 0);
        return;
    }
    hash_combine_u64(h, static_cast<uint64_t>(ty->kind) + 1);
    if (ty->kind == TypeKind::ARRAY) {
        hash_type(h, ty->element_type.get());
    } else if (ty->kind == TypeKind::RECORD) {
        for (const auto& f : ty->fields) {
            hash_combine_str(h, f.name);
            hash_type(h, f.type.get());
        }
    }
}

void hash_expr_helper(uint64_t& h, const ExprIR* expr) {
    if (!expr) {
        hash_combine_u64(h, 0xdeadbeefULL);
        return;
    }
    hash_combine_u64(h, static_cast<uint64_t>(expr->kind));
    hash_type(h, expr->type.get());

    switch (expr->kind) {
        case ExprIRKind::CONST: {
            const auto* e = static_cast<const ConstExprIR*>(expr);
            hash_combine_u64(h, static_cast<uint64_t>(e->const_kind));
            switch (e->const_kind) {
                case ConstKind::INT:
                    hash_combine_u64(h, static_cast<uint64_t>(e->int_val));
                    break;
                case ConstKind::FLOAT:
                    hash_combine_str(h, e->float_text);
                    break;
                case ConstKind::BOOL:
                    hash_combine_u64(h, e->bool_val ? 1 : 0);
                    break;
                case ConstKind::STRING:
                    hash_combine_str(h, e->string_val);
                    break;
            }
            break;
        }
        case ExprIRKind::VAR_PARAM: {
            const auto* e = static_cast<const VarParamExprIR*>(expr);
            hash_combine_str(h, e->name);
            break;
        }
        case ExprIRKind::VAR_STAGE: {
            const auto* e = static_cast<const VarStageExprIR*>(expr);
            hash_combine_str(h, e->stage_name);
            break;
        }
        case ExprIRKind::FIELD: {
            const auto* e = static_cast<const FieldExprIR*>(expr);
            hash_combine_str(h, e->field_name);
            hash_expr_helper(h, e->object.get());
            break;
        }
        case ExprIRKind::BINARY: {
            const auto* e = static_cast<const BinaryExprIR*>(expr);
            hash_combine_u64(h, static_cast<uint64_t>(e->op));
            hash_expr_helper(h, e->left.get());
            hash_expr_helper(h, e->right.get());
            break;
        }
        case ExprIRKind::UNARY: {
            const auto* e = static_cast<const UnaryExprIR*>(expr);
            hash_combine_u64(h, static_cast<uint64_t>(e->op));
            hash_expr_helper(h, e->operand.get());
            break;
        }
        case ExprIRKind::IF: {
            const auto* e = static_cast<const IfExprIR*>(expr);
            hash_expr_helper(h, e->cond.get());
            hash_expr_helper(h, e->then_branch.get());
            hash_expr_helper(h, e->else_branch.get());
            break;
        }
        case ExprIRKind::CALL_BUILTIN: {
            const auto* e = static_cast<const CallBuiltinExprIR*>(expr);
            hash_combine_str(h, e->func_name);
            hash_combine_u64(h, e->args.size());
            for (const auto& a : e->args) {
                hash_expr_helper(h, a.get());
            }
            break;
        }
        case ExprIRKind::RECORD_LIT: {
            const auto* e = static_cast<const RecordLitExprIR*>(expr);
            hash_combine_u64(h, e->fields.size());
            for (const auto& f : e->fields) {
                hash_combine_str(h, f.name);
                hash_expr_helper(h, f.value.get());
            }
            break;
        }
        case ExprIRKind::ARRAY_LIT: {
            const auto* e = static_cast<const ArrayLitExprIR*>(expr);
            hash_combine_u64(h, e->elements.size());
            for (const auto& el : e->elements) {
                hash_expr_helper(h, el.get());
            }
            break;
        }
    }
}

} // namespace

uint64_t hash_expr_ir(const ExprIR* expr) {
    uint64_t h = FNV_OFFSET_BASIS;
    hash_expr_helper(h, expr);
    return h;
}

uint64_t hash_lambda_ir(const LambdaIR& lam) {
    uint64_t h = FNV_OFFSET_BASIS;
    hash_combine_u64(h, lam.params.size());
    for (const auto& p : lam.params) {
        hash_combine_str(h, p);
    }
    hash_expr_helper(h, lam.body.get());
    return h;
}

// Precedence levels for pretty-printing:
// 1: or
// 2: and
// 3: cmp (==, !=, <, <=, >, >=)
// 4: add, sub (+, -)
// 5: mul, div, mod (*, /, %)
// 6: unary (not, -)
// 7: postfix (field access, call)
// 8: primary (const, var, parens, literals)

static int get_binary_precedence(BinaryOp op) {
    switch (op) {
        case BinaryOp::OR: return 1;
        case BinaryOp::AND: return 2;
        case BinaryOp::EQ:
        case BinaryOp::NEQ:
        case BinaryOp::LT:
        case BinaryOp::LTE:
        case BinaryOp::GT:
        case BinaryOp::GTE: return 3;
        case BinaryOp::ADD:
        case BinaryOp::SUB: return 4;
        case BinaryOp::MUL:
        case BinaryOp::DIV:
        case BinaryOp::MOD: return 5;
    }
    return 0;
}

static void format_expr_helper(const ExprIR* expr, std::ostream& out, int parent_prec, bool is_right_operand) {
    if (!expr) return;

    switch (expr->kind) {
        case ExprIRKind::CONST: {
            const auto* e = static_cast<const ConstExprIR*>(expr);
            switch (e->const_kind) {
                case ConstKind::INT:
                    out << e->int_val;
                    break;
                case ConstKind::FLOAT:
                    out << e->float_text;
                    break;
                case ConstKind::BOOL:
                    out << (e->bool_val ? "true" : "false");
                    break;
                case ConstKind::STRING:
                    out << "\"" << e->string_val << "\"";
                    break;
            }
            break;
        }
        case ExprIRKind::VAR_PARAM: {
            const auto* e = static_cast<const VarParamExprIR*>(expr);
            out << e->name;
            break;
        }
        case ExprIRKind::VAR_STAGE: {
            const auto* e = static_cast<const VarStageExprIR*>(expr);
            out << e->stage_name;
            break;
        }
        case ExprIRKind::FIELD: {
            const auto* e = static_cast<const FieldExprIR*>(expr);
            format_expr_helper(e->object.get(), out, 7, false);
            out << "." << e->field_name;
            break;
        }
        case ExprIRKind::BINARY: {
            const auto* e = static_cast<const BinaryExprIR*>(expr);
            int prec = get_binary_precedence(e->op);
            // Need parens if parent precedence is higher, or if parent has same precedence and this is right operand
            // (since all our binary operators associate left to right)
            bool need_parens = (prec < parent_prec) || (prec == parent_prec && is_right_operand);
            if (need_parens) out << "(";
            format_expr_helper(e->left.get(), out, prec, false);
            out << " " << binary_op_to_string(e->op) << " ";
            format_expr_helper(e->right.get(), out, prec, true);
            if (need_parens) out << ")";
            break;
        }
        case ExprIRKind::UNARY: {
            const auto* e = static_cast<const UnaryExprIR*>(expr);
            int prec = 6;
            bool need_parens = (prec < parent_prec);
            if (need_parens) out << "(";
            if (e->op == UnaryOp::NEG) {
                out << "-";
            } else {
                out << "not ";
            }
            format_expr_helper(e->operand.get(), out, prec, false);
            if (need_parens) out << ")";
            break;
        }
        case ExprIRKind::IF: {
            const auto* e = static_cast<const IfExprIR*>(expr);
            // If-expression has low precedence; put parens if inside arithmetic/logical expression
            bool need_parens = (parent_prec > 0);
            if (need_parens) out << "(";
            out << "if ";
            format_expr_helper(e->cond.get(), out, 0, false);
            out << " then ";
            format_expr_helper(e->then_branch.get(), out, 0, false);
            out << " else ";
            format_expr_helper(e->else_branch.get(), out, 0, false);
            if (need_parens) out << ")";
            break;
        }
        case ExprIRKind::CALL_BUILTIN: {
            const auto* e = static_cast<const CallBuiltinExprIR*>(expr);
            out << e->func_name << "(";
            for (size_t i = 0; i < e->args.size(); ++i) {
                if (i > 0) out << ", ";
                format_expr_helper(e->args[i].get(), out, 0, false);
            }
            out << ")";
            break;
        }
        case ExprIRKind::RECORD_LIT: {
            const auto* e = static_cast<const RecordLitExprIR*>(expr);
            out << "{";
            for (size_t i = 0; i < e->fields.size(); ++i) {
                if (i > 0) out << ", ";
                out << e->fields[i].name << ": ";
                format_expr_helper(e->fields[i].value.get(), out, 0, false);
            }
            out << "}";
            break;
        }
        case ExprIRKind::ARRAY_LIT: {
            const auto* e = static_cast<const ArrayLitExprIR*>(expr);
            out << "[";
            for (size_t i = 0; i < e->elements.size(); ++i) {
                if (i > 0) out << ", ";
                format_expr_helper(e->elements[i].get(), out, 0, false);
            }
            out << "]";
            break;
        }
    }
}

std::string format_expr_ir(const ExprIR* expr) {
    std::ostringstream ss;
    format_expr_helper(expr, ss, 0, false);
    return ss.str();
}

std::string format_lambda_ir(const LambdaIR& lam) {
    std::ostringstream ss;
    if (lam.params.size() == 1) {
        ss << lam.params[0] << " => ";
    } else {
        ss << "(";
        for (size_t i = 0; i < lam.params.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << lam.params[i];
        }
        ss << ") => ";
    }
    format_expr_helper(lam.body.get(), ss, 0, false);
    return ss.str();
}

} // namespace retroc
