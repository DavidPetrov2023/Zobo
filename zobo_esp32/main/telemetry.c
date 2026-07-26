/**
 * Telemetry Module - periodic device status POST.
 *
 * Endpoint URL embeds the device token (one robot = one token).
 * For multi-device deployments, store URL in NVS and set via BLE.
 *
 * The server may return a command in the response body, e.g.:
 *   {"command": {"action": "ota_update", "url": "...", "version": "1.3.0"}}
 * which is dispatched to the OTA manager from here.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "esp_sleep.h"
#include "esp_system.h"

#include "telemetry.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "led.h"
#include "power_mode.h"

#define SLEEP_CYCLE_S         (10 * 60)  // deep sleep duration when in SLEEP mode

static const char *TAG = "TELEMETRY";

#define TELEMETRY_URL        "https://petrovelektronika.cz/devices/api.php?token=671111d43f8bdf1c181d668e84eca6db"
#define TELEMETRY_INTERVAL_S 5
#define DEVICE_NAME          "Zobo Robot"
#define RESPONSE_BUF_SIZE    1024

static char s_device_id[20] = {0};
static TaskHandle_t s_telemetry_task = NULL;
// Set when ota_manager_start_update() is dispatched from a sleep-cycle wake,
// so telemetry_sleep_cycle() knows NOT to deep-sleep — that would kill the
// OTA download mid-flight. OTA task reboots the device on completion.
static volatile bool s_ota_dispatched = false;

typedef struct {
    char buf[RESPONSE_BUF_SIZE];
    size_t len;
} response_ctx_t;

// Called from the system event loop when DHCP completes. Kicks the telemetry
// task so the first POST happens within ~1 s of WiFi being usable, instead of
// waiting for the next 15 s tick.
static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_telemetry_task) {
        ESP_LOGI(TAG, "WiFi got IP - waking telemetry task");
        xTaskNotifyGive(s_telemetry_task);
    }
}

static void build_device_id(void)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_device_id, sizeof(s_device_id),
                 "Zobo-%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        snprintf(s_device_id, sizeof(s_device_id), "Zobo-unknown");
    }
}

// Slow oscillating room temperature simulation around ~22 C, rounded to 1 decimal.
static float fake_temperature(int64_t uptime_s)
{
    float t = 22.0f + 1.8f * sinf((float)uptime_s / 600.0f);
    return roundf(t * 10.0f) / 10.0f;
}

static char *build_payload(void)
{
    int64_t uptime_s = esp_timer_get_time() / 1000000;
    float temp = fake_temperature(uptime_s);

    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f", temp);

    cJSON *root = cJSON_CreateObject();

    cJSON *device_info = cJSON_AddObjectToObject(root, "DeviceInfo");
    cJSON_AddStringToObject(device_info, "ID", s_device_id);
    cJSON_AddStringToObject(device_info, "Firmware", ota_manager_get_version());

    cJSON *live = cJSON_AddObjectToObject(root, "LiveStatus");
    cJSON_AddNumberToObject(live, "Temperature", temp);
    cJSON_AddNumberToObject(live, "Uptime", (double)uptime_s);
    cJSON_AddNumberToObject(live, "ErrorCode", 0);
    cJSON_AddStringToObject(live, "EventState", "idle");
    cJSON_AddStringToObject(live, "PowerMode", power_mode_str(power_mode_load()));

    cJSON *product = cJSON_AddObjectToObject(root, "ProductInfo");
    // Field name matches the auto-detect key in devices/api.php
    cJSON_AddStringToObject(product, "Název zařízení", DEVICE_NAME);

    cJSON *sensors = cJSON_AddArrayToObject(product, "Sensors");
    cJSON *sensor = cJSON_CreateObject();
    cJSON_AddStringToObject(sensor, "Name", "Teplota mistnosti");
    cJSON_AddStringToObject(sensor, "Value", temp_str);
    cJSON_AddStringToObject(sensor, "Unit", "°C");
    cJSON_AddStringToObject(sensor, "Type", "fake");
    cJSON_AddItemToArray(sensors, sensor);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    response_ctx_t *ctx = (response_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx && evt->data_len > 0) {
        size_t avail = sizeof(ctx->buf) - 1 - ctx->len;
        if (avail > 0) {
            size_t to_copy = evt->data_len < (int)avail ? evt->data_len : avail;
            memcpy(ctx->buf + ctx->len, evt->data, to_copy);
            ctx->len += to_copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static void handle_server_command(const char *response_body)
{
    if (!response_body || response_body[0] == '\0') return;

    cJSON *root = cJSON_Parse(response_body);
    if (!root) {
        ESP_LOGW(TAG, "Cannot parse response JSON");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (cmd && cJSON_IsObject(cmd)) {
        const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "action"));
        if (action && strcmp(action, "ota_update") == 0) {
            const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "url"));
            const char *version = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "version"));
            if (url) {
                ESP_LOGI(TAG, "Server-triggered OTA: version=%s, url=%s",
                         version ? version : "?", url);
                esp_err_t r = ota_manager_start_update(url);
                if (r == ESP_OK) {
                    s_ota_dispatched = true;
                } else {
                    ESP_LOGE(TAG, "OTA start failed: %s", esp_err_to_name(r));
                }
            }
        } else if (action && strcmp(action, "wake") == 0) {
            if (power_mode_load() == POWER_MODE_ACTIVE) {
                ESP_LOGI(TAG, "wake received but already ACTIVE — no-op");
            } else {
                ESP_LOGI(TAG, "Server-triggered wake: switching to ACTIVE mode");
                cJSON_Delete(root);
                power_mode_save(POWER_MODE_ACTIVE);
                vTaskDelay(pdMS_TO_TICKS(200));  // flush logs
                esp_restart();
                // never returns
            }
        } else if (action && strcmp(action, "sleep") == 0) {
            // Persist SLEEP mode and enter deep sleep immediately. Safe to call
            // even if already SLEEP — the next boot will just go through the
            // sleep cycle once more.
            ESP_LOGI(TAG, "Server-triggered sleep: entering %ds deep sleep", SLEEP_CYCLE_S);
            cJSON_Delete(root);
            power_mode_save(POWER_MODE_SLEEP);
            led_set_rgb(false, false, false);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_CYCLE_S * 1000000ULL);
            esp_deep_sleep_start();
            // never returns
        } else if (action && strcmp(action, "set_led") == 0) {
            // The server sends the three channels separately. LEDs hang off
            // plain GPIO with no PWM, so each channel is only on or off and the
            // eight combinations are everything the hardware can do.
            bool r = cJSON_IsTrue(cJSON_GetObjectItem(cmd, "r"));
            bool g = cJSON_IsTrue(cJSON_GetObjectItem(cmd, "g"));
            bool b = cJSON_IsTrue(cJSON_GetObjectItem(cmd, "b"));
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "color"));
            ESP_LOGI(TAG, "Server-triggered LED colour: %s (R=%d G=%d B=%d)",
                     name ? name : "?", r, g, b);
            led_set_user_color(r, g, b);
        } else {
            ESP_LOGW(TAG, "Unknown command action: %s", action ? action : "(null)");
        }
    }

    cJSON_Delete(root);
}

static esp_err_t post_telemetry(void)
{
    char *body = build_payload();
    if (!body) return ESP_FAIL;

    response_ctx_t resp = {0};

    esp_http_client_config_t cfg = {
        .url = TELEMETRY_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .event_handler = http_event_handler,
        .user_data = &resp,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "POST ok (HTTP %d, resp=%dB)", status, (int)resp.len);
        led_telemetry_pulse();
        handle_server_command(resp.buf);
    } else {
        ESP_LOGW(TAG, "POST failed: err=%s, status=%d", esp_err_to_name(err), status);
        err = ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    free(body);
    return err;
}

static void telemetry_task(void *arg)
{
    uint32_t tick = 0;
    while (1) {
        // Wait up to the interval, OR until on_got_ip() notifies us.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TELEMETRY_INTERVAL_S * 1000));

        tick++;
        wifi_status_t wstatus = wifi_manager_get_status();
        if (wstatus == WIFI_STATUS_CONNECTED) {
            ESP_LOGI(TAG, "tick #%lu — posting", (unsigned long)tick);
            post_telemetry();
        } else {
            ESP_LOGW(TAG, "tick #%lu — skip, WiFi status=%d", (unsigned long)tick, wstatus);
        }
    }
}

esp_err_t telemetry_init(void)
{
    build_device_id();
    ESP_LOGI(TAG, "Telemetry init, device_id=%s, interval=%ds",
             s_device_id, TELEMETRY_INTERVAL_S);

    BaseType_t ok = xTaskCreate(telemetry_task, "telemetry", 8192, NULL, 3, &s_telemetry_task);
    if (ok != pdPASS) return ESP_FAIL;

    // Wake the task instantly whenever WiFi (re)connects and gets an IP.
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL);

    // If WiFi was already up before we registered (e.g. auto-connect from NVS
    // beat us to it), kick the first POST right away.
    if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) {
        xTaskNotifyGive(s_telemetry_task);
    }
    return ESP_OK;
}

void telemetry_sleep_cycle(void)
{
    build_device_id();
    ESP_LOGI(TAG, "SLEEP cycle wake: %s · checking for queued commands", s_device_id);

    // Brief magenta blink so a watching engineer can tell the wake is happening.
    led_set_rgb(true, false, true);
    vTaskDelay(pdMS_TO_TICKS(60));
    led_set_rgb(false, false, false);

    // wifi_manager_init() only loads creds — actually initiate the connection.
    // Without this the sleep cycle would sit at WIFI_DISCONNECTED until BLE
    // pushes credentials, which never happens (BLE is off in SLEEP mode).
    if (wifi_manager_has_credentials()) {
        wifi_manager_connect();
    } else {
        ESP_LOGW(TAG, "SLEEP cycle: no saved WiFi credentials");
    }

    // Wait up to 15 s for WiFi association + DHCP.
    for (int i = 0; i < 30; i++) {
        if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) {
        // post_telemetry → handle_server_command may esp_restart() (wake) or
        // esp_deep_sleep_start() (explicit sleep). If we return, no transition
        // happened and we fall through to the default 10-min sleep below.
        post_telemetry();
    } else {
        ESP_LOGW(TAG, "SLEEP cycle: WiFi never came up, sleeping anyway");
    }

    // If the just-received command kicked off an OTA, do NOT deep-sleep —
    // that would tear down the WiFi/HTTP stack mid-download and brick the
    // update. OTA task will reboot the device when finished. Add a safety
    // timeout so a stalled OTA can't keep the radio on forever (max ~5 min).
    if (s_ota_dispatched) {
        ESP_LOGI(TAG, "OTA in progress — staying awake instead of deep-sleeping");
        const int safety_timeout_s = 300;
        for (int i = 0; i < safety_timeout_s; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        ESP_LOGW(TAG, "OTA safety timeout reached, falling back to deep sleep");
    }

    ESP_LOGI(TAG, "Sleeping %d seconds", SLEEP_CYCLE_S);
    led_set_rgb(false, false, false);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_CYCLE_S * 1000000ULL);
    esp_deep_sleep_start();
    // never returns
}
