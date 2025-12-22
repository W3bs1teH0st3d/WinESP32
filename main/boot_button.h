/**
 * Win Recovery - BOOT Button Handler
 * Handles GPIO0 button with single/triple/long press detection
 */

#ifndef BOOT_BUTTON_H
#define BOOT_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// GPIO0 is the BOOT button on ESP32-P4
#define BOOT_BUTTON_GPIO    0

// Timing constants (in milliseconds)
#define BOOT_BTN_DEBOUNCE_MS        50      // Debounce time
#define BOOT_BTN_MULTI_PRESS_MS     500     // Max time between presses for multi-press
#define BOOT_BTN_LONG_PRESS_MS      1000    // Long press threshold

// Button events
typedef enum {
    BOOT_BTN_NONE = 0,      // No event
    BOOT_BTN_SINGLE,        // Single press - AOD toggle in normal mode
    BOOT_BTN_DOUBLE,        // Double press - reserved
    BOOT_BTN_TRIPLE,        // Triple press - Recovery mode trigger
    BOOT_BTN_LONG           // Long press (1s) - Select/confirm in recovery
} boot_button_event_t;

/**
 * Initialize BOOT button GPIO
 * Sets up GPIO0 as input with pull-up
 */
void boot_button_init(void);

/**
 * Check if BOOT button is currently pressed
 * @return true if button is pressed (GPIO low)
 */
bool boot_button_is_pressed(void);

/**
 * Get button event (non-blocking)
 * Call this periodically to detect button events
 * @return Button event type
 */
boot_button_event_t boot_button_get_event(void);

/**
 * Check if button is held during boot
 * Call early in boot sequence to detect recovery entry
 * @return true if button was held for >1s during boot
 */
bool boot_button_check_held_at_boot(void);

/**
 * Reset button state machine
 * Call when switching contexts (e.g., entering recovery)
 */
void boot_button_reset_state(void);

#ifdef __cplusplus
}
#endif

#endif // BOOT_BUTTON_H
