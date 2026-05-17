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
- OTA aktualizace firmware přes vlastní server (`petrovelektronika.cz/robot/`)
- Periodická telemetrie do `/devices/` dashboardu (každých 30s)
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
python build_flash.py          # Build + flash přes USB
python build_flash.py -n       # Jen build (bez flashe)
```

## OTA Release (vlastní server)

Firmware se distribuuje přes `petrovelektronika.cz/robot/`, ne přes GitHub.
Telefonní app stáhne version.json, robot pak stáhne binárku přes URL s tokenem.

### Setup (jednorázově)

1. **Na serveru** v `/var/www/html/.env`:
   ```
   ADMIN_ACCESS_KEY=<sdílený klíč pro admin endpointy>
   OTA_DOWNLOAD_TOKEN=<token pro firmware download>
   ```
2. **Lokálně** vytvoř `zobo_esp32/.env`:
   ```
   ADMIN_ACCESS_KEY=<stejný jako na serveru>
   ```
3. **Ve Flutter** v `lib/pages/settings_page.dart` nastav `otaToken` na hodnotu `OTA_DOWNLOAD_TOKEN`.
4. **Nginx**: `client_max_body_size 16M;` v `/etc/nginx/nginx.conf` (default 1MB stačí na 1.3MB firmware).

### Release flow

```bash
# 1. Bumpni verzi v zobo_esp32/main/ota_manager.h (FIRMWARE_VERSION)
# 2. Build + upload na server v jednom kroku:
cd zobo_esp32
python release_server.py --notes "Changelog popis"

# 3. V telefonní appce: Settings → Check for Updates → Update
#    (ESP stáhne novou binárku ze serveru přes WiFi)
```

Skripty:
- `release_server.py` — build + POST na `/robot/api.php` (aktuální flow)
- `release.py` — starý GitHub Releases flow (ponechán jako fallback, nepoužívat)

### Admin URL

- `https://petrovelektronika.cz/robot/?key=<ADMIN_KEY>` — release management + endpoint URL
- `https://petrovelektronika.cz/devices/?key=<ADMIN_KEY>` — live telemetrie všech zařízení

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
