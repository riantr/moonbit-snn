// spatial_test.mbt - port of test/utils/spatial_test.jl.
//
// Three testsets from the Julia original:
//   1. place_populations - random placement
//   2. periodic_distance - scalar
//   3. linear_network - ring weight matrix (default + custom params)
//
// Note: Julia returns Array{Array{Float32}, 2} (a 2D matrix). Our port
// returns a flat row-major Float array to work around MoonBit's parser
// limitation with Array[Array[Float]] initialisation. Index via
// row_major_get(W, n, i, j).

///|
test "place_populations - random placement (E=40, I=10)" {
  let rng = Xoshiro::new(42UL)
  let pops = place_populations_e_i(40, 10, 1.0F, 1.0F, rng)
  // Verify counts
  assert_eq(pops.e.length(), 40)
  assert_eq(pops.i.length(), 10)
  // Verify all points are in [0, 1] x [0, 1]
  let mut k = 0
  while k < pops.e.length() {
    let pt = pops.e[k]
    if pt.x < 0.0F || pt.x > 1.0F {
      fail("E point x out of range")
    }
    if pt.y < 0.0F || pt.y > 1.0F {
      fail("E point y out of range")
    }
    k = k + 1
  }
  k = 0
  while k < pops.i.length() {
    let pt = pops.i[k]
    if pt.x < 0.0F || pt.x > 1.0F {
      fail("I point x out of range")
    }
    if pt.y < 0.0F || pt.y > 1.0F {
      fail("I point y out of range")
    }
    k = k + 1
  }
}

///|
test "periodic_distance_scalar" {
  // |0.1 - 0.9| = 0.8, min(0.8, 1 - 0.8) = 0.2
  let d1 = periodic_distance_scalar(0.1F, 0.9F, 1.0F)
  if (d1 - 0.2F).abs() >= 1.0e-6F {
    fail("d(0.1, 0.9) != 0.2")
  }
  // |0.0 - 0.5| = 0.5, min(0.5, 0.5) = 0.5
  let d2 = periodic_distance_scalar(0.0F, 0.5F, 1.0F)
  if (d2 - 0.5F).abs() >= 1.0e-6F {
    fail("d(0.0, 0.5) != 0.5")
  }
  // |0.0 - 0.0| = 0
  let d3 = periodic_distance_scalar(0.0F, 0.0F, 1.0F)
  if d3 >= 1.0e-6F {
    fail("d(0.0, 0.0) != 0")
  }
  // symmetry: d(p1, p2) == d(p2, p1)
  let d4 = periodic_distance_scalar(0.3F, 0.8F, 1.0F)
  let d5 = periodic_distance_scalar(0.8F, 0.3F, 1.0F)
  if (d4 - d5).abs() >= 1.0e-6F {
    fail("distance not symmetric")
  }
  // never exceeds grid_size/2
  let d_max = periodic_distance_scalar(0.1F, 0.9F, 1.0F)
  if d_max > 0.5F {
    fail("distance exceeds half-period")
  }
}

///|
test "linear_network - default ring (N=20)" {
  let n = 20
  let w = linear_network(n)
  // size: n*n flat array
  assert_eq(w.length(), n * n)
  // diagonal == 0
  let mut i = 0
  while i < n {
    if row_major_get(w, n, i, i) != 0.0F {
      fail("diagonal not zero")
    }
    i = i + 1
  }
  // non-negative
  i = 0
  while i < n {
    let mut j = 0
    while j < n {
      if row_major_get(w, n, i, j) < 0.0F {
        fail("negative weight")
      }
      j = j + 1
    }
    i = i + 1
  }
  // max <= 2.1 (default w_max = 2.0)
  let mut max_w = 0.0F
  i = 0
  while i < n {
    let mut j = 0
    while j < n {
      let v = row_major_get(w, n, i, j)
      if v > max_w {
        max_w = v
      }
      j = j + 1
    }
    i = i + 1
  }
  if max_w > 2.1F {
    fail("max weight > 2.1")
  }
}

///|
test "linear_network_with - custom sigma_w=0.5, w_max=3.0 (N=16)" {
  let n = 16
  let w = linear_network_with(n, 0.5F, 3.0F)
  assert_eq(w.length(), n * n)
  let mut max_w = 0.0F
  let mut i = 0
  while i < n {
    let mut j = 0
    while j < n {
      let v = row_major_get(w, n, i, j)
      if v > max_w {
        max_w = v
      }
      j = j + 1
    }
    i = i + 1
  }
  if max_w > 3.1F {
    fail("custom max weight > 3.1")
  }
}