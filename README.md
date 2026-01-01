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
- **OTA aktualizace** - Vzdálená aktualizace firmware přes WiFi
- **WiFi konfigurace** - Nastavení WiFi přes BLE z mobilní aplikace
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

### WiFi a OTA příkazy

| Příkaz | Hodnota | Popis |
|--------|---------|-------|
| WiFi Set | `0x50` | Nastavení WiFi credentials (SSID\0PASSWORD\0) |
| WiFi Connect | `0x51` | Připojení k WiFi |
| WiFi Disconnect | `0x52` | Odpojení od WiFi |
| WiFi Status | `0x53` | Dotaz na stav WiFi |
| WiFi Clear | `0x54` | Smazání uložených credentials |
| OTA Update | `0x60` | Spuštění OTA aktualizace (URL\0) |
| OTA Check | `0x61` | Kontrola dostupnosti aktualizace |
| Get Version | `0x62` | Dotaz na verzi firmware |
| Get Info | `0x63` | Dotaz na informace o zařízení |

## Sestavení

### Flutter aplikace (zobo_flutter)

```bash
cd zobo_flutter
flutter pub get
flutter build apk --release
```

### ESP-IDF firmware (zobo_esp32)

**Požadavky:**
- ESP-IDF v5.0+ (doporučeno v5.2)
- Python 3.8+

**Instalace ESP-IDF (Windows):**
1. Stáhněte [ESP-IDF Tools Installer](https://dl.espressif.com/dl/esp-idf/)
2. Nainstalujte s výchozím nastavením
3. Spusťte "ESP-IDF 5.x CMD" nebo "ESP-IDF 5.x PowerShell"

**Sestavení a nahrání:**
```bash
cd zobo_esp32
idf.py build
idf.py -p COM9 flash monitor
```

**Poznámky:**
- Port `COM9` nahraďte skutečným portem vašeho ESP32 (zjistíte v Správci zařízení)
- První build může trvat několik minut
- Pro ukončení monitoru stiskněte `Ctrl+]`

### OTA aktualizace firmware

Robot podporuje vzdálenou aktualizaci firmware přes WiFi. Pro snadnou automatizaci použijte přiložené skripty.

**Automatizovaný OTA server (doporučeno):**
```bash
cd zobo_esp32

# Plný workflow: clean → build → flash → server
python ota_server.py

# Pouze build a server (bez flash)
python ota_server.py -n

# Pouze server (bez build)
python ota_server.py -s

# Použít jiný COM port
python ota_server.py --port COM5
```

Skript automaticky:
- Najde ESP-IDF instalaci (z VS Code nastavení nebo standardních cest)
- Zbuildí firmware
- Vytvoří `version.json` s verzí a datem buildu
- Spustí HTTP server na portu 8080

**Serial monitor:**
```bash
python monitor.py        # Výchozí COM9
python monitor.py COM5   # Jiný port
python monitor.py --list # Seznam dostupných portů
```

**Aktualizace z aplikace:**
1. **Nastavení WiFi** - V aplikaci přejděte do Settings a zadejte SSID a heslo WiFi
2. **Připojení** - Klikněte na "Connect" a počkejte na připojení
3. **Kontrola verze** - Aplikace automaticky zkontroluje dostupnou verzi na serveru
4. **Start Update** - Zobrazí dialog s porovnáním verzí:
   - **Update Available** (zelená) - novější verze k dispozici
   - **Reinstall** (oranžová) - stejná verze, varování před reinstalací
   - **Downgrade** (červená) - starší verze, varování před downgradem
5. **Progress bar** - Průběh stahování firmware v reálném čase
6. **Auto-restart** - Po úspěšné aktualizaci se ESP32 automaticky restartuje

**Poznámky k OTA:**
- ESP32 i telefon musí být na stejné WiFi síti jako OTA server
- Na Windows může být potřeba povolit port 8080 ve firewallu
- Firmware používá anti-rollback ochranu - po úspěšném bootu se nová verze potvrdí

**GitHub Releases (produkce):**

Pro vzdálené aktualizace odkudkoliv na světě:
```bash
cd zobo_esp32

# Vytvořit GitHub release (vyžaduje gh CLI)
python release.py

# Nebo jako draft
python release.py --draft
```

Skript automaticky:
- Zbuildí firmware
- Vytvoří version.json
- Nahraje na GitHub Releases

Aplikace pak automaticky stahuje z:
`https://github.com/DavidPetrov2023/Zobo/releases/latest/download/`

**Lokální server (development):**
```bash
cd zobo_esp32
python ota_server.py
```

V aplikaci přepnout na lokální server: `settings_page.dart` → `useGitHubReleases = false`

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
- **Settings stránka:**
  - Konfigurace WiFi (SSID, heslo)
  - OTA aktualizace firmware s progress barem
  - Automatická detekce dostupných aktualizací
  - Zobrazení verze firmware (aktuální i serverová)

## Licence

Tento projekt je proprietární. Všechna práva vyhrazena.

## Autor

David Petrov
