#pragma once

#include "ir/graph.hpp"
#include "edit/edit.hpp"
#include <string>

namespace retroc {

struct VerificationResult {
    bool ok = false;
    std::string mismatch_details;
    std::vector<std::pair<std::string, std::string>> propagated_outputs;
    std::vector<std::pair<std::string, std::string>> rerun_outputs;
    std::vector<std::pair<std::string, Value>> propagated_output_values;
    std::vector<std::pair<std::string, Value>> rerun_output_values;
};

// Compares the output of propagating edits on an active execution
// against a full rerun of the edited program from scratch with an empty memo.
VerificationResult verify_edits(const std::string& original_source,
                                const EditScript& script,
                                const std::string& source_dir = ".");

} // namespace retroc
