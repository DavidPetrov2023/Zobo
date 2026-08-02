/**
 * Aktualizace firmwaru kamery po siti.
 *
 * Kamera sedi za NATem, takze si novy obraz musi stahnout sama - povel prijde
 * jako {"ota":"https://..."} na tema zobo/cam/ctrl, ktere uz stejne odebira.
 * Certifikaty jsou v binarce kvuli TLS spojeni na broker, takze HTTPS stazeni
 * nic dalsiho nestoji.
 *
 * Pojistka: cerstve nahrany obraz plati jen na zkousku. Za platny se prohlasi
 * teprve ve chvili, kdy se znovu prihlasi na broker - tedy kdyz je prokazatelne
 * na siti a dosazitelny. Kdyz se to do OTA_CONFIRM_TIMEOUT_MS nestihne, kamera
 * se restartuje a zavadec ji vrati do predchoziho slotu. Bez toho by spatny
 * build znamenal cestu za robotem s USB-TTL adapterem v kapse.
 */

#ifndef OTA_CAM_H
#define OTA_CAM_H

#include "esp_err.h"
#include <stdbool.h>

// Cislo, ktere kamera hlasi ve stavu a v /get - jinak se po vzdalene
// aktualizaci neda poznat, jestli se novy firmware vubec chytil.
#define CAM_FW_VERSION "1.1.5"

// Jak dlouho ma novy obraz na to, aby se pripojil k brokeru a potvrdil se.
#define OTA_CONFIRM_TIMEOUT_MS 120000

// Zjisti, jestli bezici obraz ceka na potvrzeni, a nastartuje hlidac.
esp_err_t ota_cam_init(void);

// Bezi na zkousku a jeste nepotvrzeny?
bool ota_cam_pending(void);

// Volat, jakmile je kamera prokazatelne na siti (pripojeni k brokeru).
void ota_cam_confirm(void);

// Stahne firmware z URL do volneho slotu a restartuje.
esp_err_t ota_cam_start(const char *url);

// Probiha zrovna stahovani?
bool ota_cam_busy(void);

// Odstavi kameru pred zapisem do flash.
//
// Mazani flash vypina cache, a obsluha preruseni kamery lezi ve flash - kdyz na
// ni v tu chvili dojde rada, nema se odkud spustit a desku shodi interrupt
// watchdog (rst:0x8 TG1WDT_SYS_RESET). Projevovalo se to jako nahodne umrti
// zhruba pri kazdem druhem nahravani. Po zapisu se deska stejne restartuje,
// takze odstavenim kamery o nic neprichazime.
void ota_cam_stop_camera(void);

#endif // OTA_CAM_H
