#!/usr/bin/env python3
"""
OTA Server for Zobo ESP32
Automatically builds firmware, flashes to device, and starts HTTP server for OTA updates.
Run from any terminal - no ESP-IDF environment needed.

Usage:
  python ota_server.py           # Full: clean, build, flash, serve
  python ota_server.py --no-flash  # Build and serve only (no flash)
  python ota_server.py -s        # Skip build, just serve existing firmware
"""

import os
import sys
import json
import socket
import subprocess
import re
import shutil
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler
from datetime import datetime

# Configuration
PORT = 8080
COM_PORT = "COM9"  # Change this to your ESP32 COM port
SCRIPT_DIR = Path(__file__).parent.resolve()
BUILD_DIR = SCRIPT_DIR / "build"
MAIN_C = SCRIPT_DIR / "main" / "main.c"
OTA_MANAGER_H = SCRIPT_DIR / "main" / "ota_manager.h"

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
    possible_paths = [
        Path(os.environ.get("IDF_PATH", "")),
    ]

    # Check VS Code ESP-IDF extension settings
    vscode_settings = Path.home() / "AppData/Roaming/Code/User/settings.json"
    if vscode_settings.exists():
        try:
            settings = json.loads(vscode_settings.read_text())
            if "idf.espIdfPathWin" in settings:
                possible_paths.append(Path(settings["idf.espIdfPathWin"]))
        except:
            pass

    # Common locations
    possible_paths.extend([
        Path.home() / "esp" / "v5.2.6" / "esp-idf",
        Path.home() / "esp" / "esp-idf",
        Path("C:/Espressif/frameworks/esp-idf-v5.2.6"),
        Path("C:/esp/esp-idf"),
    ])

    # Check ~/esp for version folders
    esp_home = Path.home() / "esp"
    if esp_home.exists():
        for p in esp_home.iterdir():
            if p.is_dir() and p.name.startswith("v"):
                possible_paths.append(p / "esp-idf")

    for path in possible_paths:
        if path and path.exists() and (path / "export.bat").exists():
            return path

    return None

def get_firmware_version():
    """Extract firmware version from ota_manager.h or main.c."""
    # Try ota_manager.h first
    for file in [OTA_MANAGER_H, MAIN_C]:
        try:
            content = file.read_text()
            match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
            if match:
                return match.group(1)
        except:
            pass
    return "unknown"

def clean_build():
    """Remove build directory for clean build."""
    if BUILD_DIR.exists():
        print("Cleaning old build...")
        shutil.rmtree(BUILD_DIR)
        print("Clean complete.")

def run_idf_command(idf_path, command):
    """Run idf.py command in ESP-IDF environment."""
    build_script = SCRIPT_DIR / "_temp_cmd.bat"
    build_script.write_text(f'''@echo off
call "{idf_path}\\export.bat" >nul 2>&1
cd /d "{SCRIPT_DIR}"
{command}
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

def build_firmware(idf_path):
    """Build firmware using ESP-IDF."""
    print("\n" + "="*60)
    print("  Building firmware...")
    print("="*60 + "\n")

    return run_idf_command(idf_path, "idf.py build")

def flash_firmware(idf_path, port):
    """Flash firmware to ESP32."""
    print("\n" + "="*60)
    print(f"  Flashing to {port}...")
    print("="*60 + "\n")

    return run_idf_command(idf_path, f"idf.py -p {port} flash")

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

def print_usage():
    print("""
Usage: python ota_server.py [options]

Options:
  (no options)    Full workflow: clean, build, flash, serve
  --no-flash, -n  Build and serve only, skip flashing
  --skip-build, -s  Skip build, just serve existing firmware
  --port PORT     COM port for flashing (default: COM9)
  --help, -h      Show this help

Examples:
  python ota_server.py              # Full: clean + build + flash + serve
  python ota_server.py -n           # Build + serve (no flash)
  python ota_server.py -s           # Just serve existing build
  python ota_server.py --port COM5  # Use different COM port
""")

def main():
    print("\n" + "="*60)
    print("  Zobo ESP32 OTA Server")
    print("="*60)

    # Parse arguments
    args = sys.argv[1:]

    if "--help" in args or "-h" in args:
        print_usage()
        sys.exit(0)

    skip_build = "--skip-build" in args or "-s" in args
    no_flash = "--no-flash" in args or "-n" in args

    # Get COM port
    port = COM_PORT
    if "--port" in args:
        idx = args.index("--port")
        if idx + 1 < len(args):
            port = args[idx + 1]

    # Find ESP-IDF
    idf_path = find_esp_idf()
    if not idf_path and not skip_build:
        print("\nERROR: ESP-IDF not found!")
        print("Install ESP-IDF or run with --skip-build if firmware already exists.")
        print("\nSearched locations:")
        print("  - VS Code ESP-IDF extension settings")
        print("  - ~/esp/v*/esp-idf")
        print("  - IDF_PATH environment variable")
        sys.exit(1)

    if idf_path:
        print(f"\nESP-IDF: {idf_path}")

    # Workflow
    if not skip_build:
        # Clean build
        clean_build()

        # Build firmware
        if not build_firmware(idf_path):
            print("\nERROR: Build failed!")
            sys.exit(1)
        print("\nBuild successful!")

        # Flash to device
        if not no_flash:
            if not flash_firmware(idf_path, port):
                print(f"\nWARNING: Flash failed on {port}")
                print("Continuing with server anyway...")
            else:
                print("\nFlash successful!")

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
