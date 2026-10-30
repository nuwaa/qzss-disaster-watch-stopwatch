param(
    [string]$GnssPort = 'COM11',
    [string]$WatchPort = 'COM10',
    [switch]$SampleOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$tokenFile = 'work/watch-token.txt'
if (-not (Test-Path -LiteralPath $tokenFile)) {
    throw 'Missing work/watch-token.txt. Run scripts/setup_watch.ps1 first.'
}
$env:PYTHONPATH = "${root}\src;${root}\work\deps"
$env:PYTHONUTF8 = '1'
$env:QZSS_WATCH_TOKEN = (Get-Content -LiteralPath $tokenFile -Raw).Trim()
try {
    if ($SampleOnly) {
        python -m qzss_watch.cli sample --watch-usb $WatchPort 1> $null
    } else {
        New-Item -ItemType Directory -Force 'logs' | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $rawLog = "logs/live-usb-$stamp-raw.jsonl"
        $reportLog = "logs/live-usb-$stamp-reports.jsonl"
        Write-Host "Receiving GNSS on $GnssPort and sending to StopWatch on $WatchPort for 5 minutes."
        Write-Host "Raw log: $rawLog"
        Write-Host "Decoded log: $reportLog"
        python -m qzss_watch.cli receive $GnssPort --baud 9600 --enable-sfrbx --watch-usb $WatchPort --seconds 300 --raw-log $rawLog --output $reportLog 1> $null
    }
    if ($LASTEXITCODE -ne 0) { throw "Receiver failed with exit code $LASTEXITCODE" }
}
finally {
    Remove-Item Env:QZSS_WATCH_TOKEN -ErrorAction SilentlyContinue
}
