/**
 * WiFi Manager Module
 */

#include "wifi_manager.h"
#include "led.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"

static const char *TAG = "WIFI";

// NVS namespace and keys
#define NVS_NAMESPACE       "wifi_creds"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASSWORD    "password"

// Event group bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// State
static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static char s_ip_addr[16] = "";
static char s_ssid[33] = "";
static char s_password[65] = "";
static bool s_initialized = false;
static int s_retry_count = 0;
#define MAX_RETRY 5

// Event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected! Reason: %d", disconn->reason);
        // Common reasons: 2=AUTH_EXPIRE, 15=4WAY_HANDSHAKE_TIMEOUT, 201=NO_AP_FOUND, 202=AUTH_FAIL
        if (disconn->reason == 201) {
            ESP_LOGE(TAG, "Reason 201: AP not found - check SSID or signal strength");
        } else if (disconn->reason == 202 || disconn->reason == 15) {
            ESP_LOGE(TAG, "Reason %d: Authentication failed - check password", disconn->reason);
        }

        if (s_retry_count < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying connection... (%d/%d)", s_retry_count, MAX_RETRY);
        } else {
            s_wifi_status = WIFI_STATUS_FAILED;
            if (s_wifi_event_group) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
        s_ip_addr[0] = '\0';
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected! IP: %s", s_ip_addr);
        s_retry_count = 0;
        s_wifi_status = WIFI_STATUS_CONNECTED;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        led_indicate_wifi_connected();
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Create event group
    s_wifi_event_group = xEventGroupCreate();

    // Load credentials from NVS
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t ssid_len = sizeof(s_ssid);
        size_t pass_len = sizeof(s_password);

        if (nvs_get_str(nvs_handle, NVS_KEY_SSID, s_ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_password, &pass_len) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded WiFi credentials for SSID: %s", s_ssid);
        }
        nvs_close(nvs_handle);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return ret;
    }

    ret = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password ? password : "");
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        strncpy(s_password, password ? password : "", sizeof(s_password) - 1);
        ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", s_ssid);
    }

    return ret;
}

bool wifi_manager_has_credentials(void)
{
    return strlen(s_ssid) > 0;
}

esp_err_t wifi_manager_connect(void)
{
    if (!s_initialized) {
        wifi_manager_init();
    }

    if (!wifi_manager_has_credentials()) {
        ESP_LOGW(TAG, "No WiFi credentials stored");
        return ESP_ERR_NOT_FOUND;
    }

    s_wifi_status = WIFI_STATUS_CONNECTING;
    s_retry_count = 0;
    led_indicate_wifi_connecting();

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password));

    // Auto-detect authentication mode
    if (strlen(s_password) == 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }

    // Enable scan for all channels (helps with coexistence)
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to %s...", s_ssid);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to %s", s_ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Connection timeout");
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_wifi_status = WIFI_STATUS_DISCONNECTED;
    s_ip_addr[0] = '\0';
    ESP_LOGI(TAG, "Disconnected");
    return ESP_OK;
}

wifi_status_t wifi_manager_get_status(void)
{
    return s_wifi_status;
}

const char* wifi_manager_get_ip(void)
{
    return s_ip_addr;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    s_ssid[0] = '\0';
    s_password[0] = '\0';
    ESP_LOGI(TAG, "Credentials cleared");
    return ret;
}
