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

#endif // TELEMETRY_H
