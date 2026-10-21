#include "doctest.h"
#include "common/value.hpp"
#include "bytecode/bytecode.hpp"
#include "runtime/executor.hpp"
#include "runtime/csv.hpp"
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

TEST_CASE("Value representation and hashing") {
    Value i1 = Value::make_int(42);
    Value i2 = Value::make_int(42);
    Value i3 = Value::make_int(43);
    CHECK(i1 == i2);
    CHECK(i1 != i3);
    CHECK(i1.hash() == i2.hash());
    CHECK(i1.hash() != i3.hash());

    Value f1 = Value::make_float(3.0);
    Value f2 = Value::make_float(3.0);
    CHECK(f1 == f2);
    CHECK(f1.to_string() == "3.0");

    Value f3 = Value::make_float(0.25);
    CHECK(f3.to_string() == "0.25");

    Value b1 = Value::make_bool(true);
    CHECK(b1.to_string() == "true");

    Value s1 = Value::make_string("hello\nworld");
    CHECK(s1.to_string() == "\"hello\\nworld\"");

    Value arr = Value::make_array({Value::make_int(1), Value::make_int(2)});
    CHECK(arr.to_string() == "[1, 2]");

    Value rec = Value::make_record({
        {"id", std::make_shared<Value>(Value::make_int(1))},
        {"name", std::make_shared<Value>(Value::make_string("ana"))}
    });
    CHECK(rec.to_string() == "{id: 1, name: \"ana\"}");
}

TEST_CASE("Ex1 — filter / map / reduce") {
    std::string src = R"(
input xs: [int] = [1, 2, 3, 4, 5, 6];
let evens = filter(xs, x => x % 2 == 0);
let sq = map(evens, x => x * x);
let total = reduce(sq, 0, (a, x) => a + x);
output sq;
output total;
)";
    Graph graph = parse_and_build(src);
    GraphExecutor executor(graph);
    std::ostringstream ss;
    executor.run(&ss);

    std::string expected = "sq = [4, 16, 36]\ntotal = 56\n";
    CHECK(ss.str() == expected);
}

TEST_CASE("Ex2 — records and scalar stage") {
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
    Graph graph = parse_and_build(src);
    GraphExecutor executor(graph);
    std::ostringstream ss;
    executor.run(&ss);

    std::string expected = "avg = 3.0\n";
    CHECK(ss.str() == expected);
}

TEST_CASE("Ex3 — join") {
    std::string src = R"(
input users: [{ uid: int, name: string }] = [{ uid: 1, name: "ana" }, { uid: 2, name: "bo" }];
input orders: [{ oid: int, uid: int, amt: int }] = [
  { oid: 10, uid: 1, amt: 5 }, { oid: 11, uid: 1, amt: 7 }, { oid: 12, uid: 2, amt: 3 }
];
let joined = join(users, orders, u => u.uid, o => o.uid);
let amts = map(joined, j => j.right.amt);
output amts;
)";
    Graph graph = parse_and_build(src);
    GraphExecutor executor(graph);
    std::ostringstream ss;
    executor.run(&ss);

    std::string expected = "amts = [5, 7, 3]\n";
    CHECK(ss.str() == expected);
}

TEST_CASE("Ex4 — fn and if-expression") {
    std::string src = R"(
fn clamp01(x: float) -> float = if x < 0.0 then 0.0 else if x > 1.0 then 1.0 else x;
input xs: [float] = [-0.5, 0.25, 1.5];
let ys = map(xs, x => clamp01(x));
output ys;
)";
    Graph graph = parse_and_build(src);
    GraphExecutor executor(graph);
    std::ostringstream ss;
    executor.run(&ss);

    std::string expected = "ys = [0.0, 0.25, 1.0]\n";
    CHECK(ss.str() == expected);
}

TEST_CASE("Runtime error E501: division and modulo by zero") {
    std::string div_zero_src = R"(
input xs: [int] = [1, 2, 3];
let ys = map(xs, x => x / 0);
output ys;
)";
    Graph g1 = parse_and_build(div_zero_src);
    GraphExecutor ex1(g1);
    CHECK_THROWS_AS(ex1.run(), RuntimeError);

    std::string mod_zero_src = R"(
input xs: [int] = [1, 2, 3];
let ys = map(xs, x => x % 0);
output ys;
)";
    Graph g2 = parse_and_build(mod_zero_src);
    GraphExecutor ex2(g2);
    CHECK_THROWS_AS(ex2.run(), RuntimeError);
}

TEST_CASE("Runtime error E501: to_int on NaN / infinity") {
    std::string nan_src = R"(
let x = to_int(0.0 / 0.0);
output x;
)";
    Graph g1 = parse_and_build(nan_src);
    GraphExecutor ex1(g1);
    CHECK_THROWS_AS(ex1.run(), RuntimeError);
}

TEST_CASE("Empty collections evaluation") {
    std::string empty_src = R"(
input xs: [int] = [];
let f = filter(xs, x => x > 0);
let m = map(f, x => x * 2);
let total = reduce(m, 100, (a, x) => a + x);
output m;
output total;
)";
    Graph g = parse_and_build(empty_src);
    GraphExecutor ex(g);
    std::ostringstream ss;
    ex.run(&ss);
    std::string expected = "m = []\ntotal = 100\n";
    CHECK(ss.str() == expected);
}

TEST_CASE("Determinism test") {
    std::string src = R"(
input xs: [int] = [5, 2, 8, 1, 9];
let sorted_like = map(xs, x => x * 3);
output sorted_like;
)";
    Graph g = parse_and_build(src);
    
    std::ostringstream ss1;
    GraphExecutor ex1(g);
    ex1.run(&ss1);

    std::ostringstream ss2;
    GraphExecutor ex2(g);
    ex2.run(&ss2);

    CHECK(ss1.str() == ss2.str());
}

TEST_CASE("Phase 6: Invariant I8 (empty memo equals memoized run)") {
    std::string src = R"(
input xs: [int] = [1, 2, 3, 4, 5, 6];
let evens = filter(xs, x => x % 2 == 0);
let sq = map(evens, x => x * x);
let total = reduce(sq, 0, (a, x) => a + x);
output sq;
output total;
)";
    // 1. Run with empty memo (enable_memo = false)
    Graph g1 = parse_and_build(src);
    GraphExecutor ex_empty(g1, ".", /*enable_memo=*/false);
    std::ostringstream ss_empty;
    ex_empty.run(&ss_empty);

    // 2. Run with memo (enable_memo = true)
    Graph g2 = parse_and_build(src);
    GraphExecutor ex_memo(g2, ".", /*enable_memo=*/true);
    std::ostringstream ss_memo;
    ex_memo.run(&ss_memo);

    // I8: Outputs are unaffected by memo state
    CHECK(ss_empty.str() == ss_memo.str());

    // 3. Second run with populated memo gives exact same outputs
    std::ostringstream ss_memo2;
    ex_memo.run(&ss_memo2);
    CHECK(ss_memo2.str() == ss_empty.str());

    // Check that ops were reused on the second run!
    const auto& history = ex_memo.get_history();
    REQUIRE(history.entries.size() == 2);
    CHECK(history.entries[0].ops_recomputed > 0);
    CHECK(history.entries[1].ops_reused > 0);
}

TEST_CASE("Phase 6: Per-node memo sizes and versions") {
    std::string src = R"(
input users: [{ uid: int, name: string }] = [{ uid: 1, name: "ana" }, { uid: 2, name: "bo" }];
input orders: [{ oid: int, uid: int, amt: int }] = [
  { oid: 10, uid: 1, amt: 5 }, { oid: 11, uid: 1, amt: 7 }, { oid: 12, uid: 2, amt: 3 }
];
let joined = join(users, orders, u => u.uid, o => o.uid);
let amts = map(joined, j => j.right.amt);
let total = reduce(amts, 0, (a, x) => a + x);
output total;
)";
    Graph g = parse_and_build(src);
    GraphExecutor executor(g, ".", /*enable_memo=*/true);
    executor.run();

    // Check memo sizes
    const auto* joined_node = g.find_node_by_name("joined");
    REQUIRE(joined_node != nullptr);
    CHECK(joined_node->memo.join_memo.size() == 6); // 2 users * 3 orders
    CHECK(joined_node->memo.size() == 6);
    CHECK(joined_node->version == 1);
    CHECK(joined_node->outHash != 0);

    const auto* map_node = g.find_node_by_name("amts");
    REQUIRE(map_node != nullptr);
    CHECK(map_node->memo.map_memo.size() == 3); // 3 matched items
    CHECK(map_node->memo.size() == 3);
    CHECK(map_node->version == 1);

    const auto* reduce_node = g.find_node_by_name("total");
    REQUIRE(reduce_node != nullptr);
    CHECK(reduce_node->memo.reduce_memo.valid == true);
    CHECK(reduce_node->memo.size() == 1);
    CHECK(reduce_node->version == 1);

    // Check memory accounting
    size_t total_bytes = executor.total_memo_bytes();
    CHECK(total_bytes > 0);
    CHECK(executor.get_history().total_memory_bytes() == total_bytes);
}
