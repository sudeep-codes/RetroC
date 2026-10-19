#include "lexer/lexer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open file " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static int dump_tokens(const std::string& path) {
    std::string source = read_file(path);
    retroc::Lexer lexer(source);
    auto res = lexer.tokenize();

    if (std::holds_alternative<retroc::CompileError>(res)) {
        const auto& err = std::get<retroc::CompileError>(res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    const auto& tokens = std::get<std::vector<retroc::Token>>(res);
    for (const auto& tok : tokens) {
        std::cout << tok.loc.line << ":" << tok.loc.col << " "
                  << retroc::token_kind_to_string(tok.kind);
        if (tok.kind == retroc::TokenKind::IDENT ||
            tok.kind == retroc::TokenKind::INT_LIT ||
            tok.kind == retroc::TokenKind::FLOAT_LIT ||
            tok.kind == retroc::TokenKind::STRING_LIT) {
            std::cout << "(\"" << tok.text << "\")";
        }
        std::cout << "\n";
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "RetroC compiler runtime v0.1.0\n";
        std::cout << "Usage: retroc <command> [args...]\n";
        std::cout << "Commands:\n";
        std::cout << "  dump-tokens <file.rc>   Tokenize file and print token stream\n";
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "dump-tokens") {
        if (argc < 3) {
            std::cerr << "Usage: retroc dump-tokens <file.rc>\n";
            return 1;
        }
        return dump_tokens(argv[2]);
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
