# run_release_bench.ps1 -- run a package in release mode and report wall time.
#
# Why this exists
# ---------------
# Everything else in verify/ runs `moon run .` or `moon test`, which default to
# the debug profile. On 2026-10-13 that was measured to be 14x slower than
# release on nn/linear_forward: 837 ms per call in debug against 60.2 ms in
# release, same source, same machine. Any performance number taken from a debug
# run in this repo is off by that factor, so timing work has to go through
# here.
#
# The 14x figure came from subtracting a reps=0 baseline from a reps=20 run.
# That subtraction is required, not pedantry: on Windows the `time` builtin
# reports roughly 0.02 s of user CPU for a run whose wall clock is 2 s, so
# user/sys are useless here and only real elapsed time minus a fixed baseline
# gives a usable figure.
#
# Usage (from the repo root):
#   & .\verify\run_release_bench.ps1 -Package .\mbt\bench_lin
#
#   -Package    a main package directory, default .\mbt\bench_lin
#   -Repeats    extra moon build invocations to amortise, default 1
#
# Exit code mirrors `moon run`'s, not a timing verdict: a script that reported
# "too slow" as a failure would make this a gate rather than a measurement.

[CmdletBinding()]
param(
  [string] $Package = '.\mbt\bench_lin',
  [int]    $Repeats  = 1
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pkgPath  = Join-Path $repoRoot $Package

if (-not (Test-Path $pkgPath)) {
  Write-Error "no such package: $Package (looked in $pkgPath)"
  exit 2
}

Push-Location $pkgPath
try {
  Write-Host "building release in $pkgPath"
  & moon build --release --target native .
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $exe = Get-ChildItem -Path _build -Recurse -Filter '*.exe' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
  if (-not $exe) {
    Write-Error "build reported success but produced no .exe under _build"
    exit 2
  }

  Write-Host "running $($exe.Name) $($Repeats)x"
  for ($i = 1; $i -le $Repeats; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $exe.FullName
    $code = $LASTEXITCODE
    $sw.Stop()
    # Milliseconds, one decimal, so two runs are visibly distinguishable.
    $ms = [Math]::Round($sw.Elapsed.TotalMilliseconds, 1)
    Write-Host ("  run {0}: {1} ms (exit {2})" -f $i, $ms, $code)
    if ($code -ne 0) { exit $code }
  }
}
finally {
  Pop-Location
}