#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace retroc {

using EditId = uint64_t;

// History log entry per ARCHITECTURE.md section 5 and EDIT_SEMANTICS.md section 12:
// {EditId, edit text, nodes touched, ops recomputed, ops reused, outputs changed}
struct HistoryEntry {
    EditId edit_id = 0;
    std::string description;           // "initial run" or edit text
    std::vector<std::string> nodes_touched;
    size_t ops_recomputed = 0;
    size_t ops_reused = 0;
    std::vector<std::string> outputs_changed;
    size_t total_memo_bytes = 0;       // memory accounting across all nodes
};

struct HistoryLog {
    std::vector<HistoryEntry> entries;
    EditId next_edit_id = 1;

    void add_entry(HistoryEntry entry) {
        entries.push_back(std::move(entry));
    }

    size_t total_memory_bytes() const {
        if (entries.empty()) return 0;
        return entries.back().total_memo_bytes;
    }
};

} // namespace retroc
