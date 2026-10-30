#pragma once

#include "common/value.hpp"
#include "common/error.hpp"
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace retroc {

enum class EditKind {
    MODIFY,
    INSERT,
    DELETE,
    INSERT_RECORD,
    DELETE_RECORD
};

struct Edit {
    virtual ~Edit() = default;
    virtual EditKind kind() const = 0;
    virtual std::string to_string() const = 0;

    SourceLocation loc;
};

// Edit A: retro modify <input>[<RecordId>].<field> = <literal>
struct ModifyEdit : public Edit {
    std::string input_stage;
    RecordId record_id = 0;
    std::string field_name; // empty if modifying scalar/primitive element directly, or field name
    Value new_value;
    std::string raw_literal_text;

    EditKind kind() const override { return EditKind::MODIFY; }

    std::string to_string() const override {
        std::string s = "retro modify " + input_stage + "[" + std::to_string(record_id) + "]";
        if (!field_name.empty()) {
            s += "." + field_name;
        }
        s += " = " + new_value.to_string();
        return s;
    }
};

// Edit B: retro insert before <stage> : map(_, <lambda>) [as <name>]
// or:     retro insert before <stage> as <name> : map(_, <lambda>)
struct InsertEdit : public Edit {
    std::string before_stage;
    std::string new_stage_name; // if empty, defaults to <before_stage>$ins<k>
    std::string builtin_name;   // "map" or "filter"
    std::string raw_lambda_text; // e.g. "r => clamp(r, -1.0, 1.0)"

    EditKind kind() const override { return EditKind::INSERT; }

    std::string to_string() const override {
        std::string s = "retro insert before " + before_stage;
        if (!new_stage_name.empty()) {
            s += " as " + new_stage_name;
        }
        s += " : " + builtin_name + "(_, " + raw_lambda_text + ")";
        return s;
    }
};

// Edit C: retro delete <stage>
struct DeleteEdit : public Edit {
    std::string stage_name;

    EditKind kind() const override { return EditKind::DELETE; }

    std::string to_string() const override {
        return "retro delete " + stage_name;
    }
};

// Phase 12.3: retro insert into <input> : <literal>
// The new record gets a fresh RecordId (larger than any used before in the
// input) and is appended at the end.
struct InsertRecordEdit : public Edit {
    std::string input_stage;
    std::string raw_literal_text; // e.g. "{ id: 7, v: 1.5, ok: true }"

    EditKind kind() const override { return EditKind::INSERT_RECORD; }

    std::string to_string() const override {
        return "retro insert into " + input_stage + " : " + raw_literal_text;
    }
};

// Phase 12.3: retro delete <input>[<RecordId>]
struct DeleteRecordEdit : public Edit {
    std::string input_stage;
    RecordId record_id = 0;

    EditKind kind() const override { return EditKind::DELETE_RECORD; }

    std::string to_string() const override {
        return "retro delete " + input_stage + "[" + std::to_string(record_id) + "]";
    }
};

struct EditScript {
    std::vector<std::shared_ptr<Edit>> edits;
};

class EditScriptParser {
public:
    explicit EditScriptParser(std::string text) : text_(std::move(text)) {}

    std::variant<EditScript, CompileError> parse();

private:
    std::string text_;
};

} // namespace retroc
