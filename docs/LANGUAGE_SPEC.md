# LANGUAGE_SPEC — RetroC mini-language (`.rc`)

Status: v1, frozen after Phase 0. A pure, statically typed dataflow language. Every top-level `input` and `let` is a **stage** (a named node in the IR). Edits refer to stages by name.

## 1. Lexical rules
- Whitespace separates tokens. Comments: `// to end of line`.
- Keywords: `input let fn output if then else true false and or not from int float bool string`.
- IDENT: `[A-Za-z_][A-Za-z0-9_]*` (not a keyword). `_` alone is reserved for edit scripts.
- INT: `[0-9]+`. FLOAT: `[0-9]+\.[0-9]+`. STRING: double quotes, escapes `\" \\ \n \t`.
- Operators/punctuation: `+ - * / % == != < <= > >= => -> = . , ; : ( ) [ ] { }`

## 2. Grammar (EBNF)
```
program     = { item } EOF ;
item        = input_decl | fn_decl | let_decl | output_decl ;
input_decl  = "input" IDENT ":" type ( "=" expr | "from" STRING ) ";" ;
fn_decl     = "fn" IDENT "(" [ param { "," param } ] ")" "->" type "=" expr ";" ;
param       = IDENT ":" type ;
let_decl    = "let" IDENT [ ":" type ] "=" expr ";" ;
output_decl = "output" IDENT ";" ;

type        = "int" | "float" | "bool" | "string"
            | "[" type "]"
            | "{" field_ty { "," field_ty } "}" ;
field_ty    = IDENT ":" type ;

expr        = lambda | if_expr | or_expr ;
lambda      = ( IDENT | "(" IDENT { "," IDENT } ")" ) "=>" expr ;
if_expr     = "if" expr "then" expr "else" expr ;
or_expr     = and_expr { "or" and_expr } ;
and_expr    = not_expr { "and" not_expr } ;
not_expr    = "not" not_expr | cmp_expr ;
cmp_expr    = add_expr [ ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) add_expr ] ;
add_expr    = mul_expr { ( "+" | "-" ) mul_expr } ;
mul_expr    = unary { ( "*" | "/" | "%" ) unary } ;
unary       = "-" unary | postfix ;
postfix     = primary { "." IDENT | "(" [ expr { "," expr } ] ")" } ;
primary     = INT | FLOAT | STRING | "true" | "false" | IDENT
            | "(" expr ")" | array_lit | record_lit ;
array_lit   = "[" [ expr { "," expr } ] "]" ;
record_lit  = "{" IDENT ":" expr { "," IDENT ":" expr } "}" ;
```
Parser note: `(a, b) => e` vs a parenthesized expression is decided by lookahead (scan to the matching `)` and check for `=>`).

## 3. Types
- Base: `int` (64-bit signed), `float` (IEEE double), `bool`, `string`.
- Collections `[T]`: ordered sequences. Records `{ f1: T1, ... }`: field order is irrelevant for typing, canonical (declaration) order for printing.
- No implicit conversions. Use `to_float`, `to_int` (truncates toward zero).
- `+` works on int, float, string (concatenation). `- * /` on int or float (both operands same type). `%` on int. Comparisons on int, float, string; `== !=` also on bool and records.
- Type inference: `let` infers from the right side. Lambda parameter types are inferred from the builtin they are passed to. Lambdas are NOT first-class: they may appear only as arguments to builtins.

## 4. Built-ins (all pure)
| Builtin | Type | Meaning |
|---|---|---|
| `filter(c, f)` | `[T], (T)->bool -> [T]` | keep elements where f is true; order kept |
| `map(c, f)` | `[T], (T)->U -> [U]` | apply f per element; order kept |
| `reduce(c, init, f)` | `[T], A, (A,T)->A -> A` | left fold in order |
| `join(a, b, ka, kb)` | `[A], [B], (A)->K, (B)->K -> [{left: A, right: B}]` | equi-join; output ordered by left order, then right order |
| `length(c)` | `[T] -> int` | number of elements |
| `to_float(x)` / `to_int(x)` | `int->float` / `float->int` | conversion |
| `abs(x)`, `min(a,b)`, `max(a,b)` | numeric | as named |
| `clamp(x, lo, hi)` | numeric | `min(max(x, lo), hi)` |
Join key type K must be int, string, or bool.

## 5. Semantics
- Evaluation is by value, side-effect free. A stage's value depends only on the values of stages it references.
- `fn` bodies are single expressions; no recursion (direct or mutual): error E402.
- Scoping: stages are visible to later items only (no forward references). Lambda params shadow nothing: reusing a stage name as a lambda param is error E403.
- `input x: T = expr;` gives inline data. `input x: T from "file.csv";` loads a CSV with a header row matching the record fields (flat records of base types only).
- Record identity: each input element gets a stable `RecordId` = 0-based position at load time. MAP/FILTER keep it. JOIN outputs use the pair `(leftId, rightId)`.
- `output x;` prints `x = <value>` on one line (formats below). Outputs print in source order.
- Runtime errors (integer divide/modulo by zero, int overflow is wraparound, `to_int` of NaN/inf) abort with code E501. Float division by zero yields inf/nan per IEEE.

## 6. Print format (deterministic)
- int: decimal. float: shortest round-trip decimal, always with a `.` (e.g. `3.0`, `0.25`). bool: `true`/`false`. string: double-quoted with escapes.
- Array: `[a, b, c]`. Record: `{f1: v1, f2: v2}` (declaration order).

## 7. Error codes
| Code | Meaning |
|---|---|
| E101 | unterminated string / bad escape |
| E102 | unexpected character |
| E201 | syntax error (expected X, found Y) |
| E301 | type mismatch |
| E302 | unknown field |
| E303 | wrong arity / argument type for builtin |
| E304 | lambda used outside a builtin argument |
| E401 | undefined name |
| E402 | recursion not allowed |
| E403 | duplicate or shadowing name |
| E501 | runtime error |
| E601 | invalid edit (see EDIT_SEMANTICS.md) |

## 8. Example programs with expected output (use as golden tests)

### Ex1 — filter / map / reduce
```
input xs: [int] = [1, 2, 3, 4, 5, 6];
let evens = filter(xs, x => x % 2 == 0);
let sq = map(evens, x => x * x);
let total = reduce(sq, 0, (a, x) => a + x);
output sq;
output total;
```
Expected:
```
sq = [4, 16, 36]
total = 56
```

### Ex2 — records and scalar stage
```
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
```
Expected: `avg = 3.0`

### Ex3 — join
```
input users: [{ uid: int, name: string }] = [{ uid: 1, name: "ana" }, { uid: 2, name: "bo" }];
input orders: [{ oid: int, uid: int, amt: int }] = [
  { oid: 10, uid: 1, amt: 5 }, { oid: 11, uid: 1, amt: 7 }, { oid: 12, uid: 2, amt: 3 }
];
let joined = join(users, orders, u => u.uid, o => o.uid);
let amts = map(joined, j => j.right.amt);
output amts;
```
Expected: `amts = [5, 7, 3]`

### Ex4 — fn and if-expression
```
fn clamp01(x: float) -> float = if x < 0.0 then 0.0 else if x > 1.0 then 1.0 else x;
input xs: [float] = [-0.5, 0.25, 1.5];
let ys = map(xs, x => clamp01(x));
output ys;
```
Expected: `ys = [0.0, 0.25, 1.0]`

### Ex5 — type error (expected: E301 at line 2)
```
input xs: [int] = [1, 2];
let a = map(xs, x => x + 1.0);
```

### Ex6 — undefined name (expected: E401 at line 1)
```
let a = map(missing, x => x);
```

### Ex7 — recursion rejected (expected: E402)
```
fn f(x: int) -> int = f(x - 1);
```

## 9. Out of scope for v1 (do not implement)
`for`/`while`, user-defined generics, first-class functions, indexing `a[i]`, mutation, modules, `sort`, `groupby`. (Stretch ideas live in PHASES.md Phase 12.)
