// #region Define traits with default implementations

///|
/// is_being_cancelled may be unused if there's no async test
#warnings("-unused_value")
trait MoonBit_Async_Test_Driver {
  fn run_async_tests(@moonbitlang/core/prelude.Array[async () -> Unit]) -> Unit = _
  fn is_being_cancelled() -> Bool = _
}

///|
impl MoonBit_Async_Test_Driver with fn run_async_tests(_) {
  ()
}

///|
impl MoonBit_Async_Test_Driver with fn is_being_cancelled() {
  false
}

///|
trait MoonBit_Test_Driver {
  /// returns if the test is found and executed
  fn run_test(
    @moonbitlang/core/prelude.Array[async () -> Unit],
    String,
    Int,
    () -> Unit,
    (String, String, Bool) -> Unit,
    (Error) -> String,
  ) -> Bool raise MoonBitTestDriverInternalSkipTest = _
}

///|
impl MoonBit_Test_Driver with fn run_test(_, _, _, _, _, _) {
  false
}

// #endregion
// #region Define abstract types for namespace with implementation declarations

///|
priv type MoonBit_Test_Driver_Internal_No_Args

///|
impl MoonBit_Test_Driver for MoonBit_Test_Driver_Internal_No_Args

///|
priv type MoonBit_Test_Driver_Internal_With_Args

///|
impl MoonBit_Test_Driver for MoonBit_Test_Driver_Internal_With_Args

///|
priv type MoonBit_Test_Driver_Internal_Async_No_Args

///|
impl MoonBit_Test_Driver for MoonBit_Test_Driver_Internal_Async_No_Args

///|
priv type MoonBit_Test_Driver_Internal_Async_With_Args

///|
impl MoonBit_Test_Driver for MoonBit_Test_Driver_Internal_Async_With_Args

///|
priv type MoonBit_Test_Driver_Internal_With_Bench_Args

///|
impl MoonBit_Test_Driver for MoonBit_Test_Driver_Internal_With_Bench_Args

///|
priv type MoonBit_Async_Test_Driver_Impl

///|
impl MoonBit_Async_Test_Driver for MoonBit_Async_Test_Driver_Impl

///|
extend MoonBit_Test_Driver_Internal_No_Args with MoonBit_Test_Driver::{run_test}

///|
extend MoonBit_Test_Driver_Internal_With_Args with MoonBit_Test_Driver::{
  run_test,
}

///|
extend MoonBit_Test_Driver_Internal_Async_No_Args with MoonBit_Test_Driver::{
  run_test,
}

///|
extend MoonBit_Test_Driver_Internal_Async_With_Args with MoonBit_Test_Driver::{
  run_test,
}

///|
extend MoonBit_Test_Driver_Internal_With_Bench_Args with MoonBit_Test_Driver::{
  run_test,
}

///|
extend MoonBit_Async_Test_Driver_Impl with MoonBit_Async_Test_Driver::{
  run_async_tests,
  is_being_cancelled,
}

///|
// `is_being_cancelled` is only called from async-test templates. This shared
// scaffold is also generated for packages without async tests, so reference
// the method value to keep strict warning lists from treating the explicit
// extend as unused without invoking cancellation logic during initialization.
fn init {
  ignore(MoonBit_Async_Test_Driver_Impl::is_being_cancelled)
}

// #endregion

// #region Utility Types

///|
/// Maybe unused if there's no tests
#warnings("-unused_type_declaration")
priv struct MoonBitTestDriverInternalTestMap[F](
  @moonbitlang/core/prelude.Map[
    String,
    @moonbitlang/core/prelude.Map[Int, (F, ReadOnlyArray[String])],
  ]
)

///|
priv suberror MoonBitTestDriverInternalSkipTest {
  MoonBitTestDriverInternalSkipTest(name~ : String)
}

// #endregion
///|
fn moonbit_test_driver_internal_error_to_string(x : Error) -> String = "%error.to_string"

///|
#cfg(any(target="native", target="llvm"))
fn string_get(s : String, index : Int) -> Int = "%string_get"

///|
#cfg(not(target="js"))
fn moonbit_unsafe_char_from_int(x : Int) -> Char = "%identity"

///|
#cfg(any(target="native", target="llvm"))
const MOONBIT_TEST_DRIVER_INTERNAL_IS_NATIVE = true

///|
#cfg(not(any(target="native", target="llvm")))
const MOONBIT_TEST_DRIVER_INTERNAL_IS_NATIVE = false

///|
#cfg(all(any(target="native", target="llvm"), platform="windows"))
priv struct MoonBitTestDriverInternalOsString(String)

///|
#cfg(all(any(target="native", target="llvm"), not(platform="windows")))
priv struct MoonBitTestDriverInternalOsString(Bytes)

///|
#cfg(all(any(target="native", target="llvm"), platform="windows"))
fn MoonBitTestDriverInternalOsString::to_string(
  self : MoonBitTestDriverInternalOsString,
) -> String {
  self.0
}

///|
#cfg(all(any(target="native", target="llvm"), not(platform="windows")))
fn MoonBitTestDriverInternalOsString::to_string(
  self : MoonBitTestDriverInternalOsString,
) -> String {
  let bytes = self.0
  let res = @moonbitlang/core/prelude.StringBuilder()
  let len = bytes.length()
  let mut i = 0
  while i < len {
    let mut c = bytes[i].to_int()
    if c < 0x80 {
      res.write_char(moonbit_unsafe_char_from_int(c))
      i += 1
    } else if c < 0xE0 {
      if i + 1 >= len {
        break
      }
      c = ((c & 0x1F) << 6) | (bytes[i + 1].to_int() & 0x3F)
      res.write_char(moonbit_unsafe_char_from_int(c))
      i += 2
    } else if c < 0xF0 {
      if i + 2 >= len {
        break
      }
      c = ((c & 0x0F) << 12) |
        ((bytes[i + 1].to_int() & 0x3F) << 6) |
        (bytes[i + 2].to_int() & 0x3F)
      res.write_char(moonbit_unsafe_char_from_int(c))
      i += 3
    } else {
      if i + 3 >= len {
        break
      }
      c = ((c & 0x07) << 18) |
        ((bytes[i + 1].to_int() & 0x3F) << 12) |
        ((bytes[i + 2].to_int() & 0x3F) << 6) |
        (bytes[i + 3].to_int() & 0x3F)
      c -= 0x10000
      res.write_char(moonbit_unsafe_char_from_int((c >> 10) + 0xD800))
      res.write_char(moonbit_unsafe_char_from_int((c & 0x3FF) + 0xDC00))
      i += 4
    }
  }
  res.to_string()
}

// =========================================================
// ================== BEGIN WASM SPECIFIC ==================
// =========================================================

///|
#cfg(any(target="wasm", target="wasm-gc"))
fn moonbit_test_driver_internal_get_file_name(
  file_name : MoonbitTestDriverInternalExternString,
) -> String {
  moonbit_test_driver_internal_string_from_extern(file_name)
}

///|
#cfg(any(target="wasm", target="wasm-gc"))
#external
type MoonbitTestDriverInternalStringReadHandle

///|
#cfg(any(target="wasm", target="wasm-gc"))
#external
type MoonbitTestDriverInternalExternString

///|
#cfg(any(target="wasm", target="wasm-gc"))
fn moonbit_test_driver_internal_begin_read_string(
  s : MoonbitTestDriverInternalExternString,
) -> MoonbitTestDriverInternalStringReadHandle = "__moonbit_fs_unstable" "begin_read_string"

///|
#cfg(any(target="wasm", target="wasm-gc"))
fn moonbit_test_driver_internal_string_read_char(
  handle : MoonbitTestDriverInternalStringReadHandle,
) -> Int = "__moonbit_fs_unstable" "string_read_char"

///|
#cfg(any(target="wasm", target="wasm-gc"))
fn moonbit_test_driver_internal_finish_read_string(
  handle : MoonbitTestDriverInternalStringReadHandle,
) = "__moonbit_fs_unstable" "finish_read_string"

///|
#cfg(any(target="wasm", target="wasm-gc"))
fn fixedarray_to_bytes(arr : FixedArray[Byte]) -> Bytes = "%identity"

///|
#cfg(any(target="wasm", target="wasm-gc"))
fn moonbit_test_driver_internal_string_from_extern(
  e : MoonbitTestDriverInternalExternString,
) -> String {
  let buf = FixedArray::make(512, b'\x00')
  let mut len = 0
  let handle = moonbit_test_driver_internal_begin_read_string(e)
  while true {
    let ch = moonbit_test_driver_internal_string_read_char(handle)
    if ch == -1 {
      break
    } else {
      let written_bytes_len = buf.set_utf16le_char(
        len,
        moonbit_unsafe_char_from_int(ch),
      )
      len += written_bytes_len
    }
  }
  moonbit_test_driver_internal_finish_read_string(handle)
  fixedarray_to_bytes(buf).to_unchecked_string(offset=0, length=len).to_string()
}

// =========================================================
// =================== END WASM SPECIFIC ===================
// =========================================================

// =========================================================
// ============== BEGIN NATIVE/LLVM SPECIFIC ===============
// =========================================================

///|
#cfg(any(target="native", target="llvm"))
fn moonbit_test_driver_internal_native_parse_args() -> @moonbitlang/core/prelude.Array[
  (String, Int),
] {
  fn moonbit_test_driver_internal_parse_int_(s : String) -> Int {
    let mut res = 0
    let len = s.length()
    for i in 0..<len {
      res = res * 10 + (string_get(s, i) - '0')
    }
    res
  }

  fn moonbit_test_driver_internal_split_mbt_string(
    s : String,
    sep : Char,
  ) -> @moonbitlang/core/prelude.Array[String] {
    let res = []
    let mut i = 0
    let mut start = 0
    while i < s.length() {
      if moonbit_unsafe_char_from_int(string_get(s, i)) == sep {
        res.push(s.unsafe_substring(start~, end=i))
        start = i + 1
      }
      i += 1
    }
    if start < s.length() {
      res.push(s.unsafe_substring(start~, end=s.length()))
    }
    res
  }

  let file_and_index = []
  let cli_args = moonbit_test_driver_internal_get_cli_args_ffi()
  let test_args = moonbit_test_driver_internal_split_mbt_string(
    cli_args[1].to_string(),
    '/',
  )
  for arg in test_args {
    let file_and_range = moonbit_test_driver_internal_split_mbt_string(arg, ':')
    let file = file_and_range[0]
    let range = file_and_range[1]
    let start_and_end = moonbit_test_driver_internal_split_mbt_string(
      range, '-',
    )
    let start = moonbit_test_driver_internal_parse_int_(start_and_end[0])
    let end = moonbit_test_driver_internal_parse_int_(start_and_end[1])
    for i in start..<end {
      file_and_index.push((file, i))
    }
  }
  file_and_index
}

///|
#cfg(any(target="native", target="llvm"))
extern "C" fn moonbit_test_driver_internal_get_cli_args_ffi() -> FixedArray[
  MoonBitTestDriverInternalOsString,
] = "moonbit_rt_get_cli_args"

// =========================================================
// =============== END NATIVE/LLVM SPECIFIC ================
// =========================================================

// =========================================================
// -================== BEGIN JS SPECIFIC ===================
// =========================================================

///|
#cfg(target="js")
#external
type MoonBitTestDriverInternalTestEntry

///|
#cfg(target="js")
extern "js" fn MoonBitTestDriverInternalTestEntry::filename(
  self : Self,
) -> String =
  #| (entry) => entry[0]

///|
#cfg(target="js")
extern "js" fn MoonBitTestDriverInternalTestEntry::index(self : Self) -> Int =
  #| (entry) => entry[1]

///|
/// JavaScript error captured by the generated test driver.
pub(all) suberror MoonBitTestDriverInternalJsError {
  MoonBitTestDriverInternalJsError(String)
}

///|
/// Maybe unused if there's no tests
#cfg(target="js")
#warnings("-unused_value")
fn moonbit_test_driver_internal_catch_error(
  f : () -> Unit raise,
  on_ok~ : () -> Unit,
  on_err~ : (Error) -> Unit,
) -> Unit {
  moonbit_test_driver_internal_js_catch(
    on_err=err_str => on_err(MoonBitTestDriverInternalJsError(err_str)),
    () => {
      try f() catch {
        err => on_err(err)
      } noraise {
        _ => on_ok()
      }
    },
  )
}

///|
#cfg(target="js")
extern "js" fn moonbit_test_driver_internal_js_catch(
  f : () -> Unit,
  on_err~ : (String) -> Unit,
) =
  #| (f, on_err) => {
  #|   try {
  #|     f()
  #|   } catch (err) {
  #|     const msg = err.stack.toString()
  #|     on_err(msg)
  #|   }
  #| }

///|
/// Maybe unused if there's no tests
#cfg(not(target="js"))
#warnings("-unused_value")
fn moonbit_test_driver_internal_catch_error(
  f : () -> Unit raise,
  on_ok~ : () -> Unit,
  on_err~ : (Error) -> Unit,
) -> Unit {
  try f() catch {
    err => on_err(err)
  } noraise {
    _ => on_ok()
  }
}

// =========================================================
// -=================== END JS SPECIFIC ====================
// =========================================================
// This file is introduced when tests without args is needed

///|
let moonbit_test_driver_internal_no_args_tests : MoonBitTestDriverInternalTestMap[
  () -> Unit raise,
] = {
  "vecplot_test.mbt": {
    0: (__test_766563706c6f745f746573742e6d6274_0, ["count_spikes counts rising edges"]),
    1: (__test_766563706c6f745f746573742e6d6274_1, ["count_spikes on empty monitor is 0"]),
    2: (__test_766563706c6f745f746573742e6d6274_2, ["dump_summary prints min/max/mean"]),
    3: (__test_766563706c6f745f746573742e6d6274_3, ["duration returns t[-1] - t[0]"]),
    4: (__test_766563706c6f745f746573742e6d6274_4, ["duration of empty monitor is 0"]),
    5: (__test_766563706c6f745f746573742e6d6274_5, ["ascii_plot renders without crashing on a v monitor"]),
    6: (__test_766563706c6f745f746573742e6d6274_6, ["ascii_plot renders spike markers for fire monitor"]),
    7: (__test_766563706c6f745f746573742e6d6274_7, ["ascii_plot on empty monitor is a no-op"]),
    8: (__test_766563706c6f745f746573742e6d6274_8, ["firing_rate computes Hz from spikes and duration"]),
    9: (__test_766563706c6f745f746573742e6d6274_9, ["firing_rate on empty monitor is 0"]),
    10: (__test_766563706c6f745f746573742e6d6274_10, ["firing_rate on zero-duration monitor is 0"]),
    11: (__test_766563706c6f745f746573742e6d6274_11, ["spike_times returns timestamps at rising edges"]),
    12: (__test_766563706c6f745f746573742e6d6274_12, ["spike_times on empty monitor returns empty array"]),
    13: (__test_766563706c6f745f746573742e6d6274_13, ["count_spikes_interval filters by time window"]),
    14: (__test_766563706c6f745f746573742e6d6274_14, ["mean / min / max compute basic statistics"]),
    15: (__test_766563706c6f745f746573742e6d6274_15, ["mean on empty monitor is 0"]),
    16: (__test_766563706c6f745f746573742e6d6274_16, ["firing_rate_interval computes Hz in sub-window"]),
    17: (__test_766563706c6f745f746573742e6d6274_17, ["clear_records resets data and times"]),
  },
  "util_test.mbt": {
    0: (__test_7574696c5f746573742e6d6274_0, ["rand_value — length matches n"]),
    1: (__test_7574696c5f746573742e6d6274_1, ["rand_value — all values in [min, max]"]),
    2: (__test_7574696c5f746573742e6d6274_2, ["rand_value — handles reversed p1 > p2"]),
    3: (__test_7574696c5f746573742e6d6274_3, ["rand_value — degenerate p1 == p2 returns all p1"]),
    4: (__test_7574696c5f746573742e6d6274_4, ["exp32(0) == 1.0"]),
    5: (__test_7574696c5f746573742e6d6274_5, ["exp32(1) ~ exp(1)"]),
    6: (__test_7574696c5f746573742e6d6274_6, ["exp32(-1) ~ exp(-1)"]),
    7: (__test_7574696c5f746573742e6d6274_7, ["exp32(-20) > 0 (clamped to -10)"]),
    8: (__test_7574696c5f746573742e6d6274_8, ["exp64(0) == 1.0"]),
    9: (__test_7574696c5f746573742e6d6274_9, ["exp256(0) == 1.0"]),
    10: (__test_7574696c5f746573742e6d6274_10, ["name — two-argument form"]),
    11: (__test_7574696c5f746573742e6d6274_11, ["name — three-argument form with k"]),
    12: (__test_7574696c5f746573742e6d6274_12, ["str_name — two-argument form"]),
    13: (__test_7574696c5f746573742e6d6274_13, ["str_name — three-argument form with k"]),
    14: (__test_7574696c5f746573742e6d6274_14, ["str_name_single — one-argument form"]),
    15: (__test_7574696c5f746573742e6d6274_15, ["str_name_single — two-argument form with k"]),
    16: (__test_7574696c5f746573742e6d6274_16, ["f2l — pads short string to default 10"]),
    17: (__test_7574696c5f746573742e6d6274_17, ["f2l — pads to custom length"]),
    18: (__test_7574696c5f746573742e6d6274_18, ["f2l — truncates long string"]),
    19: (__test_7574696c5f746573742e6d6274_19, ["f2l — exact length returned unchanged"]),
    20: (__test_7574696c5f746573742e6d6274_20, ["f2l — pads numeric-looking string"]),
  },
  "units_test.mbt": {
    0: (__test_756e6974735f746573742e6d6274_0, ["length units"]),
    1: (__test_756e6974735f746573742e6d6274_1, ["time units"]),
    2: (__test_756e6974735f746573742e6d6274_2, ["voltage units"]),
    3: (__test_756e6974735f746573742e6d6274_3, ["current units"]),
    4: (__test_756e6974735f746573742e6d6274_4, ["capacitance units"]),
    5: (__test_756e6974735f746573742e6d6274_5, ["conductance units"]),
    6: (__test_756e6974735f746573742e6d6274_6, ["resistance units"]),
    7: (__test_756e6974735f746573742e6d6274_7, ["concentration units"]),
  },
  "turnover_test.mbt": {
    0: (__test_7475726e6f7665725f746573742e6d6274_0, ["RandomTurnover::new defaults"]),
    1: (__test_7475726e6f7665725f746573742e6d6274_1, ["RandomTurnover::new custom threshold + mu"]),
    2: (__test_7475726e6f7665725f746573742e6d6274_2, ["RandomTurnover::new rate → tau derivation"]),
    3: (__test_7475726e6f7665725f746573742e6d6274_3, ["ActivityDependentTurnover::new defaults"]),
    4: (__test_7475726e6f7665725f746573742e6d6274_4, ["ActivityDependentTurnover::new custom fraction"]),
    5: (__test_7475726e6f7665725f746573742e6d6274_5, ["Turnover::new initial state"]),
    6: (__test_7475726e6f7665725f746573742e6d6274_6, ["turnover_plasticity — τ=0 disables periodic gate"]),
    7: (__test_7475726e6f7665725f746573742e6d6274_7, ["turnover_plasticity — period_steps fires"]),
    8: (__test_7475726e6f7665725f746573742e6d6274_8, ["turnover_plasticity — ActivityDependent pre/post trace decay"]),
    9: (__test_7475726e6f7665725f746573742e6d6274_9, ["synaptic_turnover — no candidates when p_values > threshold"]),
    10: (__test_7475726e6f7665725f746573742e6d6274_10, ["synaptic_turnover — rewires when p_values < threshold"]),
    11: (__test_7475726e6f7665725f746573742e6d6274_11, ["synaptic_turnover — quantiles handle empty array"]),
  },
  "time_test.mbt": {
    0: (__test_74696d655f746573742e6d6274_0, ["Time::new"]),
    1: (__test_74696d655f746573742e6d6274_1, ["Time::from_ms"]),
    2: (__test_74696d655f746573742e6d6274_2, ["update_time"]),
    3: (__test_74696d655f746573742e6d6274_3, ["reset_time"]),
  },
  "synapse_params_test.mbt": {
    0: (__test_73796e617073655f706172616d735f746573742e6d6274_0, ["DeltaSynapse::new: empty struct (no parameters)"]),
    1: (__test_73796e617073655f706172616d735f746573742e6d6274_1, ["DeltaSynapseVars::new: zero-initialised"]),
    2: (__test_73796e617073655f706172616d735f746573742e6d6274_2, ["delta_synapse_step: ge/gi accumulate from glu/gaba, then reset"]),
    3: (__test_73796e617073655f706172616d735f746573742e6d6274_3, ["SingleExpSynapse::new: defaults match Julia"]),
    4: (__test_73796e617073655f706172616d735f746573742e6d6274_4, ["SingleExpSynapseVars::new: zero-init + step decay"]),
    5: (__test_73796e617073655f706172616d735f746573742e6d6274_5, ["conductance_synapse_current: syn_curr = ge*(v-E_e)*gsyn_e + gi*(v-E_i)*gsyn_i"]),
    6: (__test_73796e617073655f706172616d735f746573742e6d6274_6, ["DoubleExpSynapse::new: defaults match Julia"]),
    7: (__test_73796e617073655f706172616d735f746573742e6d6274_7, ["DoubleExpSynapseVars::new: zero-init, all four arrays"]),
    8: (__test_73796e617073655f706172616d735f746573742e6d6274_8, ["double_exp_synapse_step: 2-state ODE integrates spike input"]),
    9: (__test_73796e617073655f706172616d735f746573742e6d6274_9, ["CurrentSynapse::new: defaults match Julia"]),
    10: (__test_73796e617073655f706172616d735f746573742e6d6274_10, ["CurrentSynapseVars::new: zero-init + step decay"]),
    11: (__test_73796e617073655f706172616d735f746573742e6d6274_11, ["current_synapse_current: syn_curr = -(ge - gi)"]),
  },
  "structs_extra_test.mbt": {
    0: (__test_737472756374735f65787472615f746573742e6d6274_0, ["EmptyParam::new defaults"]),
    1: (__test_737472756374735f65787472615f746573742e6d6274_1, ["EmptyParam::with_type custom"]),
    2: (__test_737472756374735f65787472615f746573742e6d6274_2, ["NetworkModel::new is_valid = true"]),
    3: (__test_737472756374735f65787472615f746573742e6d6274_3, ["Time::from_ms — 100ms → tt = 800"]),
    4: (__test_737472756374735f65787472615f746573742e6d6274_4, ["Time::from_ms — 0.5ms → tt = 4"]),
    5: (__test_737472756374735f65787472615f746573742e6d6274_5, ["Time::update_time advances correctly"]),
  },
  "stp_timestep_test.mbt": {
    0: (__test_7374705f74696d65737465705f746573742e6d6274_0, ["MarkramSTPParameterTimestep::new: defaults match Julia"]),
    1: (__test_7374705f74696d65737465705f746573742e6d6274_1, ["MarkramSTPEntryTimestep::new: fields populated, vars initialised"]),
    2: (__test_7374705f74696d65737465705f746573742e6d6274_2, ["markram_stp_step_timestep: no-spike continuous decay toward baseline"]),
    3: (__test_7374705f74696d65737465705f746573742e6d6274_3, ["markram_stp_step_timestep: spike bump before continuous update"]),
    4: (__test_7374705f74696d65737465705f746573742e6d6274_4, ["markram_stp_step_timestep: rho broadcast to all outgoing connections"]),
    5: (__test_7374705f74696d65737465705f746573742e6d6274_5, ["markram_stp_step_timestep: rho stays in (0, 1) bounds over time"]),
    6: (__test_7374705f74696d65737465705f746573742e6d6274_6, ["markram_stp_step_timestep: weights stay finite over many steps"]),
    7: (__test_7374705f74696d65737465705f746573742e6d6274_7, ["markram_stp_step_timestep: inactive flag disables rule"]),
  },
  "stp_test.mbt": {
    0: (__test_7374705f746573742e6d6274_0, ["MarkramSTPParameter::new: defaults match SNN.jl (200ms, 1500ms, 0.2)"]),
    1: (__test_7374705f746573742e6d6274_1, ["MarkramSTPVariables::new: arrays sized n_pre, initial state"]),
    2: (__test_7374705f746573742e6d6274_2, ["MarkramSTPVariables::new: custom parameter reflected in init"]),
    3: (__test_7374705f746573742e6d6274_3, ["markram_stp_step: first spike primes state — first ρ broadcast = U"]),
    4: (__test_7374705f746573742e6d6274_4, ["markram_stp_step: forward_synapse applies ρ scaling on first spike"]),
    5: (__test_7374705f746573742e6d6274_5, ["markram_stp_step: long ΔT restores u → U and x → 1 (full recovery)"]),
    6: (__test_7374705f746573742e6d6274_6, ["markram_stp_step: short ΔT — depression visible (x drops further)"]),
    7: (__test_7374705f746573742e6d6274_7, ["markram_stp_step: ρ broadcasts to all outgoing connections"]),
    8: (__test_7374705f746573742e6d6274_8, ["markram_stp_step: pre[0] fires, pre[1] silent — only pre[0] state updates"]),
    9: (__test_7374705f746573742e6d6274_9, ["compose with STPEntryKind: scales post.he by ρ automatically"]),
    10: (__test_7374705f746573742e6d6274_10, ["markram_stp_step: active=[false] is a no-op"]),
    11: (__test_7374705f746573742e6d6274_11, ["forward_synapse: rho empty — applies full weight (existing behavior)"]),
    12: (__test_7374705f746573742e6d6274_12, ["MarkramSTPParameterHet::homogeneous: matches MarkramSTPParameter defaults per-pre"]),
    13: (__test_7374705f746573742e6d6274_13, ["MarkramSTPEntryHet::new: initial state uses per-pre U[j]"]),
    14: (__test_7374705f746573742e6d6274_14, ["markram_stp_step_het: first spike primes state with per-pre U[j] and τF[j]/τD[j]"]),
    15: (__test_7374705f746573742e6d6274_15, ["markram_stp_step_het: per-pre params produce different recoveries at same ΔT"]),
    16: (__test_7374705f746573742e6d6274_16, ["compose with STPEntryKind::MarkramSTPHet_: dispatches per-pre params"]),
    17: (__test_7374705f746573742e6d6274_17, ["markram_stp_step_het: rho empty — silent no-op (backward-compat guard)"]),
    18: (__test_7374705f746573742e6d6274_18, ["bit-exactness: 3-spike trajectory @ ISI=10ms matches analytical formula"]),
    19: (__test_7374705f746573742e6d6274_19, ["bit-exactness: long ΔT (100000ms) — expf underflows to 0 for Float32"]),
  },
  "stimulus_timed_test.mbt": {
    0: (__test_7374696d756c75735f74696d65645f746573742e6d6274_0, ["SpikeTimeParameter::new sorts by spike time"]),
    1: (__test_7374696d756c75735f74696d65645f746573742e6d6274_1, ["SpikeTimeParameter::new keeps already-sorted input"]),
    2: (__test_7374696d756c75735f74696d65645f746573742e6d6274_2, ["stimulate_spiketime deposits weight at each spike time"]),
    3: (__test_7374696d756c75735f74696d65645f746573742e6d6274_3, ["stimulate_spiketime handles multiple spikes in one step"]),
  },
  "stimulus_poisson_test.mbt": {
    0: (__test_7374696d756c75735f706f6973736f6e5f746573742e6d6274_0, ["sample_poisson(0) = 0"]),
    1: (__test_7374696d756c75735f706f6973736f6e5f746573742e6d6274_1, ["sample_poisson(negative) = 0"]),
    2: (__test_7374696d756c75735f706f6973736f6e5f746573742e6d6274_2, ["sample_poisson mean is approximately lambda"]),
    3: (__test_7374696d756c75735f706f6973736f6e5f746573742e6d6274_3, ["ornstein_uhlenbeck_step: stationary mean ~ μ"]),
    4: (__test_7374696d756c75735f706f6973736f6e5f746573742e6d6274_4, ["ornstein_uhlenbeck_step: clamps negative to 0"]),
  },
  "stimulus_poisson_layer_tripod_test.mbt": {
    0: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_0, ["PoissonLayerStimulusTripod::new: shapes and target fields"]),
    1: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_1, ["PoissonLayerStimulusTripod: p=1.0 → all weights = μ (Fixed)"]),
    2: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_2, ["stimulate_layer_tripod: high rate → glu_d1 accumulates"]),
    3: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_3, ["stimulate_layer_tripod: :gaba target → gaba_d1 buffer"]),
    4: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_4, ["stimulate_layer_tripod: soma target → glu_s only"]),
    5: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_5, ["stimulate_layer_tripod: zero rate → no conductance change"]),
    6: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f747269706f645f746573742e6d6274_6, ["stimulate_layer_tripod: inactive source → no fire"]),
  },
  "stimulus_poisson_layer_test.mbt": {
    0: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_0, ["PoissonLayer::new: defaults"]),
    1: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_1, ["PoissonLayer::with_n: N sources all active"]),
    2: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_2, ["PoissonLayer::with_active: respects inactive flags"]),
    3: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_3, ["PoissonLayer::with_conn: full parameter set"]),
    4: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_4, ["PoissonLayerStimulus::new: weight/connectivity shapes"]),
    5: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_5, ["PoissonLayerStimulus::new: Fixed weights = μ"]),
    6: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_6, ["PoissonLayerStimulus::new: Normal weights have mean ≈ μ"]),
    7: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_7, ["stimulate_layer: zero rate → no fire, no conductance change"]),
    8: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_8, ["stimulate_layer: inactive source → no fire"]),
    9: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_9, ["stimulate_layer: high rate fires, weight accumulates on post"]),
    10: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f746573742e6d6274_10, ["stimulate_layer: writes to gaba for :gi sym"]),
  },
  "stimulus_poisson_layer_ballandstick_test.mbt": {
    0: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f62616c6c616e64737469636b5f746573742e6d6274_0, ["PoissonLayerStimulusBallAndStick::new: shapes and target fields"]),
    1: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f62616c6c616e64737469636b5f746573742e6d6274_1, ["PoissonLayerStimulusBallAndStick: p=1.0 → Fixed weights = μ"]),
    2: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f62616c6c616e64737469636b5f746573742e6d6274_2, ["stimulate_layer_ball: high rate → glu_d accumulates"]),
    3: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f62616c6c616e64737469636b5f746573742e6d6274_3, ["stimulate_layer_ball: soma target → glu_s only"]),
    4: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f62616c6c616e64737469636b5f746573742e6d6274_4, ["stimulate_layer_ball: :gaba target → gaba_d"]),
    5: (__test_7374696d756c75735f706f6973736f6e5f6c617965725f62616c6c616e64737469636b5f746573742e6d6274_5, ["stimulate_layer_ball: zero rate → no conductance change"]),
  },
  "stimulus_group_test.mbt": {
    0: (__test_7374696d756c75735f67726f75705f746573742e6d6274_0, ["StimulusGroup::new — default name + active all true"]),
    1: (__test_7374696d756c75735f67726f75705f746573742e6d6274_1, ["StimulusGroup::new — custom name"]),
    2: (__test_7374696d756c75735f67726f75705f746573742e6d6274_2, ["StimulusGroup::n_elements — count"]),
    3: (__test_7374696d756c75735f67726f75705f746573742e6d6274_3, ["StimulusGroup::n_elements — empty group"]),
    4: (__test_7374696d756c75735f67726f75705f746573742e6d6274_4, ["StimulusGroup::set_active_all — toggles all"]),
    5: (__test_7374696d756c75735f67726f75705f746573742e6d6274_5, ["StimulusGroup::set_active_one — single toggle"]),
    6: (__test_7374696d756c75735f67726f75705f746573742e6d6274_6, ["StimulusGroup::is_active — reflects set_active calls"]),
    7: (__test_7374696d756c75735f67726f75705f746573742e6d6274_7, ["StimulusGroup::tag — get element name"]),
  },
  "stimulus_current_noise_test.mbt": {
    0: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_0, ["CurrentNoise::new — all zeros"]),
    1: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_1, ["CurrentNoise::custom — explicit values"]),
    2: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_2, ["stimulate_noise — α=0: I = I_base + noise"]),
    3: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_3, ["stimulate_noise — α=0, σ>0: I varies due to noise"]),
    4: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_4, ["stimulate_noise — α=1: I holds previous value (no update)"]),
    5: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_5, ["stimulate_noise — α=0.5: 50/50 blend with previous"]),
    6: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_6, ["stimulate_noise — 1000 steps stays bounded with decay"]),
    7: (__test_7374696d756c75735f63757272656e745f6e6f6973655f746573742e6d6274_7, ["stimulate_noise — same seed → same output (deterministic)"]),
  },
  "stimulus_balanced_test.mbt": {
    0: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_0, ["BalancedParameter: defaults match Julia"]),
    1: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_1, ["BalancedParameter: custom values"]),
    2: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_2, ["BalancedStimulus::new: zero-initialized state"]),
    3: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_3, ["BalancedStimulus::new: drives post.receptors (alias-check via stimulate)"]),
    4: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_4, ["stimulate_balanced: writes to ge/gi, fires sometimes"]),
    5: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_5, ["stimulate_balanced: zero rate 鈫?no change"]),
    6: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_6, ["stimulate_balanced: rate adaptation drives r toward r0"]),
    7: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_7, ["stimulate_balanced: same_input=true broadcasts ge to all neurons"]),
    8: (__test_7374696d756c75735f62616c616e6365645f746573742e6d6274_8, ["reset_balanced: clears r, noise, fire"]),
  },
  "stdp_vstdp_test.mbt": {
    0: (__test_737464705f76737464705f746573742e6d6274_0, ["VstdpParameter::new: defaults match Julia"]),
    1: (__test_737464705f76737464705f746573742e6d6274_1, ["VstdpVariables::new: shape and zeros"]),
    2: (__test_737464705f76737464705f746573742e6d6274_2, ["vstdp_step: pre-fire + high v_post → LTD"]),
    3: (__test_737464705f76737464705f746573742e6d6274_3, ["vstdp_step: post-fire + high v_pre → LTP"]),
    4: (__test_737464705f76737464705f746573742e6d6274_4, ["vstdp_step: low voltages → no weight change"]),
    5: (__test_737464705f76737464705f746573742e6d6274_5, ["vstdp_step: weight clamps at w_max (LTP)"]),
    6: (__test_737464705f76737464705f746573742e6d6274_6, ["vstdp_step: weight clamps at w_min (LTD)"]),
    7: (__test_737464705f76737464705f746573742e6d6274_7, ["vstdp_step: both pre and post fire same step (LTD then LTP)"]),
    8: (__test_737464705f76737464705f746573742e6d6274_8, ["vstdp_step: multi-pre multi-post (Julia vSTDP test pattern)"]),
  },
  "stdp_test.mbt": {
    0: (__test_737464705f746573742e6d6274_0, ["STDPGerstner::new defaults are sensible"]),
    1: (__test_737464705f746573742e6d6274_1, ["STDPVariables::new initializes to zeros"]),
    2: (__test_737464705f746573742e6d6274_2, ["stdp_step: pre-then-post potentiation"]),
    3: (__test_737464705f746573742e6d6274_3, ["stdp_step: post-then-pre depression"]),
    4: (__test_737464705f746573742e6d6274_4, ["stdp_step: weight clamp prevents runaway"]),
    5: (__test_737464705f746573742e6d6274_5, ["STDPEntry::new constructs a valid entry"]),
    6: (__test_737464705f746573742e6d6274_6, ["compose with stdp applies plasticity automatically"]),
    7: (__test_737464705f746573742e6d6274_7, ["gerstner_kernel: LTP for dt > 0"]),
    8: (__test_737464705f746573742e6d6274_8, ["gerstner_kernel: LTD for dt < 0"]),
    9: (__test_737464705f746573742e6d6274_9, ["gerstner_kernel: zero at dt = 0"]),
    10: (__test_737464705f746573742e6d6274_10, ["gerstner_kernel: decays with distance"]),
    11: (__test_737464705f746573742e6d6274_11, ["stdp_weight_decorrelated returns A_pre*tau_pre - A_post*tau_post"]),
    12: (__test_737464705f746573742e6d6274_12, ["STDPMexicanHat::new defaults are sensible"]),
    13: (__test_737464705f746573742e6d6274_13, ["mexican_hat_kernel: x=0 -> 1"]),
    14: (__test_737464705f746573742e6d6274_14, ["mexican_hat_kernel: x=1 -> 0"]),
    15: (__test_737464705f746573742e6d6274_15, ["mexican_hat_kernel: x>1 -> negative (LTD regime)"]),
    16: (__test_737464705f746573742e6d6274_16, ["mexican_hat_kernel: NaN guard returns 0"]),
    17: (__test_737464705f746573742e6d6274_17, ["STDPMexicanHat step arrays initialise to zeros"]),
    18: (__test_737464705f746573742e6d6274_18, ["stdp_mexican_hat_step: pre-then-post potentiation (small)"]),
    19: (__test_737464705f746573742e6d6274_19, ["stdp_mexican_hat_step: both fire -> both spike passes apply"]),
    20: (__test_737464705f746573742e6d6274_20, ["stdp_mexican_hat_step: trace decay with dt"]),
    21: (__test_737464705f746573742e6d6274_21, ["stdp_mexican_hat_step: clamp at w_max"]),
    22: (__test_737464705f746573742e6d6274_22, ["STDPAntiSymmetric::new defaults are sensible"]),
    23: (__test_737464705f746573742e6d6274_23, ["STDPAntiSymmetricVariables::new initializes to zeros"]),
    24: (__test_737464705f746573742e6d6274_24, ["stdp_antisymmetric_step: pre spike reduces W via to_y trace"]),
    25: (__test_737464705f746573742e6d6274_25, ["stdp_antisymmetric_step: post spike increases W via tr_x trace"]),
    26: (__test_737464705f746573742e6d6274_26, ["stdp_antisymmetric_step: both fire -> both updates apply"]),
    27: (__test_737464705f746573742e6d6274_27, ["find_pre_for_conn: binary search rowptr"]),
    28: (__test_737464705f746573742e6d6274_28, ["stdp_mexican_hat_step: 2x2 all-to-all post-spike path"]),
    29: (__test_737464705f746573742e6d6274_29, ["compose dispatches STDPMexicanHat entry automatically"]),
    30: (__test_737464705f746573742e6d6274_30, ["compose dispatches STDPAntiSymmetric entry automatically"]),
    31: (__test_737464705f746573742e6d6274_31, ["STDPEntryKind variants are pub(all)"]),
  },
  "stdp_symmetric_test.mbt": {
    0: (__test_737464705f73796d6d65747269635f746573742e6d6274_0, ["STDPSymmetric::new: defaults match Julia"]),
    1: (__test_737464705f73796d6d65747269635f746573742e6d6274_1, ["STDPSymmetric::new: custom values"]),
    2: (__test_737464705f73796d6d65747269635f746573742e6d6274_2, ["STDPSymmetricVariables::new: zero-initialised, all four traces"]),
    3: (__test_737464705f73796d6d65747269635f746573742e6d6274_3, ["STDPEntrySymmetric::new: fields populated"]),
    4: (__test_737464705f73796d6d65747269635f746573742e6d6274_4, ["STDPEntrySymmetric::change_plasticity: swaps param, preserves vars"]),
    5: (__test_737464705f73796d6d65747269635f746573742e6d6274_5, ["stdp_symmetric_step: pre spike adds alpha_pre + (A_x*to_x - A_y*to_y)/..."]),
    6: (__test_737464705f73796d6d65747269635f746573742e6d6274_6, ["stdp_symmetric_step: post spike adds alpha_post + (A_x*tr_x - A_y*tr_y)/..."]),
    7: (__test_737464705f73796d6d65747269635f746573742e6d6274_7, ["stdp_symmetric_step: weight clamps at w_max (LTP side)"]),
    8: (__test_737464705f73796d6d65747269635f746573742e6d6274_8, ["stdp_symmetric_step: weights stay finite over many steps"]),
    9: (__test_737464705f73796d6d65747269635f746573742e6d6274_9, ["IstdpTime::new: defaults match Julia"]),
    10: (__test_737464705f73796d6d65747269635f746573742e6d6274_10, ["IstdpTime: custom fields preserved"]),
  },
  "stdp_confavreux_test.mbt": {
    0: (__test_737464705f636f6e666176726575785f746573742e6d6274_0, ["STDPConfavreux2025::new: defaults match Julia"]),
    1: (__test_737464705f636f6e666176726575785f746573742e6d6274_1, ["STDPEntryConfavreux2025::new: vars zero-initialised"]),
    2: (__test_737464705f636f6e666176726575785f746573742e6d6274_2, ["STDPEntryConfavreux2025::change_plasticity: swaps param, preserves vars"]),
    3: (__test_737464705f636f6e666176726575785f746573742e6d6274_3, ["stdp_confavreux_step: pre spike adds kappa * tpost[i] + alpha"]),
    4: (__test_737464705f636f6e666176726575785f746573742e6d6274_4, ["stdp_confavreux_step: post spike adds gamma * tpre[j] + beta"]),
    5: (__test_737464705f636f6e666176726575785f746573742e6d6274_5, ["stdp_confavreux_step: pre + post both fire same step 鈥?additive"]),
    6: (__test_737464705f636f6e666176726575785f746573742e6d6274_6, ["stdp_confavreux_step: weight clamps at w_max (LTP side)"]),
    7: (__test_737464705f636f6e666176726575785f746573742e6d6274_7, ["stdp_confavreux_step: weights stay finite over many steps"]),
    8: (__test_737464705f636f6e666176726575785f746573742e6d6274_8, ["stdp_confavreux_step: integration 鈥?random pre/post spikes"]),
  },
  "stdp_change_plasticity_test.mbt": {
    0: (__test_737464705f6368616e67655f706c61737469636974795f746573742e6d6274_0, ["STDPEntry::change_plasticity: swap STDPGerstner parameters"]),
    1: (__test_737464705f6368616e67655f706c61737469636974795f746573742e6d6274_1, ["STDPEntry::change_plasticity: preserves vars state"]),
    2: (__test_737464705f6368616e67655f706c61737469636974795f746573742e6d6274_2, ["STDPEntryMexicanHat::change_plasticity: swap parameters"]),
    3: (__test_737464705f6368616e67655f706c61737469636974795f746573742e6d6274_3, ["STDPEntryAntiSymmetric::change_plasticity: swap parameters"]),
    4: (__test_737464705f6368616e67655f706c61737469636974795f746573742e6d6274_4, ["STDPEntry::change_plasticity: subsequent steps use new params"]),
  },
  "spatial_test.mbt": {
    0: (__test_7370617469616c5f746573742e6d6274_0, ["place_populations - random placement (E=40, I=10)"]),
    1: (__test_7370617469616c5f746573742e6d6274_1, ["periodic_distance_scalar"]),
    2: (__test_7370617469616c5f746573742e6d6274_2, ["linear_network - default ring (N=20)"]),
    3: (__test_7370617469616c5f746573742e6d6274_3, ["linear_network_with - custom sigma_w=0.5, w_max=3.0 (N=16)"]),
  },
  "sparse_matrix_test.mbt": {
    0: (__test_7370617273655f6d61747269785f746573742e6d6274_0, ["SparseMatrixCSR::empty"]),
    1: (__test_7370617273655f6d61747269785f746573742e6d6274_1, ["SparseMatrixCSR::from_dense preserves non-zeros"]),
    2: (__test_7370617273655f6d61747269785f746573742e6d6274_2, ["SparseMatrixCSR::set inserts new value"]),
    3: (__test_7370617273655f6d61747269785f746573742e6d6274_3, ["SparseMatrixCSR::set replaces existing value"]),
    4: (__test_7370617273655f6d61747269785f746573742e6d6274_4, ["SparseMatrixCSR::set across multiple rows"]),
    5: (__test_7370617273655f6d61747269785f746573742e6d6274_5, ["SparseMatrixCSR::forward only fires on pre spikes"]),
    6: (__test_7370617273655f6d61747269785f746573742e6d6274_6, ["SparseMatrixCSR::random produces expected nnz"]),
    7: (__test_7370617273655f6d61747269785f746573742e6d6274_7, ["SparseMatrixCSR::random — fixed seed is deterministic"]),
    8: (__test_7370617273655f6d61747269785f746573742e6d6274_8, ["ConnectRule::FixedIn — every post has same in-degree"]),
    9: (__test_7370617273655f6d61747269785f746573742e6d6274_9, ["ConnectRule::FixedOut — every pre has same out-degree"]),
    10: (__test_7370617273655f6d61747269785f746573742e6d6274_10, ["ConnectRule::Bernoulli — matches default random()"]),
  },
  "sparse_matrix_extra_test.mbt": {
    0: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_0, ["matrix_size — Npre=4, Npost=2 synapse"]),
    1: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_1, ["matrix_get — returns the weight for an existing connection"]),
    2: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_2, ["matrix_set — updates the weight of an existing connection"]),
    3: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_3, ["matrix_set — vector update (set multiple to 0.0F)"]),
    4: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_4, ["matrix_set — connection modify (set then read back)"]),
    5: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_5, ["matrix_nnz — count non-zero entries"]),
    6: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_6, ["matrix_set — zero-out diagonal (set everything to 0.0F)"]),
    7: (__test_7370617273655f6d61747269785f65787472615f746573742e6d6274_7, ["matrix_set — full bulk-set preserves nnz + rowptr structure"]),
  },
  "snn_utils_params_test.mbt": {
    0: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_0, ["LKD2014::new: defaults match Julia"]),
    1: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_1, ["Duarte2019::new: defaults match Julia"]),
    2: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_2, ["LKD2014 fields used to build IF + AdEx parameters"]),
    3: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_3, ["LKD2014Soma::new — defaults match Julia lkd2014_soma"]),
    4: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_4, ["Quaresima2023Dend::new — defaults match Julia quaresima2023_dend"]),
    5: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_5, ["eyal_equivalent_nar — NAR=1.8 default computes AMPA/NMDA g0"]),
    6: (__test_736e6e5f7574696c735f706172616d735f746573742e6d6274_6, ["Quaresima2024UpDown::new — nar + tau_d stored, dend_receptors populated"]),
  },
  "single_exp_synapse_test.mbt": {
    0: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_0, ["SingleExpParameter::new defaults match Julia"]),
    1: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_1, ["SingleExpSynapseVars::new zero-init"]),
    2: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_2, ["update_synapses_single_exp — spike input + decay"]),
    3: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_3, ["update_synapses_single_exp — no spike input decays to zero"]),
    4: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_4, ["synaptic_current_single_exp — conductance current"]),
    5: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_5, ["synaptic_current_single_exp — pure inhibitory"]),
    6: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_6, ["synaptic_current_single_exp — pure excitatory"]),
    7: (__test_73696e676c655f6578705f73796e617073655f746573742e6d6274_7, ["update_synapses_single_exp — 1000 steps stays finite"]),
  },
  "sim_test.mbt": {
    0: (__test_73696d5f746573742e6d6274_0, ["Monitor without sr records every step"]),
    1: (__test_73696d5f746573742e6d6274_1, ["Monitor with sr=1kHz records every 8th step"]),
    2: (__test_73696d5f746573742e6d6274_2, ["Monitor with sr=8kHz records every step"]),
    3: (__test_73696d5f746573742e6d6274_3, ["Monitor with sr=0.5kHz records every 16th step"]),
  },
  "sim_control_test.mbt": {
    0: (__test_73696d5f636f6e74726f6c5f746573742e6d6274_0, ["get_time: zero before sim"]),
    1: (__test_73696d5f636f6e74726f6c5f746573742e6d6274_1, ["get_time: advances after sim_for"]),
    2: (__test_73696d5f636f6e74726f6c5f746573742e6d6274_2, ["reset_time_heterogeneous: resets to zero"]),
    3: (__test_73696d5f636f6e74726f6c5f746573742e6d6274_3, ["Monitor: spike count accumulates over sim"]),
    4: (__test_73696d5f636f6e74726f6c5f746573742e6d6274_4, ["Monitor::new_v with sampling rate stores sr"]),
    5: (__test_73696d5f636f6e74726f6c5f746573742e6d6274_5, ["HeterogeneousModel: compose with multiple pops + conns"]),
  },
  "set_plasticity_active_test.mbt": {
    0: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_0, ["STDPEntry::set_ltp_active: toggles vars.active[0]"]),
    1: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_1, ["stdp_step respects vars.active=false"]),
    2: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_2, ["MarkramSTPEntry::set_stp_active: toggles vars.active[0]"]),
    3: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_3, ["MarkramSTPEntryHet::set_stp_active: toggles vars.active[0]"]),
    4: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_4, ["MarkramSTPEntryTimestep::set_stp_active: toggles vars.active[0]"]),
    5: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_5, ["STDPEntry::set_ltp_active: round-trip preserves the vars state"]),
    6: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_6, ["MarkramSTPEntry::set_stp_active: round-trip preserves the vars state"]),
    7: (__test_7365745f706c61737469636974795f6163746976655f746573742e6d6274_7, ["set_LTP_active / set_STP_active: alias names match Julia convention (sans !)"]),
  },
  "rng_test.mbt": {
    0: (__test_726e675f746573742e6d6274_0, ["next_u64 advances state"]),
    1: (__test_726e675f746573742e6d6274_1, ["next_f32 returns Float32 in [0, 1)"]),
    2: (__test_726e675f746573742e6d6274_2, ["rand_f32 produces n values"]),
    3: (__test_726e675f746573742e6d6274_3, ["two Xoshiro with same seed produce same sequence"]),
    4: (__test_726e675f746573742e6d6274_4, ["Xoshiro::new is deterministic"]),
  },
  "receptor_types_test.mbt": {
    0: (__test_7265636570746f725f74797065735f746573742e6d6274_0, ["eyal_nmda / soma_nmda — Eyal/Soma NMDA defaults"]),
    1: (__test_7265636570746f725f74797065735f746573742e6d6274_1, ["miles_gaba_soma / duarte_glu_soma — single Receptor fields"]),
    2: (__test_7265636570746f725f74797065735f746573742e6d6274_2, ["eyal_glu_dend / miles_gaba_dend — Glutamatergic/GABAergic bundles"]),
    3: (__test_7265636570746f725f74797065735f746573742e6d6274_3, ["soma_glu / soma_gaba — soma-targeted bundles"]),
    4: (__test_7265636570746f725f74797065735f746573742e6d6274_4, ["tripod_soma_receptors — 4-receptor collection shape"]),
    5: (__test_7265636570746f725f74797065735f746573742e6d6274_5, ["tripod_dend_receptors — EyalGluDend + MilesGabaDend"]),
    6: (__test_7265636570746f725f74797065735f746573742e6d6274_6, ["soma_receptors — SomaGlu + SomaGABA"]),
    7: (__test_7265636570746f725f74797065735f746573742e6d6274_7, ["Receptors::rec array is fresh each call (no shared state)"]),
  },
  "receptor_test.mbt": {
    0: (__test_7265636570746f725f746573742e6d6274_0, ["nmda_gating: B(v) ≈ 1 at very hyperpolarized v"]),
    1: (__test_7265636570746f725f746573742e6d6274_1, ["nmda_gating: B(v) increases as v depolarizes"]),
    2: (__test_7265636570746f725f746573742e6d6274_2, ["nmda_gating: B(v) saturates to 1 at very depolarized v"]),
    3: (__test_7265636570746f725f746573742e6d6274_3, ["Receptor::simple: alpha + inv fields computed correctly"]),
    4: (__test_7265636570746f725f746573742e6d6274_4, ["Receptor::nmda: is_nmda flag set"]),
    5: (__test_7265636570746f725f746573742e6d6274_5, ["step_receptor: 2-state ODE decays g after pulse"]),
    6: (__test_7265636570746f725f746573742e6d6274_6, ["step_receptor: multiple steps decay to zero"]),
    7: (__test_7265636570746f725f746573742e6d6274_7, ["receptor_current: simple (non-NMDA) drives I = gsyn*g*(v-e_rev)"]),
    8: (__test_7265636570746f725f746573742e6d6274_8, ["receptor_current: NMDA gate reduces current at hyperpolarized v"]),
    9: (__test_7265636570746f725f746573742e6d6274_9, ["Receptors::new: 4 elements with correct ordering"]),
  },
  "receptor_from_array_test.mbt": {
    0: (__test_7265636570746f725f66726f6d5f61727261795f746573742e6d6274_0, ["Receptors::from_array — length 4 array"]),
    1: (__test_7265636570746f725f66726f6d5f61727261795f746573742e6d6274_1, ["Receptors::from_array — same as Receptors::new with same args"]),
    2: (__test_7265636570746f725f66726f6d5f61727261795f746573742e6d6274_2, ["Receptors::from_array — index ordering preserved"]),
  },
  "receptor_extra_test.mbt": {
    0: (__test_7265636570746f725f65787472615f746573742e6d6274_0, ["alpha_synapse — (τd - τr) / (τd * τr) matches Julia"]),
    1: (__test_7265636570746f725f65787472615f746573742e6d6274_1, ["Glutamatergic::new defaults — AMPA + NMDA"]),
    2: (__test_7265636570746f725f65787472615f746573742e6d6274_2, ["Glutamatergic::custom — custom AMPA + NMDA"]),
    3: (__test_7265636570746f725f65787472615f746573742e6d6274_3, ["GABAergic::new defaults — GABAa + GABAb"]),
    4: (__test_7265636570746f725f65787472615f746573742e6d6274_4, ["GABAergic::custom — custom GABAa + GABAb"]),
    5: (__test_7265636570746f725f65787472615f746573742e6d6274_5, ["Glutamatergic + GABAergic — fields preserved through struct"]),
    6: (__test_7265636570746f725f65787472615f746573742e6d6274_6, ["alpha_synapse = same value as Receptor::simple.alpha field"]),
    7: (__test_7265636570746f725f65787472615f746573742e6d6274_7, ["Receptors::from_pair — Glu + GABA → 4-element collection"]),
    8: (__test_7265636570746f725f65787472615f746573742e6d6274_8, ["Receptors::from_pair — custom glu + gaba preserved"]),
    9: (__test_7265636570746f725f65787472615f746573742e6d6274_9, ["receptors_current — sums AMPA + NMDA + GABAa + GABAb"]),
    10: (__test_7265636570746f725f65787472615f746573742e6d6274_10, ["receptors_current — zero g_matrix stays zero"]),
    11: (__test_7265636570746f725f65787472615f746573742e6d6274_11, ["receptors_current — pure AMPA contribution matches receptor_current"]),
  },
  "population_test.mbt": {
    0: (__test_706f70756c6174696f6e5f746573742e6d6274_0, ["AnyPop sim — IF only"]),
    1: (__test_706f70756c6174696f6e5f746573742e6d6274_1, ["AnyPop sim — heterogeneous 5 populations"]),
    2: (__test_706f70756c6174696f6e5f746573742e6d6274_2, ["AnyPop n_neurons"]),
  },
  "parameters_test.mbt": {
    0: (__test_706172616d65746572735f746573742e6d6274_0, ["IFParameter::new: defaults match Julia + custom via IFParameter::custom"]),
    1: (__test_706172616d65746572735f746573742e6d6274_1, ["AdExParameter::new: defaults match Julia"]),
    2: (__test_706172616d65746572735f746573742e6d6274_2, ["AdExParameter::with_vr: custom Vr"]),
    3: (__test_706172616d65746572735f746573742e6d6274_3, ["IZParameter::new / rs / fs: defaults match Julia"]),
    4: (__test_706172616d65746572735f746573742e6d6274_4, ["MorrisLecarParameter::new: defaults match Julia"]),
    5: (__test_706172616d65746572735f746573742e6d6274_5, ["HetRecParameter::new: defaults match Julia"]),
    6: (__test_706172616d65746572735f746573742e6d6274_6, ["PostSpike::new: defaults match Julia"]),
    7: (__test_706172616d65746572735f746573742e6d6274_7, ["AdExPostSpike (Tripod variant): all PostSpike fields present"]),
    8: (__test_706172616d65746572735f746573742e6d6274_8, ["IF has DoubleExpSynapse-style scalar time constants"]),
    9: (__test_706172616d65746572735f746573742e6d6274_9, ["Receptor / NMDAVoltageDependency constructors work"]),
  },
  "parameters_extra_test.mbt": {
    0: (__test_706172616d65746572735f65787472615f746573742e6d6274_0, ["HetRecParameter::new — defaults"]),
    1: (__test_706172616d65746572735f65787472615f746573742e6d6274_1, ["HetRecParameter::custom — Nd + τm + overlap"]),
    2: (__test_706172616d65746572735f65787472615f746573742e6d6274_2, ["NMDAVoltageDependency::eyal — defaults"]),
    3: (__test_706172616d65746572735f65787472615f746573742e6d6274_3, ["NMDAVoltageDependency::custom — explicit mg / b / k"]),
    4: (__test_706172616d65746572735f65787472615f746573742e6d6274_4, ["NMDAVoltageDependency::custom — different mg shifts gating"]),
    5: (__test_706172616d65746572735f65787472615f746573742e6d6274_5, ["Receptors::new — exactly 4 entries"]),
    6: (__test_706172616d65746572735f65787472615f746573742e6d6274_6, ["Receptors::new — target field per entry"]),
  },
  "neuron_wc_test.mbt": {
    0: (__test_6e6575726f6e5f77635f746573742e6d6274_0, ["WilsonCowan::new initialises x from Normal(0, 0.5)"]),
    1: (__test_6e6575726f6e5f77635f746573742e6d6274_1, ["step_wc: x decays toward 0 with I=0, g=0"]),
    2: (__test_6e6575726f6e5f77635f746573742e6d6274_2, ["step_wc: r = tanh(x) after integration"]),
    3: (__test_6e6575726f6e5f77635f746573742e6d6274_3, ["RateSynapse::new and forward_rate"]),
    4: (__test_6e6575726f6e5f77635f746573742e6d6274_4, ["WC_ integrates via AnyPop enum"]),
  },
  "neuron_tripod_test.mbt": {
    0: (__test_6e6575726f6e5f747269706f645f746573742e6d6274_0, ["Tripod::new — initial state"]),
    1: (__test_6e6575726f6e5f747269706f645f746573742e6d6274_1, ["Tripod — no input, all compartments decay toward el"]),
    2: (__test_6e6575726f6e5f747269706f645f746573742e6d6274_2, ["Tripod — soma spike fires under strong current"]),
    3: (__test_6e6575726f6e5f747269706f645f746573742e6d6274_3, ["Tripod — single-exp synapse step on soma"]),
    4: (__test_6e6575726f6e5f747269706f645f746573742e6d6274_4, ["Tripod — single-exp synapse step on both dendrites"]),
    5: (__test_6e6575726f6e5f747269706f645f746573742e6d6274_5, ["Tripod — Heun integration runs without NaN"]),
  },
  "neuron_tripod_het_test.mbt": {
    0: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_0, ["TripodHet::new: shapes and init"]),
    1: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_1, ["TripodHet::new: per-neuron vt/vr values respected"]),
    2: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_2, ["step_tripod_het: tonic drive produces spikes"]),
    3: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_3, ["step_tripod_het: zero drive, no spikes (passive decay)"]),
    4: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_4, ["step_tripod_het: heterogeneous threshold — only low-vt neurons spike"]),
    5: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_5, ["step_tripod_het: v_d1/v_d2 finite (no runaway) under heavy drive"]),
    6: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_6, ["tripod_het_soma_step_synapses: glu_s → ge_s decay"]),
    7: (__test_6e6575726f6e5f747269706f645f6865745f746573742e6d6274_7, ["step_tripod_het: spike counter via repeat-with-reset pattern"]),
  },
  "neuron_poisson_test.mbt": {
    0: (__test_6e6575726f6e5f706f6973736f6e5f746573742e6d6274_0, ["Poisson::new initial state"]),
    1: (__test_6e6575726f6e5f706f6973736f6e5f746573742e6d6274_1, ["Poisson rate matches frequency"]),
    2: (__test_6e6575726f6e5f706f6973736f6e5f746573742e6d6274_2, ["Poisson high rate always fires"]),
  },
  "neuron_multipod_test.mbt": {
    0: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_0, ["MultipodParameter::new — wraps array"]),
    1: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_1, ["MultipodParameter::uniform — nd copies"]),
    2: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_2, ["Multipod::new nd=2 shapes"]),
    3: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_3, ["Multipod::new nd=3 shapes"]),
    4: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_4, ["Multipod::new initial v_s in [vr, vt]"]),
    5: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_5, ["Multipod::step nd=2 zero-input runs cleanly"]),
    6: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_6, ["Multipod::step nd=3 zero-input runs cleanly"]),
    7: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_7, ["Multipod::step integrates soma + dendrite currents without NaN"]),
    8: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_8, ["Multipod::step fire flag toggles under high drive"]),
    9: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_9, ["Multipod::new — w_s, he_s, hi_s, ge_s, gi_s zero"]),
    10: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_10, ["Multipod::new — after_spike field shape + he_d/hi_d zero"]),
    11: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_11, ["Multipod::new — g_d / h_d / dv / dv_temp / cs / iv lengths"]),
    12: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_12, ["Multipod::new — g_d[d][i][n] nested zero-init across all 3 levels"]),
    13: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_13, ["Multipod::new — g_d / h_d are independent per-dendrite slices"]),
    14: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_14, ["Multipod::new — receptor index 3 (GABAb) accessible"]),
    15: (__test_6e6575726f6e5f6d756c7469706f645f746573742e6d6274_15, ["Multipod::step preserves receptor markers"]),
  },
  "neuron_ml_test.mbt": {
    0: (__test_6e6575726f6e5f6d6c5f746573742e6d6274_0, ["MorrisLecarParameter defaults"]),
    1: (__test_6e6575726f6e5f6d6c5f746573742e6d6274_1, ["MorrisLecar::new initial state"]),
    2: (__test_6e6575726f6e5f6d6c5f746573742e6d6274_2, ["MorrisLecar one step is stable"]),
  },
  "neuron_iz_test.mbt": {
    0: (__test_6e6575726f6e5f697a5f746573742e6d6274_0, ["IZ::new initialises v = -65"]),
    1: (__test_6e6575726f6e5f697a5f746573742e6d6274_1, ["IZ::new initialises u = b * v = -13"]),
    2: (__test_6e6575726f6e5f697a5f746573742e6d6274_2, ["IZParameter defaults match Julia"]),
    3: (__test_6e6575726f6e5f697a5f746573742e6d6274_3, ["IZ: one step from rest"]),
  },
  "neuron_iz_custom_test.mbt": {
    0: (__test_6e6575726f6e5f697a5f637573746f6d5f746573742e6d6274_0, ["IZParameter::custom — arbitrary (a, b, c, d) round-trip"]),
    1: (__test_6e6575726f6e5f697a5f637573746f6d5f746573742e6d6274_1, ["IZParameter::custom — IZ::new accepts the custom params"]),
    2: (__test_6e6575726f6e5f697a5f637573746f6d5f746573742e6d6274_2, ["IZParameter::custom — matches rs() for matching (a, b, c, d)"]),
  },
  "neuron_inhomogeneous_poisson_test.mbt": {
    0: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_0, ["InhomogeneousPoissonParam::new — defaults"]),
    1: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_1, ["InhomogeneousPoissonParam::custom — β / τ / r0"]),
    2: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_2, ["InhomogeneousPoisson::new — defaults: N=100, fire all false, r=r0"]),
    3: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_3, ["InhomogeneousPoisson::new — custom N and β"]),
    4: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_4, ["step_inhomogeneous_poisson — fire mask bounded by N"]),
    5: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_5, ["step_inhomogeneous_poisson — non-default β + custom N"]),
    6: (__test_6e6575726f6e5f696e686f6d6f67656e656f75735f706f6973736f6e5f746573742e6d6274_6, ["step_inhomogeneous_poisson — many steps produce realistic rates"]),
  },
  "neuron_if_test.mbt": {
    0: (__test_6e6575726f6e5f69665f746573742e6d6274_0, ["IF::new initialises v in [Vr, Vt)"]),
    1: (__test_6e6575726f6e5f69665f746573742e6d6274_1, ["IF::new zero state"]),
    2: (__test_6e6575726f6e5f69665f746573742e6d6274_2, ["IF: tab_const / dt = 16 (0.125 ms step, 2ms refractory)"]),
    3: (__test_6e6575726f6e5f69665f746573742e6d6274_3, ["IF: step_neuron produces no fire when v is below Vt"]),
  },
  "neuron_if_sinexp_test.mbt": {
    0: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_0, ["IFSinExpParameter::new — default τe/τi = 6/2"]),
    1: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_1, ["IFSinExpParameter::lkd_pv — LKD 2014 PV defaults"]),
    2: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_2, ["IFSinExp::new — population size, initial state at rest"]),
    3: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_3, ["ifsinexp_step — single-exp ge decay (no he/hi)"]),
    4: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_4, ["ifsinexp_step — fire when v >= vt, reset to vr"]),
    5: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_5, ["ifsinexp_step — refractory period"]),
    6: (__test_6e6575726f6e5f69665f73696e6578705f746573742e6d6274_6, ["ifsinexp_step — 100-step stability (ge/gi bounded)"]),
  },
  "neuron_if_gsyn_test.mbt": {
    0: (__test_6e6575726f6e5f69665f6773796e5f746573742e6d6274_0, ["IFParameterGsyn::new — fields are set"]),
    1: (__test_6e6575726f6e5f69665f6773796e5f746573742e6d6274_1, ["IFParameterGsyn::from_base — uses supplied base"]),
    2: (__test_6e6575726f6e5f69665f6773796e5f746573742e6d6274_2, ["IFParameterGsyn::apply — sets every neuron's gsyn arrays"]),
    3: (__test_6e6575726f6e5f69665f6773796e5f746573742e6d6274_3, ["IF::with_gsyn — convenience constructor applies scales"]),
    4: (__test_6e6575726f6e5f69665f6773796e5f746573742e6d6274_4, ["synaptic_current — gsyn_e scales the excitatory drive"]),
  },
  "neuron_if_canahp_test.mbt": {
    0: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_0, ["IFCANAHParameter::new defaults match Julia"]),
    1: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_1, ["IFCANAHP::new — shapes and zero-initialised state"]),
    2: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_2, ["update_neuron_ifcanahp — Ca drive toward Ca0"]),
    3: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_3, ["update_neuron_ifcanahp — CAN/AHP gating from Ca"]),
    4: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_4, ["update_neuron_ifcanahp — membrane update with leak"]),
    5: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_5, ["update_spike_ifcanahp — fire and reset"]),
    6: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_6, ["update_spike_ifcanahp — no fire when v <= Vt"]),
    7: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_7, ["integrate_ifcanahp — full step is finite"]),
    8: (__test_6e6575726f6e5f69665f63616e6168705f746573742e6d6274_8, ["integrate_ifcanahp — CAN/AHP off (default g_can=0, g_ahp=0)"]),
  },
  "neuron_identity_test.mbt": {
    0: (__test_6e6575726f6e5f6964656e746974795f746573742e6d6274_0, ["Identity::new constructs population with zero state"]),
    1: (__test_6e6575726f6e5f6964656e746974795f746573742e6d6274_1, ["Identity::step_neuron_id fires when g > 0"]),
    2: (__test_6e6575726f6e5f6964656e746974795f746573742e6d6274_2, ["Identity::step_neuron_id accumulates h across steps"]),
  },
  "neuron_hh_test.mbt": {
    0: (__test_6e6575726f6e5f68685f746573742e6d6274_0, ["HHParameter defaults"]),
    1: (__test_6e6575726f6e5f68685f746573742e6d6274_1, ["HH::new initial state"]),
  },
  "neuron_hetrec_test.mbt": {
    0: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_0, ["HetRecParameter::new — Julia defaults"]),
    1: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_1, ["HetRec::new — array sizes match Julia HetRec layout"]),
    2: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_2, ["HetRec::new — baseline rates uniform in [0, 1]"]),
    3: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_3, ["HetRec::new — dendritic time constants uniform in [10, 100] ms"]),
    4: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_4, ["HetRec::new — overlap=0 → only own dendrites per soma"]),
    5: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_5, ["step_hetrec — zero input → v_d, v_s stay at 0, no spikes"]),
    6: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_6, ["step_hetrec — constant is_ drives v_d, then v_s via M"]),
    7: (__test_6e6575726f6e5f6865747265635f746573742e6d6274_7, ["step_hetrec — long sim no NaN, mean v_s in reasonable range"]),
  },
  "neuron_gif_test.mbt": {
    0: (__test_6e6575726f6e5f6769665f746573742e6d6274_0, ["GIFParameter::new defaults match pure LIF"]),
    1: (__test_6e6575726f6e5f6769665f746573742e6d6274_1, ["GIFParameter::adex — Brette-Gerstner 2005 defaults"]),
    2: (__test_6e6575726f6e5f6769665f746573742e6d6274_2, ["GIF::new — shapes and zero-initialised state"]),
    3: (__test_6e6575726f6e5f6769665f746573742e6d6274_3, ["update_neuron_gif — pure LIF (τw=0): no adaptation"]),
    4: (__test_6e6575726f6e5f6769665f746573742e6d6274_4, ["update_neuron_gif — AdEx mode: adaptation w increments on fire"]),
    5: (__test_6e6575726f6e5f6769665f746573742e6d6274_5, ["update_neuron_gif — refractory countdown"]),
    6: (__test_6e6575726f6e5f6769665f746573742e6d6274_6, ["update_neuron_gif — w sub-threshold decay (AdEx mode)"]),
    7: (__test_6e6575726f6e5f6769665f746573742e6d6274_7, ["integrate_gif — full step end-to-end"]),
  },
  "neuron_extended_if_test.mbt": {
    0: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_0, ["ExtendedIFParameter::new defaults match Julia"]),
    1: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_1, ["ExtendedIFParameter::custom preserves all 11 fields"]),
    2: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_2, ["ExtendedIF::new — shapes and zero-initialised state"]),
    3: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_3, ["update_synapses_extended_if — 3-buffer exponential decay"]),
    4: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_4, ["update_neuron_extended_if — tonic current drives v up"]),
    5: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_5, ["update_neuron_extended_if — fire and reset when v > Vt"]),
    6: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_6, ["update_neuron_extended_if — refractory countdown skips step"]),
    7: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_7, ["update_neuron_extended_if — α-gating (dendritic interaction)"]),
    8: (__test_6e6575726f6e5f657874656e6465645f69665f746573742e6d6274_8, ["integrate_extended_if — runs both phases end-to-end"]),
  },
  "neuron_dendrite_test.mbt": {
    0: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_0, ["Dendrite::new — defaults match Julia Dendrite defaults"]),
    1: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_1, ["Dendrite::custom — per-neuron parameter arrays"]),
    2: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_2, ["dendrite_step — passive decay at v=El, no parent/syn input"]),
    3: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_3, ["dendrite_step — leak current pulls v toward El"]),
    4: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_4, ["dendrite_step — axial current from parent at higher v"]),
    5: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_5, ["g_axial — bit-exact match for Julia formula π*d²/(4*Ri*l)"]),
    6: (__test_6e6575726f6e5f64656e64726974655f746573742e6d6274_6, ["g_mem and c_mem — bit-exact formulas"]),
  },
  "neuron_ballandstick_test.mbt": {
    0: (__test_6e6575726f6e5f62616c6c616e64737469636b5f746573742e6d6274_0, ["BallAndStick::new — initial state"]),
    1: (__test_6e6575726f6e5f62616c6c616e64737469636b5f746573742e6d6274_1, ["BallAndStick — no input, v_s and v_d decay toward el"]),
    2: (__test_6e6575726f6e5f62616c6c616e64737469636b5f746573742e6d6274_2, ["BallAndStick — soma spike fires when threshold reached"]),
    3: (__test_6e6575726f6e5f62616c6c616e64737469636b5f746573742e6d6274_3, ["BallAndStick — single-exp synapse step on soma"]),
    4: (__test_6e6575726f6e5f62616c6c616e64737469636b5f746573742e6d6274_4, ["BallAndStick — single-exp synapse step on dendrite"]),
    5: (__test_6e6575726f6e5f62616c6c616e64737469636b5f746573742e6d6274_5, ["BallAndStick — Heun integration runs without NaN"]),
  },
  "neuron_adex_test.mbt": {
    0: (__test_6e6575726f6e5f616465785f746573742e6d6274_0, ["AdEx::new initialises v in [Vr, Vt)"]),
    1: (__test_6e6575726f6e5f616465785f746573742e6d6274_1, ["AdEx::new initialises threshold to Vt"]),
    2: (__test_6e6575726f6e5f616465785f746573742e6d6274_2, ["AdEx::new initialises tabs to 1"]),
    3: (__test_6e6575726f6e5f616465785f746573742e6d6274_3, ["AdExParameter defaults match Julia"]),
  },
  "neuron_adex_sinexp_test.mbt": {
    0: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_0, ["AdExSinExpParameter::new — defaults match AdExParameter"]),
    1: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_1, ["AdExSinExpParameter::lkd_adex — LKD 2014 values"]),
    2: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_2, ["AdExSinExp::new — initial state"]),
    3: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_3, ["AdExSinExp — no input, v decays toward el (steady state)"]),
    4: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_4, ["AdExSinExp — sin-exp synapse step: glu is consumed into ge"]),
    5: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_5, ["AdExSinExp — strong current drives v to threshold and fires"]),
    6: (__test_6e6575726f6e5f616465785f73696e6578705f746573742e6d6274_6, ["AdExSinExp — gaba injection into inhibitory channel"]),
  },
  "neuron_adex_multi_timescale_test.mbt": {
    0: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_0, ["AdExMultiTimescaleParameter::new defaults match Julia"]),
    1: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_1, ["AdExMultiTimescale::new — shapes and zero-initialised state"]),
    2: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_2, ["update_synapses_adex_multi — 2-state ODE decay"]),
    3: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_3, ["update_synapses_adex_multi — independent per-receptor buffers"]),
    4: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_4, ["synaptic_current_adex_multi — glu receptor uses E_e"]),
    5: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_5, ["synaptic_current_adex_multi — gaba receptor uses E_i"]),
    6: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_6, ["synaptic_current_adex_multi — zero g_buf stays zero"]),
    7: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_7, ["update_soma_adex_multi — fire when v > theta"]),
    8: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_8, ["update_soma_adex_multi — no fire when v <= theta"]),
    9: (__test_6e6575726f6e5f616465785f6d756c74695f74696d657363616c655f746573742e6d6274_9, ["integrate_adex_multi — full step is finite"]),
  },
  "neuron_adex_het_test.mbt": {
    0: (__test_6e6575726f6e5f616465785f6865745f746573742e6d6274_0, ["AdExParameterHet::homogeneous: matches AdExParameter defaults per-ne"]),
    1: (__test_6e6575726f6e5f616465785f6865745f746573742e6d6274_1, ["AdExHet::new: initial state — v spread over vr[i]..vt[i], threshold = vt[i]"]),
    2: (__test_6e6575726f6e5f616465785f6865745f746573742e6d6274_2, ["step_adex_het: homogeneous params match AdEx scalar step"]),
    3: (__test_6e6575726f6e5f616465785f6865745f746573742e6d6274_3, ["step_adex_het: per-ne τm differences produce different v dynamics"]),
  },
  "metaplasticity_test.mbt": {
    0: (__test_6d657461706c61737469636974795f746573742e6d6274_0, ["MultiplicativeNorm::new — tau field"]),
    1: (__test_6d657461706c61737469636974795f746573742e6d6274_1, ["AdditiveNorm::new — tau field"]),
    2: (__test_6d657461706c61737469636974795f746573742e6d6274_2, ["SynapseNormalization — W0 captures initial per-post weight sums"]),
    3: (__test_6d657461706c61737469636974795f746573742e6d6274_3, ["SynapseNormalization — W0 sums across multiple synapses (same post)"]),
    4: (__test_6d657461706c61737469636974795f746573742e6d6274_4, ["SynapseNormalization — AdditiveNorm form"]),
    5: (__test_6d657461706c61737469636974795f746573742e6d6274_5, ["SynapseNormalization — W0 sums initial weights (full connectivity)"]),
    6: (__test_6d657461706c61737469636974795f746573742e6d6274_6, ["metaplasticity_step — multiplicative: W0 captured, then shrinkage restored"]),
    7: (__test_6d657461706c61737469636974795f746573742e6d6274_7, ["metaplasticity_step — multiplicative shrinkage + restore"]),
    8: (__test_6d657461706c61737469636974795f746573742e6d6274_8, ["metaplasticity_step — additive shrinkage correction"]),
    9: (__test_6d657461706c61737469636974795f746573742e6d6274_9, ["metaplasticity_step — zero W1 handled gracefully (multiplicative)"]),
    10: (__test_6d657461706c61737469636974795f746573742e6d6274_10, ["SynapseTarget — post_id field captured"]),
    11: (__test_6d657461706c61737469636974795f746573742e6d6274_11, ["SynapseNormalization — accepts all targets sharing same post_id"]),
    12: (__test_6d657461706c61737469636974795f746573742e6d6274_12, ["metaplasticity_step_gated — fires only every τ/dt steps"]),
    13: (__test_6d657461706c61737469636974795f746573742e6d6274_13, ["metaplasticity_step_gated — τ=0 disables periodic normalization"]),
  },
  "math_native_test.mbt": {
    0: (__test_6d6174685f6e61746976655f746573742e6d6274_0, ["expf(0) = 1"]),
    1: (__test_6d6174685f6e61746976655f746573742e6d6274_1, ["expf(1) is bit-exact"]),
    2: (__test_6d6174685f6e61746976655f746573742e6d6274_2, ["expf is monotonic"]),
    3: (__test_6d6174685f6e61746976655f746573742e6d6274_3, ["sigmoid_f32(0) = 0.5"]),
    4: (__test_6d6174685f6e61746976655f746573742e6d6274_4, ["sigmoid_f32 is in (0, 1)"]),
  },
  "izhikevich_synapses_postspike_test.mbt": {
    0: (__test_697a68696b65766963685f73796e61707365735f706f73747370696b655f746573742e6d6274_0, ["step_iz_synapses_postspike — decayed ge with no refractory"]),
    1: (__test_697a68696b65766963685f73796e61707365735f706f73747370696b655f746573742e6d6274_1, ["step_iz_synapses_postspike — tabs countdown during refractory"]),
    2: (__test_697a68696b65766963685f73796e61707365735f706f73747370696b655f746573742e6d6274_2, ["step_iz_synapses_postspike — does NOT modify v or u"]),
    3: (__test_697a68696b65766963685f73796e61707365735f706f73747370696b655f746573742e6d6274_3, ["step_iz_synapses_postspike — tabs_const=0 ≡ step_iz_synapses"]),
    4: (__test_697a68696b65766963685f73796e61707365735f706f73747370696b655f746573742e6d6274_4, ["step_iz_synapses_postspike — multi-neuron heterogeneous tabs"]),
  },
  "izhikevich_step_synapses_test.mbt": {
    0: (__test_697a68696b65766963685f737465705f73796e61707365735f746573742e6d6274_0, ["step_iz_synapses — exponential decay of ge"]),
    1: (__test_697a68696b65766963685f737465705f73796e61707365735f746573742e6d6274_1, ["step_iz_synapses — does not touch v or u"]),
    2: (__test_697a68696b65766963685f737465705f73796e61707365735f746573742e6d6274_2, ["step_iz_synapses — 100-step stability (ge decays to 0)"]),
    3: (__test_697a68696b65766963685f737465705f73796e61707365735f746573742e6d6274_3, ["step_iz then step_iz_synapses — full equivalence"]),
  },
  "izhikevich_reset_with_postspike_test.mbt": {
    0: (__test_697a68696b65766963685f72657365745f776974685f706f73747370696b655f746573742e6d6274_0, ["iz_reset_with_postspike — clears v/u/fire/ge/gi and resets tabs_const"]),
    1: (__test_697a68696b65766963685f72657365745f776974685f706f73747370696b655f746573742e6d6274_1, ["iz_reset_with_postspike — next fire uses new tabs_const"]),
    2: (__test_697a68696b65766963685f72657365745f776974685f706f73747370696b655f746573742e6d6274_2, ["iz_reset_with_postspike — preserves i (external input)"]),
    3: (__test_697a68696b65766963685f72657365745f776974685f706f73747370696b655f746573742e6d6274_3, ["iz_reset_with_postspike — same postspike as iz_reset clears everything"]),
    4: (__test_697a68696b65766963685f72657365745f776974685f706f73747370696b655f746573742e6d6274_4, ["iz_reset_with_postspike — multi-neuron heterogeneous postspike"]),
  },
  "izhikevich_reset_test_bitexact_test.mbt": {
    0: (__test_697a68696b65766963685f72657365745f746573745f62697465786163745f746573742e6d6274_0, ["iz_reset / two fresh populations with same seed are bit-exact"]),
    1: (__test_697a68696b65766963685f72657365745f746573745f62697465786163745f746573742e6d6274_1, ["iz_reset / post-reset trajectory matches fresh-population trajectory bit-exactly"]),
    2: (__test_697a68696b65766963685f72657365745f746573745f62697465786163745f746573742e6d6274_2, ["iz_reset / single neuron 50-step + reset + 50-step is bit-exact"]),
    3: (__test_697a68696b65766963685f72657365745f746573745f62697465786163745f746573742e6d6274_3, ["iz_reset / bit-exactness across FS parameters"]),
    4: (__test_697a68696b65766963685f72657365745f746573745f62697465786163745f746573742e6d6274_4, ["iz_reset / bit-exactness across custom IB-like parameters"]),
  },
  "izhikevich_reset_test.mbt": {
    0: (__test_697a68696b65766963685f72657365745f746573742e6d6274_0, ["iz_reset — restores v to -65 and u to b*-65"]),
    1: (__test_697a68696b65766963685f72657365745f746573742e6d6274_1, ["iz_reset — clears fire, ge, gi, tabs"]),
    2: (__test_697a68696b65766963685f72657365745f746573742e6d6274_2, ["iz_reset — does NOT modify i (external input)"]),
    3: (__test_697a68696b65766963685f72657365745f746573742e6d6274_3, ["iz_reset — step_iz after reset produces deterministic trajectory"]),
    4: (__test_697a68696b65766963685f72657365745f746573742e6d6274_4, ["iz_reset — works for FS parameters (b=0.2 still)"]),
  },
  "izhikevich_postspike_test.mbt": {
    0: (__test_697a68696b65766963685f706f73747370696b655f746573742e6d6274_0, ["IZPostSpike::new — default 1-step refractory"]),
    1: (__test_697a68696b65766963685f706f73747370696b655f746573742e6d6274_1, ["IZPostSpike::custom — arbitrary refractory period"]),
    2: (__test_697a68696b65766963685f706f73747370696b655f746573742e6d6274_2, ["IZ::init_with_postspike — initial state honours v_init / u_init"]),
    3: (__test_697a68696b65766963685f706f73747370696b655f746573742e6d6274_3, ["IZ::init_with_postspike — step_iz evolves from initial state"]),
    4: (__test_697a68696b65766963685f706f73747370696b655f746573742e6d6274_4, ["IZPostSpike + IZ::init_with_postspike — fire/i/ge/gi zero-init"]),
  },
  "izhikevich_postspike_step_test.mbt": {
    0: (__test_697a68696b65766963685f706f73747370696b655f737465705f746573742e6d6274_0, ["step_iz_with_postspike — single neuron, force-fire on first step"]),
    1: (__test_697a68696b65766963685f706f73747370696b655f737465705f746573742e6d6274_1, ["step_iz_with_postspike — refractory blocks membrane but not synapse"]),
    2: (__test_697a68696b65766963685f706f73747370696b655f737465705f746573742e6d6274_2, ["step_iz_with_postspike — synapse decay runs during refractory"]),
    3: (__test_697a68696b65766963685f706f73747370696b655f737465705f746573742e6d6274_3, ["step_iz_with_postspike — tabs_const=0 behaves like step_iz"]),
    4: (__test_697a68696b65766963685f706f73747370696b655f737465705f746573742e6d6274_4, ["step_iz_with_postspike — multi-neuron, only refractory ones freeze"]),
  },
  "izhikevich_postspike_step_synapses_test.mbt": {
    0: (__test_697a68696b65766963685f706f73747370696b655f737465705f73796e61707365735f746573742e6d6274_0, ["postspike step+synapses / tab countdown through combined loop"]),
    1: (__test_697a68696b65766963685f706f73747370696b655f737465705f73796e61707365735f746573742e6d6274_1, ["postspike step+synapses / ge decays during refractory"]),
    2: (__test_697a68696b65766963685f706f73747370696b655f737465705f73796e61707365735f746573742e6d6274_2, ["postspike step+synapses / multi-neuron heterogeneous refractory"]),
    3: (__test_697a68696b65766963685f706f73747370696b655f737465705f73796e61707365735f746573742e6d6274_3, ["postspike step+synapses / injection between decay and step"]),
    4: (__test_697a68696b65766963685f706f73747370696b655f737465705f73796e61707365735f746573742e6d6274_4, ["postspike step+synapses / 100-step round-trip is deterministic"]),
  },
  "izhikevich_postspike_step_main_test.mbt": {
    0: (__test_697a68696b65766963685f706f73747370696b655f737465705f6d61696e5f746573742e6d6274_0, ["step_iz_with_postspike — 200-step stable, no NaN"]),
    1: (__test_697a68696b65766963685f706f73747370696b655f737465705f6d61696e5f746573742e6d6274_1, ["step_iz_with_postspike — fire rate bounded by refractory"]),
    2: (__test_697a68696b65766963685f706f73747370696b655f737465705f6d61696e5f746573742e6d6274_2, ["step_iz_with_postspike — 5-neuron heterogeneous initial fire"]),
    3: (__test_697a68696b65766963685f706f73747370696b655f737465705f6d61696e5f746573742e6d6274_3, ["step_iz_with_postspike — u adaptation continues during refractory"]),
  },
  "izhikevich_postspike_bit_exact_test.mbt": {
    0: (__test_697a68696b65766963685f706f73747370696b655f6269745f65786163745f746573742e6d6274_0, ["postspike timing — tabs counts down 2,1,0 after single fire"]),
    1: (__test_697a68696b65766963685f706f73747370696b655f6269745f65786163745f746573742e6d6274_1, ["postspike timing — tabs_const=5: 5 steps of refractory"]),
    2: (__test_697a68696b65766963685f706f73747370696b655f6269745f65786163745f746573742e6d6274_2, ["postspike timing — second fire after refractory re-extends"]),
  },
  "izhikevich_julia_ge_gi_test.mbt": {
    0: (__test_697a68696b65766963685f6a756c69615f67655f67695f746573742e6d6274_0, ["sample_julia_initial_ge_gi — output length matches n"]),
    1: (__test_697a68696b65766963685f6a756c69615f67655f67695f746573742e6d6274_1, ["sample_julia_initial_ge_gi — ge mean ≈ 40 (4*10=40), gi mean ≈ 200 (20*10=200)"]),
    2: (__test_697a68696b65766963685f6a756c69615f67655f67695f746573742e6d6274_2, ["sample_julia_initial_ge_gi — seeded reproducibility"]),
    3: (__test_697a68696b65766963685f6a756c69615f67655f67695f746573742e6d6274_3, ["sample_julia_initial_ge_gi — ge never negative (clamped)"]),
    4: (__test_697a68696b65766963685f6a756c69615f67655f67695f746573742e6d6274_4, ["sample_julia_initial_ge_gi — ge/gi distributions differ"]),
  },
  "izhikevich_init_with_test.mbt": {
    0: (__test_697a68696b65766963685f696e69745f776974685f746573742e6d6274_0, ["IZ::init_with — custom v/u arrays honoured"]),
    1: (__test_697a68696b65766963685f696e69745f776974685f746573742e6d6274_1, ["IZ::init_with — fire / i / ge / gi zero-initialised"]),
    2: (__test_697a68696b65766963685f696e69745f776974685f746573742e6d6274_2, ["IZ::init_uniform — uniform v_init and u_init"]),
    3: (__test_697a68696b65766963685f696e69745f776974685f746573742e6d6274_3, ["IZ::init_with — step_iz advances v from initial state"]),
    4: (__test_697a68696b65766963685f696e69745f776974685f746573742e6d6274_4, ["IZ::init_with — restart-from-state preserves simulation continuity"]),
  },
  "izhikevich_bitexact_test.mbt": {
    0: (__test_697a68696b65766963685f62697465786163745f746573742e6d6274_0, ["IZ bit-exact — fixed seed, 10pA, 100ms: v[0] at key indices"]),
    1: (__test_697a68696b65766963685f62697465786163745f746573742e6d6274_1, ["IZ bit-exact — two runs from same seed produce identical trace"]),
    2: (__test_697a68696b65766963685f62697465786163745f746573742e6d6274_2, ["IZ bit-exact — single run trace summary (regression guard)"]),
  },
  "izhikevich_bitexact_custom_test.mbt": {
    0: (__test_697a68696b65766963685f62697465786163745f637573746f6d5f746573742e6d6274_0, ["IZ bit-exact — custom RS-like (a=0.02, b=0.2, c=-65, d=8)"]),
    1: (__test_697a68696b65766963685f62697465786163745f637573746f6d5f746573742e6d6274_1, ["IZ bit-exact — custom FS-like (a=0.1, d=2) faster than RS"]),
    2: (__test_697a68696b65766963685f62697465786163745f637573746f6d5f746573742e6d6274_2, ["IZ custom — different c resets the post-fire voltage correctly"]),
  },
  "izhikevich_balanced_postspike_test.mbt": {
    0: (__test_697a68696b65766963685f62616c616e6365645f706f73747370696b655f746573742e6d6274_0, ["izhikevich_balanced_postspike — 1 kHz Bernoulli drive reaches ~1kHz expected input rate"]),
    1: (__test_697a68696b65766963685f62616c616e6365645f706f73747370696b655f746573742e6d6274_1, ["izhikevich_balanced_postspike — refractory caps firing rate"]),
    2: (__test_697a68696b65766963685f62616c616e6365645f706f73747370696b655f746573742e6d6274_2, ["izhikevich_balanced_postspike — fire rate is bounded by 1/(tabs_const*dt)"]),
    3: (__test_697a68696b65766963685f62616c616e6365645f706f73747370696b655f746573742e6d6274_3, ["izhikevich_balanced_postspike — seeded reproducibility"]),
    4: (__test_697a68696b65766963685f62616c616e6365645f706f73747370696b655f746573742e6d6274_4, ["izhikevich_balanced_postspike — refractory decrement tracks per-neuron"]),
  },
  "izhikevich_EPSP_windowed_by_compartment_test.mbt": {
    0: (__test_697a68696b65766963685f455053505f77696e646f7765645f62795f636f6d706172746d656e745f746573742e6d6274_0, ["epsp_normalise_windowed_by_compartment / distinct windows give non-zero std"]),
    1: (__test_697a68696b65766963685f455053505f77696e646f7765645f62795f636f6d706172746d656e745f746573742e6d6274_1, ["epsp_normalise_windowed_by_compartment / baseline std > 0 yields non-trivial z"]),
    2: (__test_697a68696b65766963685f455053505f77696e646f7765645f62795f636f6d706172746d656e745f746573742e6d6274_2, ["epsp_normalise_windowed_by_compartment / multi-compartment per-column z-scores"]),
    3: (__test_697a68696b65766963685f455053505f77696e646f7765645f62795f636f6d706172746d656e745f746573742e6d6274_3, ["epsp_normalise_windowed_by_compartment / invalid observation window returns empty"]),
    4: (__test_697a68696b65766963685f455053505f77696e646f7765645f62795f636f6d706172746d656e745f746573742e6d6274_4, ["epsp_normalise_windowed_by_compartment / invalid baseline window returns empty"]),
  },
  "istdp_test.mbt": {
    0: (__test_69737464705f746573742e6d6274_0, ["IstdpRate::new: defaults match Julia"]),
    1: (__test_69737464705f746573742e6d6274_1, ["IstdpRateVariables::new: zero-initialised"]),
    2: (__test_69737464705f746573742e6d6274_2, ["IstdpRateEntry::new: fields populated"]),
    3: (__test_69737464705f746573742e6d6274_3, ["IstdpRateEntry::change_plasticity: swaps param, preserves vars"]),
    4: (__test_69737464705f746573742e6d6274_4, ["istdp_rate_step: trace decay + spike bump"]),
    5: (__test_69737464705f746573742e6d6274_5, ["istdp_rate_step: pre spike adds eta * (tpost - 2 * r * tau_y)"]),
    6: (__test_69737464705f746573742e6d6274_6, ["istdp_rate_step: post spike adds eta * tpre[j]"]),
    7: (__test_69737464705f746573742e6d6274_7, ["istdp_rate_step: weight clamps at w_max (LTP side)"]),
    8: (__test_69737464705f746573742e6d6274_8, ["istdp_rate_step: weights stay finite over many steps"]),
    9: (__test_69737464705f746573742e6d6274_9, ["istdp_rate_step: integration — random pre/post spikes"]),
  },
  "istdp_potential_test.mbt": {
    0: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_0, ["IstdpPotential::new: defaults match Julia"]),
    1: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_1, ["IstdpPotentialVariables::new: zero-initialised"]),
    2: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_2, ["IstdpPotentialEntry::new: fields populated"]),
    3: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_3, ["IstdpPotentialEntry::change_plasticity: swaps param, preserves vars"]),
    4: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_4, ["istdp_potential_step: tpre decays to 0; tpost low-pass filters v_post"]),
    5: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_5, ["istdp_potential_step: pre spike adds eta * (tpost - v0)"]),
    6: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_6, ["istdp_potential_step: post spike adds eta * tpre[j]"]),
    7: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_7, ["istdp_potential_step: weight clamps at w_max (LTP side)"]),
    8: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_8, ["istdp_potential_step: weights stay finite over many steps"]),
    9: (__test_69737464705f706f74656e7469616c5f746573742e6d6274_9, ["istdp_potential_step: integration — random drive + varying v_post"]),
  },
  "inhomogeneous_poisson_bitexact_test.mbt": {
    0: (__test_696e686f6d6f67656e656f75735f706f6973736f6e5f62697465786163745f746573742e6d6274_0, ["InhomogeneousPoisson — long-run rate converges to r0"]),
    1: (__test_696e686f6d6f67656e656f75735f706f6973736f6e5f62697465786163745f746573742e6d6274_1, ["InhomogeneousPoisson — no NaN states after long sim"]),
    2: (__test_696e686f6d6f67656e656f75735f706f6973736f6e5f62697465786163745f746573742e6d6274_2, ["InhomogeneousPoisson — different seed produces different trajectory"]),
    3: (__test_696e686f6d6f67656e656f75735f706f6973736f6e5f62697465786163745f746573742e6d6274_3, ["InhomogeneousPoisson — β = 0 reduces to homogeneous Poisson"]),
  },
  "infer_receptors_test.mbt": {
    0: (__test_696e6665725f7265636570746f72735f746573742e6d6274_0, ["infer_receptors — empty input → empty result"]),
    1: (__test_696e6665725f7265636570746f72735f746573742e6d6274_1, ["infer_receptors — single glu receptor"]),
    2: (__test_696e6665725f7265636570746f72735f746573742e6d6274_2, ["infer_receptors — single gaba receptor"]),
    3: (__test_696e6665725f7265636570746f72735f746573742e6d6274_3, ["infer_receptors — full 4-receptor set"]),
    4: (__test_696e6665725f7265636570746f72735f746573742e6d6274_4, ["infer_receptors — mixed order preserved"]),
    5: (__test_696e6665725f7265636570746f72735f746573742e6d6274_5, ["infer_receptors — unknown target dropped (no error)"]),
    6: (__test_696e6665725f7265636570746f72735f746573742e6d6274_6, ["count_by_target — sums both buckets"]),
  },
  "infer_receptors_extra_test.mbt": {
    0: (__test_696e6665725f7265636570746f72735f65787472615f746573742e6d6274_0, ["count_glu / count_gaba — separate counts"]),
    1: (__test_696e6665725f7265636570746f72735f65787472615f746573742e6d6274_1, ["count_glu — 0 when no glu receptors"]),
    2: (__test_696e6665725f7265636570746f72735f65787472615f746573742e6d6274_2, ["infer_receptors_from — same as infer_receptors(rs.rec)"]),
    3: (__test_696e6665725f7265636570746f72735f65787472615f746573742e6d6274_3, ["infer_receptors — unknown target is dropped silently"]),
    4: (__test_696e6665725f7265636570746f72735f65787472615f746573742e6d6274_4, ["count_by_target — total receptor count"]),
  },
  "get_synapse_symbol_test.mbt": {
    0: (__test_6765745f73796e617073655f73796d626f6c5f746573742e6d6274_0, ["get_synapse_symbol — glu/gaba pass-through"]),
    1: (__test_6765745f73796e617073655f73796d626f6c5f746573742e6d6274_1, ["get_synapse_symbol — he/ge → glu"]),
    2: (__test_6765745f73796e617073655f73796d626f6c5f746573742e6d6274_2, ["get_synapse_symbol — hi/gi → gaba"]),
    3: (__test_6765745f73796e617073655f73796d626f6c5f746573742e6d6274_3, ["get_synapse_symbol — unknown symbol passthrough"]),
  },
  "empty_stimulus_test.mbt": {
    0: (__test_656d7074795f7374696d756c75735f746573742e6d6274_0, ["EmptyStimulus::new — default"]),
    1: (__test_656d7074795f7374696d756c75735f746573742e6d6274_1, ["EmptyStimulus::with_param — custom EmptyParam"]),
    2: (__test_656d7074795f7374696d756c75735f746573742e6d6274_2, ["stimulate_empty — no-op (doesn't crash)"]),
    3: (__test_656d7074795f7374696d756c75735f746573742e6d6274_3, ["stimulate_empty — multiple calls don't accumulate state"]),
  },
  "dump_params_test.mbt": {
    0: (__test_64756d705f706172616d735f746573742e6d6274_0, ["TripletRule::pfister_alltoall_full defaults match Julia"]),
    1: (__test_64756d705f706172616d735f746573742e6d6274_1, ["TripletRule custom struct literal preserves every field"]),
    2: (__test_64756d705f706172616d735f746573742e6d6274_2, ["NLTAH struct literal preserves every field"]),
    3: (__test_64756d705f706172616d735f746573742e6d6274_3, ["ClopathSTDP::lkd defaults match Julia lkd_stdp literal"]),
    4: (__test_64756d705f706172616d735f746573742e6d6274_4, ["ClopathSTDP::lkd — w_min + w_max bounds"]),
    5: (__test_64756d705f706172616d735f746573742e6d6274_5, ["ClopathSTDP custom struct literal preserves every field"]),
    6: (__test_64756d705f706172616d735f746573742e6d6274_6, ["ISTDP::new defaults match Julia ISTDP struct"]),
    7: (__test_64756d705f706172616d735f746573742e6d6274_7, ["ISTDP::vogels factory matches Julia vogels_istdp()"]),
    8: (__test_64756d705f706172616d735f746573742e6d6274_8, ["ISTDP custom struct literal preserves every field"]),
  },
  "double_exp_current_synapse_test.mbt": {
    0: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_0, ["DoubleExpCurrentParameter::new defaults match Julia"]),
    1: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_1, ["DoubleExpCurrentSynapseVars::new zero-init"]),
    2: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_2, ["update_synapses_double_exp_current — Euler 2-state ODE from spike input"]),
    3: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_3, ["update_synapses_double_exp_current — excitatory vs inhibitory routing"]),
    4: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_4, ["synaptic_current_double_exp — current = -(ge - gi)"]),
    5: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_5, ["synaptic_current_double_exp — pure excitatory drive (gi=0)"]),
    6: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_6, ["synaptic_current_double_exp — pure inhibitory drive (ge=0)"]),
    7: (__test_646f75626c655f6578705f63757272656e745f73796e617073655f746573742e6d6274_7, ["update_synapses — 100 steps stays finite"]),
  },
  "dendneuron_parameter_test.mbt": {
    0: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_0, ["Physiology::human defaults match Julia human_dend"]),
    1: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_1, ["Physiology::mouse defaults match Julia mouse_dend"]),
    2: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_2, ["Physiology::custom preserves all 3 fields"]),
    3: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_3, ["DendNeuronParameter::new defaults — Tripod (2 dendrites)"]),
    4: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_4, ["TripodParameter::new — Tripod shortcut"]),
    5: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_5, ["BallAndStickParameter::new — BallAndStick shortcut"]),
    6: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_6, ["DendNeuronParameter::custom — auto-infer BallAndStick (1 dend)"]),
    7: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_7, ["DendNeuronParameter::custom — auto-infer Tripod (2 dend)"]),
    8: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_8, ["DendNeuronParameter::custom — auto-infer Multipod (3+ dend)"]),
    9: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_9, ["population_from_dend_neuron — returns parameter (Julia passthrough)"]),
    10: (__test_64656e646e6575726f6e5f706172616d657465725f746573742e6d6274_10, ["Physiology fields stored as Float32 (bit-exact with Julia)"]),
  },
  "connection_spiking_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_0, ["SpikingSynapse::new: delays empty by default"]),
    1: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_1, ["SpikingSynapse::set_constant_delay: populates delays"]),
    2: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_2, ["SpikingSynapse::random_with_delays: Normal distribution sampling"]),
    3: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_3, ["SpikingSynapse::forward_synapse: no-delay fast path applies immediately"]),
    4: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_4, ["SpikingSynapse::forward_synapse with delay: schedules events"]),
    5: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_5, ["SpikingSynapse::deliver_pending_synapse: delivers only past-due events"]),
    6: (__test_636f6e6e656374696f6e5f7370696b696e675f746573742e6d6274_6, ["SpikingSynapse: delay_dist composition across steps"]),
  },
  "connection_spiking_iz_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f7370696b696e675f697a5f746573742e6d6274_0, ["SpikingSynapseIZ::new builds empty matrix"]),
    1: (__test_636f6e6e656374696f6e5f7370696b696e675f697a5f746573742e6d6274_1, ["SpikingSynapseIZ::random populates with non-zero weights"]),
    2: (__test_636f6e6e656374696f6e5f7370696b696e675f697a5f746573742e6d6274_2, ["SpikingSynapseIZ::forward deposits weight into post.ge on pre fire"]),
    3: (__test_636f6e6e656374696f6e5f7370696b696e675f697a5f746573742e6d6274_3, ["SpikingSynapseIZ::forward routes gi to post.gi"]),
  },
  "connection_spiking_extra_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_0, ["spiking_synapse::new — (p, μ) random connections"]),
    1: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_1, ["spiking_synapse::new — (p, μ, σ) random connections"]),
    2: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_2, ["spiking_synapse::new — identity (1-to-1) matrix has N nnz"]),
    3: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_3, ["spiking_synapse::new — inhibitory :gi"]),
    4: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_4, ["spiking_synapse::with_name — defaults to empty"]),
    5: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_5, ["spiking_synapse::with_name — sets custom label"]),
    6: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_6, ["spiking_synapse — NoLTP / NoSTP defaults"]),
    7: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_7, ["EmptyConnection::new — defaults to empty name"]),
    8: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_8, ["EmptyConnection::with_name — sets custom label"]),
    9: (__test_636f6e6e656374696f6e5f7370696b696e675f65787472615f746573742e6d6274_9, ["spiking_synapse — explicit dense matrix via from_dense"]),
  },
  "connection_receptor_tripod_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f7265636570746f725f747269706f645f746573742e6d6274_0, ["ReceptorSynapseTripod::new: shapes and initial state"]),
    1: (__test_636f6e6e656374696f6e5f7265636570746f725f747269706f645f746573742e6d6274_1, ["ReceptorSynapseTripod: AMPA target receptor → glu_d1 buffer"]),
    2: (__test_636f6e6e656374696f6e5f7265636570746f725f747269706f645f746573742e6d6274_2, ["ReceptorSynapseTripod: GABAa target → gaba_d1 buffer"]),
    3: (__test_636f6e6e656374696f6e5f7265636570746f725f747269706f645f746573742e6d6274_3, ["ReceptorSynapseTripod: soma compartment routing"]),
    4: (__test_636f6e6e656374696f6e5f7265636570746f725f747269706f645f746573742e6d6274_4, ["step_receptors_tripod_synapse: 2-state ODE consumes buffer, populates ge_out"]),
    5: (__test_636f6e6e656374696f6e5f7265636570746f725f747269706f645f746573742e6d6274_5, ["ReceptorSynapseTripod: non-firing pre → no conductance change"]),
  },
  "connection_receptor_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f7265636570746f725f746573742e6d6274_0, ["ReceptorSynapse::new — defaults: glu=[0,1], gaba=[2,3]"]),
    1: (__test_636f6e6e656374696f6e5f7265636570746f725f746573742e6d6274_1, ["ReceptorSynapse — custom glu_receptors / gaba_receptors"]),
    2: (__test_636f6e6e656374696f6e5f7265636570746f725f746573742e6d6274_2, ["ReceptorSynapse::set_target_receptor — change edge target"]),
    3: (__test_636f6e6e656374696f6e5f7265636570746f725f746573742e6d6274_3, ["forward_receptor_synapse — routes to glu buffer for AMPA"]),
    4: (__test_636f6e6e656374696f6e5f7265636570746f725f746573742e6d6274_4, ["forward_receptor_synapse — routes to gaba buffer for GABAa"]),
  },
  "connection_pinning_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_0, ["PINningSparseSynapse::new 鈥?fields populated"]),
    1: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_1, ["PINningSparseSynapse::new 鈥?P diagonal = 伪, off-diagonal = 0"]),
    2: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_2, ["PINningSparseSynapse::set_target 鈥?sets f[i]"]),
    3: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_3, ["forward_pinning_synapse 鈥?q and g updated from P*W*rJ"]),
    4: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_4, ["forward_pinning_synapse 鈥?resets q and g before update"]),
    5: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_5, ["pinning_plasticity 鈥?C = 1/(1+dot(q,rI)) scaling"]),
    6: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_6, ["pinning_plasticity 鈥?C = 1/(1+0) = 1 when q路rI = 0"]),
    7: (__test_636f6e6e656374696f6e5f70696e6e696e675f746573742e6d6274_7, ["pinning_plasticity 鈥?multi-neuron dot(q, rI)"]),
  },
  "connection_compartment_test.mbt": {
    0: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_0, ["CompartmentSynapseBall::random: matrix shape and target"]),
    1: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_1, ["CompartmentSynapseBall: pre-fire onto :soma → glu_s accumulates"]),
    2: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_2, ["CompartmentSynapseBall: target=:d routes to glu_d not glu_s"]),
    3: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_3, ["CompartmentSynapseBall: :gi sym routes to gaba buffers"]),
    4: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_4, ["CompartmentSynapseBall: non-firing pre → no conductance change"]),
    5: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_5, ["CompartmentSynapseBall: pending queue delays delivery"]),
    6: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_6, ["CompartmentSynapseTripod: target=:d2 routes to glu_d2"]),
    7: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_7, ["CompartmentSynapseTripod: target=:soma routes to glu_s"]),
    8: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_8, ["CompartmentSynapseTripod: :gi sym writes to gaba_d1"]),
    9: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_9, ["CompartmentSynapseTripod: pending queue delays delivery"]),
    10: (__test_636f6e6e656374696f6e5f636f6d706172746d656e745f746573742e6d6274_10, ["CompartmentSynapseBall: rho scaling applied to weights"]),
  },
  "confraveux2025_synapse_test.mbt": {
    0: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_0, ["Confavreux2025Parameter::new defaults match Julia"]),
    1: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_1, ["Confavreux2025SynapseVars::new zero-init"]),
    2: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_2, ["update_synapses_confraveux2025 — AMPA + GABA spike input"]),
    3: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_3, ["update_synapses_confraveux2025 — NMDA coupled to AMPA"]),
    4: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_4, ["update_synapses_confraveux2025 — no spike input decays to zero"]),
    5: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_5, ["synaptic_current_confraveux2025 — weighted AMPA + NMDA"]),
    6: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_6, ["synaptic_current_confraveux2025 — GABA inhibitory drive"]),
    7: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_7, ["synaptic_current_confraveux2025 — α-blend dominance"]),
    8: (__test_636f6e66726176657578323032355f73796e617073655f746573742e6d6274_8, ["update_synapses_confraveux2025 — 1000 steps stays finite"]),
  },
  "compose_test.mbt": {
    0: (__test_636f6d706f73655f746573742e6d6274_0, ["compose builds HeterogeneousModel"]),
    1: (__test_636f6d706f73655f746573742e6d6274_1, ["step_heterogeneous integrates"]),
    2: (__test_636f6d706f73655f746573742e6d6274_2, ["heterogeneous_sim_for runs the loop"]),
    3: (__test_636f6d706f73655f746573742e6d6274_3, ["compose with Poisson stimulus drives the network"]),
    4: (__test_636f6d706f73655f746573742e6d6274_4, ["CurrentStimulus injects i_base into I[i]"]),
    5: (__test_636f6d706f73655f746573742e6d6274_5, ["compose with CurrentStimulus drives spiking"]),
    6: (__test_636f6d706f73655f746573742e6d6274_6, ["get_time_heterogeneous starts at 0 and advances"]),
    7: (__test_636f6d706f73655f746573742e6d6274_7, ["reset_time_heterogeneous resets clock to 0"]),
  },
  "chain_bitexact_test.mbt": {
    0: (__test_636861696e5f62697465786163745f746573742e6d6274_0, ["chain — final voltages stay in expected range"]),
    1: (__test_636861696e5f62697465786163745f746573742e6d6274_1, ["chain — recorded step count matches sim duration"]),
    2: (__test_636861696e5f62697465786163745f746573742e6d6274_2, ["chain — v[0] reaches at least -55mV during 100ms (chain activates)"]),
    3: (__test_636861696e5f62697465786163745f746573742e6d6274_3, ["chain — no NaN values in final state"]),
  },
  "ca_plasticity_test.mbt": {
    0: (__test_63615f706c61737469636974795f746573742e6d6274_0, ["CaPlasticityParameter::new defaults match Julia"]),
    1: (__test_63615f706c61737469636974795f746573742e6d6274_1, ["CaPlasticityParameter::custom sets every field"]),
    2: (__test_63615f706c61737469636974795f746573742e6d6274_2, ["CaPlasticityVariables::new zero-init + correct lengths"]),
    3: (__test_63615f706c61737469636974795f746573742e6d6274_3, ["CaPlasticityEntry::new initialises vars + mut param"]),
    4: (__test_63615f706c61737469636974795f746573742e6d6274_4, ["CaPlasticityEntry::change_plasticity swaps param, preserves vars"]),
    5: (__test_63615f706c61737469636974795f746573742e6d6274_5, ["ca_plasticity_step 鈥?pre-fire adds tpost[i] to W[s]"]),
    6: (__test_63615f706c61737469636974795f746573742e6d6274_6, ["ca_plasticity_step 鈥?post-fire adds tpre[j] to W[s]"]),
    7: (__test_63615f706c61737469636974795f746573742e6d6274_7, ["ca_plasticity_step 鈥?weight clamps at w_max"]),
    8: (__test_63615f706c61737469636974795f746573742e6d6274_8, ["ca_plasticity_step 鈥?trace decay toward zero"]),
    9: (__test_63615f706c61737469636974795f746573742e6d6274_9, ["ca_plasticity_step 鈥?pre-fire bumps tpre[j] by A_pre"]),
    10: (__test_63615f706c61737469636974795f746573742e6d6274_10, ["CaPlasticityEntry::set_ltp_active 鈥?toggles active flag"]),
  },
  "bimodal_kernel_test.mbt": {
    0: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_0, ["kde — known Gaussian dataset, t=mean → peak"]),
    1: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_1, ["kde — empty data returns 0"]),
    2: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_2, ["global_kde — shape matches v_range length"]),
    3: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_3, ["get_maxima — strict local maxima only"]),
    4: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_4, ["get_maxima — plateaus don't count (strictly >)"]),
    5: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_5, ["is_bimodal — single peak → not bimodal"]),
    6: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_6, ["is_bimodal — two equal peaks → bimodal"]),
    7: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_7, ["is_bimodal — one peak too small relative to other → not bimodal"]),
    8: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_8, ["count_maxima — counts only real maxima above ratio"]),
    9: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_9, ["critical_window — unimodal Gaussian data returns small h"]),
    10: (__test_62696d6f64616c5f6b65726e656c5f746573742e6d6274_10, ["all_windows — counter length matches max_b"]),
  },
  "analysis_weight_test.mbt": {
    0: (__test_616e616c797369735f7765696768745f746573742e6d6274_0, ["average_weight — empty intersection returns 0.0"]),
    1: (__test_616e616c797369735f7765696768745f746573742e6d6274_1, ["average_weight — full connectivity uniform weight"]),
    2: (__test_616e616c797369735f7765696768745f746573742e6d6274_2, ["average_weight — partial connectivity (1 of 4 edges)"]),
    3: (__test_616e616c797369735f7765696768745f746573742e6d6274_3, ["average_weight — mixed pre-pop excludes non-listed pre"]),
    4: (__test_616e616c797369735f7765696768745f746573742e6d6274_4, ["average_weight — pre=full, post=subset (filters post)"]),
    5: (__test_616e616c797369735f7765696768745f746573742e6d6274_5, ["average_weight — out-of-bounds post filter returns 0.0"]),
    6: (__test_616e616c797369735f7765696768745f746573742e6d6274_6, ["average_weight_dynamics — uniform weights, 3 timesteps"]),
    7: (__test_616e616c797369735f7765696768745f746573742e6d6274_7, ["average_weight_dynamics — no matched edges returns zeros"]),
    8: (__test_616e616c797369735f7765696768745f746573742e6d6274_8, ["weights_indices — full connectivity"]),
    9: (__test_616e616c797369735f7765696768745f746573742e6d6274_9, ["weights_indices — partial filter (pre=0 only)"]),
    10: (__test_616e616c797369735f7765696768745f746573742e6d6274_10, ["update_weight — multiplies matched edges by factor"]),
    11: (__test_616e616c797369735f7765696768745f746573742e6d6274_11, ["update_weight — no matched edges is no-op"]),
  },
  "analysis_sttc_test.mbt": {
    0: (__test_616e616c797369735f737474635f746573742e6d6274_0, ["tile_fraction — empty train returns 0"]),
    1: (__test_616e616c797369735f737474635f746573742e6d6274_1, ["tile_fraction — single spike inside interval"]),
    2: (__test_616e616c797369735f737474635f746573742e6d6274_2, ["tile_fraction — two spikes far apart (non-overlapping)"]),
    3: (__test_616e616c797369735f737474635f746573742e6d6274_3, ["tile_fraction — two spikes close (overlapping tiles)"]),
    4: (__test_616e616c797369735f737474635f746573742e6d6274_4, ["coincident_fraction — empty A returns 0"]),
    5: (__test_616e616c797369735f737474635f746573742e6d6274_5, ["coincident_fraction — exact match returns 1"]),
    6: (__test_616e616c797369735f737474635f746573742e6d6274_6, ["coincident_fraction — within window returns 1"]),
    7: (__test_616e616c797369735f737474635f746573742e6d6274_7, ["coincident_fraction — outside window returns 0"]),
    8: (__test_616e616c797369735f737474635f746573742e6d6274_8, ["coincident_fraction — half coincident"]),
    9: (__test_616e616c797369735f737474635f746573742e6d6274_9, ["sttc_pair — identical trains give 1.0"]),
    10: (__test_616e616c797369735f737474635f746573742e6d6274_10, ["sttc_pair — symmetric (STTC(A,B) = STTC(B,A))"]),
    11: (__test_616e616c797369735f737474635f746573742e6d6274_11, ["sttc_pair — non-overlapping trains give negative STTC"]),
    12: (__test_616e616c797369735f737474635f746573742e6d6274_12, ["sttc_matrix — diagonal is 1.0, symmetric, bounded in [-1, 1]"]),
  },
  "analysis_smooth_test.mbt": {
    0: (__test_616e616c797369735f736d6f6f74685f746573742e6d6274_0, ["gaussian_smooth — zero sigma returns copy"]),
    1: (__test_616e616c797369735f736d6f6f74685f746573742e6d6274_1, ["gaussian_smooth — symmetric, narrow sigma: preserves signal"]),
    2: (__test_616e616c797369735f736d6f6f74685f746573742e6d6274_2, ["gaussian_smooth — symmetric, wide sigma: averages heavily"]),
    3: (__test_616e616c797369735f736d6f6f74685f746573742e6d6274_3, ["gaussian_smooth — causal (Left): only past samples contribute"]),
    4: (__test_616e616c797369735f736d6f6f74685f746573742e6d6274_4, ["gaussian_smooth — output length equals input length"]),
    5: (__test_616e616c797369735f736d6f6f74685f746573742e6d6274_5, ["gaussian_smooth — boundary renormalisation keeps edge stable"]),
  },
  "analysis_protocols_test.mbt": {
    0: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_0, ["get_poisson_spikes — rate=0 always returns 0"]),
    1: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_1, ["get_poisson_spikes — rate*dt=1 always returns 1"]),
    2: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_2, ["get_poisson_spikes — seeded reproducibility"]),
    3: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_3, ["get_poisson_spikes — frequency approximation (mean ≈ rate*dt)"]),
    4: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_4, ["logrange — 5 points from 1 to 100 (decade endpoints)"]),
    5: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_5, ["logrange — 1 point entry returns [x1]"]),
    6: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_6, ["logrange — n=0 returns empty array"]),
    7: (__test_616e616c797369735f70726f746f636f6c735f746573742e6d6274_7, ["logrange — log spacing is geometric"]),
  },
  "analysis_populations_test.mbt": {
    0: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_0, ["population_indices — 10 + 5 → E=1..10, I=11..15"]),
    1: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_1, ["population_indices — non-overlapping ranges cover 1..N_total"]),
    2: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_2, ["population_indices — three pops (E, I, PV) covers 1..20"]),
    3: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_3, ["population_indices — single pop starts at 1"]),
    4: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_4, ["filter_items — default drops 'noise_*' labels"]),
    5: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_5, ["filter_items_with — DropNoise equivalent to default"]),
    6: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_6, ["filter_items_with — Greater(7) keeps only N > 7"]),
    7: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_7, ["filter_items_with — Less(7) keeps only N < 7"]),
    8: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_8, ["filter_items_with — Equal(5) keeps only N == 5"]),
    9: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_9, ["average_conn_strength — A→A block mean / μ"]),
    10: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_10, ["average_conn_strength — μ = 2.0 halves the means"]),
    11: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_11, ["average_conn_strength — single pop, all-zeros block"]),
    12: (__test_616e616c797369735f706f70756c6174696f6e735f746573742e6d6274_12, ["PopIndex::length — end - start + 1"]),
  },
  "analysis_poisson_input_test.mbt": {
    0: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_0, ["poisson_input_single — rate=0 returns all-false"]),
    1: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_1, ["poisson_input_single — high rate (500Hz, 100ms) produces multiple spikes"]),
    2: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_2, ["poisson_input_single — seeded reproducibility"]),
    3: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_3, ["poisson_input_single — 100Hz × 1000ms → ~100 spikes"]),
    4: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_4, ["poisson_input_single — spike indices are strictly increasing"]),
    5: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_5, ["poisson_input — matrix shape (neurons × n_spikes)"]),
    6: (__test_616e616c797369735f706f6973736f6e5f696e7075745f746573742e6d6274_6, ["poisson_input — different neurons have different patterns (likely)"]),
  },
  "analysis_lif_closedform_test.mbt": {
    0: (__test_616e616c797369735f6c69665f636c6f736564666f726d5f746573742e6d6274_0, ["LIF closed-form — v(t) tracks analytical solution within 2mV"]),
    1: (__test_616e616c797369735f6c69665f636c6f736564666f726d5f746573742e6d6274_1, ["LIF closed-form — v(t) approaches V_rest + I*R for large t"]),
    2: (__test_616e616c797369735f6c69665f636c6f736564666f726d5f746573742e6d6274_2, ["LIF closed-form — decay to rest when I=0"]),
    3: (__test_616e616c797369735f6c69665f636c6f736564666f726d5f746573742e6d6274_3, ["LIF closed-form — exponential decay constant matches τm"]),
  },
  "analysis_isi_test.mbt": {
    0: (__test_616e616c797369735f6973695f746573742e6d6274_0, ["isi_cv2_one — empty spike train returns 0"]),
    1: (__test_616e616c797369735f6973695f746573742e6d6274_1, ["isi_cv2_one — single spike returns 0 (only 1 ISI)"]),
    2: (__test_616e616c797369735f6973695f746573742e6d6274_2, ["isi_cv2_one — two spikes return 0 (only 1 ISI, no CV2 pair)"]),
    3: (__test_616e616c797369735f6973695f746573742e6d6274_3, ["isi_cv2_one — perfectly regular spikes return 0"]),
    4: (__test_616e616c797369735f6973695f746573742e6d6274_4, ["isi_cv2_one — alternating ISIs (10, 20, 10, 20) yield CV2=2/3"]),
    5: (__test_616e616c797369735f6973695f746573742e6d6274_5, ["isi_cv2_one — irregular spikes yield non-zero CV2"]),
    6: (__test_616e616c797369735f6973695f746573742e6d6274_6, ["isi_cv2_one — CV2 bounded by [0, 2]"]),
    7: (__test_616e616c797369735f6973695f746573742e6d6274_7, ["isi_cv2 — per-neuron CV2 with mixed regularity"]),
    8: (__test_616e616c797369735f6973695f746573742e6d6274_8, ["isi_cv2 — all silent population returns all-zero CV2 array"]),
  },
  "analysis_intervals_test.mbt": {
    0: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_0, ["time_in_interval — empty intervals returns false"]),
    1: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_1, ["time_in_interval — x at start of interval (inclusive)"]),
    2: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_2, ["time_in_interval — x at end of interval (inclusive)"]),
    3: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_3, ["time_in_interval — x in middle of second interval"]),
    4: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_4, ["time_in_interval — x outside all intervals"]),
    5: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_5, ["start_interval — returns start of containing interval"]),
    6: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_6, ["start_interval — returns -1.0F if no interval contains x"]),
    7: (__test_616e616c797369735f696e74657276616c735f746573742e6d6274_7, ["end_interval — returns end of containing interval"]),
  },
  "analysis_EPSP_window_extra_test.mbt": {
    0: (__test_616e616c797369735f455053505f77696e646f775f65787472615f746573742e6d6274_0, ["exc_peak_window — multi-compartment picks correct column"]),
    1: (__test_616e616c797369735f455053505f77696e646f775f65787472615f746573742e6d6274_1, ["inh_trough_window — multi-compartment picks correct column"]),
    2: (__test_616e616c797369735f455053505f77696e646f775f65787472615f746573742e6d6274_2, ["exc_peak_with_time — boundary case (peak at spiketime)"]),
    3: (__test_616e616c797369735f455053505f77696e646f775f65787472615f746573742e6d6274_3, ["exc_peak_with_time — multi-compartment"]),
  },
  "analysis_EPSP_test.mbt": {
    0: (__test_616e616c797369735f455053505f746573742e6d6274_0, ["exc_peak — single-compartment voltage rises then falls"]),
    1: (__test_616e616c797369735f455053505f746573742e6d6274_1, ["exc_peak — spiketime offset skips leading samples"]),
    2: (__test_616e616c797369735f455053505f746573742e6d6274_2, ["exc_peak — rest offset subtracted from peak"]),
    3: (__test_616e616c797369735f455053505f746573742e6d6274_3, ["inh_trough — inhibitory dip below rest"]),
    4: (__test_616e616c797369735f455053505f746573742e6d6274_4, ["exc_peak — multi-compartment selects the right column"]),
  },
  "analysis_EPSP_pair_test.mbt": {
    0: (__test_616e616c797369735f455053505f706169725f746573742e6d6274_0, ["epsp_pair — both exc peak and inh trough in one pass"]),
    1: (__test_616e616c797369735f455053505f706169725f746573742e6d6274_1, ["epsp_pair — monotonic voltage returns (0, 0)"]),
    2: (__test_616e616c797369735f455053505f706169725f746573742e6d6274_2, ["epsp_pair — single-pass matches individual exc_peak + inh_trough"]),
    3: (__test_616e616c797369735f455053505f706169725f746573742e6d6274_3, ["epsp_pair_window — bounded window"]),
    4: (__test_616e616c797369735f455053505f706169725f746573742e6d6274_4, ["epsp_pair_window — empty/inverted window returns (0, 0)"]),
  },
  "analysis_EPSP_normalise_test.mbt": {
    0: (__test_616e616c797369735f455053505f6e6f726d616c6973655f746573742e6d6274_0, ["epsp_normalise — constant trace returns all zeros (zero std)"]),
    1: (__test_616e616c797369735f455053505f6e6f726d616c6973655f746573742e6d6274_1, ["epsp_normalise — z-scores have mean ≈ 0 and unit variance"]),
    2: (__test_616e616c797369735f455053505f6e6f726d616c6973655f746573742e6d6274_2, ["epsp_normalise — multi-compartment selects the right column"]),
    3: (__test_616e616c797369735f455053505f6e6f726d616c6973655f746573742e6d6274_3, ["epsp_normalise — invalid window returns empty"]),
    4: (__test_616e616c797369735f455053505f6e6f726d616c6973655f746573742e6d6274_4, ["epsp_normalise — output length matches window size"]),
  },
  "analysis_EPSP_normalise_by_compartment_test.mbt": {
    0: (__test_616e616c797369735f455053505f6e6f726d616c6973655f62795f636f6d706172746d656e745f746573742e6d6274_0, ["epsp_normalise_by_compartment — multi-compartment returns per-column z-scores"]),
    1: (__test_616e616c797369735f455053505f6e6f726d616c6973655f62795f636f6d706172746d656e745f746573742e6d6274_1, ["epsp_normalise_by_compartment — invalid window returns empty"]),
    2: (__test_616e616c797369735f455053505f6e6f726d616c6973655f62795f636f6d706172746d656e745f746573742e6d6274_2, ["epsp_normalise_by_compartment — single-compartment case matches epsp_normalise"]),
    3: (__test_616e616c797369735f455053505f6e6f726d616c6973655f62795f636f6d706172746d656e745f746573742e6d6274_3, ["epsp_normalise_by_compartment — degenerate compartment returns zeros"]),
    4: (__test_616e616c797369735f455053505f6e6f726d616c6973655f62795f636f6d706172746d656e745f746573742e6d6274_4, ["epsp_normalise_by_compartment — different std per compartment"]),
  },
  "analysis_EPSP_extensions_test.mbt": {
    0: (__test_616e616c797369735f455053505f657874656e73696f6e735f746573742e6d6274_0, ["exc_peak_with_time — peak value and time index"]),
    1: (__test_616e616c797369735f455053505f657874656e73696f6e735f746573742e6d6274_1, ["exc_peak_with_time — plateau (first occurrence wins)"]),
    2: (__test_616e616c797369735f455053505f657874656e73696f6e735f746573742e6d6274_2, ["inh_trough_with_time — trough and time"]),
    3: (__test_616e616c797369735f455053505f657874656e73696f6e735f746573742e6d6274_3, ["exc_peak_window — peak within [t_start, t_end]"]),
    4: (__test_616e616c797369735f455053505f657874656e73696f6e735f746573742e6d6274_4, ["exc_peak_window — empty/inverted window returns 0"]),
    5: (__test_616e616c797369735f455053505f657874656e73696f6e735f746573742e6d6274_5, ["inh_trough_window — trough within [t_start, t_end]"]),
  },
  "analysis_EI_balance_test.mbt": {
    0: (__test_616e616c797369735f45495f62616c616e63655f746573742e6d6274_0, ["kei_balance_per_neuron — constant voltage equals target → index 0"]),
    1: (__test_616e616c797369735f45495f62616c616e63655f746573742e6d6274_1, ["kei_balance_per_neuron — per-neuron settling at t=5"]),
    2: (__test_616e616c797369735f45495f62616c616e63655f746573742e6d6274_2, ["kei_balance_per_neuron — outside tolerance → 0"]),
    3: (__test_616e616c797369735f45495f62616c616e63655f746573742e6d6274_3, ["kei_balance_per_neuron — exactly at tolerance boundary"]),
    4: (__test_616e616c797369735f45495f62616c616e63655f746573742e6d6274_4, ["kei_balance_population — settles at t=5 (mean reaches -55)"]),
    5: (__test_616e616c797369735f45495f62616c616e63655f746573742e6d6274_5, ["kei_balance_population — never settles → returns 0"]),
  },
  "aggregate_scaling_test.mbt": {
    0: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_0, ["AggregateScalingParameter::new defaults"]),
    1: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_1, ["AggregateScalingParameter::uniform (N=5, 10Hz)"]),
    2: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_2, ["AggregateScalingParameter::uniform custom tau_a/tau_e"]),
    3: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_3, ["AggregateScaling::new initial state"]),
    4: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_4, ["aggregate_scaling_forward: y decays without spike"]),
    5: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_5, ["aggregate_scaling_forward: y bumps on spike"]),
    6: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_6, ["aggregate_scaling_plasticity: periodic weight rescaling"]),
    7: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_7, ["aggregate_scaling_plasticity: skip on non-periodic step"]),
    8: (__test_6167677265676174655f7363616c696e675f746573742e6d6274_8, ["aggregate_scaling_plasticity: zero wt guarded"]),
  },
  "adex_sinexp_bitexact_test.mbt": {
    0: (__test_616465785f73696e6578705f62697465786163745f746573742e6d6274_0, ["AdExSinExp — tonic drive produces no NaN state"]),
    1: (__test_616465785f73696e6578705f62697465786163745f746573742e6d6274_1, ["AdExSinExp — voltages stay in physiological range"]),
    2: (__test_616465785f73696e6578705f62697465786163745f746573742e6d6274_2, ["AdExSinExp — fires at least once under tonic drive"]),
    3: (__test_616465785f73696e6578705f62697465786163745f746573742e6d6274_3, ["AdExSinExp — firing rate bounded (no runaway)"]),
  },
}


///|
impl MoonBit_Test_Driver for MoonBit_Test_Driver_Internal_No_Args with fn run_test(
  _,
  filename,
  index,
  handle_start,
  handle_result,
  error_to_string,
) {
  if moonbit_test_driver_internal_no_args_tests.0.get(filename)
    is Some(index_map) &&
    index_map.get(index) is Some((f, attrs)) {
    let name = if attrs is [name, ..] { name } else { "" }
    if MOONBIT_TEST_DRIVER_INTERNAL_IS_NATIVE {
      for attr in attrs {
        if attr is [.. "panic", ..] {
          raise MoonBitTestDriverInternalSkipTest(name~)
        }
      }
    }
    handle_start()
    moonbit_test_driver_internal_catch_error(
      f,
      on_ok=() => handle_result(name, "", false),
      on_err=err => handle_result(name, error_to_string(err), false),
    )
    true
  } else {
    false
  }
}
///|
#cfg(any(target="native", target="llvm"))
#test_entry
fn main {
  let async_tests = []
  for arg in moonbit_test_driver_internal_native_parse_args() {
    moonbit_test_driver_internal_do_execute(async_tests, arg.0, arg.1)
  }
  MoonBit_Async_Test_Driver_Impl::run_async_tests(async_tests)
}

///|
#cfg(target="js")
#test_entry
fn main {
  let test_params = moonbit_test_driver_js_parse_args()
  moonbit_test_driver_internal_execute(test_params)
  moonbit_test_driver_finish()
}

///|
#cfg(any(target="wasm", target="wasm-gc"))
#test_entry
fn main {
  ()
}

///|
#cfg(target="js")
extern "js" fn moonbit_test_driver_js_parse_args() -> FixedArray[
  MoonBitTestDriverInternalTestEntry,
] =
  #|() => {
  #|  try {
  #|    let testParams = []
  #|    // check if there is command line argument
  #|    if (process.argv.length > 2) {
  #|      const testArgs = JSON.parse(process.argv[2])
  #|      testParams = testArgs.file_and_index.flatMap(([file, ranges]) => {
  #|        let arr = []
  #|        for (let range of ranges) {
  #|          for (let i = range.start; i < range.end; i++) {
  #|            arr.push([file, i])
  #|          }
  #|        }
  #|        return arr
  #|      })
  #|      return testParams
  #|    }
  #|  } catch (error) {
  #|    console.error('failed to parse args: ', error.message)
  #|    process.exit(1)
  #|  }
  #|}

///|
/// Executes selected JavaScript tests from parsed test entries.
#cfg(target="js")
pub fn moonbit_test_driver_internal_execute(
  tests : FixedArray[MoonBitTestDriverInternalTestEntry],
) -> Unit {
  let async_tests = []
  for i in 0..<tests.length() {
    let entry = tests[i]
    moonbit_test_driver_internal_do_execute(
      async_tests,
      entry.filename(),
      entry.index(),
    )
  }
  MoonBit_Async_Test_Driver_Impl::run_async_tests(async_tests)
}

///|
#cfg(target="wasm")
let moonbit_test_driver_internal_wasm_async_tests : Array[async () -> Unit] = []

///|
/// Executes one selected WebAssembly test and stores async work for finalization.
#cfg(target="wasm")
pub fn moonbit_test_driver_internal_execute(
  filename : MoonbitTestDriverInternalExternString,
  index : Int,
) -> Unit {
  let filename = moonbit_test_driver_internal_get_file_name(filename)
  moonbit_test_driver_internal_do_execute(
    moonbit_test_driver_internal_wasm_async_tests, filename, index,
  )
}

///|
/// Executes one selected WebAssembly GC test and drains async work immediately.
#cfg(target="wasm-gc")
pub fn moonbit_test_driver_internal_execute(
  filename : MoonbitTestDriverInternalExternString,
  index : Int,
) -> Unit {
  let filename = moonbit_test_driver_internal_get_file_name(filename)
  let async_tests = []
  moonbit_test_driver_internal_do_execute(async_tests, filename, index)
  MoonBit_Async_Test_Driver_Impl::run_async_tests(async_tests)
}

///|
/// Finishes pending WebAssembly async tests and coverage collection.
#cfg(target="wasm")
pub fn moonbit_test_driver_finish() -> Unit {
  MoonBit_Async_Test_Driver_Impl::run_async_tests(
    moonbit_test_driver_internal_wasm_async_tests,
  )
  
}

///|
/// Finishes coverage collection for backends without pending async work.
#cfg(any(target="js", target="wasm-gc"))
pub fn moonbit_test_driver_finish() -> Unit {
  
}

///|
fn moonbit_test_driver_internal_do_execute(
  async_tests : Array[async () -> Unit],
  filename : String,
  index : Int,
) -> Unit {
  // These two are internalized to avoid unused warnings

  fn handle_start() {
    let file_name = filename.escape()
    @moonbitlang/core/prelude.println("----- BEGIN MOON TEST RESULT -----")
    @moonbitlang/core/prelude.println(
      "{\"type\":\"start\",\"file\":\{file_name},\"index\":\{index}}",
    )
    @moonbitlang/core/prelude.println("----- END MOON TEST RESULT -----")
  }

  fn handle_result(_testname : String, message : String, skipped : Bool) {
    // In WASM, this function is called by the external test driver, but here
    // in native mode there's nothing that calls it, so we call it manually.
    if !skipped && MOONBIT_TEST_DRIVER_INTERNAL_IS_NATIVE {
      
    }
    let file_name = filename.escape()
    let message = message.escape()
    @moonbitlang/core/prelude.println("----- BEGIN MOON TEST RESULT -----")
    @moonbitlang/core/prelude.println(
      "{\"type\":\"result\",\"file\":\{file_name},\"index\":\{index},\"message\":\{message}}",
    )
    @moonbitlang/core/prelude.println("----- END MOON TEST RESULT -----")
  }

  fn error_to_string(err : Error) -> String {
    match err {
      @moonbitlang/core/prelude.Failure::Failure(e)
      | @moonbitlang/core/prelude.InspectError::InspectError(e)
      | @moonbitlang/core/prelude.SnapshotError::SnapshotError(e)
      | MoonBitTestDriverInternalJsError(e) => e
      e => moonbit_test_driver_internal_error_to_string(e)
    }
  }

  try {
    if !(MoonBit_Test_Driver_Internal_No_Args::run_test(
        async_tests, filename, index, handle_start, handle_result, error_to_string,
      ) ||
      MoonBit_Test_Driver_Internal_With_Args::run_test(
        async_tests, filename, index, handle_start, handle_result, error_to_string,
      ) ||
      MoonBit_Test_Driver_Internal_Async_No_Args::run_test(
        async_tests, filename, index, handle_start, handle_result, error_to_string,
      ) ||
      MoonBit_Test_Driver_Internal_Async_With_Args::run_test(
        async_tests, filename, index, handle_start, handle_result, error_to_string,
      ) ||
      MoonBit_Test_Driver_Internal_With_Bench_Args::run_test(
        async_tests, filename, index, handle_start, handle_result, error_to_string,
      )) {
      raise MoonBitTestDriverInternalSkipTest(name="")
    }
  } catch {
    err => {
      guard err is MoonBitTestDriverInternalSkipTest(name~)
      handle_result(name, "skipped test", true)
    }
  }
}
