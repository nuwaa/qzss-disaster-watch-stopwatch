$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$wifiName = Read-Host 'Wi-Fi SSID for StopWatch'
$securePassword = Read-Host 'Wi-Fi password' -AsSecureString
$wifiPassword = [System.Net.NetworkCredential]::new('', $securePassword).Password
if ([string]::IsNullOrWhiteSpace($wifiName) -or [string]::IsNullOrEmpty($wifiPassword)) {
    throw 'Enter both SSID and password.'
}

$tokenBytes = New-Object byte[] 24
$generator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try { $generator.GetBytes($tokenBytes) } finally { $generator.Dispose() }
$token = [BitConverter]::ToString($tokenBytes).Replace('-', '')
$ssidLiteral = ConvertTo-Json -InputObject $wifiName -Compress
$passwordLiteral = ConvertTo-Json -InputObject $wifiPassword -Compress
$tokenLiteral = ConvertTo-Json -InputObject $token -Compress

$header = @"
#pragma once
#define WATCH_WIFI_SSID $ssidLiteral
#define WATCH_WIFI_PASSWORD $passwordLiteral
#define WATCH_TOKEN $tokenLiteral
"@
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path (Get-Location).Path 'firmware/stopwatch/include/secrets.h'), $header, $utf8)
New-Item -ItemType Directory -Force 'work' | Out-Null
[System.IO.File]::WriteAllText((Join-Path (Get-Location).Path 'work/watch-token.txt'), $token, [System.Text.Encoding]::ASCII)
$wifiPassword = $null
Write-Host 'Local settings saved. Secret files are ignored by Git.'
