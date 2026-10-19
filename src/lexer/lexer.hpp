#pragma once

#include "lexer/token.hpp"
#include "common/error.hpp"
#include <string>
#include <vector>
#include <variant>

namespace retroc {

class Lexer {
public:
    explicit Lexer(std::string source);

    // Tokenizes until EOF. If a lexical error occurs, returns CompileError.
    std::variant<std::vector<Token>, CompileError> tokenize();

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool is_at_end() const;
    void skip_whitespace_and_comments();

    TokenKind check_keyword_or_ident(const std::string& text) const;

    std::string source_;
    size_t cursor_{0};
    int line_{1};
    int col_{1};
};

} // namespace retroc
