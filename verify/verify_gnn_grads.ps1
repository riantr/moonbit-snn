# verify_gnn_grads.ps1 -- run a check INSIDE the library package.
#
# The gradient gate is the default, but the is-main flip below is the
# only way anything can run in this package at all, so it is shared
# rather than copied: `run_gnn_train_demo.ps1` calls straight through to
# this script with a different runner. Two copies of a flip/restore that
# has to leave a library byte-identical is one more way for the library
# to end up with `is-main` still in it.
#
#   & .\verify\verify_gnn_grads.ps1
#   & .\verify\verify_gnn_grads.ps1 -RunnerFile gnn_train_runner.mbt -LogName train_out.log
#
# WHY THE PARAMETERS ARE NAMED `*File`/`*Name` AND NOT `runner`/`log`:
# PowerShell variable names are CASE-INSENSITIVE, so a `$Runner`
# parameter and a `$runner` local are THE SAME VARIABLE. The first
# version had both, and the local assignment overwrote the parameter
# with a full path -- so the staged copy was named
# `zz_D:\...\gnn_gradcheck_runner.mbt` and Copy-Item rejected it with
# "the format of the given path is not supported", which says nothing
# about the actual mistake. The locals below all carry a `Path`
# suffix so no parameter can ever collide with one again.
#
# WHY A CHECK EXISTS AND WHY IT FLIPS is-main
#
# The obvious home for a runner is `mbt/examples/gnn_gradcheck/`, a
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
# So checks run INSIDE the library package, where there is no import at
# all. This script does the flip, runs, and reverts:
#
#   1. copy verify/<RunnerFile> into mbt/zz_<RunnerFile>
#   2. add "is-main": true to mbt/moon.pkg
#   3. moon run .      (from mbt/)
#   4. restore both files
#
# The library-side functions (gnn_gradcheck.mbt, gnn_paramcheck.mbt,
# gnn_train_demo.mbt) are the durable artifact; a runner is a thin
# driver over them.
#
# Exit code mirrors `moon run`'s, not a failure count.

param(
  [string]$RunnerFile = 'gnn_gradcheck_runner.mbt',
  [string]$LogName    = 'gradcheck_out.log',
  [string]$ShowLike   = '==='
)

$ErrorActionPreference = 'Continue'

# This script lives in <repo>\verify\, so the repo root is two levels up.
$verifyDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootPath    = Split-Path -Parent $verifyDir
$mbtDir      = Join-Path $rootPath 'mbt'
$runnerPath  = Join-Path $verifyDir $RunnerFile
$stagedPath  = Join-Path $mbtDir ('zz_' + $RunnerFile)
$pkgPath     = Join-Path $mbtDir 'moon.pkg'
$moonExe     = 'C:\Users\31379\.moon\bin\moon.exe'

if (-not (Test-Path $runnerPath)) { Write-Host "missing $runnerPath"; exit 1 }
if (-not (Test-Path $pkgPath))    { Write-Host "missing $pkgPath";    exit 1 }
if ($env:GNN_CHECK_VERBOSE) {
    Write-Host "  verifyDir  = [$verifyDir]"
    Write-Host "  mbtDir     = [$mbtDir]"
    Write-Host "  runnerPath = [$runnerPath]"
    Write-Host "  stagedPath = [$stagedPath]"
}

$pkgOriginal = [System.IO.File]::ReadAllText($pkgPath)
$buildDir    = Join-Path $rootPath '_build'
$logPath     = Join-Path $buildDir $LogName
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }

try {
    Copy-Item $runnerPath $stagedPath -Force

    # Add "is-main": true to the options block, idempotently.
    if ($pkgOriginal -notmatch '"is-main"') {
        $flipped = $pkgOriginal -replace '(?m)^(\s*)\)$', "`$1`r`n  `"is-main`": true,`r`n`$1)"
        [System.IO.File]::WriteAllText($pkgPath, $flipped, [System.Text.UTF8Encoding]::new($false))
    }

    Push-Location $mbtDir
    Write-Host "running $RunnerFile (this compiles the whole package)..."
    & $moonExe run . 2>&1 | Out-File -FilePath $logPath -Encoding utf8
    $code = $LASTEXITCODE
    Pop-Location

    # Show only the marker lines, not the 5000 lines of warnings.
    Get-Content $logPath |
        Select-String -Pattern $ShowLike |
        ForEach-Object { $_.Line }
    Write-Host ""
    Write-Host "full log: $logPath"
}
finally {
    # Always restore the library, even if the run threw.
    if (Test-Path $stagedPath) {
        Move-Item $stagedPath (Join-Path $buildDir ([System.IO.Path]::GetFileName($stagedPath))) -Force
    }
    [System.IO.File]::WriteAllText($pkgPath, $pkgOriginal, [System.Text.UTF8Encoding]::new($false))
}

exit $code
