#include "parser/ast.hpp"
#include <sstream>
#include <iomanip>

namespace retroc {

const char* binary_op_to_string(BinaryOp op) {
    switch (op) {
        case BinaryOp::ADD: return "+";
        case BinaryOp::SUB: return "-";
        case BinaryOp::MUL: return "*";
        case BinaryOp::DIV: return "/";
        case BinaryOp::MOD: return "%";
        case BinaryOp::EQ:  return "==";
        case BinaryOp::NEQ: return "!=";
        case BinaryOp::LT:  return "<";
        case BinaryOp::LTE: return "<=";
        case BinaryOp::GT:  return ">";
        case BinaryOp::GTE: return ">=";
        case BinaryOp::AND: return "and";
        case BinaryOp::OR:  return "or";
    }
    return "?";
}

const char* unary_op_to_string(UnaryOp op) {
    switch (op) {
        case UnaryOp::NEG: return "-";
        case UnaryOp::NOT: return "not";
    }
    return "?";
}

std::string Type::to_string() const {
    switch (kind) {
        case TypeKind::INT: return "int";
        case TypeKind::FLOAT: return "float";
        case TypeKind::BOOL: return "bool";
        case TypeKind::STRING: return "string";
        case TypeKind::ARRAY:
            return "[" + (element_type ? element_type->to_string() : "?") + "]";
        case TypeKind::RECORD: {
            std::string res = "{";
            for (size_t i = 0; i < fields.size(); ++i) {
                if (i > 0) res += ", ";
                res += fields[i].name + ": " + (fields[i].type ? fields[i].type->to_string() : "?");
            }
            res += "}";
            return res;
        }
    }
    return "?";
}

std::string Type::to_ir_string() const {
    switch (kind) {
        case TypeKind::INT: return "int";
        case TypeKind::FLOAT: return "float";
        case TypeKind::BOOL: return "bool";
        case TypeKind::STRING: return "string";
        case TypeKind::ARRAY:
            return "[" + (element_type ? element_type->to_ir_string() : "?") + "]";
        case TypeKind::RECORD: {
            std::string res = "{";
            for (size_t i = 0; i < fields.size(); ++i) {
                if (i > 0) res += ",";
                res += fields[i].name + ":" + (fields[i].type ? fields[i].type->to_ir_string() : "?");
            }
            res += "}";
            return res;
        }
    }
    return "?";
}

static void print_indent(std::ostream& out, int indent) {
    for (int i = 0; i < indent; ++i) {
        out << "  ";
    }
}

static void dump_expr(const Expr* expr, std::ostream& out, int indent) {
    if (!expr) {
        print_indent(out, indent);
        out << "<null expr>\n";
        return;
    }

    print_indent(out, indent);
    switch (expr->kind) {
        case ExprKind::INT_LIT: {
            const auto* e = static_cast<const IntLitExpr*>(expr);
            out << "IntLit(" << e->value << ")\n";
            break;
        }
        case ExprKind::FLOAT_LIT: {
            const auto* e = static_cast<const FloatLitExpr*>(expr);
            out << "FloatLit(" << e->text << ")\n";
            break;
        }
        case ExprKind::BOOL_LIT: {
            const auto* e = static_cast<const BoolLitExpr*>(expr);
            out << "BoolLit(" << (e->value ? "true" : "false") << ")\n";
            break;
        }
        case ExprKind::STRING_LIT: {
            const auto* e = static_cast<const StringLitExpr*>(expr);
            out << "StringLit(\"" << e->value << "\")\n";
            break;
        }
        case ExprKind::IDENT: {
            const auto* e = static_cast<const IdentExpr*>(expr);
            out << "Ident(" << e->name << ")\n";
            break;
        }
        case ExprKind::ARRAY_LIT: {
            const auto* e = static_cast<const ArrayLitExpr*>(expr);
            out << "ArrayLit\n";
            for (const auto& elem : e->elements) {
                dump_expr(elem.get(), out, indent + 1);
            }
            break;
        }
        case ExprKind::RECORD_LIT: {
            const auto* e = static_cast<const RecordLitExpr*>(expr);
            out << "RecordLit\n";
            for (const auto& f : e->fields) {
                print_indent(out, indent + 1);
                out << "Field(" << f.name << "):\n";
                dump_expr(f.value.get(), out, indent + 2);
            }
            break;
        }
        case ExprKind::UNARY: {
            const auto* e = static_cast<const UnaryExpr*>(expr);
            out << "Unary(" << unary_op_to_string(e->op) << ")\n";
            dump_expr(e->operand.get(), out, indent + 1);
            break;
        }
        case ExprKind::BINARY: {
            const auto* e = static_cast<const BinaryExpr*>(expr);
            out << "Binary(" << binary_op_to_string(e->op) << ")\n";
            dump_expr(e->left.get(), out, indent + 1);
            dump_expr(e->right.get(), out, indent + 1);
            break;
        }
        case ExprKind::FIELD_ACCESS: {
            const auto* e = static_cast<const FieldAccessExpr*>(expr);
            out << "FieldAccess(." << e->field_name << ")\n";
            dump_expr(e->object.get(), out, indent + 1);
            break;
        }
        case ExprKind::CALL: {
            const auto* e = static_cast<const CallExpr*>(expr);
            out << "Call\n";
            print_indent(out, indent + 1);
            out << "Callee:\n";
            dump_expr(e->callee.get(), out, indent + 2);
            print_indent(out, indent + 1);
            out << "Args:\n";
            for (const auto& arg : e->args) {
                dump_expr(arg.get(), out, indent + 2);
            }
            break;
        }
        case ExprKind::IF: {
            const auto* e = static_cast<const IfExpr*>(expr);
            out << "If\n";
            print_indent(out, indent + 1);
            out << "Cond:\n";
            dump_expr(e->cond.get(), out, indent + 2);
            print_indent(out, indent + 1);
            out << "Then:\n";
            dump_expr(e->then_branch.get(), out, indent + 2);
            print_indent(out, indent + 1);
            out << "Else:\n";
            dump_expr(e->else_branch.get(), out, indent + 2);
            break;
        }
        case ExprKind::LAMBDA: {
            const auto* e = static_cast<const LambdaExpr*>(expr);
            out << "Lambda(";
            for (size_t i = 0; i < e->params.size(); ++i) {
                if (i > 0) out << ", ";
                out << e->params[i];
            }
            out << ")\n";
            dump_expr(e->body.get(), out, indent + 1);
            break;
        }
    }
}

void dump_ast(const Program& prog, std::ostream& out) {
    out << "Program\n";
    for (const auto& item : prog.items) {
        if (!item) continue;
        switch (item->kind) {
            case ItemKind::INPUT_DECL: {
                const auto* d = static_cast<const InputDecl*>(item.get());
                out << "  InputDecl " << d->name << ": " << (d->type ? d->type->to_string() : "?");
                if (!d->from_file.empty()) {
                    out << " from \"" << d->from_file << "\"\n";
                } else if (d->init_expr) {
                    out << " =\n";
                    dump_expr(d->init_expr.get(), out, 2);
                } else {
                    out << "\n";
                }
                break;
            }
            case ItemKind::FN_DECL: {
                const auto* d = static_cast<const FnDecl*>(item.get());
                out << "  FnDecl " << d->name << "(";
                for (size_t i = 0; i < d->params.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << d->params[i].name << ": " << (d->params[i].type ? d->params[i].type->to_string() : "?");
                }
                out << ") -> " << (d->ret_type ? d->ret_type->to_string() : "?") << " =\n";
                dump_expr(d->body.get(), out, 2);
                break;
            }
            case ItemKind::LET_DECL: {
                const auto* d = static_cast<const LetDecl*>(item.get());
                out << "  LetDecl " << d->name;
                if (d->explicit_type) {
                    out << ": " << d->explicit_type->to_string();
                }
                out << " =\n";
                dump_expr(d->init_expr.get(), out, 2);
                break;
            }
            case ItemKind::OUTPUT_DECL: {
                const auto* d = static_cast<const OutputDecl*>(item.get());
                out << "  OutputDecl " << d->name << "\n";
                break;
            }
        }
    }
}

} // namespace retroc
