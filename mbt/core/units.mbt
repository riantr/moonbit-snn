// Unit constants for the SNN simulator.
//
// Bit-exact port of SNNModels.jl/src/utils/unit.jl. Each constant is the
// result of `Float32(...)` in Julia applied to a Float64 expression.
//
// MoonBit naming convention:
//   - Julia `ms`, `mV`, `pA`, `nS`, `mA` etc. (lowercase first letter)
//     are kept as-is.
//   - Julia `Hz`, `kHz`, `M`, `MΩ`, `GΩ`, `Ω` (uppercase first letter or
//     non-ASCII) are renamed to `hz`, `khz`, `M`, `mohm`, `gohm`, `ohm`.
//     `M` keeps the original (uppercase) name because it is the SI
//     symbol for molar concentration and is a real Float32 constant.
//   - Julia `μA` / `μF` (unicode mu) drop their unicode alias; the ASCII
//     `uA` / `uF` is kept.
//
// Verification: each unit equals the Julia value, last bit. Use
// `units_test.mbt` to compare against the Julia reference.

///|
/// Base unit constants. All values are `Float` (Float32) to match Julia
/// behaviour exactly. `mV`, `ms`, `nS`, `pA`, `pF`, `GΩ` are normalised to
/// 1.0 — i.e. internal computations use these as the conversion factor.

// Length (internal: Float32(1e2) = 100.0f0)
pub let metre : Float = 100.0F

// Float32(100.0f0 / Float64(1e2)) = 1.0f0
pub let cm : Float = 1.0F

// Float32(100.0f0 / Float64(1e3)) = Float32(0.1) = 0.10000000149011612f0
pub let mm : Float = 0.1F

// Float32(100.0f0 / Float64(1e6)) = Float32(1e-4)
pub let um : Float = 0.0001F

// Float32(100.0f0 / Float64(1e9)) = Float32(1e-7)
pub let nm : Float = 1.0e-7F

// Float32(cm * cm) = 1.0f0
pub let cm2 : Float = 1.0F

// Float32(metre * metre) = 10000.0f0
pub let m2 : Float = 10000.0F

// Float32(um * um) = Float32(1e-8)
pub let um2 : Float = 1.0e-8F

// Float32(nm * nm) = Float32(1e-14)
pub let nm2 : Float = 1.0e-14F

// Float32(1e3) = 1000.0f0
pub let second : Float = 1000.0F

// alias for second
pub let s : Float = 1000.0F

// Float32(1000.0f0 / Float64(1e3)) = 1.0f0
pub let ms : Float = 1.0F

// Julia: Hz = 1 / second |> Float32 = Float32(1.0 / 1000.0f0) = 0.001f0
// Renamed from `Hz` → `hz` to satisfy MoonBit's lowercase-initial rule.
pub let hz : Float = 0.001F

// Julia: kHz = Hz * 1e3 |> Float32 = 1.0f0
// Renamed from `kHz` → `khz`.
pub let khz : Float = 1.0F

// Float32(1e3) = 1000.0f0
pub let voltage : Float = 1000.0F

// Float32(1000.0f0 / Float64(1e3)) = 1.0f0
pub let mV : Float = 1.0F

// Float32(1e12)
pub let ampere : Float = 1.0e12F

// Float32(1e12 / Float64(1e3)) = Float32(1e9)
pub let mA : Float = 1.0e9F

// Float32(1e12 / Float64(1e6)) = Float32(1e6)
pub let uA : Float = 1.0e6F

// Float32(1e12 / Float64(1e9)) = Float32(1e3) = 1000.0f0
pub let nA : Float = 1000.0F

// Float32(1e12 / Float64(1e12)) = 1.0f0
pub let pA : Float = 1.0F

// Float32(1e12)
pub let farad : Float = 1.0e12F

// Float32(1e12 / Float64(1e3)) = Float32(1e9)
pub let mF : Float = 1.0e9F

// Float32(1e12 / Float64(1e6)) = Float32(1e6)
pub let uF : Float = 1.0e6F

// Float32(1e12 / Float64(1e9)) = 1000.0f0
pub let nF : Float = 1000.0F

// Float32(1e12 / Float64(1e12)) = 1.0f0
pub let pF : Float = 1.0F

// Float32(1e9)
pub let siemens : Float = 1.0e9F

// Float32(1e9 / Float64(1e3)) = Float32(1e6)
pub let mS : Float = 1.0e6F

// alias for mS
pub let msiemens : Float = 1.0e6F

// Float32(1e9 / Float64(1e9)) = 1.0f0
pub let nS : Float = 1.0F

// alias for nS
pub let nsiemens : Float = 1.0F

// Julia: Ω = 1 / siemens |> Float32 = Float32(1e-9) = 1.0e-9f0
// Renamed from `Ω` → `ohm` (MoonBit lex limitation on non-ASCII).
pub let ohm : Float = 1.0e-9F

// Julia: MΩ = Ω * 1e6 |> Float32 = Float32(0.001) = 0.001f0
// Renamed from `MΩ` → `mohm`.
pub let mohm : Float = 0.001F

// Julia: GΩ = Ω * 1e9 |> Float32 = 1.0f0
// Renamed from `GΩ` → `gohm`.
pub let gohm : Float = 1.0F

// Julia: M = 1e6 |> Float32 = 1.0e6f0 (Molar concentration)
pub const M : Float = 1.0e6F

// Float32(1e6 / Float64(1e3)) = 1000.0f0
pub let mM : Float = 1000.0F

// Float32(1e6 * 1e-6) = 1.0f0
pub let uM : Float = 1.0F

// Float32(1e6 * 1e-9) = 0.001f0
pub let nM : Float = 0.001F
