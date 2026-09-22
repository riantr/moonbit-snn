// bottles.mbt — Pre-made model architectures (v0.19.1 + v0.19.2 + v0.19.3).
//
// Thin wrappers over the generic Chain from `chain.mbt`. Each Bottle is
// a small struct that holds an `Array[Layer]` plus the hyper-parameters
// needed to construct it. Construction is parameter-count-driven
// (number of layers / feature sizes) — the caller passes the shape
// and the bottle allocates a parameter-count-matched Linear / Conv2d
// with random initial weights via the project's xoshiro RNG.
//
// Three architectures:
//   - MLP       multi-layer perceptron, sizes = [in, h1, h2, ..., out]
//   - SimpleCNN Conv → ReLU → Pool → Conv → ReLU → Pool → Flatten → FC
//   - LeNet5    classic LeNet-5 (32x32 input, 6/16 conv channels, FC)
//
// Conventions:
//   - All initial weights are drawn from N(0, sqrt(2/n_in)) (He init).
//   - Biases start at zero.
//   - The seed for the RNG is deterministic-friendly: callers pass an
//     explicit `seed : UInt64` so the same bottle shape reproduces the
//     same weights.

///|
/// Fill an Array[Float] of length `n` with He-init (Kaiming) draws:
/// N(0, sqrt(2 / fan_in)) using a xoshiro RNG and Float32 Box-Muller.
fn he_init(rng : Xoshiro, n : Int, fan_in : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  let std = sqrtf(2.0F / Float::from_int(fan_in))
  let mut i = 0
  while i + 1 < n {
    // Box-Muller on Float32 (consume 2 uniforms -> 2 normals).
    let u1 = {
      let mut v = next_f32(rng)
      if v < 0.000001F { v = 0.000001F }
      v
    }
    let u2 = next_f32(rng)
    let z1 = sqrtf(-2.0F * logf(u1)) * cosf(2.0F * 3.14159265F * u2)
    let z2 = sqrtf(-2.0F * logf(u1)) * sin_f32(2.0F * 3.14159265F * u2)
    out[i] = z1 * std
    out[i + 1] = z2 * std
    i = i + 2
  }
  if i < n {
    // Last one if odd length.
    let u1 = {
      let mut v = next_f32(rng)
      if v < 0.000001F { v = 0.000001F }
      v
    }
    let u2 = next_f32(rng)
    let z1 = sqrtf(-2.0F * logf(u1)) * cosf(2.0F * 3.14159265F * u2)
    out[i] = z1 * std
  }
  out
}

///|
/// Float32 sin via libm.
extern "C" fn sinf(x : Float) -> Float = "sinf"

///|
fn sin_f32(x : Float) -> Float { sinf(x) }

// ---------------------------------------------------------------------------
// MLP
// ---------------------------------------------------------------------------

///|
/// A multi-layer perceptron. `sizes` is `[in, h1, h2, ..., out]`;
/// the bottle allocates Linear layers between adjacent entries with a
/// ReLU between every pair (no ReLU after the final Linear).
pub struct MLP {
  layers : Array[Layer]
  sizes : Array[Int]
}

///|
/// Build an MLP. `seed` controls the xoshiro RNG used for He init
/// (sqrt(2/n_in) per Linear layer). Use the same seed for reproducible
/// bottles.
pub fn MLP::new(sizes : Array[Int], seed : UInt64) -> MLP {
  let rng = Xoshiro::new(seed)
  let layers : Array[Layer] = []
  for i in 0..<(sizes.length() - 1) {
    let in_f = sizes[i]
    let out_f = sizes[i + 1]
    let weight = he_init(rng, out_f * in_f, in_f)
    let bias : Array[Float] = Array::make(out_f, 0.0F)
    let p = LinearParam::new(weight, bias, in_f, out_f)
    layers.push(Layer::linear(p))
    if i < sizes.length() - 2 {
      layers.push(Layer::relu())
    }
  }
  { layers, sizes }
}

///|
/// Forward pass for an MLP. Input shape is `[n, sizes[0]]`; the chain
/// handles layer chaining internally.
pub fn mlp_forward(
  m : MLP,
  input : Array[Float],
  n : Int,
) -> (Array[Float], Array[LayerCache], Int) {
  let in_f = m.sizes[0]
  let (out, caches, n_out, c_out, _h, _w) = chain_forward(
    m.layers, input, n, in_f, 1, 1,
  )
  (out, caches, n_out * c_out)
}

///|
/// Backward pass for an MLP. `d_output` has length `n * sizes[last]`.
pub fn mlp_backward(
  m : MLP,
  caches : Array[LayerCache],
  d_output : Array[Float],
  n : Int,
) -> (Array[Float], Array[LayerGrad]) {
  let out_f = m.sizes[m.sizes.length() - 1]
  chain_backward(m.layers, caches, d_output, n, out_f, 1, 1)
}

///|
/// Total learnable parameter count.
pub fn MLP::num_params(self : MLP) -> Int {
  let mut total = 0
  for layer in self.layers {
    match layer {
      Linear(p) => total = total + p.weight.length() + p.bias.length()
      _ => ()
    }
  }
  total
}

// ---------------------------------------------------------------------------
// SimpleCNN
// ---------------------------------------------------------------------------

///|
/// A small CNN: `Conv(c1, c2) → ReLU → MaxPool → Flatten → FC(10)`.
/// Designed for 28x28 MNIST-like inputs (single channel by default).
///
///   Input:   [n, in_c, 28, 28]
///   Block 1: Conv2d(in_c -> c1, 3x3, pad=1) -> ReLU -> MaxPool 2x2 / 2
///   Block 2: Conv2d(c1 -> c2, 3x3, pad=1) -> ReLU -> MaxPool 2x2 / 2
///   FC:     Linear(c2 * 7 * 7 -> 10)
pub struct SimpleCNN {
  layers : Array[Layer]
  in_c : Int
  c1 : Int
  c2 : Int
}

///|
/// Build a SimpleCNN. `in_c` is input channels (1 for MNIST, 3 for
/// color images).
pub fn SimpleCNN::new(in_c : Int, c1 : Int, c2 : Int, seed : UInt64) -> SimpleCNN {
  let rng = Xoshiro::new(seed)
  // First conv: in_c -> c1, 3x3, pad=1.
  let w1 = he_init(rng, c1 * in_c * 3 * 3, in_c * 3 * 3)
  let b1 : Array[Float] = Array::make(c1, 0.0F)
  let conv1 = Conv2dParam::new(w1, b1, c1, in_c, 3, 3, stride=1, pad=1)
  // Second conv: c1 -> c2, 3x3, pad=1.
  let w2 = he_init(rng, c2 * c1 * 3 * 3, c1 * 3 * 3)
  let b2 : Array[Float] = Array::make(c2, 0.0F)
  let conv2 = Conv2dParam::new(w2, b2, c2, c1, 3, 3, stride=1, pad=1)
  // Pool: 2x2 stride 2.
  let pool = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  // FC: c2 * 7 * 7 -> 10.
  let fc_in = c2 * 7 * 7
  let fc_out = 10
  let fc_w = he_init(rng, fc_out * fc_in, fc_in)
  let fc_b : Array[Float] = Array::make(fc_out, 0.0F)
  let fc = LinearParam::new(fc_w, fc_b, fc_in, fc_out)
  let layers : Array[Layer] = [
    Layer::conv2d(conv1), Layer::relu(), Layer::max_pool2d(pool),
    Layer::conv2d(conv2), Layer::relu(), Layer::max_pool2d(pool),
    Layer::flatten(), Layer::linear(fc),
  ]
  { layers, in_c, c1, c2 }
}

///|
/// Forward pass for SimpleCNN. Input must be `[n, in_c, 28, 28]`.
pub fn simple_cnn_forward(
  m : SimpleCNN,
  input : Array[Float],
  n : Int,
) -> (Array[Float], Array[LayerCache], Int, Int, Int, Int) {
  chain_forward(m.layers, input, n, m.in_c, 28, 28)
}

///|
/// Backward pass. `d_output` has shape `[n, 10, 1, 1]`.
pub fn simple_cnn_backward(
  m : SimpleCNN,
  caches : Array[LayerCache],
  d_output : Array[Float],
  n : Int,
) -> (Array[Float], Array[LayerGrad]) {
  chain_backward(m.layers, caches, d_output, n, 10, 1, 1)
}

// ---------------------------------------------------------------------------
// LeNet5
// ---------------------------------------------------------------------------

///|
/// Classic LeNet-5 (LeCun 1998) for 32x32 grayscale input.
///
///   C1: Conv 1->6, 5x5, stride=1, pad=0   -> [n, 6, 28, 28]
///   S2: MaxPool 2x2, stride=2              -> [n, 6, 14, 14]
///   C3: Conv 6->16, 5x5, stride=1, pad=0  -> [n, 16, 10, 10]
///   S4: MaxPool 2x2, stride=2              -> [n, 16, 5, 5]
///   C5: Conv 16->120, 5x5, stride=1, pad=0 -> [n, 120, 1, 1] (acts as FC)
///   F6: Linear 120 -> 84
///   Out: Linear 84 -> 10
///
/// Note: We use MaxPool instead of AvgPool (a common modern variant;
/// not bit-exact to the original paper but produces an equally-valid
/// classifier architecture).
pub struct LeNet5 {
  layers : Array[Layer]
}

///|
/// Build a LeNet5. Always 32x32 grayscale input -> 10 classes.
pub fn LeNet5::new(seed : UInt64) -> LeNet5 {
  let rng = Xoshiro::new(seed)
  // C1: 1 -> 6, 5x5, stride=1, pad=0.
  let w1 = he_init(rng, 6 * 1 * 5 * 5, 1 * 5 * 5)
  let b1 : Array[Float] = Array::make(6, 0.0F)
  let conv1 = Conv2dParam::new(w1, b1, 6, 1, 5, 5, stride=1, pad=0)
  let pool1 = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  // C3: 6 -> 16, 5x5, stride=1, pad=0.
  let w2 = he_init(rng, 16 * 6 * 5 * 5, 6 * 5 * 5)
  let b2 : Array[Float] = Array::make(16, 0.0F)
  let conv2 = Conv2dParam::new(w2, b2, 16, 6, 5, 5, stride=1, pad=0)
  let pool2 = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  // C5: 16 -> 120, 5x5, stride=1, pad=0.
  let w3 = he_init(rng, 120 * 16 * 5 * 5, 16 * 5 * 5)
  let b3 : Array[Float] = Array::make(120, 0.0F)
  let conv3 = Conv2dParam::new(w3, b3, 120, 16, 5, 5, stride=1, pad=0)
  // F6: 120 -> 84.
  let w4 = he_init(rng, 84 * 120, 120)
  let b4 : Array[Float] = Array::make(84, 0.0F)
  let fc1 = LinearParam::new(w4, b4, 120, 84)
  // Out: 84 -> 10.
  let w5 = he_init(rng, 10 * 84, 84)
  let b5 : Array[Float] = Array::make(10, 0.0F)
  let fc2 = LinearParam::new(w5, b5, 84, 10)
  let layers : Array[Layer] = [
    Layer::conv2d(conv1), Layer::relu(), Layer::max_pool2d(pool1),
    Layer::conv2d(conv2), Layer::relu(), Layer::max_pool2d(pool2),
    Layer::conv2d(conv3), Layer::relu(), Layer::flatten(),
    Layer::linear(fc1), Layer::relu(), Layer::linear(fc2),
  ]
  { layers }
}

///|
/// Forward pass for LeNet5. Input must be `[n, 1, 32, 32]`.
pub fn lenet5_forward(
  m : LeNet5,
  input : Array[Float],
  n : Int,
) -> (Array[Float], Array[LayerCache], Int, Int, Int, Int) {
  chain_forward(m.layers, input, n, 1, 32, 32)
}

///|
/// Backward pass. `d_output` has shape `[n, 10, 1, 1]`.
pub fn lenet5_backward(
  m : LeNet5,
  caches : Array[LayerCache],
  d_output : Array[Float],
  n : Int,
) -> (Array[Float], Array[LayerGrad]) {
  chain_backward(m.layers, caches, d_output, n, 10, 1, 1)
}