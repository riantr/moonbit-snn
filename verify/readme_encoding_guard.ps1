# readme_encoding_guard.ps1 -- run the encoding guard over mbt/README.md.
#
#   & .\verify\readme_encoding_guard.ps1                    # check only
#   & .\verify\readme_encoding_guard.ps1 -Repair            # repair + gate + write
#   & .\verify\readme_encoding_guard.ps1 -Repair -DryRun    # repair + gate, no write
#   & .\verify\readme_encoding_guard.ps1 -SelfTest          # prove the model
#   & .\verify\readme_encoding_guard.ps1 -EndToEnd          # prove the repair
#
# WHY A POWERSHELL WRAPPER AT ALL
#
# The engine is Python because the inversion is arithmetic on bytes, and
# because `git show` without --no-pager opens a pager and waits for input when
# its output is a pipe -- which hangs the caller with no output and no error.
# That is inside the engine.  This wrapper exists so the guard is run the same
# way as the other two checks in this directory, and so the exit code survives:
# the engine returns 1 for "damaged", which is what a CI step needs, and a
# bare `python ... | Out-Null` would throw that away.
#
# THE EXIT CODE IS THE POINT
#
#   0  clean, or repaired and the gate passed
#   1  damage found (check), or usage problem
#   2  the repair was refused: the gate failed
#
# A tool that repairs a file but cannot fail is worse than no tool, because the
# damage it leaves behind reads like a finished job.  This one refused to write
# three times during development, and each refusal was a real defect.
#
# PowerShell note: this script does NOT read or write the README itself.  An
# earlier round of damage to this very file came from Get-Content | Set-Content
# under a cp936 locale, and that is the one operation this guard exists to undo.
# All text handling stays in the engine, which uses explicit UTF-8.
#
# AND YES, THE LOCALS CARRY SUFFIXES, FOR THE REASON verify_gnn_grads.ps1
# gives at length: `$args` is a PowerShell AUTOMATIC variable.  Naming a local
# `$args` works right up until it silently is not the local any more.  So:
# $guardArgs, not $args; $rc, not $lastExit.
[CmdletBinding()]
param(
    [switch]$Repair,
    [switch]$DryRun,
    [switch]$SelfTest,
    [switch]$EndToEnd,
    [string]$DonorRev = '',
    [string[]]$Symbol = @()
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$guard = Join-Path $PSScriptRoot 'readme_encoding_guard.py'
$e2e = Join-Path $PSScriptRoot 'readme_encoding_guard_e2e.py'

$script:GuardExit = 0

function Invoke-Guard {
    param([string[]]$GuardArgs)
    # The exit code goes in a script variable rather than a return value: a
    # PowerShell function returns everything written to its output stream, so
    # `return $LASTEXITCODE` after a command that printed a report hands back
    # the report AND the code, and every caller then sees an array.
    $report = & python -X utf8 $guard @GuardArgs
    $script:GuardExit = $LASTEXITCODE
    $report | Write-Output
}

if ($SelfTest) { Invoke-Guard @('selftest'); exit $script:GuardExit }
if ($EndToEnd) {
    & python -X utf8 $e2e
    exit $LASTEXITCODE
}

$guardArgs = @()
if ($Repair) { $guardArgs += 'repair' } else { $guardArgs += 'check' }
if ($DryRun -and $Repair) { $guardArgs += '--dry-run' }
if ($DonorRev) { $guardArgs += @('--donor', $DonorRev) }
foreach ($s in $Symbol) { $guardArgs += @('--symbol', $s) }

Invoke-Guard $guardArgs
$rc = $script:GuardExit
switch ($rc) {
    0 { Write-Host "VERDICT: PASS" }
    1 { if ($Repair) { Write-Host "VERDICT: usage problem" }
         else { Write-Host "VERDICT: DAMAGED -- run again with -Repair" } }
    2 { Write-Host "VERDICT: REFUSED -- the gate failed, nothing was written" }
    default { Write-Host "VERDICT: unexpected exit $rc" }
}
exit $rc
