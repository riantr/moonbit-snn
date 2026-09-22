// sttc.mbt — Spike-Time Tiling Coefficient analysis example.
//
// Julia reference (SpikingNeuralNetworks.jl/test/analysis/sttc.jl):
// compute STTC between spike trains to measure pairwise correlation.
//
// MoonBit (v0.10.20):
//   - Build 3 synthetic spike trains:
//     * T1: regular spikes at every 100 ms (perfectly periodic)
//     * T2: jittered version of T1 (offset by ±5 ms; high correlation)
//     * T3: independent Poisson-like spike train (no correlation)
//   - Compute STTC matrix (3x3) using the new `sttc_matrix` function.
//   - Print the matrix.
//
// Expected qualitative result:
//   - STTC(T1, T2) ≈ high (near 1.0 if jitter << dt; depends on dt choice)
//   - STTC(T1, T3) ≈ 0 (independent)
//   - Diagonal = 1.0 always

///|
fn main {
  // Three spike trains over [0 ms, 1000 ms].
  let t1 : Array[Float] = [100.0F, 200.0F, 300.0F, 400.0F, 500.0F, 600.0F, 700.0F, 800.0F, 900.0F]
  // T2: jittered version (each spike ±5 ms)
  let t2 : Array[Float] = [102.0F, 197.0F, 305.0F, 396.0F, 504.0F, 598.0F, 703.0F, 798.0F, 902.0F]
  // T3: independent Poisson-like train (jittered uniform)
  let t3 : Array[Float] = [50.0F, 130.0F, 290.0F, 410.0F, 580.0F, 670.0F, 820.0F, 950.0F, 110.0F]
  let trains : Array[Array[Float]] = [t1, t2, t3]
  let dt = 10.0F  // coincidence window (10 ms)
  let istart = 0.0F
  let iend = 1000.0F

  // Compute STTC matrix.
  let mat = @snn_mbt.sttc_matrix(trains, dt, istart, iend)

  println("sttc.mbt: STTC matrix for 3 spike trains (dt=" + dt.to_string() + " ms)")
  println("  interval: [" + istart.to_string() + ", " + iend.to_string() + "] ms")
  println("  T1: regular (100 ms period)")
  println("  T2: jittered T1 (±5 ms)")
  println("  T3: independent Poisson-like")
  println("sttc.mbt: STTC matrix (3x3):")
  for i in 0..<3 {
    let row_str = "  [" +
      mat[i][0].to_string() + ", " +
      mat[i][1].to_string() + ", " +
      mat[i][2].to_string() + "]"
    println(row_str)
  }
  println("")
  println("Note: this exercises the STTC analysis infrastructure built in v0.10.20.")
  println("  STTC ranges in [-1, 1]: -1 = anti-correlated, 0 = independent, +1 = perfectly correlated.")
  println("  Expected: STTC(T1,T2) > 0 (high overlap), STTC(T1,T3) ≈ 0 (independent).")
  println("  Diagonal is always 1.0 (a train is perfectly correlated with itself).")
}