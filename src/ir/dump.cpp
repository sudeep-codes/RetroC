#include "ir/dump.hpp"
#include <iomanip>
#include <sstream>

namespace retroc {

void dump_ir(const Graph& graph, std::ostream& out, bool verbose) {
    for (const auto& node_id : graph.order) {
        const auto* node = graph.get_node(node_id);
        if (!node) continue;

        // Columns: node id, kind, name, `: type`, `<-` input ids, then fn/init/expr text.
        // Example:
        // n0 INPUT  rows  : [{id:int,v:float,ok:bool}]
        // n1 FILTER clean : [{id:int,v:float,ok:bool}] <- n0  fn: r => r.ok
        // n2 MAP    vals  : [float] <- n1  fn: r => r.v
        // n3 REDUCE sum   : float <- n2  init: 0.0  fn: (a, x) => a + x
        // n4 SCALAR avg   : float <- n3,n2  expr: sum / to_float(length(vals))
        // n5 OUTPUT avg   : float <- n4

        out << node->id << " ";
        out << std::left << std::setw(6) << node_kind_to_string(node->kind) << " ";
        out << std::left << std::setw(6) << node->name << ": " << (node->type ? node->type->to_ir_string() : "?");

        if (!node->inputs.empty()) {
            out << " <- ";
            for (size_t i = 0; i < node->inputs.size(); ++i) {
                if (i > 0) out << ",";
                out << node->inputs[i];
            }
        }

        // Details according to kind:
        if (node->kind == NodeKind::REDUCE) {
            if (node->init) {
                out << "  init: " << format_expr_ir(node->init.get());
            }
            if (node->fn) {
                out << "  fn: " << format_lambda_ir(*node->fn);
            }
        } else if (node->kind == NodeKind::JOIN) {
            if (node->fn) {
                out << "  ka: " << format_lambda_ir(*node->fn);
            }
            if (node->join_fn_b) {
                out << "  kb: " << format_lambda_ir(*node->join_fn_b);
            }
        } else if (node->kind == NodeKind::MAP || node->kind == NodeKind::FILTER) {
            if (node->fn) {
                out << "  fn: " << format_lambda_ir(*node->fn);
            }
        } else if (node->kind == NodeKind::SCALAR) {
            if (node->expr) {
                out << "  expr: " << format_expr_ir(node->expr.get());
            }
        }

        if (verbose) {
            out << "  [fnHash=" << node->fnHash
                << " status=" << node_status_to_string(node->status)
                << " ver=" << node->version
                << " memo_size=" << node->memo.size() << "]";
        }

        out << "\n";
    }
}

} // namespace retroc
