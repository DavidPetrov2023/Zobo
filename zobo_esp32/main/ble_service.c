/**
 * BLE UART Service
 */

#include "ble_service.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

static const char *TAG = "BLE";

// Nordic UART Service UUIDs
static uint8_t service_uuid[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
};

static uint8_t char_rx_uuid[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
};

static uint8_t char_tx_uuid[16] = {
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
};

// State variables
static uint16_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id = 0;
static bool s_connected = false;
static bool s_notify_enabled = false;
static ble_command_callback_t s_command_callback = NULL;

// GATT handles
enum {
    IDX_SVC,
    IDX_CHAR_TX,
    IDX_CHAR_TX_VAL,
    IDX_CHAR_TX_CFG,
    IDX_CHAR_RX,
    IDX_CHAR_RX_VAL,
    IDX_NB,
};

static uint16_t s_handle_table[IDX_NB];

// Advertising parameters
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t s_adv_data[] = {
    0x02, 0x01, 0x06,
    0x05, 0x09, 'Z', 'o', 'b', 'o',
    0x11, 0x07,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
};

// GATT database
static const esp_gatts_attr_db_t s_gatt_db[IDX_NB] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_PRI_SERVICE},
         ESP_GATT_PERM_READ, sizeof(service_uuid), sizeof(service_uuid), service_uuid}
    },
    [IDX_CHAR_TX] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE},
         ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_NOTIFY}}
    },
    [IDX_CHAR_TX_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_tx_uuid, 0, 500, 0, NULL}
    },
    [IDX_CHAR_TX_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_CLIENT_CONFIG},
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 0, NULL}
    },
    [IDX_CHAR_RX] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE},
         ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR}}
    },
    [IDX_CHAR_RX_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, char_rx_uuid, ESP_GATT_PERM_WRITE, 500, 0, NULL}
    },
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&s_adv_params);
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

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATT server registered");
            s_gatts_if = gatts_if;
            esp_ble_gap_config_adv_data_raw(s_adv_data, sizeof(s_adv_data));
            esp_ble_gatts_create_attr_tab(s_gatt_db, gatts_if, IDX_NB, 0);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                memcpy(s_handle_table, param->add_attr_tab.handles, sizeof(s_handle_table));
                esp_ble_gatts_start_service(s_handle_table[IDX_SVC]);
                ESP_LOGI(TAG, "Service started");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Device connected");
            s_connected = true;
            s_conn_id = param->connect.conn_id;
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Device disconnected");
            s_connected = false;
            s_notify_enabled = false;
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == s_handle_table[IDX_CHAR_RX_VAL]) {
                if (s_command_callback) {
                    s_command_callback(param->write.value, param->write.len);
                }
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                               param->write.trans_id, ESP_GATT_OK, NULL);
                }
            } else if (param->write.handle == s_handle_table[IDX_CHAR_TX_CFG]) {
                if (param->write.len == 2) {
                    uint16_t cccd = param->write.value[0] | (param->write.value[1] << 8);
                    s_notify_enabled = (cccd == 0x0001);
                    ESP_LOGI(TAG, "Notifications %s", s_notify_enabled ? "enabled" : "disabled");
                }
            }
            break;

        default:
            break;
    }
}

esp_err_t ble_service_init(void)
{
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
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));

    ESP_LOGI(TAG, "BLE service initialized");
    return ESP_OK;
}

void ble_service_set_callback(ble_command_callback_t callback)
{
    s_command_callback = callback;
}

void ble_service_send(const char *data)
{
    if (s_connected && s_notify_enabled && s_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                    s_handle_table[IDX_CHAR_TX_VAL],
                                    strlen(data), (uint8_t *)data, false);
    }
}

bool ble_service_is_connected(void)
{
    return s_connected;
}

void ble_service_pause(void)
{
    ESP_LOGI(TAG, "Pausing BLE advertising...");
    esp_ble_gap_stop_advertising();
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "BLE advertising stopped");
}

void ble_service_resume(void)
{
    ESP_LOGI(TAG, "Resuming BLE advertising...");
    esp_ble_gap_start_advertising(&s_adv_params);
    ESP_LOGI(TAG, "BLE advertising resumed");
}
