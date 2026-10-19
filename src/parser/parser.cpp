#include "parser/parser.hpp"
#include <stdexcept>

namespace retroc {

namespace {
struct ParseException {
    CompileError error;
};
} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek() const {
    if (current_ >= tokens_.size()) {
        static Token eof_tok{TokenKind::END_OF_FILE, "", {0, 0}};
        return eof_tok;
    }
    return tokens_[current_];
}

const Token& Parser::peek_next() const {
    if (current_ + 1 >= tokens_.size()) {
        static Token eof_tok{TokenKind::END_OF_FILE, "", {0, 0}};
        return eof_tok;
    }
    return tokens_[current_ + 1];
}

const Token& Parser::previous() const {
    if (current_ == 0) {
        return tokens_[0];
    }
    return tokens_[current_ - 1];
}

bool Parser::is_at_end() const {
    return peek().kind == TokenKind::END_OF_FILE;
}

bool Parser::check(TokenKind kind) const {
    if (is_at_end()) return kind == TokenKind::END_OF_FILE;
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

CompileError Parser::make_error(const SourceLocation& loc, const std::string& message) const {
    return CompileError{ErrorCode::E201, loc, message};
}

std::variant<Program, CompileError> Parser::parse() {
    Program prog;
    try {
        while (!is_at_end()) {
            prog.items.push_back(parse_item());
        }
        return prog;
    } catch (const ParseException& e) {
        return e.error;
    }
}

ItemPtr Parser::parse_item() {
    if (match(TokenKind::KW_INPUT)) {
        return parse_input_decl();
    }
    if (match(TokenKind::KW_FN)) {
        return parse_fn_decl();
    }
    if (match(TokenKind::KW_LET)) {
        return parse_let_decl();
    }
    if (match(TokenKind::KW_OUTPUT)) {
        return parse_output_decl();
    }

    const auto& tok = peek();
    throw ParseException{make_error(tok.loc, "expected declaration ('input', 'fn', 'let', or 'output'), found '" + tok.text + "'")};
}

ItemPtr Parser::parse_input_decl() {
    SourceLocation loc = previous().loc;
    if (!match(TokenKind::IDENT)) {
        throw ParseException{make_error(peek().loc, "expected identifier after 'input', found '" + peek().text + "'")};
    }
    std::string name = previous().text;

    if (!match(TokenKind::COLON)) {
        throw ParseException{make_error(peek().loc, "expected ':' after input name, found '" + peek().text + "'")};
    }

    TypePtr type = parse_type();

    ExprPtr init_expr = nullptr;
    std::string from_file = "";

    if (match(TokenKind::EQUAL)) {
        init_expr = parse_expr();
    } else if (match(TokenKind::KW_FROM)) {
        if (!match(TokenKind::STRING_LIT)) {
            throw ParseException{make_error(peek().loc, "expected string literal after 'from', found '" + peek().text + "'")};
        }
        from_file = previous().text;
    } else {
        throw ParseException{make_error(peek().loc, "expected '=' or 'from' in input declaration, found '" + peek().text + "'")};
    }

    if (!match(TokenKind::SEMICOLON)) {
        throw ParseException{make_error(peek().loc, "expected ';' after input declaration, found '" + peek().text + "'")};
    }

    return std::make_unique<InputDecl>(std::move(name), std::move(type), std::move(init_expr), std::move(from_file), loc);
}

ItemPtr Parser::parse_fn_decl() {
    SourceLocation loc = previous().loc;
    if (!match(TokenKind::IDENT)) {
        throw ParseException{make_error(peek().loc, "expected identifier after 'fn', found '" + peek().text + "'")};
    }
    std::string name = previous().text;

    if (!match(TokenKind::LPAREN)) {
        throw ParseException{make_error(peek().loc, "expected '(' after fn name, found '" + peek().text + "'")};
    }

    std::vector<FnParam> params;
    if (!check(TokenKind::RPAREN)) {
        do {
            if (!match(TokenKind::IDENT)) {
                throw ParseException{make_error(peek().loc, "expected parameter name, found '" + peek().text + "'")};
            }
            std::string p_name = previous().text;
            SourceLocation p_loc = previous().loc;

            if (!match(TokenKind::COLON)) {
                throw ParseException{make_error(peek().loc, "expected ':' after parameter name, found '" + peek().text + "'")};
            }

            TypePtr p_type = parse_type();
            params.push_back(FnParam{std::move(p_name), std::move(p_type), p_loc});
        } while (match(TokenKind::COMMA));
    }

    if (!match(TokenKind::RPAREN)) {
        throw ParseException{make_error(peek().loc, "expected ')' after parameters, found '" + peek().text + "'")};
    }

    if (!match(TokenKind::ARROW)) {
        throw ParseException{make_error(peek().loc, "expected '->' after parameters, found '" + peek().text + "'")};
    }

    TypePtr ret_type = parse_type();

    if (!match(TokenKind::EQUAL)) {
        throw ParseException{make_error(peek().loc, "expected '=' before fn body, found '" + peek().text + "'")};
    }

    ExprPtr body = parse_expr();

    if (!match(TokenKind::SEMICOLON)) {
        throw ParseException{make_error(peek().loc, "expected ';' after fn declaration, found '" + peek().text + "'")};
    }

    return std::make_unique<FnDecl>(std::move(name), std::move(params), std::move(ret_type), std::move(body), loc);
}

ItemPtr Parser::parse_let_decl() {
    SourceLocation loc = previous().loc;
    if (!match(TokenKind::IDENT)) {
        throw ParseException{make_error(peek().loc, "expected identifier after 'let', found '" + peek().text + "'")};
    }
    std::string name = previous().text;

    TypePtr explicit_type = nullptr;
    if (match(TokenKind::COLON)) {
        explicit_type = parse_type();
    }

    if (!match(TokenKind::EQUAL)) {
        throw ParseException{make_error(peek().loc, "expected '=' in let declaration, found '" + peek().text + "'")};
    }

    ExprPtr init_expr = parse_expr();

    if (!match(TokenKind::SEMICOLON)) {
        throw ParseException{make_error(peek().loc, "expected ';' after let declaration, found '" + peek().text + "'")};
    }

    return std::make_unique<LetDecl>(std::move(name), std::move(explicit_type), std::move(init_expr), loc);
}

ItemPtr Parser::parse_output_decl() {
    SourceLocation loc = previous().loc;
    if (!match(TokenKind::IDENT)) {
        throw ParseException{make_error(peek().loc, "expected identifier after 'output', found '" + peek().text + "'")};
    }
    std::string name = previous().text;

    if (!match(TokenKind::SEMICOLON)) {
        throw ParseException{make_error(peek().loc, "expected ';' after output declaration, found '" + peek().text + "'")};
    }

    return std::make_unique<OutputDecl>(std::move(name), loc);
}

TypePtr Parser::parse_type() {
    const auto& tok = peek();
    if (match(TokenKind::KW_INT)) {
        return Type::make_int(tok.loc);
    }
    if (match(TokenKind::KW_FLOAT)) {
        return Type::make_float(tok.loc);
    }
    if (match(TokenKind::KW_BOOL)) {
        return Type::make_bool(tok.loc);
    }
    if (match(TokenKind::KW_STRING)) {
        return Type::make_string(tok.loc);
    }
    if (match(TokenKind::LBRACKET)) {
        SourceLocation loc = previous().loc;
        TypePtr elem = parse_type();
        if (!match(TokenKind::RBRACKET)) {
            throw ParseException{make_error(peek().loc, "expected ']' after array element type, found '" + peek().text + "'")};
        }
        return Type::make_array(std::move(elem), loc);
    }
    if (match(TokenKind::LBRACE)) {
        SourceLocation loc = previous().loc;
        std::vector<FieldType> fields;
        do {
            if (!match(TokenKind::IDENT)) {
                throw ParseException{make_error(peek().loc, "expected field name in record type, found '" + peek().text + "'")};
            }
            std::string f_name = previous().text;
            SourceLocation f_loc = previous().loc;

            if (!match(TokenKind::COLON)) {
                throw ParseException{make_error(peek().loc, "expected ':' after field name in record type, found '" + peek().text + "'")};
            }

            TypePtr f_type = parse_type();
            fields.push_back(FieldType{std::move(f_name), std::move(f_type), f_loc});
        } while (match(TokenKind::COMMA));

        if (!match(TokenKind::RBRACE)) {
            throw ParseException{make_error(peek().loc, "expected '}' after record fields, found '" + peek().text + "'")};
        }
        return Type::make_record(std::move(fields), loc);
    }

    throw ParseException{make_error(tok.loc, "expected type name ('int', 'float', 'bool', 'string', '[...]', or '{...}'), found '" + tok.text + "'")};
}

bool Parser::is_lambda_ahead() const {
    if (check(TokenKind::IDENT)) {
        return peek_next().kind == TokenKind::FAT_ARROW;
    }
    if (check(TokenKind::LPAREN)) {
        // Lookahead past matching ')'
        size_t idx = current_ + 1;
        int depth = 1;
        while (idx < tokens_.size() && depth > 0) {
            if (tokens_[idx].kind == TokenKind::LPAREN) {
                depth++;
            } else if (tokens_[idx].kind == TokenKind::RPAREN) {
                depth--;
            }
            idx++;
        }
        if (depth == 0 && idx < tokens_.size()) {
            return tokens_[idx].kind == TokenKind::FAT_ARROW;
        }
    }
    return false;
}

ExprPtr Parser::parse_expr() {
    if (is_lambda_ahead()) {
        return parse_lambda();
    }
    if (check(TokenKind::KW_IF)) {
        return parse_if();
    }
    return parse_or();
}

ExprPtr Parser::parse_lambda() {
    SourceLocation loc = peek().loc;
    std::vector<std::string> params;

    if (match(TokenKind::IDENT)) {
        params.push_back(previous().text);
    } else if (match(TokenKind::LPAREN)) {
        if (!check(TokenKind::RPAREN)) {
            do {
                if (!match(TokenKind::IDENT)) {
                    throw ParseException{make_error(peek().loc, "expected parameter identifier in lambda, found '" + peek().text + "'")};
                }
                params.push_back(previous().text);
            } while (match(TokenKind::COMMA));
        }
        if (!match(TokenKind::RPAREN)) {
            throw ParseException{make_error(peek().loc, "expected ')' after lambda parameters, found '" + peek().text + "'")};
        }
    } else {
        throw ParseException{make_error(peek().loc, "expected lambda parameter, found '" + peek().text + "'")};
    }

    if (!match(TokenKind::FAT_ARROW)) {
        throw ParseException{make_error(peek().loc, "expected '=>' in lambda expression, found '" + peek().text + "'")};
    }

    ExprPtr body = parse_expr();
    return std::make_unique<LambdaExpr>(std::move(params), std::move(body), loc);
}

ExprPtr Parser::parse_if() {
    match(TokenKind::KW_IF);
    SourceLocation loc = previous().loc;

    ExprPtr cond = parse_expr();

    if (!match(TokenKind::KW_THEN)) {
        throw ParseException{make_error(peek().loc, "expected 'then' after if condition, found '" + peek().text + "'")};
    }

    ExprPtr then_branch = parse_expr();

    if (!match(TokenKind::KW_ELSE)) {
        throw ParseException{make_error(peek().loc, "expected 'else' after then branch, found '" + peek().text + "'")};
    }

    ExprPtr else_branch = parse_expr();

    return std::make_unique<IfExpr>(std::move(cond), std::move(then_branch), std::move(else_branch), loc);
}

ExprPtr Parser::parse_or() {
    ExprPtr expr = parse_and();

    while (match(TokenKind::KW_OR)) {
        SourceLocation loc = previous().loc;
        ExprPtr right = parse_and();
        expr = std::make_unique<BinaryExpr>(BinaryOp::OR, std::move(expr), std::move(right), loc);
    }

    return expr;
}

ExprPtr Parser::parse_and() {
    ExprPtr expr = parse_not();

    while (match(TokenKind::KW_AND)) {
        SourceLocation loc = previous().loc;
        ExprPtr right = parse_not();
        expr = std::make_unique<BinaryExpr>(BinaryOp::AND, std::move(expr), std::move(right), loc);
    }

    return expr;
}

ExprPtr Parser::parse_not() {
    if (match(TokenKind::KW_NOT)) {
        SourceLocation loc = previous().loc;
        ExprPtr operand = parse_not();
        return std::make_unique<UnaryExpr>(UnaryOp::NOT, std::move(operand), loc);
    }
    return parse_cmp();
}

ExprPtr Parser::parse_cmp() {
    ExprPtr expr = parse_add();

    if (match(TokenKind::EQ_EQ) || match(TokenKind::BANG_EQ) ||
        match(TokenKind::LT) || match(TokenKind::LT_EQ) ||
        match(TokenKind::GT) || match(TokenKind::GT_EQ)) {
        Token op_tok = previous();
        BinaryOp op;
        switch (op_tok.kind) {
            case TokenKind::EQ_EQ:  op = BinaryOp::EQ; break;
            case TokenKind::BANG_EQ: op = BinaryOp::NEQ; break;
            case TokenKind::LT:     op = BinaryOp::LT; break;
            case TokenKind::LT_EQ:  op = BinaryOp::LTE; break;
            case TokenKind::GT:     op = BinaryOp::GT; break;
            case TokenKind::GT_EQ:  op = BinaryOp::GTE; break;
            default: op = BinaryOp::EQ; break;
        }
        ExprPtr right = parse_add();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), op_tok.loc);
    }

    return expr;
}

ExprPtr Parser::parse_add() {
    ExprPtr expr = parse_mul();

    while (match(TokenKind::PLUS) || match(TokenKind::MINUS)) {
        Token op_tok = previous();
        BinaryOp op = (op_tok.kind == TokenKind::PLUS) ? BinaryOp::ADD : BinaryOp::SUB;
        ExprPtr right = parse_mul();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), op_tok.loc);
    }

    return expr;
}

ExprPtr Parser::parse_mul() {
    ExprPtr expr = parse_unary();

    while (match(TokenKind::STAR) || match(TokenKind::SLASH) || match(TokenKind::PERCENT)) {
        Token op_tok = previous();
        BinaryOp op;
        switch (op_tok.kind) {
            case TokenKind::STAR:    op = BinaryOp::MUL; break;
            case TokenKind::SLASH:   op = BinaryOp::DIV; break;
            case TokenKind::PERCENT: op = BinaryOp::MOD; break;
            default: op = BinaryOp::MUL; break;
        }
        ExprPtr right = parse_unary();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right), op_tok.loc);
    }

    return expr;
}

ExprPtr Parser::parse_unary() {
    if (match(TokenKind::MINUS)) {
        SourceLocation loc = previous().loc;
        ExprPtr operand = parse_unary();
        return std::make_unique<UnaryExpr>(UnaryOp::NEG, std::move(operand), loc);
    }
    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    ExprPtr expr = parse_primary();

    while (true) {
        if (match(TokenKind::DOT)) {
            SourceLocation loc = previous().loc;
            if (!match(TokenKind::IDENT)) {
                throw ParseException{make_error(peek().loc, "expected field identifier after '.', found '" + peek().text + "'")};
            }
            expr = std::make_unique<FieldAccessExpr>(std::move(expr), previous().text, loc);
        } else if (match(TokenKind::LPAREN)) {
            SourceLocation loc = previous().loc;
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RPAREN)) {
                do {
                    args.push_back(parse_expr());
                } while (match(TokenKind::COMMA));
            }
            if (!match(TokenKind::RPAREN)) {
                throw ParseException{make_error(peek().loc, "expected ')' after arguments, found '" + peek().text + "'")};
            }
            expr = std::make_unique<CallExpr>(std::move(expr), std::move(args), loc);
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parse_primary() {
    const auto& tok = peek();

    if (match(TokenKind::INT_LIT)) {
        int64_t val = 0;
        try {
            val = std::stoll(previous().text);
        } catch (...) {
            val = 0;
        }
        return std::make_unique<IntLitExpr>(val, tok.loc);
    }

    if (match(TokenKind::FLOAT_LIT)) {
        double val = 0.0;
        try {
            val = std::stod(previous().text);
        } catch (...) {
            val = 0.0;
        }
        return std::make_unique<FloatLitExpr>(val, previous().text, tok.loc);
    }

    if (match(TokenKind::STRING_LIT)) {
        return std::make_unique<StringLitExpr>(previous().text, tok.loc);
    }

    if (match(TokenKind::KW_TRUE)) {
        return std::make_unique<BoolLitExpr>(true, tok.loc);
    }

    if (match(TokenKind::KW_FALSE)) {
        return std::make_unique<BoolLitExpr>(false, tok.loc);
    }

    if (match(TokenKind::IDENT)) {
        return std::make_unique<IdentExpr>(previous().text, tok.loc);
    }

    if (match(TokenKind::LPAREN)) {
        ExprPtr expr = parse_expr();
        if (!match(TokenKind::RPAREN)) {
            throw ParseException{make_error(peek().loc, "expected ')' after parenthesized expression, found '" + peek().text + "'")};
        }
        return expr;
    }

    if (match(TokenKind::LBRACKET)) {
        SourceLocation loc = previous().loc;
        std::vector<ExprPtr> elements;
        if (!check(TokenKind::RBRACKET)) {
            do {
                elements.push_back(parse_expr());
            } while (match(TokenKind::COMMA));
        }
        if (!match(TokenKind::RBRACKET)) {
            throw ParseException{make_error(peek().loc, "expected ']' after array elements, found '" + peek().text + "'")};
        }
        return std::make_unique<ArrayLitExpr>(std::move(elements), loc);
    }

    if (match(TokenKind::LBRACE)) {
        SourceLocation loc = previous().loc;
        std::vector<RecordFieldInit> fields;
        if (!check(TokenKind::RBRACE)) {
            do {
                if (!match(TokenKind::IDENT)) {
                    throw ParseException{make_error(peek().loc, "expected field identifier in record literal, found '" + peek().text + "'")};
                }
                std::string f_name = previous().text;
                SourceLocation f_loc = previous().loc;

                if (!match(TokenKind::COLON)) {
                    throw ParseException{make_error(peek().loc, "expected ':' after field name in record literal, found '" + peek().text + "'")};
                }

                ExprPtr val = parse_expr();
                fields.push_back(RecordFieldInit{std::move(f_name), std::move(val), f_loc});
            } while (match(TokenKind::COMMA));
        }
        if (!match(TokenKind::RBRACE)) {
            throw ParseException{make_error(peek().loc, "expected '}' after record literal, found '" + peek().text + "'")};
        }
        return std::make_unique<RecordLitExpr>(std::move(fields), loc);
    }

    throw ParseException{make_error(tok.loc, "expected expression, found '" + tok.text + "'")};
}

} // namespace retroc
