#include "ir/builder.hpp"
#include "ir/dump.hpp"
#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace retroc {

namespace {

bool is_collection_builtin(const std::string& name) {
    return name == "filter" || name == "map" || name == "reduce" || name == "tree_reduce" || name == "join";
}

const CallExpr* as_collection_builtin_call(const Expr* expr) {
    if (!expr || expr->kind != ExprKind::CALL) return nullptr;
    const auto* call = static_cast<const CallExpr*>(expr);
    if (!call->callee || call->callee->kind != ExprKind::IDENT) return nullptr;
    const auto* id = static_cast<const IdentExpr*>(call->callee.get());
    if (is_collection_builtin(id->name)) return call;
    return nullptr;
}

} // namespace

IRBuilder::IRBuilder(const TypedProgram& program)
    : prog_(program) {
    for (const auto& item : prog_.items) {
        if (item->kind == ItemKind::FN_DECL) {
            const auto* f = static_cast<const FnDecl*>(item.get());
            fn_decls_[f->name] = f;
        }
    }
}

NodeId IRBuilder::allocate_node_id() {
    // Invariant I2: NodeIds are unique, never reused (n0, n1, n2, ...)
    NodeId id = "n" + std::to_string(next_node_idx_++);
    return id;
}

void IRBuilder::collect_stage_dependencies(const ExprIR* expr, std::vector<std::string>& stages_out) const {
    if (!expr) return;
    switch (expr->kind) {
        case ExprIRKind::CONST:
        case ExprIRKind::VAR_PARAM:
            break;
        case ExprIRKind::VAR_STAGE: {
            const auto* e = static_cast<const VarStageExprIR*>(expr);
            if (std::find(stages_out.begin(), stages_out.end(), e->stage_name) == stages_out.end()) {
                stages_out.push_back(e->stage_name);
            }
            break;
        }
        case ExprIRKind::FIELD: {
            const auto* e = static_cast<const FieldExprIR*>(expr);
            collect_stage_dependencies(e->object.get(), stages_out);
            break;
        }
        case ExprIRKind::BINARY: {
            const auto* e = static_cast<const BinaryExprIR*>(expr);
            collect_stage_dependencies(e->left.get(), stages_out);
            collect_stage_dependencies(e->right.get(), stages_out);
            break;
        }
        case ExprIRKind::UNARY: {
            const auto* e = static_cast<const UnaryExprIR*>(expr);
            collect_stage_dependencies(e->operand.get(), stages_out);
            break;
        }
        case ExprIRKind::IF: {
            const auto* e = static_cast<const IfExprIR*>(expr);
            collect_stage_dependencies(e->cond.get(), stages_out);
            collect_stage_dependencies(e->then_branch.get(), stages_out);
            collect_stage_dependencies(e->else_branch.get(), stages_out);
            break;
        }
        case ExprIRKind::CALL_BUILTIN: {
            const auto* e = static_cast<const CallBuiltinExprIR*>(expr);
            for (const auto& arg : e->args) {
                collect_stage_dependencies(arg.get(), stages_out);
            }
            break;
        }
        case ExprIRKind::RECORD_LIT: {
            const auto* e = static_cast<const RecordLitExprIR*>(expr);
            for (const auto& f : e->fields) {
                collect_stage_dependencies(f.value.get(), stages_out);
            }
            break;
        }
        case ExprIRKind::ARRAY_LIT: {
            const auto* e = static_cast<const ArrayLitExprIR*>(expr);
            for (const auto& el : e->elements) {
                collect_stage_dependencies(el.get(), stages_out);
            }
            break;
        }
    }
}

ExprIRPtr IRBuilder::lower_expr(const Expr* expr,
                                const std::unordered_map<std::string, const ExprIR*>& subst_params) {
    if (!expr) return nullptr;

    auto it_type = prog_.expr_types.find(expr);
    TypePtr ty = (it_type != prog_.expr_types.end()) ? it_type->second : nullptr;

    switch (expr->kind) {
        case ExprKind::INT_LIT: {
            const auto* e = static_cast<const IntLitExpr*>(expr);
            return ConstExprIR::make_int(e->value, ty);
        }
        case ExprKind::FLOAT_LIT: {
            const auto* e = static_cast<const FloatLitExpr*>(expr);
            return ConstExprIR::make_float(e->value, e->text, ty);
        }
        case ExprKind::BOOL_LIT: {
            const auto* e = static_cast<const BoolLitExpr*>(expr);
            return ConstExprIR::make_bool(e->value, ty);
        }
        case ExprKind::STRING_LIT: {
            const auto* e = static_cast<const StringLitExpr*>(expr);
            return ConstExprIR::make_string(e->value, ty);
        }
        case ExprKind::IDENT: {
            const auto* e = static_cast<const IdentExpr*>(expr);
            auto subst_it = subst_params.find(e->name);
            if (subst_it != subst_params.end()) {
                return subst_it->second->clone();
            }
            // If it is in stage_to_node_, it's a stage variable
            if (stage_to_node_.find(e->name) != stage_to_node_.end()) {
                return std::make_unique<VarStageExprIR>(e->name, ty);
            }
            // Otherwise, it's a parameter variable in a lambda
            return std::make_unique<VarParamExprIR>(e->name, ty);
        }
        case ExprKind::ARRAY_LIT: {
            const auto* e = static_cast<const ArrayLitExpr*>(expr);
            std::vector<ExprIRPtr> elems;
            for (const auto& el : e->elements) {
                elems.push_back(lower_expr(el.get(), subst_params));
            }
            return std::make_unique<ArrayLitExprIR>(std::move(elems), ty);
        }
        case ExprKind::RECORD_LIT: {
            const auto* e = static_cast<const RecordLitExpr*>(expr);
            std::vector<RecordFieldInitIR> fields;
            for (const auto& f : e->fields) {
                fields.push_back(RecordFieldInitIR{f.name, lower_expr(f.value.get(), subst_params)});
            }
            return std::make_unique<RecordLitExprIR>(std::move(fields), ty);
        }
        case ExprKind::UNARY: {
            const auto* e = static_cast<const UnaryExpr*>(expr);
            return std::make_unique<UnaryExprIR>(e->op, lower_expr(e->operand.get(), subst_params), ty);
        }
        case ExprKind::BINARY: {
            const auto* e = static_cast<const BinaryExpr*>(expr);
            return std::make_unique<BinaryExprIR>(e->op,
                                                  lower_expr(e->left.get(), subst_params),
                                                  lower_expr(e->right.get(), subst_params),
                                                  ty);
        }
        case ExprKind::FIELD_ACCESS: {
            const auto* e = static_cast<const FieldAccessExpr*>(expr);
            return std::make_unique<FieldExprIR>(lower_expr(e->object.get(), subst_params),
                                                 e->field_name,
                                                 ty);
        }
        case ExprKind::IF: {
            const auto* e = static_cast<const IfExpr*>(expr);
            return std::make_unique<IfExprIR>(lower_expr(e->cond.get(), subst_params),
                                              lower_expr(e->then_branch.get(), subst_params),
                                              lower_expr(e->else_branch.get(), subst_params),
                                              ty);
        }
        case ExprKind::CALL: {
            const auto* e = static_cast<const CallExpr*>(expr);
            assert(e->callee && e->callee->kind == ExprKind::IDENT);
            const auto* id = static_cast<const IdentExpr*>(e->callee.get());
            std::string name = id->name;

            // Inlining user function: fn
            auto fn_it = fn_decls_.find(name);
            if (fn_it != fn_decls_.end()) {
                const auto* fn_decl = fn_it->second;
                // Substitute arguments for parameters
                std::vector<ExprIRPtr> evaluated_args;
                for (size_t i = 0; i < fn_decl->params.size(); ++i) {
                    evaluated_args.push_back(lower_expr(e->args[i].get(), subst_params));
                }
                std::unordered_map<std::string, const ExprIR*> inlined_params;
                for (size_t i = 0; i < fn_decl->params.size(); ++i) {
                    inlined_params[fn_decl->params[i].name] = evaluated_args[i].get();
                }
                return lower_expr(fn_decl->body.get(), inlined_params);
            }

            // Built-in function call (e.g. length, to_float, abs, min, etc.)
            std::vector<ExprIRPtr> args_ir;
            for (const auto& a : e->args) {
                args_ir.push_back(lower_expr(a.get(), subst_params));
            }
            return std::make_unique<CallBuiltinExprIR>(name, std::move(args_ir), ty);
        }
        case ExprKind::LAMBDA: {
            // Lambdas handled directly in lower_lambda
            assert(false && "Unexpected bare lambda in lower_expr");
            return nullptr;
        }
    }
    return nullptr;
}

LambdaIR IRBuilder::lower_lambda(const LambdaExpr* lam,
                                 const std::unordered_map<std::string, const ExprIR*>& subst_params) {
    LambdaIR res;
    res.params = lam->params;
    // Parameters in lam->params shadow any subst_params with same name
    std::unordered_map<std::string, const ExprIR*> inner_subst = subst_params;
    for (const auto& p : lam->params) {
        inner_subst.erase(p);
    }
    res.body = lower_expr(lam->body.get(), inner_subst);
    return res;
}

// Stages that fn/init read by name become inputs after the iterated
// collections (IR_SPEC section 4), so the edge orders them first (I1) and a
// change to them reaches this node. The iterated collection may appear here
// too: reading it by name (e.g. length(xs)) depends on every record.
void IRBuilder::append_reference_inputs(Node& node) const {
    std::vector<std::string> names;
    if (node.fn) collect_stage_dependencies(node.fn->body.get(), names);
    if (node.join_fn_b) collect_stage_dependencies(node.join_fn_b->body.get(), names);
    collect_stage_dependencies(node.init.get(), names);
    for (const auto& name : names) {
        auto it = stage_to_node_.find(name);
        if (it != stage_to_node_.end()) {
            node.inputs.push_back(it->second);
        }
    }
}

std::string IRBuilder::lower_stage_expression(const std::string& target_stage_name,
                                              const Expr* expr,
                                              TypePtr explicit_type) {
    const CallExpr* call = as_collection_builtin_call(expr);

    if (!call) {
        // Not a collection builtin call -> SCALAR stage
        NodeId nid = allocate_node_id();
        Node node;
        node.id = nid;
        node.name = target_stage_name;
        node.kind = NodeKind::SCALAR;
        node.type = explicit_type;
        if (!node.type) {
            auto it = prog_.expr_types.find(expr);
            if (it != prog_.expr_types.end()) node.type = it->second;
        }

        std::unordered_map<std::string, const ExprIR*> empty_subst;
        node.expr = lower_expr(expr, empty_subst);
        node.fnHash = hash_expr_ir(node.expr.get());

        // Find stage dependencies
        std::vector<std::string> dep_stages;
        collect_stage_dependencies(node.expr.get(), dep_stages);
        for (const auto& sname : dep_stages) {
            auto it = stage_to_node_.find(sname);
            if (it != stage_to_node_.end()) {
                node.inputs.push_back(it->second);
            }
        }

        graph_.nodes[nid] = std::move(node);
        graph_.order.push_back(nid);
        stage_to_node_[target_stage_name] = nid;
        return target_stage_name;
    }

    // It is a collection builtin: filter, map, reduce, or join
    const auto* id = static_cast<const IdentExpr*>(call->callee.get());
    std::string bname = id->name;

    if (bname == "filter") {
        // filter(c, f)
        std::string input_stage_name;
        if (as_collection_builtin_call(call->args[0].get())) {
            // Nested collection builtin -> split into hidden stage s$1
            hidden_stage_counter_++;
            std::string hidden_name = "s$" + std::to_string(hidden_stage_counter_);
            input_stage_name = lower_stage_expression(hidden_name, call->args[0].get(), nullptr);
        } else {
            assert(call->args[0]->kind == ExprKind::IDENT);
            input_stage_name = static_cast<const IdentExpr*>(call->args[0].get())->name;
        }

        NodeId nid = allocate_node_id();
        Node node;
        node.id = nid;
        node.name = target_stage_name;
        node.kind = NodeKind::FILTER;
        node.inputs = {stage_to_node_.at(input_stage_name)};

        auto* lam = static_cast<const LambdaExpr*>(call->args[1].get());
        std::unordered_map<std::string, const ExprIR*> empty_subst;
        node.fn = lower_lambda(lam, empty_subst);
        node.fnHash = hash_lambda_ir(*node.fn);

        auto it = prog_.expr_types.find(expr);
        node.type = (it != prog_.expr_types.end()) ? it->second : nullptr;
        append_reference_inputs(node);

        graph_.nodes[nid] = std::move(node);
        graph_.order.push_back(nid);
        stage_to_node_[target_stage_name] = nid;
        return target_stage_name;
    }

    if (bname == "map") {
        // map(c, f)
        std::string input_stage_name;
        if (as_collection_builtin_call(call->args[0].get())) {
            hidden_stage_counter_++;
            std::string hidden_name = "s$" + std::to_string(hidden_stage_counter_);
            input_stage_name = lower_stage_expression(hidden_name, call->args[0].get(), nullptr);
        } else {
            assert(call->args[0]->kind == ExprKind::IDENT);
            input_stage_name = static_cast<const IdentExpr*>(call->args[0].get())->name;
        }

        NodeId nid = allocate_node_id();
        Node node;
        node.id = nid;
        node.name = target_stage_name;
        node.kind = NodeKind::MAP;
        node.inputs = {stage_to_node_.at(input_stage_name)};

        auto* lam = static_cast<const LambdaExpr*>(call->args[1].get());
        std::unordered_map<std::string, const ExprIR*> empty_subst;
        node.fn = lower_lambda(lam, empty_subst);
        node.fnHash = hash_lambda_ir(*node.fn);

        auto it = prog_.expr_types.find(expr);
        node.type = (it != prog_.expr_types.end()) ? it->second : nullptr;
        append_reference_inputs(node);

        graph_.nodes[nid] = std::move(node);
        graph_.order.push_back(nid);
        stage_to_node_[target_stage_name] = nid;
        return target_stage_name;
    }

    if (bname == "reduce") {
        // reduce(c, init, f)
        std::string input_stage_name;
        if (as_collection_builtin_call(call->args[0].get())) {
            hidden_stage_counter_++;
            std::string hidden_name = "s$" + std::to_string(hidden_stage_counter_);
            input_stage_name = lower_stage_expression(hidden_name, call->args[0].get(), nullptr);
        } else {
            assert(call->args[0]->kind == ExprKind::IDENT);
            input_stage_name = static_cast<const IdentExpr*>(call->args[0].get())->name;
        }

        NodeId nid = allocate_node_id();
        Node node;
        node.id = nid;
        node.name = target_stage_name;
        node.kind = NodeKind::REDUCE;
        node.inputs = {stage_to_node_.at(input_stage_name)};

        std::unordered_map<std::string, const ExprIR*> empty_subst;
        node.init = lower_expr(call->args[1].get(), empty_subst);

        auto* lam = static_cast<const LambdaExpr*>(call->args[2].get());
        node.fn = lower_lambda(lam, empty_subst);
        node.fnHash = hash_lambda_ir(*node.fn) ^ (hash_expr_ir(node.init.get()) << 1);

        auto it = prog_.expr_types.find(expr);
        node.type = (it != prog_.expr_types.end()) ? it->second : nullptr;
        append_reference_inputs(node);

        graph_.nodes[nid] = std::move(node);
        graph_.order.push_back(nid);
        stage_to_node_[target_stage_name] = nid;
        return target_stage_name;
    }

    if (bname == "tree_reduce") {
        // tree_reduce(c, init, f)
        std::string input_stage_name;
        if (as_collection_builtin_call(call->args[0].get())) {
            hidden_stage_counter_++;
            std::string hidden_name = "s$" + std::to_string(hidden_stage_counter_);
            input_stage_name = lower_stage_expression(hidden_name, call->args[0].get(), nullptr);
        } else {
            assert(call->args[0]->kind == ExprKind::IDENT);
            input_stage_name = static_cast<const IdentExpr*>(call->args[0].get())->name;
        }

        NodeId nid = allocate_node_id();
        Node node;
        node.id = nid;
        node.name = target_stage_name;
        node.kind = NodeKind::TREE_REDUCE;
        node.inputs = {stage_to_node_.at(input_stage_name)};

        std::unordered_map<std::string, const ExprIR*> empty_subst;
        node.init = lower_expr(call->args[1].get(), empty_subst);

        auto* lam = static_cast<const LambdaExpr*>(call->args[2].get());
        node.fn = lower_lambda(lam, empty_subst);
        node.fnHash = hash_lambda_ir(*node.fn) ^ (hash_expr_ir(node.init.get()) << 1);

        auto it = prog_.expr_types.find(expr);
        node.type = (it != prog_.expr_types.end()) ? it->second : nullptr;
        append_reference_inputs(node);

        graph_.nodes[nid] = std::move(node);
        graph_.order.push_back(nid);
        stage_to_node_[target_stage_name] = nid;
        return target_stage_name;
    }

    if (bname == "join") {
        // join(a, b, ka, kb)
        std::string input_a_name;
        if (as_collection_builtin_call(call->args[0].get())) {
            hidden_stage_counter_++;
            std::string hidden_name = "s$" + std::to_string(hidden_stage_counter_);
            input_a_name = lower_stage_expression(hidden_name, call->args[0].get(), nullptr);
        } else {
            assert(call->args[0]->kind == ExprKind::IDENT);
            input_a_name = static_cast<const IdentExpr*>(call->args[0].get())->name;
        }

        std::string input_b_name;
        if (as_collection_builtin_call(call->args[1].get())) {
            hidden_stage_counter_++;
            std::string hidden_name = "s$" + std::to_string(hidden_stage_counter_);
            input_b_name = lower_stage_expression(hidden_name, call->args[1].get(), nullptr);
        } else {
            assert(call->args[1]->kind == ExprKind::IDENT);
            input_b_name = static_cast<const IdentExpr*>(call->args[1].get())->name;
        }

        NodeId nid = allocate_node_id();
        Node node;
        node.id = nid;
        node.name = target_stage_name;
        node.kind = NodeKind::JOIN;
        node.inputs = {stage_to_node_.at(input_a_name), stage_to_node_.at(input_b_name)};

        std::unordered_map<std::string, const ExprIR*> empty_subst;
        auto* lam_a = static_cast<const LambdaExpr*>(call->args[2].get());
        auto* lam_b = static_cast<const LambdaExpr*>(call->args[3].get());

        node.fn = lower_lambda(lam_a, empty_subst);
        node.join_fn_b = lower_lambda(lam_b, empty_subst);
        node.fnHash = hash_lambda_ir(*node.fn) ^ (hash_lambda_ir(*node.join_fn_b) << 1);

        auto it = prog_.expr_types.find(expr);
        node.type = (it != prog_.expr_types.end()) ? it->second : nullptr;
        append_reference_inputs(node);

        graph_.nodes[nid] = std::move(node);
        graph_.order.push_back(nid);
        stage_to_node_[target_stage_name] = nid;
        return target_stage_name;
    }

    assert(false && "Unknown collection builtin");
    return "";
}

Graph IRBuilder::build() {
    for (const auto& item : prog_.items) {
        if (!item) continue;
        switch (item->kind) {
            case ItemKind::INPUT_DECL: {
                const auto* d = static_cast<const InputDecl*>(item.get());
                NodeId nid = allocate_node_id();
                Node node;
                node.id = nid;
                node.name = d->name;
                node.kind = NodeKind::INPUT;
                node.type = d->type;
                if (!d->from_file.empty()) {
                    node.from_file = d->from_file;
                } else if (d->init_expr) {
                    std::unordered_map<std::string, const ExprIR*> empty_subst;
                    node.expr = lower_expr(d->init_expr.get(), empty_subst);
                    node.fnHash = hash_expr_ir(node.expr.get());
                }
                graph_.nodes[nid] = std::move(node);
                graph_.order.push_back(nid);
                stage_to_node_[d->name] = nid;
                break;
            }
            case ItemKind::FN_DECL:
                // User functions are inlined into call sites; no IR node produced
                break;
            case ItemKind::LET_DECL: {
                const auto* d = static_cast<const LetDecl*>(item.get());
                hidden_stage_counter_ = 0; // reset per let decl
                lower_stage_expression(d->name, d->init_expr.get(), d->explicit_type);
                break;
            }
            case ItemKind::OUTPUT_DECL: {
                const auto* d = static_cast<const OutputDecl*>(item.get());
                NodeId nid = allocate_node_id();
                Node node;
                node.id = nid;
                node.name = d->name;
                node.kind = NodeKind::OUTPUT;
                assert(stage_to_node_.find(d->name) != stage_to_node_.end());
                NodeId input_node_id = stage_to_node_.at(d->name);
                node.inputs = {input_node_id};
                node.type = graph_.nodes.at(input_node_id).type;

                graph_.nodes[nid] = std::move(node);
                graph_.order.push_back(nid);
                graph_.outputs.push_back(nid);
                break;
            }
        }
    }

    // Invariant assertions: I1 (DAG and topological order) and I2 (unique NodeIds)
    graph_.assert_invariants();

    return std::move(graph_);
}

} // namespace retroc
