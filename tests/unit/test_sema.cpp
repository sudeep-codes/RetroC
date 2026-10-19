#include "doctest.h"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"

using namespace retroc;

static TypedProgram analyze_str(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(lex_res));
    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    REQUIRE(std::holds_alternative<Program>(parse_res));
    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    REQUIRE(std::holds_alternative<TypedProgram>(sema_res));
    return std::get<TypedProgram>(std::move(sema_res));
}

static CompileError analyze_error_str(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(lex_res));
    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    REQUIRE(std::holds_alternative<Program>(parse_res));
    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    REQUIRE(std::holds_alternative<CompileError>(sema_res));
    return std::get<CompileError>(sema_res);
}

TEST_CASE("Sema: Typechecking valid programs (Ex1-Ex4)") {
    SUBCASE("Ex1: filter, map, reduce") {
        auto prog = analyze_str(
            "input xs: [int] = [1, 2, 3, 4, 5, 6];\n"
            "let evens = filter(xs, x => x % 2 == 0);\n"
            "let sq = map(evens, x => x * x);\n"
            "let total = reduce(sq, 0, (a, x) => a + x);\n"
            "output sq;\n"
            "output total;\n"
        );
        CHECK(prog.stage_types["xs"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["evens"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["sq"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["total"]->kind == TypeKind::INT);
    }

    SUBCASE("Ex2: records and scalar stage") {
        auto prog = analyze_str(
            "input rows: [{ id: int, v: float, ok: bool }] = [\n"
            "  { id: 1, v: 2.0, ok: true },\n"
            "  { id: 2, v: 5.0, ok: false },\n"
            "  { id: 3, v: 4.0, ok: true }\n"
            "];\n"
            "let clean = filter(rows, r => r.ok);\n"
            "let vals = map(clean, r => r.v);\n"
            "let sum = reduce(vals, 0.0, (a, x) => a + x);\n"
            "let avg = sum / to_float(length(vals));\n"
            "output avg;\n"
        );
        CHECK(prog.stage_types["rows"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["clean"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["vals"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["sum"]->kind == TypeKind::FLOAT);
        CHECK(prog.stage_types["avg"]->kind == TypeKind::FLOAT);
    }

    SUBCASE("Ex3: join") {
        auto prog = analyze_str(
            "input users: [{ uid: int, name: string }] = [{ uid: 1, name: \"ana\" }, { uid: 2, name: \"bo\" }];\n"
            "input orders: [{ oid: int, uid: int, amt: int }] = [\n"
            "  { oid: 10, uid: 1, amt: 5 }, { oid: 11, uid: 1, amt: 7 }, { oid: 12, uid: 2, amt: 3 }\n"
            "];\n"
            "let joined = join(users, orders, u => u.uid, o => o.uid);\n"
            "let amts = map(joined, j => j.right.amt);\n"
            "output amts;\n"
        );
        CHECK(prog.stage_types["joined"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["amts"]->kind == TypeKind::ARRAY);
    }

    SUBCASE("Ex4: fn and if-expression") {
        auto prog = analyze_str(
            "fn clamp01(x: float) -> float = if x < 0.0 then 0.0 else if x > 1.0 then 1.0 else x;\n"
            "input xs: [float] = [-0.5, 0.25, 1.5];\n"
            "let ys = map(xs, x => clamp01(x));\n"
            "output ys;\n"
        );
        CHECK(prog.stage_types["ys"]->kind == TypeKind::ARRAY);
        CHECK(prog.stage_types["ys"]->element_type->kind == TypeKind::FLOAT);
    }
}

TEST_CASE("Sema: Error codes E3xx") {
    SUBCASE("E301: type mismatch (Ex5)") {
        auto err = analyze_error_str(
            "input xs: [int] = [1, 2];\n"
            "let a = map(xs, x => x + 1.0);\n"
        );
        CHECK(err.code == ErrorCode::E301);
        CHECK(err.loc.line == 2);
    }

    SUBCASE("E301: if branch type mismatch") {
        auto err = analyze_error_str("let x = if true then 1 else \"hello\";");
        CHECK(err.code == ErrorCode::E301);
    }

    SUBCASE("E302: unknown field") {
        auto err = analyze_error_str(
            "input rec: { a: int } = { a: 1 };\n"
            "let v = rec.b;\n"
        );
        CHECK(err.code == ErrorCode::E302);
    }

    SUBCASE("E302: field access on non-record") {
        auto err = analyze_error_str("let x = 42.foo;");
        CHECK(err.code == ErrorCode::E302);
    }

    SUBCASE("E303: wrong arity / argument type for builtin") {
        auto err = analyze_error_str(
            "input xs: [int] = [1];\n"
            "let a = filter(xs);\n"
        );
        CHECK(err.code == ErrorCode::E303);
    }

    SUBCASE("E304: lambda used outside a builtin argument") {
        auto err = analyze_error_str("let f = x => x + 1;");
        CHECK(err.code == ErrorCode::E304);
    }
}

TEST_CASE("Sema: Error codes E4xx") {
    SUBCASE("E401: undefined name (Ex6)") {
        auto err = analyze_error_str("let a = map(missing, x => x);\n");
        CHECK(err.code == ErrorCode::E401);
        CHECK(err.loc.line == 1);
    }

    SUBCASE("E401: forward reference rejected") {
        auto err = analyze_error_str(
            "let a = b + 1;\n"
            "let b = 10;\n"
        );
        CHECK(err.code == ErrorCode::E401);
    }

    SUBCASE("E402: recursion not allowed (Ex7)") {
        auto err = analyze_error_str("fn f(x: int) -> int = f(x - 1);\n");
        CHECK(err.code == ErrorCode::E402);
        CHECK(err.loc.line == 1);
    }

    SUBCASE("E403: duplicate stage name") {
        auto err = analyze_error_str(
            "let a = 1;\n"
            "let a = 2;\n"
        );
        CHECK(err.code == ErrorCode::E403);
    }

    SUBCASE("E403: lambda param shadows stage name") {
        auto err = analyze_error_str(
            "input xs: [int] = [1, 2];\n"
            "let a = map(xs, xs => xs + 1);\n"
        );
        CHECK(err.code == ErrorCode::E403);
    }
}
