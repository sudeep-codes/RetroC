#include "doctest.h"
#include "lexer/lexer.hpp"
#include <variant>

using namespace retroc;

TEST_CASE("Lexer: all keywords") {
    std::string src = "input let fn output if then else true false and or not from int float bool string";
    Lexer lexer(src);
    auto res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(res));
    const auto& tokens = std::get<std::vector<Token>>(res);

    std::vector<TokenKind> expected = {
        TokenKind::KW_INPUT, TokenKind::KW_LET, TokenKind::KW_FN, TokenKind::KW_OUTPUT,
        TokenKind::KW_IF, TokenKind::KW_THEN, TokenKind::KW_ELSE, TokenKind::KW_TRUE,
        TokenKind::KW_FALSE, TokenKind::KW_AND, TokenKind::KW_OR, TokenKind::KW_NOT,
        TokenKind::KW_FROM, TokenKind::KW_INT, TokenKind::KW_FLOAT, TokenKind::KW_BOOL,
        TokenKind::KW_STRING, TokenKind::END_OF_FILE
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        CHECK(tokens[i].kind == expected[i]);
    }
}

TEST_CASE("Lexer: operators and punctuation") {
    std::string src = "+ - * / % == != < <= > >= => -> = . , ; : ( ) [ ] { }";
    Lexer lexer(src);
    auto res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(res));
    const auto& tokens = std::get<std::vector<Token>>(res);

    std::vector<TokenKind> expected = {
        TokenKind::PLUS, TokenKind::MINUS, TokenKind::STAR, TokenKind::SLASH, TokenKind::PERCENT,
        TokenKind::EQ_EQ, TokenKind::BANG_EQ, TokenKind::LT, TokenKind::LT_EQ,
        TokenKind::GT, TokenKind::GT_EQ, TokenKind::FAT_ARROW, TokenKind::ARROW,
        TokenKind::EQUAL, TokenKind::DOT, TokenKind::COMMA, TokenKind::SEMICOLON,
        TokenKind::COLON, TokenKind::LPAREN, TokenKind::RPAREN, TokenKind::LBRACKET,
        TokenKind::RBRACKET, TokenKind::LBRACE, TokenKind::RBRACE, TokenKind::END_OF_FILE
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        CHECK(tokens[i].kind == expected[i]);
    }
}

TEST_CASE("Lexer: literals and identifiers") {
    std::string src = "foo _bar123 42 123.456 \"hello world\\n\\t\\\\\\\"\"";
    Lexer lexer(src);
    auto res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(res));
    const auto& tokens = std::get<std::vector<Token>>(res);

    REQUIRE(tokens.size() == 6);
    CHECK(tokens[0].kind == TokenKind::IDENT);
    CHECK(tokens[0].text == "foo");

    CHECK(tokens[1].kind == TokenKind::IDENT);
    CHECK(tokens[1].text == "_bar123");

    CHECK(tokens[2].kind == TokenKind::INT_LIT);
    CHECK(tokens[2].text == "42");

    CHECK(tokens[3].kind == TokenKind::FLOAT_LIT);
    CHECK(tokens[3].text == "123.456");

    CHECK(tokens[4].kind == TokenKind::STRING_LIT);
    CHECK(tokens[4].text == "hello world\n\t\\\"");

    CHECK(tokens[5].kind == TokenKind::END_OF_FILE);
}

TEST_CASE("Lexer: comments and whitespace") {
    std::string src = "// comment at line 1\n  let x = 10; // trailing comment\n// comment at line 3\noutput x;";
    Lexer lexer(src);
    auto res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(res));
    const auto& tokens = std::get<std::vector<Token>>(res);

    REQUIRE(tokens.size() == 9);
    // line 2: let x = 10;
    CHECK(tokens[0].loc.line == 2);
    CHECK(tokens[0].kind == TokenKind::KW_LET);
    CHECK(tokens[1].loc.line == 2);
    CHECK(tokens[1].kind == TokenKind::IDENT);
    CHECK(tokens[2].kind == TokenKind::EQUAL);
    CHECK(tokens[3].kind == TokenKind::INT_LIT);
    CHECK(tokens[4].kind == TokenKind::SEMICOLON);

    // line 4: output x;
    CHECK(tokens[5].loc.line == 4);
    CHECK(tokens[5].kind == TokenKind::KW_OUTPUT);
    CHECK(tokens[6].loc.line == 4);
    CHECK(tokens[6].kind == TokenKind::IDENT);
    CHECK(tokens[7].kind == TokenKind::SEMICOLON);
    CHECK(tokens[8].kind == TokenKind::END_OF_FILE);
}

TEST_CASE("Lexer: int vs float dot distinction") {
    std::string src = "42.foo 42.0";
    Lexer lexer(src);
    auto res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(res));
    const auto& tokens = std::get<std::vector<Token>>(res);

    REQUIRE(tokens.size() == 5);
    CHECK(tokens[0].kind == TokenKind::INT_LIT);
    CHECK(tokens[0].text == "42");
    CHECK(tokens[1].kind == TokenKind::DOT);
    CHECK(tokens[2].kind == TokenKind::IDENT);
    CHECK(tokens[2].text == "foo");
    CHECK(tokens[3].kind == TokenKind::FLOAT_LIT);
    CHECK(tokens[3].text == "42.0");
    CHECK(tokens[4].kind == TokenKind::END_OF_FILE);
}

TEST_CASE("Lexer: errors E101 unterminated string and bad escape") {
    {
        std::string src = "\"unterminated";
        Lexer lexer(src);
        auto res = lexer.tokenize();
        REQUIRE(std::holds_alternative<CompileError>(res));
        const auto& err = std::get<CompileError>(res);
        CHECK(err.code == ErrorCode::E101);
        CHECK(err.loc.line == 1);
        CHECK(err.loc.col == 1);
    }
    {
        std::string src = "  \"bad escape \\k\"";
        Lexer lexer(src);
        auto res = lexer.tokenize();
        REQUIRE(std::holds_alternative<CompileError>(res));
        const auto& err = std::get<CompileError>(res);
        CHECK(err.code == ErrorCode::E101);
        CHECK(err.loc.line == 1);
        CHECK(err.loc.col == 15);
    }
}

TEST_CASE("Lexer: errors E102 unexpected character") {
    std::string src = "let x = @;";
    Lexer lexer(src);
    auto res = lexer.tokenize();
    REQUIRE(std::holds_alternative<CompileError>(res));
    const auto& err = std::get<CompileError>(res);
    CHECK(err.code == ErrorCode::E102);
    CHECK(err.loc.line == 1);
    CHECK(err.loc.col == 9);
}
