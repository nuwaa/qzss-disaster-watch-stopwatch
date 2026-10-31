param(
    [string]$Port = 'COM11',
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WatchIp
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$tokenFile = 'work/watch-token.txt'
if (-not (Test-Path -LiteralPath $tokenFile)) {
    throw 'Missing work/watch-token.txt. Run scripts/setup_watch.ps1 first.'
}
New-Item -ItemType Directory -Force 'logs' | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$env:PYTHONPATH = "${root}\src;${root}\work\deps"
$env:PYTHONUTF8 = '1'
$env:QZSS_WATCH_TOKEN = (Get-Content -LiteralPath $tokenFile -Raw).Trim()
$rawLog = "logs/live-$stamp-raw.jsonl"
$reportLog = "logs/live-$stamp-reports.jsonl"
Write-Host "Receiving on $Port at 9600 bps for 5 minutes. Ctrl+C stops early."
Write-Host 'Progress appears every 30 seconds.'
Write-Host "Raw log: $rawLog"
Write-Host "Decoded log: $reportLog"
try {
    python -m qzss_watch.cli receive $Port --baud 9600 --enable-sfrbx --raw-log $rawLog --output $reportLog --device-url "http://${WatchIp}:8080/report" --seconds 300 1> $null
    if ($LASTEXITCODE -ne 0) { throw "Receiver failed with exit code $LASTEXITCODE" }
}
finally {
    Remove-Item Env:QZSS_WATCH_TOKEN -ErrorAction SilentlyContinue
}
