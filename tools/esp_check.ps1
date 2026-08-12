[CmdletBinding()]
param(
    [string]$Port = 'COM6'
)

$ErrorActionPreference = 'Stop'
$buildScript = Join-Path $PSScriptRoot 'esp_build.ps1'

& $buildScript
if ($LASTEXITCODE -ne 0) {
    throw 'Build check failed.'
}

$device = Get-CimInstance Win32_SerialPort | Where-Object DeviceID -eq $Port
if (-not $device) {
    throw "Build PASS, but serial port $Port is not present. Not ready to flash."
}

Write-Host "Port check PASS: $($device.Name) [$Port]"
Write-Host "PNP identity: $($device.PNPDeviceID)"
Write-Host 'Readiness PASS: firmware compiled and the selected port exists. No flash was performed.'
