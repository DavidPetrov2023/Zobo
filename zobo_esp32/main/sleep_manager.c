/**
 * Sleep Manager
 * Handles power saving with deep sleep after inactivity
 * Periodically wakes up to blink LED, then goes back to sleep
 */

#include "sleep_manager.h"
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include <inttypes.h>

static const char *TAG = "SLEEP";

// Configuration
#define INACTIVITY_TIMEOUT_MS   15000       // 15 seconds until sleep
#define DEEP_SLEEP_DURATION_US  (10000000)  // 10 seconds between wakes
#define BLINK_DURATION_MS       50          // LED on for 50ms (short blink)

// State
static volatile uint32_t last_activity_time = 0;
static volatile bool is_sleeping = false;

// Check if we woke from deep sleep
static bool woke_from_deep_sleep(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return (cause == ESP_SLEEP_WAKEUP_TIMER);
}

void sleep_manager_reset(void)
{
    last_activity_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    is_sleeping = false;
}

bool sleep_manager_is_sleeping(void)
{
    return is_sleeping;
}

static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep for 10 seconds...");

    // Turn off all LEDs before sleep
    led_set_rgb(false, false, false);

    // Small delay to allow log to flush
    vTaskDelay(pdMS_TO_TICKS(50));

    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US);

    // Enter deep sleep
    esp_deep_sleep_start();

    // Never reaches here - device resets on wake
}

static void sleep_task(void *arg)
{
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t inactive_time = now - last_activity_time;

        if (!is_sleeping && inactive_time >= INACTIVITY_TIMEOUT_MS) {
            is_sleeping = true;
            ESP_LOGI(TAG, "Entering sleep mode after %" PRIu32 " ms of inactivity", inactive_time);
            enter_deep_sleep();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool sleep_manager_check_wake(void)
{
    // Check if we woke from deep sleep timer
    if (woke_from_deep_sleep()) {
        ESP_LOGI(TAG, "Woke from deep sleep - quick blink");

        // Initialize only LED GPIO (minimal init)
        led_init();

        // Quick blink
        led_set_rgb(false, false, true);  // Blue
        vTaskDelay(pdMS_TO_TICKS(BLINK_DURATION_MS));
        led_set_rgb(false, false, false);

        // Go back to sleep immediately
        enter_deep_sleep();
        // Never returns
        return true;
    }
    return false;
}

void sleep_manager_init(void)
{
    // Normal startup - initialize activity timer
    last_activity_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    is_sleeping = false;

    xTaskCreate(sleep_task, "sleep_mgr", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "Sleep manager initialized (timeout: %d ms)", INACTIVITY_TIMEOUT_MS);
}
