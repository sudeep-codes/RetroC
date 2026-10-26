#pragma once

#include "ir/graph.hpp"
#include "runtime/executor.hpp"
#include "edit/edit.hpp"
#include <unordered_set>
#include <string>
#include <vector>

namespace retroc {

// Rebuild plan entry per stage per EDIT_SEMANTICS.md section 7
enum class StagePlanStatus {
    NEW,
    FULL,
    PARTIAL,
    REUSED,
    CLEAN
};

struct StagePlan {
    std::string stage_name;
    StagePlanStatus status;
    size_t ops_evaluated = 0;
    size_t ops_total = 0;
    size_t ops_reused = 0;
    std::string note;
};

struct RebuildPlan {
    EditId edit_id = 0;
    std::string edit_description;
    std::vector<StagePlan> stages;
    size_t recomputed_ops = 0;
    size_t reused_ops = 0;
    size_t full_rerun_ops = 0;
    double reuse_percent = 0.0;
    std::vector<std::string> updated_outputs;

    std::string to_string() const;
};

class Propagator {
public:
    explicit Propagator(Graph& graph, GraphExecutor& executor, bool stage_invalidation = false)
        : graph_(graph), executor_(executor), stage_invalidation_(stage_invalidation) {}

    // Apply a single modify edit atomically
    std::variant<RebuildPlan, CompileError> apply_modify(const ModifyEdit& edit);

    // Apply a single insert edit atomically (Edit B):
    // retro insert before <stage> : map(_, <lambda>) [as <name>]
    std::variant<RebuildPlan, CompileError> apply_insert(const InsertEdit& edit);

    // Apply a single delete edit atomically (Edit C):
    // retro delete <stage>
    std::variant<RebuildPlan, CompileError> apply_delete(const DeleteEdit& edit);

    // Explain provenance chain for an output
    // Returns list of edits that affected output_name: {EditId, description, first_stage, ops_recomputed, ops_reused}
    struct ExplainItem {
        EditId edit_id = 0;
        std::string edit_description;
        std::string first_stage;
        size_t ops_recomputed = 0;
        size_t ops_reused = 0;
    };
    std::string explain_output(const std::string& output_name) const;

    bool is_stage_invalidation() const { return stage_invalidation_; }
    void set_stage_invalidation(bool val) { stage_invalidation_ = val; }

private:
    Graph& graph_;
    GraphExecutor& executor_;
    bool stage_invalidation_ = false;
};

} // namespace retroc
