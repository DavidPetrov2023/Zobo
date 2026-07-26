/**
 * MQTT Control - low latency channel for driving and LED
 */

#include "mqtt_control.h"
#include "mqtt_config.h"
#include "led.h"
#include "motor.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "MQTT_CTRL";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_device_id[32] = {0};
static char s_topic_cmd[64] = {0};
static char s_topic_state[64] = {0};

// Timestamp of the last drive command, used by the deadman check.
static int64_t s_last_drive_us = 0;
static bool s_driving = false;

bool mqtt_control_is_connected(void) { return s_connected; }

/* ------------------------------------------------------------------ */
/*  Driving                                                            */
/* ------------------------------------------------------------------ */

// Differential drive: throttle and steer both range -1..1. Steering to the
// right speeds up the left track and slows the right one, so the robot can also
// spin in place when throttle is zero.
static void apply_drive(double throttle, double steer)
{
    if (throttle > 1) throttle = 1;
    if (throttle < -1) throttle = -1;
    if (steer > 1) steer = 1;
    if (steer < -1) steer = -1;

    double left = throttle + steer;
    double right = throttle - steer;
    if (left > 1) left = 1;
    if (left < -1) left = -1;
    if (right > 1) right = 1;
    if (right < -1) right = -1;

    if (left == 0 && right == 0) {
        motor_stop();
        s_driving = false;
        return;
    }

    // A forward ramp started over BLE would keep rewriting the duty from the
    // main loop, so it has to go before we set our own.
    motor_cancel_ramp();

    // The direction pin LOW means forward, and in that direction the duty is
    // simply the speed. With the pin HIGH the driver inverts the duty, so full
    // reverse is duty 0 and duty 255 stands still - which is why the reverse
    // branch complements the value instead of reusing it. Getting this backwards
    // made "forward" do nothing at all.
    bool left_fwd = (left >= 0);
    bool right_fwd = (right >= 0);
    motor_set_direction(!left_fwd, !right_fwd);

    // Below roughly a third of full duty the gearbox will not move at all, so
    // the usable range is compressed into 90..255 instead of 0..255.
    double al = left < 0 ? -left : left;
    double ar = right < 0 ? -right : right;
    uint8_t pl = (uint8_t)(al > 0 ? 90 + al * 165 : 0);
    uint8_t pr = (uint8_t)(ar > 0 ? 90 + ar * 165 : 0);
    if (!left_fwd) pl = 255 - pl;
    if (!right_fwd) pr = 255 - pr;
    motor_set_pwm(pl, pr);
    s_driving = true;
}

// Runs from a task; stops the motors when drive commands stop arriving. This is
// what makes a dropped connection safe.
static void deadman_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!s_driving) continue;
        int64_t age_ms = (esp_timer_get_time() - s_last_drive_us) / 1000;
        if (age_ms > MQTT_DEADMAN_MS) {
            ESP_LOGW(TAG, "No drive command for %lld ms - stopping", (long long)age_ms);
            motor_stop();
            s_driving = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Incoming commands                                                  */
/* ------------------------------------------------------------------ */

static void handle_command(const char *data, int len)
{
    // The payload is not null terminated, so parse a bounded copy.
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, data, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "Cannot parse command JSON");
        return;
    }

    cJSON *led = cJSON_GetObjectItem(root, "led");
    if (cJSON_IsObject(led)) {
        // cJSON's Is* helpers are NULL safe, so a missing channel means off.
        bool r = cJSON_IsTrue(cJSON_GetObjectItem(led, "r"));
        bool g = cJSON_IsTrue(cJSON_GetObjectItem(led, "g"));
        bool b = cJSON_IsTrue(cJSON_GetObjectItem(led, "b"));
        ESP_LOGI(TAG, "LED via MQTT: R=%d G=%d B=%d", r, g, b);
        led_set_user_color(r, g, b);
    }

    cJSON *t = cJSON_GetObjectItem(root, "t");
    cJSON *st = cJSON_GetObjectItem(root, "s");
    if (cJSON_IsNumber(t) || cJSON_IsNumber(st)) {
        double throttle = cJSON_IsNumber(t) ? t->valuedouble : 0;
        double steer = cJSON_IsNumber(st) ? st->valuedouble : 0;
        s_last_drive_us = esp_timer_get_time();
        apply_drive(throttle, steer);
    }

    cJSON_Delete(root);
}

static void publish_state(void)
{
    if (!s_connected || !s_client) return;
    char msg[160];
    snprintf(msg, sizeof(msg),
             "{\"id\":\"%s\",\"up\":%lld,\"driving\":%s,\"heap\":%u}",
             s_device_id,
             (long long)(esp_timer_get_time() / 1000000),
             s_driving ? "true" : "false",
             (unsigned)esp_get_free_heap_size());
    esp_mqtt_client_publish(s_client, s_topic_state, msg, 0, 0, 0);
}

static void state_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        publish_state();
    }
}

/* ------------------------------------------------------------------ */
/*  Events                                                             */
/* ------------------------------------------------------------------ */

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Connected to broker, subscribing %s", s_topic_cmd);
            esp_mqtt_client_subscribe(s_client, s_topic_cmd, 0);
            // WiFi power save buffers packets until the next DTIM beacon, which
            // adds 100-300 ms of jitter. That is unacceptable for driving, so it
            // is disabled while the control channel is up.
            esp_wifi_set_ps(WIFI_PS_NONE);
            publish_state();
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Disconnected from broker - stopping motors");
            // Never keep driving with no one at the controls.
            motor_stop();
            s_driving = false;
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);   // back to the power saving default
            break;

        case MQTT_EVENT_DATA:
            handle_command(event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

esp_err_t mqtt_control_start(const char *device_id)
{
    if (s_client) return ESP_OK;   // already running
    if (!device_id || !device_id[0]) return ESP_ERR_INVALID_ARG;

    strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), "zobo/%s/cmd", s_device_id);
    snprintf(s_topic_state, sizeof(s_topic_state), "zobo/%s/state", s_device_id);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.keepalive = 30,
        // The broker sits behind the website's certificate, so the standard
        // root bundle validates it - no certificate needs embedding.
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .network.reconnect_timeout_ms = 4000,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Client init failed");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Client start failed: %s", esp_err_to_name(err));
        return err;
    }

    xTaskCreate(deadman_task, "mqtt_deadman", 2048, NULL, 6, NULL);
    xTaskCreate(state_task, "mqtt_state", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "Control channel starting for %s", s_device_id);
    return ESP_OK;
}

void mqtt_control_stop(void)
{
    if (!s_client) return;
    motor_stop();
    s_driving = false;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
}
