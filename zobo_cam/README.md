# Zobo kamera (AI-Thinker ESP32-CAM)

Samostatný modul s vlastní WiFi, ne součást firmwaru robota. Robot má svůj
ESP32 plný (motory, BLE, telemetrie, MQTT) a kamera by mu vzala piny i paměť,
takže jede vedle něj jako druhé zařízení.

První fáze, kterou dělá tenhle kód: nahodit kameru a pustit obraz do místní
sítě, aby bylo na co se dívat dřív, než se to bude napojovat na web.

    http://<ip-kamery>/          stránka s živým obrazem
    http://<ip-kamery>/stream    samotný MJPEG proud (jde vložit do <img>)
    http://<ip-kamery>/jpg       jeden snímek
    http://<ip-kamery>/status    JSON: uptime, počet snímků, volná paměť
    http://<ip-kamery>/led?on=1  bílé přisvětlení (svítí opravdu hodně)

IP adresa se vypíše do sériové konzole hned po připojení.

## Zapojení k ESP-PROG

ESP32-CAM nemá USB, programuje se přes sériovou linku. ESP-PROG má na konektoru
`PROG` i signály pro automatický restart, takže není nutné držet tlačítko.

| ESP-PROG | ESP32-CAM | poznámka |
|---|---|---|
| ESP_TXD | U0R (GPIO3) | křížem |
| ESP_RXD | U0T (GPIO1) | křížem |
| ESP_EN  | EN | automatický restart |
| ESP_IO0 | IO0 | přepnutí do bootloaderu |
| GND | GND | |
| VDD | 5V | viz níže |

**Napájení je nejčastější příčina potíží.** Modul si při vysílání WiFi krátce
řekne o proud, který slabý zdroj neutáhne, a deska se restartuje dokola
(v logu `Brownout detector was triggered`). Když se to stane, napájej desku
z externího 5V zdroje a s programátorem spoj jen GND, TX, RX, EN a IO0.

Jumper napětí na ESP-PROG musí být na **3.3V** — týká se úrovní na datových
vodičích, ne napájení.

Bez automatického restartu (jen USB-TTL převodník) se do bootloaderu jde ručně:
spojit IO0 s GND, stisknout RESET, pustit, nahrát, odpojit IO0 a znovu RESET.

## Sestavení a nahrání

Přihlašovací údaje k WiFi nejsou v repozitáři:

    cp main/wifi_config.h.example main/wifi_config.h   # a vyplnit síť

Pak z PowerShellu (ne z Git Bash, tam `idf.py` není vidět):

```powershell
Set-Location C:\Cloud\AI\Zobo\zobo_cam
. "$env:USERPROFILE\esp\v5.2.6\esp-idf\export.ps1"
idf.py build
idf.py -p COM5 flash monitor      # port podle toho, co ESP-PROG vytvoří
```

ESP-PROG se v systému hlásí jako dva porty; ten sériový je obvykle ten druhý
(vyšší číslo). První je JTAG.

## Co dál

1. **Obraz na web.** Kamera je za NATem, takže se na ni z internetu nedá
   připojit. Snímky proto poputují ven — nejspíš přes MQTT broker, který už pro
   robota běží (`wss://petrovelektronika.cz/mqtt`), do stejného dashboardu, kde
   je teď zástupný obdélník. Odpadá tím děravění routeru i další server.
2. **Rozlišení a datový tok.** VGA při kvalitě 12 dělá zhruba 20–30 kB na
   snímek. Deset snímků za sekundu je tedy ~2 Mbit/s, což WiFi utáhne, ale přes
   internet je to zbytečně moc — pro řízení robota stačí menší obrázek a nižší
   frekvence.
3. **OTA.** Zatím se nahrává kabelem; oddíl na druhou kopii firmwaru v rozvržení
   flash paměti není, protože ho zabral kamerový ovladač.

## Poznámky k desce

- `GPIO0` je zároveň hodinový signál kamery i volba bootloaderu. Proto se při
  nahrávání uzemňuje a po startu ho zabere kamera.
- `GPIO4` spíná bílou LED a je sdílený se slotem na SD kartu — obojí najednou
  nejde.
- Obraz je překlopený (`set_vflip`, `set_hmirror`), protože modul bývá na
  robotu vzhůru nohama. Když bude jinak, změní se to v `camera_start()`.
