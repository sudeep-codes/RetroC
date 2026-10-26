#pragma once

#include "common/value.hpp"
#include "parser/ast.hpp"
#include <string>
#include <vector>

namespace retroc {

// Loads a CSV file into a Collection according to the target record element type.
// Flat records of base types only: int, float, bool, string.
// Header row must match field names in record type.
// Assigns 0-based stable RecordIds (0, 1, 2, ...).
Collection load_csv(const std::string& path, const TypePtr& elem_type);

// Writes a Collection to a CSV file matching the element type fields
void save_csv(const std::string& path, const Collection& collection, const TypePtr& elem_type);

} // namespace retroc
