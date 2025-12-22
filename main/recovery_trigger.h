/**
 * Win Recovery - Recovery Mode Trigger
 * Uses RTC memory to persist recovery request across reboot
 */

#ifndef RECOVERY_TRIGGER_H
#define RECOVERY_TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Magic value to indicate recovery mode request ("WINR")
#define RECOVERY_MAGIC 0x57494E52

// Recovery display modes
typedef enum {
    RECOVERY_MODE_SELECT = 0,   // Mode selection screen
    RECOVERY_MODE_UI = 1,       // Windows-style tiles (WinRE)
    RECOVERY_MODE_CONSOLE = 2   // Text console
} recovery_display_mode_t;

/**
 * Check if recovery mode was requested
 * @return true if recovery flag is set in RTC memory
 */
bool recovery_check_flag(void);

/**
 * Set recovery flag and reboot into recovery mode
 * This sets the magic value in RTC memory and calls esp_restart()
 */
void recovery_request_reboot(void);

/**
 * Clear recovery flag
 * Call this after entering recovery mode to prevent boot loop
 */
void recovery_clear_flag(void);

/**
 * Get preferred recovery display mode
 * @return Last used display mode (UI or Console)
 */
recovery_display_mode_t recovery_get_preferred_mode(void);

/**
 * Set preferred recovery display mode
 * @param mode Display mode to save
 */
void recovery_set_preferred_mode(recovery_display_mode_t mode);

/**
 * Get boot count (number of boots since last factory reset)
 * @return Boot count
 */
uint32_t recovery_get_boot_count(void);

/**
 * Increment boot count (call on each normal boot)
 */
void recovery_increment_boot_count(void);

#ifdef __cplusplus
}
#endif

#endif // RECOVERY_TRIGGER_H
