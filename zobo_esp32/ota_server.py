#!/usr/bin/env python3
"""
OTA Server for Zobo ESP32
Automatically builds firmware and starts HTTP server for OTA updates.
Run from any terminal - no ESP-IDF environment needed.
"""

import os
import sys
import json
import socket
import subprocess
import re
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler
from datetime import datetime

# Configuration
PORT = 8080
SCRIPT_DIR = Path(__file__).parent.resolve()
BUILD_DIR = SCRIPT_DIR / "build"
MAIN_C = SCRIPT_DIR / "main" / "main.c"

def get_local_ip():
    """Get local IP address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "localhost"

def find_esp_idf():
    """Find ESP-IDF installation."""
    # Common ESP-IDF locations on Windows
    possible_paths = [
        Path(os.environ.get("IDF_PATH", "")),
        Path.home() / "esp" / "esp-idf",
        Path("C:/Espressif/frameworks/esp-idf-v5.2"),
        Path("C:/Espressif/frameworks/esp-idf-v5.2.2"),
        Path("C:/Espressif/frameworks/esp-idf-v5.3"),
        Path("C:/esp/esp-idf"),
    ]

    # Also check Espressif directory for any esp-idf version
    espressif_fw = Path("C:/Espressif/frameworks")
    if espressif_fw.exists():
        for p in espressif_fw.iterdir():
            if p.name.startswith("esp-idf"):
                possible_paths.append(p)

    for path in possible_paths:
        if path and path.exists() and (path / "export.bat").exists():
            return path

    return None

def get_firmware_version():
    """Extract firmware version from main.c."""
    try:
        content = MAIN_C.read_text()
        match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        if match:
            return match.group(1)
    except:
        pass
    return "unknown"

def build_firmware(idf_path):
    """Build firmware using ESP-IDF."""
    print("\n" + "="*60)
    print("Building firmware...")
    print("="*60 + "\n")

    # Create batch script to run in ESP-IDF environment
    build_script = SCRIPT_DIR / "_build_temp.bat"
    build_script.write_text(f'''@echo off
call "{idf_path}\\export.bat" >nul 2>&1
cd /d "{SCRIPT_DIR}"
idf.py build
''')

    try:
        result = subprocess.run(
            ["cmd", "/c", str(build_script)],
            cwd=str(SCRIPT_DIR),
            capture_output=False
        )
        return result.returncode == 0
    finally:
        build_script.unlink(missing_ok=True)

def create_version_json(local_ip, version):
    """Create version.json file."""
    firmware_path = BUILD_DIR / "zobo_esp32.bin"

    if not firmware_path.exists():
        return False

    info = firmware_path.stat()
    version_data = {
        "version": version,
        "size": info.st_size,
        "date": datetime.fromtimestamp(info.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
        "url": f"http://{local_ip}:{PORT}/zobo_esp32.bin"
    }

    version_json = BUILD_DIR / "version.json"
    version_json.write_text(json.dumps(version_data, indent=2))
    return True

def start_server(local_ip):
    """Start HTTP server."""
    os.chdir(BUILD_DIR)

    handler = SimpleHTTPRequestHandler
    httpd = HTTPServer(("0.0.0.0", PORT), handler)

    print(f"\nServing at http://{local_ip}:{PORT}/")
    print(f"Firmware URL: http://{local_ip}:{PORT}/zobo_esp32.bin")
    print(f"Version JSON: http://{local_ip}:{PORT}/version.json")
    print("\nPress Ctrl+C to stop\n")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped.")
        httpd.shutdown()

def main():
    print("\n" + "="*60)
    print("  Zobo ESP32 OTA Server")
    print("="*60)

    # Check arguments
    skip_build = "--skip-build" in sys.argv or "-s" in sys.argv

    # Find ESP-IDF
    idf_path = find_esp_idf()
    if not idf_path and not skip_build:
        print("\nERROR: ESP-IDF not found!")
        print("Install ESP-IDF or run with --skip-build if firmware already exists.")
        print("\nSearched locations:")
        print("  - C:/Espressif/frameworks/esp-idf-*")
        print("  - ~/esp/esp-idf")
        print("  - IDF_PATH environment variable")
        sys.exit(1)

    if idf_path:
        print(f"\nESP-IDF found: {idf_path}")

    # Build firmware
    if not skip_build:
        if not build_firmware(idf_path):
            print("\nERROR: Build failed!")
            sys.exit(1)
        print("\nBuild successful!")

    # Check firmware exists
    firmware_path = BUILD_DIR / "zobo_esp32.bin"
    if not firmware_path.exists():
        print(f"\nERROR: Firmware not found: {firmware_path}")
        sys.exit(1)

    # Get info
    local_ip = get_local_ip()
    version = get_firmware_version()
    fw_size = firmware_path.stat().st_size / 1024

    print("\n" + "="*60)
    print("  Firmware Info")
    print("="*60)
    print(f"  Version:  {version}")
    print(f"  Size:     {fw_size:.1f} KB")
    print(f"  Path:     {firmware_path}")

    # Create version.json
    create_version_json(local_ip, version)

    print("\n" + "="*60)
    print("  Starting OTA Server")
    print("="*60)

    start_server(local_ip)

if __name__ == "__main__":
    main()
