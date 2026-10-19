#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "ir/builder.hpp"
#include "ir/dump.hpp"
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

static int dump_ast(const std::string& path) {
    std::string source = read_file(path);
    retroc::Lexer lexer(source);
    auto lex_res = lexer.tokenize();

    if (std::holds_alternative<retroc::CompileError>(lex_res)) {
        const auto& err = std::get<retroc::CompileError>(lex_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    const auto& tokens = std::get<std::vector<retroc::Token>>(lex_res);
    retroc::Parser parser(tokens);
    auto parse_res = parser.parse();

    if (std::holds_alternative<retroc::CompileError>(parse_res)) {
        const auto& err = std::get<retroc::CompileError>(parse_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    const auto& prog = std::get<retroc::Program>(parse_res);
    retroc::dump_ast(prog, std::cout);
    return 0;
}

static int check_program(const std::string& path) {
    std::string source = read_file(path);
    retroc::Lexer lexer(source);
    auto lex_res = lexer.tokenize();

    if (std::holds_alternative<retroc::CompileError>(lex_res)) {
        const auto& err = std::get<retroc::CompileError>(lex_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    const auto& tokens = std::get<std::vector<retroc::Token>>(lex_res);
    retroc::Parser parser(tokens);
    auto parse_res = parser.parse();

    if (std::holds_alternative<retroc::CompileError>(parse_res)) {
        const auto& err = std::get<retroc::CompileError>(parse_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    auto prog = std::get<retroc::Program>(std::move(parse_res));
    retroc::SemanticAnalyzer sema(std::move(prog));
    auto sema_res = sema.analyze();

    if (std::holds_alternative<retroc::CompileError>(sema_res)) {
        const auto& err = std::get<retroc::CompileError>(sema_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}

static int dump_ir_cmd(const std::string& path, bool verbose) {
    std::string source = read_file(path);
    retroc::Lexer lexer(source);
    auto lex_res = lexer.tokenize();

    if (std::holds_alternative<retroc::CompileError>(lex_res)) {
        const auto& err = std::get<retroc::CompileError>(lex_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    const auto& tokens = std::get<std::vector<retroc::Token>>(lex_res);
    retroc::Parser parser(tokens);
    auto parse_res = parser.parse();

    if (std::holds_alternative<retroc::CompileError>(parse_res)) {
        const auto& err = std::get<retroc::CompileError>(parse_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    auto prog = std::get<retroc::Program>(std::move(parse_res));
    retroc::SemanticAnalyzer sema(std::move(prog));
    auto sema_res = sema.analyze();

    if (std::holds_alternative<retroc::CompileError>(sema_res)) {
        const auto& err = std::get<retroc::CompileError>(sema_res);
        std::cerr << err.format() << std::endl;
        return 1;
    }

    const auto& typed_prog = std::get<retroc::TypedProgram>(sema_res);
    retroc::IRBuilder builder(typed_prog);
    retroc::Graph graph = builder.build();

    retroc::dump_ir(graph, std::cout, verbose);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "RetroC compiler runtime v0.1.0\n";
        std::cout << "Usage: retroc <command> [args...]\n";
        std::cout << "Commands:\n";
        std::cout << "  dump-tokens <file.rc>           Tokenize file and print token stream\n";
        std::cout << "  dump-ast <file.rc>              Parse file and print AST\n";
        std::cout << "  check <file.rc>                 Typecheck file and verify semantic rules\n";
        std::cout << "  dump-ir [--verbose] <file.rc>   Lower to IR and dump canonical graph\n";
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
    if (cmd == "dump-ast") {
        if (argc < 3) {
            std::cerr << "Usage: retroc dump-ast <file.rc>\n";
            return 1;
        }
        return dump_ast(argv[2]);
    }
    if (cmd == "check") {
        if (argc < 3) {
            std::cerr << "Usage: retroc check <file.rc>\n";
            return 1;
        }
        return check_program(argv[2]);
    }
    if (cmd == "dump-ir") {
        if (argc < 3) {
            std::cerr << "Usage: retroc dump-ir [--verbose] <file.rc>\n";
            return 1;
        }
        bool verbose = false;
        std::string file_path;
        if (std::string(argv[2]) == "--verbose") {
            if (argc < 4) {
                std::cerr << "Usage: retroc dump-ir [--verbose] <file.rc>\n";
                return 1;
            }
            verbose = true;
            file_path = argv[3];
        } else {
            file_path = argv[2];
        }
        return dump_ir_cmd(file_path, verbose);
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}


