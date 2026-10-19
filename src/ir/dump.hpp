#pragma once

#include "ir/graph.hpp"
#include <iostream>

namespace retroc {

// Dump the graph in canonical format (golden tests compare this exactly)
void dump_ir(const Graph& graph, std::ostream& out, bool verbose = false);

} // namespace retroc
