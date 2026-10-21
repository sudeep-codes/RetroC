#pragma once

#include "common/value.hpp"
#include "ir/expr_ir.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

namespace retroc {

enum class Opcode {
    PUSH_CONST,     // arg: value index in constant pool
    LOAD_PARAM,     // arg: param index (or slot)
    LOAD_STAGE,     // arg: stage index in stage pool
    GET_FIELD,      // arg: field name index in string pool
    MAKE_RECORD,    // arg: field count; expects values on stack, field names in string pool
    MAKE_ARRAY,     // arg: element count
    
    // Arithmetic & Bitwise / Logic
    ADD_INT,
    ADD_FLOAT,
    ADD_STRING,
    SUB_INT,
    SUB_FLOAT,
    MUL_INT,
    MUL_FLOAT,
    DIV_INT,        // error E501 on zero
    DIV_FLOAT,
    MOD_INT,        // error E501 on zero
    NEG_INT,
    NEG_FLOAT,
    NOT_BOOL,

    // Comparisons
    EQ,
    NEQ,
    LT_INT,
    LT_FLOAT,
    LT_STRING,
    LTE_INT,
    LTE_FLOAT,
    LTE_STRING,
    GT_INT,
    GT_FLOAT,
    GT_STRING,
    GTE_INT,
    GTE_FLOAT,
    GTE_STRING,

    // Control flow
    JUMP_IF_FALSE,  // arg: target instruction index
    JUMP,           // arg: target instruction index

    // Builtins
    BUILTIN_TO_FLOAT,
    BUILTIN_TO_INT, // error E501 if NaN/inf
    BUILTIN_ABS_INT,
    BUILTIN_ABS_FLOAT,
    BUILTIN_MIN_INT,
    BUILTIN_MIN_FLOAT,
    BUILTIN_MAX_INT,
    BUILTIN_MAX_FLOAT,
    BUILTIN_CLAMP_INT,
    BUILTIN_CLAMP_FLOAT,
    BUILTIN_LENGTH_ARRAY
};

struct Instruction {
    Opcode op;
    int32_t arg = 0;
};

struct BytecodeProgram {
    std::vector<Instruction> code;
    std::vector<Value> constants;
    std::vector<std::string> strings;
    std::vector<std::string> stage_names;
};

// Compiler: compiles an ExprIR tree into BytecodeProgram
class BytecodeCompiler {
public:
    BytecodeCompiler() = default;
    explicit BytecodeCompiler(std::vector<std::string> params);

    BytecodeProgram compile(const ExprIR* expr);

private:
    void compile_expr(const ExprIR* expr);
    int32_t add_constant(Value val);
    int32_t add_string(std::string s);
    int32_t add_stage(std::string name);
    int32_t emit(Opcode op, int32_t arg = 0);
    void patch(int32_t instr_index, int32_t arg);

    std::vector<std::string> params_;
    BytecodeProgram prog_;
};

// Runtime error exception for VM
struct RuntimeError {
    std::string message;
    explicit RuntimeError(std::string msg) : message(std::move(msg)) {}
};

// VM: executes bytecode against parameter values and stage values
class VM {
public:
    // Execute bytecode program.
    // Throws RuntimeError (code E501) on zero division, to_int nan/inf, etc.
    static Value execute(
        const BytecodeProgram& prog,
        const std::vector<Value>& params = {},
        const std::unordered_map<std::string, Value>& stage_env = {}
    );
};

} // namespace retroc
