param(
    [string]$Port = 'COM3',
    [int]$Baud = 115200,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot '.venv/Scripts/python.exe'
$build = Join-Path $PSScriptRoot 'artifacts/radiomaster-nomad'
$factoryImages = @(Get-ChildItem -LiteralPath $build -Filter '*.factory.bin' -File)
if ($factoryImages.Count -ne 1) {
    throw 'Build and package radiomaster-nomad first; exactly one factory image must be present.'
}
$factory = $factoryImages[0].FullName
$filesystemName = $factoryImages[0].Name.Replace('firmware-', 'littlefs-').Replace('.factory.bin', '.bin')
$filesystem = Join-Path $build $filesystemName
if (!(Test-Path -LiteralPath $filesystem)) {
    throw "Missing filesystem image: $filesystem"
}

$backupDirectory = Join-Path $PSScriptRoot ('backups/nomad-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backupDirectory | Out-Null
$backup = Join-Path $backupDirectory 'main-esp32-original.bin'

# Explicit chip selection rejects a C3 before any write. These operations reset the main MCU.
& $python -m esptool --chip esp32 --port $Port --baud $Baud --after hard-reset flash-id 2>&1 |
    Tee-Object -FilePath (Join-Path $backupDirectory 'flash-id.log')
if ($LASTEXITCODE -ne 0) { throw 'Main ESP32 identification failed; no installation attempted.' }

& $python -m esptool --chip esp32 --port $Port --baud $Baud --before default-reset --after hard-reset read-flash 0 ALL $backup 2>&1 |
    Tee-Object -FilePath (Join-Path $backupDirectory 'backup.log')
if ($LASTEXITCODE -ne 0) { throw 'Full backup failed; no installation attempted.' }
if ((Get-Item -LiteralPath $backup).Length -lt 4MB) {
    throw 'Detected flash is smaller than the 4 MB target; no installation attempted.'
}
Get-FileHash -LiteralPath $backup -Algorithm SHA256 |
    Format-List | Out-File -LiteralPath (Join-Path $backupDirectory 'backup-sha256.txt')
Get-FileHash -LiteralPath $factory, $filesystem -Algorithm SHA256 |
    Format-List | Out-File -LiteralPath (Join-Path $backupDirectory 'candidate-images-sha256.txt')

if ($Install) {
    # Factory data covers bootloader, partition table and app; LittleFS is a separate image.
    & $python -m esptool --chip esp32 --port $Port --baud $Baud --before default-reset --after hard-reset write-flash `
        0x0 $factory 0x300000 $filesystem 2>&1 |
        Tee-Object -FilePath (Join-Path $backupDirectory 'install.log')
    if ($LASTEXITCODE -ne 0) { throw "Installation failed; preserve the backup at $backup" }
}
Write-Host "Backup: $backup"
Write-Host $(if ($Install) { 'Nomad installed on the main ESP32.' } else { 'Backup complete; firmware unchanged.' })
