/**
 * Motor Control Module - Header
 */

#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

// Initialize motor PWM and GPIO
void motor_init(void);

// Set PWM duty cycle (0-255)
void motor_set_pwm(uint8_t left, uint8_t right);

// Set motor direction
void motor_set_direction(bool left_high, bool right_high);

// Stop both motors
void motor_stop(void);

// Ramp control
void motor_start_ramp(void);
void motor_update_ramp(void);
bool motor_is_ramping(void);
bool motor_is_latched(void);
void motor_cancel_ramp(void);

// Inactivity timeout
void motor_reset_inactivity(void);
void motor_check_inactivity(void);

#endif // MOTOR_H
