#pragma once

#include "parser/ast.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace retroc {

enum class ExprIRKind {
    CONST,
    VAR_PARAM,
    VAR_STAGE,
    FIELD,
    BINARY,
    UNARY,
    IF,
    CALL_BUILTIN,
    RECORD_LIT,
    ARRAY_LIT
};

struct ExprIR;
using ExprIRPtr = std::unique_ptr<ExprIR>;

struct ExprIR {
    ExprIRKind kind;
    TypePtr type;

    virtual ~ExprIR() = default;
    virtual ExprIRPtr clone() const = 0;
};

// Const: int, float, bool, string
enum class ConstKind {
    INT,
    FLOAT,
    BOOL,
    STRING
};

struct ConstExprIR : public ExprIR {
    ConstKind const_kind;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string float_text;
    bool bool_val = false;
    std::string string_val;

    ConstExprIR() { kind = ExprIRKind::CONST; }

    static std::unique_ptr<ConstExprIR> make_int(int64_t val, TypePtr ty) {
        auto e = std::make_unique<ConstExprIR>();
        e->const_kind = ConstKind::INT;
        e->int_val = val;
        e->type = std::move(ty);
        return e;
    }

    static std::unique_ptr<ConstExprIR> make_float(double val, std::string text, TypePtr ty) {
        auto e = std::make_unique<ConstExprIR>();
        e->const_kind = ConstKind::FLOAT;
        e->float_val = val;
        e->float_text = std::move(text);
        e->type = std::move(ty);
        return e;
    }

    static std::unique_ptr<ConstExprIR> make_bool(bool val, TypePtr ty) {
        auto e = std::make_unique<ConstExprIR>();
        e->const_kind = ConstKind::BOOL;
        e->bool_val = val;
        e->type = std::move(ty);
        return e;
    }

    static std::unique_ptr<ConstExprIR> make_string(std::string val, TypePtr ty) {
        auto e = std::make_unique<ConstExprIR>();
        e->const_kind = ConstKind::STRING;
        e->string_val = std::move(val);
        e->type = std::move(ty);
        return e;
    }

    ExprIRPtr clone() const override {
        auto copy = std::make_unique<ConstExprIR>();
        copy->type = type;
        copy->const_kind = const_kind;
        copy->int_val = int_val;
        copy->float_val = float_val;
        copy->float_text = float_text;
        copy->bool_val = bool_val;
        copy->string_val = string_val;
        return copy;
    }
};

struct VarParamExprIR : public ExprIR {
    std::string name;

    VarParamExprIR(std::string n, TypePtr ty) : name(std::move(n)) {
        kind = ExprIRKind::VAR_PARAM;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        return std::make_unique<VarParamExprIR>(name, type);
    }
};

struct VarStageExprIR : public ExprIR {
    std::string stage_name;

    VarStageExprIR(std::string n, TypePtr ty) : stage_name(std::move(n)) {
        kind = ExprIRKind::VAR_STAGE;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        return std::make_unique<VarStageExprIR>(stage_name, type);
    }
};

struct FieldExprIR : public ExprIR {
    ExprIRPtr object;
    std::string field_name;

    FieldExprIR(ExprIRPtr obj, std::string f, TypePtr ty)
        : object(std::move(obj)), field_name(std::move(f)) {
        kind = ExprIRKind::FIELD;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        return std::make_unique<FieldExprIR>(object->clone(), field_name, type);
    }
};

struct BinaryExprIR : public ExprIR {
    BinaryOp op;
    ExprIRPtr left;
    ExprIRPtr right;

    BinaryExprIR(BinaryOp o, ExprIRPtr l, ExprIRPtr r, TypePtr ty)
        : op(o), left(std::move(l)), right(std::move(r)) {
        kind = ExprIRKind::BINARY;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        return std::make_unique<BinaryExprIR>(op, left->clone(), right->clone(), type);
    }
};

struct UnaryExprIR : public ExprIR {
    UnaryOp op;
    ExprIRPtr operand;

    UnaryExprIR(UnaryOp o, ExprIRPtr opnd, TypePtr ty)
        : op(o), operand(std::move(opnd)) {
        kind = ExprIRKind::UNARY;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        return std::make_unique<UnaryExprIR>(op, operand->clone(), type);
    }
};

struct IfExprIR : public ExprIR {
    ExprIRPtr cond;
    ExprIRPtr then_branch;
    ExprIRPtr else_branch;

    IfExprIR(ExprIRPtr c, ExprIRPtr t, ExprIRPtr e, TypePtr ty)
        : cond(std::move(c)), then_branch(std::move(t)), else_branch(std::move(e)) {
        kind = ExprIRKind::IF;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        return std::make_unique<IfExprIR>(cond->clone(), then_branch->clone(), else_branch->clone(), type);
    }
};

struct CallBuiltinExprIR : public ExprIR {
    std::string func_name;
    std::vector<ExprIRPtr> args;

    CallBuiltinExprIR(std::string name, std::vector<ExprIRPtr> a, TypePtr ty)
        : func_name(std::move(name)), args(std::move(a)) {
        kind = ExprIRKind::CALL_BUILTIN;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        std::vector<ExprIRPtr> cloned_args;
        for (const auto& a : args) {
            cloned_args.push_back(a->clone());
        }
        return std::make_unique<CallBuiltinExprIR>(func_name, std::move(cloned_args), type);
    }
};

struct RecordFieldInitIR {
    std::string name;
    ExprIRPtr value;
};

struct RecordLitExprIR : public ExprIR {
    std::vector<RecordFieldInitIR> fields;

    RecordLitExprIR(std::vector<RecordFieldInitIR> flds, TypePtr ty)
        : fields(std::move(flds)) {
        kind = ExprIRKind::RECORD_LIT;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        std::vector<RecordFieldInitIR> cloned;
        for (const auto& f : fields) {
            cloned.push_back(RecordFieldInitIR{f.name, f.value->clone()});
        }
        return std::make_unique<RecordLitExprIR>(std::move(cloned), type);
    }
};

struct ArrayLitExprIR : public ExprIR {
    std::vector<ExprIRPtr> elements;

    ArrayLitExprIR(std::vector<ExprIRPtr> elems, TypePtr ty)
        : elements(std::move(elems)) {
        kind = ExprIRKind::ARRAY_LIT;
        type = std::move(ty);
    }

    ExprIRPtr clone() const override {
        std::vector<ExprIRPtr> cloned;
        for (const auto& el : elements) {
            cloned.push_back(el->clone());
        }
        return std::make_unique<ArrayLitExprIR>(std::move(cloned), type);
    }
};

// Lambda expression representation for stage parameters: (params) => body
struct LambdaIR {
    std::vector<std::string> params;
    ExprIRPtr body;

    LambdaIR clone() const {
        return LambdaIR{params, body ? body->clone() : nullptr};
    }
};

// Structural hash computation for ExprIR / LambdaIR
uint64_t hash_expr_ir(const ExprIR* expr);
uint64_t hash_lambda_ir(const LambdaIR& lam);

// Formatter to string
std::string format_expr_ir(const ExprIR* expr);
std::string format_lambda_ir(const LambdaIR& lam);

} // namespace retroc
