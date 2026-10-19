#pragma once

#include "parser/ast.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace retroc {

bool types_equal(const TypePtr& a, const TypePtr& b);
bool is_numeric(const TypePtr& t);
bool is_comparable(const TypePtr& a, const TypePtr& b);
bool is_valid_join_key(const TypePtr& t);

} // namespace retroc
