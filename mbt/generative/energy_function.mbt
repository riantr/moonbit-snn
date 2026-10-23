// energy_function.mbt -- EnergyFunction for Energy-Based Models (v0.117.0).
//
// An Energy-Based Model (LeCun et al. 2006 "A Tutorial on Energy-Based
// Learning") assigns a scalar "energy" E(x) to each input; probability
// is inversely related to energy via the Boltzmann distribution
// p(x) = exp(-E(x)) / Z. Training and sampling both revolve around the
// energy function.
//
// Scope of v0.117.0:
//   - EnergyFunction struct (MLP: input_dim -> hidden_dim -> ... -> 1).
//   - EnergyFunction::new (xavier-normal init).
//   - energy_function_predict: x -> E(x).
//
// Reference: LeCun et al. 2006; the canonical MLP backbone is reused
// from earlier score_network-style code.

///|
/// EnergyFunction: a small MLP that maps an input vector of length
/// `input_dim` to a single scalar energy.
pub struct EnergyFunction {
  input_dim : Int
  hidden_dim : Int
  num_layers : Int
  // Layer 0: input -> hidden.
  w1 : Array[Array[Float]]
  b1 : Array[Float]
  // Hidden layers.
  hidden_w : Array[Array[Array[Float]]]
  hidden_b : Array[Array[Float]]
  // Final hidden -> 1.
  w_out : Array[Array[Float]]
  b_out : Float
}

///|
/// Build a fresh EnergyFunction with `num_layers` layers of width
/// `hidden_dim` before the output projection -- an input->hidden layer plus
/// `num_layers - 1` hidden matrices. The output is a single scalar (no bias
/// array, just one Float).
pub fn EnergyFunction::new(
  input_dim : Int,
  hidden_dim : Int,
  num_layers : Int,
  seed : UInt64,
) -> EnergyFunction {
  let rng1 = @core.Xoshiro::from_state(
    seed + 10UL, seed + 11UL, seed + 12UL, seed + 13UL,
  )
  let std1 = @core.sqrtf(2.0F / Float::from_int(input_dim))
  let w1 = @rl.xavier_normal(hidden_dim, input_dim, std1, rng1)
  let b1 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let (hidden_w, hidden_b) = @rl.mlp_hidden_stack(hidden_dim, num_layers, seed)
  let rng_out = @core.Xoshiro::from_state(
    seed + 20UL, seed + 21UL, seed + 22UL, seed + 23UL,
  )
  let std_out = @core.sqrtf(2.0F / Float::from_int(hidden_dim))
  let w_out = @rl.xavier_normal(1, hidden_dim, std_out, rng_out)
  { input_dim, hidden_dim, num_layers, w1, b1, hidden_w, hidden_b, w_out, b_out: 0.0F }
}

///|
/// Forward pass through the MLP. Returns the scalar energy E(x).
pub fn energy_function_predict(
  net : EnergyFunction,
  x : Array[Float],
) -> Float {
  let hidden_dim = net.hidden_dim
  // First Linear + GELU.
  let mut h : Array[Float] = Array::make(hidden_dim, 0.0F)
  for i in 0..<hidden_dim {
    let mut a = net.b1[i]
    for k in 0..<net.input_dim {
      a = a + net.w1[i][k] * x[k]
    }
    h[i] = @activ.gelu(a)
  }
  // Hidden layers with GELU.
  for l in 0..<(net.num_layers - 1) {
    let new_h : Array[Float] = Array::make(hidden_dim, 0.0F)
    for i in 0..<hidden_dim {
      let mut a = net.hidden_b[l][i]
      for k in 0..<hidden_dim {
        a = a + net.hidden_w[l][i][k] * h[k]
      }
      new_h[i] = @activ.gelu(a)
    }
    h = new_h
  }
  // Output projection to a single scalar.
  let mut out = net.b_out
  for k in 0..<hidden_dim {
    out = out + net.w_out[0][k] * h[k]
  }
  out
}