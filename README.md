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
- OTA aktualizace firmware přes WiFi (GitHub Releases)
- WiFi konfigurace přes BLE
- RGB LED indikace stavu
- Deep sleep s úsporným režimem (~10µA)
- Automatické probouzení a blikání LED každých 10s

## Quick Start

### Flutter aplikace
```bash
cd zobo_flutter
python build_install.py        # Build + instalace + spuštění
python build_install.py -d     # Build s debug logem
python build_install.py -c     # Clean build
```

### ESP32 firmware
```bash
cd zobo_esp32
python build_flash.py          # Build + flash
python build_flash.py -n       # Jen build
python release.py              # Vytvořit GitHub release pro OTA
```

## Hardware

| Funkce | GPIO |
|--------|------|
| Motor L PWM/DIR | 16, 17 |
| Motor R PWM/DIR | 25, 26 |
| RGB LED (R/G/B) | 27, 14, 12 |
| Main LED | 5 |

## Chování

### Sleep mode
- Po 15s nečinnosti ESP32 přejde do deep sleep
- Každých 10s se probudí, krátce blikne modře a zase usne
- Při připojení aplikace posílá ping každých 5s → ESP32 zůstává vzhůru
- Po odpojení aplikace → 15s timeout → deep sleep

### LED indikace
- **Startup**: Sekvence barev (červená → modrá → zelená → bílá)
- **Sleep blink**: Krátké modré bliknutí každých 10s
- **Připojeno**: Bílá (všechny barvy)

## Požadavky

- ESP-IDF v5.0+
- Flutter SDK 3.0+
- Python 3.8+
- Android telefon s BLE

## Autor

David Petrov
