// main.mbt -- exercises gpu/'s wrappers against known-answer cases.
//
// There is no CUDA library on this machine, so every call takes the stub's CPU
// route. That is the case worth having covered: the accelerated path cannot be
// checked without a card, and the fallback has to be right everywhere,
// CI included.
//
// Output buffers are real FixedArray values rather than Arrays wrapped on the
// way out. FixedArray::from_array is a view, and a write the C side performs
// through it does not land in the Array it came from -- reads see it, writes do
// not. See README.md.

fn main {
  println("try_load   : \{@gpu.try_load()}  (0 = found, -1000 = absent)")
  println("loaded     : \{@gpu.loaded()}")

  // sgemm, trans_a = 0. A is 2x3, B is 3x2, so C is 2x2.
  // A = [[1,2,3],[4,5,6]]  B = [[7,8],[9,10],[11,12]]
  // C = [[58,64],[139,154]]
  let a : FixedArray[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F]
  let b : FixedArray[Float] = [7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F]
  let c : FixedArray[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let rc0 = @gpu.sgemm(2, 2, 3, a, b, c)
  println("sgemm rc   : \{rc0}")
  println("sgemm out  : \{c[0]} \{c[1]} | \{c[2]} \{c[3]}   (want 58 64 | 139 154)")

  // sdot
  let x : FixedArray[Float] = [1.0F, 2.0F, 3.0F]
  let y : FixedArray[Float] = [4.0F, 5.0F, 6.0F]
  let dot : FixedArray[Float] = [0.0F]
  println("sdot rc    : \{@gpu.sdot(3, x, y, dot)}")
  println("sdot out   : \{dot[0]}   (want 32)")

  // snrm2 of [3,4] is 5
  let v : FixedArray[Float] = [3.0F, 4.0F]
  let nrm : FixedArray[Float] = [0.0F]
  println("snrm2 rc   : \{@gpu.snrm2(2, v, nrm)}")
  println("snrm2 out  : \{nrm[0]}   (want 5)")

  // fill
  let buf : FixedArray[Float] = [0.0F, 0.0F, 0.0F]
  println("fill rc    : \{@gpu.fill(3, 7.0F, buf)}")
  println("fill out   : \{buf[0]} \{buf[1]} \{buf[2]}   (want 7 7 7)")

  // axpy: y += 2*x
  let p : FixedArray[Float] = [1.0F, 2.0F, 3.0F]
  let q : FixedArray[Float] = [10.0F, 20.0F, 30.0F]
  println("axpy rc    : \{@gpu.saxpy(3, p, q, alpha=2.0F)}")
  println("axpy out   : \{q[0]} \{q[1]} \{q[2]}   (want 12 24 36)")

  // A bad argument has to be reported rather than silently absorbed.
  println("bad-arg rc : \{@gpu.sdot(-1, x, y, dot)}   (want -2000)")
}
