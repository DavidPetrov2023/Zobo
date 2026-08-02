/**
 * Zobo camera - AI-Thinker ESP32-CAM
 *
 * First stage: bring the camera up and serve it on the local network as MJPEG,
 * so it can be pointed at and judged before any of it is wired to the robot or
 * to the website. Open http://<ip>/ and the picture is there.
 *
 * MJPEG means the board simply sends one JPEG after another in a single HTTP
 * response. No codec, no session setup, and every browser plays it inside a
 * plain <img>. It costs more bandwidth than a real video codec, which the ESP32
 * cannot encode anyway.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "esp_ota_ops.h"

#include "camera_pins.h"
#include "mqtt_cam.h"
#include "ota_cam.h"
#include "wifi_config.h"

static const char *TAG = "ZOBO_CAM";

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_events;
static int s_retries = 0;

// Rough throughput counters, reported by /status.
static volatile uint32_t s_frames_sent = 0;
static volatile uint32_t s_bytes_sent = 0;

/* ------------------------------------------------------------------ */
/*  WiFi                                                               */
/* ------------------------------------------------------------------ */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        // Keep retrying forever: the camera is meant to sit somewhere and work,
        // not to give up because the router rebooted.
        ESP_LOGW(TAG, "WiFi lost, reconnecting (attempt %d)", ++s_retries);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_retries = 0;
        ESP_LOGI(TAG, "Connected. Open http://" IPSTR "/ in a browser", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Streaming video through a radio that sleeps between beacons stutters, so
    // power save is off. This board is not battery powered.
    esp_wifi_set_ps(WIFI_PS_NONE);
}

/* ------------------------------------------------------------------ */
/*  Camera                                                             */
/* ------------------------------------------------------------------ */

static esp_err_t camera_start(void)
{
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        // 20 MHz je maximum, ktere senzor zvladne, ale na tomhle modulu se pri
        // nem objevuji vodorovne pruhy - hodiny jsou rychlejsi, nez staci
        // napajeni cistit. 10 MHz da o polovinu min snimku za sekundu a
        // podstatne cistsi obraz.
        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        // The sensor compresses to JPEG itself. Asking for raw pixels instead
        // would need more RAM per frame than the chip has.
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 12,          // 10 is best, 63 worst; 12 is a good trade
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,   // drop stale frames instead of queueing
    };

    // Without PSRAM there is room for one small frame at a time.
    if (!esp_psram_is_initialized()) {
        ESP_LOGW(TAG, "No PSRAM found - falling back to a smaller, slower picture");
        config.frame_size = FRAMESIZE_QVGA;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.jpeg_quality = 15;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // The OV2640 on these modules is mounted upside down on most robots;
        // flipping here is free, doing it in the browser is not.
        s->set_vflip(s, 1);
        s->set_hmirror(s, 1);

        // Factory settings assume bright daylight. Indoors that gives a dark,
        // washed out picture, so the automatics get room to work: exposure and
        // gain on, a high gain ceiling, and the night mode that lengthens
        // exposure when there is not enough light.
        s->set_gain_ctrl(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gainceiling(s, GAINCEILING_16X);
        s->set_aec2(s, 1);          // night mode in the DSP
        s->set_ae_level(s, 1);      // aim a little brighter than neutral
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_saturation(s, 0);
        s->set_bpc(s, 1);           // hide dead pixels
        s->set_wpc(s, 1);
        s->set_lenc(s, 1);          // lens vignetting correction
        s->set_raw_gma(s, 1);
        s->set_dcw(s, 1);

        ESP_LOGI(TAG, "Sensor PID 0x%02x ready", s->id.PID);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Ladeni senzoru za behu                                             */
/* ------------------------------------------------------------------ */

// Prehrat firmware kvuli kazde zmene jasu by znamenalo rozpojovat dratky, tak
// jdou hodnoty menit po siti: /set?var=brightness&val=2
static esp_err_t set_handler(httpd_req_t *req)
{
    char query[96] = { 0 };
    char var[24] = { 0 }, val[16] = { 0 };
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
        httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cekam ?var=NAZEV&val=CISLO");
        return ESP_FAIL;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "senzor neni");
        return ESP_FAIL;
    }

    int v = atoi(val);
    int res = -1;

    if (!strcmp(var, "framesize"))          res = s->set_framesize(s, (framesize_t)v);
    else if (!strcmp(var, "quality"))       res = s->set_quality(s, v);
    else if (!strcmp(var, "brightness"))    res = s->set_brightness(s, v);
    else if (!strcmp(var, "contrast"))      res = s->set_contrast(s, v);
    else if (!strcmp(var, "saturation"))    res = s->set_saturation(s, v);
    else if (!strcmp(var, "gainceiling"))   res = s->set_gainceiling(s, (gainceiling_t)v);
    else if (!strcmp(var, "agc"))           res = s->set_gain_ctrl(s, v);
    else if (!strcmp(var, "agc_gain"))      res = s->set_agc_gain(s, v);
    else if (!strcmp(var, "aec"))           res = s->set_exposure_ctrl(s, v);
    else if (!strcmp(var, "aec_value"))     res = s->set_aec_value(s, v);
    else if (!strcmp(var, "aec2"))          res = s->set_aec2(s, v);
    else if (!strcmp(var, "ae_level"))      res = s->set_ae_level(s, v);
    else if (!strcmp(var, "awb"))           res = s->set_whitebal(s, v);
    else if (!strcmp(var, "awb_gain"))      res = s->set_awb_gain(s, v);
    else if (!strcmp(var, "wb_mode"))       res = s->set_wb_mode(s, v);
    else if (!strcmp(var, "bpc"))           res = s->set_bpc(s, v);
    else if (!strcmp(var, "wpc"))           res = s->set_wpc(s, v);
    else if (!strcmp(var, "raw_gma"))       res = s->set_raw_gma(s, v);
    else if (!strcmp(var, "lenc"))          res = s->set_lenc(s, v);
    else if (!strcmp(var, "dcw"))           res = s->set_dcw(s, v);
    else if (!strcmp(var, "hmirror"))       res = s->set_hmirror(s, v);
    else if (!strcmp(var, "vflip"))         res = s->set_vflip(s, v);
    else if (!strcmp(var, "special_effect")) res = s->set_special_effect(s, v);
    else if (!strcmp(var, "colorbar"))      res = s->set_colorbar(s, v);
    // Hodiny senzoru v MHz. Vyssi = vic snimku za sekundu, ale na tomhle modulu
    // se od nejake hranice vraceji vodorovne pruhy - tohle je hleda bez reflashe.
    else if (!strcmp(var, "xclk"))          res = s->set_xclk(s, LEDC_TIMER_0, v);

    ESP_LOGI(TAG, "set %s = %d -> %d", var, v, res);
    char out[80];
    int n = snprintf(out, sizeof(out), "{\"var\":\"%s\",\"val\":%d,\"ok\":%s}",
                     var, v, res == 0 ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, out, n);
}

// Aktualni nastaveni senzoru, aby slo ladit podle cisel a ne od oka.
static esp_err_t get_handler(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "senzor neni");
        return ESP_FAIL;
    }
    camera_status_t *c = &s->status;
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{\"framesize\":%d,\"quality\":%d,\"brightness\":%d,\"contrast\":%d,\"saturation\":%d,"
        "\"gainceiling\":%d,\"agc\":%d,\"agc_gain\":%d,\"aec\":%d,\"aec_value\":%d,\"aec2\":%d,"
        "\"ae_level\":%d,\"awb\":%d,\"awb_gain\":%d,\"wb_mode\":%d,\"bpc\":%d,\"wpc\":%d,"
        "\"raw_gma\":%d,\"lenc\":%d,\"dcw\":%d,\"hmirror\":%d,\"vflip\":%d,\"xclk_mhz\":%d}",
        c->framesize, c->quality, c->brightness, c->contrast, c->saturation,
        c->gainceiling, c->agc, c->agc_gain, c->aec, c->aec_value, c->aec2,
        c->ae_level, c->awb, c->awb_gain, c->wb_mode, c->bpc, c->wpc,
        c->raw_gma, c->lenc, c->dcw, c->hmirror, c->vflip, s->xclk_freq_hz / 1000000);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, n);
}

/* ------------------------------------------------------------------ */
/*  HTTP                                                               */
/* ------------------------------------------------------------------ */

#define PART_BOUNDARY "zoboframe"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_PART = "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    char part[64];
    int64_t t_start = esp_timer_get_time();
    uint32_t frames = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Frame capture failed");
            res = ESP_FAIL;
            break;
        }

        int len = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
        res = httpd_resp_send_chunk(req, part, len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

        s_frames_sent++;
        s_bytes_sent += fb->len;
        frames++;
        esp_camera_fb_return(fb);

        // A closed browser tab shows up as a failed send; that is the only way
        // out of this loop.
        if (res != ESP_OK) break;
    }

    int64_t ms = (esp_timer_get_time() - t_start) / 1000;
    if (ms > 0) ESP_LOGI(TAG, "Stream ended: %u frames, %.1f fps", (unsigned)frames, frames * 1000.0 / ms);
    return res;
}

static esp_err_t jpg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=zobo.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    s_frames_sent++;
    s_bytes_sent += fb->len;
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"up\":%lld,\"frames\":%u,\"kbytes\":%u,\"heap\":%u,\"psram\":%s,"
                     "\"framesize\":%d,\"fw\":\"%s\",\"trial\":%s}",
                     (long long)(esp_timer_get_time() / 1000000),
                     (unsigned)s_frames_sent,
                     (unsigned)(s_bytes_sent / 1024),
                     (unsigned)esp_get_free_heap_size(),
                     esp_psram_is_initialized() ? "true" : "false",
                     s ? s->status.framesize : -1,
                     CAM_FW_VERSION,
                     ota_cam_pending() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, n);
}

// The flash LED is genuinely bright, so it is off unless asked for: /led?on=1
static esp_err_t led_handler(httpd_req_t *req)
{
    char query[32] = { 0 };
    int on = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "on", val, sizeof(val)) == ESP_OK) on = atoi(val);
    }
    gpio_set_level(CAM_PIN_FLASH_LED, on ? 1 : 0);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, on ? "{\"led\":true}" : "{\"led\":false}");
}

// Doma na stejne siti nema smysl posilat firmware oklikou pres server: nova
// binarka se nahraje rovnou sem.
//   curl --data-binary @build/zobo_cam.bin http://<ip>/ota
// Na dalku tahle cesta nefunguje (kamera je za NATem) - tam se posila povel
// {"ota":"https://..."} na zobo/cam/ctrl a kamera si obraz stahne sama.
static esp_err_t ota_push_handler(httpd_req_t *req)
{
    if (ota_cam_busy() || ota_cam_pending()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "jina aktualizace jeste nedobehla");
        return ESP_FAIL;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "neni volny slot");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "Receiving %d bytes of firmware into %s", req->content_len, target->label);
    ota_cam_stop_camera();

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin selhal");
        esp_restart();
        return ESP_FAIL;
    }

    char chunk[1024];
    int left = req->content_len;
    while (left > 0) {
        int got = httpd_req_recv(req, chunk, MIN(left, (int)sizeof(chunk)));
        if (got <= 0) {
            // Prerusene spojeni znamena nekompletni obraz - zahodit, at se
            // nedopise neco, co by se pak nespustilo.
            // Prerusene spojeni znamena nekompletni obraz - zahodit, at se
            // nedopise neco, co by se pak nespustilo. Restart je tu proto, ze
            // kamera je uz odstavena a jinak by zustal jen mrtvy obraz.
            esp_ota_abort(handle);
            ESP_LOGE(TAG, "Upload interrupted with %d bytes to go", left);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "prenos prerusen");
            esp_restart();
        }
        if (esp_ota_write(handle, chunk, got) != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "zapis selhal");
            esp_restart();
        }
        left -= got;
    }

    if (esp_ota_end(handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "obraz neprosel kontrolou");
        esp_restart();
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "prepnuti slotu selhalo");
        esp_restart();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"restart\":true}");
    ESP_LOGW(TAG, "Firmware written to %s, restarting", target->label);

    // Odpoved musi stihnout odejit driv, nez zmizi sit pod rukama.
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html lang=\"cs\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Zobo kamera</title><style>"
    "body{margin:0;background:#0a1628;color:#fff;font-family:'Segoe UI',sans-serif;text-align:center}"
    "h1{font-size:1.1rem;color:#60a5fa;padding:1rem;margin:0}"
    "img{max-width:100%;border:1px solid rgba(96,165,250,.35);border-radius:8px}"
    "a{color:#60a5fa;font-size:.85rem;margin:0 .5rem}"
    "#s{color:#6b7280;font-size:.8rem;padding:.75rem}"
    "</style></head><body>"
    "<h1>Zobo kamera</h1>"
    "<img src=\"/stream\" alt=\"zivy obraz\">"
    "<div><a href=\"/jpg\">jeden snimek</a><a href=\"/led?on=1\">svetlo zap</a>"
    "<a href=\"/led?on=0\">vyp</a></div><div id=\"s\">...</div>"
    "<script>setInterval(async()=>{try{const r=await fetch('/status');const d=await r.json();"
    "document.getElementById('s').textContent='bezi '+d.up+' s, '+d.frames+' snimku, '"
    "+d.kbytes+' kB, volna pamet '+Math.round(d.heap/1024)+' kB';}catch(e){}},2000);</script>"
    "</body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static void http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 9;
    // Nahravani firmwaru je pomalejsi nez obrazek, vychozich 5 s na prijem
    // celeho tela by na megabajtovy obraz nestacilo.
    config.recv_wait_timeout = 20;
    // The stream handler never returns while a viewer is watching, so it needs
    // a worker of its own or nothing else would answer.
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return;
    }

    httpd_uri_t uris[] = {
        { .uri = "/",       .method = HTTP_GET, .handler = index_handler },
        { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler },
        { .uri = "/jpg",    .method = HTTP_GET, .handler = jpg_handler },
        { .uri = "/status", .method = HTTP_GET, .handler = status_handler },
        { .uri = "/led",    .method = HTTP_GET, .handler = led_handler },
        { .uri = "/set",    .method = HTTP_GET, .handler = set_handler },
        { .uri = "/get",    .method = HTTP_GET, .handler = get_handler },
        { .uri = "/ota",    .method = HTTP_POST, .handler = ota_push_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "HTTP server up");
}

/* ------------------------------------------------------------------ */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    gpio_config_t led = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CAM_PIN_FLASH_LED,
    };
    gpio_config(&led);
    gpio_set_level(CAM_PIN_FLASH_LED, 0);

    ESP_LOGI(TAG, "Zobo camera %s starting, free heap %u",
             CAM_FW_VERSION, (unsigned)esp_get_free_heap_size());

    // Musi byt driv nez cokoliv, co muze spadnout: kdyz tenhle obraz prisel po
    // siti a jeste se nepotvrdil, rozjede se hlidac navratu k predchozimu.
    ota_cam_init();

    // Camera first, so its verdict is in the log before WiFi noise scrolls it
    // away. A failure is reported but not fatal: the board still joins the
    // network, and a page saying "no sensor" is far easier to debug than a
    // board that went quiet.
    bool have_camera = (camera_start() == ESP_OK);
    if (!have_camera) {
        ESP_LOGE(TAG, "No camera - check the ribbon cable and the 5V supply");
    }

    wifi_start();
    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    http_start();

    // Path out of the local network: the camera is behind NAT, so frames are
    // pushed to the broker that already runs for the robot instead of waiting
    // for someone to connect in.
    if (have_camera) mqtt_cam_start();
}
