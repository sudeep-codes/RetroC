#pragma once

#include "parser/ast.hpp"
#include "common/error.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>

namespace retroc {

enum class SymbolKind {
    STAGE_INPUT,
    STAGE_LET,
    FN,
    PARAM
};

struct Symbol {
    std::string name;
    SymbolKind kind;
    TypePtr type;
    SourceLocation loc;
};

struct TypedProgram {
    std::vector<ItemPtr> items;
    // Map expr raw pointer to its inferred TypePtr
    std::unordered_map<const Expr*, TypePtr> expr_types;
    // Map lambda raw pointer to its inferred parameter types
    std::unordered_map<const LambdaExpr*, std::vector<TypePtr>> lambda_param_types;
    // Map stage name to its type
    std::unordered_map<std::string, TypePtr> stage_types;
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(Program program);

    std::variant<TypedProgram, CompileError> analyze();

private:
    CompileError error(ErrorCode code, const SourceLocation& loc, const std::string& msg);

    void check_item(Item* item);
    void check_input_decl(InputDecl* decl);
    void check_fn_decl(FnDecl* decl);
    void check_let_decl(LetDecl* decl);
    void check_output_decl(OutputDecl* decl);

    TypePtr check_expr(Expr* expr, const TypePtr& expected_type = nullptr);
    TypePtr check_lambda(LambdaExpr* expr, const TypePtr& expected_lambda_sig);
    TypePtr check_call(CallExpr* expr);
    TypePtr check_builtin_call(CallExpr* expr, const std::string& builtin_name);

    Program program_;
    TypedProgram typed_prog_;

    // Symbol table: stages and functions currently visible (sequential visibility)
    std::unordered_map<std::string, Symbol> symbols_;
    // All declared stage names (for E403 duplicate/shadowing checks)
    std::unordered_map<std::string, SourceLocation> declared_stage_names_;
    // Local params in current function scope
    std::unordered_map<std::string, Symbol> local_params_;

    // Currently analyzing fn (for E402 recursion detection)
    std::string current_fn_name_;
};

} // namespace retroc
