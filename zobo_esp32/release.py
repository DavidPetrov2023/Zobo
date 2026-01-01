#!/usr/bin/env python3
"""
GitHub Release Script for Zobo ESP32 Firmware

This script:
1. Builds the firmware
2. Creates version.json
3. Creates a GitHub release with the firmware binary

Requirements:
- GitHub CLI (gh) must be installed and authenticated
- ESP-IDF environment must be available

Usage:
    python release.py              # Create release with current version
    python release.py --draft      # Create draft release
    python release.py --prerelease # Mark as pre-release
"""

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# Paths
SCRIPT_DIR = Path(__file__).parent
OTA_MANAGER_H = SCRIPT_DIR / "main" / "ota_manager.h"
BUILD_DIR = SCRIPT_DIR / "build"
FIRMWARE_BIN = BUILD_DIR / "zobo_esp32.bin"
VERSION_JSON = BUILD_DIR / "version.json"


def get_firmware_version():
    """Extract firmware version from ota_manager.h."""
    try:
        content = OTA_MANAGER_H.read_text()
        match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        if match:
            return match.group(1)
    except Exception as e:
        print(f"Error reading version: {e}")
    return None


def find_esp_idf():
    """Find ESP-IDF installation."""
    # Check environment variable
    idf_path = os.environ.get("IDF_PATH")
    if idf_path and Path(idf_path).exists():
        return Path(idf_path)

    # Check VS Code ESP-IDF extension settings
    vscode_settings = Path.home() / "AppData/Roaming/Code/User/settings.json"
    if vscode_settings.exists():
        try:
            settings = json.loads(vscode_settings.read_text())
            if "idf.espIdfPathWin" in settings:
                idf_path = Path(settings["idf.espIdfPathWin"])
                if idf_path.exists():
                    return idf_path
        except Exception:
            pass

    # Common paths
    common_paths = [
        Path.home() / "esp" / "esp-idf",
        Path("C:/Espressif/frameworks/esp-idf-v5.2"),
        Path("C:/esp/esp-idf"),
    ]
    for path in common_paths:
        if path.exists():
            return path

    return None


def run_command(cmd, cwd=None, shell=False):
    """Run a command and return success status."""
    print(f"  Running: {cmd if isinstance(cmd, str) else ' '.join(cmd)}")
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            shell=shell,
            capture_output=True,
            text=True
        )
        if result.returncode != 0:
            print(f"  Error: {result.stderr}")
            return False
        return True
    except Exception as e:
        print(f"  Exception: {e}")
        return False


def build_firmware():
    """Build the firmware using ESP-IDF."""
    print("\n[1/4] Building firmware...")

    idf_path = find_esp_idf()
    if not idf_path:
        print("  ERROR: ESP-IDF not found!")
        print("  Set IDF_PATH environment variable or install ESP-IDF extension in VS Code")
        return False

    print(f"  Using ESP-IDF: {idf_path}")

    # Create batch file to run build
    export_bat = idf_path / "export.bat"
    if not export_bat.exists():
        print(f"  ERROR: {export_bat} not found!")
        return False

    # Build command
    build_cmd = f'call "{export_bat}" && idf.py build'
    result = subprocess.run(
        build_cmd,
        cwd=SCRIPT_DIR,
        shell=True,
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"  Build failed: {result.stderr[-500:]}")
        return False

    if not FIRMWARE_BIN.exists():
        print(f"  ERROR: Firmware binary not found at {FIRMWARE_BIN}")
        return False

    print(f"  Firmware built: {FIRMWARE_BIN}")
    print(f"  Size: {FIRMWARE_BIN.stat().st_size / 1024:.1f} KB")
    return True


def create_version_json(version):
    """Create version.json file."""
    print("\n[2/4] Creating version.json...")

    version_data = {
        "version": version,
        "size": FIRMWARE_BIN.stat().st_size,
        "date": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "url": f"https://github.com/DavidPetrov2023/Zobo/releases/download/v{version}/zobo_esp32.bin"
    }

    VERSION_JSON.write_text(json.dumps(version_data, indent=2))
    print(f"  Created: {VERSION_JSON}")
    print(f"  Version: {version}")
    return True


def check_gh_cli():
    """Check if GitHub CLI is installed and authenticated."""
    print("\n[3/4] Checking GitHub CLI...")

    # Check if gh is installed
    try:
        result = subprocess.run(["gh", "--version"], capture_output=True, text=True)
        if result.returncode != 0:
            print("  ERROR: GitHub CLI (gh) not found!")
            print("  Install with: winget install GitHub.cli")
            return False
    except FileNotFoundError:
        print("  ERROR: GitHub CLI (gh) not installed!")
        print("  Install with: winget install GitHub.cli")
        return False

    # Check if authenticated
    result = subprocess.run(["gh", "auth", "status"], capture_output=True, text=True)
    if result.returncode != 0:
        print("  ERROR: Not authenticated with GitHub!")
        print("  Run: gh auth login")
        return False

    print("  GitHub CLI ready")
    return True


def create_release(version, draft=False, prerelease=False):
    """Create GitHub release with firmware files."""
    print("\n[4/4] Creating GitHub release...")

    tag = f"v{version}"
    title = f"Firmware {version}"
    notes = f"""## Zobo ESP32 Firmware {version}

### Changes
- See commit history for details

### Files
- `zobo_esp32.bin` - Firmware binary for OTA update
- `version.json` - Version metadata

### OTA Update
The app will automatically detect this update when connected to the robot.
"""

    # Build gh release create command
    cmd = [
        "gh", "release", "create", tag,
        "--title", title,
        "--notes", notes,
        str(FIRMWARE_BIN),
        str(VERSION_JSON)
    ]

    if draft:
        cmd.append("--draft")
    if prerelease:
        cmd.append("--prerelease")

    print(f"  Creating release: {tag}")
    result = subprocess.run(cmd, cwd=SCRIPT_DIR, capture_output=True, text=True)

    if result.returncode != 0:
        if "already exists" in result.stderr:
            print(f"  ERROR: Release {tag} already exists!")
            print("  Either delete it or bump the version in ota_manager.h")
        else:
            print(f"  ERROR: {result.stderr}")
        return False

    print(f"  Release created: {result.stdout.strip()}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Create GitHub release for Zobo firmware")
    parser.add_argument("--draft", action="store_true", help="Create as draft release")
    parser.add_argument("--prerelease", action="store_true", help="Mark as pre-release")
    parser.add_argument("--skip-build", action="store_true", help="Skip firmware build")
    args = parser.parse_args()

    print("=" * 60)
    print("  Zobo Firmware Release Script")
    print("=" * 60)

    # Get version
    version = get_firmware_version()
    if not version:
        print("ERROR: Could not determine firmware version!")
        return 1

    print(f"\n  Version: {version}")

    # Build firmware
    if not args.skip_build:
        if not build_firmware():
            return 1
    else:
        print("\n[1/4] Skipping build...")
        if not FIRMWARE_BIN.exists():
            print(f"  ERROR: Firmware binary not found at {FIRMWARE_BIN}")
            return 1

    # Create version.json
    if not create_version_json(version):
        return 1

    # Check GitHub CLI
    if not check_gh_cli():
        return 1

    # Create release
    if not create_release(version, args.draft, args.prerelease):
        return 1

    print("\n" + "=" * 60)
    print("  Release created successfully!")
    print("=" * 60)
    print(f"\n  Download URL:")
    print(f"  https://github.com/DavidPetrov2023/Zobo/releases/tag/v{version}")
    print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
