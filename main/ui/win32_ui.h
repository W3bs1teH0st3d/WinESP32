/**
 * Win32 OS - Main UI Header
 * Windows Vista Style Interface
 */

#ifndef WIN32_UI_H
#define WIN32_UI_H

#include "lvgl.h"

// Screen dimensions
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 800

// UI Colors
#define COLOR_DESKTOP_BG      0x3A6EA5
#define COLOR_TASKBAR_BG      0x1C3B6E
#define COLOR_TASKBAR_GLASS   0x2A5298
#define COLOR_START_HOVER     0x4A7DC4
#define COLOR_SYSTRAY_BG      0x0F2847
#define COLOR_WINDOW_TITLE    0x0054E3
#define COLOR_WINDOW_BG       0xECE9D8
#define COLOR_TEXT_WHITE      0xFFFFFF
#define COLOR_TEXT_BLACK      0x000000

// UI Sizes
#define TASKBAR_HEIGHT        56
#define START_BUTTON_SIZE     64
#define ICON_SIZE             48
#define ICON_SPACING          80
#define SYSTRAY_ICON_SIZE     20
#define DESKTOP_PADDING       20

// WiFi AP record structure (mock for ESP32-P4)
typedef struct {
    uint8_t ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_ap_info_t;

// All functions - no extern "C" needed for C++ only project
void win32_ui_init(void);
void win32_show_boot_screen(void);
void win32_hide_boot_screen(void);
void win32_show_desktop(void);
void win32_show_lock(void);
void win32_show_aod(void);
void win32_lock_device(void);
bool win32_is_locked(void);
void win32_power_button_pressed(void);
void win32_show_recovery_dialog(void);  // Show recovery mode confirmation dialog
void win32_toggle_start_menu(void);
void win32_show_start_menu(void);
void win32_hide_start_menu(void);
bool win32_is_start_menu_visible(void);
void win32_refresh_start_menu_user(void);  // Refresh user profile in start menu
void win32_update_time(void);
void win32_update_wifi(bool connected);
void win32_update_battery(uint8_t level, bool charging);

typedef void (*app_launch_cb_t)(const char* app_name);
void win32_set_app_launch_callback(app_launch_cb_t cb);

void app_launch(const char* app_name);
void app_calculator_create(void);
void app_clock_create(void);
void app_weather_create(void);
void app_settings_create(void);
void app_notepad_create(void);
void app_camera_create(void);
void app_my_computer_create(void);
void app_my_computer_open_path(const char *folder_name);
void app_recycle_bin_create(void);
void app_photo_viewer_create(void);
void app_flappy_create(void);
void app_paint_create(void);
void app_console_create(void);
void app_default_programs_create(void);
void app_help_create(void);
void app_voice_recorder_create(void);
void app_system_monitor_create(void);
void app_snake_create(void);
void app_js_ide_create(void);
void app_tetris_create(void);
void app_2048_create(void);
void app_minesweeper_create(void);
void app_tictactoe_create(void);
void app_memory_create(void);

void settings_show_wifi_page(void);
void settings_show_keyboard_page(void);
void settings_show_wallpaper_page(void);
void settings_show_time_page(void);
void settings_show_brightness_page(void);
void settings_show_bluetooth_page(void);
void settings_show_storage_page(void);
void settings_show_about_page(void);
void settings_show_region_page(void);  // Location/City settings
void settings_show_user_page(void);    // User profile settings
void settings_show_apps_page(void);    // Installed apps list
void settings_show_taskbar_page(void); // Taskbar icons settings
void settings_reset_pages(void);  // Reset settings page pointers when app_window is closed

// Keyboard helper - applies theme from settings
void apply_keyboard_theme(lv_obj_t *keyboard);

// Wallpaper management
void win32_set_wallpaper(int index);
int win32_get_wallpaper_index(void);
int win32_get_wallpaper_count(void);

void system_tray_toggle(void);
void system_tray_show(void);
void system_tray_hide(void);
bool system_tray_is_visible(void);

int system_wifi_init(void);
int system_wifi_scan(wifi_ap_info_t *ap_records, uint16_t *ap_count);
int system_wifi_connect(const char *ssid, const char *password);
bool system_wifi_is_connected(void);
const char* system_wifi_get_ssid(void);
uint8_t system_wifi_get_last_error(void);
const char* system_wifi_get_error_string(uint8_t reason);

// Time sync
void system_time_resync(void);

#endif // WIN32_UI_H
