/**
 * Telemetry Module - Header
 *
 * Periodic POST of device status JSON to a remote endpoint
 * (the /devices/ dashboard on petrovelektronika.cz).
 *
 * Runs as a background task. Skips POST when WiFi is not connected.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "esp_err.h"

esp_err_t telemetry_init(void);

// Used when booting in SLEEP power mode. Performs WiFi connect + one telemetry
// POST so the server can deliver a queued command (e.g. "wake"), then deep-
// sleeps the device for 10 minutes. Never returns under normal flow — the
// command handler may esp_restart() into ACTIVE mode instead.
void telemetry_sleep_cycle(void);

// Hardware id the robot reports (e.g. "Zobo-19CA30"). MQTT topics are built
// from it, so both sides must use the same string. Valid after telemetry_init().
const char *telemetry_device_id(void);

#endif // TELEMETRY_H
