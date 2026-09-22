// izhikevich_debug.mbt — debugging variant of izhikevich.mbt that prints
// per-step voltage trace for bit-exactness verification.
//
// Records v[0] at every step for the first 100 ms and prints
// min/max/mean. Compare against Julia's Izikievich_neuron.jl
// output at the same dt=0.125ms and seed.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0x1234567890ABCDEFUL, 0xFEDCBA0987654321UL)
  let param = @snn_mbt.IZParameter::rs()
  let pop = @snn_mbt.IZ::new(1, param, rng)

  // 10 pA injection
  pop.i[0] = 10.0F

  // Record v[0] every step for 100 ms.
  let dt = 0.125F
  let total_steps : Int = (100.0F / dt).to_int()
  let v_trace : Array[Float] = [pop.v[0]]
  for _ in 0..<total_steps {
    @snn_mbt.step_iz(pop, dt)
    v_trace.push(pop.v[0])
  }

  // Compute basic statistics manually.
  let n = v_trace.length()
  let mut mn = v_trace[0]
  let mut mx = v_trace[0]
  let mut sum = 0.0F
  let mut i = 0
  while i < n {
    if v_trace[i] < mn { mn = v_trace[i] }
    if v_trace[i] > mx { mx = v_trace[i] }
    sum = sum + v_trace[i]
    i = i + 1
  }
  let mean = sum / Float::from_int(n)

  println("izhikevich_debug.mbt: traced v[0] for " + total_steps.to_string() + " steps")
  println("  initial v[0] = " + v_trace[0].to_string())
  let last = v_trace.length() - 1
  println("  final v[0]   = " + v_trace[last].to_string())
  println("  v[0] min     = " + mn.to_string())
  println("  v[0] max     = " + mx.to_string())
  println("  v[0] mean    = " + mean.to_string())
}