/**
 * Win32 OS - System Settings Implementation
 * Persistent storage using LittleFS
 */

#include "system_settings.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>

static const char *TAG = "SETTINGS";
static const char *SETTINGS_FILE = "/littlefs/system.cfg";

static system_settings_t g_settings = {0};
static bool g_initialized = false;

// Apply timezone to system
static void apply_timezone(int8_t tz_offset) {
    // Build POSIX timezone string
    // Format: "UTC-X" where X is the offset (note: POSIX uses inverted sign)
    char tz_str[32];
    if (tz_offset >= 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC-%d", tz_offset);
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC+%d", -tz_offset);
    }
    setenv("TZ", tz_str, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone applied: %s (UTC%+d)", tz_str, tz_offset);
}

// Default settings
static void settings_set_defaults(system_settings_t *s) {
    memset(s, 0, sizeof(system_settings_t));
    s->brightness = 50;
    s->wallpaper_index = 0;
    s->timezone_offset = 3;  // Moscow time default
    s->time_24h_format = true;
    s->last_known_time = 0;
    s->saved_wifi_count = 0;
    
    // Keyboard defaults
    s->keyboard.height = 500;
    s->keyboard.height_percent = 62;  // ~500px of 800px
    s->keyboard.use_percent = true;
    
    // Location defaults (Moscow)
    strncpy(s->location.city_name, "Москва", sizeof(s->location.city_name) - 1);
    s->location.latitude = 55.7558f;
    s->location.longitude = 37.6173f;
    s->location.timezone = 3;
    s->location.valid = true;
    
    // User profile defaults
    strncpy(s->user.username, "User", sizeof(s->user.username) - 1);
    s->user.avatar_color = 0x4A90D9;  // Default blue
    s->user.password[0] = '\0';
    s->user.password_enabled = false;
    
    // Game scores
    s->scores.flappy_best = 0;
    
    // Personalization defaults
    s->personalization.ui_style = UI_STYLE_WIN7;
    s->personalization.desktop_grid_cols = 4;
    s->personalization.desktop_grid_rows = 5;
    memset(s->personalization.pinned_apps, 0, sizeof(s->personalization.pinned_apps));
    memset(s->personalization.icon_positions, 0, sizeof(s->personalization.icon_positions));
    s->personalization.icon_position_count = 0;
    
    s->bt_enabled = false;
    strncpy(s->bt_name, "WinEsp32-PDA", sizeof(s->bt_name) - 1);
    s->debug_mode = false;
    
    ESP_LOGI(TAG, "Settings set to defaults");
}

int settings_init(void) {
    if (g_initialized) return 0;
    
    ESP_LOGI(TAG, "Initializing system settings");
    
    // Try to load existing settings
    if (settings_load(&g_settings) != 0) {
        ESP_LOGW(TAG, "No saved settings found, using defaults");
        settings_set_defaults(&g_settings);
        settings_save(&g_settings);
    }
    
    // Validate keyboard settings (fix corrupted values)
    if (g_settings.keyboard.height_percent < 17 || g_settings.keyboard.height_percent > 80) {
        ESP_LOGW(TAG, "Invalid keyboard height %d%%, resetting to 62%%", g_settings.keyboard.height_percent);
        g_settings.keyboard.height_percent = 62;
        g_settings.keyboard.height = 496;
        g_settings.keyboard.use_percent = true;
        settings_save(&g_settings);
    }
    
    g_initialized = true;
    ESP_LOGI(TAG, "Settings initialized: brightness=%d, wallpaper=%d, wifi_count=%d, kb_height=%d%%",
             g_settings.brightness, g_settings.wallpaper_index, g_settings.saved_wifi_count,
             g_settings.keyboard.height_percent);
    
    // Apply timezone from settings
    apply_timezone(g_settings.timezone_offset);
    
    return 0;
}

int settings_load(system_settings_t *settings) {
    FILE *f = fopen(SETTINGS_FILE, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Settings file not found: %s", SETTINGS_FILE);
        return -1;
    }
    
    // Read magic header
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "WIN32CFG", 8) != 0) {
        ESP_LOGE(TAG, "Invalid settings file format");
        fclose(f);
        return -1;
    }
    
    // Read version
    uint8_t version;
    fread(&version, 1, 1, f);
    if (version != 1) {
        ESP_LOGW(TAG, "Unknown settings version: %d", version);
        fclose(f);
        return -1;
    }
    
    // Read settings
    size_t read = fread(settings, 1, sizeof(system_settings_t), f);
    fclose(f);
    
    if (read != sizeof(system_settings_t)) {
        ESP_LOGE(TAG, "Settings file corrupted (read %d, expected %d)", 
                 (int)read, (int)sizeof(system_settings_t));
        return -1;
    }
    
    ESP_LOGI(TAG, "Settings loaded successfully");
    return 0;
}

int settings_save(const system_settings_t *settings) {
    ESP_LOGI(TAG, "Saving settings to %s", SETTINGS_FILE);
    
    FILE *f = fopen(SETTINGS_FILE, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open settings file for writing");
        return -1;
    }
    
    // Write magic header
    fwrite("WIN32CFG", 1, 8, f);
    
    // Write version
    uint8_t version = 1;
    fwrite(&version, 1, 1, f);
    
    // Write settings
    fwrite(settings, 1, sizeof(system_settings_t), f);
    fclose(f);
    
    ESP_LOGI(TAG, "Settings saved successfully");
    return 0;
}

// Individual setting helpers
int settings_set_brightness(uint8_t brightness) {
    g_settings.brightness = brightness;
    ESP_LOGD(TAG, "Brightness set to %d", brightness);
    return settings_save(&g_settings);
}

uint8_t settings_get_brightness(void) {
    return g_settings.brightness;
}

int settings_set_wallpaper(int index) {
    g_settings.wallpaper_index = index;
    ESP_LOGI(TAG, "Wallpaper set to %d", index);
    return settings_save(&g_settings);
}

int settings_get_wallpaper(void) {
    return g_settings.wallpaper_index;
}

int settings_set_time(int64_t timestamp, int8_t tz_offset) {
    if (timestamp != 0) {
        g_settings.last_known_time = timestamp;
    }
    g_settings.timezone_offset = tz_offset;
    
    // Apply timezone immediately
    apply_timezone(tz_offset);
    
    ESP_LOGI(TAG, "Timezone set to UTC%+d", tz_offset);
    return settings_save(&g_settings);
}

int64_t settings_get_time(void) {
    return g_settings.last_known_time;
}

int8_t settings_get_timezone(void) {
    return g_settings.timezone_offset;
}


// WiFi credentials management
int settings_save_wifi(const char *ssid, const char *password) {
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return -1;
    }
    
    ESP_LOGI(TAG, "Saving WiFi: %s", ssid);
    
    // Check if already exists - update it
    for (int i = 0; i < g_settings.saved_wifi_count; i++) {
        if (strcmp(g_settings.saved_wifi[i].ssid, ssid) == 0) {
            strncpy(g_settings.saved_wifi[i].password, password ? password : "", 64);
            g_settings.saved_wifi[i].password[64] = '\0';
            g_settings.saved_wifi[i].valid = true;
            ESP_LOGI(TAG, "Updated existing WiFi entry at index %d", i);
            return settings_save(&g_settings);
        }
    }
    
    // Add new entry
    if (g_settings.saved_wifi_count >= 5) {
        // Shift entries to make room (remove oldest)
        ESP_LOGW(TAG, "WiFi list full, removing oldest entry");
        for (int i = 0; i < 4; i++) {
            g_settings.saved_wifi[i] = g_settings.saved_wifi[i + 1];
        }
        g_settings.saved_wifi_count = 4;
    }
    
    int idx = g_settings.saved_wifi_count;
    strncpy(g_settings.saved_wifi[idx].ssid, ssid, 32);
    g_settings.saved_wifi[idx].ssid[32] = '\0';
    strncpy(g_settings.saved_wifi[idx].password, password ? password : "", 64);
    g_settings.saved_wifi[idx].password[64] = '\0';
    g_settings.saved_wifi[idx].valid = true;
    g_settings.saved_wifi_count++;
    
    ESP_LOGI(TAG, "Added new WiFi entry at index %d, total: %d", idx, g_settings.saved_wifi_count);
    return settings_save(&g_settings);
}

int settings_get_wifi(int index, wifi_credentials_t *cred) {
    if (index < 0 || index >= g_settings.saved_wifi_count || !cred) {
        return -1;
    }
    *cred = g_settings.saved_wifi[index];
    return 0;
}

int settings_get_wifi_count(void) {
    return g_settings.saved_wifi_count;
}

int settings_find_wifi(const char *ssid, wifi_credentials_t *cred) {
    if (!ssid) return -1;
    
    for (int i = 0; i < g_settings.saved_wifi_count; i++) {
        if (strcmp(g_settings.saved_wifi[i].ssid, ssid) == 0) {
            if (cred) *cred = g_settings.saved_wifi[i];
            ESP_LOGD(TAG, "Found saved WiFi: %s at index %d", ssid, i);
            return i;
        }
    }
    return -1;
}

int settings_delete_wifi(const char *ssid) {
    if (!ssid) return -1;
    
    for (int i = 0; i < g_settings.saved_wifi_count; i++) {
        if (strcmp(g_settings.saved_wifi[i].ssid, ssid) == 0) {
            // Shift remaining entries
            for (int j = i; j < g_settings.saved_wifi_count - 1; j++) {
                g_settings.saved_wifi[j] = g_settings.saved_wifi[j + 1];
            }
            g_settings.saved_wifi_count--;
            ESP_LOGI(TAG, "Deleted WiFi: %s", ssid);
            return settings_save(&g_settings);
        }
    }
    return -1;
}

system_settings_t* settings_get_global(void) {
    return &g_settings;
}

// Keyboard settings
int settings_set_keyboard_height(uint8_t height_percent) {
    if (height_percent < 17) height_percent = 17;  // Minimum like console (135px)
    if (height_percent > 80) height_percent = 80;
    
    g_settings.keyboard.height_percent = height_percent;
    g_settings.keyboard.height = (800 * height_percent) / 100;  // Calculate pixels
    g_settings.keyboard.use_percent = true;
    
    ESP_LOGI(TAG, "Keyboard height set to %d%% (%dpx)", height_percent, g_settings.keyboard.height);
    return settings_save(&g_settings);
}

uint8_t settings_get_keyboard_height(void) {
    return g_settings.keyboard.height_percent;
}

uint16_t settings_get_keyboard_height_px(void) {
    // Ensure valid range
    uint8_t pct = g_settings.keyboard.height_percent;
    if (pct < 17 || pct > 80) pct = 62;  // Default 62%
    uint16_t px = (800 * pct) / 100;
    ESP_LOGD(TAG, "Keyboard height: %d%% = %dpx", pct, px);
    return px;
}

int settings_set_keyboard_theme(keyboard_theme_t theme) {
    g_settings.keyboard.theme = theme;
    ESP_LOGI(TAG, "Keyboard theme set to %s", theme == KEYBOARD_THEME_DARK ? "dark" : "light");
    return settings_save(&g_settings);
}

keyboard_theme_t settings_get_keyboard_theme(void) {
    return g_settings.keyboard.theme;
}


// Location settings
int settings_set_location(const char *city, float lat, float lon, int8_t tz) {
    if (!city) return -1;
    
    strncpy(g_settings.location.city_name, city, sizeof(g_settings.location.city_name) - 1);
    g_settings.location.city_name[sizeof(g_settings.location.city_name) - 1] = '\0';
    g_settings.location.latitude = lat;
    g_settings.location.longitude = lon;
    g_settings.location.timezone = tz;
    g_settings.location.valid = true;
    
    // Also update timezone
    g_settings.timezone_offset = tz;
    
    // Apply timezone immediately
    apply_timezone(tz);
    
    ESP_LOGI(TAG, "Location set: %s (%.4f, %.4f) TZ=%+d", city, lat, lon, tz);
    return settings_save(&g_settings);
}

location_settings_t* settings_get_location(void) {
    return &g_settings.location;
}

bool settings_has_location(void) {
    return g_settings.location.valid;
}


// User profile settings
int settings_set_username(const char *name) {
    if (!name) return -1;
    strncpy(g_settings.user.username, name, sizeof(g_settings.user.username) - 1);
    g_settings.user.username[sizeof(g_settings.user.username) - 1] = '\0';
    ESP_LOGI(TAG, "Username set to: %s", name);
    return settings_save(&g_settings);
}

const char* settings_get_username(void) {
    if (strlen(g_settings.user.username) == 0) {
        return "User";  // Default name
    }
    return g_settings.user.username;
}

int settings_set_avatar_color(uint32_t color) {
    g_settings.user.avatar_color = color;
    ESP_LOGI(TAG, "Avatar color set to: 0x%06X", (unsigned int)color);
    return settings_save(&g_settings);
}

uint32_t settings_get_avatar_color(void) {
    if (g_settings.user.avatar_color == 0) {
        return 0x4A90D9;  // Default blue
    }
    return g_settings.user.avatar_color;
}

int settings_set_password(const char *password) {
    if (!password) {
        // Clear password
        memset(g_settings.user.password, 0, sizeof(g_settings.user.password));
        g_settings.user.password_enabled = false;
        ESP_LOGI(TAG, "Password cleared");
    } else if (strlen(password) == 0) {
        // Empty password = disable
        memset(g_settings.user.password, 0, sizeof(g_settings.user.password));
        g_settings.user.password_enabled = false;
        ESP_LOGI(TAG, "Password disabled");
    } else {
        strncpy(g_settings.user.password, password, sizeof(g_settings.user.password) - 1);
        g_settings.user.password[sizeof(g_settings.user.password) - 1] = '\0';
        g_settings.user.password_enabled = true;
        ESP_LOGI(TAG, "Password set (length: %d)", (int)strlen(password));
    }
    return settings_save(&g_settings);
}

bool settings_check_password(const char *password) {
    if (!g_settings.user.password_enabled) {
        return true;  // No password required
    }
    if (!password) {
        return false;
    }
    return strcmp(g_settings.user.password, password) == 0;
}

bool settings_has_password(void) {
    return g_settings.user.password_enabled && strlen(g_settings.user.password) > 0;
}

int settings_set_lock_type(lock_type_t type) {
    g_settings.user.lock_type = type;
    ESP_LOGI(TAG, "Lock type set to: %d", (int)type);
    return settings_save(&g_settings);
}

lock_type_t settings_get_lock_type(void) {
    return g_settings.user.lock_type;
}

// Game scores
int settings_set_flappy_score(int32_t score) {
    if (score > g_settings.scores.flappy_best) {
        g_settings.scores.flappy_best = score;
        ESP_LOGI(TAG, "New Flappy Bird high score: %d", (int)score);
        return settings_save(&g_settings);
    }
    return 0;  // Not a new high score
}

int32_t settings_get_flappy_score(void) {
    return g_settings.scores.flappy_best;
}

// UI Style / Personalization
int settings_set_ui_style(ui_style_t style) {
    if (style > UI_STYLE_WIN11) style = UI_STYLE_WIN7;
    g_settings.personalization.ui_style = style;
    ESP_LOGI(TAG, "UI style set to: %d", (int)style);
    return settings_save(&g_settings);
}

ui_style_t settings_get_ui_style(void) {
    return g_settings.personalization.ui_style;
}

int settings_set_desktop_grid(uint8_t cols, uint8_t rows) {
    if (cols < 3) cols = 3;
    if (cols > 10) cols = 10;
    if (rows < 3) rows = 3;
    if (rows > 8) rows = 8;
    
    g_settings.personalization.desktop_grid_cols = cols;
    g_settings.personalization.desktop_grid_rows = rows;
    ESP_LOGI(TAG, "Desktop grid set to: %dx%d", cols, rows);
    return settings_save(&g_settings);
}

uint8_t settings_get_desktop_grid_cols(void) {
    uint8_t cols = g_settings.personalization.desktop_grid_cols;
    if (cols < 3 || cols > 10) cols = 4;  // Default
    return cols;
}

uint8_t settings_get_desktop_grid_rows(void) {
    uint8_t rows = g_settings.personalization.desktop_grid_rows;
    if (rows < 3 || rows > 8) rows = 5;  // Default
    return rows;
}

int settings_set_pinned_app(int index, const char *app_name) {
    if (index < 0 || index >= 3) return -1;
    
    if (app_name && app_name[0]) {
        strncpy(g_settings.personalization.pinned_apps[index], app_name, 31);
        g_settings.personalization.pinned_apps[index][31] = '\0';
        ESP_LOGI(TAG, "Pinned app %d set to: %s", index, app_name);
    } else {
        g_settings.personalization.pinned_apps[index][0] = '\0';
        ESP_LOGI(TAG, "Pinned app %d cleared", index);
    }
    return settings_save(&g_settings);
}

const char* settings_get_pinned_app(int index) {
    if (index < 0 || index >= 3) return NULL;
    if (g_settings.personalization.pinned_apps[index][0] == '\0') return NULL;
    return g_settings.personalization.pinned_apps[index];
}

int settings_save_icon_position(const char *app_name, int8_t grid_x, int8_t grid_y) {
    if (!app_name) return -1;
    
    // Check if already exists - update it
    for (int i = 0; i < g_settings.personalization.icon_position_count; i++) {
        if (strcmp(g_settings.personalization.icon_positions[i].app_name, app_name) == 0) {
            g_settings.personalization.icon_positions[i].grid_x = grid_x;
            g_settings.personalization.icon_positions[i].grid_y = grid_y;
            ESP_LOGI(TAG, "Updated icon position: %s -> (%d, %d)", app_name, grid_x, grid_y);
            return settings_save(&g_settings);
        }
    }
    
    // Add new entry
    if (g_settings.personalization.icon_position_count >= 20) {
        ESP_LOGW(TAG, "Icon position storage full");
        return -1;
    }
    
    int idx = g_settings.personalization.icon_position_count;
    strncpy(g_settings.personalization.icon_positions[idx].app_name, app_name, 31);
    g_settings.personalization.icon_positions[idx].app_name[31] = '\0';
    g_settings.personalization.icon_positions[idx].grid_x = grid_x;
    g_settings.personalization.icon_positions[idx].grid_y = grid_y;
    g_settings.personalization.icon_positions[idx].valid = true;
    g_settings.personalization.icon_position_count++;
    
    ESP_LOGI(TAG, "Saved icon position: %s -> (%d, %d)", app_name, grid_x, grid_y);
    return settings_save(&g_settings);
}

bool settings_get_icon_position(const char *app_name, int8_t *grid_x, int8_t *grid_y) {
    if (!app_name || !grid_x || !grid_y) return false;
    
    for (int i = 0; i < g_settings.personalization.icon_position_count; i++) {
        if (g_settings.personalization.icon_positions[i].valid &&
            strcmp(g_settings.personalization.icon_positions[i].app_name, app_name) == 0) {
            *grid_x = g_settings.personalization.icon_positions[i].grid_x;
            *grid_y = g_settings.personalization.icon_positions[i].grid_y;
            return true;
        }
    }
    return false;
}

int settings_clear_icon_positions(void) {
    memset(g_settings.personalization.icon_positions, 0, sizeof(g_settings.personalization.icon_positions));
    g_settings.personalization.icon_position_count = 0;
    ESP_LOGI(TAG, "Icon positions cleared");
    return settings_save(&g_settings);
}

// Factory reset
int settings_factory_reset(void) {
    ESP_LOGW(TAG, "FACTORY RESET - Deleting all settings!");
    
    // Delete settings file
    remove(SETTINGS_FILE);
    
    // Reset to defaults
    settings_set_defaults(&g_settings);
    
    // Save clean defaults
    settings_save(&g_settings);
    
    ESP_LOGI(TAG, "Factory reset complete");
    return 0;
}
