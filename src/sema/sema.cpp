#include "sema/sema.hpp"
#include "sema/types.hpp"
#include <unordered_set>
#include <algorithm>

namespace retroc {

namespace {
struct SemaException {
    CompileError error;
};

// Check if identifier is one of the built-in function names
bool is_builtin(const std::string& name) {
    static const std::unordered_set<std::string> builtins = {
        "filter", "map", "reduce", "join", "length",
        "to_float", "to_int", "abs", "min", "max", "clamp"
    };
    return builtins.find(name) != builtins.end();
}
} // namespace

SemanticAnalyzer::SemanticAnalyzer(Program program)
    : program_(std::move(program)) {}

CompileError SemanticAnalyzer::error(ErrorCode code, const SourceLocation& loc, const std::string& msg) {
    return CompileError{code, loc, msg};
}

std::variant<TypedProgram, CompileError> SemanticAnalyzer::analyze() {
    try {
        for (auto& item : program_.items) {
            check_item(item.get());
        }
        typed_prog_.items = std::move(program_.items);
        return std::move(typed_prog_);
    } catch (const SemaException& e) {
        return e.error;
    }
}

void SemanticAnalyzer::check_item(Item* item) {
    if (!item) return;
    switch (item->kind) {
        case ItemKind::INPUT_DECL:
            check_input_decl(static_cast<InputDecl*>(item));
            break;
        case ItemKind::FN_DECL:
            check_fn_decl(static_cast<FnDecl*>(item));
            break;
        case ItemKind::LET_DECL:
            check_let_decl(static_cast<LetDecl*>(item));
            break;
        case ItemKind::OUTPUT_DECL:
            check_output_decl(static_cast<OutputDecl*>(item));
            break;
    }
}

void SemanticAnalyzer::check_input_decl(InputDecl* decl) {
    // Check duplicate name (E403)
    if (symbols_.find(decl->name) != symbols_.end() || is_builtin(decl->name)) {
        throw SemaException{error(ErrorCode::E403, decl->loc, "duplicate name '" + decl->name + "'")};
    }

    if (decl->init_expr) {
        TypePtr expr_type = check_expr(decl->init_expr.get());
        if (!types_equal(decl->type, expr_type)) {
            throw SemaException{error(ErrorCode::E301, decl->init_expr->loc,
                "type mismatch in input '" + decl->name + "': declared " + decl->type->to_string() + ", got " + expr_type->to_string())};
        }
    } else if (!decl->from_file.empty()) {
        // Spec 5: input x: T from "file.csv"; loads a CSV matching record fields (flat records of base types only)
        // Must be [record_type] where record fields are base types
        if (decl->type->kind != TypeKind::ARRAY || !decl->type->element_type || decl->type->element_type->kind != TypeKind::RECORD) {
            throw SemaException{error(ErrorCode::E301, decl->loc, "CSV input '" + decl->name + "' must be an array of records: [{...}]")};
        }
        for (const auto& field : decl->type->element_type->fields) {
            if (field.type->kind != TypeKind::INT && field.type->kind != TypeKind::FLOAT &&
                field.type->kind != TypeKind::BOOL && field.type->kind != TypeKind::STRING) {
                throw SemaException{error(ErrorCode::E301, field.loc, "CSV record field '" + field.name + "' must be a base type (int, float, bool, string)")};
            }
        }
    }

    Symbol sym{decl->name, SymbolKind::STAGE_INPUT, decl->type, decl->loc};
    symbols_[decl->name] = sym;
    declared_stage_names_[decl->name] = decl->loc;
    typed_prog_.stage_types[decl->name] = decl->type;
}

void SemanticAnalyzer::check_fn_decl(FnDecl* decl) {
    if (symbols_.find(decl->name) != symbols_.end() || is_builtin(decl->name)) {
        throw SemaException{error(ErrorCode::E403, decl->loc, "duplicate name '" + decl->name + "'")};
    }

    // Set current function name to detect recursion (E402)
    current_fn_name_ = decl->name;

    // Check duplicate parameter names or parameters shadowing stage names (E403)
    local_params_.clear();
    for (const auto& p : decl->params) {
        if (declared_stage_names_.find(p.name) != declared_stage_names_.end()) {
            throw SemaException{error(ErrorCode::E403, p.loc, "parameter '" + p.name + "' shadows stage name")};
        }
        if (local_params_.find(p.name) != local_params_.end()) {
            throw SemaException{error(ErrorCode::E403, p.loc, "duplicate parameter name '" + p.name + "'")};
        }
        local_params_[p.name] = Symbol{p.name, SymbolKind::PARAM, p.type, p.loc};
    }

    TypePtr body_type = check_expr(decl->body.get());
    if (!types_equal(decl->ret_type, body_type)) {
        throw SemaException{error(ErrorCode::E301, decl->body->loc,
            "return type mismatch in fn '" + decl->name + "': declared " + decl->ret_type->to_string() + ", got " + body_type->to_string())};
    }

    local_params_.clear();
    current_fn_name_ = "";

    // After successfully analyzing fn body, add fn to symbols (so self-recursion triggers E402/E401 during body check)
    Symbol sym{decl->name, SymbolKind::FN, decl->ret_type, decl->loc};
    symbols_[decl->name] = sym;
}

void SemanticAnalyzer::check_let_decl(LetDecl* decl) {
    if (symbols_.find(decl->name) != symbols_.end() || is_builtin(decl->name)) {
        throw SemaException{error(ErrorCode::E403, decl->loc, "duplicate name '" + decl->name + "'")};
    }

    TypePtr inferred_type = check_expr(decl->init_expr.get());
    if (decl->explicit_type) {
        if (!types_equal(decl->explicit_type, inferred_type)) {
            throw SemaException{error(ErrorCode::E301, decl->init_expr->loc,
                "type mismatch in let '" + decl->name + "': declared " + decl->explicit_type->to_string() + ", got " + inferred_type->to_string())};
        }
    }

    Symbol sym{decl->name, SymbolKind::STAGE_LET, inferred_type, decl->loc};
    symbols_[decl->name] = sym;
    declared_stage_names_[decl->name] = decl->loc;
    typed_prog_.stage_types[decl->name] = inferred_type;
}

void SemanticAnalyzer::check_output_decl(OutputDecl* decl) {
    auto it = symbols_.find(decl->name);
    if (it == symbols_.end() || (it->second.kind != SymbolKind::STAGE_INPUT && it->second.kind != SymbolKind::STAGE_LET)) {
        throw SemaException{error(ErrorCode::E401, decl->loc, "output references undefined stage '" + decl->name + "'")};
    }
}

TypePtr SemanticAnalyzer::check_expr(Expr* expr, const TypePtr& expected_lambda_sig) {
    if (!expr) return nullptr;

    TypePtr result = nullptr;
    switch (expr->kind) {
        case ExprKind::INT_LIT:
            result = Type::make_int(expr->loc);
            break;
        case ExprKind::FLOAT_LIT:
            result = Type::make_float(expr->loc);
            break;
        case ExprKind::BOOL_LIT:
            result = Type::make_bool(expr->loc);
            break;
        case ExprKind::STRING_LIT:
            result = Type::make_string(expr->loc);
            break;
        case ExprKind::IDENT: {
            auto* e = static_cast<IdentExpr*>(expr);
            // Check if calling oneself recursively
            if (e->name == current_fn_name_) {
                throw SemaException{error(ErrorCode::E402, expr->loc, "recursion not allowed: '" + e->name + "' calls itself")};
            }
            // Check local parameters
            auto param_it = local_params_.find(e->name);
            if (param_it != local_params_.end()) {
                result = param_it->second.type;
                break;
            }
            // Check stages and functions in scope
            auto sym_it = symbols_.find(e->name);
            if (sym_it != symbols_.end()) {
                result = sym_it->second.type;
                break;
            }
            if (is_builtin(e->name)) {
                // Builtin referenced as a bare identifier without calling
                throw SemaException{error(ErrorCode::E304, expr->loc, "builtin '" + e->name + "' cannot be used as a value")};
            }
            throw SemaException{error(ErrorCode::E401, expr->loc, "undefined name '" + e->name + "'")};
        }
        case ExprKind::ARRAY_LIT: {
            auto* e = static_cast<ArrayLitExpr*>(expr);
            if (e->elements.empty()) {
                // Empty array requires context or type
                throw SemaException{error(ErrorCode::E301, expr->loc, "cannot infer element type of empty array literal")};
            }
            TypePtr elem_type = check_expr(e->elements[0].get());
            for (size_t i = 1; i < e->elements.size(); ++i) {
                TypePtr t = check_expr(e->elements[i].get());
                if (!types_equal(elem_type, t)) {
                    throw SemaException{error(ErrorCode::E301, e->elements[i]->loc,
                        "array element type mismatch: expected " + elem_type->to_string() + ", got " + t->to_string())};
                }
            }
            result = Type::make_array(elem_type, expr->loc);
            break;
        }
        case ExprKind::RECORD_LIT: {
            auto* e = static_cast<RecordLitExpr*>(expr);
            std::vector<FieldType> fields;
            std::unordered_set<std::string> field_names;
            for (auto& f : e->fields) {
                if (field_names.find(f.name) != field_names.end()) {
                    throw SemaException{error(ErrorCode::E403, f.loc, "duplicate field name in record: '" + f.name + "'")};
                }
                field_names.insert(f.name);
                TypePtr f_type = check_expr(f.value.get());
                fields.push_back(FieldType{f.name, f_type, f.loc});
            }
            result = Type::make_record(std::move(fields), expr->loc);
            break;
        }
        case ExprKind::UNARY: {
            auto* e = static_cast<UnaryExpr*>(expr);
            TypePtr op_type = check_expr(e->operand.get());
            if (e->op == UnaryOp::NEG) {
                if (!is_numeric(op_type)) {
                    throw SemaException{error(ErrorCode::E301, expr->loc, "unary '-' requires int or float operand, got " + op_type->to_string())};
                }
                result = op_type;
            } else if (e->op == UnaryOp::NOT) {
                if (op_type->kind != TypeKind::BOOL) {
                    throw SemaException{error(ErrorCode::E301, expr->loc, "'not' requires bool operand, got " + op_type->to_string())};
                }
                result = Type::make_bool(expr->loc);
            }
            break;
        }
        case ExprKind::BINARY: {
            auto* e = static_cast<BinaryExpr*>(expr);
            TypePtr left_type = check_expr(e->left.get());
            TypePtr right_type = check_expr(e->right.get());

            switch (e->op) {
                case BinaryOp::ADD: {
                    // + works on int, float, string
                    if (left_type->kind == TypeKind::INT && right_type->kind == TypeKind::INT) {
                        result = Type::make_int(expr->loc);
                    } else if (left_type->kind == TypeKind::FLOAT && right_type->kind == TypeKind::FLOAT) {
                        result = Type::make_float(expr->loc);
                    } else if (left_type->kind == TypeKind::STRING && right_type->kind == TypeKind::STRING) {
                        result = Type::make_string(expr->loc);
                    } else {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "'+' operands must both be int, float, or string; got " + left_type->to_string() + " and " + right_type->to_string())};
                    }
                    break;
                }
                case BinaryOp::SUB:
                case BinaryOp::MUL:
                case BinaryOp::DIV: {
                    // - * / on int or float (both same type)
                    if (left_type->kind == TypeKind::INT && right_type->kind == TypeKind::INT) {
                        result = Type::make_int(expr->loc);
                    } else if (left_type->kind == TypeKind::FLOAT && right_type->kind == TypeKind::FLOAT) {
                        result = Type::make_float(expr->loc);
                    } else {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "numeric operator requires both operands to be int or float; got " + left_type->to_string() + " and " + right_type->to_string())};
                    }
                    break;
                }
                case BinaryOp::MOD: {
                    // % on int only
                    if (left_type->kind == TypeKind::INT && right_type->kind == TypeKind::INT) {
                        result = Type::make_int(expr->loc);
                    } else {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "'%' requires int operands, got " + left_type->to_string() + " and " + right_type->to_string())};
                    }
                    break;
                }
                case BinaryOp::EQ:
                case BinaryOp::NEQ: {
                    // == != on int, float, string, bool, and records
                    if (!types_equal(left_type, right_type)) {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "comparison operands must have the same type, got " + left_type->to_string() + " and " + right_type->to_string())};
                    }
                    if (left_type->kind != TypeKind::INT && left_type->kind != TypeKind::FLOAT &&
                        left_type->kind != TypeKind::STRING && left_type->kind != TypeKind::BOOL &&
                        left_type->kind != TypeKind::RECORD) {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "equality comparison not supported on type " + left_type->to_string())};
                    }
                    result = Type::make_bool(expr->loc);
                    break;
                }
                case BinaryOp::LT:
                case BinaryOp::LTE:
                case BinaryOp::GT:
                case BinaryOp::GTE: {
                    // Comparisons on int, float, string
                    if (!is_comparable(left_type, right_type)) {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "order comparison requires both operands to be same comparable type (int, float, string), got " + left_type->to_string() + " and " + right_type->to_string())};
                    }
                    result = Type::make_bool(expr->loc);
                    break;
                }
                case BinaryOp::AND:
                case BinaryOp::OR: {
                    if (left_type->kind != TypeKind::BOOL || right_type->kind != TypeKind::BOOL) {
                        throw SemaException{error(ErrorCode::E301, expr->loc,
                            "logical operator requires bool operands, got " + left_type->to_string() + " and " + right_type->to_string())};
                    }
                    result = Type::make_bool(expr->loc);
                    break;
                }
            }
            break;
        }
        case ExprKind::FIELD_ACCESS: {
            auto* e = static_cast<FieldAccessExpr*>(expr);
            TypePtr obj_type = check_expr(e->object.get());
            if (obj_type->kind != TypeKind::RECORD) {
                throw SemaException{error(ErrorCode::E302, expr->loc,
                    "field access '.' on non-record type " + obj_type->to_string())};
            }
            auto it = std::find_if(obj_type->fields.begin(), obj_type->fields.end(), [&](const FieldType& f) {
                return f.name == e->field_name;
            });
            if (it == obj_type->fields.end()) {
                throw SemaException{error(ErrorCode::E302, expr->loc,
                    "unknown field '" + e->field_name + "' on record type " + obj_type->to_string())};
            }
            result = it->type;
            break;
        }
        case ExprKind::CALL: {
            result = check_call(static_cast<CallExpr*>(expr));
            break;
        }
        case ExprKind::IF: {
            auto* e = static_cast<IfExpr*>(expr);
            TypePtr cond_type = check_expr(e->cond.get());
            if (cond_type->kind != TypeKind::BOOL) {
                throw SemaException{error(ErrorCode::E301, e->cond->loc,
                    "'if' condition must be bool, got " + cond_type->to_string())};
            }
            TypePtr then_type = check_expr(e->then_branch.get());
            TypePtr else_type = check_expr(e->else_branch.get());
            if (!types_equal(then_type, else_type)) {
                throw SemaException{error(ErrorCode::E301, expr->loc,
                    "'if' branch type mismatch: then is " + then_type->to_string() + ", else is " + else_type->to_string())};
            }
            result = then_type;
            break;
        }
        case ExprKind::LAMBDA: {
            result = check_lambda(static_cast<LambdaExpr*>(expr), expected_lambda_sig);
            break;
        }
    }

    typed_prog_.expr_types[expr] = result;
    return result;
}

TypePtr SemanticAnalyzer::check_lambda(LambdaExpr* expr, const TypePtr& expected_lambda_sig) {
    // Lambdas are NOT first-class: they may appear only as arguments to builtins (LANGUAGE_SPEC Section 3, Error E304)
    if (!expected_lambda_sig) {
        throw SemaException{error(ErrorCode::E304, expr->loc, "lambda used outside a builtin argument")};
    }

    // Shadowing check: lambda params must not shadow stage names (E403)
    for (const auto& p : expr->params) {
        if (declared_stage_names_.find(p) != declared_stage_names_.end()) {
            throw SemaException{error(ErrorCode::E403, expr->loc, "lambda parameter '" + p + "' shadows stage name")};
        }
    }

    // expected_lambda_sig is encoded in typed_prog_ as a function or passed directly.
    return nullptr;
}

TypePtr SemanticAnalyzer::check_call(CallExpr* expr) {
    // Callee could be an ident
    if (expr->callee->kind == ExprKind::IDENT) {
        auto* ident = static_cast<IdentExpr*>(expr->callee.get());
        std::string name = ident->name;

        // Check recursion (E402)
        if (name == current_fn_name_) {
            throw SemaException{error(ErrorCode::E402, expr->loc, "recursion not allowed: '" + name + "' calls itself")};
        }

        if (is_builtin(name)) {
            return check_builtin_call(expr, name);
        }

        auto it = symbols_.find(name);
        if (it == symbols_.end()) {
            throw SemaException{error(ErrorCode::E401, expr->callee->loc, "undefined function '" + name + "'")};
        }
        if (it->second.kind != SymbolKind::FN) {
            throw SemaException{error(ErrorCode::E301, expr->callee->loc, "'" + name + "' is not a function")};
        }

        // Find FnDecl to check parameter types and arity
        FnDecl* fn_decl = nullptr;
        for (const auto& item : program_.items) {
            if (item->kind == ItemKind::FN_DECL) {
                auto* f = static_cast<FnDecl*>(item.get());
                if (f->name == name) {
                    fn_decl = f;
                    break;
                }
            }
        }

        if (fn_decl) {
            if (expr->args.size() != fn_decl->params.size()) {
                throw SemaException{error(ErrorCode::E303, expr->loc,
                    "wrong arity for function '" + name + "': expected " + std::to_string(fn_decl->params.size()) + ", got " + std::to_string(expr->args.size()))};
            }
            for (size_t i = 0; i < expr->args.size(); ++i) {
                TypePtr arg_type = check_expr(expr->args[i].get());
                if (!types_equal(fn_decl->params[i].type, arg_type)) {
                    throw SemaException{error(ErrorCode::E301, expr->args[i]->loc,
                        "argument type mismatch for parameter '" + fn_decl->params[i].name + "' in call to '" + name + "': expected " + fn_decl->params[i].type->to_string() + ", got " + arg_type->to_string())};
                }
            }
            return fn_decl->ret_type;
        }
    }

    throw SemaException{error(ErrorCode::E301, expr->callee->loc, "expression is not callable")};
}

TypePtr SemanticAnalyzer::check_builtin_call(CallExpr* expr, const std::string& name) {
    if (name == "filter") {
        // filter(c, f) : [T], (T)->bool -> [T]
        if (expr->args.size() != 2) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "filter expects 2 arguments: filter(collection, lambda)")};
        }
        TypePtr col_type = check_expr(expr->args[0].get());
        if (col_type->kind != TypeKind::ARRAY) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "first argument to filter must be an array [T], got " + col_type->to_string())};
        }
        TypePtr elem_type = col_type->element_type;

        if (expr->args[1]->kind != ExprKind::LAMBDA) {
            throw SemaException{error(ErrorCode::E303, expr->args[1]->loc, "second argument to filter must be a lambda")};
        }
        auto* lam = static_cast<LambdaExpr*>(expr->args[1].get());
        if (lam->params.size() != 1) {
            throw SemaException{error(ErrorCode::E303, lam->loc, "filter lambda must take 1 parameter, got " + std::to_string(lam->params.size()))};
        }

        // Shadowing check for lambda param
        if (declared_stage_names_.find(lam->params[0]) != declared_stage_names_.end()) {
            throw SemaException{error(ErrorCode::E403, lam->loc, "lambda parameter '" + lam->params[0] + "' shadows stage name")};
        }

        // Bind lambda param type in local_params_ and analyze body
        auto saved_params = local_params_;
        local_params_[lam->params[0]] = Symbol{lam->params[0], SymbolKind::PARAM, elem_type, lam->loc};
        TypePtr ret_type = check_expr(lam->body.get());
        local_params_ = saved_params;

        if (ret_type->kind != TypeKind::BOOL) {
            throw SemaException{error(ErrorCode::E301, lam->body->loc, "filter lambda must return bool, got " + ret_type->to_string())};
        }

        typed_prog_.lambda_param_types[lam] = {elem_type};
        typed_prog_.expr_types[lam] = ret_type;
        return col_type;
    }

    if (name == "map") {
        // map(c, f) : [T], (T)->U -> [U]
        if (expr->args.size() != 2) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "map expects 2 arguments: map(collection, lambda)")};
        }
        TypePtr col_type = check_expr(expr->args[0].get());
        if (col_type->kind != TypeKind::ARRAY) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "first argument to map must be an array [T], got " + col_type->to_string())};
        }
        TypePtr elem_type = col_type->element_type;

        if (expr->args[1]->kind != ExprKind::LAMBDA) {
            throw SemaException{error(ErrorCode::E303, expr->args[1]->loc, "second argument to map must be a lambda")};
        }
        auto* lam = static_cast<LambdaExpr*>(expr->args[1].get());
        if (lam->params.size() != 1) {
            throw SemaException{error(ErrorCode::E303, lam->loc, "map lambda must take 1 parameter, got " + std::to_string(lam->params.size()))};
        }

        // Shadowing check for lambda param
        if (declared_stage_names_.find(lam->params[0]) != declared_stage_names_.end()) {
            throw SemaException{error(ErrorCode::E403, lam->loc, "lambda parameter '" + lam->params[0] + "' shadows stage name")};
        }

        auto saved_params = local_params_;
        local_params_[lam->params[0]] = Symbol{lam->params[0], SymbolKind::PARAM, elem_type, lam->loc};
        TypePtr ret_type = check_expr(lam->body.get());
        local_params_ = saved_params;

        typed_prog_.lambda_param_types[lam] = {elem_type};
        typed_prog_.expr_types[lam] = ret_type;
        return Type::make_array(ret_type, expr->loc);
    }

    if (name == "reduce") {
        // reduce(c, init, f) : [T], A, (A, T)->A -> A
        if (expr->args.size() != 3) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "reduce expects 3 arguments: reduce(collection, init, lambda)")};
        }
        TypePtr col_type = check_expr(expr->args[0].get());
        if (col_type->kind != TypeKind::ARRAY) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "first argument to reduce must be an array [T], got " + col_type->to_string())};
        }
        TypePtr elem_type = col_type->element_type;
        TypePtr init_type = check_expr(expr->args[1].get());

        if (expr->args[2]->kind != ExprKind::LAMBDA) {
            throw SemaException{error(ErrorCode::E303, expr->args[2]->loc, "third argument to reduce must be a lambda")};
        }
        auto* lam = static_cast<LambdaExpr*>(expr->args[2].get());
        if (lam->params.size() != 2) {
            throw SemaException{error(ErrorCode::E303, lam->loc, "reduce lambda must take 2 parameters (accumulator, element), got " + std::to_string(lam->params.size()))};
        }

        for (const auto& p : lam->params) {
            if (declared_stage_names_.find(p) != declared_stage_names_.end()) {
                throw SemaException{error(ErrorCode::E403, lam->loc, "lambda parameter '" + p + "' shadows stage name")};
            }
        }
        if (lam->params[0] == lam->params[1]) {
            throw SemaException{error(ErrorCode::E403, lam->loc, "duplicate lambda parameter '" + lam->params[0] + "'")};
        }

        auto saved_params = local_params_;
        local_params_[lam->params[0]] = Symbol{lam->params[0], SymbolKind::PARAM, init_type, lam->loc};
        local_params_[lam->params[1]] = Symbol{lam->params[1], SymbolKind::PARAM, elem_type, lam->loc};
        TypePtr ret_type = check_expr(lam->body.get());
        local_params_ = saved_params;

        if (!types_equal(init_type, ret_type)) {
            throw SemaException{error(ErrorCode::E301, lam->body->loc,
                "reduce lambda return type mismatch: expected accumulator type " + init_type->to_string() + ", got " + ret_type->to_string())};
        }

        typed_prog_.lambda_param_types[lam] = {init_type, elem_type};
        typed_prog_.expr_types[lam] = ret_type;
        return init_type;
    }

    if (name == "join") {
        // join(a, b, ka, kb) : [A], [B], (A)->K, (B)->K -> [{left: A, right: B}]
        if (expr->args.size() != 4) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "join expects 4 arguments: join(a, b, ka, kb)")};
        }
        TypePtr col_a = check_expr(expr->args[0].get());
        TypePtr col_b = check_expr(expr->args[1].get());
        if (col_a->kind != TypeKind::ARRAY) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "first argument to join must be array [A], got " + col_a->to_string())};
        }
        if (col_b->kind != TypeKind::ARRAY) {
            throw SemaException{error(ErrorCode::E303, expr->args[1]->loc, "second argument to join must be array [B], got " + col_b->to_string())};
        }

        if (expr->args[2]->kind != ExprKind::LAMBDA) {
            throw SemaException{error(ErrorCode::E303, expr->args[2]->loc, "third argument to join must be a lambda")};
        }
        if (expr->args[3]->kind != ExprKind::LAMBDA) {
            throw SemaException{error(ErrorCode::E303, expr->args[3]->loc, "fourth argument to join must be a lambda")};
        }

        auto* lam_a = static_cast<LambdaExpr*>(expr->args[2].get());
        auto* lam_b = static_cast<LambdaExpr*>(expr->args[3].get());

        if (lam_a->params.size() != 1) {
            throw SemaException{error(ErrorCode::E303, lam_a->loc, "join key lambda for left side must take 1 parameter")};
        }
        if (lam_b->params.size() != 1) {
            throw SemaException{error(ErrorCode::E303, lam_b->loc, "join key lambda for right side must take 1 parameter")};
        }

        if (declared_stage_names_.find(lam_a->params[0]) != declared_stage_names_.end()) {
            throw SemaException{error(ErrorCode::E403, lam_a->loc, "lambda parameter '" + lam_a->params[0] + "' shadows stage name")};
        }
        if (declared_stage_names_.find(lam_b->params[0]) != declared_stage_names_.end()) {
            throw SemaException{error(ErrorCode::E403, lam_b->loc, "lambda parameter '" + lam_b->params[0] + "' shadows stage name")};
        }

        auto saved_params = local_params_;
        local_params_[lam_a->params[0]] = Symbol{lam_a->params[0], SymbolKind::PARAM, col_a->element_type, lam_a->loc};
        TypePtr key_a = check_expr(lam_a->body.get());
        local_params_ = saved_params;

        local_params_[lam_b->params[0]] = Symbol{lam_b->params[0], SymbolKind::PARAM, col_b->element_type, lam_b->loc};
        TypePtr key_b = check_expr(lam_b->body.get());
        local_params_ = saved_params;

        if (!types_equal(key_a, key_b)) {
            throw SemaException{error(ErrorCode::E301, lam_b->body->loc,
                "join key types do not match: left key is " + key_a->to_string() + ", right key is " + key_b->to_string())};
        }

        if (!is_valid_join_key(key_a)) {
            throw SemaException{error(ErrorCode::E301, lam_a->body->loc,
                "invalid join key type " + key_a->to_string() + ": must be int, string, or bool")};
        }

        typed_prog_.lambda_param_types[lam_a] = {col_a->element_type};
        typed_prog_.lambda_param_types[lam_b] = {col_b->element_type};
        typed_prog_.expr_types[lam_a] = key_a;
        typed_prog_.expr_types[lam_b] = key_b;

        // Result type: [{left: A, right: B}]
        std::vector<FieldType> fields = {
            FieldType{"left", col_a->element_type, expr->loc},
            FieldType{"right", col_b->element_type, expr->loc}
        };
        TypePtr record_type = Type::make_record(std::move(fields), expr->loc);
        return Type::make_array(std::move(record_type), expr->loc);
    }

    if (name == "length") {
        // length(c) : [T] -> int
        if (expr->args.size() != 1) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "length expects 1 argument")};
        }
        TypePtr t = check_expr(expr->args[0].get());
        if (t->kind != TypeKind::ARRAY) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "length expects array argument, got " + t->to_string())};
        }
        return Type::make_int(expr->loc);
    }

    if (name == "to_float") {
        if (expr->args.size() != 1) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "to_float expects 1 argument")};
        }
        TypePtr t = check_expr(expr->args[0].get());
        if (t->kind != TypeKind::INT && t->kind != TypeKind::FLOAT) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "to_float expects int or float, got " + t->to_string())};
        }
        return Type::make_float(expr->loc);
    }

    if (name == "to_int") {
        if (expr->args.size() != 1) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "to_int expects 1 argument")};
        }
        TypePtr t = check_expr(expr->args[0].get());
        if (t->kind != TypeKind::INT && t->kind != TypeKind::FLOAT) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "to_int expects int or float, got " + t->to_string())};
        }
        return Type::make_int(expr->loc);
    }

    if (name == "abs") {
        if (expr->args.size() != 1) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "abs expects 1 argument")};
        }
        TypePtr t = check_expr(expr->args[0].get());
        if (!is_numeric(t)) {
            throw SemaException{error(ErrorCode::E303, expr->args[0]->loc, "abs expects numeric argument, got " + t->to_string())};
        }
        return t;
    }

    if (name == "min" || name == "max") {
        if (expr->args.size() != 2) {
            throw SemaException{error(ErrorCode::E303, expr->loc, name + " expects 2 arguments")};
        }
        TypePtr a = check_expr(expr->args[0].get());
        TypePtr b = check_expr(expr->args[1].get());
        if (!types_equal(a, b) || !is_numeric(a)) {
            throw SemaException{error(ErrorCode::E303, expr->loc, name + " expects two arguments of the same numeric type, got " + a->to_string() + " and " + b->to_string())};
        }
        return a;
    }

    if (name == "clamp") {
        // clamp(x, lo, hi)
        if (expr->args.size() != 3) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "clamp expects 3 arguments: clamp(x, lo, hi)")};
        }
        TypePtr x = check_expr(expr->args[0].get());
        TypePtr lo = check_expr(expr->args[1].get());
        TypePtr hi = check_expr(expr->args[2].get());
        if (!is_numeric(x) || !types_equal(x, lo) || !types_equal(x, hi)) {
            throw SemaException{error(ErrorCode::E303, expr->loc, "clamp arguments must all be the same numeric type, got " + x->to_string() + ", " + lo->to_string() + ", " + hi->to_string())};
        }
        return x;
    }

    throw SemaException{error(ErrorCode::E401, expr->loc, "unknown builtin '" + name + "'")};
}

} // namespace retroc
