#pragma once

#include "common/value.hpp"
#include "ir/graph.hpp"
#include "runtime/history.hpp"
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace retroc {

struct NodeExecutionResult {
  bool is_collection = false;
  Collection collection_val;
  Value scalar_val;

  // Fast hash without constructing a temporary Value::make_array
  uint64_t hash() const {
    if (is_collection) {
      return hash_collection(collection_val);
    }
    return scalar_val.hash();
  }

  // Lightweight representation for stage_values: scalars are preserved;
  // collections don't duplicate 100k items in memory just to represent the
  // stage.
  Value as_stage_value() const {
    if (is_collection) {
      Value val;
      val.kind = ValueKind::ARRAY;
      val.int_val = static_cast<int64_t>(collection_val.size());
      return val;
    }
    return scalar_val;
  }

  // Convert to a printable or representative single Value
  Value as_value() const {
    if (is_collection) {
      std::vector<Value> items;
      items.reserve(collection_val.size());
      for (const auto &it : collection_val) {
        items.push_back(it.value);
      }
      return Value::make_array(std::move(items));
    }
    return scalar_val;
  }
};

class GraphExecutor {
public:
  explicit GraphExecutor(Graph &graph, std::string source_dir = ".",
                         bool enable_memo = true)
      : graph_(graph), source_dir_(std::move(source_dir)),
        enable_memo_(enable_memo) {}

  GraphExecutor(Graph &graph, const GraphExecutor &other)
      : graph_(graph), source_dir_(other.source_dir_),
        enable_memo_(other.enable_memo_), results_(other.results_),
        stage_values_(other.stage_values_), outputs_(other.outputs_),
        history_(other.history_) {}

  // Executes the entire graph from scratch.
  // If output_stream is provided, prints "name = <value>\n" for each OUTPUT
  // node. Returns 0 on success, or throws RuntimeError on E501.
  void run(std::ostream *output_stream = nullptr);

  // Get the execution result of a node
  const NodeExecutionResult &get_result(const NodeId &id) const;

  // Get all output values as (stage_name, string_representation)
  std::vector<std::pair<std::string, std::string>> get_outputs() const;

  // Get all output values as full Value objects (stage_name, Value)
  std::vector<std::pair<std::string, Value>> get_output_values() const;

  // History log of runs/edits
  const HistoryLog &get_history() const { return history_; }
  HistoryLog &get_history() { return history_; }

  // Memory accounting: total bytes used by all node memo tables in the graph
  size_t total_memo_bytes() const;

  // Mutable access for Propagator
  std::unordered_map<NodeId, NodeExecutionResult> &mutable_results() {
    return results_;
  }
  const std::unordered_map<NodeId, NodeExecutionResult> &
  get_all_results() const {
    return results_;
  }
  std::unordered_map<std::string, Value> &mutable_stage_values() {
    return stage_values_;
  }
  const std::unordered_map<std::string, Value> &get_all_stage_values() const {
    return stage_values_;
  }
  std::vector<std::pair<std::string, std::string>> &mutable_outputs() {
    return outputs_;
  }
  const std::string &source_dir() const { return source_dir_; }
  bool enable_memo() const { return enable_memo_; }

  // Pre-cache or provide in-memory CSV collections so repeated benchmark runs
  // don't re-read disk
  static std::unordered_map<std::string, Collection> &csv_cache() {
    static std::unordered_map<std::string, Collection> cache;
    return cache;
  }

  struct StateSnapshot {
    std::unordered_map<NodeId, NodeExecutionResult> results;
    std::unordered_map<std::string, Value> stage_values;
    std::vector<std::pair<std::string, std::string>> outputs;
    HistoryLog history;
  };

  StateSnapshot snapshot() const {
    return StateSnapshot{results_, stage_values_, outputs_, history_};
  }

  void restore(const StateSnapshot &snap) {
    results_ = snap.results;
    stage_values_ = snap.stage_values;
    outputs_ = snap.outputs;
    history_ = snap.history;
  }

private:
  void execute_node(Node &node, HistoryEntry &entry);

  Graph &graph_;
  std::string source_dir_;
  bool enable_memo_ = true;
  std::unordered_map<NodeId, NodeExecutionResult> results_;
  std::unordered_map<std::string, Value>
      stage_values_; // stage name -> single Value
  std::vector<std::pair<std::string, std::string>> outputs_;
  HistoryLog history_;
};

} // namespace retroc
