// autodiff_tape.mbt — Tape-based reverse-mode automatic differentiation (v0.31.0).
//
// Provides a `Tape[A]` recorder that captures a sequence of arithmetic
// operations during the forward pass, then computes gradients via a
// single reverse traversal. Supports both forward-mode (derivative of
// every intermediate w.r.t. one input) and backward-mode (gradient of
// one scalar output w.r.t. all inputs).
//
// API design follows moonbit-pilot/autodiff:
//   - `Loc[A] = Const(A) | Memory(Int)` — pointers to constant values
//     (no gradient) or memory slots produced by earlier instructions.
//   - `Inst[A] = Var(A) | Unary(...) | Binary(...)` — recorded ops.
//   - `Diffable` trait requires `Add + Sub` with `zero`/`one` literals.
//   - `BasicPrims[A]` wraps a Tape with algebraic ops
//     (`neg`, `add`, `sub`, `mul`, `div`).
//   - `MathPrims` wraps a `Tape[Float]` with transcendental ops
//     (`exp`, `ln`, `sin`, `cos`, `tanh`, `sigmoid`).
//
// Typical usage:
//   let tape : Tape[Float] = Tape::new()
//   let prims = BasicPrims::on(tape)
//   let x0 = tape.variable(2.0F)
//   let x1 = tape.variable(5.0F)
//   let res = prims.add(prims.neg(x0), prims.mul(x0, x1))
//   let mem = tape.eval()                              // forward pass
//   let grads = tape.diff_backward(mem)                // ∂res/∂x for all x
//
// Convention for backward functions:
//   - op1: `bwd(g, x)` returns `g · df/dx` evaluated at the input `x`.
//   - op2: `bwd_lhs(g, a, b)` returns `g · df/dlhs` at `(a, b)`;
//          `bwd_rhs(g, a, b)` returns `g · df/drhs` at `(a, b)`.

///|
/// Reference to a value in the tape — either a literal constant
/// (no gradient propagation) or a memory slot produced by an earlier
/// instruction.
pub enum Loc[A] {
  Const(A)
  Memory(Int)
}

///|
/// Recorded instruction in the tape. `Var` stores a literal value;
/// `Unary` and `Binary` capture operations with their forward and
/// backward callbacks.
pub enum Inst[A] {
  /// A variable (input) — the constant value is stored directly so
  /// `eval` can place it at the corresponding memory slot. Backward
  /// does not propagate to the input (variables are leaves).
  Var(A)
  /// A unary operation. `fwd` maps the input to the output;
  /// `bwd` maps `(g, original_input)` to the input's gradient
  /// (`g · df/dx` at the input).
  Unary((A) -> A, (A, A) -> A, Loc[A])
  /// A binary operation. `fwd` maps `(lhs, rhs)` to the output;
  /// `bwd_lhs(g, lhs, rhs)` returns the lhs gradient
  /// (`g · df/dlhs` at `(lhs, rhs)`); `bwd_rhs(g, lhs, rhs)` returns
  /// the rhs gradient (`g · df/drhs` at `(lhs, rhs)`).
  Binary((A, A) -> A, (A, A, A) -> A, (A, A, A) -> A, Loc[A], Loc[A])
}

///|
/// A linear sequence of instructions recorded during the forward
/// pass. Use `Tape::variable` to declare inputs, `Tape::op1`/`op2` to
/// record operations, `Tape::eval` to run forward, and
/// `Tape::diff_backward` (or `diff_forward`) to compute gradients.
pub struct Tape[A] {
  insts : Array[Inst[A]]
  names : Array[String]
}

///|
/// Trait required for any type that can be used with the tape.
/// Pre-implemented for `Float` (Float32) and `Double` (Float64).
pub(open) trait Diffable : Add + Sub {
  zero() -> Self
  one() -> Self
}

pub impl Diffable for Float with zero() {
  0.0F
}

pub impl Diffable for Float with one() {
  1.0F
}

pub impl Diffable for Double with zero() {
  0.0
}

pub impl Diffable for Double with one() {
  1.0
}

///|
/// Create an empty tape. All subsequent operations are appended to
/// this tape in order.
pub fn[A] Tape::new() -> Tape[A] {
  { insts: [], names: [] }
}

///|
/// Append a variable with the given value to the tape. Returns the
/// memory slot where the variable's value will be stored. Variables
/// are leaves of the computation graph — they receive gradient in
/// `diff_backward` but do not propagate further.
pub fn[A] Tape::variable(self : Tape[A], value : A) -> Loc[A] {
  let idx = self.insts.length()
  self.insts.push(Inst::Var(value))
  self.names.push("x\{idx}")
  Loc::Memory(idx)
}

///|
/// Wrap a literal value as a `Loc::Const`. Constants do not receive
/// gradient in backward mode — they are not in the parameter list.
pub fn[A] constant(value : A) -> Loc[A] {
  Loc::Const(value)
}

///|
/// Append a unary operation to the tape. Returns a function that
/// takes a `Loc[A]` input and produces a `Loc[A]` for the output.
///
/// `bwd : (A, A) -> A` takes the upstream gradient and the original
/// input value and returns the input's gradient
/// (`g · df/dx` evaluated at the input).
pub fn[A] Tape::op1(
  self : Tape[A],
  name : String,
  fwd : (A) -> A,
  bwd : (A, A) -> A,
) -> (Loc[A]) -> Loc[A] {
  fn(input : Loc[A]) -> Loc[A] {
    let idx = self.insts.length()
    self.insts.push(Inst::Unary(fwd, bwd, input))
    self.names.push(name)
    Loc::Memory(idx)
  }
}

///|
/// Append a binary operation to the tape. Returns a function that
/// takes two `Loc[A]` inputs and produces a `Loc[A]` for the output.
///
/// `bwd_lhs(g, lhs, rhs)` returns the lhs gradient
/// (`g · df/dlhs` at `(lhs, rhs)`); `bwd_rhs(g, lhs, rhs)` returns the
/// rhs gradient. Both functions receive BOTH original operands so
/// they can implement partial derivatives that depend on the partner
/// (e.g. `df/db = -a/b²` for `a / b`).
pub fn[A] Tape::op2(
  self : Tape[A],
  name : String,
  fwd : (A, A) -> A,
  bwd_lhs : (A, A, A) -> A,
  bwd_rhs : (A, A, A) -> A,
) -> (Loc[A], Loc[A]) -> Loc[A] {
  fn(lhs : Loc[A], rhs : Loc[A]) -> Loc[A] {
    let idx = self.insts.length()
    self.insts.push(Inst::Binary(fwd, bwd_lhs, bwd_rhs, lhs, rhs))
    self.names.push(name)
    Loc::Memory(idx)
  }
}

///|
/// Resolve a `Loc[A]` to its concrete value at runtime.
fn[A] resolve_loc(loc : Loc[A], mem : Array[A]) -> A {
  match loc {
    Const(c) => c
    Memory(j) => mem[j]
  }
}

///|
/// Run the forward pass, evaluating every instruction and writing its
/// result into the corresponding memory slot. Returns an array of
/// length `insts.length()` where `mem[i]` is the value of the i-th
/// instruction (variables first, then operations in order).
pub fn[A : Diffable] Tape::eval(self : Tape[A]) -> Array[A] {
  let n = self.insts.length()
  let mem : Array[A] = Array::make(n, A::zero())
  for i in 0..<n {
    match self.insts[i] {
      Var(v) => mem[i] = v
      Unary(fwd, _bwd, input) => mem[i] = fwd(resolve_loc(input, mem))
      Binary(fwd, _blhs, _brhs, lhs, rhs) =>
        mem[i] = fwd(resolve_loc(lhs, mem), resolve_loc(rhs, mem))
    }
  }
  mem
}

///|
/// Compute backward-mode gradients. Returns an array `grads` where
/// `grads[i]` is `∂output / ∂inst[i]` — i.e. the partial derivative
/// of the last instruction (assumed to be the scalar output) with
/// respect to the i-th instruction. The gradient of the output w.r.t.
/// itself is seeded to `A::one()`.
pub fn[A : Diffable] Tape::diff_backward(
  self : Tape[A],
  mem : Array[A],
) -> Array[A] {
  let n = self.insts.length()
  let grads : Array[A] = Array::make(n, A::zero())
  if n == 0 {
    return grads
  }
  // dL/d(output) = 1
  grads[n - 1] = A::one()
  // Reverse traversal — propagate gradient from each instruction
  // back to its inputs.
  for k in 0..<n {
    let idx = n - 1 - k
    let g : A = grads[idx]
    match self.insts[idx] {
      Var(_) => () // variables are leaves; no further propagation
      Unary(_fwd, bwd, input) => {
        let x : A = resolve_loc(input, mem)
        match input {
          Memory(j) => {
            let g_in : A = bwd(g, x)
            grads[j] = grads[j] + g_in
          }
          Const(_) => ()
        }
      }
      Binary(_fwd, bwd_lhs, bwd_rhs, lhs, rhs) => {
        let a : A = resolve_loc(lhs, mem)
        let b : A = resolve_loc(rhs, mem)
        match lhs {
          Memory(j) => {
            let g_in : A = bwd_lhs(g, a, b)
            grads[j] = grads[j] + g_in
          }
          Const(_) => ()
        }
        match rhs {
          Memory(j) => {
            let g_in : A = bwd_rhs(g, a, b)
            grads[j] = grads[j] + g_in
          }
          Const(_) => ()
        }
      }
    }
  }
  grads
}

///|
/// Compute forward-mode derivative of every intermediate value with
/// respect to the variable at index `wrt`. Returns an array
/// `df[i] = ∂inst[i] / ∂inst[wrt]`. The seed `df[wrt] = 1`.
///
/// **Caveat**: the backward functions for our primitive ops expect
/// the *upstream gradient* as the first argument. To re-use them in
/// forward mode, we substitute `1` for the gradient — this gives the
/// **local derivative at the input** (since `bwd(1, x) = df/dx` for
/// our convention where `bwd(g, x) = g · df/dx`).
pub fn[A : Diffable + Mul + Add] Tape::diff_forward(
  self : Tape[A],
  mem : Array[A],
  wrt~ : Int = 0,
) -> Array[A] {
  let n = self.insts.length()
  let df : Array[A] = Array::make(n, A::zero())
  if n > wrt {
    df[wrt] = A::one()
  }
  for i in 0..<n {
    match self.insts[i] {
      Var(_) => () // leaves — seed is already there
      Unary(_fwd, bwd, input) => {
        let x : A = resolve_loc(input, mem)
        let d_in : A = match input {
          Memory(j) => df[j]
          Const(_) => A::zero()
        }
        // Local derivative at the input: bwd(1, x) = df/dx.
        let local_d : A = bwd(A::one(), x)
        df[i] = local_d * d_in
      }
      Binary(_fwd, bwd_lhs, bwd_rhs, lhs, rhs) => {
        let a : A = resolve_loc(lhs, mem)
        let b : A = resolve_loc(rhs, mem)
        let da : A = match lhs {
          Memory(j) => df[j]
          Const(_) => A::zero()
        }
        let db : A = match rhs {
          Memory(j) => df[j]
          Const(_) => A::zero()
        }
        let local_lhs : A = bwd_lhs(A::one(), a, b)
        let local_rhs : A = bwd_rhs(A::one(), a, b)
        df[i] = local_lhs * da + local_rhs * db
      }
    }
  }
  df
}