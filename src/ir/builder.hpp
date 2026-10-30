#pragma once

#include "ir/graph.hpp"
#include "sema/sema.hpp"
#include <unordered_map>
#include <string>

namespace retroc {

class IRBuilder {
public:
    explicit IRBuilder(const TypedProgram& program);

    Graph build();

private:
    const TypedProgram& prog_;
    Graph graph_;
    uint32_t next_node_idx_ = 0;
    int hidden_stage_counter_ = 0;

    // Track stage name to NodeId
    std::unordered_map<std::string, NodeId> stage_to_node_;

    // User functions for inlining: name -> FnDecl*
    std::unordered_map<std::string, const FnDecl*> fn_decls_;

    NodeId allocate_node_id();

    // Lower AST Expr to ExprIR with user function inlining
    // subst_params: map local parameter names to ExprIR trees (for inlining and lambda body)
    ExprIRPtr lower_expr(const Expr* expr,
                         const std::unordered_map<std::string, const ExprIR*>& subst_params);

    // Lambda lowering
    LambdaIR lower_lambda(const LambdaExpr* lam,
                          const std::unordered_map<std::string, const ExprIR*>& subst_params);

    // Recursively decompose nested collection builtins (filter, map, reduce, join)
    // Returns stage name (or freshly generated hidden stage name s$1)
    std::string lower_stage_expression(const std::string& target_stage_name,
                                       const Expr* expr,
                                       TypePtr explicit_type);

    // Collect all referenced stage names in an ExprIR tree
    void collect_stage_dependencies(const ExprIR* expr, std::vector<std::string>& stages_out) const;
    void append_reference_inputs(Node& node) const;
};

} // namespace retroc
