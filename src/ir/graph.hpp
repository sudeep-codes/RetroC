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

    const Node* find_node_by_name(const std::string& name) const {
        for (const auto& [id, n] : nodes) {
            if (n.name == name) return &n;
        }
        return nullptr;
    }

    // Assert graph invariants (I1: DAG and topological order, I2: unique NodeIds)
    void assert_invariants() const;
};

} // namespace retroc
