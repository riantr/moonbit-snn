// empty_connection.mbt — port of
// `refs/SNNModels.jl/src/connections/empty.jl`.
//
// Julia has an `EmptySynapse` (a singleton subtype of `AbstractConnection`)
// that does nothing — used in `compose(...)` to mark a slot where no
// synapse is wired. MoonBit has no abstract type hierarchy so we model
// it as a concrete unit struct.

///|
/// A no-op connection type. Returns immediately from every method.
/// Mirrors Julia's `EmptySynapse` (zero-allocation connection used as
/// a placeholder in `compose(...)` when a slot is intentionally left
/// unwired).
pub struct EmptyConnection {
  // Optional human-readable label (mirrors Julia's `name` kwarg).
  name : String
}

///|
/// Construct an EmptyConnection. Defaults to `name = ""`.
pub fn EmptyConnection::new() -> EmptyConnection {
  { name: "" }
}

///|
/// Set the human-readable label. Returns a new struct (MoonBit fields
/// are immutable); the caller must rebind:
///
///     let ec = ec.with_name("placeholder")
pub fn EmptyConnection::with_name(_c : EmptyConnection, name : String) -> EmptyConnection {
  { name: name }
}