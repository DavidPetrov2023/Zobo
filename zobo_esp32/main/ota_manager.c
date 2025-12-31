/**
 * OTA Update Manager
 */

#include "ota_manager.h"
#include "led.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

static const char *TAG = "OTA";

static ota_status_callback_t s_callback = NULL;
static bool s_ota_in_progress = false;

// OTA task parameters
typedef struct {
    char url[256];
} ota_task_params_t;

static void notify_status(int progress, const char *status)
{
    ESP_LOGI(TAG, "OTA: %s (%d%%)", status, progress);
    if (s_callback) {
        s_callback(progress, status);
    }
}

static void ota_task(void *pvParameter)
{
    ota_task_params_t *params = (ota_task_params_t *)pvParameter;

    notify_status(0, "Starting OTA update");
    led_indicate_ota_progress();

    esp_http_client_config_t config = {
        .url = params->url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = false,
        .max_http_request_size = 0,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        notify_status(-1, "OTA begin failed");
        led_indicate_ota_fail();
        goto cleanup;
    }

    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    int downloaded = 0;
    int last_progress = 0;

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        downloaded = esp_https_ota_get_image_len_read(https_ota_handle);
        int progress = (image_size > 0) ? (downloaded * 100 / image_size) : 0;

        if (progress != last_progress && progress % 10 == 0) {
            char status[32];
            snprintf(status, sizeof(status), "Downloading: %d%%", progress);
            notify_status(progress, status);
            last_progress = progress;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        notify_status(-1, "Download failed");
        led_indicate_ota_fail();
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Incomplete data received");
        notify_status(-1, "Incomplete download");
        led_indicate_ota_fail();
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }

    err = esp_https_ota_finish(https_ota_handle);
    if (err == ESP_OK) {
        notify_status(100, "Update complete, restarting...");
        led_indicate_ota_success();
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        notify_status(-1, "Update failed");
        led_indicate_ota_fail();
    }

cleanup:
    free(params);
    s_ota_in_progress = false;
    vTaskDelete(NULL);
}

esp_err_t ota_manager_init(void)
{
    ESP_LOGI(TAG, "OTA manager initialized, firmware version: %s", FIRMWARE_VERSION);
    return ESP_OK;
}

void ota_manager_set_callback(ota_status_callback_t callback)
{
    s_callback = callback;
}

esp_err_t ota_manager_start_update(const char *url)
{
    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (!url || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ota_task_params_t *params = malloc(sizeof(ota_task_params_t));
    if (!params) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(params->url, url, sizeof(params->url) - 1);
    params->url[sizeof(params->url) - 1] = '\0';

    s_ota_in_progress = true;

    if (xTaskCreate(ota_task, "ota_task", 8192, params, 5, NULL) != pdPASS) {
        free(params);
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

const char* ota_manager_get_version(void)
{
    return FIRMWARE_VERSION;
}

esp_err_t ota_manager_check_update(const char *version_url, bool *update_available)
{
    *update_available = false;

    esp_http_client_config_t config = {
        .url = version_url,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0 || content_length > 32) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char version_buf[33] = {0};
    int read_len = esp_http_client_read(client, version_buf, sizeof(version_buf) - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        return ESP_FAIL;
    }

    // Trim whitespace
    char *end = version_buf + strlen(version_buf) - 1;
    while (end > version_buf && (*end == '\n' || *end == '\r' || *end == ' ')) {
        *end-- = '\0';
    }

    ESP_LOGI(TAG, "Current: %s, Remote: %s", FIRMWARE_VERSION, version_buf);

    // Simple version comparison (assumes semver format)
    if (strcmp(version_buf, FIRMWARE_VERSION) > 0) {
        *update_available = true;
    }

    return ESP_OK;
}
