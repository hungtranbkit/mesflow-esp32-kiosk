[CmdletBinding()]
param(
    [string]$Port = 'COM6'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot 'esp_build.ps1'
$sketchPath = Join-Path $projectRoot 'esp'
$fqbn = 'esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=default_8MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default'

function Get-SerialPort([string]$Name) {
    Get-CimInstance Win32_SerialPort | Where-Object DeviceID -eq $Name
}

$device = Get-SerialPort $Port
if (-not $device) {
    if ($PSBoundParameters.ContainsKey('Port')) {
        throw "Requested serial port $Port does not exist."
    }
    $espPorts = @(Get-CimInstance Win32_SerialPort | Where-Object {
        $_.PNPDeviceID -match '^USB\\VID_303A&PID_1001'
    })
    if ($espPorts.Count -ne 1) {
        throw "COM6 is unavailable and exactly one ESP32 USB serial device could not be identified. Found $($espPorts.Count)."
    }
    $device = $espPorts[0]
    $Port = $device.DeviceID
}

Write-Host "Selected device: $($device.Name) [$Port] $($device.PNPDeviceID)"
& $buildScript
if ($LASTEXITCODE -ne 0) {
    throw 'Build did not pass; upload was not attempted.'
}

& arduino-cli upload --port $Port --fqbn $fqbn $sketchPath
if ($LASTEXITCODE -ne 0) {
    throw "Upload failed with exit code $LASTEXITCODE."
}

Write-Host "ESP flash PASS on $Port."
