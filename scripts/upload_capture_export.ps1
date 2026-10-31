param([string]$Port = 'COM10')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path -LiteralPath 'firmware/stopwatch-log-export/.pio/build/m5stack-stopwatch-log-export/firmware.bin')) {
    throw 'Run scripts/build_capture_export.ps1 first.'
}
python scripts/check_watch_port.py $Port
if ($LASTEXITCODE -ne 0) { throw "Upload target $Port is not a connected StopWatch port." }
$drive = @('Z:', 'Y:', 'X:', 'W:') | Where-Object { -not (Test-Path ($_.ToString() + '\')) } | Select-Object -First 1
if (-not $drive) { throw 'No free drive letter for a short upload path.' }
& subst $drive $root
if ($LASTEXITCODE -ne 0) { throw 'Unable to create temporary upload drive.' }
$previousEnv = @{}
foreach ($name in @('PYTHONPATH', 'PLATFORMIO_CORE_DIR', 'TEMP', 'TMP')) {
    $previousEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:PYTHONPATH = "$drive\work\pio"
    $env:PLATFORMIO_CORE_DIR = "$drive\work\pio-core"
    $env:TEMP = "$drive\work\tmp"
    $env:TMP = $env:TEMP
    $ErrorActionPreference = 'Continue'
    python -m platformio run -d "$drive\firmware\stopwatch-log-export" -e m5stack-stopwatch-log-export -t upload --upload-port $Port 2>&1 | Tee-Object -FilePath "$drive\work\capture-export-upload.log"
    $uploadCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($uploadCode -ne 0) { throw "Upload failed with exit code $uploadCode" }
    Write-Host 'Capture export upload complete.'
}
finally {
    Set-Location $root
    & subst $drive /D | Out-Null
    foreach ($name in $previousEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnv[$name], 'Process')
    }
}
