// srnn_demo.mbt — Spiking Recurrent Neural Network training demo.
//
// Time-unrolled SRNN with IF (Integrate-and-Fire) spiking neuron,
// trained via BPTT with surrogate gradient (Zenke & Ganguli 2018).
//
// Architecture:
//   input (n_steps, n_in): Poisson spike train from a static "image"
//   For t in 0..n_steps-1:
//     I[t]    = W_in @ x[t] + W_rec @ s[t-1]   # recurrent synaptic input
//     v[t]    = α * v[t-1] + I[t]              # IF membrane (leaky)
//     s[t]    = H(v[t] - Vt)                  # 0 or 1 spike (hard forward)
//     y[t]    = W_out @ s[t]                 # readout
//   loss = CE(y[n_steps-1], target)            # loss at final timestep
//
// BPTT:
//   d_v[t] = d_s[t] * σ'(v[t] - Vt; β)        # surrogate gradient
//   d_v[t] += α * d_v[t+1] if t < n_steps-1   # temporal backflow
//   d_s[t] = W_out^T @ d_y[t]                 # readout backward
//   d_s[t] += W_rec^T @ d_v[t+1]             # recurrent gradient
//
// Reuses:
//   v0.22.0 surrogate (fast_sigmoid_surrogate for the backward)
//   v0.13.2 cross_entropy_loss (forward + backward via softmax)

// ---- Synthetic dataset ----

///|
/// Poisson-encode a static "image" vector into a (n_steps, n_in) spike
/// train. Spike probability per (t, i) = `min(1, input[i] * max_rate * dt)`.
pub fn srnn_poisson_encode(
  input : Array[Float],
  n_steps : Int,
  dt : Float,
  max_rate : Float,
  rng : Xoshiro,
) -> Array[Float] {
  let n_in = input.length()
  let out : Array[Float] = Array::make(n_steps * n_in, 0.0F)
  for t in 0..<n_steps {
    for i in 0..<n_in {
      let raw_p = input[i] * max_rate * dt
      let p = if raw_p > 1.0F { 1.0F } else { raw_p }
      let r = next_f32(rng)
      out[t * n_in + i] = if r < p { 1.0F } else { 0.0F }
    }
  }
  out
}

///|
/// Synthetic 2-class dataset: 16 samples (8 per class).
/// Class 0: x[0] in [0.7, 1.0]. Class 1: x[0] in [0, 0.3].
pub fn srnn_dataset_build(
  seed : UInt64,
) -> (Array[Float], Array[Int]) {
  let n_classes = 2
  let per_class = 8
  let n_total = n_classes * per_class
  let n_in = 2
  let images : Array[Float] = Array::make(n_total * n_in, 0.0F)
  let labels : Array[Int] = Array::make(n_total, 0)
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  for c in 0..<n_classes {
    for k in 0..<per_class {
      let idx = c * per_class + k
      labels[idx] = c
      let x0 = if c == 0 { 0.7F + 0.3F * next_f32(rng) } else { 0.3F * next_f32(rng) }
      let x1 = next_f32(rng)
      images[idx * n_in + 0] = x0
      images[idx * n_in + 1] = x1
    }
  }
  (images, labels)
}

// ---- SRNN model ----

///|
/// SRNN parameter container.
pub struct SRNN {
  n_in : Int
  n_hidden : Int
  n_classes : Int
  dt : Float
  alpha : Float      // leak factor (1 - dt/tau)
  v_thresh : Float
  w_in : Array[Float]
  w_rec : Array[Float]
  w_out : Array[Float]
  b_in : Array[Float]
  b_out : Array[Float]
}

///|
/// Forward / backward cache.
pub struct SRNNCache {
  n_steps : Int
  n_in : Int
  n_hidden : Int
  n_classes : Int
  x : Array[Float]  // input (n_steps * n_in)
  v : Array[Float]  // membrane (n_steps * n_hidden)
  spikes : Array[Float]  // spikes (n_steps * n_hidden)
  y : Array[Float]  // logits (n_steps * n_classes)
  probs : Array[Float]  // final-step softmax (n_classes)
  target : Int
  alpha : Float
  v_thresh : Float
  beta : Float
}

///|
/// Construct a SRNN with random init.
pub fn SRNN::new(
  n_in : Int,
  n_hidden : Int,
  n_classes : Int,
  seed : UInt64,
) -> SRNN {
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std_in = sqrtf(1.0F / Float::from_int(n_in))
  let std_rec = sqrtf(1.0F / Float::from_int(n_hidden))
  let std_out = sqrtf(1.0F / Float::from_int(n_hidden))
  let w_in : Array[Float] = Array::make(n_in * n_hidden, 0.0F)
  let w_rec : Array[Float] = Array::make(n_hidden * n_hidden, 0.0F)
  let w_out : Array[Float] = Array::make(n_hidden * n_classes, 0.0F)
  let b_in : Array[Float] = Array::make(n_hidden, 0.0F)
  let b_out : Array[Float] = Array::make(n_classes, 0.0F)
  let mut i = 0
  while i < n_in * n_hidden {
    let (z, _) = box_muller(rng)
    w_in[i] = Float::from_double(z) * std_in
    i = i + 1
  }
  let mut j = 0
  while j < n_hidden * n_hidden {
    let (z, _) = box_muller(rng)
    w_rec[j] = Float::from_double(z) * std_rec
    j = j + 1
  }
  let mut k = 0
  while k < n_hidden * n_classes {
    let (z, _) = box_muller(rng)
    w_out[k] = Float::from_double(z) * std_out
    k = k + 1
  }
  let dt : Float = 1.0F
  let alpha : Float = 0.9F
  let v_thresh : Float = 0.5F
  { n_in, n_hidden, n_classes, dt, alpha, v_thresh,
    w_in, w_rec, w_out, b_in, b_out }
}

///|
/// Forward pass. Returns (loss, cache).
pub fn srnn_forward(
  x : Array[Float],
  n_steps : Int,
  target : Int,
  srnn : SRNN,
  beta : Float,
) -> (Float, SRNNCache) {
  let n_in = srnn.n_in
  let n_hidden = srnn.n_hidden
  let n_classes = srnn.n_classes
  let alpha = srnn.alpha
  let v_thresh = srnn.v_thresh

  let v : Array[Float] = Array::make(n_steps * n_hidden, 0.0F)
  let spikes : Array[Float] = Array::make(n_steps * n_hidden, 0.0F)
  let y : Array[Float] = Array::make(n_steps * n_classes, 0.0F)

  // Run timesteps.
  for t in 0..<n_steps {
    let prev_off = if t > 0 { (t - 1) * n_hidden } else { 0 }
    for h in 0..<n_hidden {
      let mut acc = srnn.b_in[h]
      for j in 0..<n_in {
        acc = acc + srnn.w_in[j * n_hidden + h] * x[t * n_in + j]
      }
      if t > 0 {
        for j in 0..<n_hidden {
          acc = acc + srnn.w_rec[j * n_hidden + h] * spikes[prev_off + j]
        }
      }
      let v_prev = if t > 0 { v[(t - 1) * n_hidden + h] } else { 0.0F }
      let v_t = alpha * v_prev + acc
      v[t * n_hidden + h] = v_t
      spikes[t * n_hidden + h] = if v_t > v_thresh { 1.0F } else { 0.0F }
    }
    // y[t] = W_out @ s[t] + b_out
    for c in 0..<n_classes {
      let mut acc = srnn.b_out[c]
      for h in 0..<n_hidden {
        acc = acc + srnn.w_out[h * n_classes + c] * spikes[t * n_hidden + h]
      }
      y[t * n_classes + c] = acc
    }
  }

  // Softmax + cross-entropy at final timestep.
  let final_off = (n_steps - 1) * n_classes
  let probs : Array[Float] = spikeformer_softmax_slice(y, final_off, n_classes)
  let loss = -logf(probs[target])

  let cache : SRNNCache = {
    n_steps, n_in, n_hidden, n_classes,
    x, v, spikes, y,
    probs,
    target,
    alpha, v_thresh, beta,
  }
  (loss, cache)
}

///|
/// Helper: softmax over a slice [arr[start..start+n]) of a flat array.
fn spikeformer_softmax_slice(
  arr : Array[Float],
  start : Int,
  n : Int,
) -> Array[Float] {
  let mut max_v = arr[start]
  for i in 1..<n {
    if arr[start + i] > max_v {
      max_v = arr[start + i]
    }
  }
  let mut sum_exp = 0.0F
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let e = expf(arr[start + i] - max_v)
    out[i] = e
    sum_exp = sum_exp + e
  }
  for i in 0..<n {
    out[i] = out[i] / sum_exp
  }
  out
}

///|
/// Backward pass. Returns grads as 5-tuple of arrays.
pub fn srnn_backward(
  cache : SRNNCache,
  srnn : SRNN,
) -> (
  Array[Float],  // d_w_in
  Array[Float],  // d_w_rec
  Array[Float],  // d_w_out
  Array[Float],  // d_b_in
  Array[Float],  // d_b_out
) {
  let n_steps = cache.n_steps
  let n_in = cache.n_in
  let n_hidden = cache.n_hidden
  let n_classes = cache.n_classes
  let alpha = cache.alpha
  let v_thresh = cache.v_thresh
  let beta = cache.beta

  // d_y[n_steps-1] = probs - one_hot(target).
  let d_y : Array[Float] = Array::make(n_steps * n_classes, 0.0F)
  let final_off = (n_steps - 1) * n_classes
  for c in 0..<n_classes {
    d_y[final_off + c] = cache.probs[c] -
      (if c == cache.target { 1.0F } else { 0.0F })
  }

  // d_w_out: outer product sum over t of (spikes[t], d_y[t]).
  let d_w_out : Array[Float] = Array::make(n_hidden * n_classes, 0.0F)
  let d_b_out : Array[Float] = Array::make(n_classes, 0.0F)
  for t in 0..<n_steps {
    for c in 0..<n_classes {
      let d_o = d_y[t * n_classes + c]
      d_b_out[c] = d_b_out[c] + d_o
      for h in 0..<n_hidden {
        d_w_out[h * n_classes + c] = d_w_out[h * n_classes + c] +
          cache.spikes[t * n_hidden + h] * d_o
      }
    }
  }

  // d_s[t] = W_out^T @ d_y[t]
  let d_s : Array[Float] = Array::make(n_steps * n_hidden, 0.0F)
  for t in 0..<n_steps {
    for h in 0..<n_hidden {
      let mut acc2 = 0.0F
      for c in 0..<n_classes {
        acc2 = acc2 + srnn.w_out[h * n_classes + c] * d_y[t * n_classes + c]
      }
      d_s[t * n_hidden + h] = acc2
    }
  }

  // BPTT through time: d_v[t] accumulates from d_s[t] (surrogate) +
  // W_rec^T @ d_s[t+1] (recurrent gradient at t+1) + alpha * d_v[t+1]
  // (temporal backflow from leaky integrate).
  let d_v : Array[Float] = Array::make(n_steps * n_hidden, 0.0F)
  for h in 0..<n_hidden {
    d_v[(n_steps - 1) * n_hidden + h] = d_s[(n_steps - 1) * n_hidden + h] *
      fast_sigmoid_surrogate(cache.v[(n_steps - 1) * n_hidden + h] - v_thresh, beta)
  }
  // Backward through time.
  let mut t = n_steps - 2
  while t >= 0 {
    for h in 0..<n_hidden {
      let x = cache.v[t * n_hidden + h] - v_thresh
      let abs_x = if x < 0.0F { -x } else { x }
      let denom = 1.0F + beta * abs_x
      let sigma_prime = 1.0F / (denom * denom)
      let mut d_v_t = d_s[t * n_hidden + h] * sigma_prime
      // + W_rec^T @ d_s[t+1] (from recurrent gradient)
      for j in 0..<n_hidden {
        d_v_t = d_v_t + srnn.w_rec[j * n_hidden + h] * d_s[(t + 1) * n_hidden + j]
      }
      // + alpha * d_v[t+1] (leaky backflow)
      d_v_t = d_v_t + alpha * d_v[(t + 1) * n_hidden + h]
      d_v[t * n_hidden + h] = d_v_t
    }
    t = t - 1
  }

  // d_w_in: outer product sum over t of (x[t], d_v[t]).
  let d_w_in : Array[Float] = Array::make(n_in * n_hidden, 0.0F)
  let d_b_in : Array[Float] = Array::make(n_hidden, 0.0F)
  for t in 0..<n_steps {
    for h in 0..<n_hidden {
      let d_v_h = d_v[t * n_hidden + h]
      d_b_in[h] = d_b_in[h] + d_v_h
      for j in 0..<n_in {
        d_w_in[j * n_hidden + h] = d_w_in[j * n_hidden + h] +
          cache.x[t * n_in + j] * d_v_h
      }
    }
  }

  // d_w_rec: outer product sum over t > 0 of (s[t-1], d_v[t]).
  let d_w_rec : Array[Float] = Array::make(n_hidden * n_hidden, 0.0F)
  for t in 1..<n_steps {
    for h in 0..<n_hidden {
      let d_v_h = d_v[t * n_hidden + h]
      for j in 0..<n_hidden {
        d_w_rec[j * n_hidden + h] = d_w_rec[j * n_hidden + h] +
          cache.spikes[(t - 1) * n_hidden + j] * d_v_h
      }
    }
  }

  (d_w_in, d_w_rec, d_w_out, d_b_in, d_b_out)
}

///|
/// One training step: forward + BPTT + param update (SGD with clip=1.0).
pub fn srnn_train_step(
  image : Array[Float],
  label : Int,
  n_steps : Int,
  lr : Float,
  beta : Float,
  srnn : SRNN,
  rng : Xoshiro,
) -> Float {
  let dt = srnn.dt
  let max_rate = 50.0F
  let x = srnn_poisson_encode(image, n_steps, dt, max_rate, rng)
  let (loss, cache) = srnn_forward(x, n_steps, label, srnn, beta)
  let (d_w_in, d_w_rec, d_w_out, d_b_in, d_b_out) = srnn_backward(cache, srnn)

  clip_grad_array(d_w_in, 1.0F)
  clip_grad_array(d_w_rec, 1.0F)
  clip_grad_array(d_w_out, 1.0F)
  clip_grad_array(d_b_in, 1.0F)
  clip_grad_array(d_b_out, 1.0F)

  for i in 0..<srnn.w_in.length() {
    srnn.w_in[i] = srnn.w_in[i] - lr * d_w_in[i]
  }
  for i in 0..<srnn.w_rec.length() {
    srnn.w_rec[i] = srnn.w_rec[i] - lr * d_w_rec[i]
  }
  for i in 0..<srnn.w_out.length() {
    srnn.w_out[i] = srnn.w_out[i] - lr * d_w_out[i]
  }
  for i in 0..<srnn.b_in.length() {
    srnn.b_in[i] = srnn.b_in[i] - lr * d_b_in[i]
  }
  for i in 0..<srnn.b_out.length() {
    srnn.b_out[i] = srnn.b_out[i] - lr * d_b_out[i]
  }
  loss
}

///|
/// Compute mean loss over the full dataset (no param updates).
pub fn srnn_eval_loss(
  images : Array[Float],
  labels : Array[Int],
  n_steps : Int,
  beta : Float,
  srnn : SRNN,
  seed : UInt64,
) -> Float {
  let n_total = labels.length()
  let n_in = srnn.n_in
  let mut sum_loss = 0.0F
  for i in 0..<n_total {
    let img_off = i * n_in
    let one_img : Array[Float] = Array::make(n_in, 0.0F)
    for j in 0..<n_in {
      one_img[j] = images[img_off + j]
    }
    let seed_i : UInt64 = seed + i.to_uint64() * 1009UL
    let rng = Xoshiro::from_state(seed_i, seed_i + 1UL, seed_i + 2UL, seed_i + 3UL)
    let x = srnn_poisson_encode(one_img, n_steps, srnn.dt, 50.0F, rng)
    let (loss, _) = srnn_forward(x, n_steps, labels[i], srnn, beta)
    sum_loss = sum_loss + loss
  }
  sum_loss / Float::from_int(n_total)
}