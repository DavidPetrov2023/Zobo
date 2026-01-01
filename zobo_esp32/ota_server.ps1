# OTA Server Script for Zobo ESP32
# Builds firmware and starts HTTP server for OTA updates

param(
    [int]$Port = 8080,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Colors for output
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Info { Write-Host $args -ForegroundColor Cyan }
function Write-Warn { Write-Host $args -ForegroundColor Yellow }

Write-Info "=== Zobo OTA Server ==="
Write-Info ""

# Get local IP address
$LocalIP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object {
    $_.InterfaceAlias -notmatch 'Loopback' -and $_.IPAddress -notmatch '^169\.'
} | Select-Object -First 1).IPAddress

if (-not $LocalIP) {
    $LocalIP = "localhost"
}

# Build firmware if not skipped
if (-not $SkipBuild) {
    Write-Info "Building firmware..."

    # Find ESP-IDF environment
    $EspIdfCmd = Get-Command "idf.py" -ErrorAction SilentlyContinue

    if (-not $EspIdfCmd) {
        Write-Warn "idf.py not found in PATH. Please run this script from ESP-IDF terminal."
        Write-Warn "Or run: . $env:IDF_PATH\export.ps1"
        exit 1
    }

    Push-Location $ScriptDir
    try {
        & idf.py build
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Build failed!"
            exit 1
        }
        Write-Success "Build successful!"
    }
    finally {
        Pop-Location
    }
}

# Check if firmware exists
$FirmwarePath = Join-Path $ScriptDir "build\zobo_esp32.bin"
if (-not (Test-Path $FirmwarePath)) {
    Write-Error "Firmware not found: $FirmwarePath"
    exit 1
}

# Get firmware info
$FirmwareInfo = Get-Item $FirmwarePath
$FirmwareSize = [math]::Round($FirmwareInfo.Length / 1024, 2)
$FirmwareDate = $FirmwareInfo.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")

# Extract version from main.c
$MainC = Get-Content (Join-Path $ScriptDir "main\main.c") -Raw
if ($MainC -match '#define\s+FIRMWARE_VERSION\s+"([^"]+)"') {
    $Version = $Matches[1]
} else {
    $Version = "unknown"
}

Write-Info ""
Write-Success "=== Firmware Info ==="
Write-Host "Version:  $Version"
Write-Host "Size:     $FirmwareSize KB"
Write-Host "Built:    $FirmwareDate"
Write-Info ""
Write-Success "=== OTA Server Starting ==="
Write-Host "URL:      http://${LocalIP}:${Port}/zobo_esp32.bin"
Write-Host "Version:  http://${LocalIP}:${Port}/version.json"
Write-Info ""
Write-Warn "Press Ctrl+C to stop the server"
Write-Info ""

# Create version.json
$VersionJson = @{
    version = $Version
    size = $FirmwareInfo.Length
    date = $FirmwareDate
    url = "http://${LocalIP}:${Port}/zobo_esp32.bin"
} | ConvertTo-Json

$VersionJsonPath = Join-Path $ScriptDir "build\version.json"
$VersionJson | Out-File -FilePath $VersionJsonPath -Encoding UTF8

# Start HTTP server
Push-Location (Join-Path $ScriptDir "build")
try {
    python -m http.server $Port
}
finally {
    Pop-Location
}
