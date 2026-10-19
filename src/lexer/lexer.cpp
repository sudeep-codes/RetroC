#include "lexer/lexer.hpp"
#include <cctype>

namespace retroc {

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[cursor_];
}

char Lexer::peek_next() const {
    if (cursor_ + 1 >= source_.size()) return '\0';
    return source_[cursor_ + 1];
}

char Lexer::advance() {
    if (is_at_end()) return '\0';
    char c = source_[cursor_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::is_at_end() const {
    return cursor_ >= source_.size();
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek_next() == '/') {
            // Comment extends to end of line
            while (!is_at_end() && peek() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

TokenKind Lexer::check_keyword_or_ident(const std::string& text) const {
    if (text == "input") return TokenKind::KW_INPUT;
    if (text == "let") return TokenKind::KW_LET;
    if (text == "fn") return TokenKind::KW_FN;
    if (text == "output") return TokenKind::KW_OUTPUT;
    if (text == "if") return TokenKind::KW_IF;
    if (text == "then") return TokenKind::KW_THEN;
    if (text == "else") return TokenKind::KW_ELSE;
    if (text == "true") return TokenKind::KW_TRUE;
    if (text == "false") return TokenKind::KW_FALSE;
    if (text == "and") return TokenKind::KW_AND;
    if (text == "or") return TokenKind::KW_OR;
    if (text == "not") return TokenKind::KW_NOT;
    if (text == "from") return TokenKind::KW_FROM;
    if (text == "int") return TokenKind::KW_INT;
    if (text == "float") return TokenKind::KW_FLOAT;
    if (text == "bool") return TokenKind::KW_BOOL;
    if (text == "string") return TokenKind::KW_STRING;
    return TokenKind::IDENT;
}

std::variant<std::vector<Token>, CompileError> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_whitespace_and_comments();
        if (is_at_end()) {
            tokens.push_back({TokenKind::END_OF_FILE, "", {line_, col_}});
            break;
        }

        SourceLocation start_loc{line_, col_};
        char c = advance();

        // Single-character or 2-character operators
        switch (c) {
            case '+': tokens.push_back({TokenKind::PLUS, "+", start_loc}); continue;
            case '-':
                if (peek() == '>') {
                    advance();
                    tokens.push_back({TokenKind::ARROW, "->", start_loc});
                } else {
                    tokens.push_back({TokenKind::MINUS, "-", start_loc});
                }
                continue;
            case '*': tokens.push_back({TokenKind::STAR, "*", start_loc}); continue;
            case '/': tokens.push_back({TokenKind::SLASH, "/", start_loc}); continue;
            case '%': tokens.push_back({TokenKind::PERCENT, "%", start_loc}); continue;
            case '=':
                if (peek() == '=') {
                    advance();
                    tokens.push_back({TokenKind::EQ_EQ, "==", start_loc});
                } else if (peek() == '>') {
                    advance();
                    tokens.push_back({TokenKind::FAT_ARROW, "=>", start_loc});
                } else {
                    tokens.push_back({TokenKind::EQUAL, "=", start_loc});
                }
                continue;
            case '!':
                if (peek() == '=') {
                    advance();
                    tokens.push_back({TokenKind::BANG_EQ, "!=", start_loc});
                    continue;
                }
                return CompileError{ErrorCode::E102, start_loc, "unexpected character '!' (did you mean '!=')"};
            case '<':
                if (peek() == '=') {
                    advance();
                    tokens.push_back({TokenKind::LT_EQ, "<=", start_loc});
                } else {
                    tokens.push_back({TokenKind::LT, "<", start_loc});
                }
                continue;
            case '>':
                if (peek() == '=') {
                    advance();
                    tokens.push_back({TokenKind::GT_EQ, ">=", start_loc});
                } else {
                    tokens.push_back({TokenKind::GT, ">", start_loc});
                }
                continue;
            case '.': tokens.push_back({TokenKind::DOT, ".", start_loc}); continue;
            case ',': tokens.push_back({TokenKind::COMMA, ",", start_loc}); continue;
            case ';': tokens.push_back({TokenKind::SEMICOLON, ";", start_loc}); continue;
            case ':': tokens.push_back({TokenKind::COLON, ":", start_loc}); continue;
            case '(': tokens.push_back({TokenKind::LPAREN, "(", start_loc}); continue;
            case ')': tokens.push_back({TokenKind::RPAREN, ")", start_loc}); continue;
            case '[': tokens.push_back({TokenKind::LBRACKET, "[", start_loc}); continue;
            case ']': tokens.push_back({TokenKind::RBRACKET, "]", start_loc}); continue;
            case '{': tokens.push_back({TokenKind::LBRACE, "{", start_loc}); continue;
            case '}': tokens.push_back({TokenKind::RBRACE, "}", start_loc}); continue;

            case '"': {
                // String literal
                std::string str_val;
                bool terminated = false;
                while (!is_at_end()) {
                    char sc = peek();
                    if (sc == '"') {
                        advance();
                        terminated = true;
                        break;
                    }
                    if (sc == '\n' || sc == '\r') {
                        // Unterminated across line without escaping
                        return CompileError{ErrorCode::E101, start_loc, "unterminated string literal"};
                    }
                    if (sc == '\\') {
                        SourceLocation escape_loc{line_, col_};
                        advance(); // consume '\'
                        if (is_at_end()) {
                            return CompileError{ErrorCode::E101, escape_loc, "unterminated escape sequence in string literal"};
                        }
                        char esc = advance();
                        if (esc == '"') str_val.push_back('"');
                        else if (esc == '\\') str_val.push_back('\\');
                        else if (esc == 'n') str_val.push_back('\n');
                        else if (esc == 't') str_val.push_back('\t');
                        else {
                            return CompileError{ErrorCode::E101, escape_loc, std::string("bad escape character '\\") + esc + "' in string literal"};
                        }
                    } else {
                        str_val.push_back(advance());
                    }
                }
                if (!terminated) {
                    return CompileError{ErrorCode::E101, start_loc, "unterminated string literal"};
                }
                tokens.push_back({TokenKind::STRING_LIT, str_val, start_loc});
                continue;
            }

            default:
                break;
        }

        // Numbers: INT [0-9]+ or FLOAT [0-9]+\.[0-9]+
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num_str;
            num_str.push_back(c);
            while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                num_str.push_back(advance());
            }

            // Check if float: dot followed by digit
            if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
                num_str.push_back(advance()); // consume '.'
                while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    num_str.push_back(advance());
                }
                tokens.push_back({TokenKind::FLOAT_LIT, num_str, start_loc});
            } else {
                tokens.push_back({TokenKind::INT_LIT, num_str, start_loc});
            }
            continue;
        }

        // Identifiers and Keywords: [A-Za-z_][A-Za-z0-9_]*
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string ident_str;
            ident_str.push_back(c);
            while (!is_at_end()) {
                char next_c = peek();
                if (std::isalnum(static_cast<unsigned char>(next_c)) || next_c == '_') {
                    ident_str.push_back(advance());
                } else {
                    break;
                }
            }
            TokenKind kind = check_keyword_or_ident(ident_str);
            tokens.push_back({kind, ident_str, start_loc});
            continue;
        }

        // Unexpected character
        return CompileError{ErrorCode::E102, start_loc, std::string("unexpected character '") + c + "'"};
    }

    return tokens;
}

} // namespace retroc
