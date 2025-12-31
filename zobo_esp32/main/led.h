/**
 * LED Control Module - Header
 */

#ifndef LED_H
#define LED_H

#include <stdbool.h>

// Initialize LED GPIO
void led_init(void);

// Set RGB LED state
void led_set_rgb(bool red, bool green, bool blue);

// Set main LED state
void led_set_main(bool on);

// Run startup LED sequence
void led_startup_sequence(void);

// LED patterns for status indication
void led_indicate_wifi_connecting(void);
void led_indicate_wifi_connected(void);
void led_indicate_ota_progress(void);
void led_indicate_ota_success(void);
void led_indicate_ota_fail(void);

#endif // LED_H
