/**
 * Sleep Manager - Header
 * Handles power saving with light sleep after inactivity
 */

#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <stdbool.h>

// Check if woke from deep sleep - call BEFORE other init!
// Returns true if woke from sleep (will blink and sleep again)
bool sleep_manager_check_wake(void);

// Initialize sleep manager (call after other modules)
void sleep_manager_init(void);

// Reset inactivity timer (call on any activity)
void sleep_manager_reset(void);

// Check if device is in sleep mode
bool sleep_manager_is_sleeping(void);

#endif // SLEEP_MANAGER_H
