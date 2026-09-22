// tensor.mbt 鈥?Tensor struct + element-wise ops + broadcasting.
//
// Design:
//   - Tensor wraps a flat `Array[Float]` plus a `shape` array.
//   - Memory layout is row-major (C-order), same as NCHW conv /
//     maxpool / linear / etc.
//   - Reshape is a no-op data-wise (only updates shape) when total
//     element count is preserved.
//   - Element-wise ops use **broadcasting** following the standard
//     right-align rule:
//       align shapes from the right; for each axis dim is compatible if
//       equal or one of them is 1; missing leading dims are treated as
//       size-1 (implicit broadcast).
//   - All ops return a fresh Tensor (no aliasing of inputs).

///|
/// Tensor struct: row-major flat data + shape.
pub struct Tensor {
  data : Array[Float]
  shape : Array[Int]
}

///|
fn total_size(shape : Array[Int]) -> Int {
  let mut n = 1
  for i in 0..<shape.length() {
    n = n * shape[i]
  }
  n
}

///|
/// Build a Tensor with the given shape, filled with zeros.
pub fn Tensor::zeros(shape : Array[Int]) -> Tensor {
  { data: Array::make(total_size(shape), 0.0F), shape }
}

///|
/// Build a Tensor with the given shape, filled with ones.
pub fn Tensor::ones(shape : Array[Int]) -> Tensor {
  { data: Array::make(total_size(shape), 1.0F), shape }
}

///|
/// Build a Tensor from a flat data array + shape. Does NOT copy data.
pub fn Tensor::from(data : Array[Float], shape : Array[Int]) -> Tensor {
  { data, shape }
}

///|
/// Reshape to a new shape. Panics if total element count mismatches.
pub fn Tensor::reshape(self : Tensor, shape : Array[Int]) -> Tensor {
  if total_size(shape) != self.data.length() {
    abort("Tensor::reshape: total size mismatch")
  }
  { data: self.data, shape }
}

///|
/// Element count.
pub fn Tensor::numel(self : Tensor) -> Int {
  self.data.length()
}

///|
/// Compute the broadcast shape of two input shapes. Returns
/// `[-1]` if incompatible.
fn broadcast_shape(a : Array[Int], b : Array[Int]) -> Array[Int] {
  let ndim = if a.length() > b.length() { a.length() } else { b.length() }
  let out : Array[Int] = Array::make(ndim, 1)
  let a_offset = ndim - a.length()
  let b_offset = ndim - b.length()
  let mut i = 0
  while i < ndim {
    let ai = if i < a_offset { 1 } else { a[i - a_offset] }
    let bi = if i < b_offset { 1 } else { b[i - b_offset] }
    if ai == bi {
      out[i] = ai
    } else if ai == 1 {
      out[i] = bi
    } else if bi == 1 {
      out[i] = ai
    } else {
      return [-1]
    }
    i = i + 1
  }
  out
}

///|
/// Compute the row-major strides for a shape (right-most dim is stride 1).
fn compute_strides(shape : Array[Int]) -> Array[Int] {
  let n = shape.length()
  let strides : Array[Int] = Array::make(n, 1)
  if n == 0 { return strides }
  let mut j = n - 2
  while j >= 0 {
    strides[j] = strides[j + 1] * shape[j + 1]
    j = j - 1
  }
  strides
}

///|
/// Apply a binary element-wise op on two broadcast-compatible tensors.
/// `f(a, b) -> Float` is the element-wise function.
fn elementwise_binary(
  a : Tensor,
  b : Tensor,
  f : (Float, Float) -> Float,
) -> Tensor {
  let out_shape = broadcast_shape(a.shape, b.shape)
  if out_shape.length() == 1 && out_shape[0] == -1 {
    abort("elementwise: incompatible broadcast shapes")
  }
  let n = total_size(out_shape)
  let out_data : Array[Float] = Array::make(n, 0.0F)
  let out_strides = compute_strides(out_shape)
  let a_strides = compute_strides(a.shape)
  let b_strides = compute_strides(b.shape)
  let out_offset_a = out_shape.length() - a.shape.length()
  let out_offset_b = out_shape.length() - b.shape.length()
  let mut i = 0
  while i < n {
    let mut rem = i
    let mut ai = 0
    let mut bi = 0
    let mut j = 0
    while j < out_shape.length() {
      let coord = rem / out_strides[j]
      rem = rem - coord * out_strides[j]
      let a_axis = j - out_offset_a
      if a_axis >= 0 {
        if a.shape[a_axis] != 1 {
          ai = ai + coord * a_strides[a_axis]
        }
      }
      let b_axis = j - out_offset_b
      if b_axis >= 0 {
        if b.shape[b_axis] != 1 {
          bi = bi + coord * b_strides[b_axis]
        }
      }
      j = j + 1
    }
    out_data[i] = f(a.data[ai], b.data[bi])
    i = i + 1
  }
  { data: out_data, shape: out_shape }
}

///|
/// Tensor add (with broadcasting).
pub fn tensor_add(a : Tensor, b : Tensor) -> Tensor {
  elementwise_binary(a, b, fn(x, y) { x + y })
}

///|
/// Tensor subtract (with broadcasting).
pub fn tensor_sub(a : Tensor, b : Tensor) -> Tensor {
  elementwise_binary(a, b, fn(x, y) { x - y })
}

///|
/// Tensor multiply (with broadcasting).
pub fn tensor_mul(a : Tensor, b : Tensor) -> Tensor {
  elementwise_binary(a, b, fn(x, y) { x * y })
}

///|
/// Tensor divide (with broadcasting).
pub fn tensor_div(a : Tensor, b : Tensor) -> Tensor {
  elementwise_binary(a, b, fn(x, y) { x / y })
}