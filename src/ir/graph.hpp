#pragma once

#include "ir/node.hpp"
#include <map>
#include <vector>
#include <string>

namespace retroc {

struct Graph {
    std::map<NodeId, Node> nodes;
    std::vector<NodeId> order;     // topological order
    std::vector<NodeId> outputs;   // output node ids in source order

    const Node* get_node(const NodeId& id) const {
        auto it = nodes.find(id);
        if (it != nodes.end()) return &it->second;
        return nullptr;
    }

    Node* get_node(const NodeId& id) {
        auto it = nodes.find(id);
        if (it != nodes.end()) return &it->second;
        return nullptr;
    }

    // An OUTPUT node carries the name of the stage it prints. Edits name
    // stages (EDIT_SEMANTICS section 1), so the stage wins; without this the
    // answer would depend on unordered_map iteration order.
    const Node* find_node_by_name(const std::string& name) const {
        const Node* output = nullptr;
        for (const auto& [id, n] : nodes) {
            if (n.name != name) continue;
            if (n.kind != NodeKind::OUTPUT) return &n;
            output = &n;
        }
        return output;
    }

    Node* find_node_by_name(const std::string& name) {
        return const_cast<Node*>(static_cast<const Graph*>(this)->find_node_by_name(name));
    }

    // Assert graph invariants (I1: DAG and topological order, I2: unique NodeIds)
    void assert_invariants() const;

    // Recompute topological order (I1)
    bool recompute_topological_order();

    Graph clone() const {
        Graph g;
        for (const auto& [id, node] : nodes) {
            g.nodes.emplace(id, node.clone());
        }
        g.order = order;
        g.outputs = outputs;
        return g;
    }
};

} // namespace retroc
