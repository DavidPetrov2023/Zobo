#!/usr/bin/env python3
"""
Build and Install script for Zobo Flutter app
Automatically handles dependencies, builds APK and installs to phone.

Usage:
  python build_install.py           # Build and install
  python build_install.py --no-install  # Build only
  python build_install.py --clean   # Clean build first
"""

import os
import sys
import subprocess
import hashlib
from pathlib import Path

# Configuration
SCRIPT_DIR = Path(__file__).parent.resolve()
PUBSPEC_YAML = SCRIPT_DIR / "pubspec.yaml"
PUBSPEC_LOCK = SCRIPT_DIR / "pubspec.lock"
DEPS_HASH_FILE = SCRIPT_DIR / ".deps_hash"
APK_PATH = SCRIPT_DIR / "build/app/outputs/flutter-apk/app-release.apk"


def get_pubspec_hash():
    """Calculate hash of pubspec.yaml to detect changes."""
    if PUBSPEC_YAML.exists():
        content = PUBSPEC_YAML.read_bytes()
        return hashlib.md5(content).hexdigest()
    return None


def needs_pub_get():
    """Check if flutter pub get is needed."""
    # No lock file = definitely need pub get
    if not PUBSPEC_LOCK.exists():
        return True, "pubspec.lock neexistuje"

    # Check if pubspec.yaml changed since last pub get
    current_hash = get_pubspec_hash()
    if DEPS_HASH_FILE.exists():
        saved_hash = DEPS_HASH_FILE.read_text().strip()
        if current_hash != saved_hash:
            return True, "pubspec.yaml se změnil"
    else:
        # First run with this script
        return True, "první spuštění"

    return False, None


def save_pubspec_hash():
    """Save current pubspec.yaml hash."""
    current_hash = get_pubspec_hash()
    if current_hash:
        DEPS_HASH_FILE.write_text(current_hash)


def run_command(cmd, description):
    """Run a command and show output."""
    print(f"\n{'='*60}")
    print(f"  {description}")
    print('='*60 + "\n")

    result = subprocess.run(
        cmd,
        cwd=str(SCRIPT_DIR),
        shell=True
    )
    return result.returncode == 0


def check_flutter():
    """Check if Flutter is available."""
    result = subprocess.run(
        "flutter --version",
        cwd=str(SCRIPT_DIR),
        shell=True,
        capture_output=True
    )
    return result.returncode == 0


def check_adb():
    """Check if ADB is available and device connected."""
    result = subprocess.run(
        "adb devices",
        shell=True,
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        return False, "ADB není dostupný"

    lines = result.stdout.strip().split('\n')
    devices = [l for l in lines[1:] if l.strip() and 'device' in l]

    if not devices:
        return False, "Žádné zařízení není připojeno"

    return True, devices[0].split()[0]


def clean_build():
    """Clean Flutter build."""
    print("Cleaning build...")
    subprocess.run("flutter clean", cwd=str(SCRIPT_DIR), shell=True)


def print_usage():
    print("""
Usage: python build_install.py [options]

Options:
  (no options)      Build and install to phone
  --no-install, -n  Build only, skip installation
  --clean, -c       Clean build directory first
  --force-deps, -f  Force flutter pub get
  --debug, -d       Enable debug mode (show log in app)
  --help, -h        Show this help

Examples:
  python build_install.py              # Build + install (bez logu)
  python build_install.py -d           # Build + install s logem
  python build_install.py -n           # Build only
  python build_install.py -c           # Clean + build + install
""")


def main():
    print("\n" + "="*60)
    print("  Zobo Flutter Build & Install")
    print("="*60)

    # Parse arguments
    args = sys.argv[1:]

    if "--help" in args or "-h" in args:
        print_usage()
        sys.exit(0)

    do_clean = "--clean" in args or "-c" in args
    no_install = "--no-install" in args or "-n" in args
    force_deps = "--force-deps" in args or "-f" in args
    debug_mode = "--debug" in args or "-d" in args

    # Check Flutter
    if not check_flutter():
        print("\nERROR: Flutter není dostupný!")
        print("Nainstaluj Flutter SDK a přidej do PATH")
        sys.exit(1)

    # Check ADB if we need to install
    if not no_install:
        adb_ok, adb_info = check_adb()
        if not adb_ok:
            print(f"\nERROR: {adb_info}")
            print("Připoj telefon přes USB a povol USB debugging")
            sys.exit(1)
        print(f"\n  Zařízení: {adb_info}")

    # Clean if requested
    if do_clean:
        clean_build()

    # Check dependencies
    need_deps, reason = needs_pub_get()
    if force_deps:
        need_deps = True
        reason = "vynuceno parametrem -f"

    if need_deps:
        print(f"\n  Závislosti: nutná aktualizace ({reason})")
        if not run_command("flutter pub get", "Stahování závislostí..."):
            print("\nERROR: flutter pub get selhalo!")
            sys.exit(1)
        save_pubspec_hash()
    else:
        print("\n  Závislosti: OK (bez změn)")

    # Build APK
    build_cmd = "flutter build apk --release"
    if debug_mode:
        build_cmd += " --dart-define=DEBUG_MODE=true"
        print("\n  Mode: DEBUG (s logem)")
    else:
        print("\n  Mode: RELEASE (bez logu)")

    if not run_command(build_cmd, "Building APK..."):
        print("\nERROR: Build selhal!")
        sys.exit(1)

    # Check APK exists
    if not APK_PATH.exists():
        print(f"\nERROR: APK nebylo vytvořeno: {APK_PATH}")
        sys.exit(1)

    apk_size = APK_PATH.stat().st_size / (1024 * 1024)
    print(f"\n  APK: {apk_size:.1f} MB")

    # Install to phone
    if not no_install:
        if not run_command(f"adb install -r \"{APK_PATH}\"", "Instalace do telefonu..."):
            print("\nERROR: Instalace selhala!")
            sys.exit(1)

        # Launch the app
        run_command(
            "adb shell am start -n cz.davidpetrov.zobo_flutter/.MainActivity",
            "Spouštění aplikace..."
        )

    # Summary
    print("\n" + "="*60)
    print("  Hotovo!")
    print("="*60)
    print(f"  APK:  {APK_PATH}")
    print(f"  Size: {apk_size:.1f} MB")
    if not no_install:
        print("  Status: Nainstalováno v telefonu")
    print()


if __name__ == "__main__":
    main()
