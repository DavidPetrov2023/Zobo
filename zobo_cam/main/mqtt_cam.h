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
#include <stdint.h>

esp_err_t mqtt_cam_start(void);

// Dela nekdo divaka? Pouziva se i na stavovou stranku.
bool mqtt_cam_has_viewer(void);

// Jak dlouho trvalo odeslani posledniho snimku, v ms. Blizi-li se to perode
// snimku, posila se rychleji, nez linka staci odvest, a obraz zacne chodit
// opozdeny - coz na propustnosti nepoznas, ta pri fronte vypada dobre.
uint32_t mqtt_cam_publish_ms(void);

// Rozestup mezi snimky v ms. Nizsi cislo znamena plynulejsi obraz jen do chvile,
// nez se priblizi dobe odeslani (`pub_ms`) - pak uz se snimky nestihaji odvest,
// vrsi se fronta a obraz sice chodi porad, ale zpozdeny.
uint32_t mqtt_cam_get_period_ms(void);
void mqtt_cam_set_period_ms(uint32_t ms);

#endif // MQTT_CAM_H
