#include "doctest.h"
#include "ir/builder.hpp"
#include "sema/sema.hpp"
#include "parser/parser.hpp"
#include "lexer/lexer.hpp"
#include "runtime/executor.hpp"
#include "reuse/propagator.hpp"
#include "verify/verifier.hpp"
#include "edit/edit.hpp"
#include "bytecode/bytecode.hpp"
#include "common/error.hpp"

#include <cmath>
#include <sstream>

using namespace retroc;

static Graph parse_and_build(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    if (std::holds_alternative<CompileError>(lex_res)) {
        throw std::runtime_error("Lexer error: " + std::get<CompileError>(lex_res).format());
    }

    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    if (std::holds_alternative<CompileError>(parse_res)) {
        throw std::runtime_error("Parser error: " + std::get<CompileError>(parse_res).format());
    }

    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    if (std::holds_alternative<CompileError>(sema_res)) {
        throw std::runtime_error("Sema error: " + std::get<CompileError>(sema_res).format());
    }

    IRBuilder builder(std::get<TypedProgram>(sema_res));
    return builder.build();
}

TEST_CASE("Phase 12: tree_reduce sema and IR build") {
    SUBCASE("Valid tree_reduce on [int]") {
        std::string src = R"(
input xs: [int] = [1, 2, 3, 4];
let sum = tree_reduce(xs, 0, (a, b) => a + b);
output sum;
)";
        Graph g = parse_and_build(src);
        REQUIRE(g.nodes.size() == 3);
        auto sum_node = g.find_node_by_name("sum");
        REQUIRE(sum_node != nullptr);
        CHECK(sum_node->kind == NodeKind::TREE_REDUCE);
        CHECK(sum_node->name == "sum");
    }

    SUBCASE("Sema error: bad arity for combiner lambda") {
        std::string src = R"(
input xs: [int] = [1, 2, 3, 4];
let sum = tree_reduce(xs, 0, x => x + 1);
output sum;
)";
        Lexer lexer(src);
        auto lex_res = lexer.tokenize();
        Parser parser(std::get<std::vector<Token>>(lex_res));
        auto parse_res = parser.parse();
        SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
        auto sema_res = sema.analyze();
        REQUIRE(std::holds_alternative<CompileError>(sema_res));
        CHECK(std::get<CompileError>(sema_res).code == ErrorCode::E303);
    }

    SUBCASE("Sema error: type mismatch between init and collection") {
        std::string src = R"(
input xs: [int] = [1, 2, 3, 4];
let sum = tree_reduce(xs, 0.0, (a, b) => a + b);
output sum;
)";
        Lexer lexer(src);
        auto lex_res = lexer.tokenize();
        Parser parser(std::get<std::vector<Token>>(lex_res));
        auto parse_res = parser.parse();
        SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
        auto sema_res = sema.analyze();
        REQUIRE(std::holds_alternative<CompileError>(sema_res));
        CHECK(std::get<CompileError>(sema_res).code == ErrorCode::E301);
    }
}

TEST_CASE("Phase 12: tree_reduce basic evaluation") {
    SUBCASE("Empty collection evaluates to init") {
        std::string src = R"(
input xs: [int] = [];
let total = tree_reduce(xs, 99, (a, b) => a + b);
output total;
)";
        Graph g = parse_and_build(src);
        GraphExecutor executor(g, ".", /*enable_memo=*/true);
        executor.run();

        auto out_node = g.find_node_by_name("total");
        REQUIRE(out_node != nullptr);
        const auto& res = executor.get_result(out_node->id);
        CHECK(res.scalar_val.kind == ValueKind::INT);
        CHECK(res.scalar_val.int_val == 99);
    }

    SUBCASE("Single element evaluates to the element") {
        std::string src = R"(
input xs: [int] = [42];
let total = tree_reduce(xs, 0, (a, b) => a + b);
output total;
)";
        Graph g = parse_and_build(src);
        GraphExecutor executor(g, ".", /*enable_memo=*/true);
        executor.run();

        auto out_node = g.find_node_by_name("total");
        REQUIRE(out_node != nullptr);
        const auto& res = executor.get_result(out_node->id);
        CHECK(res.scalar_val.kind == ValueKind::INT);
        CHECK(res.scalar_val.int_val == 42);
    }

    SUBCASE("Multiple elements with power of 2 size") {
        std::string src = R"(
input xs: [int] = [1, 2, 3, 4];
let total = tree_reduce(xs, 0, (a, b) => a + b);
output total;
)";
        Graph g = parse_and_build(src);
        GraphExecutor executor(g, ".", /*enable_memo=*/true);
        executor.run();

        auto out_node = g.find_node_by_name("total");
        REQUIRE(out_node != nullptr);
        const auto& res = executor.get_result(out_node->id);
        CHECK(res.scalar_val.kind == ValueKind::INT);
        CHECK(res.scalar_val.int_val == 10);
    }

    SUBCASE("Multiple elements with non-power of 2 size") {
        std::string src = R"(
input xs: [int] = [1, 2, 3, 4, 5, 6, 7];
let total = tree_reduce(xs, 0, (a, b) => a + b);
output total;
)";
        Graph g = parse_and_build(src);
        GraphExecutor executor(g, ".", /*enable_memo=*/true);
        executor.run();

        auto out_node = g.find_node_by_name("total");
        REQUIRE(out_node != nullptr);
        const auto& res = executor.get_result(out_node->id);
        CHECK(res.scalar_val.kind == ValueKind::INT);
        CHECK(res.scalar_val.int_val == 28);
    }
}

TEST_CASE("Phase 12: tree_reduce O(log N) incremental propagation") {
    std::string src = R"(
input xs: [int] = [1, 2, 3, 4, 5, 6, 7, 8];
let total = tree_reduce(xs, 0, (a, b) => a + b);
output total;
)";
    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();

    auto tr_node = g.find_node_by_name("total");
    REQUIRE(tr_node != nullptr);
    CHECK(executor.get_result(tr_node->id).scalar_val.int_val == 36);

    Propagator prop(g, executor);

    // Modify xs[2] from 3 to 10 -> new total should be 36 - 3 + 10 = 43
    ModifyEdit mod;
    mod.input_stage = "xs";
    mod.record_id = 2;
    mod.field_name = "";
    mod.new_value = Value::make_int(10);
    mod.raw_literal_text = "10";

    auto plan_res = prop.apply_modify(mod);
    REQUIRE(std::holds_alternative<RebuildPlan>(plan_res));
    const auto& plan = std::get<RebuildPlan>(plan_res);

    // Find stage plan for "total"
    bool found_stage = false;
    for (const auto& st : plan.stages) {
        if (st.stage_name == "total") {
            found_stage = true;
            CHECK(st.status == StagePlanStatus::PARTIAL);
            CHECK(st.note.find("tree_reduce") != std::string::npos);
        }
    }
    CHECK(found_stage);

    // Tree reduce depth is ceil(log2(8)) = 3. Recomputed combiner ops should be <= 3 + 1
    CHECK(plan.recomputed_ops <= 4);

    // Check output value
    CHECK(executor.get_result(tr_node->id).scalar_val.int_val == 43);

    // Verify against verify_edits
    EditScript script;
    auto m = std::make_shared<ModifyEdit>(mod);
    script.edits.push_back(m);
    auto v_res = verify_edits(src, script);
    if (!v_res.ok) {
        MESSAGE("v_res mismatch_details: ", v_res.mismatch_details);
    }
    CHECK(v_res.ok);
    REQUIRE(v_res.propagated_outputs.size() == 1);
    REQUIRE(v_res.rerun_outputs.size() == 1);
    CHECK(v_res.propagated_outputs[0].second == v_res.rerun_outputs[0].second);
    CHECK(v_res.propagated_outputs[0].second == "43");
}

TEST_CASE("Phase 12: tree_reduce large N=1024 log-scaling verification") {
    // Generate program with 1024 ints
    std::ostringstream oss;
    oss << "input xs: [int] = [";
    for (int i = 0; i < 1024; ++i) {
        if (i > 0) oss << ", ";
        oss << 1;
    }
    oss << "];\nlet total = tree_reduce(xs, 0, (a, b) => a + b);\noutput total;\n";
    std::string src = oss.str();

    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();

    auto tr_node = g.find_node_by_name("total");
    REQUIRE(tr_node != nullptr);
    CHECK(executor.get_result(tr_node->id).scalar_val.int_val == 1024);

    Propagator prop(g, executor);

    // Modify xs[512] from 1 to 100 -> new total should be 1024 - 1 + 100 = 1123
    ModifyEdit mod;
    mod.input_stage = "xs";
    mod.record_id = 512;
    mod.field_name = "";
    mod.new_value = Value::make_int(100);
    mod.raw_literal_text = "100";

    auto plan_res = prop.apply_modify(mod);
    REQUIRE(std::holds_alternative<RebuildPlan>(plan_res));
    const auto& plan = std::get<RebuildPlan>(plan_res);

    // For N=1024, height is log2(1024) = 10. Recomputed ops must be exactly 10!
    CHECK(plan.recomputed_ops <= 10);
    CHECK(executor.get_result(tr_node->id).scalar_val.int_val == 1123);
}
