#include "doctest.h"
#include "edit/edit.hpp"
#include "reuse/propagator.hpp"
#include "verify/verifier.hpp"
#include "ir/builder.hpp"
#include "sema/sema.hpp"
#include "parser/parser.hpp"
#include "lexer/lexer.hpp"
#include <sstream>

using namespace retroc;

static Graph parse_and_build(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(lex_res));

    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    REQUIRE(std::holds_alternative<Program>(parse_res));

    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    REQUIRE(std::holds_alternative<TypedProgram>(sema_res));

    IRBuilder builder(std::get<TypedProgram>(sema_res));
    return builder.build();
}

TEST_CASE("Edit script parser: modify syntax") {
    std::string script_text = R"(
// Modify edit script
retro modify rows[1].ok = true
retro modify xs[0] = 42
)";
    EditScriptParser parser(script_text);
    auto res = parser.parse();
    REQUIRE(std::holds_alternative<EditScript>(res));

    const auto& script = std::get<EditScript>(res);
    REQUIRE(script.edits.size() == 2);

    CHECK(script.edits[0]->kind() == EditKind::MODIFY);
    const auto& e1 = static_cast<const ModifyEdit&>(*script.edits[0]);
    CHECK(e1.input_stage == "rows");
    CHECK(e1.record_id == 1);
    CHECK(e1.field_name == "ok");
    CHECK(e1.new_value.kind == ValueKind::BOOL);
    CHECK(e1.new_value.bool_val == true);

    CHECK(script.edits[1]->kind() == EditKind::MODIFY);
    const auto& e2 = static_cast<const ModifyEdit&>(*script.edits[1]);
    CHECK(e2.input_stage == "xs");
    CHECK(e2.record_id == 0);
    CHECK(e2.field_name == "");
    CHECK(e2.new_value.kind == ValueKind::INT);
    CHECK(e2.new_value.int_val == 42);
}

TEST_CASE("Edit script parser: error cases") {
    // Bad command
    {
        EditScriptParser parser("foo bar");
        auto res = parser.parse();
        CHECK(std::holds_alternative<CompileError>(res));
    }
    // Missing brackets
    {
        EditScriptParser parser("retro modify rows.ok = true");
        auto res = parser.parse();
        CHECK(std::holds_alternative<CompileError>(res));
    }
    // Bad literal
    {
        EditScriptParser parser("retro modify rows[0].ok = invalid_tok");
        auto res = parser.parse();
        CHECK(std::holds_alternative<CompileError>(res));
    }
}

TEST_CASE("Phase 7: Edit A worked example (Ex2 modify rows[1].ok = true)") {
    std::string src = R"(
input rows: [{ id: int, v: float, ok: bool }] = [
  { id: 1, v: 2.0, ok: true },
  { id: 2, v: 5.0, ok: false },
  { id: 3, v: 4.0, ok: true }
];
let clean = filter(rows, r => r.ok);
let vals = map(clean, r => r.v);
let sum = reduce(vals, 0.0, (a, x) => a + x);
let avg = sum / to_float(length(vals));
output avg;
)";

    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();

    auto out_initial = executor.get_outputs();
    REQUIRE(out_initial.size() == 1);
    CHECK(out_initial[0].second == "3.0");

    // Apply modify: rows[1].ok = true
    ModifyEdit edit;
    edit.input_stage = "rows";
    edit.record_id = 1;
    edit.field_name = "ok";
    edit.new_value = Value::make_bool(true);
    edit.raw_literal_text = "true";

    Propagator prop(g, executor);
    auto plan_res = prop.apply_modify(edit);
    REQUIRE(std::holds_alternative<RebuildPlan>(plan_res));

    const auto& plan = std::get<RebuildPlan>(plan_res);
    // EDIT_SEMANTICS.md section 9 worked example:
    // clean re-evaluates record 1 (now kept)
    // vals evaluates record 1 (1 op)
    // sum recomputes fold over 3 elements (3 ops)
    // avg 1 op
    // New avg: (2.0 + 5.0 + 4.0) / 3.0 = 3.6666666666666665
    auto out_edited = executor.get_outputs();
    REQUIRE(out_edited.size() == 1);
    CHECK(out_edited[0].first == "avg");
    CHECK(out_edited[0].second.substr(0, 5) == "3.666");

    CHECK(plan.recomputed_ops > 0);
    CHECK(plan.reused_ops > 0);
    CHECK(plan.updated_outputs == std::vector<std::string>{"avg"});
}

TEST_CASE("Phase 7: modify-to-same-value does zero recompute") {
    std::string src = R"(
input rows: [{ id: int, v: float, ok: bool }] = [
  { id: 1, v: 2.0, ok: true },
  { id: 2, v: 5.0, ok: false },
  { id: 3, v: 4.0, ok: true }
];
let clean = filter(rows, r => r.ok);
let vals = map(clean, r => r.v);
let sum = reduce(vals, 0.0, (a, x) => a + x);
let avg = sum / to_float(length(vals));
output avg;
)";

    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();

    // Modify row 0 ok to true (already true!)
    ModifyEdit edit;
    edit.input_stage = "rows";
    edit.record_id = 0;
    edit.field_name = "ok";
    edit.new_value = Value::make_bool(true);
    edit.raw_literal_text = "true";

    Propagator prop(g, executor);
    auto plan_res = prop.apply_modify(edit);
    REQUIRE(std::holds_alternative<RebuildPlan>(plan_res));

    const auto& plan = std::get<RebuildPlan>(plan_res);
    CHECK(plan.recomputed_ops == 0);
    CHECK(plan.updated_outputs.empty());
}

TEST_CASE("Phase 7: invalid edit preconditions produce E601") {
    std::string src = R"(
input rows: [{ id: int, v: float, ok: bool }] = [
  { id: 1, v: 2.0, ok: true }
];
output rows;
)";
    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();
    Propagator prop(g, executor);

    // 1. Unknown stage
    {
        ModifyEdit e;
        e.input_stage = "nonexistent";
        e.record_id = 0;
        e.field_name = "v";
        e.new_value = Value::make_float(1.0);
        auto res = prop.apply_modify(e);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
    }
    // 2. Unknown RecordId
    {
        ModifyEdit e;
        e.input_stage = "rows";
        e.record_id = 99;
        e.field_name = "v";
        e.new_value = Value::make_float(1.0);
        auto res = prop.apply_modify(e);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
    }
    // 3. Unknown field
    {
        ModifyEdit e;
        e.input_stage = "rows";
        e.record_id = 0;
        e.field_name = "bad_field";
        e.new_value = Value::make_float(1.0);
        auto res = prop.apply_modify(e);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
    }
    // 4. Type mismatch (v is float, try setting int)
    {
        ModifyEdit e;
        e.input_stage = "rows";
        e.record_id = 0;
        e.field_name = "v";
        e.new_value = Value::make_int(10);
        auto res = prop.apply_modify(e);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
    }
}

TEST_CASE("Phase 7: verify_edits correctness statement") {
    std::string src = R"(
input rows: [{ id: int, v: float, ok: bool }] = [
  { id: 1, v: 2.0, ok: true },
  { id: 2, v: 5.0, ok: false },
  { id: 3, v: 4.0, ok: true }
];
let clean = filter(rows, r => r.ok);
let vals = map(clean, r => r.v);
let sum = reduce(vals, 0.0, (a, x) => a + x);
let avg = sum / to_float(length(vals));
output avg;
)";
    EditScript script;
    auto e = std::make_shared<ModifyEdit>();
    e->input_stage = "rows";
    e->record_id = 1;
    e->field_name = "ok";
    e->new_value = Value::make_bool(true);
    e->raw_literal_text = "true";
    script.edits.push_back(e);

    auto res = verify_edits(src, script);
    CHECK(res.ok == true);
    REQUIRE(res.propagated_outputs.size() == 1);
    REQUIRE(res.rerun_outputs.size() == 1);
    CHECK(res.propagated_outputs[0].second == res.rerun_outputs[0].second);
}

TEST_CASE("Phase 8: Edit B insert stage (map and filter)") {
    std::string src = R"(
input xs: [float] = [-0.5, 0.25, 1.5];
let ys = map(xs, x => x * 2.0);
output ys;
)";
    // Insert clamp before ys
    EditScript script;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "ys";
    ins->new_stage_name = "clamped";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "x => if x < 0.0 then 0.0 else x";
    script.edits.push_back(ins);

    auto res = verify_edits(src, script);
    CHECK(res.ok == true);
    REQUIRE(res.propagated_outputs.size() == 1);
    REQUIRE(res.rerun_outputs.size() == 1);
    CHECK(res.propagated_outputs[0].second == res.rerun_outputs[0].second);
    // xs = [-0.5, 0.25, 1.5] -> clamped = [0.0, 0.25, 1.5] -> ys = [0.0, 0.5, 3.0]
    CHECK(res.propagated_outputs[0].second == "[0.0, 0.5, 3.0]");
}

TEST_CASE("Phase 8: Edit C delete stage") {
    std::string src = R"(
input xs: [float] = [1.0, 2.0, 3.0];
let doubled = map(xs, x => x * 2.0);
let shifted = map(doubled, x => x + 1.0);
output shifted;
)";
    EditScript script;
    auto del = std::make_shared<DeleteEdit>();
    del->stage_name = "doubled";
    script.edits.push_back(del);

    auto res = verify_edits(src, script);
    CHECK(res.ok == true);
    REQUIRE(res.propagated_outputs.size() == 1);
    REQUIRE(res.rerun_outputs.size() == 1);
    CHECK(res.propagated_outputs[0].second == res.rerun_outputs[0].second);
    // shifted now takes xs: [1.0+1.0, 2.0+1.0, 3.0+1.0] = [2.0, 3.0, 4.0]
    CHECK(res.propagated_outputs[0].second == "[2.0, 3.0, 4.0]");
}

TEST_CASE("Phase 8: Atomic rollback on invalid edits") {
    std::string src = R"(
input xs: [int] = [1, 2, 3];
let ys = map(xs, x => x + 1);
output ys;
)";
    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();
    Propagator prop(g, executor);

    // 1. Invalid insert: insert before an INPUT
    {
        InsertEdit ins;
        ins.before_stage = "xs";
        ins.builtin_name = "map";
        ins.raw_lambda_text = "x => x";
        auto res = prop.apply_insert(ins);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
        CHECK(g.nodes.size() == 3); // state unchanged
    }

    // 2. Invalid insert: lambda type error
    {
        InsertEdit ins;
        ins.before_stage = "ys";
        ins.builtin_name = "map";
        ins.raw_lambda_text = "x => x + 1.0"; // int + float -> type error
        auto res = prop.apply_insert(ins);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
        CHECK(g.nodes.size() == 3);
    }

    // 3. Invalid delete: delete INPUT stage
    {
        DeleteEdit del;
        del.stage_name = "xs";
        auto res = prop.apply_delete(del);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
        CHECK(g.nodes.size() == 3);
    }

    // 4. Invalid delete: delete the direct source of an OUTPUT
    {
        DeleteEdit del;
        del.stage_name = "ys";
        auto res = prop.apply_delete(del);
        REQUIRE(std::holds_alternative<CompileError>(res));
        CHECK(std::get<CompileError>(res).code == ErrorCode::E601);
        CHECK(g.nodes.size() == 3);
    }
}

TEST_CASE("Phase 8: Metamorphic tests (insert identity map)") {
    // TEST_PLAN.md section 5:
    // "Insert an identity map (x => x) => outputs unchanged, 100% reuse downstream."
    std::string src = R"(
input xs: [float] = [1.0, 2.0, 3.0];
let ys = map(xs, x => x * 2.0);
output ys;
)";
    EditScript script;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "ys";
    ins->new_stage_name = "id_map";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "x => x";
    script.edits.push_back(ins);

    auto res = verify_edits(src, script);
    CHECK(res.ok == true);
    REQUIRE(res.propagated_outputs.size() == 1);
    CHECK(res.propagated_outputs[0].second == "[2.0, 4.0, 6.0]");
}

