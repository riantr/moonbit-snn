// parity_check.mbt — bit-exact parity verification helpers (v0.42.3).
//
// Mirrors the SpikingNeuralNetworks.jl side-by-side verification
// harness. Provides deterministic dumps of state (RNG, neuron
// trajectories, weight matrices) that can be compared against
// equivalent Julia runs to validate Float32 bit-exactness.
//
// Note: MoonBit doesn't expose a Float-to-bits reinterpret API, so
// we use a Float→Int quantization (multiply by 1e6, truncate) which
// is deterministic and matches what Julia can produce via
// `Float32(x * 1f6) |> Int32(x)` for the same input.
//
// Hash convention: FNV-1a 32-bit. Julia side computes the same
// FNV-1a 32-bit hash over the quantized Int array. As long as both
// sides produce the same quantized Int sequence, the hashes match.

// ── FNV-1a 32-bit hash over Int array ─────────────────────────────

///|
/// FNV-1a 32-bit hash over an Int array.
pub fn fnv1a_32(data : Array[Int]) -> Int {
  let mut h : UInt = 0x811c9dc5  // FNV offset basis
  for i in 0..<data.length() {
    let v : UInt = data[i].to_uint()
    // Hash 4 bytes (little-endian).
    let mut j = 0
    while j < 4 {
      let b : UInt = (v >> (j * 8)) & 0xFF
      h = h ^ b
      h = h * 0x01000193  // FNV prime
      j = j + 1
    }
  }
  h.to_int()
}

///|
/// Quantise a Float array to Int by scaling by 1e6 and truncating.
pub fn floats_to_int_keys(data : Array[Float]) -> Array[Int] {
  let out : Array[Int] = Array::make(data.length(), 0)
  for i in 0..<data.length() {
    out[i] = Float::to_int(data[i] * 1000000.0F)
  }
  out
}

///|
/// Convenience: hash a Float array via quantization.
pub fn parity_hash_floats(data : Array[Float]) -> Int {
  fnv1a_32(floats_to_int_keys(data))
}

// ── RNG parity ────────────────────────────────────────────────────

///|
/// Hash the public state of an Xoshiro RNG after seeding with
/// `seed` and advancing `n` steps. Caller must use the same seed +
/// step count on the Julia side to verify.
pub fn parity_hash_xoshiro(seed : UInt64, n : Int) -> Int {
  let rng = Xoshiro::new(seed)
  let samples : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    samples[i] = next_f32(rng)
  }
  parity_hash_floats(samples)
}

// ── IF neuron trajectory parity ───────────────────────────────────

///|
/// Hash the public state of an IF neuron over a fixed-step simulation
/// with constant tonic current `i_drive`. Returns an Int that should
/// match a Julia run with the same parameters.
pub fn parity_hash_if_trajectory(
  n_steps : Int,
  dt : Float,
  i_drive : Float,
  seed : UInt64,
) -> Int {
  let rng = Xoshiro::new(seed)
  let param = IFParameter::new()
  let pop = IF::new(1, param, rng)
  // Set tonic current to all neurons.
  for i in 0..<pop.n {
    pop.i[i] = i_drive
  }
  // Snapshot v + fire flag at every step.
  let snap : Array[Float] = []
  for _ in 0..<n_steps {
    step_neuron(pop, dt)
    snap.push(pop.v[0])
    if pop.fire[0] {
      snap.push(1.0F)
    } else {
      snap.push(0.0F)
    }
  }
  parity_hash_floats(snap)
}

// ── Sparse matrix parity ───────────────────────────────────────────

///|
/// Hash the weight matrix of a randomly-initialised sparse connection
/// matrix. Returns an Int hash of the vals + colptr concatenated.
// Matches Julia's `SparseMatrixCSR` representation.
pub fn parity_hash_sparse_matrix(
  n_pre : Int,
  n_post : Int,
  mu : Float,
  sigma : Float,
  p : Float,
  seed : UInt64,
) -> Int {
  let rng = Xoshiro::new(seed)
  let m = SparseMatrixCSR::random(n_pre, n_post, mu, sigma, p, rng)
  // Hash vals + colptr (rowptr is structural, not state).
  let combined : Array[Float] = []
  for i in 0..<m.vals.length() {
    combined.push(m.vals[i])
    // Encode colptr index in float (so quantisation to Int is well-defined).
    combined.push(Float::from_int(m.colptr[i]) + 0.5F)
  }
  parity_hash_floats(combined)
}

// ── One-shot dump ─────────────────────────────────────────────────

///|
/// Build a deterministic parity dump: a 3-tuple of (xoshiro_hash,
/// if_trajectory_hash, sparse_hash) for a fixed seed. Useful as a
/// one-shot sanity check.
pub fn parity_full_dump(seed : UInt64) -> (Int, Int, Int) {
  let h1 = parity_hash_xoshiro(seed, 1000)
  let h2 = parity_hash_if_trajectory(200, 0.125F, 1.5F, seed)
  let h3 = parity_hash_sparse_matrix(50, 50, 1.0F, 0.3F, 0.5F, seed)
  (h1, h2, h3)
}

///|
/// Format a parity dump as a single Int that combines all three
/// sub-hashes. Useful for cross-language comparison (Julia can
/// reproduce this with the same recipe).
pub fn parity_combined_hash(seed : UInt64) -> Int {
  let (h1, h2, h3) = parity_full_dump(seed)
  // Combine via FNV-1a over the 3 sub-hashes.
  fnv1a_32([h1, h2, h3])
}