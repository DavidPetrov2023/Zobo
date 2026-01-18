/**
 * Zobo ESP32 Robot Controller - Main Application
 *
 * Features:
 * - BLE UART control
 * - Motor PWM control
 * - RGB LED control
 * - WiFi configuration via BLE
 * - OTA firmware updates
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "motor.h"
#include "led.h"
#include "ble_service.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "sleep_manager.h"

static const char *TAG = "ZOBO";

// BLE Command codes
#define CMD_BACKWARD        0x00
#define CMD_FORWARD         0x01
#define CMD_STOP            0x02
#define CMD_RIGHT           0x03
#define CMD_LEFT            0x04
#define CMD_MANUAL_PWM      0x05
#define CMD_LED_GREEN       10
#define CMD_LED_RED         20
#define CMD_LED_BLUE        30
#define CMD_LED_ALL         40

// Extended commands for WiFi/OTA
#define CMD_WIFI_SET        0x50    // Set WiFi credentials: 0x50 + SSID\0PASSWORD\0
#define CMD_WIFI_CONNECT    0x51    // Connect to WiFi
#define CMD_WIFI_DISCONNECT 0x52    // Disconnect from WiFi
#define CMD_WIFI_STATUS     0x53    // Get WiFi status
#define CMD_WIFI_CLEAR      0x54    // Clear saved credentials
#define CMD_OTA_UPDATE      0x60    // Start OTA: 0x60 + URL\0
#define CMD_OTA_CHECK       0x61    // Check for update: 0x61 + VERSION_URL\0
#define CMD_GET_VERSION     0x62    // Get firmware version
#define CMD_GET_INFO        0x63    // Get device info
#define CMD_PING            0x70    // Keepalive ping

// OTA status callback - sends status to BLE
static void ota_status_callback(int progress, const char *status)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "OTA:%d:%s", progress, status);
    ble_service_send(buf);
}

// Process motor/LED commands
static void process_motor_command(uint8_t cmd, uint8_t param)
{
    switch (cmd) {
        case CMD_BACKWARD:
            motor_cancel_ramp();
            motor_reset_inactivity();
            motor_set_pwm(50, 50);
            motor_set_direction(true, true);
            ESP_LOGI(TAG, "Moving backward");
            break;

        case CMD_FORWARD:
            motor_reset_inactivity();
            motor_start_ramp();
            break;

        case CMD_STOP:
            motor_cancel_ramp();
            motor_stop();
            led_set_main(false);
            ESP_LOGI(TAG, "Stopped");
            break;

        case CMD_RIGHT:
            motor_cancel_ramp();
            motor_reset_inactivity();
            motor_set_pwm(200, 255 - 200);
            motor_set_direction(false, true);
            ESP_LOGI(TAG, "Turning right");
            break;

        case CMD_LEFT:
            motor_cancel_ramp();
            motor_reset_inactivity();
            motor_set_pwm(255 - 200, 200);
            motor_set_direction(true, false);
            ESP_LOGI(TAG, "Turning left");
            break;

        case CMD_MANUAL_PWM:
            motor_cancel_ramp();
            motor_reset_inactivity();
            if (param >= 50) {
                motor_set_pwm(180 - (param - 50), 180 + (param - 50));
            } else {
                motor_set_pwm(180 + (50 - param), 180 - (50 - param));
            }
            motor_set_direction(false, false);
            ESP_LOGI(TAG, "Manual PWM: %d", param);
            break;

        case CMD_LED_GREEN:
            led_set_rgb(false, true, false);
            ESP_LOGI(TAG, "LED: Green");
            break;

        case CMD_LED_RED:
            led_set_rgb(true, false, false);
            ESP_LOGI(TAG, "LED: Red");
            break;

        case CMD_LED_BLUE:
            led_set_rgb(false, false, true);
            ESP_LOGI(TAG, "LED: Blue");
            break;

        case CMD_LED_ALL:
            led_set_rgb(true, true, true);
            ESP_LOGI(TAG, "LED: All");
            break;
    }
}

// WiFi connect task
static void wifi_connect_task(void *arg)
{
    char response[128];
    vTaskDelay(pdMS_TO_TICKS(500)); // Give BLE time to finish

    if (wifi_manager_connect() == ESP_OK) {
        snprintf(response, sizeof(response), "WIFI:CONNECTED:%s",
                 wifi_manager_get_ip());
    } else {
        snprintf(response, sizeof(response), "WIFI:ERR:Connection failed");
    }
    ble_service_send(response);
    vTaskDelete(NULL);
}

// Process WiFi commands
static void process_wifi_command(uint8_t cmd, uint8_t *data, uint16_t len)
{
    char response[128];

    switch (cmd) {
        case CMD_WIFI_SET: {
            // Format: SSID\0PASSWORD\0
            if (len < 2) {
                ble_service_send("WIFI:ERR:Invalid data");
                return;
            }

            // Debug: log raw data
            ESP_LOGI(TAG, "WiFi SET raw data len=%d:", len);
            for (int i = 0; i < len && i < 64; i++) {
                ESP_LOGI(TAG, "  [%d] = 0x%02X '%c'", i, data[i], (data[i] >= 32 && data[i] < 127) ? data[i] : '.');
            }

            char *ssid = (char *)data;
            char *password = ssid + strlen(ssid) + 1;

            ESP_LOGI(TAG, "Parsed SSID='%s' (len=%d), Password='***' (len=%d)",
                     ssid, strlen(ssid), strlen(password));

            if (wifi_manager_set_credentials(ssid, password) == ESP_OK) {
                snprintf(response, sizeof(response), "WIFI:OK:Saved %s", ssid);
            } else {
                snprintf(response, sizeof(response), "WIFI:ERR:Save failed");
            }
            ble_service_send(response);
            break;
        }

        case CMD_WIFI_CONNECT:
            ble_service_send("WIFI:CONNECTING");
            // Start WiFi connection in a separate task to avoid BLE/WiFi coexistence issues
            xTaskCreate(wifi_connect_task, "wifi_connect", 4096, NULL, 5, NULL);
            break;

        case CMD_WIFI_DISCONNECT:
            wifi_manager_disconnect();
            ble_service_send("WIFI:DISCONNECTED");
            break;

        case CMD_WIFI_STATUS: {
            wifi_status_t status = wifi_manager_get_status();
            const char *status_str[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "FAILED"};
            if (status == WIFI_STATUS_CONNECTED) {
                snprintf(response, sizeof(response), "WIFI:%s:%s",
                         status_str[status], wifi_manager_get_ip());
            } else {
                snprintf(response, sizeof(response), "WIFI:%s", status_str[status]);
            }
            ble_service_send(response);
            break;
        }

        case CMD_WIFI_CLEAR:
            wifi_manager_clear_credentials();
            ble_service_send("WIFI:CLEARED");
            break;
    }
}

// Process OTA commands
static void process_ota_command(uint8_t cmd, uint8_t *data, uint16_t len)
{
    char response[128];

    switch (cmd) {
        case CMD_OTA_UPDATE: {
            if (len < 1) {
                ble_service_send("OTA:ERR:No URL");
                return;
            }
            char *url = (char *)data;

            // Check WiFi connection
            if (wifi_manager_get_status() != WIFI_STATUS_CONNECTED) {
                ble_service_send("OTA:ERR:WiFi not connected");
                return;
            }

            if (ota_manager_start_update(url) == ESP_OK) {
                ble_service_send("OTA:STARTED");
            } else {
                ble_service_send("OTA:ERR:Failed to start");
            }
            break;
        }

        case CMD_OTA_CHECK: {
            if (len < 1) {
                ble_service_send("OTA:ERR:No URL");
                return;
            }
            char *url = (char *)data;

            if (wifi_manager_get_status() != WIFI_STATUS_CONNECTED) {
                ble_service_send("OTA:ERR:WiFi not connected");
                return;
            }

            bool update_available = false;
            if (ota_manager_check_update(url, &update_available) == ESP_OK) {
                snprintf(response, sizeof(response), "OTA:CHECK:%s",
                         update_available ? "AVAILABLE" : "UP_TO_DATE");
            } else {
                snprintf(response, sizeof(response), "OTA:ERR:Check failed");
            }
            ble_service_send(response);
            break;
        }

        case CMD_GET_VERSION:
            snprintf(response, sizeof(response), "VERSION:%s", ota_manager_get_version());
            ble_service_send(response);
            break;

        case CMD_GET_INFO:
            snprintf(response, sizeof(response), "INFO:Zobo v%s,WiFi:%s",
                     ota_manager_get_version(),
                     wifi_manager_has_credentials() ? "configured" : "not_set");
            ble_service_send(response);
            break;
    }
}

// BLE command handler
static void ble_command_handler(uint8_t *data, uint16_t len)
{
    if (len < 1) return;

    // Reset sleep timer on any BLE command
    sleep_manager_reset();

    uint8_t cmd = data[0];
    uint8_t param = (len > 1) ? data[1] : 0;

    ESP_LOGI(TAG, "Command: 0x%02X, len: %d", cmd, len);

    // Route command to appropriate handler
    if (cmd <= CMD_LED_ALL) {
        process_motor_command(cmd, param);
        ble_service_send("OK");
    } else if (cmd >= CMD_WIFI_SET && cmd <= CMD_WIFI_CLEAR) {
        process_wifi_command(cmd, data + 1, len - 1);
    } else if (cmd >= CMD_OTA_UPDATE && cmd <= CMD_GET_INFO) {
        process_ota_command(cmd, data + 1, len - 1);
    } else if (cmd == CMD_PING) {
        // Keepalive ping - just reset sleep timer (already done above)
        // No response needed to reduce traffic
    } else {
        ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
        ble_service_send("ERR:Unknown");
    }
}

// Control loop task
static void control_loop_task(void *arg)
{
    const TickType_t delay = pdMS_TO_TICKS(10);

    while (1) {
        motor_update_ramp();
        motor_check_inactivity();
        vTaskDelay(delay);
    }
}

// Main entry point
void app_main(void)
{
    // Check if woke from deep sleep - if so, blink and sleep again
    // This must be FIRST to avoid full initialization
    sleep_manager_check_wake();

    ESP_LOGI(TAG, "Zobo ESP32 Robot Controller v%s Starting...", ota_manager_get_version());

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize hardware
    led_init();
    motor_init();

    // Run LED startup sequence (only on fresh boot, not from sleep)
    led_startup_sequence();

    // Initialize WiFi manager (loads saved credentials)
    wifi_manager_init();

    // Initialize OTA manager
    ota_manager_init();
    ota_manager_set_callback(ota_status_callback);

    // Initialize BLE
    ble_service_init();
    ble_service_set_callback(ble_command_handler);

    // Initialize sleep manager
    sleep_manager_init();

    ESP_LOGI(TAG, "Ready! Waiting for BLE connection...");

    // Start control loop task
    xTaskCreate(control_loop_task, "control_loop", 4096, NULL, 5, NULL);
}
