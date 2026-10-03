// score_network.mbt -- ScoreNetwork for DDPM (v0.114.0).
//
// The reverse-process network in DDPM is typically a U-Net or a simple
// MLP that takes the noisy input x_t and the diffusion timestep t and
// predicts either the noise that was added (epsilon-prediction) or the
// clean sample directly (x_0-prediction). Here we ship the epsilon
// variant with a simple MLP backbone and a sinusoidal time embedding
// (Vaswani et al. 2017 "Attention is All You Need" position encoding,
// reused for the scalar timestep).
//
// Scope of v0.114.0:
//   - Sinusoidal time embedding helpers.
//   - ScoreNetwork struct (input_dim + time_dim + hidden + num_layers).
//   - score_network_predict: x_t + t -> epsilon (Float32).
//
// Reference: Ho et al. 2020 (DDPM); Vaswani et al. 2017 (sinusoidal PE).

///|
/// ScoreNetwork: MLP denoiser. For input x_t of length `input_dim` and
/// timestep t in [0, T), we compute a sinusoidal time embedding of
/// dim `time_dim`, concatenate it with x_t, and pass through `num_layers`
/// MLP layers of width `hidden_dim` with GELU activation. The final
/// linear projects back to `input_dim`.
pub struct ScoreNetwork {
  input_dim : Int
  time_dim : Int
  hidden_dim : Int
  num_layers : Int
  // First projection: (hidden_dim x (input_dim + time_dim)).
  w_in : Array[Array[Float]]
  b_in : Array[Float]
  // Hidden layers: each (hidden_dim x hidden_dim).
  hidden_w : Array[Array[Array[Float]]]
  hidden_b : Array[Array[Float]]
  // Output projection: (input_dim x hidden_dim).
  w_out : Array[Array[Float]]
  b_out : Array[Float]
}

///|
/// Build a fresh ScoreNetwork with xavier-normal init.
pub fn ScoreNetwork::new(
  input_dim : Int,
  time_dim : Int,
  hidden_dim : Int,
  num_layers : Int,
  seed : UInt64,
) -> ScoreNetwork {
  let combined = input_dim + time_dim
  let rng1 = Xoshiro::from_state(
    seed + 10UL, seed + 11UL, seed + 12UL, seed + 13UL,
  )
  let std1 = sqrtf(2.0F / Float::from_int(combined))
  let w_in = xavier_normal(hidden_dim, combined, std1, rng1)
  let b_in : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_w : Array[Array[Array[Float]]] = Array::make(
    num_layers - 1, Array::make(0, Array::make(0, 0.0F)),
  )
  let hidden_b : Array[Array[Float]] = Array::make(num_layers - 1, Array::make(0, 0.0F))
  for l in 0..<(num_layers - 1) {
    let rng_l = Xoshiro::from_state(
      seed + (l + 100).to_uint64() * 7UL,
      seed + (l + 100).to_uint64() * 11UL,
      seed + (l + 100).to_uint64() * 13UL,
      seed + (l + 100).to_uint64() * 17UL,
    )
    let std_l = sqrtf(2.0F / Float::from_int(hidden_dim))
    hidden_w[l] = xavier_normal(hidden_dim, hidden_dim, std_l, rng_l)
    hidden_b[l] = Array::make(hidden_dim, 0.0F)
  }
  let rng_out = Xoshiro::from_state(
    seed + 20UL, seed + 21UL, seed + 22UL, seed + 23UL,
  )
  let std_out = sqrtf(2.0F / Float::from_int(hidden_dim))
  let w_out = xavier_normal(input_dim, hidden_dim, std_out, rng_out)
  let b_out : Array[Float] = Array::make(input_dim, 0.0F)
  { input_dim, time_dim, hidden_dim, num_layers, w_in, b_in, hidden_w, hidden_b, w_out, b_out }
}

///|
/// Sinusoidal time embedding for a scalar timestep `t`. Returns a fresh
/// array of length `time_dim` where indices 0,2,4,... hold
/// sin(t * omega_k) and indices 1,3,5,... hold cos(t * omega_k), with
/// omega_k = 10000^(-k / (time_dim/2)).
pub fn sinusoidal_time_embedding(
  t : Int,
  time_dim : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(time_dim, 0.0F)
  let half = time_dim / 2
  for k in 0..<half {
    // omega_k = 10000^(-2k / time_dim) = exp(-2k * ln(10000) / time_dim).
    let two_k_f = Float::from_int(2 * k)
    let denom = Float::from_int(time_dim)
    let omega = expf(-two_k_f * 9.21034F / denom)
    let arg = Float::from_int(t) * omega
    out[2 * k] = sinf(arg)
    out[2 * k + 1] = cosf(arg)
  }
  // If time_dim is odd, fill the last slot with a constant zero
  // (it would have no matching pair).
  if time_dim % 2 == 1 {
    out[time_dim - 1] = 0.0F
  }
  out
}

///|
/// Forward pass through the score network. Returns the predicted noise
/// epsilon (length input_dim).
pub fn score_network_predict(
  net : ScoreNetwork,
  x_t : Array[Float],
  t : Int,
) -> Array[Float] {
  let input_dim = net.input_dim
  let time_dim = net.time_dim
  let hidden_dim = net.hidden_dim
  let combined = input_dim + time_dim
  let time_emb = sinusoidal_time_embedding(t, time_dim)
  // Build combined input.
  let z : Array[Float] = Array::make(combined, 0.0F)
  for i in 0..<input_dim {
    z[i] = x_t[i]
  }
  for i in 0..<time_dim {
    z[input_dim + i] = time_emb[i]
  }
  // First Linear + GELU.
  let mut h : Array[Float] = Array::make(hidden_dim, 0.0F)
  for i in 0..<hidden_dim {
    let mut a = net.b_in[i]
    for k in 0..<combined {
      a = a + net.w_in[i][k] * z[k]
    }
    h[i] = gelu(a)
  }
  // Hidden layers with GELU.
  for l in 0..<(net.num_layers - 1) {
    let new_h : Array[Float] = Array::make(hidden_dim, 0.0F)
    for i in 0..<hidden_dim {
      let mut a = net.hidden_b[l][i]
      for k in 0..<hidden_dim {
        a = a + net.hidden_w[l][i][k] * h[k]
      }
      new_h[i] = gelu(a)
    }
    h = new_h
  }
  // Output projection (no activation).
  let out : Array[Float] = Array::make(input_dim, 0.0F)
  for i in 0..<input_dim {
    let mut a = net.b_out[i]
    for k in 0..<hidden_dim {
      a = a + net.w_out[i][k] * h[k]
    }
    out[i] = a
  }
  out
}
