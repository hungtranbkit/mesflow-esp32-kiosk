[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sketchPath = Join-Path $projectRoot 'esp'
$outputPath = Join-Path $projectRoot 'build\esp'
$fqbn = 'esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=default_8MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default'

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    throw 'arduino-cli is not available on PATH.'
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
Write-Host "Building $sketchPath"
Write-Host "FQBN: $fqbn"
& arduino-cli compile --fqbn $fqbn --output-dir $outputPath $sketchPath
if ($LASTEXITCODE -ne 0) {
    throw "Arduino compile failed with exit code $LASTEXITCODE."
}

Write-Host 'ESP build PASS.'
