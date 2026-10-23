// bench_lin/main.mbt -- the release-mode timing harness for the hot kernel.
//
// WHY THIS EXISTS. The `perf` gate used to grep a COMMENT out of
// `gpu/moon.pkg` for the string "98.4 ms" and report PASS. It measured
// nothing: the number was prose, and the budget it printed was never
// compared against anything. A gate that cannot produce a verdict is not a
// gate. This package is what makes the verdict possible.
//
// WHAT IS MEASURED. `linear_forward` at the LeNet5 fc1 shape -- batch 64,
// 4000 -> 500 -- which is 128 million multiply-accumulates, or 256 MFLOP,
// per call. It is the kernel the `gpu/` seam was designed around, and its
// cost is what decides whether a GPU dispatch is worth writing.
//
// HOW, and why each part is there.
//
//   * The inputs are deterministic (an integer hash of the index), so the
//     work cannot be simplified away and two runs measure the same thing.
//
//   * `env.now()` is read around the CALLS only. Building the arrays and
//     starting the process are outside the timed region, so there is no
//     fixed overhead inside it to subtract.
//
//   * One warm-up call precedes the timed ones. The first call is the one
//     that pays for the page faults on the working set -- 1 MB of input and
//     8 MB of weight -- and charging that to the kernel would make the
//     number depend on how many pages the process happened to fault in.
//
//   * Every output element is summed. A release build is allowed to
//     eliminate a call whose result is unused, and the kernel writes 32000
//     floats; if only three of them were read, an inlining optimiser could
//     drop the other 31997 multiplies and the measurement would be of
//     nothing. Summing all of them costs 32000 adds against 128 million
//     multiplies -- about 0.03% -- and removes the question.
//
// The repeat count and the raw delta are both printed, and the division is
// left to the caller, so the unit of `env.now()` is a fact the gate
// establishes rather than an assumption baked into the number here.
//
//   moon run --release ./bench_lin

///|
fn main {
  let n = 64
  let in_f = 4000
  let out_f = 500
  let reps = 20

  // Deterministic inputs: no RNG, so the benchmark is reproducible and the
  // compiler cannot fold the arithmetic into a constant.
  let input = Array::make(n * in_f, 0.0F)
  let mut i = 0
  while i < n * in_f {
    input[i] = Float::from_int(i % 17) * 0.01F - 0.08F
    i = i + 1
  }
  let weight = Array::make(out_f * in_f, 0.0F)
  let mut j = 0
  while j < out_f * in_f {
    weight[j] = Float::from_int(j % 23) * 0.005F - 0.05F
    j = j + 1
  }
  let bias = Array::make(out_f, 0.0F)
  let param = @nn.LinearParam::new(weight, bias, in_f, out_f)

  // The warm-up is not timed, and its result is used, so it cannot be
  // eliminated either.
  let warm = @nn.linear_forward(input, n, param)
  let mut acc = warm[0]

  let t0 = @env.now()
  for _ in 0..<reps {
    let out = @nn.linear_forward(input, n, param)
    let mut k = 0
    while k < out.length() {
      acc = acc + out[k]
      k = k + 1
    }
  }
  let t1 = @env.now()

  println("bench_delta: " + (t1 - t0).to_string())
  println("bench_reps: " + reps.to_string())
  println("bench_checksum: " + acc.to_string())
}
