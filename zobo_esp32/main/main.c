/**
 * Zobo ESP32 Robot Controller - Native ESP-IDF Implementation
 *
 * BLE UART Service for robot control with:
 * - Motor PWM control (left/right)
 * - RGB LED control
 * - Forward ramp acceleration
 * - Inactivity timeout
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

static const char *TAG = "ZOBO";

// ============================================================================
// GPIO Pin Definitions
// ============================================================================

// Motor PWM pins
#define PWM_MOTOR_LEFT      16  // P11
#define MOTOR_LEFT_DIR      17  // P13
#define PWM_MOTOR_RIGHT     25  // P3
#define MOTOR_RIGHT_DIR     26  // P12

// LED pins (active LOW)
#define LED_MAIN            5
#define LED_RED             27
#define LED_GREEN           14
#define LED_BLUE            12

// ============================================================================
// PWM Configuration
// ============================================================================

#define PWM_FREQ_HZ         5000
#define PWM_RESOLUTION      LEDC_TIMER_8_BIT
#define PWM_CHANNEL_LEFT    LEDC_CHANNEL_0
#define PWM_CHANNEL_RIGHT   LEDC_CHANNEL_1

// ============================================================================
// Ramp and Timing Configuration
// ============================================================================

#define RAMP_START_PWM      100
#define RAMP_END_PWM        255
#define RAMP_DURATION_MS    2000
#define LOOP_DELAY_MS       10
#define INACTIVITY_MS       300
#define INACTIVITY_TICKS    (INACTIVITY_MS / LOOP_DELAY_MS)

// ============================================================================
// BLE UUIDs (Nordic UART Service)
// ============================================================================

// Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
static uint8_t service_uuid[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
};

// RX UUID: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
static uint8_t char_rx_uuid[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
};

// TX UUID: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
static uint8_t char_tx_uuid[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
};

// ============================================================================
// BLE State Variables
// ============================================================================

static uint16_t gatts_if_global = ESP_GATT_IF_NONE;
static uint16_t conn_id_global = 0;
static bool device_connected = false;
static uint16_t tx_handle = 0;
static uint16_t tx_cccd_handle = 0;
static bool tx_notify_enabled = false;

// ============================================================================
// Motor Control State
// ============================================================================

static bool ramp_forward_active = false;
static bool forward_latched = false;
static uint32_t ramp_start_ms = 0;
static int inactivity_timer = 0;
static bool timer_active = false;

// ============================================================================
// BLE Advertising Data
// ============================================================================

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_data[] = {
    0x02, 0x01, 0x06,                   // Flags: LE General Discoverable, BR/EDR Not Supported
    0x05, 0x09, 'Z', 'o', 'b', 'o',     // Complete Local Name: "Zobo"
    0x11, 0x07,                         // Complete List of 128-bit Service UUIDs
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
};

// ============================================================================
// GATT Service/Characteristic Handles
// ============================================================================

enum {
    IDX_SVC,
    IDX_CHAR_TX,
    IDX_CHAR_TX_VAL,
    IDX_CHAR_TX_CFG,    // CCCD for notifications
    IDX_CHAR_RX,
    IDX_CHAR_RX_VAL,
    IDX_NB,
};

static uint16_t handle_table[IDX_NB];

// GATT Service Database
static const esp_gatts_attr_db_t gatt_db[IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_PRI_SERVICE},
         ESP_GATT_PERM_READ, sizeof(service_uuid), sizeof(service_uuid), service_uuid}
    },
    // TX Characteristic Declaration
    [IDX_CHAR_TX] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE},
         ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_NOTIFY}}
    },
    // TX Characteristic Value
    [IDX_CHAR_TX_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_tx_uuid,
         0, 500, 0, NULL}
    },
    // TX CCCD (Client Characteristic Configuration Descriptor)
    [IDX_CHAR_TX_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_CLIENT_CONFIG},
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 0, NULL}
    },
    // RX Characteristic Declaration
    [IDX_CHAR_RX] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE},
         ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR}}
    },
    // RX Characteristic Value
    [IDX_CHAR_RX_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, char_rx_uuid,
         ESP_GATT_PERM_WRITE, 500, 0, NULL}
    },
};

// ============================================================================
// Motor Control Functions
// ============================================================================

static void motor_init(void)
{
    // Configure direction pins as outputs
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << MOTOR_LEFT_DIR) | (1ULL << MOTOR_RIGHT_DIR),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    // Configure left motor PWM channel
    ledc_channel_config_t left_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PWM_CHANNEL_LEFT,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_MOTOR_LEFT,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&left_conf);

    // Configure right motor PWM channel
    ledc_channel_config_t right_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PWM_CHANNEL_RIGHT,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_MOTOR_RIGHT,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&right_conf);

    ESP_LOGI(TAG, "Motor PWM initialized");
}

static void motor_set_pwm(uint8_t left, uint8_t right)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_LEFT, left);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_LEFT);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_RIGHT, right);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_RIGHT);
}

static void motor_set_direction(bool left_high, bool right_high)
{
    gpio_set_level(MOTOR_LEFT_DIR, left_high ? 1 : 0);
    gpio_set_level(MOTOR_RIGHT_DIR, right_high ? 1 : 0);
}

static void motor_stop(void)
{
    motor_set_pwm(0, 0);
    motor_set_direction(false, false);
    ramp_forward_active = false;
    forward_latched = false;
}

// ============================================================================
// LED Control Functions
// ============================================================================

static void led_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_MAIN) | (1ULL << LED_RED) |
                        (1ULL << LED_GREEN) | (1ULL << LED_BLUE),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    // Turn off all LEDs (active LOW, so HIGH = off)
    gpio_set_level(LED_MAIN, 0);
    gpio_set_level(LED_RED, 1);
    gpio_set_level(LED_GREEN, 1);
    gpio_set_level(LED_BLUE, 1);

    ESP_LOGI(TAG, "LEDs initialized");
}

static void led_set_rgb(bool red, bool green, bool blue)
{
    // Active LOW LEDs
    gpio_set_level(LED_RED, red ? 0 : 1);
    gpio_set_level(LED_GREEN, green ? 0 : 1);
    gpio_set_level(LED_BLUE, blue ? 0 : 1);
}

static void led_startup_sequence(void)
{
    // Startup LED sequence like in PlatformIO version
    gpio_set_level(LED_MAIN, 0);
    gpio_set_level(LED_RED, 1);
    gpio_set_level(LED_GREEN, 1);
    gpio_set_level(LED_BLUE, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    gpio_set_level(LED_MAIN, 1);
    gpio_set_level(LED_RED, 0);  // Red on
    vTaskDelay(pdMS_TO_TICKS(1000));

    gpio_set_level(LED_BLUE, 0);  // Blue on
    gpio_set_level(LED_RED, 1);   // Red off
    vTaskDelay(pdMS_TO_TICKS(1000));

    gpio_set_level(LED_GREEN, 0); // Green on
    gpio_set_level(LED_BLUE, 1);  // Blue off
    vTaskDelay(pdMS_TO_TICKS(1000));

    gpio_set_level(LED_GREEN, 1); // Green off
    vTaskDelay(pdMS_TO_TICKS(1000));

    // All RGB on
    gpio_set_level(LED_RED, 0);
    gpio_set_level(LED_GREEN, 0);
    gpio_set_level(LED_BLUE, 0);
}

// ============================================================================
// BLE Send Data
// ============================================================================

static void ble_send_data(const char *str)
{
    if (device_connected && tx_notify_enabled && gatts_if_global != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(gatts_if_global, conn_id_global,
                                    handle_table[IDX_CHAR_TX_VAL],
                                    strlen(str), (uint8_t *)str, false);
    }
}

// ============================================================================
// Command Processing
// ============================================================================

static void process_command(uint8_t *data, uint16_t len)
{
    if (len < 1) return;

    uint8_t cmd = data[0];
    uint8_t param = (len > 1) ? data[1] : 0;

    ESP_LOGI(TAG, "Command: 0x%02X, Param: %d", cmd, param);

    switch (cmd) {
        case 0x00:  // Backward
            forward_latched = false;
            ramp_forward_active = false;
            timer_active = true;
            inactivity_timer = INACTIVITY_TICKS;
            motor_set_pwm(100, 100);
            motor_set_direction(true, true);
            ESP_LOGI(TAG, "Moving backward");
            break;

        case 0x01:  // Forward with ramp
            timer_active = true;
            inactivity_timer = INACTIVITY_TICKS;
            motor_set_direction(false, false);

            if (!ramp_forward_active && !forward_latched) {
                ramp_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ramp_forward_active = true;
                motor_set_pwm(RAMP_START_PWM, RAMP_START_PWM);
                ESP_LOGI(TAG, "Starting forward ramp");
            }
            break;

        case 0x02:  // Stop
            forward_latched = false;
            ramp_forward_active = false;
            motor_stop();
            gpio_set_level(LED_MAIN, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
            gpio_set_level(LED_MAIN, 0);
            ESP_LOGI(TAG, "Stopped");
            break;

        case 0x03:  // Right
            forward_latched = false;
            ramp_forward_active = false;
            timer_active = true;
            inactivity_timer = INACTIVITY_TICKS;
            motor_set_pwm(150, 255 - 150);
            motor_set_direction(true, false);
            ESP_LOGI(TAG, "Turning right");
            break;

        case 0x04:  // Left
            forward_latched = false;
            ramp_forward_active = false;
            timer_active = true;
            inactivity_timer = INACTIVITY_TICKS;
            motor_set_pwm(255 - 150, 150);
            motor_set_direction(false, true);
            ESP_LOGI(TAG, "Turning left");
            break;

        case 0x05:  // Manual PWM control
            forward_latched = false;
            ramp_forward_active = false;
            timer_active = true;
            inactivity_timer = INACTIVITY_TICKS;

            if (param >= 50) {
                motor_set_pwm(180 - (param - 50), 180 + (param - 50));
            } else {
                motor_set_pwm(180 + (50 - param), 180 - (50 - param));
            }
            motor_set_direction(false, false);
            ESP_LOGI(TAG, "Manual PWM: %d", param);
            break;

        case 10:  // Green LED
            led_set_rgb(false, true, false);
            ESP_LOGI(TAG, "LED: Green");
            break;

        case 20:  // Red LED
            led_set_rgb(true, false, false);
            ESP_LOGI(TAG, "LED: Red");
            break;

        case 30:  // Blue LED
            led_set_rgb(false, false, true);
            ESP_LOGI(TAG, "LED: Blue");
            break;

        case 40:  // All LEDs
            led_set_rgb(true, true, true);
            ESP_LOGI(TAG, "LED: All");
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
            break;
    }
}

// ============================================================================
// BLE GAP Event Handler
// ============================================================================

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started");
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// BLE GATTS Event Handler
// ============================================================================

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATT server registered");
            gatts_if_global = gatts_if;

            // Set advertising data
            esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));

            // Create attribute table
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_NB, 0);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                memcpy(handle_table, param->add_attr_tab.handles, sizeof(handle_table));
                tx_handle = handle_table[IDX_CHAR_TX_VAL];
                tx_cccd_handle = handle_table[IDX_CHAR_TX_CFG];
                esp_ble_gatts_start_service(handle_table[IDX_SVC]);
                ESP_LOGI(TAG, "Service started");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Device connected");
            device_connected = true;
            conn_id_global = param->connect.conn_id;
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Device disconnected");
            device_connected = false;
            tx_notify_enabled = false;
            motor_stop();
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == handle_table[IDX_CHAR_RX_VAL]) {
                // RX characteristic - process command
                process_command(param->write.value, param->write.len);
                ble_send_data("6.25");

                // Send response if needed
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                               param->write.trans_id, ESP_GATT_OK, NULL);
                }
            } else if (param->write.handle == tx_cccd_handle) {
                // CCCD write - enable/disable notifications
                if (param->write.len == 2) {
                    uint16_t cccd_value = param->write.value[0] | (param->write.value[1] << 8);
                    tx_notify_enabled = (cccd_value == 0x0001);
                    ESP_LOGI(TAG, "TX notifications %s", tx_notify_enabled ? "enabled" : "disabled");
                }
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// BLE Initialization
// ============================================================================

static void ble_init(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Release classic BT memory
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // Initialize Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Register callbacks
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    // Set MTU
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));

    ESP_LOGI(TAG, "BLE initialized");
}

// ============================================================================
// Main Control Loop Task
// ============================================================================

static void control_loop_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));

        // Handle forward ramp
        if (ramp_forward_active) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t elapsed = now - ramp_start_ms;

            if (elapsed >= RAMP_DURATION_MS) {
                motor_set_pwm(RAMP_END_PWM, RAMP_END_PWM);
                ramp_forward_active = false;
                forward_latched = true;
                ESP_LOGI(TAG, "Ramp complete, latched at max");
            } else {
                uint16_t pwm_now = RAMP_START_PWM +
                    (uint32_t)(RAMP_END_PWM - RAMP_START_PWM) * elapsed / RAMP_DURATION_MS;
                motor_set_pwm(pwm_now, pwm_now);
            }
        }

        // Handle inactivity timeout
        if (inactivity_timer > 0) {
            inactivity_timer--;
        } else if (timer_active) {
            timer_active = false;
            ramp_forward_active = false;
            forward_latched = false;
            motor_set_pwm(0, 0);
            motor_set_direction(false, false);
            ESP_LOGI(TAG, "Inactivity timeout - motors stopped");
        }
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "Zobo ESP32 Robot Controller Starting...");

    // Initialize hardware
    led_init();
    motor_init();

    // Run LED startup sequence
    led_startup_sequence();

    // Initialize BLE
    ble_init();

    ESP_LOGI(TAG, "BLE ready, waiting for connection...");

    // Start control loop task
    xTaskCreate(control_loop_task, "control_loop", 4096, NULL, 5, NULL);
}
