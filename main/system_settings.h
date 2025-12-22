/**
 * Win32 OS - System Settings
 * Persistent storage for system configuration using LittleFS
 */

#ifndef SYSTEM_SETTINGS_H
#define SYSTEM_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

// WiFi credentials structure
typedef struct {
    char ssid[33];
    char password[65];
    bool valid;
} wifi_credentials_t;

// Keyboard theme
typedef enum {
    KEYBOARD_THEME_DARK = 0,   // Dark theme (default)
    KEYBOARD_THEME_LIGHT = 1   // Light theme
} keyboard_theme_t;

// Keyboard settings structure
typedef struct {
    uint16_t height;        // Height in pixels (default 500)
    uint8_t height_percent; // Height as % of screen (30-80)
    bool use_percent;       // Use percent or fixed height
    keyboard_theme_t theme; // Dark or light theme
} keyboard_settings_t;

// Location/City settings structure
typedef struct {
    char city_name[64];     // City name (UTF-8)
    float latitude;         // Latitude
    float longitude;        // Longitude
    int8_t timezone;        // Timezone offset from UTC
    bool valid;             // Is location set
} location_settings_t;

// Lock screen type
typedef enum {
    LOCK_TYPE_SLIDE = 0,    // Slide to unlock (default)
    LOCK_TYPE_PIN = 1,      // 4-digit PIN code
    LOCK_TYPE_PASSWORD = 2  // Full password with keyboard
} lock_type_t;

// User profile structure
typedef struct {
    char username[32];      // Display name
    uint32_t avatar_color;  // Avatar background color (hex)
    char password[32];      // Lock screen password/PIN (empty = no password)
    bool password_enabled;  // Is password required for unlock
    lock_type_t lock_type;  // Type of lock screen
} user_profile_t;

// Game scores structure
typedef struct {
    int32_t flappy_best;    // Flappy Bird best score
    // Add more games here
} game_scores_t;

// UI Style enum
typedef enum {
    UI_STYLE_WIN7 = 0,      // Windows 7 style (default)
    UI_STYLE_WINXP = 1,     // Windows XP style
    UI_STYLE_WIN11 = 2      // Windows 11 style
} ui_style_t;

// Desktop icon position
typedef struct {
    char app_name[32];
    int8_t grid_x;
    int8_t grid_y;
    bool valid;
} icon_position_t;

// Personalization settings
typedef struct {
    ui_style_t ui_style;            // Current UI style
    uint8_t desktop_grid_cols;      // Desktop grid columns (default 4)
    uint8_t desktop_grid_rows;      // Desktop grid rows (default 5)
    char pinned_apps[3][32];        // Up to 3 pinned taskbar apps
    icon_position_t icon_positions[20];  // Custom icon positions
    uint8_t icon_position_count;
} personalization_t;

// System settings structure
typedef struct {
    // Display
    uint8_t brightness;
    int wallpaper_index;
    
    // Time
    int8_t timezone_offset;  // Hours from UTC
    bool time_24h_format;
    int64_t last_known_time;  // Unix timestamp
    
    // WiFi (up to 5 saved networks)
    wifi_credentials_t saved_wifi[5];
    int saved_wifi_count;
    
    // Keyboard
    keyboard_settings_t keyboard;
    
    // Location
    location_settings_t location;
    
    // User profile
    user_profile_t user;
    
    // Game scores
    game_scores_t scores;
    
    // Personalization
    personalization_t personalization;
    
    // Bluetooth
    bool bt_enabled;
    char bt_name[32];
    
    // Debug
    bool debug_mode;
} system_settings_t;

// Initialize settings system (call once at startup)
int settings_init(void);

// Load all settings from storage
int settings_load(system_settings_t *settings);

// Save all settings to storage
int settings_save(const system_settings_t *settings);

// Individual setting helpers
int settings_set_brightness(uint8_t brightness);
uint8_t settings_get_brightness(void);

int settings_set_wallpaper(int index);
int settings_get_wallpaper(void);

int settings_set_time(int64_t timestamp, int8_t tz_offset);
int64_t settings_get_time(void);
int8_t settings_get_timezone(void);

// WiFi credentials management
int settings_save_wifi(const char *ssid, const char *password);
int settings_get_wifi(int index, wifi_credentials_t *cred);
int settings_get_wifi_count(void);
int settings_find_wifi(const char *ssid, wifi_credentials_t *cred);
int settings_delete_wifi(const char *ssid);

// Keyboard settings
int settings_set_keyboard_height(uint8_t height_percent);
uint8_t settings_get_keyboard_height(void);
uint16_t settings_get_keyboard_height_px(void);
int settings_set_keyboard_theme(keyboard_theme_t theme);
keyboard_theme_t settings_get_keyboard_theme(void);

// Location settings
int settings_set_location(const char *city, float lat, float lon, int8_t tz);
location_settings_t* settings_get_location(void);
bool settings_has_location(void);

// User profile settings
int settings_set_username(const char *name);
const char* settings_get_username(void);
int settings_set_avatar_color(uint32_t color);
uint32_t settings_get_avatar_color(void);
int settings_set_password(const char *password);
bool settings_check_password(const char *password);
bool settings_has_password(void);
int settings_set_lock_type(lock_type_t type);
lock_type_t settings_get_lock_type(void);

// Game scores
int settings_set_flappy_score(int32_t score);
int32_t settings_get_flappy_score(void);

// UI Style / Personalization
int settings_set_ui_style(ui_style_t style);
ui_style_t settings_get_ui_style(void);

int settings_set_desktop_grid(uint8_t cols, uint8_t rows);
uint8_t settings_get_desktop_grid_cols(void);
uint8_t settings_get_desktop_grid_rows(void);

int settings_set_pinned_app(int index, const char *app_name);
const char* settings_get_pinned_app(int index);

int settings_save_icon_position(const char *app_name, int8_t grid_x, int8_t grid_y);
bool settings_get_icon_position(const char *app_name, int8_t *grid_x, int8_t *grid_y);
int settings_clear_icon_positions(void);

// Factory reset - deletes all settings
int settings_factory_reset(void);

// Get global settings pointer (for direct access)
system_settings_t* settings_get_global(void);

#endif // SYSTEM_SETTINGS_H
