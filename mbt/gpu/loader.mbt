// loader.mbt -- Availability check for the accelerated path.
//
// One process-wide handle, mirroring linalg_gpu's loader.mbt. The difference is
// what a caller does with the answer: here the point is not to fail fast at
// startup but to let a hot loop hoist the availability check out of itself and
// branch once on the result it kept.
//
// There is deliberately no `ensure_loaded` on the wrappers. A dispatch inside
// linear_forward that re-checks availability per call would add a call and a
// branch to a function that currently has neither; the caller should load once
// and then branch on the result it kept.

///|
/// Load liblinear_accel now and cache the handle for the rest of the process.
///
/// Returns 0 on success; ERR_LIB_NOT_LOADED otherwise. After a success every
/// call in ffi.mbt resolves against the loaded library and later calls return
/// 0 without touching the dynamic linker again.
///
/// After a failure this keeps returning ERR_LIB_NOT_LOADED -- there is no retry
/// inside a single call. A caller that changes LINEAR_ACCEL_LIB should call it
/// again, and the stub re-runs its search.
pub fn try_load() -> Int {
  la_try_load()
}

///|
/// True if liblinear_accel is loaded and usable.
///
/// Both this and `try_load` go through `la_try_load`, which caches its handle:
/// the stub returns the cached result without touching the dynamic linker
/// again. There is deliberately no separate "just probe" entry point -- one
/// FFI symbol that does both beats two that have to be kept in agreement about
/// what "loaded" means, and a probe that does not load cannot answer the
/// question callers actually have.
pub fn loaded() -> Bool {
  la_try_load() == 0
}
