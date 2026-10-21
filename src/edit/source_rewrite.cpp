#include "edit/source_rewrite.hpp"
#include "lexer/lexer.hpp"
#include <sstream>
#include <vector>

namespace retroc {

std::variant<std::string, CompileError> apply_modify_to_source(const std::string& source_code,
                                                              const ModifyEdit& edit) {
    // We want to locate:
    // input <edit.input_stage> ... = [ ... { ... <edit.field_name>: <old_val> ... } ... ]
    // and replace <old_val> with edit.raw_literal_text.
    // We can use the lexer to find tokens cleanly.
    Lexer lexer(source_code);
    auto tok_res = lexer.tokenize();
    if (std::holds_alternative<CompileError>(tok_res)) {
        return std::get<CompileError>(tok_res);
    }
    const auto& tokens = std::get<std::vector<Token>>(tok_res);

    // Find input <stage_name>
    size_t i = 0;
    bool found_input = false;
    while (i < tokens.size()) {
        if (tokens[i].kind == TokenKind::KW_INPUT && i + 1 < tokens.size() &&
            tokens[i + 1].kind == TokenKind::IDENT && tokens[i + 1].text == edit.input_stage) {
            found_input = true;
            i += 2;
            break;
        }
        i++;
    }

    if (!found_input) {
        return CompileError{ErrorCode::E601, edit.loc, "source rewrite: input stage '" + edit.input_stage + "' not found"};
    }

    // Skip until '='
    while (i < tokens.size() && tokens[i].kind != TokenKind::EQUAL) {
        i++;
    }
    if (i >= tokens.size()) {
        return CompileError{ErrorCode::E601, edit.loc, "source rewrite: '=' not found in input decl"};
    }
    i++; // past '='

    // Expect '['
    while (i < tokens.size() && tokens[i].kind != TokenKind::LBRACKET) {
        i++;
    }
    if (i >= tokens.size()) {
        return CompileError{ErrorCode::E601, edit.loc, "source rewrite: '[' not found"};
    }
    i++; // past '['

    // Find record by RecordId (0-indexed element)
    RecordId current_record = 0;
    while (i < tokens.size()) {
        if (tokens[i].kind == TokenKind::RBRACKET) {
            break;
        }

        if (tokens[i].kind == TokenKind::LBRACE) {
            // Start of a record
            if (current_record == edit.record_id) {
                // Inside target record! Find <field_name>
                i++; // past '{'
                while (i < tokens.size() && tokens[i].kind != TokenKind::RBRACE) {
                    if (tokens[i].kind == TokenKind::IDENT && tokens[i].text == edit.field_name) {
                        i++; // past field name
                        if (i < tokens.size() && tokens[i].kind == TokenKind::COLON) {
                            i++; // past ':'
                            // Now tokens[i] (and possible unary minus) is the value to replace!
                            const Token& start_tok = tokens[i];
                            const Token* end_tok = &tokens[i];
                            if (tokens[i].kind == TokenKind::MINUS && i + 1 < tokens.size()) {
                                end_tok = &tokens[i + 1];
                            }

                            // Calculate line/col in source_code to replace exact substring
                            // We can build the new source code by lines or character offsets
                            std::istringstream stream(source_code);
                            std::string line;
                            std::vector<std::string> lines;
                            while (std::getline(stream, line)) {
                                lines.push_back(line);
                            }

                            size_t l_idx = start_tok.loc.line - 1;
                            if (l_idx < lines.size()) {
                                size_t c_start = start_tok.loc.col - 1;
                                size_t c_end = end_tok->loc.col - 1 + end_tok->text.size();
                                std::string prefix = lines[l_idx].substr(0, c_start);
                                std::string suffix = (c_end <= lines[l_idx].size()) ? lines[l_idx].substr(c_end) : "";
                                lines[l_idx] = prefix + edit.raw_literal_text + suffix;

                                std::ostringstream out;
                                for (size_t l = 0; l < lines.size(); ++l) {
                                    out << lines[l];
                                    if (l + 1 < lines.size() || (source_code.back() == '\n')) {
                                        out << "\n";
                                    }
                                }
                                return out.str();
                            }
                        }
                    }
                    i++;
                }
                return CompileError{ErrorCode::E601, edit.loc, "source rewrite: field '" + edit.field_name + "' not found"};
            } else {
                // Skip to matching '}'
                int brace_depth = 1;
                i++;
                while (i < tokens.size() && brace_depth > 0) {
                    if (tokens[i].kind == TokenKind::LBRACE) brace_depth++;
                    else if (tokens[i].kind == TokenKind::RBRACE) brace_depth--;
                    i++;
                }
                current_record++;
                continue;
            }
        } else if (tokens[i].kind == TokenKind::INT_LIT || tokens[i].kind == TokenKind::FLOAT_LIT ||
                   tokens[i].kind == TokenKind::STRING_LIT || tokens[i].kind == TokenKind::KW_TRUE ||
                   tokens[i].kind == TokenKind::KW_FALSE || tokens[i].kind == TokenKind::MINUS) {
            // Primitive element in array
            if (current_record == edit.record_id && edit.field_name.empty()) {
                const Token& start_tok = tokens[i];
                const Token* end_tok = &tokens[i];
                if (tokens[i].kind == TokenKind::MINUS && i + 1 < tokens.size()) {
                    end_tok = &tokens[i + 1];
                }

                std::istringstream stream(source_code);
                std::string line;
                std::vector<std::string> lines;
                while (std::getline(stream, line)) {
                    lines.push_back(line);
                }

                size_t l_idx = start_tok.loc.line - 1;
                if (l_idx < lines.size()) {
                    size_t c_start = start_tok.loc.col - 1;
                    size_t c_end = end_tok->loc.col - 1 + end_tok->text.size();
                    std::string prefix = lines[l_idx].substr(0, c_start);
                    std::string suffix = (c_end <= lines[l_idx].size()) ? lines[l_idx].substr(c_end) : "";
                    lines[l_idx] = prefix + edit.raw_literal_text + suffix;

                    std::ostringstream out;
                    for (size_t l = 0; l < lines.size(); ++l) {
                        out << lines[l];
                        if (l + 1 < lines.size() || (source_code.back() == '\n')) {
                            out << "\n";
                        }
                    }
                    return out.str();
                }
            }
            if (tokens[i].kind == TokenKind::MINUS) i++;
            current_record++;
        }
        i++;
    }

    return CompileError{ErrorCode::E601, edit.loc, "source rewrite: RecordId " + std::to_string(edit.record_id) + " not found"};
}

std::variant<std::string, CompileError> apply_insert_to_source(const std::string& source_code,
                                                              const InsertEdit& edit,
                                                              const std::string& actual_stage_name) {
    // We want to insert `let <actual_stage_name> = <builtin>(<first_input>, <lambda>);\n`
    // immediately before the definition of `let <edit.before_stage> = ...;`
    // and replace the first argument of <edit.before_stage> with <actual_stage_name>.
    Lexer lexer(source_code);
    auto tok_res = lexer.tokenize();
    if (std::holds_alternative<CompileError>(tok_res)) {
        return std::get<CompileError>(tok_res);
    }
    const auto& tokens = std::get<std::vector<Token>>(tok_res);

    // Find `let <edit.before_stage>`
    size_t target_tok_idx = 0;
    bool found_stage = false;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].kind == TokenKind::KW_LET && tokens[i + 1].kind == TokenKind::IDENT &&
            tokens[i + 1].text == edit.before_stage) {
            target_tok_idx = i;
            found_stage = true;
            break;
        }
    }

    if (!found_stage) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "source rewrite: stage '" + edit.before_stage + "' not found"};
    }

    // Now find the builtin call in `<edit.before_stage>`: `let <before_stage> = <builtin>(<first_input>, ...)`
    // Find '=' then builtin then '(' then first_input
    size_t eq_idx = target_tok_idx;
    while (eq_idx < tokens.size() && tokens[eq_idx].kind != TokenKind::EQUAL &&
           tokens[eq_idx].kind != TokenKind::SEMICOLON) {
        eq_idx++;
    }
    if (eq_idx >= tokens.size() || tokens[eq_idx].kind != TokenKind::EQUAL) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "source rewrite: '=' not found for stage '" + edit.before_stage + "'"};
    }

    size_t lparen_idx = eq_idx;
    while (lparen_idx < tokens.size() && tokens[lparen_idx].kind != TokenKind::LPAREN &&
           tokens[lparen_idx].kind != TokenKind::SEMICOLON) {
        lparen_idx++;
    }
    if (lparen_idx >= tokens.size() || lparen_idx + 1 >= tokens.size() ||
        tokens[lparen_idx].kind != TokenKind::LPAREN || tokens[lparen_idx + 1].kind != TokenKind::IDENT) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "source rewrite: collection call not found in stage '" + edit.before_stage + "'"};
    }

    const Token& first_input_tok = tokens[lparen_idx + 1];
    std::string first_input_name = first_input_tok.text;

    // Build the new let stage line to insert
    std::string new_stage_line = "let " + actual_stage_name + " = " + edit.builtin_name +
                                 "(" + first_input_name + ", " + edit.raw_lambda_text + ");\n";

    // Split source_code into lines
    std::istringstream stream(source_code);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    // Replace first_input in target line with actual_stage_name
    size_t in_line_idx = first_input_tok.loc.line - 1;
    if (in_line_idx < lines.size()) {
        size_t c_start = first_input_tok.loc.col - 1;
        size_t c_end = c_start + first_input_tok.text.size();
        std::string prefix = lines[in_line_idx].substr(0, c_start);
        std::string suffix = (c_end <= lines[in_line_idx].size()) ? lines[in_line_idx].substr(c_end) : "";
        lines[in_line_idx] = prefix + actual_stage_name + suffix;
    }

    // Insert new_stage_line before let_tok line
    size_t let_line_idx = tokens[target_tok_idx].loc.line - 1;
    std::ostringstream out;
    for (size_t l = 0; l < lines.size(); ++l) {
        if (l == let_line_idx) {
            out << new_stage_line;
        }
        out << lines[l];
        if (l + 1 < lines.size() || (source_code.back() == '\n')) {
            out << "\n";
        }
    }
    return out.str();
}

std::variant<std::string, CompileError> apply_delete_to_source(const std::string& source_code,
                                                              const DeleteEdit& edit,
                                                              const std::string& input_stage_name) {
    // Delete stage <edit.stage_name> definition line and replace references to <edit.stage_name> with <input_stage_name>
    Lexer lexer(source_code);
    auto tok_res = lexer.tokenize();
    if (std::holds_alternative<CompileError>(tok_res)) {
        return std::get<CompileError>(tok_res);
    }
    const auto& tokens = std::get<std::vector<Token>>(tok_res);

    int delete_line = -1;
    std::vector<Token> to_replace;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind == TokenKind::KW_LET && i + 1 < tokens.size() &&
            tokens[i + 1].kind == TokenKind::IDENT && tokens[i + 1].text == edit.stage_name) {
            delete_line = tokens[i].loc.line;
            i++; // skip ident
            continue;
        }
        if (tokens[i].kind == TokenKind::IDENT && tokens[i].text == edit.stage_name) {
            to_replace.push_back(tokens[i]);
        }
    }

    if (delete_line == -1) {
        return CompileError{ErrorCode::E601, edit.loc,
                            "source rewrite: stage '" + edit.stage_name + "' not found to delete"};
    }

    std::istringstream stream(source_code);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    // Replace references backwards per line so column offsets stay valid
    for (auto it = to_replace.rbegin(); it != to_replace.rend(); ++it) {
        size_t l_idx = it->loc.line - 1;
        if (l_idx < lines.size() && static_cast<int>(it->loc.line) != delete_line) {
            size_t c_start = it->loc.col - 1;
            size_t c_end = c_start + it->text.size();
            std::string prefix = lines[l_idx].substr(0, c_start);
            std::string suffix = (c_end <= lines[l_idx].size()) ? lines[l_idx].substr(c_end) : "";
            lines[l_idx] = prefix + input_stage_name + suffix;
        }
    }

    std::ostringstream out;
    for (size_t l = 0; l < lines.size(); ++l) {
        if (static_cast<int>(l + 1) == delete_line) {
            continue; // delete this line
        }
        out << lines[l];
        if (l + 1 < lines.size() || (source_code.back() == '\n')) {
            out << "\n";
        }
    }
    return out.str();
}

} // namespace retroc
