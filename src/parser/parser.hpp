#pragma once

#include "lexer/token.hpp"
#include "common/error.hpp"
#include "parser/ast.hpp"
#include <vector>
#include <variant>

namespace retroc {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    std::variant<Program, CompileError> parse();

private:
    const Token& peek() const;
    const Token& peek_next() const;
    const Token& previous() const;
    bool is_at_end() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& advance();

    // Error helper
    CompileError make_error(const SourceLocation& loc, const std::string& message) const;

    // Declarations
    ItemPtr parse_item();
    ItemPtr parse_input_decl();
    ItemPtr parse_fn_decl();
    ItemPtr parse_let_decl();
    ItemPtr parse_output_decl();

    // Types
    TypePtr parse_type();

    // Expressions
    ExprPtr parse_expr();
    ExprPtr parse_lambda();
    ExprPtr parse_if();
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_not();
    ExprPtr parse_cmp();
    ExprPtr parse_add();
    ExprPtr parse_mul();
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();

    // Lookahead helper for lambda vs paren
    bool is_lambda_ahead() const;

    std::vector<Token> tokens_;
    size_t current_{0};
};

} // namespace retroc
