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

size_t Memo::bytes() const {
    size_t total = 0;
    // Map entries: RecordId (8) + MapMemoEntry (~32-48 bytes + hash table node overhead)
    total += map_memo.size() * (sizeof(RecordId) + sizeof(MapMemoEntry) + 32);
    // Filter entries: RecordId (8) + FilterMemoEntry (~24 bytes + hash table node overhead)
    total += filter_memo.size() * (sizeof(RecordId) + sizeof(FilterMemoEntry) + 32);
    // Join entries: JoinPairKey (16) + JoinMemoEntry (~56 bytes + hash table node overhead)
    total += join_memo.size() * (sizeof(JoinPairKey) + sizeof(JoinMemoEntry) + 32);
    if (reduce_memo.valid) total += sizeof(ReduceMemoEntry);
    if (scalar_memo.valid) total += sizeof(ScalarMemoEntry);
    return total;
}

} // namespace retroc
