#include "lexer/token.hpp"

namespace retroc {

const char* token_kind_to_string(TokenKind kind) {
    switch (kind) {
        case TokenKind::KW_INPUT:   return "input";
        case TokenKind::KW_LET:     return "let";
        case TokenKind::KW_FN:      return "fn";
        case TokenKind::KW_OUTPUT:  return "output";
        case TokenKind::KW_IF:      return "if";
        case TokenKind::KW_THEN:    return "then";
        case TokenKind::KW_ELSE:    return "else";
        case TokenKind::KW_TRUE:    return "true";
        case TokenKind::KW_FALSE:   return "false";
        case TokenKind::KW_AND:     return "and";
        case TokenKind::KW_OR:      return "or";
        case TokenKind::KW_NOT:     return "not";
        case TokenKind::KW_FROM:    return "from";
        case TokenKind::KW_INT:     return "int";
        case TokenKind::KW_FLOAT:   return "float";
        case TokenKind::KW_BOOL:    return "bool";
        case TokenKind::KW_STRING:  return "string";

        case TokenKind::IDENT:      return "IDENT";
        case TokenKind::INT_LIT:    return "INT";
        case TokenKind::FLOAT_LIT:  return "FLOAT";
        case TokenKind::STRING_LIT: return "STRING";

        case TokenKind::PLUS:       return "+";
        case TokenKind::MINUS:      return "-";
        case TokenKind::STAR:       return "*";
        case TokenKind::SLASH:      return "/";
        case TokenKind::PERCENT:    return "%";
        case TokenKind::EQ_EQ:      return "==";
        case TokenKind::BANG_EQ:    return "!=";
        case TokenKind::LT:         return "<";
        case TokenKind::LT_EQ:      return "<=";
        case TokenKind::GT:         return ">";
        case TokenKind::GT_EQ:      return ">=";
        case TokenKind::FAT_ARROW:  return "=>";
        case TokenKind::ARROW:      return "->";
        case TokenKind::EQUAL:      return "=";
        case TokenKind::DOT:        return ".";
        case TokenKind::COMMA:      return ",";
        case TokenKind::SEMICOLON:  return ";";
        case TokenKind::COLON:      return ":";
        case TokenKind::LPAREN:     return "(";
        case TokenKind::RPAREN:     return ")";
        case TokenKind::LBRACKET:   return "[";
        case TokenKind::RBRACKET:   return "]";
        case TokenKind::LBRACE:     return "{";
        case TokenKind::RBRACE:     return "}";

        case TokenKind::END_OF_FILE: return "EOF";
    }
    return "UNKNOWN";
}

} // namespace retroc
