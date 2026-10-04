# verify_gnn_grads.ps1 -- run the GNN gradient checks (Batch Z).
#
# WHY THIS EXISTS AND WHY IT FLIPS is-main
#
# The obvious home for the runner is `mbt/examples/gnn_gradcheck/`, a
# sub-package that imports `riantr/snn_mbt`. That does not work in
# moon 0.1.20260920 on this project: a freshly created sub-package
# cannot resolve ANY symbol from the parent module -- not even `Graph`
# -- so the import never binds. A sub-package's qualified name has to
# be registered by a committed `pkg.generated.mbti` before the import
# resolves, and `moon info` cannot generate that file because
# generation needs the import to already resolve. Hand-writing it does
# not help. The pre-existing `examples/chain` only appears to work
# because its `pkg.generated.mbti` and `_build` are committed
# artifacts from an older toolchain, and every symbol it uses predates
# the interface snapshot moon now reads.
#
# `moon test` is not an option either: this package has 511 .mbt files
# and Moon inlines every source path into the Windows command line,
# which caps out at 32K (CreateProcessW).
#
# So the checks run INSIDE the library package, where there is no
# import at all. This script does the flip, runs, and reverts:
#
#   1. copy verify/gnn_gradcheck_runner.mbt into mbt/
#   2. add "is-main": true to mbt/moon.pkg
#   3. moon run .      (from mbt/)
#   4. restore both files
#
# The library-side check functions (gnn_gradcheck.mbt,
# gnn_paramcheck.mbt) are the durable artifact; the runner is a thin
# driver over them.
#
# Usage (from the repo root):
#   pwsh -NoProfile -File verify\verify_gnn_grads.ps1
#
# Exit code mirrors the number of failed checks, capped at 1.

$ErrorActionPreference = 'Continue'

# This script lives in <repo>\verify\, so the repo root is two levels up.
$verifyDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root      = Split-Path -Parent $verifyDir
$mbtDir    = Join-Path $root 'mbt'
$runner    = Join-Path $verifyDir 'gnn_gradcheck_runner.mbt'
$staged    = Join-Path $mbtDir 'zz_gradcheck_runner.mbt'
$pkgFile   = Join-Path $mbtDir 'moon.pkg'
$moon      = 'C:\Users\31379\.moon\bin\moon.exe'

if (-not (Test-Path $runner))  { Write-Host "missing $runner";  exit 1 }
if (-not (Test-Path $pkgFile)) { Write-Host "missing $pkgFile"; exit 1 }

$pkgOriginal = [System.IO.File]::ReadAllText($pkgFile)
$logFile     = Join-Path $root '_build\gradcheck_out.log'

try {
    Copy-Item $runner $staged -Force

    # Add "is-main": true to the options block, idempotently.
    if ($pkgOriginal -notmatch '"is-main"') {
        $flipped = $pkgOriginal -replace '(?m)^(\s*)\)$', "`$1`r`n  `"is-main`": true,`r`n`$1)"
        [System.IO.File]::WriteAllText($pkgFile, $flipped, [System.Text.UTF8Encoding]::new($false))
    }

    Push-Location $mbtDir
    Write-Host "running gradient checks (this compiles the whole package)..."
    & $moon run . 2>&1 | Out-File -FilePath $logFile -Encoding utf8
    $code = $LASTEXITCODE
    Pop-Location

    # Show only the verdict lines, not the 5000 lines of warnings.
    Get-Content $logFile |
        Select-String -Pattern '===' |
        ForEach-Object { $_.Line }
    Write-Host ""
    Write-Host "full log: $logFile"
}
finally {
    # Always restore the library, even if the run threw.
    if (Test-Path $staged) { Move-Item $staged (Join-Path $root '_build\zz_gradcheck_runner.mbt') -Force }
    [System.IO.File]::WriteAllText($pkgFile, $pkgOriginal, [System.Text.UTF8Encoding]::new($false))
}

exit $code
