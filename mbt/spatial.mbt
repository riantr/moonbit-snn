// spatial.mbt - spatial utility port.

///|
pub(all) struct Point2D {
  x : Float
  y : Float
}

///|
pub(all) struct PlacedPops {
  e : Array[Point2D]
  i : Array[Point2D]
}

///|
pub fn place_populations_e_i(
  n_e : Int,
  n_i : Int,
  grid_x : Float,
  grid_y : Float,
  rng : Xoshiro,
) -> PlacedPops {
  let e_arr : Array[Point2D] = Array::make(n_e, { x: 0.0F, y: 0.0F })
  let i_arr : Array[Point2D] = Array::make(n_i, { x: 0.0F, y: 0.0F })
  let mut k = 0
  while k < n_e {
    let rx = next_f32(rng) * grid_x
    let ry = next_f32(rng) * grid_y
    e_arr[k] = { x: rx, y: ry }
    k = k + 1
  }
  k = 0
  while k < n_i {
    let rx = next_f32(rng) * grid_x
    let ry = next_f32(rng) * grid_y
    i_arr[k] = { x: rx, y: ry }
    k = k + 1
  }
  { e: e_arr, i: i_arr }
}

///|
pub fn periodic_distance_scalar(p1 : Float, p2 : Float, grid_size : Float) -> Float {
  let d = if p1 > p2 {
    p1 - p2
  } else {
    p2 - p1
  }
  let complement = grid_size - d
  if d < complement {
    d
  } else {
    complement
  }
}

///|
pub fn linear_network(n : Int) -> Array[Float] {
  linear_network_with(n, 0.38F, 2.0F)
}

///|
pub fn linear_network_with(
  n : Int,
  sigma_w : Float,
  w_max : Float,
) -> Array[Float] {
  let two_pi : Float = Float::from_double(6.283185307179586)
  let total = n * n
  let w_arr : Array[Float] = Array::make(total, 0.0F)
  let positions : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    positions[i] = two_pi * Float::from_int(i) / Float::from_int(n)
    i = i + 1
  }
  i = 0
  while i < n {
    let mut j = 0
    while j < n {
      let mut diff = positions[j] - positions[i]
      if diff < 0.0F {
        diff = -diff
      }
      let two_pi_minus_diff = two_pi - diff
      let d_min = if diff < two_pi_minus_diff {
        diff
      } else {
        two_pi_minus_diff
      }
      let two_sigma_sq = 2.0F * sigma_w * sigma_w
      let exponent = -(d_min * d_min) / two_sigma_sq
      let gauss = expf(exponent)
      w_arr[i * n + j] = w_max * gauss
      j = j + 1
    }
    w_arr[i * n + i] = 0.0F
    i = i + 1
  }
  w_arr
}

///|
pub fn row_major_get(w : Array[Float], n : Int, i : Int, j : Int) -> Float {
  w[i * n + j]
}