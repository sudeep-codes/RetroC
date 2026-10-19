#pragma once

#include "common/location.hpp"
#include <string>

namespace retroc {

enum class TokenKind {
    // Keywords
    KW_INPUT,
    KW_LET,
    KW_FN,
    KW_OUTPUT,
    KW_IF,
    KW_THEN,
    KW_ELSE,
    KW_TRUE,
    KW_FALSE,
    KW_AND,
    KW_OR,
    KW_NOT,
    KW_FROM,
    KW_INT,
    KW_FLOAT,
    KW_BOOL,
    KW_STRING,

    // Identifiers & Literals
    IDENT,
    INT_LIT,
    FLOAT_LIT,
    STRING_LIT,

    // Operators & Punctuation
    PLUS,       // +
    MINUS,      // -
    STAR,       // *
    SLASH,      // /
    PERCENT,    // %
    EQ_EQ,      // ==
    BANG_EQ,    // !=
    LT,         // <
    LT_EQ,      // <=
    GT,         // >
    GT_EQ,      // >=
    FAT_ARROW,  // =>
    ARROW,      // ->
    EQUAL,      // =
    DOT,        // .
    COMMA,      // ,
    SEMICOLON,  // ;
    COLON,      // :
    LPAREN,     // (
    RPAREN,     // )
    LBRACKET,   // [
    RBRACKET,   // ]
    LBRACE,     // {
    RBRACE,     // }

    // Special
    END_OF_FILE
};

const char* token_kind_to_string(TokenKind kind);

struct Token {
    TokenKind kind;
    std::string text;
    SourceLocation loc;
};

} // namespace retroc
