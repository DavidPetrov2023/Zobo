#include "mqtt_cam.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

#include "mqtt_config.h"
#include "ota_cam.h"

static const char *TAG = "CAM_MQTT";

#define TOPIC_FRAME "zobo/cam/frame"
#define TOPIC_STATE "zobo/cam/state"
#define TOPIC_CTRL  "zobo/cam/ctrl"

// Jak dlouho po posledni zprave od prohlizece se jeste vysila. Prohlizec hlasi
// zajem kazde 3 s, takze jeden ztraceny paket obraz neprerusi.
#define VIEWER_TIMEOUT_MS 9000

// Cil je plynulost, ne filmova kvalita. Pri VGA a peti snimcich za sekundu byl
// obraz na rizeni trhany, pritom senzor sam zvladne 24 sn/s v QVGA - strop byl
// tady, ne v kamere. Deset snimku v QVGA vyjde na ~70 kB/s, tedy min dat, nez
// stalo pet snimku ve VGA (~85 kB/s), takze linka serveru na tom vydela taky.
#define FRAME_PERIOD_MS 100

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static volatile int64_t s_last_viewer_us = 0;
static volatile uint32_t s_sent = 0;
static char s_id[24] = "ZoboCam";

bool mqtt_cam_has_viewer(void)
{
    if (s_last_viewer_us == 0) return false;
    return (esp_timer_get_time() - s_last_viewer_us) / 1000 < VIEWER_TIMEOUT_MS;
}

static void publish_state(void)
{
    if (!s_connected) return;
    char msg[224];
    int n = snprintf(msg, sizeof(msg),
                     "{\"id\":\"%s\",\"up\":%lld,\"watching\":%s,\"sent\":%u,"
                     "\"fw\":\"%s\",\"ota\":%s,\"trial\":%s}",
                     s_id,
                     (long long)(esp_timer_get_time() / 1000000),
                     mqtt_cam_has_viewer() ? "true" : "false",
                     (unsigned)s_sent,
                     CAM_FW_VERSION,
                     ota_cam_busy() ? "\"running\"" : "false",
                     // Dokud je tohle true, novy obraz jeste neprosel zkouskou a
                     // muze se sam vratit k predchozimu - na dalku je to jediny
                     // rozdil mezi "chytlo se to" a "za chvili to bude zpatky".
                     ota_cam_pending() ? "true" : "false");
    esp_mqtt_client_publish(s_client, TOPIC_STATE, msg, n, 0, 0);
}

// Snimky jsou binarni a velke, takze QoS 0 a bez uchovani: stary snimek nema
// cenu dorucovat, za chvili je stejne dalsi.
static void frame_task(void *arg)
{
    (void)arg;
    int64_t last_state_us = 0;

    for (;;) {
        if (!s_connected || !mqtt_cam_has_viewer()) {
            // Nikdo se nediva - kamera si odpocine a linka je volna.
            vTaskDelay(pdMS_TO_TICKS(500));
            if (s_connected && esp_timer_get_time() - last_state_us > 5000000) {
                last_state_us = esp_timer_get_time();
                publish_state();
            }
            continue;
        }

        int64_t t0 = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            int id = esp_mqtt_client_publish(s_client, TOPIC_FRAME, (const char *)fb->buf, fb->len, 0, 0);
            if (id >= 0) s_sent++;
            esp_camera_fb_return(fb);
        }

        if (esp_timer_get_time() - last_state_us > 3000000) {
            last_state_us = esp_timer_get_time();
            publish_state();
        }

        int64_t spent_ms = (esp_timer_get_time() - t0) / 1000;
        int64_t wait = FRAME_PERIOD_MS - spent_ms;
        vTaskDelay(pdMS_TO_TICKS(wait > 10 ? wait : 10));
    }
}

// Vytahne hodnotu retezcoveho klice z ploche JSON zpravy. Kvuli jednomu klici
// se nevyplati tahat sem parser - zpravy si posilame sami a jsou triviální.
static bool json_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);

    const char *end = strchr(p, '"');
    if (!end || (size_t)(end - p) >= out_len) return false;

    memcpy(out, p, end - p);
    out[end - p] = 0;
    return true;
}

static void handle_ctrl(const char *data, int len)
{
    // Staci, ze zprava prisla - je to tep divaka. Obsah se cte jen kvuli
    // vypnuti ("watch":false) a povelu k aktualizaci.
    // Data z MQTT nekonci nulou, tak si udelame vlastni kopii. Buffer musi
    // pojmout celou URL firmwaru, ne jen kratky povel.
    char buf[320];
    int n = len;
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    memcpy(buf, data, n);
    buf[n] = 0;

    char url[256];
    if (json_string(buf, "ota", url, sizeof(url))) {
        ESP_LOGW(TAG, "Update requested: %s", url);
        esp_err_t err = ota_cam_start(url);
        if (err != ESP_OK) ESP_LOGE(TAG, "Update refused: %s", esp_err_to_name(err));
        return;
    }

    if (strstr(buf, "\"watch\":false")) {
        s_last_viewer_us = 0;
        ESP_LOGI(TAG, "Viewer left");
        return;
    }
    if (!mqtt_cam_has_viewer()) ESP_LOGI(TAG, "Viewer arrived");
    s_last_viewer_us = esp_timer_get_time();
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Broker connected, publishing to %s", TOPIC_FRAME);
            esp_mqtt_client_subscribe(s_client, TOPIC_CTRL, 0);
            // Az tady je nova verze prokazatelne na siti a dosazitelna povelem.
            // Driv ji za dobrou prohlasit nelze - prave tohle je ta schopnost,
            // o kterou pri spatnem buildu jde.
            ota_cam_confirm();
            publish_state();
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            s_last_viewer_us = 0;
            ESP_LOGW(TAG, "Broker lost");
            break;

        case MQTT_EVENT_DATA:
            handle_ctrl(event->data, event->data_len);
            break;

        default:
            break;
    }
}

esp_err_t mqtt_cam_start(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_id, sizeof(s_id), "ZoboCam-%02X%02X%02X", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.keepalive = 30,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .network.reconnect_timeout_ms = 4000,
        // Jeden snimek ma desitky kilobajtu, takze vychozi buffery nestaci.
        .buffer.size = 4096,
        .buffer.out_size = 40960,
        .task.stack_size = 6144,
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

    xTaskCreate(frame_task, "cam_frames", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Camera id %s, streaming only while somebody watches", s_id);
    return ESP_OK;
}
