#pragma once

#include "edit/edit.hpp"
#include <string>

namespace retroc {

// Apply modify edit script to the source text of a program file.
// Returns rewritten program source on success, or throws/returns error.
std::variant<std::string, CompileError> apply_modify_to_source(const std::string& source_code,
                                                              const ModifyEdit& edit);

std::variant<std::string, CompileError> apply_insert_to_source(const std::string& source_code,
                                                              const InsertEdit& edit,
                                                              const std::string& actual_stage_name);

std::variant<std::string, CompileError> apply_delete_to_source(const std::string& source_code,
                                                              const DeleteEdit& edit,
                                                              const std::string& input_stage_name);

} // namespace retroc
