#!/usr/bin/env python3
"""
Release Script for Zobo ESP32 Firmware - Petrov server upload.

Builds firmware and uploads it to the OTA server at petrovelektronika.cz.
No GitHub release is created. For the old GitHub-based flow, use release.py.

Requirements:
- ESP-IDF environment available (auto-detected via VS Code settings or env var).
- ADMIN_ACCESS_KEY for the upload endpoint (from env, .env, or --key arg).

Usage:
    python release_server.py                    # build + upload to default server
    python release_server.py --skip-build       # upload existing build/zobo_esp32.bin
    python release_server.py --notes "Fix X"    # add changelog notes
    python release_server.py --server URL       # override upload endpoint
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib import request as urlrequest

SCRIPT_DIR = Path(__file__).parent
OTA_MANAGER_H = SCRIPT_DIR / "main" / "ota_manager.h"
BUILD_DIR = SCRIPT_DIR / "build"
FIRMWARE_BIN = BUILD_DIR / "zobo_esp32.bin"

DEFAULT_SERVER = "https://petrovelektronika.cz/robot/api.php"


def load_dotenv(path):
    """Load KEY=VALUE pairs from a .env file into os.environ."""
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def get_firmware_version():
    match = re.search(
        r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"',
        OTA_MANAGER_H.read_text(),
    )
    return match.group(1) if match else None


def find_esp_idf():
    idf_path = os.environ.get("IDF_PATH")
    if idf_path and Path(idf_path).exists():
        return Path(idf_path)

    vscode_settings = Path.home() / "AppData/Roaming/Code/User/settings.json"
    if vscode_settings.exists():
        try:
            settings = json.loads(vscode_settings.read_text())
            if "idf.espIdfPathWin" in settings:
                p = Path(settings["idf.espIdfPathWin"])
                if p.exists():
                    return p
        except Exception:
            pass

    for p in [
        Path.home() / "esp" / "esp-idf",
        Path("C:/Espressif/frameworks/esp-idf-v5.2"),
        Path("C:/esp/esp-idf"),
    ]:
        if p.exists():
            return p
    return None


def build_firmware():
    print("\n[1/2] Building firmware...")
    idf_path = find_esp_idf()
    if not idf_path:
        print("  ERROR: ESP-IDF not found. Set IDF_PATH or install the VS Code extension.")
        return False

    export_bat = idf_path / "export.bat"
    if not export_bat.exists():
        print(f"  ERROR: {export_bat} not found.")
        return False

    print(f"  Using ESP-IDF: {idf_path}")
    result = subprocess.run(
        f'call "{export_bat}" && idf.py build',
        cwd=SCRIPT_DIR,
        shell=True,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  Build failed:\n{result.stderr[-800:]}")
        return False

    if not FIRMWARE_BIN.exists():
        print(f"  ERROR: Firmware binary not found at {FIRMWARE_BIN}")
        return False

    print(f"  Built: {FIRMWARE_BIN} ({FIRMWARE_BIN.stat().st_size / 1024:.1f} KB)")
    return True


def build_multipart_body(version, notes, bin_bytes):
    """Build a multipart/form-data body without external dependencies."""
    boundary = "----zobo-release-boundary-" + os.urandom(8).hex()
    crlf = b"\r\n"
    parts = []

    def add_field(name, value):
        parts.append(f"--{boundary}".encode())
        parts.append(f'Content-Disposition: form-data; name="{name}"'.encode())
        parts.append(b"")
        parts.append(value.encode("utf-8"))

    add_field("version", version)
    add_field("notes", notes or "")

    parts.append(f"--{boundary}".encode())
    parts.append(
        b'Content-Disposition: form-data; name="firmware"; filename="zobo_esp32.bin"'
    )
    parts.append(b"Content-Type: application/octet-stream")
    parts.append(b"")
    parts.append(bin_bytes)
    parts.append(f"--{boundary}--".encode())
    parts.append(b"")

    body = crlf.join(parts)
    return body, f"multipart/form-data; boundary={boundary}"


def upload_firmware(server, admin_key, version, notes):
    print("\n[2/2] Uploading to OTA server...")
    print(f"  Server: {server}")
    print(f"  Version: {version}")

    bin_bytes = FIRMWARE_BIN.read_bytes()
    body, content_type = build_multipart_body(version, notes, bin_bytes)

    url = f"{server}?action=upload&key={admin_key}"
    req = urlrequest.Request(url, data=body, method="POST")
    req.add_header("Content-Type", content_type)
    req.add_header("Content-Length", str(len(body)))

    try:
        with urlrequest.urlopen(req, timeout=60) as resp:
            payload = json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        # Try to extract server error message if present.
        msg = str(e)
        if hasattr(e, "read"):
            try:
                msg = e.read().decode("utf-8")
            except Exception:
                pass
        print(f"  ERROR: Upload failed: {msg}")
        return False

    if payload.get("status") != "ok":
        print(f"  ERROR: Server rejected upload: {payload}")
        return False

    info = payload.get("version", {})
    print("  Upload OK!")
    print(f"    Version : {info.get('version')}")
    print(f"    Size    : {info.get('size')} bytes")
    print(f"    SHA256  : {info.get('sha256', '')[:32]}...")
    print(f"    Download: {info.get('url')}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Build + upload Zobo firmware to OTA server")
    parser.add_argument("--skip-build", action="store_true", help="Skip build, upload existing binary")
    parser.add_argument("--notes", default="", help="Release notes / changelog")
    parser.add_argument("--server", default=None, help="Override upload endpoint URL")
    parser.add_argument("--key", default=None, help="Override ADMIN_ACCESS_KEY")
    args = parser.parse_args()

    load_dotenv(SCRIPT_DIR / ".env")

    server = args.server or os.environ.get("OTA_SERVER_URL") or DEFAULT_SERVER
    admin_key = args.key or os.environ.get("ADMIN_ACCESS_KEY")

    if not admin_key:
        print("ERROR: ADMIN_ACCESS_KEY not set.")
        print("  Set it via --key, env var, or zobo_esp32/.env file.")
        return 1

    version = get_firmware_version()
    if not version:
        print("ERROR: Could not read FIRMWARE_VERSION from ota_manager.h")
        return 1

    print("=" * 60)
    print(f"  Zobo OTA Release - v{version}")
    print("=" * 60)

    if not args.skip_build:
        if not build_firmware():
            return 1
    else:
        if not FIRMWARE_BIN.exists():
            print(f"ERROR: --skip-build but {FIRMWARE_BIN} does not exist")
            return 1
        print(f"\n[1/2] Using existing: {FIRMWARE_BIN}")

    if not upload_firmware(server, admin_key, version, args.notes):
        return 1

    print("\n" + "=" * 60)
    print("  Released successfully.")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    sys.exit(main())
