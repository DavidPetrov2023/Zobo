/**
 * MQTT Control - low latency channel for driving and LED
 *
 * The telemetry POST loop is fine for OTA and power mode, but its command
 * latency is one telemetry period (~5 s), which is unusable for driving. This
 * module keeps a persistent MQTT connection instead, giving ~50 ms.
 *
 * Topics (device_id is the hardware id the robot reports, e.g. Zobo-19CA30):
 *   zobo/<device_id>/cmd    subscribed - drive and LED commands
 *   zobo/<device_id>/state  published  - heartbeat and status
 *
 * Safety: driving commands carry absolute state, and if none arrives within
 * MQTT_DEADMAN_MS the motors stop by themselves. A dropped connection therefore
 * stops the robot instead of leaving it running.
 */

#ifndef MQTT_CONTROL_H
#define MQTT_CONTROL_H

#include <stdbool.h>
#include "esp_err.h"

// Stop the motors if no drive command arrives within this window.
#define MQTT_DEADMAN_MS 400

// Start the client. Safe to call once WiFi is up; reconnects are handled
// internally. device_id is copied.
esp_err_t mqtt_control_start(const char *device_id);

// Stop the client and release the connection (used before deep sleep).
void mqtt_control_stop(void);

// True while the broker connection is up.
bool mqtt_control_is_connected(void);

#endif // MQTT_CONTROL_H
