#include "edit/edit.hpp"
#include "lexer/lexer.hpp"
#include <sstream>
#include <cctype>

namespace retroc {

namespace {

// Helper to skip whitespace and check comments
void skip_whitespace_and_comments(const std::string& line, size_t& pos) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '\r')) {
        pos++;
    }
}

} // namespace

std::variant<EditScript, CompileError> EditScriptParser::parse() {
    EditScript script;
    std::istringstream stream(text_);
    std::string line;
    int line_num = 0;

    while (std::getline(stream, line)) {
        line_num++;
        size_t pos = 0;
        skip_whitespace_and_comments(line, pos);

        if (pos >= line.size()) {
            continue; // empty line
        }

        // Check for comment //
        if (pos + 1 < line.size() && line[pos] == '/' && line[pos + 1] == '/') {
            continue;
        }

        SourceLocation line_loc{line_num, static_cast<int>(pos + 1)};

        // Tokenize line using Lexer
        std::string line_content = line.substr(pos);
        // Strip trailing comment if present
        size_t c_idx = 0;
        bool in_str = false;
        while (c_idx < line_content.size()) {
            if (line_content[c_idx] == '"' && (c_idx == 0 || line_content[c_idx - 1] != '\\')) {
                in_str = !in_str;
            } else if (!in_str && c_idx + 1 < line_content.size() && line_content[c_idx] == '/' && line_content[c_idx + 1] == '/') {
                line_content = line_content.substr(0, c_idx);
                break;
            }
            c_idx++;
        }

        Lexer lexer(line_content);
        auto tok_res = lexer.tokenize();
        if (std::holds_alternative<CompileError>(tok_res)) {
            auto err = std::get<CompileError>(tok_res);
            err.loc.line = line_num;
            err.loc.col += static_cast<int>(pos);
            return err;
        }

        const auto& tokens = std::get<std::vector<Token>>(tok_res);
        if (tokens.empty() || (tokens.size() == 1 && tokens[0].kind == TokenKind::END_OF_FILE)) {
            continue;
        }

        size_t t = 0;
        auto peek = [&](size_t offset = 0) -> const Token& {
            if (t + offset < tokens.size()) return tokens[t + offset];
            return tokens.back();
        };

        auto match_ident = [&](const std::string& name) -> bool {
            if (peek().kind == TokenKind::IDENT && peek().text == name) {
                t++;
                return true;
            }
            return false;
        };

        if (!match_ident("retro")) {
            return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                "expected 'retro', found '" + peek().text + "'"};
        }

        if (match_ident("modify")) {
            // Expected: <input>[<RecordId>].<field> = <literal>
            // or: <input>[<RecordId>] = <literal> (if element is primitive)
            if (peek().kind != TokenKind::IDENT) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected input stage identifier after 'retro modify', found '" + peek().text + "'"};
            }
            std::string input_stage = peek().text;
            t++;

            if (peek().kind != TokenKind::LBRACKET) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected '[' after input stage name, found '" + peek().text + "'"};
            }
            t++;

            if (peek().kind != TokenKind::INT_LIT) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected integer RecordId inside '[]', found '" + peek().text + "'"};
            }
            RecordId record_id = 0;
            try {
                record_id = static_cast<RecordId>(std::stoull(peek().text));
            } catch (...) {
                return CompileError{ErrorCode::E601, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "invalid RecordId integer literal '" + peek().text + "'"};
            }
            t++;

            if (peek().kind != TokenKind::RBRACKET) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected ']' after RecordId, found '" + peek().text + "'"};
            }
            t++;

            std::string field_name;
            if (peek().kind == TokenKind::DOT) {
                t++;
                if (peek().kind != TokenKind::IDENT) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected field name after '.', found '" + peek().text + "'"};
                }
                field_name = peek().text;
                t++;
            }

            if (peek().kind != TokenKind::EQUAL) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected '=' before literal value, found '" + peek().text + "'"};
            }
            t++;

            // Literal value: INT_LIT, FLOAT_LIT, STRING_LIT, KW_TRUE, KW_FALSE, or MINUS + INT_LIT/FLOAT_LIT
            bool negative = false;
            if (peek().kind == TokenKind::MINUS) {
                negative = true;
                t++;
            }

            Value lit_val;
            std::string lit_text;
            if (peek().kind == TokenKind::INT_LIT) {
                int64_t v = std::stoll(peek().text);
                if (negative) v = -v;
                lit_val = Value::make_int(v);
                lit_text = negative ? ("-" + peek().text) : peek().text;
                t++;
            } else if (peek().kind == TokenKind::FLOAT_LIT) {
                double v = std::stod(peek().text);
                if (negative) v = -v;
                lit_val = Value::make_float(v);
                lit_text = negative ? ("-" + peek().text) : peek().text;
                t++;
            } else if (peek().kind == TokenKind::STRING_LIT) {
                if (negative) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "unexpected '-' before string literal"};
                }
                lit_val = Value::make_string(peek().text);
                lit_text = "\"" + peek().text + "\"";
                t++;
            } else if (peek().kind == TokenKind::KW_TRUE) {
                if (negative) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "unexpected '-' before bool literal"};
                }
                lit_val = Value::make_bool(true);
                lit_text = "true";
                t++;
            } else if (peek().kind == TokenKind::KW_FALSE) {
                if (negative) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "unexpected '-' before bool literal"};
                }
                lit_val = Value::make_bool(false);
                lit_text = "false";
                t++;
            } else {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected literal value (int, float, bool, string), found '" + peek().text + "'"};
            }

            if (peek().kind != TokenKind::END_OF_FILE) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "unexpected token after literal: '" + peek().text + "'"};
            }

            auto edit = std::make_shared<ModifyEdit>();
            edit->loc = line_loc;
            edit->input_stage = input_stage;
            edit->record_id = record_id;
            edit->field_name = field_name;
            edit->new_value = std::move(lit_val);
            edit->raw_literal_text = lit_text;
            script.edits.push_back(std::move(edit));
        } else if (match_ident("insert")) {
            // retro insert into <input> : <literal>   (Phase 12.3)
            if (match_ident("into")) {
                if (peek().kind != TokenKind::IDENT) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected input stage name after 'retro insert into', found '" + peek().text + "'"};
                }
                std::string input_stage = peek().text;
                t++;
                if (peek().kind != TokenKind::COLON) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected ':' before the record literal, found '" + peek().text + "'"};
                }
                t++;
                if (peek().kind == TokenKind::END_OF_FILE) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected a literal after ':'"};
                }
                // Keep the literal's source text (string tokens drop their quotes);
                // it is type-checked against the input when the edit is applied.
                std::string literal = line_content.substr(static_cast<size_t>(peek().loc.col - 1));
                while (!literal.empty() && std::isspace(static_cast<unsigned char>(literal.back()))) {
                    literal.pop_back();
                }
                auto rec_edit = std::make_shared<InsertRecordEdit>();
                rec_edit->loc = line_loc;
                rec_edit->input_stage = input_stage;
                rec_edit->raw_literal_text = literal;
                script.edits.push_back(std::move(rec_edit));
                continue;
            }
            // retro insert before <stage> [as <name>] : map(_, <lambda>)
            // or: filter(_, <lambda>)
            if (!match_ident("before")) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected 'before' after 'retro insert', found '" + peek().text + "'"};
            }

            if (peek().kind != TokenKind::IDENT) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected target stage name after 'before', found '" + peek().text + "'"};
            }
            std::string before_stage = peek().text;
            t++;

            std::string new_stage_name;
            if (match_ident("as")) {
                if (peek().kind != TokenKind::IDENT) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected stage alias identifier after 'as', found '" + peek().text + "'"};
                }
                new_stage_name = peek().text;
                t++;
            }

            if (peek().kind != TokenKind::COLON) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected ':' before stage expression, found '" + peek().text + "'"};
            }
            t++; // past ':'

            if (peek().kind != TokenKind::IDENT || (peek().text != "map" && peek().text != "filter")) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected 'map' or 'filter' in inserted stage, found '" + peek().text + "'"};
            }
            std::string builtin_name = peek().text;
            t++;

            if (peek().kind != TokenKind::LPAREN) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected '(' after '" + builtin_name + "'"};
            }
            t++;

            // Expect '_'
            if (peek().kind != TokenKind::IDENT || peek().text != "_") {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected '_' as first argument to inserted stage, found '" + peek().text + "'"};
            }
            t++;

            if (peek().kind != TokenKind::COMMA) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected ',' after '_'"};
            }
            t++;

            // Extract the lambda expression text up to the matching closing ')'
            // We can gather tokens until the last RPAREN before EOF
            size_t lambda_start_tok = t;
            int paren_depth = 1;
            while (t < tokens.size() && paren_depth > 0) {
                if (tokens[t].kind == TokenKind::LPAREN) paren_depth++;
                else if (tokens[t].kind == TokenKind::RPAREN) {
                    paren_depth--;
                    if (paren_depth == 0) break;
                }
                t++;
            }

            if (paren_depth != 0) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "unclosed ')' in inserted stage call"};
            }

            size_t lambda_end_tok = t;
            t++; // past final ')'

            if (peek().kind != TokenKind::END_OF_FILE) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "unexpected token after inserted stage: '" + peek().text + "'"};
            }

            // Reconstruct raw lambda text from tokens
            std::string raw_lambda;
            for (size_t k = lambda_start_tok; k < lambda_end_tok; ++k) {
                if (!raw_lambda.empty()) raw_lambda += " ";
                raw_lambda += tokens[k].text;
            }

            auto ins_edit = std::make_shared<InsertEdit>();
            ins_edit->loc = line_loc;
            ins_edit->before_stage = before_stage;
            ins_edit->new_stage_name = new_stage_name;
            ins_edit->builtin_name = builtin_name;
            ins_edit->raw_lambda_text = raw_lambda;
            script.edits.push_back(std::move(ins_edit));
        } else if (match_ident("delete")) {
            // retro delete <stage>
            if (peek().kind != TokenKind::IDENT) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "expected stage name to delete after 'retro delete', found '" + peek().text + "'"};
            }
            std::string stage_name = peek().text;
            t++;

            // retro delete <input>[<RecordId>]   (Phase 12.3)
            if (peek().kind == TokenKind::LBRACKET) {
                t++;
                if (peek().kind != TokenKind::INT_LIT) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected integer RecordId inside '[]', found '" + peek().text + "'"};
                }
                RecordId record_id = 0;
                try {
                    record_id = static_cast<RecordId>(std::stoull(peek().text));
                } catch (...) {
                    return CompileError{ErrorCode::E601, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "invalid RecordId integer literal '" + peek().text + "'"};
                }
                t++;
                if (peek().kind != TokenKind::RBRACKET) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "expected ']' after RecordId, found '" + peek().text + "'"};
                }
                t++;
                if (peek().kind != TokenKind::END_OF_FILE) {
                    return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                        "unexpected token after 'retro delete <input>[<RecordId>]': '" + peek().text + "'"};
                }
                auto rec_edit = std::make_shared<DeleteRecordEdit>();
                rec_edit->loc = line_loc;
                rec_edit->input_stage = stage_name;
                rec_edit->record_id = record_id;
                script.edits.push_back(std::move(rec_edit));
                continue;
            }

            if (peek().kind != TokenKind::END_OF_FILE) {
                return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                    "unexpected token after 'retro delete <stage>': '" + peek().text + "'"};
            }

            auto del_edit = std::make_shared<DeleteEdit>();
            del_edit->loc = line_loc;
            del_edit->stage_name = stage_name;
            script.edits.push_back(std::move(del_edit));
        } else {
            return CompileError{ErrorCode::E201, SourceLocation{line_num, peek().loc.col + static_cast<int>(pos)},
                                "unknown edit command '" + peek().text + "', expected 'modify', 'insert', or 'delete'"};
        }
    }

    return script;
}

} // namespace retroc
