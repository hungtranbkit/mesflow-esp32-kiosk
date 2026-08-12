[CmdletBinding()]
param(
    [string]$Port = 'COM6'
)

$ErrorActionPreference = 'Stop'
$device = Get-CimInstance Win32_SerialPort | Where-Object DeviceID -eq $Port
if (-not $device) {
    throw "Serial port $Port does not exist."
}

Write-Host "Opening $($device.Name) [$Port] at 115200 baud. Press Ctrl+C to close."
& arduino-cli monitor --port $Port --config baudrate=115200
if ($LASTEXITCODE -ne 0) {
    throw "Serial monitor failed with exit code $LASTEXITCODE."
}
