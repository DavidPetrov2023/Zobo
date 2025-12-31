# Zobo - ESP32 BLE Robot Controller

Zobo je robot ovládaný přes ESP32 s Bluetooth Low Energy (BLE) komunikací. Projekt obsahuje firmware pro ESP32 a mobilní aplikaci pro Android.

## Struktura projektu

```
Zobo/
├── zobo_flutter/       # Flutter mobilní aplikace (Android/iOS)
├── zobo_esp32/         # Nativní ESP-IDF firmware
└── zobo_eagle/         # Návrh PCB
```

## Funkce

- **BLE UART komunikace** - Nordic UART Service pro bezdrátové ovládání
- **Řízení motorů** - Duální PWM řízení motorů se směrem
- **Plynulá akcelerace** - Rampa vpřed z PWM 100 na 255 během 2 sekund
- **Bezpečnostní timeout** - Automatické zastavení po 300ms nečinnosti
- **RGB LED ovládání** - Stavová indikace pomocí RGB LED
- **Multiplatformní aplikace** - Flutter aplikace pro Android (připraveno i pro iOS)

## Hardware

### Konfigurace pinů (ESP32)

| Funkce | GPIO |
|--------|------|
| Motor levý PWM | 16 |
| Motor levý směr | 17 |
| Motor pravý PWM | 25 |
| Motor pravý směr | 26 |
| LED červená | 27 |
| LED zelená | 14 |
| LED modrá | 12 |
| LED hlavní | 5 |

### BLE Service UUIDs (Nordic UART)

| Charakteristika | UUID |
|-----------------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (zápis) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (notifikace) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

## Příkazy robota

| Příkaz | Hodnota | Popis |
|--------|---------|-------|
| Vzad | `0x00` | Jízda vzad s PWM 100 |
| Vpřed | `0x01` | Jízda vpřed s rampou akcelerace |
| Stop | `0x02` | Zastavení motorů |
| Vpravo | `0x03` | Otočení vpravo |
| Vlevo | `0x04` | Otočení vlevo |
| Manuál | `0x05` | Manuální PWM řízení (param: 0-100) |
| LED zelená | `0x0A` (10) | Nastavení LED na zelenou |
| LED červená | `0x14` (20) | Nastavení LED na červenou |
| LED modrá | `0x1E` (30) | Nastavení LED na modrou |
| LED vše | `0x28` (40) | Zapnutí všech LED |

## Sestavení

### Flutter aplikace (zobo_flutter)

```bash
cd zobo_flutter
flutter pub get
flutter build apk --release
```

### ESP-IDF firmware (zobo_esp32)

```bash
cd zobo_esp32
idf.py build
idf.py -p COM9 flash monitor
```

## Požadavky

### Mobilní aplikace
- Flutter SDK 3.0+
- Android SDK 34
- Dart 3.0+

### Firmware (ESP-IDF)
- ESP-IDF v5.0+

## Funkce aplikace

Flutter aplikace poskytuje:
- Skenování a připojení BLE zařízení
- D-Pad ovládání pro pohyb robota
- Tlačítka pro ovládání RGB LED
- Zobrazení stavu připojení
- Prohlížeč logu příkazů

## Licence

Tento projekt je proprietární. Všechna práva vyhrazena.

## Autor

David Petrov
