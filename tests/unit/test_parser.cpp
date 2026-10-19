#include "doctest.h"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

using namespace retroc;

static Program parse_str(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(lex_res));
    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    REQUIRE(std::holds_alternative<Program>(parse_res));
    return std::get<Program>(std::move(parse_res));
}


static CompileError parse_error_str(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(lex_res));
    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    REQUIRE(std::holds_alternative<CompileError>(parse_res));
    return std::get<CompileError>(parse_res);
}

TEST_CASE("Parser: Top-level declarations") {
    SUBCASE("Input inline") {
        auto prog = parse_str("input x: int = 42;");
        REQUIRE(prog.items.size() == 1);
        auto* d = dynamic_cast<InputDecl*>(prog.items[0].get());
        REQUIRE(d != nullptr);
        CHECK(d->name == "x");
        CHECK(d->type->kind == TypeKind::INT);
        CHECK(d->init_expr != nullptr);
        CHECK(d->from_file.empty());
    }

    SUBCASE("Input from file") {
        auto prog = parse_str("input data: [{ a: int }] from \"test.csv\";");
        REQUIRE(prog.items.size() == 1);
        auto* d = dynamic_cast<InputDecl*>(prog.items[0].get());
        REQUIRE(d != nullptr);
        CHECK(d->name == "data");
        CHECK(d->type->kind == TypeKind::ARRAY);
        CHECK(d->from_file == "test.csv");
        CHECK(d->init_expr == nullptr);
    }

    SUBCASE("Fn declaration") {
        auto prog = parse_str("fn add(a: int, b: int) -> int = a + b;");
        REQUIRE(prog.items.size() == 1);
        auto* d = dynamic_cast<FnDecl*>(prog.items[0].get());
        REQUIRE(d != nullptr);
        CHECK(d->name == "add");
        REQUIRE(d->params.size() == 2);
        CHECK(d->params[0].name == "a");
        CHECK(d->params[1].name == "b");
        CHECK(d->ret_type->kind == TypeKind::INT);
        CHECK(d->body != nullptr);
    }

    SUBCASE("Let declaration with and without explicit type") {
        auto prog = parse_str("let a = 1; let b: float = 2.0;");
        REQUIRE(prog.items.size() == 2);
        auto* l1 = dynamic_cast<LetDecl*>(prog.items[0].get());
        REQUIRE(l1 != nullptr);
        CHECK(l1->name == "a");
        CHECK(l1->explicit_type == nullptr);

        auto* l2 = dynamic_cast<LetDecl*>(prog.items[1].get());
        REQUIRE(l2 != nullptr);
        CHECK(l2->name == "b");
        REQUIRE(l2->explicit_type != nullptr);
        CHECK(l2->explicit_type->kind == TypeKind::FLOAT);
    }

    SUBCASE("Output declaration") {
        auto prog = parse_str("output result;");
        REQUIRE(prog.items.size() == 1);
        auto* d = dynamic_cast<OutputDecl*>(prog.items[0].get());
        REQUIRE(d != nullptr);
        CHECK(d->name == "result");
    }
}

TEST_CASE("Parser: Operator precedence and associativity") {
    SUBCASE("1 + 2 * 3 is 1 + (2 * 3)") {
        auto prog = parse_str("let x = 1 + 2 * 3;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* b = dynamic_cast<BinaryExpr*>(l->init_expr.get());
        REQUIRE(b != nullptr);
        CHECK(b->op == BinaryOp::ADD);
        CHECK(b->left->kind == ExprKind::INT_LIT);
        REQUIRE(b->right->kind == ExprKind::BINARY);
        auto* b_right = dynamic_cast<BinaryExpr*>(b->right.get());
        CHECK(b_right->op == BinaryOp::MUL);
    }

    SUBCASE("1 * 2 + 3 is (1 * 2) + 3") {
        auto prog = parse_str("let x = 1 * 2 + 3;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* b = dynamic_cast<BinaryExpr*>(l->init_expr.get());
        REQUIRE(b != nullptr);
        CHECK(b->op == BinaryOp::ADD);
        REQUIRE(b->left->kind == ExprKind::BINARY);
        auto* b_left = dynamic_cast<BinaryExpr*>(b->left.get());
        CHECK(b_left->op == BinaryOp::MUL);
        CHECK(b->right->kind == ExprKind::INT_LIT);
    }

    SUBCASE("Left associativity: 1 - 2 - 3 is (1 - 2) - 3") {
        auto prog = parse_str("let x = 1 - 2 - 3;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* b = dynamic_cast<BinaryExpr*>(l->init_expr.get());
        REQUIRE(b != nullptr);
        CHECK(b->op == BinaryOp::SUB);
        REQUIRE(b->left->kind == ExprKind::BINARY);
        auto* b_left = dynamic_cast<BinaryExpr*>(b->left.get());
        CHECK(b_left->op == BinaryOp::SUB);
        CHECK(b->right->kind == ExprKind::INT_LIT);
    }

    SUBCASE("Logical precedence: not a and b or c is ((not a) and b) or c") {
        auto prog = parse_str("let x = not a and b or c;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* b_or = dynamic_cast<BinaryExpr*>(l->init_expr.get());
        REQUIRE(b_or != nullptr);
        CHECK(b_or->op == BinaryOp::OR);
        auto* b_and = dynamic_cast<BinaryExpr*>(b_or->left.get());
        REQUIRE(b_and != nullptr);
        CHECK(b_and->op == BinaryOp::AND);
        CHECK(b_and->left->kind == ExprKind::UNARY);
    }

    SUBCASE("Comparisons non-associative / lower than add") {
        auto prog = parse_str("let x = a + 1 < b * 2;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* b_cmp = dynamic_cast<BinaryExpr*>(l->init_expr.get());
        REQUIRE(b_cmp != nullptr);
        CHECK(b_cmp->op == BinaryOp::LT);
        CHECK(b_cmp->left->kind == ExprKind::BINARY);
        CHECK(b_cmp->right->kind == ExprKind::BINARY);
    }
}

TEST_CASE("Parser: Lambda vs Parenthesized Expression Lookahead") {
    SUBCASE("Single ident lambda: x => x + 1") {
        auto prog = parse_str("let f = x => x + 1;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* lam = dynamic_cast<LambdaExpr*>(l->init_expr.get());
        REQUIRE(lam != nullptr);
        REQUIRE(lam->params.size() == 1);
        CHECK(lam->params[0] == "x");
    }

    SUBCASE("Multi-param lambda: (a, b) => a + b") {
        auto prog = parse_str("let f = (a, b) => a + b;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* lam = dynamic_cast<LambdaExpr*>(l->init_expr.get());
        REQUIRE(lam != nullptr);
        REQUIRE(lam->params.size() == 2);
        CHECK(lam->params[0] == "a");
        CHECK(lam->params[1] == "b");
    }

    SUBCASE("Parenthesized expression: (a + b) * c") {
        auto prog = parse_str("let f = (a + b) * c;");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* b = dynamic_cast<BinaryExpr*>(l->init_expr.get());
        REQUIRE(b != nullptr);
        CHECK(b->op == BinaryOp::MUL);
        CHECK(b->left->kind == ExprKind::BINARY);
        auto* b_sub = dynamic_cast<BinaryExpr*>(b->left.get());
        CHECK(b_sub->op == BinaryOp::ADD);
    }

    SUBCASE("Nested lambda in call: filter(xs, x => x.ok)") {
        auto prog = parse_str("let f = filter(xs, x => x.ok);");
        auto* l = dynamic_cast<LetDecl*>(prog.items[0].get());
        auto* call = dynamic_cast<CallExpr*>(l->init_expr.get());
        REQUIRE(call != nullptr);
        REQUIRE(call->args.size() == 2);
        auto* lam = dynamic_cast<LambdaExpr*>(call->args[1].get());
        REQUIRE(lam != nullptr);
        CHECK(lam->params[0] == "x");
    }
}

TEST_CASE("Parser: Syntax errors (E201)") {
    SUBCASE("Missing semicolon") {
        auto err = parse_error_str("let x = 1");
        CHECK(err.code == ErrorCode::E201);
    }

    SUBCASE("Missing type in input") {
        auto err = parse_error_str("input x = 1;");
        CHECK(err.code == ErrorCode::E201);
    }

    SUBCASE("Missing then in if") {
        auto err = parse_error_str("let x = if true 1 else 2;");
        CHECK(err.code == ErrorCode::E201);
    }

    SUBCASE("Unclosed bracket in array") {
        auto err = parse_error_str("let x = [1, 2;");
        CHECK(err.code == ErrorCode::E201);
    }

    SUBCASE("Unexpected token at top level") {
        auto err = parse_error_str("+ 1;");
        CHECK(err.code == ErrorCode::E201);
    }
}
