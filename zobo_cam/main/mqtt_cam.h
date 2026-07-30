/**
 * Cesta obrazu ven ze site.
 *
 * Kamera sedi za NATem, takze se na ni z internetu neda pripojit. Snimky proto
 * odchazi na MQTT broker, ktery uz bezi kvuli robotovi, a stranka na webu si je
 * odtamtud vyzvedne.
 *
 * Vysila se jen kdyz se nekdo diva: prohlizec pravidelne hlasi zajem na tema
 * zobo/cam/ctrl a kamera po chvili ticha prestane posilat. Bez toho by uzka
 * linka serveru tekla nepretrzite i v noci, kdy se nikdo nekouka.
 */

#ifndef MQTT_CAM_H
#define MQTT_CAM_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t mqtt_cam_start(void);

// Dela nekdo divaka? Pouziva se i na stavovou stranku.
bool mqtt_cam_has_viewer(void);

#endif // MQTT_CAM_H
