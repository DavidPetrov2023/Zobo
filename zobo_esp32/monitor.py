#!/usr/bin/env python3
"""
Serial Monitor for ESP32
Simple serial monitor for viewing ESP32 output.

Usage:
  python monitor.py           # Use default COM9
  python monitor.py COM5      # Use specific port
  python monitor.py --list    # List available ports
"""

import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Installing pyserial...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
    import serial
    import serial.tools.list_ports

DEFAULT_PORT = "COM9"
BAUD_RATE = 115200

def list_ports():
    """List available COM ports."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No COM ports found.")
        return

    print("\nAvailable COM ports:")
    print("-" * 50)
    for port in ports:
        print(f"  {port.device}: {port.description}")
    print()

def monitor(port):
    """Start serial monitor."""
    print(f"\n{'='*60}")
    print(f"  ESP32 Serial Monitor - {port} @ {BAUD_RATE} baud")
    print(f"{'='*60}")
    print("Press Ctrl+C to exit\n")

    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
        ser.flushInput()

        while True:
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='replace')
                    print(line, end='')
                except:
                    pass
            else:
                time.sleep(0.01)

    except serial.SerialException as e:
        print(f"\nERROR: Cannot open {port}")
        print(f"  {e}")
        print("\nTip: Make sure the port is not used by another program.")
        list_ports()
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n\nMonitor stopped.")
    finally:
        if 'ser' in locals():
            ser.close()

def main():
    args = sys.argv[1:]

    if "--list" in args or "-l" in args:
        list_ports()
        return

    if "--help" in args or "-h" in args:
        print(__doc__)
        return

    # Get port from args or use default
    port = args[0] if args else DEFAULT_PORT

    monitor(port)

if __name__ == "__main__":
    main()
