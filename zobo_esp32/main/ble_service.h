/**
 * BLE UART Service - Header
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Command callback - called when data received via BLE
typedef void (*ble_command_callback_t)(uint8_t *data, uint16_t len);

// Initialize BLE service
esp_err_t ble_service_init(void);

// Set command callback
void ble_service_set_callback(ble_command_callback_t callback);

// Send data to connected client
void ble_service_send(const char *data);

// Check if device is connected
bool ble_service_is_connected(void);

// Pause BLE (for WiFi coexistence)
void ble_service_pause(void);

// Resume BLE
void ble_service_resume(void);

#endif // BLE_SERVICE_H
