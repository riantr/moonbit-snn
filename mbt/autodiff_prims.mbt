// autodiff_prims.mbt — Primitive operations on `Tape[A]` (v0.31.0).
//
// The primitive operations are split into two bundles to avoid forcing
// a single type signature across generic and Float-only operations:
//
//   - `BasicPrims[A]` — generic in `A : Neg + Add + Sub + Mul + Div +
//     Diffable`. Provides `neg`, `add`, `sub`, `mul`, `div`.
//   - `MathPrims` — Float-specialised. Provides `exp`, `ln`, `sin`,
//     `cos`, `tanh`, `sigmoid` via libm FFI (`expf`, `logf`, etc.).
//
// Both bundles wrap a shared `Tape[A]`; the basic bundle can be used
// alone if transcendental ops are not needed.
//
// Backward-function conventions (matching `Tape::op1` / `Tape::op2`):
//   - Unary `bwd(g, x)` returns `g · df/dx` at the input `x`.
//   - Binary `bwd_lhs(g, a, b)` returns `g · df/dlhs` at `(a, b)`;
//     `bwd_rhs(g, a, b)` returns `g · df/drhs` at `(a, b)`. Both
//     receive BOTH operands so partial derivatives can depend on the
//     partner (e.g. `df/db = -a/b²` for `a / b`).

///|
/// Generic algebraic primitive ops.
pub struct BasicPrims[A] {
  tape : Tape[A]
  neg : (Loc[A]) -> Loc[A]
  add : (Loc[A], Loc[A]) -> Loc[A]
  sub : (Loc[A], Loc[A]) -> Loc[A]
  mul : (Loc[A], Loc[A]) -> Loc[A]
  div : (Loc[A], Loc[A]) -> Loc[A]
}

///|
/// Build the `BasicPrims` bundle around the given tape.
pub fn[A : Neg + Add + Sub + Mul + Div] BasicPrims::on(
  tape : Tape[A],
) -> BasicPrims[A] {
  // neg(x) = -x; bwd(g, _) = -g
  let neg = tape.op1("neg", fn(a) { -a }, fn(g, _x) { -g })
  // add(a, b) = a + b; df/dlhs = 1, df/drhs = 1
  let add = tape.op2(
    "+", fn(a, b) { a + b }, fn(g, _a, _b) { g }, fn(g, _a, _b) { g },
  )
  // sub(a, b) = a - b; df/dlhs = 1, df/drhs = -1
  let sub = tape.op2(
    "-", fn(a, b) { a - b }, fn(g, _a, _b) { g }, fn(g, _a, _b) { -g },
  )
  // mul(a, b) = a * b; df/dlhs = b, df/drhs = a
  let mul = tape.op2(
    "*", fn(a, b) { a * b }, fn(g, _a, b) { g * b }, fn(g, a, _b) { g * a },
  )
  // div(a, b) = a / b; df/dlhs = 1/b, df/drhs = -a/b²
  let div = tape.op2(
    "/",
    fn(a, b) { a / b },
    fn(g, _a, b) { g / b },
    fn(g, a, b) { -g * a / (b * b) },
  )
  { tape, neg, add, sub, mul, div }
}

///|
/// Float-only transcendental primitive ops.
pub struct MathPrims {
  tape : Tape[Float]
  exp : (Loc[Float]) -> Loc[Float]
  ln : (Loc[Float]) -> Loc[Float]
  sin : (Loc[Float]) -> Loc[Float]
  cos : (Loc[Float]) -> Loc[Float]
  tanh : (Loc[Float]) -> Loc[Float]
  sigmoid : (Loc[Float]) -> Loc[Float]
}

///|
/// Build the `MathPrims` bundle around the given Float tape.
pub fn MathPrims::on(tape : Tape[Float]) -> MathPrims {
  // exp(x) = e^x; bwd(g, x) = g * e^x = g * expf(x)
  let exp = tape.op1("exp", fn(a) { expf(a) }, fn(g, x) { g * expf(x) })
  // ln(x); bwd(g, x) = g / x
  let ln = tape.op1("ln", fn(a) { logf(a) }, fn(g, x) { g / x })
  // sin(x); bwd(g, x) = g * cos(x)
  let sin = tape.op1("sin", fn(a) { sinf(a) }, fn(g, x) { g * cosf(x) })
  // cos(x); bwd(g, x) = -g * sin(x)
  let cos = tape.op1("cos", fn(a) { cosf(a) }, fn(g, x) { -g * sinf(x) })
  // tanh(x); bwd(g, x) = g * (1 - tanh^2(x))
  let tanh = tape.op1("tanh", fn(a) { tanhf(a) }, fn(g, x) {
    let t = tanhf(x)
    g * (1.0F - t * t)
  })
  // sigmoid(x) = 1 / (1 + exp(-x)); bwd(g, x) = g * s * (1 - s)
  let sigmoid = tape.op1("sigmoid", fn(a) { sigmoid_f32(a) }, fn(g, x) {
    let s = sigmoid_f32(x)
    g * s * (1.0F - s)
  })
  { tape, exp, ln, sin, cos, tanh, sigmoid }
}