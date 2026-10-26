#include "bytecode/bytecode.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace retroc {

BytecodeCompiler::BytecodeCompiler(std::vector<std::string> params)
    : params_(std::move(params)) {}

int32_t BytecodeCompiler::add_constant(Value val) {
    for (size_t i = 0; i < prog_.constants.size(); ++i) {
        if (prog_.constants[i] == val) {
            return static_cast<int32_t>(i);
        }
    }
    prog_.constants.push_back(std::move(val));
    return static_cast<int32_t>(prog_.constants.size() - 1);
}

int32_t BytecodeCompiler::add_string(std::string s) {
    for (size_t i = 0; i < prog_.strings.size(); ++i) {
        if (prog_.strings[i] == s) {
            return static_cast<int32_t>(i);
        }
    }
    prog_.strings.push_back(std::move(s));
    return static_cast<int32_t>(prog_.strings.size() - 1);
}

int32_t BytecodeCompiler::add_stage(std::string name) {
    for (size_t i = 0; i < prog_.stage_names.size(); ++i) {
        if (prog_.stage_names[i] == name) {
            return static_cast<int32_t>(i);
        }
    }
    prog_.stage_names.push_back(std::move(name));
    return static_cast<int32_t>(prog_.stage_names.size() - 1);
}

int32_t BytecodeCompiler::emit(Opcode op, int32_t arg) {
    prog_.code.push_back(Instruction{op, arg});
    return static_cast<int32_t>(prog_.code.size() - 1);
}

void BytecodeCompiler::patch(int32_t instr_index, int32_t arg) {
    prog_.code[instr_index].arg = arg;
}

BytecodeProgram BytecodeCompiler::compile(const ExprIR* expr) {
    prog_ = BytecodeProgram{};
    compile_expr(expr);
    return std::move(prog_);
}

void BytecodeCompiler::compile_expr(const ExprIR* expr) {
    if (!expr) return;

    switch (expr->kind) {
        case ExprIRKind::CONST: {
            const auto* c = static_cast<const ConstExprIR*>(expr);
            Value val;
            switch (c->const_kind) {
                case ConstKind::INT:
                    val = Value::make_int(c->int_val);
                    break;
                case ConstKind::FLOAT:
                    val = Value::make_float(c->float_val);
                    break;
                case ConstKind::BOOL:
                    val = Value::make_bool(c->bool_val);
                    break;
                case ConstKind::STRING:
                    val = Value::make_string(c->string_val);
                    break;
            }
            int32_t idx = add_constant(std::move(val));
            emit(Opcode::PUSH_CONST, idx);
            break;
        }

        case ExprIRKind::VAR_PARAM: {
            const auto* v = static_cast<const VarParamExprIR*>(expr);
            int32_t idx = -1;
            for (size_t i = 0; i < params_.size(); ++i) {
                if (params_[i] == v->name) {
                    idx = static_cast<int32_t>(i);
                    break;
                }
            }
            emit(Opcode::LOAD_PARAM, idx);
            break;
        }

        case ExprIRKind::VAR_STAGE: {
            const auto* v = static_cast<const VarStageExprIR*>(expr);
            int32_t idx = add_stage(v->stage_name);
            emit(Opcode::LOAD_STAGE, idx);
            break;
        }

        case ExprIRKind::FIELD: {
            const auto* f = static_cast<const FieldExprIR*>(expr);
            compile_expr(f->object.get());
            int32_t name_idx = add_string(f->field_name);
            emit(Opcode::GET_FIELD, name_idx);
            break;
        }

        case ExprIRKind::BINARY: {
            const auto* b = static_cast<const BinaryExprIR*>(expr);
            
            // Short-circuiting for AND / OR
            if (b->op == BinaryOp::AND) {
                compile_expr(b->left.get());
                int32_t jump_false = emit(Opcode::JUMP_IF_FALSE, 0);
                compile_expr(b->right.get());
                int32_t jump_end = emit(Opcode::JUMP, 0);
                
                int32_t false_target = static_cast<int32_t>(prog_.code.size());
                patch(jump_false, false_target);
                int32_t c_false = add_constant(Value::make_bool(false));
                emit(Opcode::PUSH_CONST, c_false);
                
                int32_t end_target = static_cast<int32_t>(prog_.code.size());
                patch(jump_end, end_target);
                return;
            }
            if (b->op == BinaryOp::OR) {
                // if left is true, result is true, else evaluate right
                compile_expr(b->left.get());
                // Invert condition: NOT left; then JUMP_IF_FALSE to true_target
                emit(Opcode::NOT_BOOL, 0);
                int32_t jump_if_was_true = emit(Opcode::JUMP_IF_FALSE, 0);
                compile_expr(b->right.get());
                int32_t jump_end = emit(Opcode::JUMP, 0);

                int32_t true_target = static_cast<int32_t>(prog_.code.size());
                patch(jump_if_was_true, true_target);
                int32_t c_true = add_constant(Value::make_bool(true));
                emit(Opcode::PUSH_CONST, c_true);

                int32_t end_target = static_cast<int32_t>(prog_.code.size());
                patch(jump_end, end_target);
                return;
            }

            compile_expr(b->left.get());
            compile_expr(b->right.get());

            TypeKind operand_kind = b->left->type ? b->left->type->kind : TypeKind::INT;

            switch (b->op) {
                case BinaryOp::ADD:
                    if (operand_kind == TypeKind::INT) emit(Opcode::ADD_INT);
                    else if (operand_kind == TypeKind::FLOAT) emit(Opcode::ADD_FLOAT);
                    else if (operand_kind == TypeKind::STRING) emit(Opcode::ADD_STRING);
                    break;
                case BinaryOp::SUB:
                    if (operand_kind == TypeKind::INT) emit(Opcode::SUB_INT);
                    else emit(Opcode::SUB_FLOAT);
                    break;
                case BinaryOp::MUL:
                    if (operand_kind == TypeKind::INT) emit(Opcode::MUL_INT);
                    else emit(Opcode::MUL_FLOAT);
                    break;
                case BinaryOp::DIV:
                    if (operand_kind == TypeKind::INT) emit(Opcode::DIV_INT);
                    else emit(Opcode::DIV_FLOAT);
                    break;
                case BinaryOp::MOD:
                    emit(Opcode::MOD_INT);
                    break;
                case BinaryOp::EQ:
                    emit(Opcode::EQ);
                    break;
                case BinaryOp::NEQ:
                    emit(Opcode::NEQ);
                    break;
                case BinaryOp::LT:
                    if (operand_kind == TypeKind::INT) emit(Opcode::LT_INT);
                    else if (operand_kind == TypeKind::FLOAT) emit(Opcode::LT_FLOAT);
                    else emit(Opcode::LT_STRING);
                    break;
                case BinaryOp::LTE:
                    if (operand_kind == TypeKind::INT) emit(Opcode::LTE_INT);
                    else if (operand_kind == TypeKind::FLOAT) emit(Opcode::LTE_FLOAT);
                    else emit(Opcode::LTE_STRING);
                    break;
                case BinaryOp::GT:
                    if (operand_kind == TypeKind::INT) emit(Opcode::GT_INT);
                    else if (operand_kind == TypeKind::FLOAT) emit(Opcode::GT_FLOAT);
                    else emit(Opcode::GT_STRING);
                    break;
                case BinaryOp::GTE:
                    if (operand_kind == TypeKind::INT) emit(Opcode::GTE_INT);
                    else if (operand_kind == TypeKind::FLOAT) emit(Opcode::GTE_FLOAT);
                    else emit(Opcode::GTE_STRING);
                    break;
                default:
                    break;
            }
            break;
        }

        case ExprIRKind::UNARY: {
            const auto* u = static_cast<const UnaryExprIR*>(expr);
            compile_expr(u->operand.get());
            if (u->op == UnaryOp::NEG) {
                if (u->operand->type && u->operand->type->kind == TypeKind::FLOAT) {
                    emit(Opcode::NEG_FLOAT);
                } else {
                    emit(Opcode::NEG_INT);
                }
            } else if (u->op == UnaryOp::NOT) {
                emit(Opcode::NOT_BOOL);
            }
            break;
        }

        case ExprIRKind::IF: {
            const auto* if_expr = static_cast<const IfExprIR*>(expr);
            compile_expr(if_expr->cond.get());
            int32_t jump_false = emit(Opcode::JUMP_IF_FALSE, 0);
            compile_expr(if_expr->then_branch.get());
            int32_t jump_end = emit(Opcode::JUMP, 0);

            int32_t false_target = static_cast<int32_t>(prog_.code.size());
            patch(jump_false, false_target);
            compile_expr(if_expr->else_branch.get());

            int32_t end_target = static_cast<int32_t>(prog_.code.size());
            patch(jump_end, end_target);
            break;
        }

        case ExprIRKind::CALL_BUILTIN: {
            const auto* c = static_cast<const CallBuiltinExprIR*>(expr);
            for (const auto& a : c->args) {
                compile_expr(a.get());
            }

            if (c->func_name == "to_float") {
                emit(Opcode::BUILTIN_TO_FLOAT);
            } else if (c->func_name == "to_int") {
                emit(Opcode::BUILTIN_TO_INT);
            } else if (c->func_name == "length") {
                emit(Opcode::BUILTIN_LENGTH_ARRAY);
            } else if (c->func_name == "abs") {
                if (c->args[0]->type && c->args[0]->type->kind == TypeKind::FLOAT) {
                    emit(Opcode::BUILTIN_ABS_FLOAT);
                } else {
                    emit(Opcode::BUILTIN_ABS_INT);
                }
            } else if (c->func_name == "min") {
                if (c->args[0]->type && c->args[0]->type->kind == TypeKind::FLOAT) {
                    emit(Opcode::BUILTIN_MIN_FLOAT);
                } else {
                    emit(Opcode::BUILTIN_MIN_INT);
                }
            } else if (c->func_name == "max") {
                if (c->args[0]->type && c->args[0]->type->kind == TypeKind::FLOAT) {
                    emit(Opcode::BUILTIN_MAX_FLOAT);
                } else {
                    emit(Opcode::BUILTIN_MAX_INT);
                }
            } else if (c->func_name == "clamp") {
                if (c->args[0]->type && c->args[0]->type->kind == TypeKind::FLOAT) {
                    emit(Opcode::BUILTIN_CLAMP_FLOAT);
                } else {
                    emit(Opcode::BUILTIN_CLAMP_INT);
                }
            }
            break;
        }

        case ExprIRKind::RECORD_LIT: {
            const auto* r = static_cast<const RecordLitExprIR*>(expr);
            for (const auto& f : r->fields) {
                compile_expr(f.value.get());
            }
            emit(Opcode::MAKE_RECORD, static_cast<int32_t>(r->fields.size()));
            for (const auto& f : r->fields) {
                emit(Opcode::PUSH_CONST, add_string(f.name));
            }
            break;
        }

        case ExprIRKind::ARRAY_LIT: {
            const auto* a = static_cast<const ArrayLitExprIR*>(expr);
            for (const auto& el : a->elements) {
                compile_expr(el.get());
            }
            emit(Opcode::MAKE_ARRAY, static_cast<int32_t>(a->elements.size()));
            break;
        }
    }
}

namespace {
struct FixedVMStack {
    Value buf[64];
    size_t sz = 0;
    std::vector<Value> overflow;

    void push_back(Value v) {
        if (sz < 64) {
            buf[sz++] = std::move(v);
        } else {
            overflow.push_back(std::move(v));
            sz++;
        }
    }

    void pop_back() {
        if (sz > 64) {
            overflow.pop_back();
            --sz;
        } else if (sz > 0) {
            --sz;
        }
    }

    Value& back() {
        if (sz > 64) {
            return overflow.back();
        }
        return buf[sz - 1];
    }

    const Value& back() const {
        if (sz > 64) {
            return overflow.back();
        }
        return buf[sz - 1];
    }

    bool empty() const {
        return sz == 0;
    }

    size_t size() const {
        return sz;
    }
};
}

Value VM::execute(
    const BytecodeProgram& prog,
    const Value* params,
    size_t num_params,
    const std::unordered_map<std::string, Value>& stage_env
) {
    (void)num_params;
    FixedVMStack stack;

    int32_t ip = 0;
    const int32_t n = static_cast<int32_t>(prog.code.size());

    while (ip < n) {
        const auto& instr = prog.code[ip++];
        switch (instr.op) {
            case Opcode::PUSH_CONST:
                stack.push_back(prog.constants[instr.arg]);
                break;

            case Opcode::LOAD_PARAM:
                stack.push_back(params[instr.arg]);
                break;

            case Opcode::LOAD_STAGE: {
                const std::string& name = prog.stage_names[instr.arg];
                auto it = stage_env.find(name);
                if (it != stage_env.end()) {
                    stack.push_back(it->second);
                } else {
                    throw RuntimeError("Stage '" + name + "' not found in runtime environment");
                }
                break;
            }

            case Opcode::GET_FIELD: {
                Value obj = std::move(stack.back());
                stack.pop_back();
                const std::string& field_name = prog.strings[instr.arg];
                bool found = false;
                for (const auto& f : obj.record_val) {
                    if (f.name == field_name) {
                        stack.push_back(f.value ? *f.value : Value{});
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    throw RuntimeError("Record missing field '" + field_name + "'");
                }
                break;
            }

            case Opcode::MAKE_RECORD: {
                int32_t count = instr.arg;
                // Next `count` instructions are PUSH_CONST with string indices
                std::vector<std::string> field_names(count);
                for (int32_t i = 0; i < count; ++i) {
                    int32_t str_idx = prog.code[ip++].arg;
                    field_names[i] = prog.strings[str_idx];
                }
                std::vector<RecordField> fields(count);
                // Values are in reverse order on stack: stack.pop_back() gives last value
                for (int32_t i = count - 1; i >= 0; --i) {
                    fields[i].name = std::move(field_names[i]);
                    fields[i].value = std::make_shared<Value>(std::move(stack.back()));
                    stack.pop_back();
                }
                stack.push_back(Value::make_record(std::move(fields)));
                break;
            }

            case Opcode::MAKE_ARRAY: {
                int32_t count = instr.arg;
                std::vector<Value> elems(count);
                for (int32_t i = count - 1; i >= 0; --i) {
                    elems[i] = std::move(stack.back());
                    stack.pop_back();
                }
                stack.push_back(Value::make_array(std::move(elems)));
                break;
            }

            case Opcode::ADD_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(a + b));
                break;
            }
            case Opcode::ADD_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(a + b));
                break;
            }
            case Opcode::ADD_STRING: {
                std::string b = std::move(stack.back().string_val); stack.pop_back();
                std::string a = std::move(stack.back().string_val); stack.pop_back();
                stack.push_back(Value::make_string(a + b));
                break;
            }

            case Opcode::SUB_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(a - b));
                break;
            }
            case Opcode::SUB_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(a - b));
                break;
            }

            case Opcode::MUL_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(a * b));
                break;
            }
            case Opcode::MUL_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(a * b));
                break;
            }

            case Opcode::DIV_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                if (b == 0) {
                    throw RuntimeError("Integer division by zero");
                }
                stack.push_back(Value::make_int(a / b));
                break;
            }
            case Opcode::DIV_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(a / b));
                break;
            }

            case Opcode::MOD_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                if (b == 0) {
                    throw RuntimeError("Integer modulo by zero");
                }
                stack.push_back(Value::make_int(a % b));
                break;
            }

            case Opcode::NEG_INT: {
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(-a));
                break;
            }
            case Opcode::NEG_FLOAT: {
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(-a));
                break;
            }
            case Opcode::NOT_BOOL: {
                bool a = stack.back().bool_val; stack.pop_back();
                stack.push_back(Value::make_bool(!a));
                break;
            }

            case Opcode::EQ: {
                Value b = std::move(stack.back()); stack.pop_back();
                Value a = std::move(stack.back()); stack.pop_back();
                stack.push_back(Value::make_bool(a == b));
                break;
            }
            case Opcode::NEQ: {
                Value b = std::move(stack.back()); stack.pop_back();
                Value a = std::move(stack.back()); stack.pop_back();
                stack.push_back(Value::make_bool(a != b));
                break;
            }

            case Opcode::LT_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_bool(a < b));
                break;
            }
            case Opcode::LT_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_bool(a < b));
                break;
            }
            case Opcode::LT_STRING: {
                std::string b = std::move(stack.back().string_val); stack.pop_back();
                std::string a = std::move(stack.back().string_val); stack.pop_back();
                stack.push_back(Value::make_bool(a < b));
                break;
            }

            case Opcode::LTE_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_bool(a <= b));
                break;
            }
            case Opcode::LTE_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_bool(a <= b));
                break;
            }
            case Opcode::LTE_STRING: {
                std::string b = std::move(stack.back().string_val); stack.pop_back();
                std::string a = std::move(stack.back().string_val); stack.pop_back();
                stack.push_back(Value::make_bool(a <= b));
                break;
            }

            case Opcode::GT_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_bool(a > b));
                break;
            }
            case Opcode::GT_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_bool(a > b));
                break;
            }
            case Opcode::GT_STRING: {
                std::string b = std::move(stack.back().string_val); stack.pop_back();
                std::string a = std::move(stack.back().string_val); stack.pop_back();
                stack.push_back(Value::make_bool(a > b));
                break;
            }

            case Opcode::GTE_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_bool(a >= b));
                break;
            }
            case Opcode::GTE_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_bool(a >= b));
                break;
            }
            case Opcode::GTE_STRING: {
                std::string b = std::move(stack.back().string_val); stack.pop_back();
                std::string a = std::move(stack.back().string_val); stack.pop_back();
                stack.push_back(Value::make_bool(a >= b));
                break;
            }

            case Opcode::JUMP_IF_FALSE: {
                bool cond = stack.back().bool_val;
                stack.pop_back();
                if (!cond) {
                    ip = instr.arg;
                }
                break;
            }
            case Opcode::JUMP: {
                ip = instr.arg;
                break;
            }

            case Opcode::BUILTIN_TO_FLOAT: {
                int64_t v = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_float(static_cast<double>(v)));
                break;
            }
            case Opcode::BUILTIN_TO_INT: {
                double v = stack.back().float_val; stack.pop_back();
                if (std::isnan(v) || std::isinf(v)) {
                    throw RuntimeError("to_int of NaN or infinity");
                }
                stack.push_back(Value::make_int(static_cast<int64_t>(v)));
                break;
            }

            case Opcode::BUILTIN_ABS_INT: {
                int64_t v = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(std::abs(v)));
                break;
            }
            case Opcode::BUILTIN_ABS_FLOAT: {
                double v = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(std::abs(v)));
                break;
            }

            case Opcode::BUILTIN_MIN_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(std::min(a, b)));
                break;
            }
            case Opcode::BUILTIN_MIN_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(std::min(a, b)));
                break;
            }

            case Opcode::BUILTIN_MAX_INT: {
                int64_t b = stack.back().int_val; stack.pop_back();
                int64_t a = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(std::max(a, b)));
                break;
            }
            case Opcode::BUILTIN_MAX_FLOAT: {
                double b = stack.back().float_val; stack.pop_back();
                double a = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(std::max(a, b)));
                break;
            }

            case Opcode::BUILTIN_CLAMP_INT: {
                int64_t hi = stack.back().int_val; stack.pop_back();
                int64_t lo = stack.back().int_val; stack.pop_back();
                int64_t x = stack.back().int_val; stack.pop_back();
                stack.push_back(Value::make_int(std::min(std::max(x, lo), hi)));
                break;
            }
            case Opcode::BUILTIN_CLAMP_FLOAT: {
                double hi = stack.back().float_val; stack.pop_back();
                double lo = stack.back().float_val; stack.pop_back();
                double x = stack.back().float_val; stack.pop_back();
                stack.push_back(Value::make_float(std::min(std::max(x, lo), hi)));
                break;
            }

            case Opcode::BUILTIN_LENGTH_ARRAY: {
                Value arr = std::move(stack.back()); stack.pop_back();
                int64_t sz = (arr.int_val != 0) ? arr.int_val : static_cast<int64_t>(arr.array_val.size());
                stack.push_back(Value::make_int(sz));
                break;
            }
        }
    }

    if (stack.empty()) {
        throw RuntimeError("VM execution produced no result");
    }
    return stack.back();
}

} // namespace retroc
