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

# Scratch for the captured streams. `_scratch/` is gitignored, and the files
# are overwritten rather than deleted, so nothing here has to remove
# anything.
$scratch = Join-Path $repoRoot '_scratch\bench'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

function Invoke-Native {
  # Run a native command with its streams captured to files, and print them.
  #
  # Not `2>&1`: in Windows PowerShell 5.1 that converts stderr into error
  # records, and with `$ErrorActionPreference = 'Stop'` the first one is
  # terminating -- so a build that merely SAID something on stderr, as moon
  # does on every successful build ("Finished. moon: no work to do"), killed
  # the script. Redirecting to files sidesteps the error-record machinery
  # entirely, and the verdict comes from the exit code, which is the only
  # thing that knows whether the command worked.
  param([string] $File, [string[]] $Arguments, [string] $WorkingDirectory)

  $outFile = Join-Path $scratch 'stdout.txt'
  $errFile = Join-Path $scratch 'stderr.txt'
  $start = @{
    FilePath             = $File
    WorkingDirectory     = $WorkingDirectory
    NoNewWindow          = $true
    Wait                 = $true
    PassThru             = $true
    RedirectStandardOutput = $outFile
    RedirectStandardError  = $errFile
  }
  # `Start-Process` rejects an empty `-ArgumentList`, and the benchmark
  # executable takes none.
  if ($Arguments -and $Arguments.Count -gt 0) { $start['ArgumentList'] = $Arguments }
  $p = Start-Process @start
  foreach ($f in @($outFile, $errFile)) {
    if (Test-Path $f) { Get-Content $f | Out-Host }
  }
  return $p.ExitCode
}

function Find-BenchExe {
  # The executable lands in the MODULE's _build, not the package's. A package
  # inside a module is built as part of that module, so `mbt/bench_lin` has no
  # `_build` of its own and the binary shows up as
  # `mbt/_build/native/release/build/bench_lin/bench_lin.exe`. Searching only
  # the package's own `_build` finds nothing, and the harness then reports
  # "build reported success but produced no .exe" for a build that worked.
  #
  # The name is matched rather than the extension, because the binary has no
  # extension off Windows.
  param([string] $PkgPath, [string] $RepoRoot)

  $leaf = Split-Path -Leaf $PkgPath
  $roots = @((Join-Path $PkgPath '_build'))

  $moduleRoot = $PkgPath
  while ($moduleRoot -and -not (Test-Path (Join-Path $moduleRoot 'moon.mod'))) {
    $parent = Split-Path -Parent $moduleRoot
    if (-not $parent -or $parent -eq $moduleRoot) { $moduleRoot = $null }
    else { $moduleRoot = $parent }
  }
  if ($moduleRoot) { $roots += (Join-Path $moduleRoot '_build') }

  $found = @()
  foreach ($root in $roots) {
    if (Test-Path $root) {
      $found += Get-ChildItem -Path $root -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq "$leaf.exe" -or $_.Name -eq $leaf }
    }
  }
  return $found | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

if (-not (Test-Path $pkgPath)) {
  Write-Error "no such package: $Package (looked in $pkgPath)"
  exit 2
}

Push-Location $pkgPath
try {
  Write-Host "building release in $pkgPath"
  $buildExit = Invoke-Native -File 'moon' `
      -Arguments @('build', '--release', '--target', 'native', '.') `
      -WorkingDirectory $pkgPath
  if ($buildExit -ne 0) { exit $buildExit }

  $exe = Find-BenchExe -PkgPath $pkgPath -RepoRoot $repoRoot
  if (-not $exe) {
    Write-Error "build reported success but produced no executable for '$leaf' under the module's _build"
    exit 2
  }

  Write-Host "running $($exe.Name) $($Repeats)x"
  for ($i = 1; $i -le $Repeats; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-Native -File $exe.FullName -Arguments @() -WorkingDirectory $pkgPath
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

# Explicit, so that a caller reading $LASTEXITCODE gets a verdict rather
# than whatever the last native command happened to leave behind.
exit 0