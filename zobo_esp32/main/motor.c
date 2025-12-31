/**
 * Motor Control Module
 */

#include "motor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MOTOR";

// GPIO Pin Definitions
#define PWM_MOTOR_LEFT      16
#define MOTOR_LEFT_DIR      17
#define PWM_MOTOR_RIGHT     25
#define MOTOR_RIGHT_DIR     26

// PWM Configuration
#define PWM_FREQ_HZ         5000
#define PWM_RESOLUTION      LEDC_TIMER_8_BIT
#define PWM_CHANNEL_LEFT    LEDC_CHANNEL_0
#define PWM_CHANNEL_RIGHT   LEDC_CHANNEL_1

// Ramp Configuration
#define RAMP_START_PWM      100
#define RAMP_END_PWM        255
#define RAMP_DURATION_MS    2000
#define LOOP_DELAY_MS       10
#define INACTIVITY_MS       300
#define INACTIVITY_TICKS    (INACTIVITY_MS / LOOP_DELAY_MS)

// State variables
static bool ramp_forward_active = false;
static bool forward_latched = false;
static uint32_t ramp_start_ms = 0;
static int inactivity_timer = 0;
static bool timer_active = false;

void motor_init(void)
{
    // Configure direction pins as outputs
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << MOTOR_LEFT_DIR) | (1ULL << MOTOR_RIGHT_DIR),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    // Configure left motor PWM channel
    ledc_channel_config_t left_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PWM_CHANNEL_LEFT,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_MOTOR_LEFT,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&left_conf);

    // Configure right motor PWM channel
    ledc_channel_config_t right_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PWM_CHANNEL_RIGHT,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_MOTOR_RIGHT,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&right_conf);

    ESP_LOGI(TAG, "Motor PWM initialized");
}

void motor_set_pwm(uint8_t left, uint8_t right)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_LEFT, left);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_LEFT);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_RIGHT, right);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_RIGHT);
}

void motor_set_direction(bool left_high, bool right_high)
{
    gpio_set_level(MOTOR_LEFT_DIR, left_high ? 1 : 0);
    gpio_set_level(MOTOR_RIGHT_DIR, right_high ? 1 : 0);
}

void motor_stop(void)
{
    motor_set_pwm(0, 0);
    motor_set_direction(false, false);
    ramp_forward_active = false;
    forward_latched = false;
}

void motor_start_ramp(void)
{
    if (!ramp_forward_active && !forward_latched) {
        ramp_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        ramp_forward_active = true;
        motor_set_pwm(RAMP_START_PWM, RAMP_START_PWM);
        motor_set_direction(false, false);
        ESP_LOGI(TAG, "Starting forward ramp");
    }
}

void motor_update_ramp(void)
{
    if (ramp_forward_active) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = now - ramp_start_ms;

        if (elapsed >= RAMP_DURATION_MS) {
            motor_set_pwm(RAMP_END_PWM, RAMP_END_PWM);
            ramp_forward_active = false;
            forward_latched = true;
            ESP_LOGI(TAG, "Ramp complete, latched at max");
        } else {
            uint16_t pwm_now = RAMP_START_PWM +
                (uint32_t)(RAMP_END_PWM - RAMP_START_PWM) * elapsed / RAMP_DURATION_MS;
            motor_set_pwm(pwm_now, pwm_now);
        }
    }
}

bool motor_is_ramping(void)
{
    return ramp_forward_active;
}

bool motor_is_latched(void)
{
    return forward_latched;
}

void motor_cancel_ramp(void)
{
    ramp_forward_active = false;
    forward_latched = false;
}

void motor_reset_inactivity(void)
{
    timer_active = true;
    inactivity_timer = INACTIVITY_TICKS;
}

void motor_check_inactivity(void)
{
    if (inactivity_timer > 0) {
        inactivity_timer--;
    } else if (timer_active) {
        timer_active = false;
        ramp_forward_active = false;
        forward_latched = false;
        motor_set_pwm(0, 0);
        motor_set_direction(false, false);
        ESP_LOGI(TAG, "Inactivity timeout - motors stopped");
    }
}
