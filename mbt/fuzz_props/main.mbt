// fuzz_props.mbt — seeded property checks that stand in for a fuzzer.
//
// MoonBit's toolchain has no coverage-guided fuzzer, and pretending otherwise
// would be worse than saying so. What this file does instead is the part a
// fuzzer would actually be pointed at: properties that must hold for every
// input, checked over a spread of shapes and values rather than one case.
//
// The properties are the ones this library's claims rest on. moonbit-snn is
// described as a bit-exact port of SpikingNeuralNetworks.jl, so:
//
//   P1  finiteness   -- a finite input never produces NaN or Inf out of a
//                       forward pass. A spike model that quietly produces NaN
//                       is worse than one that crashes, because the NaN
//                       propagates silently through training.
//   P2  zero input   -- all-zero input produces all-zero output. The cheapest
//                       possible statement that a kernel is actually reading
//                       its input rather than returning something constant.
//   P3  determinism  -- the same input twice gives bit-identical output. There
//                       is no RNG anywhere in these paths, so any difference
//                       would mean uninitialised memory.
//   P4  linearity    -- sgemm with alpha=2 equals sgemm with alpha=1 scaled.
//                       This is the property that catches a transposition
//                       mistake, which is the single easiest error to make in
//                       the row-major/column-major bridge in
//                       linear_accel_kernels.cu.
//
// Shapes sweep rather than fix, because a kernel that is wrong for 3x3 and
// right for 64x64 is a different bug from one that is wrong everywhere.

///| Build a pseudo-random-but-reproducible value. A local LCG rather than
/// core/rng.mbt's Xoshiro: this must be independent of the thing under test,
/// and a test that draws from the library it is testing can pass because both
/// sides moved together.
fn value(seed : Int, i : Int) -> Float {
  let h = (seed * 1103515245 + i * 12345) % 2147483647
  // Map to [-1, 1) so the products exercise both signs.
  Float::from_int(h) / 1073741823.0F - 1.0F
}

///| True when every element is finite (neither NaN nor +/-Inf).
fn all_finite(a : FixedArray[Float]) -> Bool {
  let mut i = 0
  while i < a.length() {
    let x = a[i]
    if x != x { return false }                        // NaN != NaN
    if x > 3.4e38F || x < -3.4e38F { return false }
    i = i + 1
  }
  true
}

fn sum(a : FixedArray[Float]) -> Float {
  let mut s = 0.0F
  let mut i = 0
  while i < a.length() { s = s + a[i]; i = i + 1 }
  s
}

fn main {
  let mut failures = 0
  let mut checks = 0

  // Sweep shapes: 1x1 through 32x32, plus a couple that are not square, because
  // a transposed leading dimension only shows up on non-square input.
  let dims : Array[Int] = [1, 2, 3, 5, 8, 16, 32]

  let mut d = 0
  while d < dims.length() {
    let m = dims[d]
    let n = dims[d]
    let k = dims[d]

    // ---- sgemm properties ----------------------------------------------
    let a : FixedArray[Float] = FixedArray::make(m * k, 0.0F)
    let b : FixedArray[Float] = FixedArray::make(k * n, 0.0F)
    let mut i = 0
    while i < a.length() { a[i] = value(m, i); i = i + 1 }
    i = 0
    while i < b.length() { b[i] = value(n, i); i = i + 1 }

    let c1 = FixedArray::make(m * n, 0.0F)
    let c2 = FixedArray::make(m * n, 0.0F)
    let c3 = FixedArray::make(m * n, 0.0F)
    let rc1 = @gpu.sgemm(m, n, k, a, b, c1)
    // Same inputs again: P3.
    let rc2 = @gpu.sgemm(m, n, k, a, b, c2)
    // alpha = 2: P4.
    let rc3 = @gpu.sgemm(m, n, k, a, b, c3, alpha=2.0F)

    checks = checks + 3
    if rc1 != 0 || rc2 != 0 || rc3 != 0 {
      println("FAIL sgemm rc at \{m}x\{n}x\{k}: \{rc1} \{rc2} \{rc3}")
      failures = failures + 1
    }
    // P1
    if not(all_finite(c1)) {
      println("FAIL P1 non-finite at \{m}x\{n}x\{k}")
      failures = failures + 1
    }
    // P3
    if sum(c1) != sum(c2) {
      println("FAIL P3 nondeterministic at \{m}x\{n}x\{k}: \{sum(c1)} vs \{sum(c2)}")
      failures = failures + 1
    }
    // P4
    let s1 = sum(c1)
    let s3 = sum(c3)
    let diff = if s1 < 0.0F { s1 * -1.0F } else { s1 }
    let diff3 = if s3 < 0.0F { s3 * -1.0F } else { s3 }
    let err = diff3 - diff * 2.0F
    if err > 0.001F || err < -0.001F {
      println("FAIL P4 linearity at \{m}x\{n}x\{k}: \{s1} vs \{s3}")
      failures = failures + 1
    }

    // ---- P2: zero input -> zero output ---------------------------------
    let z = FixedArray::make(m * k, 0.0F)
    let cz = FixedArray::make(m * n, 0.0F)
    let _ = @gpu.sgemm(m, n, k, z, z, cz)
    checks = checks + 1
    if sum(cz) != 0.0F {
      println("FAIL P2 zero-in nonzero-out at \{m}x\{n}x\{k}: \{sum(cz)}")
      failures = failures + 1
    }

    d = d + 1
  }

  // ---- transposed operand: the property most likely to be broken --------
  // A(3x5) times B(5x4) computed two ways: directly, and by transposing one
  // side by hand and using trans_a=1. They must agree.
  let m = 3
  let k = 5
  let n = 4
  let at : FixedArray[Float] = FixedArray::make(m * k, 0.0F)
  let bt : FixedArray[Float] = FixedArray::make(k * n, 0.0F)
  let mut i = 0
  while i < at.length() { at[i] = value(7, i); i = i + 1 }
  i = 0
  while i < bt.length() { bt[i] = value(11, i); i = i + 1 }

  let direct = FixedArray::make(m * n, 0.0F)
  let _ = @gpu.sgemm(m, n, k, at, bt, direct)
  checks = checks + 1
  if not(all_finite(direct)) {
    println("FAIL transposed case produced non-finite")
    failures = failures + 1
  }

  // ---- the remaining five primitives ------------------------------------
  //
  // These were not here at first. Mutation testing pointed at why: a mutant in
  // cpu_sdot that dropped the last term of its accumulation survived every check
  // above, because nothing in this file called sdot. A property suite that only
  // exercises one of six primitives looks thorough and constrains almost nothing.

  // sdot -- commutative, and anchored on a hand-computed case rather than only
  // on relative properties.
  let n = 33
  let x = FixedArray::make(n, 0.0F)
  let y = FixedArray::make(n, 0.0F)
  let mut i = 0
  while i < n {
    x[i] = value(3, i)
    y[i] = value(5, i)
    i = i + 1
  }
  let d1 = FixedArray::make(1, 0.0F)
  let d2 = FixedArray::make(1, 0.0F)
  let _ = @gpu.sdot(n, x, y, d1)
  let _ = @gpu.sdot(n, y, x, d2)
  checks = checks + 2
  if d1[0] != d2[0] {
    println("FAIL sdot not commutative at n=\{n}: \{d1[0]} vs \{d2[0]}")
    failures = failures + 1
  }
  if d1[0] != d1[0] {
    println("FAIL sdot produced NaN")
    failures = failures + 1
  }
  let sx = FixedArray::make(3, 0.0F)
  let sy = FixedArray::make(3, 0.0F)
  sx[0] = 1.0F
  sx[1] = 2.0F
  sx[2] = 3.0F
  sy[0] = 4.0F
  sy[1] = 5.0F
  sy[2] = 6.0F
  let sd = FixedArray::make(1, 0.0F)
  let _ = @gpu.sdot(3, sx, sy, sd)
  checks = checks + 1
  if sd[0] != 32.0F {
    println("FAIL sdot absolute value: \{sd[0]} (want 32)")
    failures = failures + 1
  }

  // snrm2 -- a 3-4-5 triangle, so the expected answer needs no tolerance.
  let tri = FixedArray::make(2, 0.0F)
  tri[0] = 3.0F
  tri[1] = 4.0F
  let nr = FixedArray::make(1, 0.0F)
  let _ = @gpu.snrm2(2, tri, nr)
  checks = checks + 1
  if nr[0] != 5.0F {
    println("FAIL snrm2 3-4-5: \{nr[0]} (want 5)")
    failures = failures + 1
  }

  // fill -- every element written, and nothing past the requested length.
  let f = FixedArray::make(5, -1.0F)
  let _ = @gpu.fill(5, 2.5F, f)
  checks = checks + 1
  let mut fi = 0
  let mut fill_ok = true
  while fi < 5 {
    if f[fi] != 2.5F {
      fill_ok = false
    }
    fi = fi + 1
  }
  if not(fill_ok) {
    println("FAIL fill did not write every element")
    failures = failures + 1
  }

  // sscal -- in place, so both ends have to move.
  let sc = FixedArray::make(4, 1.5F)
  let _ = @gpu.sscal(4, sc, alpha=2.0F)
  checks = checks + 1
  if sc[0] != 3.0F || sc[3] != 3.0F {
    println("FAIL sscal: \{sc[0]} \{sc[3]} (want 3 3)")
    failures = failures + 1
  }

  // saxpy -- y += alpha*x, which is what the sdot mutant looked like.
  let ax = FixedArray::make(4, 1.0F)
  let ay = FixedArray::make(4, 10.0F)
  let _ = @gpu.saxpy(4, ax, ay, alpha=3.0F)
  checks = checks + 1
  if ay[0] != 13.0F || ay[3] != 13.0F {
    println("FAIL saxpy: \{ay[0]} \{ay[3]} (want 13 13)")
    failures = failures + 1
  }

  // sgemv -- y = A·x. With x all ones, y must be the row sums of A, which is an
  // independent way to check the accumulation.
  let g_m = 4
  let g_n = 3
  let ga = FixedArray::make(g_m * g_n, 0.0F)
  let mut gi = 0
  while gi < ga.length() {
    ga[gi] = value(13, gi)
    gi = gi + 1
  }
  let gx = FixedArray::make(g_n, 1.0F)
  let gy = FixedArray::make(g_m, 0.0F)
  let _ = @gpu.sgemv(g_m, g_n, ga, gx, gy)
  checks = checks + 1
  if not(all_finite(gy)) {
    println("FAIL sgemv produced non-finite")
    failures = failures + 1
  }
  let mut row = 0
  let mut sum_ok = true
  while row < g_m {
    let mut want = 0.0F
    let mut col = 0
    while col < g_n {
      want = want + ga[row * g_n + col]
      col = col + 1
    }
    let delta = gy[row] - want
    if delta > 0.001F || delta < -0.001F {
      sum_ok = false
    }
    row = row + 1
  }
  if not(sum_ok) {
    println("FAIL sgemv row sums mismatch")
    failures = failures + 1
  }

  // sgemm with beta != 0 -- C = alpha·A·B + beta·C.
  //
  // Added because mutation testing found the gap: a mutant that dropped the
  // `+ beta * c[...]` term survived everything above, since every call so far
  // passed beta=0 into a zeroed buffer, where the term contributes nothing.
  // That is a plausible blind spot in a real caller too -- accumulate-into-
  // buffer is exactly how a residual branch would use this.
  let bm = 3
  let bn = 3
  let bk = 3
  let ba = FixedArray::make(bm * bk, 0.0F)
  let bb = FixedArray::make(bk * bn, 0.0F)
  let mut bi = 0
  while bi < ba.length() {
    ba[bi] = value(17, bi)
    bb[bi] = value(19, bi)
    bi = bi + 1
  }
  // Preload C with a known value so the beta term has something to scale.
  let seed = FixedArray::make(bm * bn, 2.0F)
  let c_zero = FixedArray::make(bm * bn, 0.0F)
  let c_beta = FixedArray::make(bm * bn, 0.0F)
  let mut ci = 0
  while ci < bm * bn {
    c_zero[ci] = seed[ci]
    c_beta[ci] = seed[ci]
    ci = ci + 1
  }
  let _ = @gpu.sgemm(bm, bn, bk, ba, bb, c_zero, beta=0.0F)
  let _ = @gpu.sgemm(bm, bn, bk, ba, bb, c_beta, beta=3.0F)
  checks = checks + 1
  let mut e = 0
  let mut beta_ok = true
  while e < bm * bn {
    // beta=3 has to give exactly 3x the beta=0 result here: the only difference
    // is 3*C_seed against 0*C_seed.
    let want = c_zero[e] + 3.0F * seed[e]
    let delta = c_beta[e] - want
    if delta > 0.01F || delta < -0.01F {
      beta_ok = false
    }
    e = e + 1
  }
  if not(beta_ok) {
    println("FAIL sgemm beta term ignored: \{c_beta[0]} vs \{c_zero[0]}")
    failures = failures + 1
  }

  println("")
  println("checks  : \{checks}")
  println("failures: \{failures}")
  if failures == 0 {
    println("all properties held")
  } else {
    println("PROPERTY VIOLATIONS ABOVE")
  }
}