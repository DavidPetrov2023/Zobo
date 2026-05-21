/**
 * Power Mode — high-level power state stored in NVS so it survives reboots.
 *
 * ACTIVE: full operation, telemetry every 5 s, BLE on (today's behaviour).
 * SLEEP:  minimal wake every 10 min just to POST telemetry and check whether
 *         the operator queued a "wake" command from the web UI. Otherwise
 *         deep-sleeps right back, saving ~95 % of average power.
 */

#ifndef POWER_MODE_H
#define POWER_MODE_H

#include "esp_err.h"

typedef enum {
    POWER_MODE_ACTIVE = 0,  // default for a fresh device
    POWER_MODE_SLEEP  = 1,
} power_mode_t;

// Read from NVS. Defaults to POWER_MODE_ACTIVE when the key is unset (fresh
// flash) so an OTA can't accidentally orphan a robot in sleep mode.
power_mode_t power_mode_load(void);

// Persist to NVS. Safe to call from any task.
esp_err_t power_mode_save(power_mode_t mode);

// Stringify for logs and telemetry payload.
const char *power_mode_str(power_mode_t mode);

#endif // POWER_MODE_H
