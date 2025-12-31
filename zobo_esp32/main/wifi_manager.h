/**
 * WiFi Manager Module - Header
 * Handles WiFi connection and credentials storage in NVS
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

// WiFi status
typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
} wifi_status_t;

// Initialize WiFi manager (loads credentials from NVS if available)
esp_err_t wifi_manager_init(void);

// Set WiFi credentials and save to NVS
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

// Check if credentials are stored
bool wifi_manager_has_credentials(void);

// Connect to WiFi using stored credentials
esp_err_t wifi_manager_connect(void);

// Disconnect from WiFi
esp_err_t wifi_manager_disconnect(void);

// Get current WiFi status
wifi_status_t wifi_manager_get_status(void);

// Get current IP address (returns empty string if not connected)
const char* wifi_manager_get_ip(void);

// Clear stored credentials
esp_err_t wifi_manager_clear_credentials(void);

#endif // WIFI_MANAGER_H
