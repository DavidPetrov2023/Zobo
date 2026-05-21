/**
 * Power Mode — NVS-backed persistent state.
 */

#include "power_mode.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "POWER";
static const char *NVS_NAMESPACE = "powermode";
static const char *NVS_KEY = "mode";

power_mode_t power_mode_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // No namespace yet — fresh flash or first run. Default to ACTIVE.
        return POWER_MODE_ACTIVE;
    }

    uint8_t v = 0;
    err = nvs_get_u8(h, NVS_KEY, &v);
    nvs_close(h);

    if (err != ESP_OK) {
        return POWER_MODE_ACTIVE;
    }
    return (v == (uint8_t)POWER_MODE_SLEEP) ? POWER_MODE_SLEEP : POWER_MODE_ACTIVE;
}

esp_err_t power_mode_save(power_mode_t mode)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(h, NVS_KEY, (uint8_t)mode);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved power mode: %s", power_mode_str(mode));
    } else {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    }
    return err;
}

const char *power_mode_str(power_mode_t mode)
{
    return (mode == POWER_MODE_SLEEP) ? "SLEEP" : "ACTIVE";
}
