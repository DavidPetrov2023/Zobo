#!/usr/bin/env python3
"""
Build and Flash script for Zobo ESP32
Automatically builds firmware and flashes to device.
Run from any terminal - no ESP-IDF environment needed.

Usage:
  python build_flash.py           # Build and flash
  python build_flash.py --no-flash  # Build only (no flash)
  python build_flash.py --clean   # Clean build first
"""

import os
import sys
import json
import subprocess
import re
import shutil
from pathlib import Path

# Configuration
COM_PORT = "COM9"  # Change this to your ESP32 COM port
SCRIPT_DIR = Path(__file__).parent.resolve()
BUILD_DIR = SCRIPT_DIR / "build"
OTA_MANAGER_H = SCRIPT_DIR / "main" / "ota_manager.h"

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
    """Extract firmware version from ota_manager.h."""
    try:
        content = OTA_MANAGER_H.read_text()
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
call "{idf_path}\export.bat" >nul 2>&1
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

def print_usage():
    print("""
Usage: python build_flash.py [options]

Options:
  (no options)    Build and flash
  --no-flash, -n  Build only, skip flashing
  --clean, -c     Clean build directory first
  --port PORT     COM port for flashing (default: COM9)
  --help, -h      Show this help

Examples:
  python build_flash.py              # Build + flash
  python build_flash.py -n           # Build only
  python build_flash.py -c           # Clean + build + flash
  python build_flash.py --port COM5  # Use different COM port
""")

def main():
    print("\n" + "="*60)
    print("  Zobo ESP32 Build & Flash")
    print("="*60)

    # Parse arguments
    args = sys.argv[1:]

    if "--help" in args or "-h" in args:
        print_usage()
        sys.exit(0)

    do_clean = "--clean" in args or "-c" in args
    no_flash = "--no-flash" in args or "-n" in args

    # Get COM port
    port = COM_PORT
    if "--port" in args:
        idx = args.index("--port")
        if idx + 1 < len(args):
            port = args[idx + 1]

    # Find ESP-IDF
    idf_path = find_esp_idf()
    if not idf_path:
        print("\nERROR: ESP-IDF not found!")
        print("\nSearched locations:")
        print("  - VS Code ESP-IDF extension settings")
        print("  - ~/esp/v*/esp-idf")
        print("  - IDF_PATH environment variable")
        sys.exit(1)

    version = get_firmware_version()
    print(f"\n  Version:  {version}")
    print(f"  ESP-IDF:  {idf_path}")

    # Clean if requested
    if do_clean:
        clean_build()

    # Build firmware
    if not build_firmware(idf_path):
        print("\nERROR: Build failed!")
        sys.exit(1)
    print("\nBuild successful!")

    # Flash to device
    if not no_flash:
        if not flash_firmware(idf_path, port):
            print(f"\nERROR: Flash failed on {port}")
            sys.exit(1)
        print("\nFlash successful!")

    # Show result
    firmware_path = BUILD_DIR / "zobo_esp32.bin"
    if firmware_path.exists():
        fw_size = firmware_path.stat().st_size / 1024
        print("\n" + "="*60)
        print("  Done!")
        print("="*60)
        print(f"  Version:  {version}")
        print(f"  Size:     {fw_size:.1f} KB")
        print(f"  Binary:   {firmware_path}")
        print()

if __name__ == "__main__":
    main()
