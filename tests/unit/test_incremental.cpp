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

static Graph build_graph(const std::string& src) {
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

// n records { id: i, v: i, ok: i % 3 != 0 } followed by the given stages
static std::string make_program(size_t n, const std::string& stages) {
    std::ostringstream src;
    src << "input rows: [{ id: int, v: int, ok: bool }] = [\n";
    for (size_t i = 0; i < n; ++i) {
        src << "  { id: " << i << ", v: " << i << ", ok: " << ((i % 3 != 0) ? "true" : "false") << " }";
        src << (i + 1 < n ? ",\n" : "\n");
    }
    src << "];\n" << stages;
    return src.str();
}

static ModifyEdit modify_int(const std::string& stage, RecordId id, const std::string& field, int64_t v) {
    ModifyEdit e;
    e.input_stage = stage;
    e.record_id = id;
    e.field_name = field;
    e.new_value = Value::make_int(v);
    e.raw_literal_text = std::to_string(v);
    return e;
}

static ModifyEdit modify_bool(const std::string& stage, RecordId id, const std::string& field, bool v) {
    ModifyEdit e;
    e.input_stage = stage;
    e.record_id = id;
    e.field_name = field;
    e.new_value = Value::make_bool(v);
    e.raw_literal_text = v ? "true" : "false";
    return e;
}

static const StagePlan* find_stage(const RebuildPlan& plan, const std::string& name) {
    for (const auto& sp : plan.stages) {
        if (sp.stage_name == name) return &sp;
    }
    return nullptr;
}

// Every node's stored outHash must equal a from-scratch hash of its result,
// i.e. the O(1) term_sum patches never drift.
static void check_hashes_consistent(const Graph& g, const GraphExecutor& ex) {
    for (const auto& [id, node] : g.nodes) {
        const auto& res = ex.get_result(id);
        if (res.is_collection) {
            CHECK(res.term_sum == collection_term_sum(res.collection_val));
            CHECK(node.outHash == hash_collection(res.collection_val));
        } else {
            CHECK(node.outHash == res.scalar_val.hash());
        }
    }
}

TEST_CASE("Incremental: collection hash patches match a full rehash") {
    Collection col;
    for (RecordId i = 0; i < 50; ++i) {
        col.push_back(CollectionItem{i, Value::make_int(static_cast<int64_t>(i) * 7)});
    }
    uint64_t sum = collection_term_sum(col);
    CHECK(hash_collection(col) == collection_hash_from_sum(sum, col.size()));

    // replace record 10
    sum -= collection_item_term(10, col[10].value.hash());
    col[10].value = Value::make_int(-1);
    sum += collection_item_term(10, col[10].value.hash());
    CHECK(collection_hash_from_sum(sum, col.size()) == hash_collection(col));

    // remove record 20
    sum -= collection_item_term(20, col[20].value.hash());
    col.erase(col.begin() + 20);
    CHECK(collection_hash_from_sum(sum, col.size()) == hash_collection(col));

    // same values under different RecordIds must hash differently
    Collection a = {{0, Value::make_int(1)}, {1, Value::make_int(2)}};
    Collection b = {{0, Value::make_int(2)}, {1, Value::make_int(1)}};
    CHECK(hash_collection(a) != hash_collection(b));
}

TEST_CASE("Incremental: single-record modify evaluates O(1) records per stage") {
    const size_t n = 1000;
    std::string src = make_program(n, R"(
let kept = filter(rows, r => r.ok);
let doubled = map(kept, r => { id: r.id, v: r.v * 2 });
let big = filter(doubled, r => r.v > 100);
let total = reduce(big, 0, (acc, r) => acc + r.v);
output total;
output big;
)");
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);

    // Record 500 (ok, v=500 -> 1000 doubled) changes value but stays in every filter
    auto r1 = prop.apply_modify(modify_int("rows", 500, "v", 400));
    REQUIRE(std::holds_alternative<RebuildPlan>(r1));
    const auto& p1 = std::get<RebuildPlan>(r1);
    for (const char* stage : {"kept", "doubled", "big"}) {
        const StagePlan* sp = find_stage(p1, stage);
        REQUIRE(sp != nullptr);
        CHECK(sp->ops_evaluated == 1);
        CHECK(sp->ops_reused == sp->ops_total - 1);
    }
    check_hashes_consistent(g, ex);

    // Record 502 flips out of the first filter: downstream stages must drop it
    // without re-evaluating anything.
    auto r2 = prop.apply_modify(modify_bool("rows", 502, "ok", false));
    REQUIRE(std::holds_alternative<RebuildPlan>(r2));
    const auto& p2 = std::get<RebuildPlan>(r2);
    REQUIRE(find_stage(p2, "kept") != nullptr);
    REQUIRE(find_stage(p2, "doubled") != nullptr);
    CHECK(find_stage(p2, "kept")->ops_evaluated == 1);
    CHECK(find_stage(p2, "doubled")->ops_evaluated == 0);
    check_hashes_consistent(g, ex);

    // Record 0 flips in: it is evaluated once per stage on the way down
    auto r3 = prop.apply_modify(modify_bool("rows", 0, "ok", true));
    REQUIRE(std::holds_alternative<RebuildPlan>(r3));
    REQUIRE(find_stage(std::get<RebuildPlan>(r3), "doubled") != nullptr);
    CHECK(find_stage(std::get<RebuildPlan>(r3), "doubled")->ops_evaluated == 1);
    check_hashes_consistent(g, ex);

    // Same result as a from-scratch run of the edited program
    EditScript script;
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("rows", 500, "v", 400)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 502, "ok", false)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 0, "ok", true)));
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Incremental: unchanged record value stops at the first stage (early cutoff)") {
    std::string src = make_program(200, R"(
let kept = filter(rows, r => r.ok);
let ids = map(kept, r => r.id);
let total = reduce(ids, 0, (acc, x) => acc + x);
output total;
)");
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);

    // v is not read by ids, so ids' output is unchanged and total is never visited
    auto r = prop.apply_modify(modify_int("rows", 10, "v", 9999));
    REQUIRE(std::holds_alternative<RebuildPlan>(r));
    const auto& plan = std::get<RebuildPlan>(r);
    CHECK(find_stage(plan, "ids") != nullptr);
    CHECK(find_stage(plan, "total") == nullptr);
    CHECK(plan.updated_outputs.empty());
    check_hashes_consistent(g, ex);
}

TEST_CASE("Incremental: B1 stage-level invalidation recomputes whole stages, same outputs") {
    std::string src = make_program(300, R"(
let kept = filter(rows, r => r.ok);
let doubled = map(kept, r => { id: r.id, v: r.v * 2 });
let total = reduce(doubled, 0, (acc, r) => acc + r.v);
output total;
)");
    Graph g1 = build_graph(src);
    GraphExecutor ex1(g1, ".", true);
    ex1.run();
    Propagator b1(g1, ex1, /*stage_invalidation=*/true);

    Graph g2 = build_graph(src);
    GraphExecutor ex2(g2, ".", true);
    ex2.run();
    Propagator b2(g2, ex2);

    auto e = modify_int("rows", 7, "v", 70);
    auto p1 = std::get<RebuildPlan>(b1.apply_modify(e));
    auto p2 = std::get<RebuildPlan>(b2.apply_modify(e));

    const StagePlan* kept1 = find_stage(p1, "kept");
    const StagePlan* doubled1 = find_stage(p1, "doubled");
    REQUIRE(kept1 != nullptr);
    REQUIRE(doubled1 != nullptr);
    CHECK(kept1->ops_evaluated == kept1->ops_total);
    CHECK(doubled1->ops_evaluated == doubled1->ops_total);
    CHECK(find_stage(p2, "doubled")->ops_evaluated == 1);
    CHECK(p1.recomputed_ops > p2.recomputed_ops);
    CHECK(ex1.get_outputs() == ex2.get_outputs());
    check_hashes_consistent(g1, ex1);
}

TEST_CASE("Incremental: stage reference analysis") {
    std::string src = R"(
input xs: [int] = [1, 2, 3];
let ys = map(xs, x => x + 1);
let zs = filter(ys, y => y > 2);
let n = length(ys);
let both = to_float(length(zs)) + 1.0;
output n;
output both;
output zs;
)";
    Graph g = build_graph(src);
    StageRefMap refs = analyze_stage_refs(g);
    CHECK(refs["ys"] == StageRefUse::LENGTH_ONLY);
    CHECK(refs["zs"] == StageRefUse::LENGTH_ONLY);
    CHECK(refs.find("xs") == refs.end());

    GraphExecutor ex(g, ".", true);
    std::ostringstream out;
    ex.run(&out);
    CHECK(out.str() == "n = 3\nboth = 3.0\nzs = [3, 4]\n");
}

TEST_CASE("Incremental: insert and delete reuse downstream records and match rerun") {
    std::string src = make_program(400, R"(
let kept = filter(rows, r => r.ok);
let doubled = map(kept, r => { id: r.id, v: r.v * 2, ok: r.ok });
let total = reduce(doubled, 0, (acc, r) => acc + r.v);
output total;
output doubled;
)");
    // Insert a stage that changes only records with v > 390, then delete it
    EditScript script;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "doubled";
    ins->new_stage_name = "capped";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "r => { id: r.id, v: if r.v > 390 then 390 else r.v, ok: r.ok }";
    script.edits.push_back(ins);
    auto del = std::make_shared<DeleteEdit>();
    del->stage_name = "capped";
    script.edits.push_back(del);

    auto vr = verify_edits(src, script);
    CHECK(vr.ok);

    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);
    auto pi = std::get<RebuildPlan>(prop.apply_insert(*ins));
    // Only records 391..399 that pass the filter change: doubled re-evaluates
    // just those and reuses the rest.
    const StagePlan* d = find_stage(pi, "doubled");
    REQUIRE(d != nullptr);
    CHECK(d->ops_evaluated < 10);
    CHECK(d->ops_reused > 250);
    check_hashes_consistent(g, ex);

    auto pd = std::get<RebuildPlan>(prop.apply_delete(*del));
    REQUIRE(find_stage(pd, "doubled") != nullptr);
    CHECK(find_stage(pd, "doubled")->ops_evaluated < 10);
    check_hashes_consistent(g, ex);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}
