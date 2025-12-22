/**
 * Win Recovery - Recovery UI Module
 * Windows Recovery Environment style interface
 */

#ifndef RECOVERY_UI_H
#define RECOVERY_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "recovery_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

// Recovery UI colors (WinRE style)
#define RECOVERY_COLOR_BG           0x0078D4    // Windows Blue
#define RECOVERY_COLOR_TILE         0xFFFFFF    // White tiles
#define RECOVERY_COLOR_TILE_HOVER   0xE5F1FB    // Light blue hover
#define RECOVERY_COLOR_TEXT_TITLE   0x000000    // Black title
#define RECOVERY_COLOR_TEXT_DESC    0x666666    // Gray description
#define RECOVERY_COLOR_ACCENT       0x0078D4    // Blue accent

// Console mode colors
#define CONSOLE_COLOR_BG            0x000000    // Black
#define CONSOLE_COLOR_TEXT          0x00FF00    // Green
#define CONSOLE_COLOR_TEXT_WHITE    0xFFFFFF    // White
#define CONSOLE_COLOR_ERROR         0xFF4444    // Red
#define CONSOLE_COLOR_WARNING       0xFFAA00    // Orange
#define CONSOLE_COLOR_PROMPT        0x00AAFF    // Cyan

// Recovery menu tiles
typedef enum {
    TILE_REBOOT = 0,
    TILE_CONSOLE,
    TILE_WIPE_DATA,
    TILE_STARTUP_SETTINGS,
    TILE_RESET_LOCK,
    TILE_DIAGNOSTICS,
    TILE_FACTORY_RESET,
    TILE_POWER_OFF,
    TILE_COUNT
} recovery_tile_t;

// Tile info structure
typedef struct {
    const char *icon;       // Unicode icon or NULL
    const char *title;      // Tile title
    const char *desc;       // Tile description
} recovery_tile_info_t;

/**
 * Start recovery UI
 * This takes over the display and shows mode selection
 */
void recovery_ui_start(void);

/**
 * Set recovery display mode
 * @param mode Display mode (UI or Console)
 */
void recovery_ui_set_mode(recovery_display_mode_t mode);

/**
 * Get current recovery display mode
 * @return Current display mode
 */
recovery_display_mode_t recovery_ui_get_mode(void);

/**
 * Handle BOOT button event in recovery mode
 * @param event Button event type
 */
void recovery_ui_handle_button(int event);

/**
 * Check if recovery UI is active
 * @return true if in recovery mode
 */
bool recovery_ui_is_active(void);

/**
 * Exit recovery mode and reboot normally
 */
void recovery_ui_exit_and_reboot(void);

// Console mode functions
/**
 * Initialize console mode
 */
void recovery_console_init(void);

/**
 * Process console command
 * @param cmd Command string
 */
void recovery_console_process_cmd(const char *cmd);

/**
 * Print text to console
 * @param text Text to print
 */
void recovery_console_print(const char *text);

/**
 * Print colored text to console
 * @param text Text to print
 * @param color Text color (hex)
 */
void recovery_console_print_color(const char *text, uint32_t color);

/**
 * Clear console output
 */
void recovery_console_clear(void);

#ifdef __cplusplus
}
#endif

#endif // RECOVERY_UI_H
