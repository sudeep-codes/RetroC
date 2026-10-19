#include "doctest.h"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "ir/builder.hpp"
#include "ir/dump.hpp"
#include <sstream>

using namespace retroc;

static Graph build_graph_from_source(const std::string& src) {
    Lexer lexer(src);
    auto lex_res = lexer.tokenize();
    REQUIRE(std::holds_alternative<std::vector<Token>>(lex_res));
    Parser parser(std::get<std::vector<Token>>(lex_res));
    auto parse_res = parser.parse();
    REQUIRE(std::holds_alternative<Program>(parse_res));
    SemanticAnalyzer sema(std::get<Program>(std::move(parse_res)));
    auto sema_res = sema.analyze();
    REQUIRE(std::holds_alternative<TypedProgram>(sema_res));
    const auto& typed_prog = std::get<TypedProgram>(sema_res);
    IRBuilder builder(typed_prog);
    return builder.build();
}

static std::string dump_graph_str(const Graph& graph, bool verbose = false) {
    std::ostringstream ss;
    dump_ir(graph, ss, verbose);
    return ss.str();
}

TEST_CASE("IR: Build and Dump Ex1-Ex4") {
    SUBCASE("Ex1: filter, map, reduce, outputs") {
        std::string src =
            "input xs: [int] = [1, 2, 3, 4, 5, 6];\n"
            "let evens = filter(xs, x => x % 2 == 0);\n"
            "let sq = map(evens, x => x * x);\n"
            "let total = reduce(sq, 0, (a, x) => a + x);\n"
            "output sq;\n"
            "output total;\n";
        Graph g = build_graph_from_source(src);
        g.assert_invariants();

        CHECK(g.nodes.size() == 6);
        CHECK(g.outputs.size() == 2);
        CHECK(g.order.size() == 6);

        std::string dump = dump_graph_str(g);
        std::string expected =
            "n0 INPUT  xs    : [int]\n"
            "n1 FILTER evens : [int] <- n0  fn: x => x % 2 == 0\n"
            "n2 MAP    sq    : [int] <- n1  fn: x => x * x\n"
            "n3 REDUCE total : int <- n2  init: 0  fn: (a, x) => a + x\n"
            "n4 OUTPUT sq    : [int] <- n2\n"
            "n5 OUTPUT total : int <- n3\n";
        CHECK(dump == expected);
    }

    SUBCASE("Ex2: records, scalar stage, canonical IR dump") {
        std::string src =
            "input rows: [{ id: int, v: float, ok: bool }] = [\n"
            "  { id: 1, v: 2.0, ok: true },\n"
            "  { id: 2, v: 5.0, ok: false },\n"
            "  { id: 3, v: 4.0, ok: true }\n"
            "];\n"
            "let clean = filter(rows, r => r.ok);\n"
            "let vals = map(clean, r => r.v);\n"
            "let sum = reduce(vals, 0.0, (a, x) => a + x);\n"
            "let avg = sum / to_float(length(vals));\n"
            "output avg;\n";
        Graph g = build_graph_from_source(src);
        g.assert_invariants();

        std::string dump = dump_graph_str(g);
        std::string expected =
            "n0 INPUT  rows  : [{id:int,v:float,ok:bool}]\n"
            "n1 FILTER clean : [{id:int,v:float,ok:bool}] <- n0  fn: r => r.ok\n"
            "n2 MAP    vals  : [float] <- n1  fn: r => r.v\n"
            "n3 REDUCE sum   : float <- n2  init: 0.0  fn: (a, x) => a + x\n"
            "n4 SCALAR avg   : float <- n3,n2  expr: sum / to_float(length(vals))\n"
            "n5 OUTPUT avg   : float <- n4\n";
        CHECK(dump == expected);
    }

    SUBCASE("Ex3: join and map field access") {
        std::string src =
            "input users: [{ uid: int, name: string }] = [{ uid: 1, name: \"ana\" }, { uid: 2, name: \"bo\" }];\n"
            "input orders: [{ oid: int, uid: int, amt: int }] = [\n"
            "  { oid: 10, uid: 1, amt: 5 }, { oid: 11, uid: 1, amt: 7 }, { oid: 12, uid: 2, amt: 3 }\n"
            "];\n"
            "let joined = join(users, orders, u => u.uid, o => o.uid);\n"
            "let amts = map(joined, j => j.right.amt);\n"
            "output amts;\n";
        Graph g = build_graph_from_source(src);
        g.assert_invariants();

        std::string dump = dump_graph_str(g);
        std::string expected =
            "n0 INPUT  users : [{uid:int,name:string}]\n"
            "n1 INPUT  orders: [{oid:int,uid:int,amt:int}]\n"
            "n2 JOIN   joined: [{left:{uid:int,name:string},right:{oid:int,uid:int,amt:int}}] <- n0,n1  ka: u => u.uid  kb: o => o.uid\n"
            "n3 MAP    amts  : [int] <- n2  fn: j => j.right.amt\n"
            "n4 OUTPUT amts  : [int] <- n3\n";
        CHECK(dump == expected);
    }

    SUBCASE("Ex4: user fn inlining") {
        std::string src =
            "fn clamp01(x: float) -> float = if x < 0.0 then 0.0 else if x > 1.0 then 1.0 else x;\n"
            "input xs: [float] = [-0.5, 0.25, 1.5];\n"
            "let ys = map(xs, x => clamp01(x));\n"
            "output ys;\n";
        Graph g = build_graph_from_source(src);
        g.assert_invariants();

        std::string dump = dump_graph_str(g);
        std::string expected =
            "n0 INPUT  xs    : [float]\n"
            "n1 MAP    ys    : [float] <- n0  fn: x => if x < 0.0 then 0.0 else if x > 1.0 then 1.0 else x\n"
            "n2 OUTPUT ys    : [float] <- n1\n";
        CHECK(dump == expected);
    }
}

TEST_CASE("IR: Nested Builtin Splitting (s$1, s$2)") {
    SUBCASE("Nested map(filter(xs, f), g)") {
        std::string src =
            "input xs: [int] = [1, 2, 3, 4];\n"
            "let res = map(filter(xs, x => x > 2), x => x * 10);\n"
            "output res;\n";
        Graph g = build_graph_from_source(src);
        g.assert_invariants();

        // Expect:
        // n0 INPUT  xs
        // n1 FILTER s$1 <- n0
        // n2 MAP    res <- n1
        // n3 OUTPUT res <- n2
        CHECK(g.nodes.size() == 4);
        const Node* s1 = g.find_node_by_name("s$1");
        REQUIRE(s1 != nullptr);
        CHECK(s1->kind == NodeKind::FILTER);
        CHECK(s1->inputs.size() == 1);
        CHECK(s1->inputs[0] == "n0");

        const Node* res_node = g.find_node_by_name("res");
        REQUIRE(res_node != nullptr);
        CHECK(res_node->kind == NodeKind::MAP);
        CHECK(res_node->inputs.size() == 1);
        CHECK(res_node->inputs[0] == s1->id);

        std::string dump = dump_graph_str(g);
        std::string expected =
            "n0 INPUT  xs    : [int]\n"
            "n1 FILTER s$1   : [int] <- n0  fn: x => x > 2\n"
            "n2 MAP    res   : [int] <- n1  fn: x => x * 10\n"
            "n3 OUTPUT res   : [int] <- n2\n";
        CHECK(dump == expected);
    }

    SUBCASE("Double nested builtins") {
        std::string src =
            "input xs: [int] = [1, 2, 3, 4, 5];\n"
            "let res = reduce(map(filter(xs, x => x % 2 == 1), x => x * 2), 0, (a, x) => a + x);\n"
            "output res;\n";
        Graph g = build_graph_from_source(src);
        g.assert_invariants();

        // Expect s$2 for filter, s$1 for map, res for reduce
        CHECK(g.nodes.size() == 5);
        std::string dump = dump_graph_str(g);
        std::string expected =
            "n0 INPUT  xs    : [int]\n"
            "n1 FILTER s$2   : [int] <- n0  fn: x => x % 2 == 1\n"
            "n2 MAP    s$1   : [int] <- n1  fn: x => x * 2\n"
            "n3 REDUCE res   : int <- n2  init: 0  fn: (a, x) => a + x\n"
            "n4 OUTPUT res   : int <- n3\n";
        CHECK(dump == expected);
    }
}

TEST_CASE("IR: Invariants I1 and I2 Verification") {
    SUBCASE("Unique NodeIds and Topological Order Assertions") {
        std::string src =
            "input a: int = 1;\n"
            "let b = a + 1;\n"
            "let c = a + b;\n"
            "output c;\n";
        Graph g = build_graph_from_source(src);
        // Will assert I1 and I2 without throwing / failing
        g.assert_invariants();
        CHECK(g.order[0] == "n0");
        CHECK(g.order[1] == "n1");
        CHECK(g.order[2] == "n2");
        CHECK(g.order[3] == "n3");
    }
}
