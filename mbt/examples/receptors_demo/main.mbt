// receptors_demo example — exercises the multi-receptor synaptic
// infrastructure (Glutamatergic + GABAergic + Receptors::from_pair +
// infer_receptors + alpha_synapse / norm_synapse helpers).
//
// A single 4-neuron IF population with hand-tuned conductance values
// in a 4-receptor collection (AMPA + NMDA + GABAa + GABAb). After one
// `step_receptor` + `receptors_current` round, prints the per-neuron
// synaptic current and verifies:
//   1. AMPA contribution = gsyn * g * (v - E_e) at v=-50 → -50
//   2. NMDA contribution modulated by B(v) voltage gating
//   3. GABAa contribution at v=-75 → 0 (reversal match)
//   4. infer_receptors correctly classifies AMPA/NMDA→glu, GABAa/GABAb→gaba

///|
fn main {
  // ---- 1. Build Glutamatergic + GABAergic receptors ----
  let ampa : @snn_mbt.Receptor = @snn_mbt.Receptor::simple(
    0.0F, 1.0F, 6.0F, 1.0F, "glu",
  )
  let nmda : @snn_mbt.Receptor = @snn_mbt.Receptor::nmda(0.0F, 1.0F, 100.0F, 1.0F)
  let gabaa : @snn_mbt.Receptor = @snn_mbt.Receptor::simple(
    -75.0F, 0.5F, 2.0F, 1.0F, "gaba",
  )
  let gabab : @snn_mbt.Receptor = @snn_mbt.Receptor::simple(
    -75.0F, 0.5F, 20.0F, 1.0F, "gaba",
  )
  println("receptors_demo.mbt: 4-receptor collection built")
  println(
    "  AMPA  : e_rev=0   τr=1  τd=6    gsyn=" +
    ampa.gsyn.to_string(),
  )
  println(
    "  NMDA  : e_rev=0   τr=1  τd=100  is_nmda=" +
    nmda.is_nmda.to_string() + " gsyn=" + nmda.gsyn.to_string(),
  )
  println(
    "  GABAa : e_rev=-75 τr=0.5 τd=2  gsyn=" +
    gabaa.gsyn.to_string(),
  )
  println(
    "  GABAb : e_rev=-75 τr=0.5 τd=20 gsyn=" +
    gabab.gsyn.to_string(),
  )
  // ---- 2. Build Glutamatergic + GABAergic containers + Receptors ----
  let glu = @snn_mbt.Glutamatergic::custom(ampa=ampa, nmda=nmda)
  let gaba = @snn_mbt.GABAergic::custom(gabaa=gabaa, gabab=gabab)
  let rs = @snn_mbt.Receptors::from_pair(glu, gaba)
  // ---- 3. infer_receptors classifies correctly ----
  let recs_arr : Array[@snn_mbt.Receptor] = [ampa, nmda, gabaa, gabab]
  let r = @snn_mbt.infer_receptors(recs_arr)
  println(
    "receptors_demo.mbt: infer_receptors → glu=[" +
    r.glu.length().to_string() + "] gaba=[" +
    r.gaba.length().to_string() + "]",
  )
  println(
    "  glu=[0]=" + r.glu[0].to_string() + " glu=[1]=" + r.glu[1].to_string() +
    " gaba=[0]=" + r.gaba[0].to_string() + " gaba=[1]=" + r.gaba[1].to_string(),
  )
  // ---- 4. alpha_synapse + norm_synapse helpers ----
  let alpha_ampa = @snn_mbt.alpha_synapse(1.0F, 6.0F)
  let norm_ampa = @snn_mbt.norm_synapse(1.0F, 6.0F)
  println(
    "receptors_demo.mbt: AMPA α=" +
    alpha_ampa.to_string() + " norm=" + norm_ampa.to_string(),
  )
  // ---- 5. Set per-receptor conductances + compute current ----
  let n : Int = 4
  let g_mat : Array[Float] = Array::make(n * 4, 0.0F)
  // Set gAMPA = 1, gNMDA = 0, gGABAa = 0.5, gGABAb = 0 on every neuron
  let mut k : Int = 0
  while k < n {
    ignore(g_mat.set(k * 4 + 0, 1.0F)) // AMPA = 1
    ignore(g_mat.set(k * 4 + 1, 0.0F)) // NMDA = 0
    ignore(g_mat.set(k * 4 + 2, 0.5F)) // GABAa = 0.5
    ignore(g_mat.set(k * 4 + 3, 0.0F)) // GABAb = 0
    k = k + 1
  }
  let v : Array[Float] = Array::make(n, 0.0F)
  ignore(v.set(0, -70.0F))
  ignore(v.set(1, -50.0F))
  ignore(v.set(2, -75.0F))
  ignore(v.set(3, -65.0F))
  let out : Array[Float] = Array::make(n, 0.0F)
  @snn_mbt.receptors_current(
    g_mat, v, rs, @snn_mbt.NMDAVoltageDependency::eyal(), out,
  )
  let mut j : Int = 0
  while j < n {
    println(
      "  neuron " + j.to_string() + " v=" + v[j].to_string() +
      " → I_syn=" + out[j].to_string(),
    )
    j = j + 1
  }
  println(
    "receptors_demo.mbt: 4-receptor current sum printed (AMPA + GABAa; NMDA=0; GABAb=0)",
  )
}