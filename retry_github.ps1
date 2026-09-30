$ErrorActionPreference = 'Stop'
Set-Location D:\src\MiniMax\Projects\MoonBit\moonbit-snn
for ($i = 1; $i -le 3; $i++) {
  Write-Host "--- attempt $i ---"
  git push github master 2>&1 | Select-Object -First 3
  if ($LASTEXITCODE -eq 0) { break } else { Start-Sleep -Seconds 8 }
}