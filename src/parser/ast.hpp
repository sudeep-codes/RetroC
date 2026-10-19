#pragma once

#include "common/location.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>
#include <cstdint>

namespace retroc {

// Forward declarations
struct Type;
struct Expr;
struct Item;

using TypePtr = std::shared_ptr<Type>;
using ExprPtr = std::unique_ptr<Expr>;
using ItemPtr = std::unique_ptr<Item>;

// Types
enum class TypeKind {
    INT,
    FLOAT,
    BOOL,
    STRING,
    ARRAY,
    RECORD
};

struct FieldType {
    std::string name;
    TypePtr type;
    SourceLocation loc;
};

struct Type {
    TypeKind kind;
    SourceLocation loc;
    TypePtr element_type;            // For ARRAY: [T]
    std::vector<FieldType> fields;   // For RECORD: { f1: T1, ... }

    static TypePtr make_int(SourceLocation loc = {}) {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::INT;
        t->loc = loc;
        return t;
    }
    static TypePtr make_float(SourceLocation loc = {}) {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::FLOAT;
        t->loc = loc;
        return t;
    }
    static TypePtr make_bool(SourceLocation loc = {}) {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::BOOL;
        t->loc = loc;
        return t;
    }
    static TypePtr make_string(SourceLocation loc = {}) {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::STRING;
        t->loc = loc;
        return t;
    }
    static TypePtr make_array(TypePtr elem, SourceLocation loc = {}) {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::ARRAY;
        t->loc = loc;
        t->element_type = std::move(elem);
        return t;
    }
    static TypePtr make_record(std::vector<FieldType> fields, SourceLocation loc = {}) {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::RECORD;
        t->loc = loc;
        t->fields = std::move(fields);
        return t;
    }

    std::string to_string() const;
    std::string to_ir_string() const;
};

// Expressions
enum class ExprKind {
    INT_LIT,
    FLOAT_LIT,
    BOOL_LIT,
    STRING_LIT,
    IDENT,
    ARRAY_LIT,
    RECORD_LIT,
    UNARY,
    BINARY,
    FIELD_ACCESS,
    CALL,
    IF,
    LAMBDA
};

enum class UnaryOp {
    NEG, // -
    NOT  // not
};

enum class BinaryOp {
    ADD,      // +
    SUB,      // -
    MUL,      // *
    DIV,      // /
    MOD,      // %
    EQ,       // ==
    NEQ,      // !=
    LT,       // <
    LTE,      // <=
    GT,       // >
    GTE,      // >=
    AND,      // and
    OR        // or
};

const char* binary_op_to_string(BinaryOp op);
const char* unary_op_to_string(UnaryOp op);

struct Expr {
    ExprKind kind;
    SourceLocation loc;

    virtual ~Expr() = default;
};

struct IntLitExpr : public Expr {
    int64_t value;
    IntLitExpr(int64_t val, SourceLocation l) : value(val) {
        kind = ExprKind::INT_LIT;
        loc = l;
    }
};

struct FloatLitExpr : public Expr {
    double value;
    std::string text; // keep original text representation for lossless precision/formatting
    FloatLitExpr(double val, std::string txt, SourceLocation l)
        : value(val), text(std::move(txt)) {
        kind = ExprKind::FLOAT_LIT;
        loc = l;
    }
};

struct BoolLitExpr : public Expr {
    bool value;
    BoolLitExpr(bool val, SourceLocation l) : value(val) {
        kind = ExprKind::BOOL_LIT;
        loc = l;
    }
};

struct StringLitExpr : public Expr {
    std::string value;
    StringLitExpr(std::string val, SourceLocation l) : value(std::move(val)) {
        kind = ExprKind::STRING_LIT;
        loc = l;
    }
};

struct IdentExpr : public Expr {
    std::string name;
    IdentExpr(std::string n, SourceLocation l) : name(std::move(n)) {
        kind = ExprKind::IDENT;
        loc = l;
    }
};

struct ArrayLitExpr : public Expr {
    std::vector<ExprPtr> elements;
    ArrayLitExpr(std::vector<ExprPtr> elems, SourceLocation l) : elements(std::move(elems)) {
        kind = ExprKind::ARRAY_LIT;
        loc = l;
    }
};

struct RecordFieldInit {
    std::string name;
    ExprPtr value;
    SourceLocation loc;
};

struct RecordLitExpr : public Expr {
    std::vector<RecordFieldInit> fields;
    RecordLitExpr(std::vector<RecordFieldInit> flds, SourceLocation l) : fields(std::move(flds)) {
        kind = ExprKind::RECORD_LIT;
        loc = l;
    }
};

struct UnaryExpr : public Expr {
    UnaryOp op;
    ExprPtr operand;
    UnaryExpr(UnaryOp o, ExprPtr opnd, SourceLocation l)
        : op(o), operand(std::move(opnd)) {
        kind = ExprKind::UNARY;
        loc = l;
    }
};

struct BinaryExpr : public Expr {
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;
    BinaryExpr(BinaryOp o, ExprPtr l_expr, ExprPtr r_expr, SourceLocation l)
        : op(o), left(std::move(l_expr)), right(std::move(r_expr)) {
        kind = ExprKind::BINARY;
        loc = l;
    }
};

struct FieldAccessExpr : public Expr {
    ExprPtr object;
    std::string field_name;
    FieldAccessExpr(ExprPtr obj, std::string field, SourceLocation l)
        : object(std::move(obj)), field_name(std::move(field)) {
        kind = ExprKind::FIELD_ACCESS;
        loc = l;
    }
};

struct CallExpr : public Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    CallExpr(ExprPtr callee_expr, std::vector<ExprPtr> call_args, SourceLocation l)
        : callee(std::move(callee_expr)), args(std::move(call_args)) {
        kind = ExprKind::CALL;
        loc = l;
    }
};

struct IfExpr : public Expr {
    ExprPtr cond;
    ExprPtr then_branch;
    ExprPtr else_branch;
    IfExpr(ExprPtr c, ExprPtr t, ExprPtr e, SourceLocation l)
        : cond(std::move(c)), then_branch(std::move(t)), else_branch(std::move(e)) {
        kind = ExprKind::IF;
        loc = l;
    }
};

struct LambdaExpr : public Expr {
    std::vector<std::string> params;
    ExprPtr body;
    LambdaExpr(std::vector<std::string> p, ExprPtr b, SourceLocation l)
        : params(std::move(p)), body(std::move(b)) {
        kind = ExprKind::LAMBDA;
        loc = l;
    }
};

// Items (Top-level declarations)
enum class ItemKind {
    INPUT_DECL,
    FN_DECL,
    LET_DECL,
    OUTPUT_DECL
};

struct Item {
    ItemKind kind;
    SourceLocation loc;
    virtual ~Item() = default;
};

struct InputDecl : public Item {
    std::string name;
    TypePtr type;
    ExprPtr init_expr;         // non-null if inline init
    std::string from_file;     // non-empty if from "file.csv"

    InputDecl(std::string n, TypePtr t, ExprPtr init, std::string file, SourceLocation l)
        : name(std::move(n)), type(std::move(t)), init_expr(std::move(init)), from_file(std::move(file)) {
        kind = ItemKind::INPUT_DECL;
        loc = l;
    }
};

struct FnParam {
    std::string name;
    TypePtr type;
    SourceLocation loc;
};

struct FnDecl : public Item {
    std::string name;
    std::vector<FnParam> params;
    TypePtr ret_type;
    ExprPtr body;

    FnDecl(std::string n, std::vector<FnParam> p, TypePtr ret, ExprPtr b, SourceLocation l)
        : name(std::move(n)), params(std::move(p)), ret_type(std::move(ret)), body(std::move(b)) {
        kind = ItemKind::FN_DECL;
        loc = l;
    }
};

struct LetDecl : public Item {
    std::string name;
    TypePtr explicit_type; // optional
    ExprPtr init_expr;

    LetDecl(std::string n, TypePtr t, ExprPtr init, SourceLocation l)
        : name(std::move(n)), explicit_type(std::move(t)), init_expr(std::move(init)) {
        kind = ItemKind::LET_DECL;
        loc = l;
    }
};

struct OutputDecl : public Item {
    std::string name;

    OutputDecl(std::string n, SourceLocation l)
        : name(std::move(n)) {
        kind = ItemKind::OUTPUT_DECL;
        loc = l;
    }
};

struct Program {
    std::vector<ItemPtr> items;
};

// AST Dump / print utilities
void dump_ast(const Program& prog, std::ostream& out);

} // namespace retroc
