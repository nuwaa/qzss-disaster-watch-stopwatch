$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path -LiteralPath 'firmware/stopwatch/include/secrets.h')) {
    throw 'Run scripts/setup_watch.ps1 first.'
}

# A short temporary drive path avoids Windows compiler command-line limits.
$drive = @('Z:', 'Y:', 'X:', 'W:') | Where-Object { -not (Test-Path ($_.ToString() + '\')) } | Select-Object -First 1
if (-not $drive) { throw 'No free drive letter for a short build path.' }
& subst $drive $root
if ($LASTEXITCODE -ne 0) { throw 'Unable to create temporary build drive.' }
try {
    $env:PYTHONPATH = "$drive\work\pio"
    $env:PLATFORMIO_CORE_DIR = "$drive\work\pio-core"
    $env:TEMP = "$drive\work\tmp"
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force $env:TEMP | Out-Null
    $ErrorActionPreference = 'Continue'
    python -m platformio run -d "$drive\firmware\stopwatch" 2>&1 | Tee-Object -FilePath "$drive\work\watch-build.log"
    $buildCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($buildCode -ne 0) { throw "Build failed with exit code $buildCode" }
    Write-Host 'Build succeeded. Firmware is ready for upload.'
}
finally {
    Set-Location $root
    & subst $drive /D | Out-Null
}
