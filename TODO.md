# Zobo TODO

## Priorita: Vysoká

- [ ] **Delayed OTA validation** - Potvrdit nový firmware až po úspěšném BLE spojení nebo 60s běhu (ne hned v init)
- [ ] **GitHub Actions** - CI/CD pro automatické buildy a release

## Priorita: Střední

- [ ] **Unit testy** - Alespoň kritické funkce (OTA, WiFi manager)
- [ ] **Error handling** - Propagovat chyby uživateli v appce (ne jen logovat)
- [ ] **Automatické verzování** - Verze z git tagu místo manuální změny v ota_manager.h

## Priorita: Nízká

- [ ] **Dokumentace kódu** - Komentáře k API funkcím
- [ ] **Crash reporting** - Firebase Crashlytics pro Flutter app
- [ ] **Podepsaný APK** - Pro případné vydání na Play Store
