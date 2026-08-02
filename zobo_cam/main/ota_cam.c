#include "ota_cam.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"

static const char *TAG = "CAM_OTA";

static volatile bool s_busy = false;
static volatile bool s_pending = false;

bool ota_cam_busy(void)    { return s_busy; }
bool ota_cam_pending(void) { return s_pending; }

void ota_cam_confirm(void)
{
    if (!s_pending) return;
    s_pending = false;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "New image confirmed from the network: %s", esp_err_to_name(err));
}

// Novy obraz dostane jednu sanci dostat se na sit. Kdyz ji promarni, restart -
// a protoze se jeste nepotvrdil, zavadec nabootuje ten predchozi.
static void confirm_watchdog(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(OTA_CONFIRM_TIMEOUT_MS));

    if (s_pending) {
        ESP_LOGE(TAG, "New image never reached the broker in %d s - rolling back",
                 OTA_CONFIRM_TIMEOUT_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(200));   // at' log jeste odejde po seriove lince
        esp_restart();
    }
    vTaskDelete(NULL);
}

esp_err_t ota_cam_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending = true;
        ESP_LOGW(TAG, "Running a fresh image on trial from %s - waiting for the broker",
                 running->label);
        xTaskCreate(confirm_watchdog, "ota_confirm", 2048, NULL, 4, NULL);
    } else {
        ESP_LOGI(TAG, "Firmware %s running from %s", CAM_FW_VERSION,
                 running ? running->label : "?");
    }
    return ESP_OK;
}

void ota_cam_stop_camera(void)
{
    esp_err_t err = esp_camera_deinit();
    ESP_LOGW(TAG, "Camera stopped for the flash write: %s", esp_err_to_name(err));
    // At' dobehne rozdelany snimek driv, nez zacne mazani flash.
    vTaskDelay(pdMS_TO_TICKS(200));
}

typedef struct {
    char url[256];
} ota_params_t;

static void ota_task(void *arg)
{
    ota_params_t *p = (ota_params_t *)arg;

    ESP_LOGI(TAG, "Downloading %s", p->url);
    ota_cam_stop_camera();

    esp_http_client_config_t http = {
        .url = p->url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        // Na LAN se da poslat i plain http:// - certifikat by tam stejne nikdo
        // nemel, a kdyz je kamera na dosah, je to nejrychlejsi cesta.
        .crt_bundle_attach = strncmp(p->url, "https://", 8) == 0 ? esp_crt_bundle_attach : NULL,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .max_redirection_count = 5,
    };

    esp_https_ota_config_t cfg = { .http_config = &http };
    esp_https_ota_handle_t h = NULL;

    esp_err_t err = esp_https_ota_begin(&cfg, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Begin failed: %s", esp_err_to_name(err));
        goto done;
    }

    int total = esp_https_ota_get_image_size(h);
    int last = -1;
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int pct = total > 0 ? esp_https_ota_get_image_len_read(h) * 100 / total : 0;
        if (pct / 10 != last) {
            last = pct / 10;
            ESP_LOGI(TAG, "%d %%", pct);
        }
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(h);
        goto done;
    }

    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Finish failed: %s", esp_err_to_name(err));
        goto done;
    }

    ESP_LOGI(TAG, "Image written, restarting into it");
    free(p);
    s_busy = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

done:
    // Kamera uz je odstavena, takze i po neuspechu je jedina cesta zpatky k
    // obrazu restart. Bezici obraz se nemenil, nastartuje ten samy.
    ESP_LOGE(TAG, "Update failed, restarting to bring the camera back");
    free(p);
    s_busy = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

esp_err_t ota_cam_start(const char *url)
{
    if (!url || !*url) return ESP_ERR_INVALID_ARG;
    if (s_busy) {
        ESP_LOGW(TAG, "Update already running");
        return ESP_ERR_INVALID_STATE;
    }
    // Aktualizovat obraz, ktery sam jeste neprosel zkouskou, by zahodilo jedinou
    // funkcni zalohu ve druhem slotu.
    if (s_pending) {
        ESP_LOGW(TAG, "Running image is still on trial - not updating yet");
        return ESP_ERR_INVALID_STATE;
    }

    ota_params_t *p = calloc(1, sizeof(ota_params_t));
    if (!p) return ESP_ERR_NO_MEM;
    strncpy(p->url, url, sizeof(p->url) - 1);

    s_busy = true;
    if (xTaskCreate(ota_task, "cam_ota", 8192, p, 5, NULL) != pdPASS) {
        free(p);
        s_busy = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}
