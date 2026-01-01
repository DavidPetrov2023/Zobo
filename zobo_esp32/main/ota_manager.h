/**
 * OTA Update Manager - Header
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

// OTA status callback
typedef void (*ota_status_callback_t)(int progress, const char *status);

// Firmware version info
#define FIRMWARE_VERSION "1.0.3"

// Initialize OTA manager
esp_err_t ota_manager_init(void);

// Set status callback
void ota_manager_set_callback(ota_status_callback_t callback);

// Start OTA update from URL
// Returns ESP_OK if update started, ESP_FAIL on error
esp_err_t ota_manager_start_update(const char *url);

// Get current firmware version
const char* ota_manager_get_version(void);

// Check for update at URL (returns true if newer version available)
// version_url should point to a text file containing just the version string
esp_err_t ota_manager_check_update(const char *version_url, bool *update_available);

#endif // OTA_MANAGER_H
