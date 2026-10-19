#include "ir/node.hpp"

namespace retroc {

const char* node_kind_to_string(NodeKind kind) {
    switch (kind) {
        case NodeKind::INPUT:  return "INPUT";
        case NodeKind::MAP:    return "MAP";
        case NodeKind::FILTER: return "FILTER";
        case NodeKind::REDUCE: return "REDUCE";
        case NodeKind::JOIN:   return "JOIN";
        case NodeKind::SCALAR: return "SCALAR";
        case NodeKind::OUTPUT: return "OUTPUT";
    }
    return "?";
}

const char* node_status_to_string(NodeStatus status) {
    switch (status) {
        case NodeStatus::CLEAN:   return "CLEAN";
        case NodeStatus::DIRTY:   return "DIRTY";
        case NodeStatus::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}

} // namespace retroc
