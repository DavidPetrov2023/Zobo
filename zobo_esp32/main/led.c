/**
 * LED Control Module
 */

#include "led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "LED";

// LED pins (active LOW)
#define LED_MAIN            5
#define LED_RED             27
#define LED_GREEN           14
#define LED_BLUE            12

void led_init(void)
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

void led_set_rgb(bool red, bool green, bool blue)
{
    // Active LOW LEDs
    gpio_set_level(LED_RED, red ? 0 : 1);
    gpio_set_level(LED_GREEN, green ? 0 : 1);
    gpio_set_level(LED_BLUE, blue ? 0 : 1);
}

void led_set_main(bool on)
{
    gpio_set_level(LED_MAIN, on ? 1 : 0);
}

void led_startup_sequence(void)
{
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

void led_indicate_wifi_connecting(void)
{
    // Blue blinking
    led_set_rgb(false, false, true);
}

void led_indicate_wifi_connected(void)
{
    // Green solid
    led_set_rgb(false, true, false);
}

void led_indicate_ota_progress(void)
{
    // Blue + Green = Cyan
    led_set_rgb(false, true, true);
}

void led_indicate_ota_success(void)
{
    // Green blinking
    for (int i = 0; i < 5; i++) {
        led_set_rgb(false, true, false);
        vTaskDelay(pdMS_TO_TICKS(200));
        led_set_rgb(false, false, false);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void led_indicate_ota_fail(void)
{
    // Red blinking
    for (int i = 0; i < 5; i++) {
        led_set_rgb(true, false, false);
        vTaskDelay(pdMS_TO_TICKS(200));
        led_set_rgb(false, false, false);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
