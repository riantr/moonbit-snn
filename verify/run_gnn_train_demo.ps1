# run_gnn_train_demo.ps1 -- run the end-to-end training demo (v0.155.0).
#
# A thin entry point. The is-main flip, the run, and the restore all
# live in verify_gnn_grads.ps1, because that is the only mechanism in
# this package that can execute a check at all (see its header for why
# `examples/` and `moon test` are both unavailable). Duplicating that
# flip here would give the library two chances to be left with
# `is-main: true` in moon.pkg.
#
# The demo prints its own tables; `-ShowLike` is widened from the
# gradient gate's '===' because this run is a report, not a one-line
# verdict.
#
# Usage (from the repo root):
#   & .\verify\run_gnn_train_demo.ps1

$ErrorActionPreference = 'Continue'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $here 'verify_gnn_grads.ps1') `
    -RunnerFile 'gnn_train_runner.mbt' `
    -LogName    'train_demo_out.log' `
    -ShowLike   '===|GCN|GIN|MPNN|PNA|GAT|LinearProbe|learned|fitted|probe|VERDICT|DID NOT'
exit $LASTEXITCODE
