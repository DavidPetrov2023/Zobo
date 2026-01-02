# Zobo - ESP32 BLE Robot Controller

ESP32 robot ovládaný přes Bluetooth Low Energy s Flutter aplikací pro Android.

## Struktura

```
Zobo/
├── zobo_flutter/    # Flutter mobilní aplikace
├── zobo_esp32/      # ESP-IDF firmware
└── zobo_eagle/      # Návrh PCB
```

## Funkce

- BLE ovládání (Nordic UART Service)
- Duální PWM řízení motorů s plynulou akcelerací
- OTA aktualizace firmware přes WiFi (lokální server nebo GitHub Releases)
- WiFi konfigurace přes BLE
- RGB LED indikace

## Quick Start

### Flutter aplikace
```bash
cd zobo_flutter
python build_install.py        # Build + instalace do telefonu
python build_install.py -d     # Build s debug logem
```

### ESP32 firmware
```bash
cd zobo_esp32
python build_flash.py          # Build + flash
python build_flash.py -n       # Jen build
```

### OTA aktualizace
```bash
# GitHub release
python release.py
```

## Hardware

| Funkce | GPIO |
|--------|------|
| Motor L PWM/DIR | 16, 17 |
| Motor R PWM/DIR | 25, 26 |
| RGB LED (R/G/B) | 27, 14, 12 |

## Požadavky

- ESP-IDF v5.0+
- Flutter SDK 3.0+
- Python 3.8+

## Autor

David Petrov
