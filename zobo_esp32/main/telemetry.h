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

#endif // TELEMETRY_H
