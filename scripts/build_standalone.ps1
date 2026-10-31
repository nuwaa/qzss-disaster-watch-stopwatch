param([switch]$NotificationTest, [switch]$DecoderTest, [switch]$LifecycleTest, [switch]$DcxTest)
$ErrorActionPreference = 'Stop'
if ((@($NotificationTest, $DecoderTest, $LifecycleTest, $DcxTest) | Where-Object { $_ }).Count -gt 1) { throw 'Choose only one test mode.' }
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$drive = @('Z:', 'Y:', 'X:', 'W:') | Where-Object { -not (Test-Path ($_.ToString() + '\')) } | Select-Object -First 1
if (-not $drive) { throw 'No free drive letter for a short build path.' }
& subst $drive $root
if ($LASTEXITCODE -ne 0) { throw 'Unable to create temporary build drive.' }
$previousEnv = @{}
foreach ($name in @('PYTHONPATH', 'PLATFORMIO_CORE_DIR', 'TEMP', 'TMP')) {
    $previousEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:PYTHONPATH = "$drive\work\pio"
    $env:PLATFORMIO_CORE_DIR = "$drive\work\pio-core"
    $env:TEMP = "$drive\work\tmp"
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force $env:TEMP | Out-Null
    $ErrorActionPreference = 'Continue'
    $pioEnv = if ($NotificationTest) { 'm5stack-stopwatch-notification-test' } elseif ($DecoderTest) { 'm5stack-stopwatch-decoder-test' } elseif ($LifecycleTest) { 'm5stack-stopwatch-lifecycle-test' } elseif ($DcxTest) { 'm5stack-stopwatch-dcx-test' } else { 'm5stack-stopwatch-standalone' }
    python -m platformio run -d "$drive\firmware\stopwatch-standalone" -e $pioEnv 2>&1 | Tee-Object -FilePath "$drive\work\standalone-build.log"
    $buildCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($buildCode -ne 0) { throw "Build failed with exit code $buildCode" }
    Write-Host "$pioEnv build succeeded."
}
finally {
    Set-Location $root
    & subst $drive /D | Out-Null
    foreach ($name in $previousEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnv[$name], 'Process')
    }
}
