# Zobo - ESP32 BLE Robot Controller

Zobo is an ESP32-based robot controller with Bluetooth Low Energy (BLE) communication. The project includes firmware for ESP32 and a mobile app for Android.

## Project Structure

```
Zobo/
├── zobo_flutter/       # Flutter mobile app (Android/iOS)
├── zobo_platformio/    # PlatformIO Arduino firmware
├── zobo_esp32/         # Native ESP-IDF firmware
└── zobo_eagle/         # PCB design files
```

## Features

- **BLE UART Communication** - Nordic UART Service for wireless control
- **Motor Control** - Dual motor PWM control with direction
- **Smooth Acceleration** - Forward ramp from 100 to 255 PWM over 2 seconds
- **Safety Timeout** - Auto-stop after 300ms of inactivity
- **RGB LED Control** - Status indication via RGB LED
- **Cross-Platform App** - Flutter app for Android (iOS ready)

## Hardware

### Pin Configuration (ESP32)

| Function | GPIO |
|----------|------|
| Motor Left PWM | 16 |
| Motor Left DIR | 17 |
| Motor Right PWM | 25 |
| Motor Right DIR | 26 |
| LED Red | 27 |
| LED Green | 14 |
| LED Blue | 12 |
| LED Main | 5 |

### BLE Service UUIDs (Nordic UART)

| Characteristic | UUID |
|----------------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (Write) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (Notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

## Robot Commands

| Command | Value | Description |
|---------|-------|-------------|
| Backward | `0x00` | Move backward at PWM 100 |
| Forward | `0x01` | Move forward with ramp acceleration |
| Stop | `0x02` | Stop all motors |
| Right | `0x03` | Turn right |
| Left | `0x04` | Turn left |
| Manual | `0x05` | Manual PWM control (param: 0-100) |
| LED Green | `0x0A` (10) | Set LED to green |
| LED Red | `0x14` (20) | Set LED to red |
| LED Blue | `0x1E` (30) | Set LED to blue |
| LED All | `0x28` (40) | Turn on all LEDs |

## Building

### Flutter App (zobo_flutter)

```bash
cd zobo_flutter
flutter pub get
flutter build apk --release
```

### PlatformIO Firmware (zobo_platformio)

```bash
cd zobo_platformio
pio run
pio run -t upload -p COM9
```

### ESP-IDF Firmware (zobo_esp32)

```bash
cd zobo_esp32
idf.py build
idf.py -p COM9 flash monitor
```

## Requirements

### Mobile App
- Flutter SDK 3.0+
- Android SDK 34
- Dart 3.0+

### Firmware (PlatformIO)
- PlatformIO Core
- ESP32 Arduino Framework

### Firmware (ESP-IDF)
- ESP-IDF v5.0+

## App Screenshots

The Flutter app provides:
- BLE device scanning and connection
- D-Pad controls for robot movement
- RGB LED control buttons
- Connection status display
- Command log viewer

## License

This project is proprietary. All rights reserved.

## Author

David Petrov
