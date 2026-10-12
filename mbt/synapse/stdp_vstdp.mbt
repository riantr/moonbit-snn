// stdp_vstdp.mbt — vSTDPParameter (Litwin-Kumar-Doiron 2014
// voltage-dependent STDP variant).
//
// Julia reference (plasticity_params.jl):
//   vSTDPParameter(A_LTD, A_LTP, θ_LTD, θ_LTP, Wmax)
//
// Step rule (per Litwin-Kumar-Doiron 2014, eq. 1):
//   - On pre-synaptic spike: if v_post > θ_LTD, weight += -A_LTD
//     (depression triggered by high post-synaptic voltage).
//   - On post-synaptic spike: if v_pre > θ_LTP, weight += +A_LTP
//     (potentiation triggered by high pre-synaptic voltage).
//
// This is the simplest variant of vSTDP and matches the form used in
// SpikingNeuralNetworks.jl/test/syn/with_plasticity.jl ("vSTDPParameter
// (Npre == Npost)" + "vSTDPParameter (Npre > Npost, x size regression)").
//
// Bit-exact note: this matches Julia's `@turbo`-accelerated loop
// semantically (same condition, same direction of weight change).
// Float32 throughout.

// NOTE: MoonBit requires type names to start with uppercase. We use
// `VstdpParameter` / `VstdpVariables` instead of Julia's lowercase
// `vSTDPParameter` / `vSTDPVariables` (the `v` is preserved in field
// names a_ltd/a_ltp/theta_ltd/theta_ltp but the struct identifier
// gets capitalized).

///|
/// VstdpParameter — Litwin-Kumar-Doiron 2014 voltage-dependent STDP.
pub(all) struct VstdpParameter {
  a_ltd : Float
  a_ltp : Float
  theta_ltd : Float
  theta_ltp : Float
  tau_pre : Float
  tau_post : Float
  w_max : Float
  w_min : Float
}

///|
pub fn VstdpParameter::new() -> VstdpParameter {
  {
    a_ltd: 1.0e-3F,
    a_ltp: 2.0e-3F,
    theta_ltd: 1.0F,
    theta_ltp: 1.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 50.0F,
    w_min: 0.0F,
  }
}

///|
/// VstdpVariables — per-connection weight state for vSTDP.
pub struct VstdpVariables {
  w : Array[Float]
}

///|
pub fn VstdpVariables::new(n_pre : Int, n_post : Int) -> VstdpVariables {
  { w: Array::make(n_pre * n_post, 0.0F) }
}

///|
/// One step of vSTDP: scan pre-fire and post-fire arrays; apply LTD
/// on pre-fires (if v_post > θ_LTD) and LTP on post-fires
/// (if v_pre > θ_LTP). Clamps weights to [w_min, w_max].
pub fn vstdp_step(
  vars : VstdpVariables,
  param : VstdpParameter,
  pre_v : Array[Float],
  post_v : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
) -> Unit {
  let n_pre = pre_v.length()
  let n_post = post_v.length()
  let a_ltd = param.a_ltd
  let a_ltp = param.a_ltp
  let theta_ltd = param.theta_ltd
  let theta_ltp = param.theta_ltp
  let w_max = param.w_max
  let w_min = param.w_min
  let mut j = 0
  while j < n_pre {
    if pre_fire[j] {
      let mut i = 0
      while i < n_post {
        if post_v[i] > theta_ltd {
          let idx = j * n_post + i
          let w_new = vars.w[idx] - a_ltd
          vars.w[idx] = if w_new < w_min { w_min } else { w_new }
        }
        i = i + 1
      }
    }
    j = j + 1
  }
  let mut i = 0
  while i < n_post {
    if post_fire[i] {
      let mut j2 = 0
      while j2 < n_pre {
        if pre_v[j2] > theta_ltp {
          let idx = j2 * n_post + i
          let w_new = vars.w[idx] + a_ltp
          vars.w[idx] = if w_new > w_max { w_max } else { w_new }
        }
        j2 = j2 + 1
      }
    }
    i = i + 1
  }
}

///|
/// Visualise the vSTDP voltage rule: a 2D grid showing which (v_pre, v_post)
/// quadrants produce LTD / LTP / no-change.
pub fn vstdp_plot(
  param : VstdpParameter,
  width? : Int = 50,
  height? : Int = 15,
) -> Unit {
  let n_cols = if width > 1 { width } else { 50 }
  let n_rows = if height > 1 { height } else { 15 }
  let v_lo = -80.0F
  let v_hi = 20.0F
  println(
    "vSTDP rule (param θ_ltd=" + param.theta_ltd.to_string() + " θ_ltp=" +
    param.theta_ltp.to_string() + "):",
  )
  println("  '+' = LTP (v_pre > θ_LTP); '-' = LTD (v_post > θ_LTD); '.' = no-change")
  let mut r = 0
  while r < n_rows {
    let v_post = v_hi - Float::from_int(r) * (v_hi - v_lo) / Float::from_int(n_rows - 1)
    let mut line = ""
    let mut c = 0
    while c < n_cols {
      let v_pre = v_lo + Float::from_int(c) * (v_hi - v_lo) / Float::from_int(n_cols - 1)
      let ch = if v_pre > param.theta_ltp && v_post > param.theta_ltd {
        '+'
      } else if v_pre > param.theta_ltp {
        '+'
      } else if v_post > param.theta_ltd {
        '-'
      } else {
        '.'
      }
      line = line + ch.to_string()
      c = c + 1
    }
    println("  v_post=" + v_post.to_string() + " |" + line + "|")
    r = r + 1
  }
}