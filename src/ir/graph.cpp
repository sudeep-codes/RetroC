#include "ir/graph.hpp"
#include <unordered_set>
#include <unordered_map>
#include <cassert>
#include <stdexcept>

namespace retroc {

void Graph::assert_invariants() const {
#ifndef NDEBUG
    // Invariant I2: NodeIds are unique and match the keys in nodes map
    // (std::map naturally has unique keys; verify each node.id == key)
    std::unordered_set<NodeId> seen_ids;
    for (const auto& [id, node] : nodes) {
        assert(node.id == id && "Invariant I2 violated: Node id must match map key");
        assert(seen_ids.insert(id).second && "Invariant I2 violated: NodeId must be unique");
    }

    // Invariant I1: The graph is a DAG; order is a valid topological order.
    // 1. Every node in nodes must appear in order, and order size matches nodes size.
    assert(order.size() == nodes.size() && "Invariant I1 violated: order must contain all nodes");
    
    std::unordered_map<NodeId, size_t> pos_in_order;
    for (size_t i = 0; i < order.size(); ++i) {
        const auto& id = order[i];
        assert(nodes.find(id) != nodes.end() && "Invariant I1 violated: node in order does not exist in graph");
        assert(pos_in_order.find(id) == pos_in_order.end() && "Invariant I1 violated: duplicate node in order");
        pos_in_order[id] = i;
    }

    // 2. For every node, all of its input nodes must appear strictly before it in topological order (no cycles, valid DAG).
    for (const auto& [id, node] : nodes) {
        size_t node_pos = pos_in_order.at(id);
        for (const auto& input_id : node.inputs) {
            auto in_it = pos_in_order.find(input_id);
            assert(in_it != pos_in_order.end() && "Invariant I1 violated: input node does not exist in graph");
            assert(in_it->second < node_pos && "Invariant I1 violated: input node must appear before dependent node in topological order");
        }
    }

    // Check outputs reference valid nodes
    for (const auto& out_id : outputs) {
        assert(nodes.find(out_id) != nodes.end() && "Output references non-existent node");
    }
#endif
}

bool Graph::recompute_topological_order() {
    // Kahn's algorithm or DFS topological sort
    // In-degree for each node = number of its inputs
    std::unordered_map<NodeId, size_t> in_degree;
    std::unordered_map<NodeId, std::vector<NodeId>> adj; // u -> [v] where u is an input to v

    for (const auto& [id, node] : nodes) {
        in_degree[id] = node.inputs.size();
        for (const auto& in_id : node.inputs) {
            adj[in_id].push_back(id);
        }
    }

    // Queue of nodes with in_degree == 0, preserving stable original order if possible
    std::vector<NodeId> zero_in;
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) {
            zero_in.push_back(id);
        }
    }

    std::vector<NodeId> new_order;
    new_order.reserve(nodes.size());

    size_t head = 0;
    while (head < zero_in.size()) {
        NodeId u = zero_in[head++];
        new_order.push_back(u);

        for (const auto& v : adj[u]) {
            if (--in_degree[v] == 0) {
                zero_in.push_back(v);
            }
        }
    }

    if (new_order.size() != nodes.size()) {
        return false; // Cycle detected
    }

    order = std::move(new_order);
    return true;
}

} // namespace retroc
