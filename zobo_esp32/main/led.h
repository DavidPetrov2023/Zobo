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

// Drive the blue LED to blink during OTA — slow at 0 %, faster as progress
// rises, solid on at 100 %. Safe to call from event callback (non-blocking).
// Pass progress < 0 to stop blinking.
void led_ota_set_progress(int progress);

// Brief (~60 ms) green flash to acknowledge a successful telemetry POST.
// Skips itself if OTA is in progress so it does not interrupt the blue blinker.
// Blocking; call from a task context (not from an ISR).
void led_telemetry_pulse(void);

#endif // LED_H
