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

// The stage itself, not the OUTPUT node that shares its name
static const Node& node_named(const Graph& g, const std::string& name) {
    for (const auto& [id, node] : g.nodes) {
        if (node.name == name && node.kind != NodeKind::OUTPUT) return node;
    }
    FAIL("no stage " << name);
    return g.nodes.begin()->second;
}

TEST_CASE("Cost model: additive reduce detection") {
    std::string src = make_program(5, R"(
let ids = map(rows, r => r.v);
let fs = map(rows, r => to_float(r.v));
let a = reduce(ids, 0, (acc, x) => acc + x);
let b = reduce(rows, 3, (acc, r) => r.v * 2 + acc);
let c = reduce(fs, 0.0, (acc, x) => acc + x);
let d = reduce(ids, 0, (acc, x) => acc * x);
let e = reduce(ids, 0, (acc, x) => acc + acc);
let f = reduce(ids, 0, (acc, x) => if x > acc then x else acc);
let g = reduce(ids, 0, (acc, x) => acc + x + length(ids));
output a; output b; output c; output d; output e; output f; output g;
)");
    Graph g = build_graph(src);
    CHECK(additive_reduce_term(node_named(g, "a")) != nullptr);
    CHECK(additive_reduce_term(node_named(g, "b")) != nullptr);   // term + acc
    CHECK(additive_reduce_term(node_named(g, "c")) == nullptr);   // float: not associative
    CHECK(additive_reduce_term(node_named(g, "d")) == nullptr);   // not +
    CHECK(additive_reduce_term(node_named(g, "e")) == nullptr);   // term reads acc
    CHECK(additive_reduce_term(node_named(g, "f")) == nullptr);   // not +
    CHECK(additive_reduce_term(node_named(g, "g")) == nullptr);   // (acc + x) + ...: acc not alone
}

TEST_CASE("Cost model: additive reduce is patched per changed record") {
    const size_t n = 1000;
    std::string src = make_program(n, R"(
let kept = filter(rows, r => r.ok);
let total = reduce(kept, 7, (acc, r) => r.v * 3 + acc);
let fsum = reduce(kept, 0.0, (acc, r) => acc + to_float(r.v));
output total;
output fsum;
)");
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);

    // value change, record leaving the filter, record entering the filter
    EditScript script;
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("rows", 500, "v", -40)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 502, "ok", false)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 3, "ok", true)));
    for (const auto& e : script.edits) {
        auto r = prop.apply_modify(*std::static_pointer_cast<ModifyEdit>(e));
        REQUIRE(std::holds_alternative<RebuildPlan>(r));
        const auto& plan = std::get<RebuildPlan>(r);
        const StagePlan* t = find_stage(plan, "total");
        REQUIRE(t != nullptr);
        CHECK(t->status == StagePlanStatus::PARTIAL);
        CHECK(t->ops_evaluated <= 1);
        // the float sum is not additive and still folds every record
        const StagePlan* fs = find_stage(plan, "fsum");
        REQUIRE(fs != nullptr);
        CHECK(fs->ops_evaluated == fs->ops_total);
        check_hashes_consistent(g, ex);
    }
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Cost model: many changed records fall back to a full fold") {
    std::string src = make_program(64, R"(
let doubled = map(rows, r => { id: r.id, v: r.v * 2, ok: r.ok });
let total = reduce(doubled, 0, (acc, r) => acc + r.v);
output total;
)");
    // the inserted stage changes every record, so patching would cost more
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "doubled";
    ins->new_stage_name = "plus";
    ins->builtin_name = "map";
    ins->raw_lambda_text = "r => { id: r.id, v: r.v + 1, ok: r.ok }";
    EditScript script;
    script.edits.push_back(ins);

    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);
    auto plan = std::get<RebuildPlan>(prop.apply_insert(*ins));
    const StagePlan* t = find_stage(plan, "total");
    REQUIRE(t != nullptr);
    CHECK(t->status == StagePlanStatus::FULL);
    CHECK(t->ops_evaluated == 64);
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Cost model: additive reduce wraps around like the fold") {
    std::string src = R"(
input xs: [{ id: int, v: int }] = [
  { id: 0, v: 9223372036854775807 }, { id: 1, v: 5 }, { id: 2, v: -3 }
];
let total = reduce(xs, 1, (acc, r) => acc + r.v);
output total;
)";
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);
    EditScript script;
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("xs", 2, "v", 9223372036854775807LL)));
    auto r = prop.apply_modify(*std::static_pointer_cast<ModifyEdit>(script.edits[0]));
    REQUIRE(std::holds_alternative<RebuildPlan>(r));
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Cost model: join reuses keys of unchanged records") {
    std::ostringstream src;
    src << "input users: [{ uid: int, w: int }] = [\n";
    for (int i = 0; i < 50; ++i) src << "  { uid: " << i << ", w: " << i * 2 << " }" << (i < 49 ? ",\n" : "\n");
    src << "];\ninput orders: [{ oid: int, uid: int, amt: int }] = [\n";
    for (int i = 0; i < 200; ++i) {
        src << "  { oid: " << i << ", uid: " << (i * 7) % 60 << ", amt: " << i << " }" << (i < 199 ? ",\n" : "\n");
    }
    src << "];\n"
           "let j = join(users, orders, u => u.uid, o => o.uid);\n"
           "let amts = map(j, p => p.right.amt + p.left.w);\n"
           "let total = reduce(amts, 0, (acc, x) => acc + x);\n"
           "output total;\noutput amts;\n";
    std::string program = src.str();

    Graph g = build_graph(program);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);

    EditScript script;
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("orders", 10, "amt", 999)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("orders", 11, "uid", 3)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("users", 3, "w", -1)));
    for (const auto& e : script.edits) {
        auto plan = std::get<RebuildPlan>(prop.apply_modify(*std::static_pointer_cast<ModifyEdit>(e)));
        const StagePlan* js = find_stage(plan, "j");
        REQUIRE(js != nullptr);
        CHECK(js->ops_total == 250);
        CHECK(js->ops_evaluated == 1); // only the edited record's key
        check_hashes_consistent(g, ex);
    }
    auto vr = verify_edits(program, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Cost model: hash join keeps nested-loop order with duplicate keys") {
    std::string src = R"(
input a: [{ k: string, x: int }] = [{ k: "x", x: 1 }, { k: "y", x: 2 }, { k: "x", x: 3 }];
input b: [{ k: string, y: int }] = [{ k: "x", y: 10 }, { k: "y", y: 20 }, { k: "x", y: 30 }];
let j = join(a, b, l => l.k, r => r.k);
let s = map(j, p => p.left.x * 100 + p.right.y);
output s;
)";
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    std::ostringstream out;
    ex.run(&out);
    // left order, then right order within each left record
    CHECK(out.str() == "s = [110, 130, 220, 310, 330]\n");
}

TEST_CASE("Cost model: join patched in place when keys are kept; re-entering records rebuild") {
    std::ostringstream src;
    src << "input users: [{ uid: int, w: int, on: bool }] = [\n";
    for (int i = 0; i < 20; ++i) {
        src << "  { uid: " << i << ", w: " << i << ", on: true }" << (i < 19 ? ",\n" : "\n");
    }
    src << "];\ninput orders: [{ oid: int, uid: int, amt: int }] = [\n";
    for (int i = 0; i < 60; ++i) {
        src << "  { oid: " << i << ", uid: " << i % 20 << ", amt: " << i << " }" << (i < 59 ? ",\n" : "\n");
    }
    src << "];\n"
           "let live = filter(users, u => u.on);\n"
           "let j = join(live, orders, u => u.uid, o => o.uid);\n"
           "let amts = map(j, p => p.right.amt * p.left.w);\n"
           "let total = reduce(amts, 0, (acc, x) => acc + x);\n"
           "output total;\noutput amts;\n";
    std::string program = src.str();

    Graph g = build_graph(program);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);

    EditScript script;
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("orders", 5, "amt", 500)));  // key kept
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("users", 4, "w", 40)));      // key kept
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("users", 7, "on", false))); // leaves
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("users", 7, "on", true)));  // re-enters
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("users", 7, "w", 70)));      // key kept
    for (size_t i = 0; i < script.edits.size(); ++i) {
        auto plan = std::get<RebuildPlan>(prop.apply_edit(*script.edits[i]));
        const StagePlan* js = find_stage(plan, "j");
        REQUIRE(js != nullptr);
        bool patched = js->note == "(join patched in place)";
        // edits 3 and 4 add or remove a join input record, so they rebuild (BUG-006)
        CHECK(patched == (i != 2 && i != 3));
        const StagePlan* am = find_stage(plan, "amts");
        // a kept key touches only the output records of the edited input record
        if (patched && am) CHECK(am->ops_evaluated <= 3);
        check_hashes_consistent(g, ex);
    }
    auto vr = verify_edits(program, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Regression: stage lookup prefers the stage over its OUTPUT node") {
    // Many stages so that unordered_map iteration order varies; every output
    // shares its stage's name.
    std::string src = make_program(30, R"(
let a = filter(rows, r => r.v > 2);
let w = map(a, r => r.id);
let hi = filter(a, r => r.ok);
let q = reduce(w, 7, (acc, x) => x * 2 + acc);
let mx = reduce(w, 0, (acc, x) => if x > acc then x else acc);
output q;
output mx;
output hi;
output w;
)");
    Graph g = build_graph(src);
    for (const char* name : {"a", "w", "hi", "q", "mx"}) {
        const Node* n = g.find_node_by_name(name);
        REQUIRE(n != nullptr);
        CHECK(n->kind != NodeKind::OUTPUT);
    }

    // insert before an output's source stage, then delete it again
    EditScript script;
    auto ins = std::make_shared<InsertEdit>();
    ins->before_stage = "hi";
    ins->new_stage_name = "pos";
    ins->builtin_name = "filter";
    ins->raw_lambda_text = "x => x.id >= 5";
    script.edits.push_back(ins);
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("rows", 7, "v", 1)));
    auto del = std::make_shared<DeleteEdit>();
    del->stage_name = "pos";
    script.edits.push_back(del);
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
}

TEST_CASE("BUG-001: a lambda that reads another stage sees its new value") {
    // The case from MEMORY.md: verify used to report [25, 8, 9] vs rerun [25, 17, 18]
    std::string src = R"(
input xs: [int] = [1, 2, 3];
let total = reduce(xs, 0, (a, x) => a + x);
let ys = map(xs, x => x + total);
output ys;
)";
    Graph g = build_graph(src);
    const Node* ys = &node_named(g, "ys");
    const Node* total = &node_named(g, "total");
    REQUIRE(ys->inputs.size() == 2);
    CHECK(reads_by_name(*ys, total->id));
    CHECK_FALSE(reads_by_name(*ys, ys->inputs[0]));

    EditScript script;
    auto m = std::make_shared<ModifyEdit>();
    m->input_stage = "xs";
    m->record_id = 0;
    m->new_value = Value::make_int(10);
    m->raw_literal_text = "10";
    script.edits.push_back(m);
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    REQUIRE(vr.rerun_outputs.size() == 1);
    CHECK(vr.rerun_outputs[0].second == "[25, 17, 18]");
    CHECK(vr.propagated_outputs == vr.rerun_outputs);
}

TEST_CASE("BUG-001: filter and length() references, and delete of a referenced stage") {
    std::string src = make_program(40, R"(
let kept = filter(rows, r => r.ok);
let n = length(kept);
let low = filter(rows, r => r.v < n);
let total = reduce(low, 0, (acc, r) => acc + r.v);
output total;
output low;
)");
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);

    EditScript script;
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 0, "ok", true)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 1, "ok", false)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_bool("rows", 3, "ok", true)));
    for (const auto& e : script.edits) {
        REQUIRE(std::holds_alternative<RebuildPlan>(prop.apply_edit(*e)));
        check_hashes_consistent(g, ex);
    }
    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);

    // kept is read by name in n: deleting it must be rejected with no change
    auto before = ex.get_outputs();
    DeleteEdit del;
    del.stage_name = "kept";
    auto r = prop.apply_delete(del);
    REQUIRE(std::holds_alternative<CompileError>(r));
    CHECK(std::get<CompileError>(r).code == ErrorCode::E601);
    CHECK(ex.get_outputs() == before);
}

TEST_CASE("Phase 12.3: parse record insert and delete edits") {
    EditScriptParser parser(
        "retro insert into rows : { id: 9, v: 1, name: \"a b\", ok: true }\n"
        "retro delete rows[3]\n"
        "retro delete kept\n");
    auto res = parser.parse();
    REQUIRE(std::holds_alternative<EditScript>(res));
    const auto& s = std::get<EditScript>(res);
    REQUIRE(s.edits.size() == 3);
    REQUIRE(s.edits[0]->kind() == EditKind::INSERT_RECORD);
    const auto& ins = static_cast<const InsertRecordEdit&>(*s.edits[0]);
    CHECK(ins.input_stage == "rows");
    CHECK(ins.raw_literal_text == "{ id: 9, v: 1, name: \"a b\", ok: true }");
    REQUIRE(s.edits[1]->kind() == EditKind::DELETE_RECORD);
    CHECK(static_cast<const DeleteRecordEdit&>(*s.edits[1]).record_id == 3);
    CHECK(s.edits[2]->kind() == EditKind::DELETE);
}

static std::shared_ptr<InsertRecordEdit> insert_rec(const std::string& input, const std::string& lit) {
    auto e = std::make_shared<InsertRecordEdit>();
    e->input_stage = input;
    e->raw_literal_text = lit;
    return e;
}

static std::shared_ptr<DeleteRecordEdit> delete_rec(const std::string& input, RecordId id) {
    auto e = std::make_shared<DeleteRecordEdit>();
    e->input_stage = input;
    e->record_id = id;
    return e;
}

TEST_CASE("Phase 12.3: record insert and delete propagate and match a rerun") {
    std::string src = make_program(300, R"(
let kept = filter(rows, r => r.ok);
let doubled = map(kept, r => { id: r.id, v: r.v * 2 });
let total = reduce(doubled, 0, (acc, r) => acc + r.v);
let vs = map(doubled, r => r.v);
let tr = tree_reduce(vs, 0, (a, b) => a + b);
output total;
output doubled;
output tr;
)");
    EditScript script;
    script.edits.push_back(delete_rec("rows", 100));
    script.edits.push_back(insert_rec("rows", "{ id: 1000, v: 7, ok: true }"));
    script.edits.push_back(delete_rec("rows", 299));                  // the last record
    script.edits.push_back(insert_rec("rows", "{ id: 1001, v: -5, ok: true }"));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("rows", 200, "v", 1)));
    script.edits.push_back(std::make_shared<ModifyEdit>(modify_int("rows", 301, "v", 9)));

    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);
    for (size_t i = 0; i < script.edits.size(); ++i) {
        auto r = prop.apply_edit(*script.edits[i]);
        REQUIRE(std::holds_alternative<RebuildPlan>(r));
        const StagePlan* d = find_stage(std::get<RebuildPlan>(r), "doubled");
        if (d) CHECK(d->ops_evaluated <= 1);   // one record per edit
        check_hashes_consistent(g, ex);
    }
    // ids are never reused: after deleting 299, the inserts got 300 and 301
    const auto& in = ex.get_result(node_named(g, "rows").id).collection_val;
    CHECK(in.back().id == 301);
    CHECK(in.size() == 300);

    auto vr = verify_edits(src, script);
    CHECK(vr.ok);
    CHECK(ex.get_outputs() == vr.rerun_outputs);
}

TEST_CASE("Phase 12.3: invalid record edits are rejected without changes") {
    std::string src = make_program(10, "let kept = filter(rows, r => r.ok);\noutput kept;\n");
    Graph g = build_graph(src);
    GraphExecutor ex(g, ".", true);
    ex.run();
    Propagator prop(g, ex);
    auto before = ex.get_outputs();
    for (const auto& e : std::vector<std::shared_ptr<Edit>>{
             insert_rec("rows", "{ id: 1, v: 2.5, ok: true }"),    // v is int
             insert_rec("rows", "{ id: 1, ok: true }"),            // missing field
             insert_rec("kept", "{ id: 1, v: 2, ok: true }"),      // not an INPUT
             delete_rec("rows", 10),                                // no such record
         }) {
        auto r = prop.apply_edit(*e);
        REQUIRE(std::holds_alternative<CompileError>(r));
        CHECK(std::get<CompileError>(r).code == ErrorCode::E601);
        CHECK(ex.get_outputs() == before);
    }
}
