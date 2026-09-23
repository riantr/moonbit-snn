// autodiff_tape_test.mbt — Tests for tape-based reverse-mode AD.

///|
test "Tape: variable stored at correct memory slot" {
  let tape : Tape[Float] = Tape::new()
  let x0 = tape.variable(2.0F)
  let x1 = tape.variable(5.0F)
  // x0 should be at memory[0], x1 at memory[1].
  match x0 {
    Memory(j) => assert_eq(j, 0)
    Const(_) => fail("x0 should be Memory(0), not Const")
  }
  match x1 {
    Memory(j) => assert_eq(j, 1)
    Const(_) => fail("x1 should be Memory(1), not Const")
  }
  let mem = tape.eval()
  assert_eq(mem.length(), 2)
  assert_eq(mem[0], 2.0F)
  assert_eq(mem[1], 5.0F)
}

///|
test "Tape::diff_backward: linear example from textbook" {
  // f(x0, x1) = -x0 + x0 * x1; expected grads [4, 2, 1, 1, 1].
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, neg, mul, .. } = prims
  let x0 = tape.variable(2.0F)
  let x1 = tape.variable(5.0F)
  let res = add(neg(x0), mul(x0, x1))
  ignore(res)
  let mem = tape.eval()
  assert_eq(mem.length(), 5)
  // mem = [2, 5, -2, 10, 8]
  assert_eq(mem[0], 2.0F)
  assert_eq(mem[1], 5.0F)
  assert_eq(mem[2], -2.0F)
  assert_eq(mem[3], 10.0F)
  assert_eq(mem[4], 8.0F)
  let grads = tape.diff_backward(mem)
  assert_eq(grads[0], 4.0F)
  assert_eq(grads[1], 2.0F)
  assert_eq(grads[2], 1.0F)
  assert_eq(grads[3], 1.0F)
  assert_eq(grads[4], 1.0F)
}

///|
test "Tape::diff_backward: f(x) = x^2 at x = 3 → df/dx = 6" {
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { mul, .. } = prims
  let x = tape.variable(3.0F)
  let x_sq = mul(x, x)
  ignore(x_sq)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // f(x) = x^2, df/dx = 2x = 6 at x = 3.
  if (grads[0] - 6.0F).abs() > 0.0001F {
    fail("df/dx = \{grads[0]}, expected 6.0")
  }
}

///|
test "Tape::diff_backward: f(x) = exp(x) at x = 0 → df/dx = 1" {
  let tape : Tape[Float] = Tape::new()
  let prims = MathPrims::on(tape)
  let { exp, .. } = prims
  let x = tape.variable(0.0F)
  let y = exp(x)
  ignore(y)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // exp(0) = 1, dex/dx = exp(0) = 1.
  if (grads[0] - 1.0F).abs() > 0.0001F {
    fail("df/dx = \{grads[0]}, expected 1.0")
  }
}

///|
test "Tape::diff_backward: f(x) = ln(x) at x = 2 → df/dx = 0.5" {
  let tape : Tape[Float] = Tape::new()
  let prims = MathPrims::on(tape)
  let { ln, .. } = prims
  let x = tape.variable(2.0F)
  let y = ln(x)
  ignore(y)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // d ln(x) / dx = 1 / x = 0.5 at x = 2.
  if (grads[0] - 0.5F).abs() > 0.0001F {
    fail("df/dx = \{grads[0]}, expected 0.5")
  }
}

///|
test "Tape::diff_backward: f(x) = tanh(x) at x = 0 → df/dx = 1" {
  let tape : Tape[Float] = Tape::new()
  let prims = MathPrims::on(tape)
  let { tanh, .. } = prims
  let x = tape.variable(0.0F)
  let y = tanh(x)
  ignore(y)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // d tanh(x) / dx = 1 - tanh^2(x) = 1 - 0 = 1.
  if (grads[0] - 1.0F).abs() > 0.0001F {
    fail("df/dx = \{grads[0]}, expected 1.0")
  }
}

///|
test "Tape::diff_backward: f(x) = sigmoid(x) at x = 0 → df/dx = 0.25" {
  let tape : Tape[Float] = Tape::new()
  let prims = MathPrims::on(tape)
  let { sigmoid, .. } = prims
  let x = tape.variable(0.0F)
  let y = sigmoid(x)
  ignore(y)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // sigmoid(0) = 0.5; d sigmoid / dx = s · (1 - s) = 0.25.
  if (grads[0] - 0.25F).abs() > 0.001F {
    fail("df/dx = \{grads[0]}, expected ~0.25")
  }
}

///|
test "Tape::diff_backward: chain rule (a+b)*c" {
  // f(a, b, c) = (a + b) * c. df/da = c, df/db = c, df/dc = a + b.
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, mul, .. } = prims
  let a = tape.variable(2.0F)
  let b = tape.variable(3.0F)
  let c = tape.variable(4.0F)
  let ab = add(a, b)
  let res = mul(ab, c)
  ignore(res)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // x0=a, x1=b, x2=c, x3=a+b=5, x4=(a+b)*c=20
  if (grads[0] - 4.0F).abs() > 0.0001F {
    fail("df/da = \{grads[0]}, expected 4")
  }
  if (grads[1] - 4.0F).abs() > 0.0001F {
    fail("df/db = \{grads[1]}, expected 4")
  }
  if (grads[2] - 5.0F).abs() > 0.0001F {
    fail("df/dc = \{grads[2]}, expected 5")
  }
}

///|
test "Tape::diff_backward: constants don't receive gradient" {
  // f(x) = x + 3. The gradient w.r.t. x should be 1; 3 is constant.
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, .. } = prims
  let x = tape.variable(5.0F)
  let y = add(x, constant(3.0F))
  ignore(y)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // x0=5, x1=8; df/dx = 1, df/d(constant) = 1 but doesn't propagate
  // since it's a Loc::Const.
  assert_eq(grads[0], 1.0F)
  // The output (x1) has gradient 1.
  assert_eq(grads[1], 1.0F)
}

///|
test "Tape::diff_backward: division gradient" {
  // f(x) = 6 / x. df/dx = -6 / x^2 = -6/9 = -0.666... at x = 3.
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { div, .. } = prims
  let six = constant(6.0F)
  let x = tape.variable(3.0F)
  let y = div(six, x)
  ignore(y)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  // inst[0] = Var(x=3), inst[1] = Div. mem[0] = 3, mem[1] = 2.
  // df/d(x) = grads[0] = bwd_rhs(1, 6, 3) = -1 * 6 / 9 = -2/3.
  if (grads[0] - (-2.0F / 3.0F)).abs() > 0.001F {
    fail("df/dx = \{grads[0]}, expected -0.6667")
  }
}

///|
test "Tape::diff_forward: linear example w.r.t. x0" {
  // Same f = -x0 + x0*x1. df/dx0 = -1 + x1 = -1 + 5 = 4 at x0=2, x1=5.
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, neg, mul, .. } = prims
  let x0 = tape.variable(2.0F)
  let x1 = tape.variable(5.0F)
  let res = add(neg(x0), mul(x0, x1))
  ignore(res)
  let mem = tape.eval()
  let df_dx0 = tape.diff_forward(mem, wrt=0)
  // x0:1, x1:0, x2=neg(x0):-1, x3=x0*x1:5, x4=x2+x3:4
  assert_eq(df_dx0[0], 1.0F)
  assert_eq(df_dx0[1], 0.0F)
  assert_eq(df_dx0[2], -1.0F)
  assert_eq(df_dx0[3], 5.0F)
  assert_eq(df_dx0[4], 4.0F)
}

///|
test "Tape::diff_forward: linear example w.r.t. x1" {
  // df/dx1 = 0 + x0 = 2.
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, neg, mul, .. } = prims
  let x0 = tape.variable(2.0F)
  let x1 = tape.variable(5.0F)
  let res = add(neg(x0), mul(x0, x1))
  ignore(res)
  let mem = tape.eval()
  let df_dx1 = tape.diff_forward(mem, wrt=1)
  assert_eq(df_dx1[0], 0.0F)
  assert_eq(df_dx1[1], 1.0F)
  assert_eq(df_dx1[2], 0.0F)
  assert_eq(df_dx1[3], 2.0F)
  assert_eq(df_dx1[4], 2.0F)
}

///|
test "Tape::eval: empty tape returns empty mem" {
  let tape : Tape[Float] = Tape::new()
  let mem = tape.eval()
  assert_eq(mem.length(), 0)
}

///|
test "constant: literal value not in memory" {
  // A constant doesn't take up a memory slot.
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, .. } = prims
  let x = tape.variable(2.0F)
  let y = add(x, constant(3.0F))
  ignore(y)
  let mem = tape.eval()
  // x0=2, x1=5; constants don't get a slot.
  assert_eq(mem.length(), 2)
  assert_eq(mem[0], 2.0F)
  assert_eq(mem[1], 5.0F)
}