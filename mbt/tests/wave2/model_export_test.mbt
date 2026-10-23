// model_export_test.mbt -- hand TARNet's flat parameters to the PyTorch oracle.
//
// WHY THIS FILE EXISTS SEPARATELY FROM THE GATE. `tarnet_flatten` is the only
// source of these numbers: `Tarnet::new` draws from core's Xoshiro, so nothing
// outside MoonBit can reproduce them and no second RNG will agree. The oracle
// therefore needs them handed over, and that handover is the step that was
// missing -- `verify/gen_model_oracle.py` has been in the tree since it was
// written and has never been run, because nothing could run it.
//
// One line, prefixed `TNORACLE`, which is what `parse_params` scans for. The
// encoding is detected rather than assumed on the Python side, because the log
// comes out of PowerShell's `*>` redirection as UTF-16LE.
//
// The fixture below MUST stay identical to the oracle's constants
// (N=3, IN_CHAN=6, T=6, BN_CHAN=FUSE=EMB=ATT=8, CLASSES=4, 3573 parameters) and
// to `tn_model_fixture` in `tarnet_model_test.mbt`. If any of the three drift,
// the golden describes a different network than the one under test -- and a
// golden that quietly describes the wrong model is worse than no golden,
// because it goes on passing.
//
//   moon test -p riantr/snn_mbt/tests/wave2 *> $env:TEMP\tnexp.log
//   python verify/gen_model_oracle.py $env:TEMP\tnexp.log

///|
/// The shape the oracle is hardcoded to. Every number here is duplicated on the
/// Python side; a change to one without the other produces a gate that either
/// panics on a length or, worse, compares against the wrong network.
const TN_EXPORT_CLASSES : Int = 4
const TN_EXPORT_IN_CHAN : Int = 6
const TN_EXPORT_T : Int = 6
const TN_EXPORT_NPARAMS : Int = 3573
const TN_EXPORT_SEED : UInt64 = 141UL

fn tn_export_fixture() -> @tarnet.Tarnet {
  @tarnet.Tarnet::new(
    TN_EXPORT_CLASSES,
    TN_EXPORT_IN_CHAN,
    TN_EXPORT_T,
    width=8,
    hid_chan=8,
    kernel=3,
    repeats_s=1,
    repeats_m=1,
    repeats_l=1,
    fuse_out=8,
    emb_dim=8,
    att_hidden=8,
    seed=TN_EXPORT_SEED,
  )
}

test "TARNet oracle: export the flat parameter vector" {
  let net = tn_export_fixture()
  let n_params = @tarnet.tarnet_param_count(net)
  if n_params != TN_EXPORT_NPARAMS {
    fail("this model has " + n_params.to_string() + " parameters, the oracle is hardcoded to " + TN_EXPORT_NPARAMS.to_string() + "; one of the two changed and the golden would describe a different network")
  }
  let flat = @tarnet.tarnet_flatten(net)
  let mut line = "TNORACLE params "
  let mut i = 0
  while i < flat.length() {
    if i > 0 {
      line = line + " "
    }
    line = line + flat[i].to_string()
    i = i + 1
  }
  // A NaN or an infinity anywhere in the vector would reach Python's float()
  // and become a golden full of NaN, which compares equal to nothing and fails
  // in a way that looks like a model mismatch. Cheaper to stop here, where the
  // cause is obvious.
  if line.contains("NaN") || line.contains("nan") || line.contains("inf") {
    fail("the parameter vector contains a NaN or an infinity")
  }
  println("TNORACLE export: " + flat.length().to_string() + " parameters, seed " + TN_EXPORT_SEED.to_string())
  println(line)
}
