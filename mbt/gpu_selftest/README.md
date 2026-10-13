# gpu_selftest

Exercises gpu/'s wrappers against known-answer cases and prints the numbers
rather than asserting, so a failure is visible in a log instead of a test
count. Run it with:

    moon run ./gpu_selftest --target native

## What this machine can and cannot test

There is no CUDA library here, so every call takes the stub's CPU route. That is
deliberate: the accelerated path is the one that cannot be checked without a
card, and the fallback is the one that has to be right on every machine
including CI.

## A caveat that matters, found by running it

`FixedArray::from_array` is a view, not a copy, and a write performed by the C
side through a `FixedArray` does not appear in the `Array` it was made from.
Reads are fine -- the memory is shared -- but a result the stub computes in
place is not visible to MoonBit code holding the `Array`.

So the output buffers have to be true `FixedArray`s that MoonBit reads back
directly, not `Array`s wrapped on the way out. That is why main.mbt prints
through `FixedArray` values. Passing an `Array` and expecting to see the result
is the mistake this file exists to document.

## Expected output without a CUDA library

    try_load   : -1000  (0 = found, -1000 = absent)
    loaded     : false
    sgemm rc   : 0
    sgemm out  : 58 64 | 139 154
    sdot rc    : 0
    sdot out   : 32
    snrm2 rc   : 0
    snrm2 out  : 5
    fill rc    : 0
    fill out   : 7 7 7
    axpy rc    : 0
    axpy out   : 12 24 36
    bad-arg rc : -2000
