#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "ir/builder.hpp"
#include "ir/dump.hpp"
#include "runtime/executor.hpp"
#include "bytecode/bytecode.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

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

static int run_cmd(const std::string& path, bool verbose) {
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

    // Source directory for relative CSV path loading
    std::filesystem::path p(path);
    std::string source_dir = p.parent_path().string();
    if (source_dir.empty()) {
        source_dir = ".";
    }

    retroc::GraphExecutor executor(graph, source_dir);
    try {
        executor.run(&std::cout);
        if (verbose) {
            std::cout << "\n--- Execution & Memo Stats ---\n";
            for (const auto& node_id : graph.order) {
                const auto* n = graph.get_node(node_id);
                if (n) {
                    std::cout << n->id << " " << n->name
                              << ": version=" << n->version
                              << " memo_entries=" << n->memo.size()
                              << " memo_bytes=" << n->memo.bytes() << "\n";
                }
            }
            std::cout << "Total memo memory: " << executor.total_memo_bytes() << " bytes\n";
            const auto& history = executor.get_history();
            if (!history.entries.empty()) {
                const auto& last = history.entries.back();
                std::cout << "Ops recomputed: " << last.ops_recomputed
                          << ", Ops reused: " << last.ops_reused << "\n";
            }
        }
    } catch (const retroc::RuntimeError& err) {
        std::cerr << "Runtime error [E501]: " << err.message << std::endl;
        return 4; // Exit code 4: runtime error per ARCHITECTURE.md
    }
    return 0;
}

#include "edit/edit.hpp"
#include "reuse/propagator.hpp"
#include "verify/verifier.hpp"

static int edit_cmd(const std::string& prog_path, const std::string& edits_path) {
    std::string source = read_file(prog_path);
    if (source.empty()) return 1;

    std::string edits_text = read_file(edits_path);
    if (edits_text.empty()) return 1;

    retroc::Lexer lexer(source);
    auto lex_res = lexer.tokenize();
    if (std::holds_alternative<retroc::CompileError>(lex_res)) {
        std::cerr << std::get<retroc::CompileError>(lex_res).format() << std::endl;
        return 1;
    }

    retroc::Parser parser(std::get<std::vector<retroc::Token>>(lex_res));
    auto parse_res = parser.parse();
    if (std::holds_alternative<retroc::CompileError>(parse_res)) {
        std::cerr << std::get<retroc::CompileError>(parse_res).format() << std::endl;
        return 1;
    }

    retroc::SemanticAnalyzer sema(std::get<retroc::Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    if (std::holds_alternative<retroc::CompileError>(sema_res)) {
        std::cerr << std::get<retroc::CompileError>(sema_res).format() << std::endl;
        return 1;
    }

    const auto& typed_prog = std::get<retroc::TypedProgram>(sema_res);
    retroc::IRBuilder builder(typed_prog);
    retroc::Graph graph = builder.build();

    std::filesystem::path p(prog_path);
    std::string source_dir = p.parent_path().string();
    if (source_dir.empty()) source_dir = ".";

    retroc::GraphExecutor executor(graph, source_dir, /*enable_memo=*/true);
    try {
        executor.run();
    } catch (const retroc::RuntimeError& err) {
        std::cerr << "Runtime error [E501]: " << err.message << std::endl;
        return 4;
    }

    retroc::EditScriptParser edit_parser(edits_text);
    auto edit_res = edit_parser.parse();
    if (std::holds_alternative<retroc::CompileError>(edit_res)) {
        const auto& err = std::get<retroc::CompileError>(edit_res);
        std::cerr << err.format() << std::endl;
        return (err.code == retroc::ErrorCode::E601) ? 2 : 1;
    }

    const auto& script = std::get<retroc::EditScript>(edit_res);
    retroc::Propagator propagator(graph, executor);

    for (const auto& edit_ptr : script.edits) {
        if (edit_ptr->kind() == retroc::EditKind::MODIFY) {
            const auto& mod_edit = static_cast<const retroc::ModifyEdit&>(*edit_ptr);
            auto plan_res = propagator.apply_modify(mod_edit);
            if (std::holds_alternative<retroc::CompileError>(plan_res)) {
                const auto& err = std::get<retroc::CompileError>(plan_res);
                std::cerr << err.format() << std::endl;
                return 2; // Exit code 2: invalid edit per ARCHITECTURE.md
            }
            const auto& plan = std::get<retroc::RebuildPlan>(plan_res);
            std::cout << plan.to_string();
        } else if (edit_ptr->kind() == retroc::EditKind::INSERT) {
            const auto& ins_edit = static_cast<const retroc::InsertEdit&>(*edit_ptr);
            auto plan_res = propagator.apply_insert(ins_edit);
            if (std::holds_alternative<retroc::CompileError>(plan_res)) {
                const auto& err = std::get<retroc::CompileError>(plan_res);
                std::cerr << err.format() << std::endl;
                return 2; // Exit code 2: invalid edit per ARCHITECTURE.md
            }
            const auto& plan = std::get<retroc::RebuildPlan>(plan_res);
            std::cout << plan.to_string();
        } else if (edit_ptr->kind() == retroc::EditKind::DELETE) {
            const auto& del_edit = static_cast<const retroc::DeleteEdit&>(*edit_ptr);
            auto plan_res = propagator.apply_delete(del_edit);
            if (std::holds_alternative<retroc::CompileError>(plan_res)) {
                const auto& err = std::get<retroc::CompileError>(plan_res);
                std::cerr << err.format() << std::endl;
                return 2; // Exit code 2: invalid edit per ARCHITECTURE.md
            }
            const auto& plan = std::get<retroc::RebuildPlan>(plan_res);
            std::cout << plan.to_string();
        }
    }

    // Print final outputs
    for (const auto& [name, val] : executor.get_outputs()) {
        std::cout << name << " = " << val << "\n";
    }

    return 0;
}

static int verify_cmd(const std::string& prog_path, const std::string& edits_path) {
    std::string source = read_file(prog_path);
    if (source.empty()) return 1;

    std::string edits_text = read_file(edits_path);
    if (edits_text.empty()) return 1;

    retroc::EditScriptParser edit_parser(edits_text);
    auto edit_res = edit_parser.parse();
    if (std::holds_alternative<retroc::CompileError>(edit_res)) {
        const auto& err = std::get<retroc::CompileError>(edit_res);
        std::cerr << err.format() << std::endl;
        return (err.code == retroc::ErrorCode::E601) ? 2 : 1;
    }

    const auto& script = std::get<retroc::EditScript>(edit_res);
    std::filesystem::path p(prog_path);
    std::string source_dir = p.parent_path().string();
    if (source_dir.empty()) source_dir = ".";

    auto ver_res = retroc::verify_edits(source, script, source_dir);
    if (!ver_res.ok) {
        std::cerr << "Verification failed:\n" << ver_res.mismatch_details << std::endl;
        return 3; // Exit code 3: verification mismatch per ARCHITECTURE.md
    }

    std::cout << "Verification OK: all outputs match full rerun exactly.\n";
    for (const auto& [name, val] : ver_res.propagated_outputs) {
        std::cout << name << " = " << val << "\n";
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "RetroC compiler runtime v0.1.0\n";
        std::cout << "Usage: retroc <command> [args...]\n";
        std::cout << "Commands:\n";
        std::cout << "  run [--verbose] <file.rc>       Execute program from scratch\n";
        std::cout << "  dump-tokens <file.rc>           Tokenize file and print token stream\n";
        std::cout << "  dump-ast <file.rc>              Parse file and print AST\n";
        std::cout << "  check <file.rc>                 Typecheck file and verify semantic rules\n";
        std::cout << "  dump-ir [--verbose] <file.rc>   Lower to IR and dump canonical graph\n";
        std::cout << "  edit <file.rc> <script.rcs>     Apply edits and print rebuild plans\n";
        std::cout << "  verify <file.rc> <script.rcs>   Verify propagation against full rerun\n";
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "run") {
        if (argc < 3) {
            std::cerr << "Usage: retroc run [--verbose] <file.rc>\n";
            return 1;
        }
        bool verbose = false;
        std::string file_path;
        if (std::string(argv[2]) == "--verbose") {
            if (argc < 4) {
                std::cerr << "Usage: retroc run [--verbose] <file.rc>\n";
                return 1;
            }
            verbose = true;
            file_path = argv[3];
        } else {
            file_path = argv[2];
        }
        return run_cmd(file_path, verbose);
    }
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
    if (cmd == "edit") {
        if (argc < 4) {
            std::cerr << "Usage: retroc edit <file.rc> <script.rcs>\n";
            return 1;
        }
        return edit_cmd(argv[2], argv[3]);
    }
    if (cmd == "verify") {
        if (argc < 4) {
            std::cerr << "Usage: retroc verify <file.rc> <script.rcs>\n";
            return 1;
        }
        return verify_cmd(argv[2], argv[3]);
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}


