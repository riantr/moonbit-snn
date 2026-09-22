// spatial.mbt — spatial utilities demo.
//
// Mirrors the place_populations + periodic_distance + linear_network
// sections of refs/SNNModels.jl/test/utils/spatial_test.jl.
//
// This example prints:
//   1. A few sampled 2D positions in a periodic grid.
//   2. A few periodic-distance checks on a 1D torus.
//   3. The default ring weight matrix dimensions and max weight.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()

  // 1. place_populations_e_i: 40 E + 10 I on a [0, 1]^2 periodic grid.
  let pops = @snn_mbt.place_populations_e_i(40, 10, 1.0F, 1.0F, rng)
  println("place_populations_e_i(40, 10, [0,1]^2):")
  println("  E[0]  = (" + pops.e[0].x.to_string() + ", " + pops.e[0].y.to_string() + ")")
  println("  E[5]  = (" + pops.e[5].x.to_string() + ", " + pops.e[5].y.to_string() + ")")
  println("  I[0]  = (" + pops.i[0].x.to_string() + ", " + pops.i[0].y.to_string() + ")")
  println("  E count: " + pops.e.length().to_string())
  println("  I count: " + pops.i.length().to_string())

  // 2. periodic_distance_scalar: 1D torus checks.
  println("periodic_distance_scalar:")
  let d1 = @snn_mbt.periodic_distance_scalar(0.1F, 0.9F, 1.0F)
  let d2 = @snn_mbt.periodic_distance_scalar(0.0F, 0.5F, 1.0F)
  let d3 = @snn_mbt.periodic_distance_scalar(0.0F, 0.0F, 1.0F)
  println("  d(0.1, 0.9) on grid_size=1.0 = " + d1.to_string())
  println("  d(0.0, 0.5) on grid_size=1.0 = " + d2.to_string())
  println("  d(0.0, 0.0) on grid_size=1.0 = " + d3.to_string())

  // 3. linear_network_with: default ring (sigma_w=0.38, w_max=2.0) for N=20.
  let n = 20
  let w = @snn_mbt.linear_network(n)
  println("linear_network(N=20):")
  println("  flat length: " + w.length().to_string() + " (expected " + (n * n).to_string() + ")")
  let mut max_w = 0.0F
  let mut i = 0
  while i < n {
    let mut j = 0
    while j < n {
      let v = @snn_mbt.row_major_get(w, n, i, j)
      if v > max_w {
        max_w = v
      }
      j = j + 1
    }
    i = i + 1
  }
  println("  max weight: " + max_w.to_string() + " (should be <= 2.1)")
  println("  diagonal [0,0] = " + @snn_mbt.row_major_get(w, n, 0, 0).to_string() + " (expected 0.0)")
  println("  diagonal [5,5] = " + @snn_mbt.row_major_get(w, n, 5, 5).to_string() + " (expected 0.0)")
  println("  off-diagonal [0, 1] = " + @snn_mbt.row_major_get(w, n, 0, 1).to_string() + " (close to w_max)")

  // 4. linear_network_with custom: sigma_w=0.5, w_max=3.0, N=16.
  let n2 = 16
  let w2 = @snn_mbt.linear_network_with(n2, 0.5F, 3.0F)
  println("linear_network_with(N=16, sigma_w=0.5, w_max=3.0):")
  println("  flat length: " + w2.length().to_string() + " (expected " + (n2 * n2).to_string() + ")")
  let mut max_w2 = 0.0F
  i = 0
  while i < n2 {
    let mut j = 0
    while j < n2 {
      let v = @snn_mbt.row_major_get(w2, n2, i, j)
      if v > max_w2 {
        max_w2 = v
      }
      j = j + 1
    }
    i = i + 1
  }
  println("  max weight: " + max_w2.to_string() + " (should be <= 3.1)")
}