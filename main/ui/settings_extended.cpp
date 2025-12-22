/**
 * Win32 OS - Extended Settings
 * WiFi, Keyboard, and other advanced settings
 */

#include "win32_ui.h"
#include "system_settings.h"
#include "hardware/hardware.h"
#include "recovery_trigger.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Custom font with Cyrillic support
#include "assets.h"
#define UI_FONT &CodeProVariable

static const char *TAG = "SETTINGS_EXT";

// External references
extern lv_obj_t *app_window;

// Settings pages - NOTE: These are children of app_window, so they get deleted
// when app_window is deleted. We track them only to avoid creating duplicates.
static lv_obj_t *settings_wifi_page = NULL;
static lv_obj_t *settings_keyboard_page = NULL;
static lv_obj_t *settings_wallpaper_page = NULL;
static lv_obj_t *settings_time_page = NULL;

// Helper to check if object is still valid (child of app_window)
static bool is_valid_child(lv_obj_t *obj) {
    if (!obj || !app_window) return false;
    // Check if obj is still a child of app_window
    uint32_t child_cnt = lv_obj_get_child_count(app_window);
    for (uint32_t i = 0; i < child_cnt; i++) {
        if (lv_obj_get_child(app_window, i) == obj) return true;
    }
    return false;
}

// Forward declarations
static void settings_wifi_scan_clicked(lv_event_t *e);
static void settings_wifi_item_clicked(lv_event_t *e);
static void show_wifi_password_dialog(const char *ssid, bool is_secured);

// WiFi password dialog elements
static lv_obj_t *wifi_password_dialog = NULL;
static lv_obj_t *wifi_password_textarea = NULL;
static lv_obj_t *wifi_password_keyboard = NULL;
static char pending_ssid[33] = {0};

// Structure to store network info in user_data
typedef struct {
    char ssid[33];
    uint8_t authmode;
} wifi_network_info_t;

// ============ WIFI SETTINGS PAGE ============

void settings_show_wifi_page(void)
{
    ESP_LOGI(TAG, "Opening WiFi settings");
    
    // Reset keyboard page pointer since we're switching pages
    settings_keyboard_page = NULL;
    
    // Delete existing wifi page only if it's still a valid child
    if (settings_wifi_page && is_valid_child(settings_wifi_page)) {
        lv_obj_delete(settings_wifi_page);
    }
    settings_wifi_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_wifi_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_wifi_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_wifi_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_wifi_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_wifi_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_wifi_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_wifi_page, 0, 0);
    lv_obj_set_style_radius(settings_wifi_page, 0, 0);
    lv_obj_set_style_pad_all(settings_wifi_page, 10, 0);
    lv_obj_set_flex_flow(settings_wifi_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_wifi_page, 8, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_wifi_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        app_settings_create();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // WiFi status - Vista style panel
    lv_obj_t *status_cont = lv_obj_create(settings_wifi_page);
    lv_obj_set_size(status_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(status_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(status_cont, 1, 0);
    lv_obj_set_style_radius(status_cont, 4, 0);
    lv_obj_set_style_pad_all(status_cont, 12, 0);
    lv_obj_remove_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *status_label = lv_label_create(status_cont);
    if (system_wifi_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Connected: %s", system_wifi_get_ssid());
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x008800), 0);
    } else {
        lv_label_set_text(status_label, "Not connected");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xCC0000), 0);
    }
    
    // Scan button - Vista style
    lv_obj_t *scan_btn = lv_obj_create(settings_wifi_page);
    lv_obj_set_size(scan_btn, lv_pct(100), 40);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(scan_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(scan_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(scan_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(scan_btn, 1, 0);
    lv_obj_set_style_radius(scan_btn, 4, 0);
    lv_obj_add_flag(scan_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(scan_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scan_btn, settings_wifi_scan_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan for Networks");
    lv_obj_set_style_text_color(scan_label, lv_color_white(), 0);
    lv_obj_center(scan_label);
    lv_obj_remove_flag(scan_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Networks list header
    lv_obj_t *networks_header = lv_label_create(settings_wifi_page);
    lv_label_set_text(networks_header, "Available Networks");
    lv_obj_set_style_text_color(networks_header, lv_color_hex(0x1A5090), 0);
    
    // Networks list container - white background
    lv_obj_t *networks_list = lv_obj_create(settings_wifi_page);
    lv_obj_set_size(networks_list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(networks_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(networks_list, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(networks_list, 1, 0);
    lv_obj_set_style_radius(networks_list, 4, 0);
    lv_obj_set_style_pad_all(networks_list, 5, 0);
    lv_obj_set_flex_flow(networks_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(networks_list, 5, 0);
    
    lv_obj_t *placeholder = lv_label_create(networks_list);
    lv_label_set_text(placeholder, "Tap 'Scan' to find networks");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x888888), 0);
}

static void settings_wifi_scan_clicked(lv_event_t *e)
{
    ESP_LOGI(TAG, "WiFi scan clicked");
    
    lv_obj_t *scan_btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *page = lv_obj_get_parent(scan_btn);
    lv_obj_t *networks_list = lv_obj_get_child(page, -1);
    
    lv_obj_clean(networks_list);
    
    lv_obj_t *scanning_label = lv_label_create(networks_list);
    lv_label_set_text(scanning_label, "Scanning...");
    lv_obj_set_style_text_color(scanning_label, lv_color_hex(0x0054E3), 0);
    
    // Perform scan
    wifi_ap_info_t ap_records[20];
    uint16_t ap_count = 20;
    int ret = system_wifi_scan(ap_records, &ap_count);
    
    lv_obj_delete(scanning_label);
    
    if (ret != 0 || ap_count == 0) {
        lv_obj_t *error_label = lv_label_create(networks_list);
        lv_label_set_text(error_label, "No networks found");
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF6666), 0);
        return;
    }
    
    int valid_count = 0;
    for (int i = 0; i < ap_count; i++) {
        // Filter: skip empty SSID or 0 dBm signal
        if (ap_records[i].ssid[0] == '\0' || ap_records[i].rssi == 0) {
            continue;
        }
        
        valid_count++;
        
        lv_obj_t *item = lv_obj_create(networks_list);
        lv_obj_set_size(item, lv_pct(100), 60);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 6, 0);
        lv_obj_set_style_pad_all(item, 10, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xE8E8FF), LV_STATE_PRESSED);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // SSID
        lv_obj_t *ssid_label = lv_label_create(item);
        lv_label_set_text(ssid_label, (const char *)ap_records[i].ssid);
        lv_obj_set_style_text_color(ssid_label, lv_color_black(), 0);
        lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_remove_flag(ssid_label, LV_OBJ_FLAG_CLICKABLE);
        
        // Signal strength
        char signal_str[32];
        int rssi = ap_records[i].rssi;
        const char *signal_quality;
        if (rssi > -50) signal_quality = "Excellent";
        else if (rssi > -60) signal_quality = "Good";
        else if (rssi > -70) signal_quality = "Fair";
        else signal_quality = "Weak";
        
        snprintf(signal_str, sizeof(signal_str), "%s (%d dBm)", signal_quality, rssi);
        
        lv_obj_t *signal_label = lv_label_create(item);
        lv_label_set_text(signal_label, signal_str);
        lv_obj_set_style_text_color(signal_label, lv_color_hex(0x666666), 0);
        lv_obj_align(signal_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_remove_flag(signal_label, LV_OBJ_FLAG_CLICKABLE);
        
        // Security icon (authmode != 0 means secured)
        if (ap_records[i].authmode != 0) {
            lv_obj_t *lock_label = lv_label_create(item);
            lv_label_set_text(lock_label, "LOCK");
            lv_obj_set_style_text_color(lock_label, lv_color_hex(0x888888), 0);
            lv_obj_align(lock_label, LV_ALIGN_TOP_RIGHT, 0, 0);
            lv_obj_remove_flag(lock_label, LV_OBJ_FLAG_CLICKABLE);
        }
        
        // Store network info in user data
        wifi_network_info_t *net_info = (wifi_network_info_t *)malloc(sizeof(wifi_network_info_t));
        strncpy(net_info->ssid, (const char *)ap_records[i].ssid, 32);
        net_info->ssid[32] = '\0';
        net_info->authmode = ap_records[i].authmode;
        lv_obj_set_user_data(item, net_info);
        
        lv_obj_add_event_cb(item, settings_wifi_item_clicked, LV_EVENT_CLICKED, NULL);
    }
    
    if (valid_count == 0) {
        lv_obj_t *error_label = lv_label_create(networks_list);
        lv_label_set_text(error_label, "No valid networks found");
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF6666), 0);
    }
}

static void settings_wifi_item_clicked(lv_event_t *e)
{
    lv_obj_t *item = (lv_obj_t *)lv_event_get_target(e);
    wifi_network_info_t *net_info = (wifi_network_info_t *)lv_obj_get_user_data(item);
    
    if (!net_info) return;
    
    ESP_LOGI(TAG, "WiFi network clicked: %s (secured: %d)", net_info->ssid, net_info->authmode);
    
    // If network is secured, show password dialog
    if (net_info->authmode != 0) {
        show_wifi_password_dialog(net_info->ssid, true);
    } else {
        // Open network - connect directly
        int ret = system_wifi_connect(net_info->ssid, "");
        if (ret == 0) {
            ESP_LOGI(TAG, "Connected to %s", net_info->ssid);
            settings_show_wifi_page();
        }
    }
}

// Password dialog callbacks
static void wifi_password_connect_clicked(lv_event_t *e)
{
    if (!wifi_password_textarea) {
        ESP_LOGE(TAG, "Password textarea is NULL!");
        return;
    }
    
    const char *password = lv_textarea_get_text(wifi_password_textarea);
    int pass_len = password ? (int)strlen(password) : 0;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Connect button clicked!");
    ESP_LOGI(TAG, "  SSID: %s", pending_ssid);
    ESP_LOGI(TAG, "  Password length: %d", pass_len);
    if (pass_len > 0 && pass_len < 8) {
        ESP_LOGW(TAG, "  WARNING: Password too short (min 8 chars for WPA)");
    }
    ESP_LOGI(TAG, "========================================");
    
    // Copy password before deleting dialog
    char password_copy[65] = {0};
    if (password && pass_len > 0) {
        strncpy(password_copy, password, sizeof(password_copy) - 1);
    }
    
    // Close dialog
    if (wifi_password_dialog) {
        lv_obj_delete(wifi_password_dialog);
        wifi_password_dialog = NULL;
        wifi_password_textarea = NULL;
        wifi_password_keyboard = NULL;
    }
    
    // Connect with copied password
    int ret = system_wifi_connect(pending_ssid, password_copy);
    if (ret == 0) {
        ESP_LOGI(TAG, "Connected to %s", pending_ssid);
        // Save WiFi credentials on successful connection
        settings_save_wifi(pending_ssid, password_copy);
        ESP_LOGI(TAG, "WiFi credentials saved for: %s", pending_ssid);
    } else {
        uint8_t err = system_wifi_get_last_error();
        ESP_LOGE(TAG, "Failed to connect to %s - Error: %d (%s)", 
                 pending_ssid, err, system_wifi_get_error_string(err));
    }
    
    // Refresh page
    settings_show_wifi_page();
}

static void wifi_password_cancel_clicked(lv_event_t *e)
{
    if (wifi_password_dialog) {
        lv_obj_delete(wifi_password_dialog);
        wifi_password_dialog = NULL;
        wifi_password_textarea = NULL;
        wifi_password_keyboard = NULL;
    }
}

static void show_wifi_password_dialog(const char *ssid, bool is_secured)
{
    // Store SSID for later
    strncpy(pending_ssid, ssid, sizeof(pending_ssid) - 1);
    pending_ssid[sizeof(pending_ssid) - 1] = '\0';
    
    // Delete existing dialog if any
    if (wifi_password_dialog) {
        lv_obj_delete(wifi_password_dialog);
    }
    
    // Create fullscreen dialog (better for keyboard)
    wifi_password_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(wifi_password_dialog, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(wifi_password_dialog, 0, 0);
    lv_obj_set_style_bg_color(wifi_password_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_width(wifi_password_dialog, 0, 0);
    lv_obj_set_style_radius(wifi_password_dialog, 0, 0);
    lv_obj_set_style_pad_all(wifi_password_dialog, 8, 0);
    lv_obj_remove_flag(wifi_password_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar - compact
    lv_obj_t *title_bar = lv_obj_create(wifi_password_dialog);
    lv_obj_set_size(title_bar, lv_pct(100), 36);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 4, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Connect to WiFi");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, UI_FONT, 0);
    lv_obj_center(title_label);
    
    // Network name - compact
    lv_obj_t *ssid_label = lv_label_create(wifi_password_dialog);
    char ssid_text[64];
    snprintf(ssid_text, sizeof(ssid_text), "Network: %s", ssid);
    lv_label_set_text(ssid_label, ssid_text);
    lv_obj_set_style_text_color(ssid_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(ssid_label, UI_FONT, 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 8, 42);
    
    // Password textarea - bigger and more prominent
    wifi_password_textarea = lv_textarea_create(wifi_password_dialog);
    lv_obj_set_size(wifi_password_textarea, SCREEN_WIDTH - 20, 55);
    lv_obj_align(wifi_password_textarea, LV_ALIGN_TOP_MID, 0, 68);
    lv_textarea_set_one_line(wifi_password_textarea, true);
    lv_textarea_set_password_mode(wifi_password_textarea, true);
    lv_textarea_set_placeholder_text(wifi_password_textarea, "Enter password...");
    lv_obj_set_style_bg_color(wifi_password_textarea, lv_color_white(), 0);
    lv_obj_set_style_border_color(wifi_password_textarea, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(wifi_password_textarea, 2, 0);
    lv_obj_set_style_text_font(wifi_password_textarea, UI_FONT, 0);
    lv_obj_set_style_pad_all(wifi_password_textarea, 12, 0);
    
    // Row with checkbox and buttons - compact horizontal layout
    lv_obj_t *controls_row = lv_obj_create(wifi_password_dialog);
    lv_obj_set_size(controls_row, SCREEN_WIDTH - 16, 45);
    lv_obj_align(controls_row, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_bg_opa(controls_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls_row, 0, 0);
    lv_obj_set_style_pad_all(controls_row, 0, 0);
    lv_obj_remove_flag(controls_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Show password checkbox - left side
    lv_obj_t *show_pass_cb = lv_checkbox_create(controls_row);
    lv_checkbox_set_text(show_pass_cb, "Show");
    lv_obj_set_style_text_color(show_pass_cb, lv_color_black(), 0);
    lv_obj_align(show_pass_cb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(show_pass_cb, [](lv_event_t *e) {
        lv_obj_t *cb = (lv_obj_t *)lv_event_get_target(e);
        bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
        lv_textarea_set_password_mode(wifi_password_textarea, !checked);
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Cancel button - middle
    lv_obj_t *cancel_btn = lv_btn_create(controls_row);
    lv_obj_set_size(cancel_btn, 110, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_CENTER, -65, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_add_event_cb(cancel_btn, wifi_password_cancel_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(cancel_label, UI_FONT, 0);
    lv_obj_center(cancel_label);
    
    // Connect button - right side
    lv_obj_t *connect_btn = lv_btn_create(controls_row);
    lv_obj_set_size(connect_btn, 130, 40);
    lv_obj_align(connect_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(connect_btn, 6, 0);
    lv_obj_add_event_cb(connect_btn, wifi_password_connect_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_set_style_text_color(connect_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(connect_label, UI_FONT, 0);
    lv_obj_center(connect_label);
    
    // Keyboard - use settings for height
    uint16_t kb_height = settings_get_keyboard_height_px();
    ESP_LOGI(TAG, "WiFi dialog keyboard height from settings: %dpx", kb_height);
    if (kb_height < 136 || kb_height > 700) {
        kb_height = 496;  // Fallback to 62%
        ESP_LOGW(TAG, "Invalid keyboard height, using fallback: %dpx", kb_height);
    }
    
    wifi_password_keyboard = lv_keyboard_create(wifi_password_dialog);
    lv_obj_set_size(wifi_password_keyboard, SCREEN_WIDTH, kb_height);
    lv_obj_align(wifi_password_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_password_keyboard, wifi_password_textarea);
    
    // Apply theme (don't set custom font - use default for symbols)
    apply_keyboard_theme(wifi_password_keyboard);
}

// ============ KEYBOARD SETTINGS PAGE ============

void settings_show_keyboard_page(void)
{
    ESP_LOGI(TAG, "Opening Keyboard settings");
    
    // Reset wifi page pointer since we're switching pages
    settings_wifi_page = NULL;
    
    // Delete existing keyboard page only if it's still a valid child
    if (settings_keyboard_page && is_valid_child(settings_keyboard_page)) {
        lv_obj_delete(settings_keyboard_page);
    }
    settings_keyboard_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_keyboard_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_keyboard_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_keyboard_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_keyboard_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_keyboard_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_keyboard_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_keyboard_page, 0, 0);
    lv_obj_set_style_radius(settings_keyboard_page, 0, 0);
    lv_obj_set_style_pad_all(settings_keyboard_page, 10, 0);
    lv_obj_set_flex_flow(settings_keyboard_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_keyboard_page, 15, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_keyboard_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        app_settings_create();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Get current keyboard height from settings
    uint8_t current_height = settings_get_keyboard_height();
    ESP_LOGI(TAG, "Keyboard settings page: current height = %d%%", current_height);
    if (current_height < 17 || current_height > 80) {
        ESP_LOGW(TAG, "Invalid keyboard height %d%%, using default 62%%", current_height);
        current_height = 62;
    }
    
    // Keyboard height setting - Vista style panel
    lv_obj_t *height_cont = lv_obj_create(settings_keyboard_page);
    lv_obj_set_size(height_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(height_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(height_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(height_cont, 1, 0);
    lv_obj_set_style_radius(height_cont, 4, 0);
    lv_obj_set_style_pad_all(height_cont, 15, 0);
    lv_obj_set_flex_flow(height_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(height_cont, 10, 0);
    lv_obj_remove_flag(height_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *height_label = lv_label_create(height_cont);
    lv_label_set_text(height_label, "Keyboard Height");
    lv_obj_set_style_text_color(height_label, lv_color_hex(0x1A5090), 0);
    
    lv_obj_t *height_value = lv_label_create(height_cont);
    char init_buf[32];
    const char *init_size;
    if (current_height < 30) init_size = "Compact";
    else if (current_height < 50) init_size = "Small";
    else if (current_height < 65) init_size = "Medium";
    else init_size = "Large";
    snprintf(init_buf, sizeof(init_buf), "%s (%d%%) - %dpx", init_size, current_height, (800 * current_height) / 100);
    lv_label_set_text(height_value, init_buf);
    lv_obj_set_style_text_color(height_value, lv_color_hex(0x0066CC), 0);
    
    lv_obj_t *height_slider = lv_slider_create(height_cont);
    lv_obj_set_width(height_slider, lv_pct(100));
    lv_slider_set_range(height_slider, 17, 80);  // 17% = 136px (like console keyboard 135px)
    lv_slider_set_value(height_slider, current_height, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(height_slider, lv_color_hex(0x0054E3), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(height_slider, lv_color_hex(0x0054E3), LV_PART_KNOB);
    
    lv_obj_add_event_cb(height_slider, [](lv_event_t *e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        lv_obj_t *cont = lv_obj_get_parent(slider);
        lv_obj_t *value_label = lv_obj_get_child(cont, 1);
        
        int32_t value = lv_slider_get_value(slider);
        char buf[48];
        const char *size_name;
        if (value < 30) size_name = "Compact";  // 17-29% (like console)
        else if (value < 50) size_name = "Small";
        else if (value < 65) size_name = "Medium";
        else size_name = "Large";
        
        int px = (800 * value) / 100;
        snprintf(buf, sizeof(buf), "%s (%ld%%) - %dpx", size_name, (long)value, px);
        lv_label_set_text(value_label, buf);
        
        // Save to settings
        ESP_LOGI("KB_SETTINGS", "Saving keyboard height: %ld%% = %dpx", (long)value, px);
        settings_set_keyboard_height((uint8_t)value);
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Keyboard theme setting - Vista style panel
    lv_obj_t *theme_cont = lv_obj_create(settings_keyboard_page);
    lv_obj_set_size(theme_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(theme_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(theme_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(theme_cont, 1, 0);
    lv_obj_set_style_radius(theme_cont, 4, 0);
    lv_obj_set_style_pad_all(theme_cont, 15, 0);
    lv_obj_set_flex_flow(theme_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(theme_cont, 10, 0);
    lv_obj_remove_flag(theme_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *theme_label = lv_label_create(theme_cont);
    lv_label_set_text(theme_label, "Keyboard Theme");
    lv_obj_set_style_text_color(theme_label, lv_color_hex(0x1A5090), 0);
    
    // Theme buttons row
    lv_obj_t *theme_row = lv_obj_create(theme_cont);
    lv_obj_set_size(theme_row, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(theme_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(theme_row, 0, 0);
    lv_obj_set_style_pad_all(theme_row, 0, 0);
    lv_obj_remove_flag(theme_row, LV_OBJ_FLAG_SCROLLABLE);
    
    keyboard_theme_t current_theme = settings_get_keyboard_theme();
    
    // Dark theme button
    lv_obj_t *dark_btn = lv_btn_create(theme_row);
    lv_obj_set_size(dark_btn, 120, 45);
    lv_obj_align(dark_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(dark_btn, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(dark_btn, current_theme == KEYBOARD_THEME_DARK ? lv_color_hex(0x00FF00) : lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(dark_btn, current_theme == KEYBOARD_THEME_DARK ? 3 : 1, 0);
    lv_obj_set_style_radius(dark_btn, 6, 0);
    lv_obj_add_event_cb(dark_btn, [](lv_event_t *e) {
        settings_set_keyboard_theme(KEYBOARD_THEME_DARK);
        settings_show_keyboard_page();  // Refresh
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *dark_label = lv_label_create(dark_btn);
    lv_label_set_text(dark_label, "Dark");
    lv_obj_set_style_text_color(dark_label, lv_color_white(), 0);
    lv_obj_center(dark_label);
    
    // Light theme button
    lv_obj_t *light_btn = lv_btn_create(theme_row);
    lv_obj_set_size(light_btn, 120, 45);
    lv_obj_align(light_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(light_btn, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_border_color(light_btn, current_theme == KEYBOARD_THEME_LIGHT ? lv_color_hex(0x00FF00) : lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(light_btn, current_theme == KEYBOARD_THEME_LIGHT ? 3 : 1, 0);
    lv_obj_set_style_radius(light_btn, 6, 0);
    lv_obj_add_event_cb(light_btn, [](lv_event_t *e) {
        settings_set_keyboard_theme(KEYBOARD_THEME_LIGHT);
        settings_show_keyboard_page();  // Refresh
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *light_label = lv_label_create(light_btn);
    lv_label_set_text(light_label, "Light");
    lv_obj_set_style_text_color(light_label, lv_color_black(), 0);
    lv_obj_center(light_label);
    
    // Info text
    lv_obj_t *info = lv_label_create(settings_keyboard_page);
    lv_label_set_text(info, "Settings apply to all keyboards in the system.");
    lv_obj_set_style_text_color(info, lv_color_hex(0x666666), 0);
    lv_obj_set_width(info, lv_pct(100));
}


// Reset settings page pointers - call this when app_window is closed
void settings_reset_pages(void)
{
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    settings_wallpaper_page = NULL;
    settings_time_page = NULL;
}

// Apply keyboard theme from settings
void apply_keyboard_theme(lv_obj_t *keyboard)
{
    if (!keyboard) return;
    
    keyboard_theme_t theme = settings_get_keyboard_theme();
    
    // Make keyboard fully opaque (not transparent like default)
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    
    // DON'T set custom font for keyboard items - use default LVGL font
    // which contains FontAwesome symbols (checkmark, backspace, etc.)
    // The text_font is set separately where needed, but NOT for LV_PART_ITEMS
    
    if (theme == KEYBOARD_THEME_DARK) {
        // Dark theme (matches console keyboard style)
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x333333), LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
        lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
        lv_obj_set_style_radius(keyboard, 4, LV_PART_ITEMS);
    } else {
        // Light theme
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(0xE8E8E8), 0);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(0x000000), LV_PART_ITEMS);
        lv_obj_set_style_border_color(keyboard, lv_color_hex(0xCCCCCC), LV_PART_ITEMS);
        lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
        lv_obj_set_style_radius(keyboard, 4, LV_PART_ITEMS);
    }
}


// ============ PERSONALIZATION SETTINGS PAGE ============

// Include assets for wallpaper previews
#include "assets.h"

// Forward declaration for UI style change
extern void win32_recreate_ui(void);

static void wallpaper_item_clicked(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Wallpaper selected: %d", index);
    win32_set_wallpaper(index);
    
    // Refresh page to update selection
    settings_show_wallpaper_page();
}

static void ui_style_changed(lv_event_t *e)
{
    ui_style_t style = (ui_style_t)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "UI style changed to: %d", style);
    settings_set_ui_style(style);
    
    // Refresh page to show selection
    settings_show_wallpaper_page();
}

void settings_show_wallpaper_page(void)
{
    ESP_LOGI(TAG, "Opening Personalization settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    
    // Delete existing page only if it's still a valid child
    if (settings_wallpaper_page && is_valid_child(settings_wallpaper_page)) {
        lv_obj_delete(settings_wallpaper_page);
    }
    settings_wallpaper_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_wallpaper_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_wallpaper_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_wallpaper_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_wallpaper_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_wallpaper_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_wallpaper_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_wallpaper_page, 0, 0);
    lv_obj_set_style_radius(settings_wallpaper_page, 0, 0);
    lv_obj_set_style_pad_all(settings_wallpaper_page, 10, 0);
    lv_obj_set_flex_flow(settings_wallpaper_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_wallpaper_page, 10, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_wallpaper_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        app_settings_create();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Title - Personalization
    lv_obj_t *title = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(title, "Personalization");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    
    // ============ UI STYLE SECTION ============
    lv_obj_t *style_header = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(style_header, "UI Style (requires restart)");
    lv_obj_set_style_text_color(style_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(style_header, UI_FONT, 0);
    
    // Style buttons container
    lv_obj_t *style_cont = lv_obj_create(settings_wallpaper_page);
    lv_obj_set_size(style_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(style_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(style_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(style_cont, 1, 0);
    lv_obj_set_style_radius(style_cont, 4, 0);
    lv_obj_set_style_pad_all(style_cont, 8, 0);
    lv_obj_remove_flag(style_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(style_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(style_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    ui_style_t current_style = settings_get_ui_style();
    
    // Win7 style button
    lv_obj_t *win7_btn = lv_btn_create(style_cont);
    lv_obj_set_size(win7_btn, 100, 40);
    lv_obj_set_style_bg_color(win7_btn, current_style == UI_STYLE_WIN7 ? lv_color_hex(0x4A90D9) : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(win7_btn, 4, 0);
    lv_obj_add_event_cb(win7_btn, ui_style_changed, LV_EVENT_CLICKED, (void*)(intptr_t)UI_STYLE_WIN7);
    
    lv_obj_t *win7_label = lv_label_create(win7_btn);
    lv_label_set_text(win7_label, "Win7");
    lv_obj_set_style_text_color(win7_label, current_style == UI_STYLE_WIN7 ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_text_font(win7_label, UI_FONT, 0);
    lv_obj_center(win7_label);
    
    // WinXP style button
    lv_obj_t *winxp_btn = lv_btn_create(style_cont);
    lv_obj_set_size(winxp_btn, 100, 40);
    lv_obj_set_style_bg_color(winxp_btn, current_style == UI_STYLE_WINXP ? lv_color_hex(0x0A246A) : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(winxp_btn, 4, 0);
    lv_obj_add_event_cb(winxp_btn, ui_style_changed, LV_EVENT_CLICKED, (void*)(intptr_t)UI_STYLE_WINXP);
    
    lv_obj_t *winxp_label = lv_label_create(winxp_btn);
    lv_label_set_text(winxp_label, "WinXP");
    lv_obj_set_style_text_color(winxp_label, current_style == UI_STYLE_WINXP ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_text_font(winxp_label, UI_FONT, 0);
    lv_obj_center(winxp_label);
    
    // Win11 style button
    lv_obj_t *win11_btn = lv_btn_create(style_cont);
    lv_obj_set_size(win11_btn, 100, 40);
    lv_obj_set_style_bg_color(win11_btn, current_style == UI_STYLE_WIN11 ? lv_color_hex(0x202020) : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(win11_btn, 4, 0);
    lv_obj_add_event_cb(win11_btn, ui_style_changed, LV_EVENT_CLICKED, (void*)(intptr_t)UI_STYLE_WIN11);
    
    lv_obj_t *win11_label = lv_label_create(win11_btn);
    lv_label_set_text(win11_label, "Win11");
    lv_obj_set_style_text_color(win11_label, current_style == UI_STYLE_WIN11 ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_text_font(win11_label, UI_FONT, 0);
    lv_obj_center(win11_label);
    
    // ============ WALLPAPER SECTION ============
    lv_obj_t *wallpaper_header = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(wallpaper_header, "Wallpaper");
    lv_obj_set_style_text_color(wallpaper_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(wallpaper_header, UI_FONT, 0);
    
    // Current wallpaper info - Vista style panel
    int current_idx = win32_get_wallpaper_index();
    char current_info[64];
    snprintf(current_info, sizeof(current_info), "Current: %s", wallpapers[current_idx].name);
    
    lv_obj_t *current_cont = lv_obj_create(settings_wallpaper_page);
    lv_obj_set_size(current_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(current_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(current_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(current_cont, 1, 0);
    lv_obj_set_style_radius(current_cont, 4, 0);
    lv_obj_set_style_pad_all(current_cont, 10, 0);
    lv_obj_remove_flag(current_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *current_label = lv_label_create(current_cont);
    lv_label_set_text(current_label, current_info);
    lv_obj_set_style_text_color(current_label, lv_color_hex(0x008800), 0);
    
    // Wallpaper grid container - white background
    lv_obj_t *grid = lv_obj_create(settings_wallpaper_page);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(grid, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(grid, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(grid, 1, 0);
    lv_obj_set_style_radius(grid, 4, 0);
    lv_obj_set_style_pad_all(grid, 8, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    
    // Add wallpaper previews
    int wallpaper_count = win32_get_wallpaper_count();
    for (int i = 0; i < wallpaper_count; i++) {
        // Wallpaper item container - Vista style
        lv_obj_t *item = lv_obj_create(grid);
        lv_obj_set_size(item, 140, 180);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_border_width(item, i == current_idx ? 3 : 1, 0);
        lv_obj_set_style_border_color(item, i == current_idx ? lv_color_hex(0x4A90D9) : lv_color_hex(0x7EB4EA), 0);
        lv_obj_set_style_radius(item, 6, 0);
        lv_obj_set_style_pad_all(item, 5, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD4E4F7), LV_STATE_PRESSED);
        
        // Preview image (scaled down)
        lv_obj_t *preview = lv_image_create(item);
        lv_image_set_src(preview, wallpapers[i].image);
        lv_image_set_scale(preview, 64);  // Scale to ~25% (256/1000)
        lv_obj_align(preview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_remove_flag(preview, LV_OBJ_FLAG_CLICKABLE);
        
        // Wallpaper name
        lv_obj_t *name_label = lv_label_create(item);
        lv_label_set_text(name_label, wallpapers[i].name);
        lv_obj_set_style_text_color(name_label, lv_color_black(), 0);
        lv_obj_set_style_text_font(name_label, UI_FONT, 0);
        lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_remove_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
        
        // Selected indicator
        if (i == current_idx) {
            lv_obj_t *check = lv_label_create(item);
            lv_label_set_text(check, "OK");
            lv_obj_set_style_text_color(check, lv_color_hex(0x00AA00), 0);
            lv_obj_align(check, LV_ALIGN_TOP_RIGHT, -5, 5);
            lv_obj_remove_flag(check, LV_OBJ_FLAG_CLICKABLE);
        }
        
        // Click handler
        lv_obj_add_event_cb(item, wallpaper_item_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    
    // ============ DESKTOP GRID SECTION ============
    lv_obj_t *grid_header = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(grid_header, "Desktop Grid");
    lv_obj_set_style_text_color(grid_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(grid_header, UI_FONT, 0);
    
    // Grid settings container
    lv_obj_t *grid_cont = lv_obj_create(settings_wallpaper_page);
    lv_obj_set_size(grid_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(grid_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(grid_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(grid_cont, 1, 0);
    lv_obj_set_style_radius(grid_cont, 4, 0);
    lv_obj_set_style_pad_all(grid_cont, 10, 0);
    lv_obj_remove_flag(grid_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(grid_cont, 8, 0);
    
    uint8_t current_cols = settings_get_desktop_grid_cols();
    uint8_t current_rows = settings_get_desktop_grid_rows();
    
    // Columns setting
    lv_obj_t *cols_row = lv_obj_create(grid_cont);
    lv_obj_set_size(cols_row, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(cols_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cols_row, 0, 0);
    lv_obj_set_style_pad_all(cols_row, 0, 0);
    lv_obj_remove_flag(cols_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *cols_label = lv_label_create(cols_row);
    lv_label_set_text(cols_label, "Columns:");
    lv_obj_set_style_text_color(cols_label, lv_color_black(), 0);
    lv_obj_align(cols_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    static lv_obj_t *cols_value_label;
    cols_value_label = lv_label_create(cols_row);
    char cols_buf[8];
    snprintf(cols_buf, sizeof(cols_buf), "%d", current_cols);
    lv_label_set_text(cols_value_label, cols_buf);
    lv_obj_set_style_text_color(cols_value_label, lv_color_hex(0x0066CC), 0);
    lv_obj_align(cols_value_label, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t *cols_minus = lv_btn_create(cols_row);
    lv_obj_set_size(cols_minus, 40, 30);
    lv_obj_align(cols_minus, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_obj_set_style_bg_color(cols_minus, lv_color_hex(0x4A90D9), 0);
    lv_obj_add_event_cb(cols_minus, [](lv_event_t *e) {
        uint8_t cols = settings_get_desktop_grid_cols();
        uint8_t rows = settings_get_desktop_grid_rows();
        if (cols > 2) {
            settings_set_desktop_grid(cols - 1, rows);
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", cols - 1);
            lv_label_set_text(cols_value_label, buf);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cols_minus_lbl = lv_label_create(cols_minus);
    lv_label_set_text(cols_minus_lbl, "-");
    lv_obj_set_style_text_color(cols_minus_lbl, lv_color_white(), 0);
    lv_obj_center(cols_minus_lbl);
    
    lv_obj_t *cols_plus = lv_btn_create(cols_row);
    lv_obj_set_size(cols_plus, 40, 30);
    lv_obj_align(cols_plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(cols_plus, lv_color_hex(0x4A90D9), 0);
    lv_obj_add_event_cb(cols_plus, [](lv_event_t *e) {
        uint8_t cols = settings_get_desktop_grid_cols();
        uint8_t rows = settings_get_desktop_grid_rows();
        if (cols < 6) {
            settings_set_desktop_grid(cols + 1, rows);
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", cols + 1);
            lv_label_set_text(cols_value_label, buf);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cols_plus_lbl = lv_label_create(cols_plus);
    lv_label_set_text(cols_plus_lbl, "+");
    lv_obj_set_style_text_color(cols_plus_lbl, lv_color_white(), 0);
    lv_obj_center(cols_plus_lbl);
    
    // Rows setting
    lv_obj_t *rows_row = lv_obj_create(grid_cont);
    lv_obj_set_size(rows_row, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(rows_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rows_row, 0, 0);
    lv_obj_set_style_pad_all(rows_row, 0, 0);
    lv_obj_remove_flag(rows_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *rows_label = lv_label_create(rows_row);
    lv_label_set_text(rows_label, "Rows:");
    lv_obj_set_style_text_color(rows_label, lv_color_black(), 0);
    lv_obj_align(rows_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    static lv_obj_t *rows_value_label;
    rows_value_label = lv_label_create(rows_row);
    char rows_buf[8];
    snprintf(rows_buf, sizeof(rows_buf), "%d", current_rows);
    lv_label_set_text(rows_value_label, rows_buf);
    lv_obj_set_style_text_color(rows_value_label, lv_color_hex(0x0066CC), 0);
    lv_obj_align(rows_value_label, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t *rows_minus = lv_btn_create(rows_row);
    lv_obj_set_size(rows_minus, 40, 30);
    lv_obj_align(rows_minus, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_obj_set_style_bg_color(rows_minus, lv_color_hex(0x4A90D9), 0);
    lv_obj_add_event_cb(rows_minus, [](lv_event_t *e) {
        uint8_t cols = settings_get_desktop_grid_cols();
        uint8_t rows = settings_get_desktop_grid_rows();
        if (rows > 3) {
            settings_set_desktop_grid(cols, rows - 1);
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", rows - 1);
            lv_label_set_text(rows_value_label, buf);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rows_minus_lbl = lv_label_create(rows_minus);
    lv_label_set_text(rows_minus_lbl, "-");
    lv_obj_set_style_text_color(rows_minus_lbl, lv_color_white(), 0);
    lv_obj_center(rows_minus_lbl);
    
    lv_obj_t *rows_plus = lv_btn_create(rows_row);
    lv_obj_set_size(rows_plus, 40, 30);
    lv_obj_align(rows_plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(rows_plus, lv_color_hex(0x4A90D9), 0);
    lv_obj_add_event_cb(rows_plus, [](lv_event_t *e) {
        uint8_t cols = settings_get_desktop_grid_cols();
        uint8_t rows = settings_get_desktop_grid_rows();
        if (rows < 10) {
            settings_set_desktop_grid(cols, rows + 1);
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", rows + 1);
            lv_label_set_text(rows_value_label, buf);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rows_plus_lbl = lv_label_create(rows_plus);
    lv_label_set_text(rows_plus_lbl, "+");
    lv_obj_set_style_text_color(rows_plus_lbl, lv_color_white(), 0);
    lv_obj_center(rows_plus_lbl);
    
    // Reset icons button
    lv_obj_t *reset_icons_btn = lv_btn_create(grid_cont);
    lv_obj_set_size(reset_icons_btn, lv_pct(100), 36);
    lv_obj_set_style_bg_color(reset_icons_btn, lv_color_hex(0xCC4444), 0);
    lv_obj_set_style_radius(reset_icons_btn, 4, 0);
    lv_obj_add_event_cb(reset_icons_btn, [](lv_event_t *e) {
        settings_clear_icon_positions();
        ESP_LOGI("SETTINGS", "Icon positions reset - restart to apply");
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *reset_icons_lbl = lv_label_create(reset_icons_btn);
    lv_label_set_text(reset_icons_lbl, "Reset Icon Positions");
    lv_obj_set_style_text_color(reset_icons_lbl, lv_color_white(), 0);
    lv_obj_center(reset_icons_lbl);
    
    lv_obj_t *grid_note = lv_label_create(grid_cont);
    lv_label_set_text(grid_note, "Grid changes require restart");
    lv_obj_set_style_text_color(grid_note, lv_color_hex(0x888888), 0);
    
    // ============ TASKBAR SECTION ============
    lv_obj_t *taskbar_header = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(taskbar_header, "Taskbar");
    lv_obj_set_style_text_color(taskbar_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(taskbar_header, UI_FONT, 0);
    
    // Taskbar settings button
    lv_obj_t *taskbar_btn = lv_btn_create(settings_wallpaper_page);
    lv_obj_set_size(taskbar_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(taskbar_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(taskbar_btn, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(taskbar_btn, 1, 0);
    lv_obj_set_style_radius(taskbar_btn, 4, 0);
    lv_obj_set_style_bg_color(taskbar_btn, lv_color_hex(0xD4E4F7), LV_STATE_PRESSED);
    lv_obj_add_event_cb(taskbar_btn, [](lv_event_t *e) {
        settings_show_taskbar_page();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *taskbar_btn_lbl = lv_label_create(taskbar_btn);
    lv_label_set_text(taskbar_btn_lbl, LV_SYMBOL_LIST " Taskbar Icons Settings");
    lv_obj_set_style_text_color(taskbar_btn_lbl, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(taskbar_btn_lbl, UI_FONT, 0);
    lv_obj_center(taskbar_btn_lbl);
    
    // SD Card section (placeholder for future)
    lv_obj_t *sd_header = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(sd_header, "Custom Wallpapers (SD Card)");
    lv_obj_set_style_text_color(sd_header, lv_color_hex(0x0054E3), 0);
    
    lv_obj_t *sd_info = lv_label_create(settings_wallpaper_page);
    lv_label_set_text(sd_info, "Place JPG files in /wallpapers/ on SD card");
    lv_obj_set_style_text_color(sd_info, lv_color_hex(0x888888), 0);
}


// ============ TIME SETTINGS PAGE ============

void settings_show_time_page(void)
{
    ESP_LOGI(TAG, "Opening Time settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    settings_wallpaper_page = NULL;
    
    // Delete existing page only if it's still a valid child
    if (settings_time_page && is_valid_child(settings_time_page)) {
        lv_obj_delete(settings_time_page);
    }
    settings_time_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_time_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_time_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_time_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_time_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_time_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_time_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_time_page, 0, 0);
    lv_obj_set_style_radius(settings_time_page, 0, 0);
    lv_obj_set_style_pad_all(settings_time_page, 10, 0);
    lv_obj_set_flex_flow(settings_time_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_time_page, 12, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_time_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        app_settings_create();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(settings_time_page);
    lv_label_set_text(title, "Date & Time Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    
    // Current time display - Vista style panel
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char time_buf[64];
    snprintf(time_buf, sizeof(time_buf), "Current: %02d:%02d:%02d", 
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    lv_obj_t *time_cont = lv_obj_create(settings_time_page);
    lv_obj_set_size(time_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(time_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(time_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(time_cont, 1, 0);
    lv_obj_set_style_radius(time_cont, 4, 0);
    lv_obj_set_style_pad_all(time_cont, 12, 0);
    lv_obj_remove_flag(time_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *current_time = lv_label_create(time_cont);
    lv_label_set_text(current_time, time_buf);
    lv_obj_set_style_text_color(current_time, lv_color_hex(0x008800), 0);
    lv_obj_set_style_text_font(current_time, UI_FONT, 0);
    
    // Timezone setting - Vista style panel
    lv_obj_t *tz_cont = lv_obj_create(settings_time_page);
    lv_obj_set_size(tz_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(tz_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(tz_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(tz_cont, 1, 0);
    lv_obj_set_style_radius(tz_cont, 4, 0);
    lv_obj_set_style_pad_all(tz_cont, 15, 0);
    lv_obj_set_flex_flow(tz_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tz_cont, 10, 0);
    lv_obj_remove_flag(tz_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *tz_label = lv_label_create(tz_cont);
    lv_label_set_text(tz_label, "Timezone (UTC offset)");
    lv_obj_set_style_text_color(tz_label, lv_color_black(), 0);
    
    int8_t current_tz = settings_get_timezone();
    char tz_val_buf[32];
    snprintf(tz_val_buf, sizeof(tz_val_buf), "UTC%+d", current_tz);
    
    lv_obj_t *tz_value = lv_label_create(tz_cont);
    lv_label_set_text(tz_value, tz_val_buf);
    lv_obj_set_style_text_color(tz_value, lv_color_hex(0x0054E3), 0);
    
    lv_obj_t *tz_slider = lv_slider_create(tz_cont);
    lv_obj_set_width(tz_slider, lv_pct(100));
    lv_slider_set_range(tz_slider, -12, 14);
    lv_slider_set_value(tz_slider, current_tz, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tz_slider, lv_color_hex(0x0054E3), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tz_slider, lv_color_hex(0x0054E3), LV_PART_KNOB);
    
    lv_obj_add_event_cb(tz_slider, [](lv_event_t *e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        lv_obj_t *cont = lv_obj_get_parent(slider);
        lv_obj_t *value_label = lv_obj_get_child(cont, 1);
        
        int32_t value = lv_slider_get_value(slider);
        char buf[32];
        snprintf(buf, sizeof(buf), "UTC%+ld", (long)value);
        lv_label_set_text(value_label, buf);
        
        // Save timezone and resync time
        settings_set_time(0, (int8_t)value);
        system_time_resync();
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // 24h format toggle
    lv_obj_t *format_cont = lv_obj_create(settings_time_page);
    lv_obj_set_size(format_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(format_cont, lv_color_white(), 0);
    lv_obj_set_style_border_color(format_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(format_cont, 1, 0);
    lv_obj_set_style_radius(format_cont, 8, 0);
    lv_obj_set_style_pad_all(format_cont, 15, 0);
    lv_obj_remove_flag(format_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *format_label = lv_label_create(format_cont);
    lv_label_set_text(format_label, "24-hour format");
    lv_obj_set_style_text_color(format_label, lv_color_black(), 0);
    lv_obj_align(format_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *format_switch = lv_switch_create(format_cont);
    lv_obj_align(format_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(format_switch, lv_color_hex(0x00AA00), (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    
    system_settings_t *s = settings_get_global();
    if (s->time_24h_format) {
        lv_obj_add_state(format_switch, LV_STATE_CHECKED);
    }
    
    lv_obj_add_event_cb(format_switch, [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
        bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
        system_settings_t *s = settings_get_global();
        s->time_24h_format = checked;
        settings_save(s);
        ESP_LOGI("TIME", "24h format: %s", checked ? "ON" : "OFF");
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Common timezones info
    lv_obj_t *tz_info = lv_label_create(settings_time_page);
    lv_label_set_text(tz_info, "Common: UTC+3 Moscow, UTC+0 London\nUTC-5 New York, UTC+8 Beijing");
    lv_obj_set_style_text_color(tz_info, lv_color_hex(0x666666), 0);
    lv_obj_set_width(tz_info, lv_pct(100));
    
    // NTP sync button (placeholder)
    lv_obj_t *sync_btn = lv_btn_create(settings_time_page);
    lv_obj_set_size(sync_btn, lv_pct(100), 45);
    lv_obj_set_style_bg_color(sync_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(sync_btn, 6, 0);
    
    lv_obj_t *sync_label = lv_label_create(sync_btn);
    lv_label_set_text(sync_label, "Sync with Internet (NTP)");
    lv_obj_set_style_text_color(sync_label, lv_color_white(), 0);
    lv_obj_center(sync_label);
    
    lv_obj_add_event_cb(sync_btn, [](lv_event_t *e) {
        ESP_LOGI("TIME", "NTP sync requested (not implemented yet)");
        // TODO: Implement NTP sync when WiFi is connected
    }, LV_EVENT_CLICKED, NULL);
}

// ============ BRIGHTNESS SETTINGS PAGE ============

static lv_obj_t *settings_brightness_page = NULL;

void settings_show_brightness_page(void)
{
    ESP_LOGI(TAG, "Opening Brightness settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    settings_wallpaper_page = NULL;
    settings_time_page = NULL;
    
    if (settings_brightness_page && is_valid_child(settings_brightness_page)) {
        lv_obj_delete(settings_brightness_page);
    }
    settings_brightness_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_brightness_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_brightness_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_brightness_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_brightness_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_brightness_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_brightness_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_brightness_page, 0, 0);
    lv_obj_set_style_radius(settings_brightness_page, 0, 0);
    lv_obj_set_style_pad_all(settings_brightness_page, 10, 0);
    lv_obj_set_flex_flow(settings_brightness_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_brightness_page, 15, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_brightness_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        app_settings_create();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);

    // Title
    lv_obj_t *title = lv_label_create(settings_brightness_page);
    lv_label_set_text(title, "Brightness");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    
    // Get current brightness
    uint8_t current_brightness = settings_get_brightness();
    if (current_brightness < 10) current_brightness = 50;
    
    // Brightness container - Vista style panel
    lv_obj_t *br_cont = lv_obj_create(settings_brightness_page);
    lv_obj_set_size(br_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(br_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(br_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(br_cont, 1, 0);
    lv_obj_set_style_radius(br_cont, 4, 0);
    lv_obj_set_style_pad_all(br_cont, 15, 0);
    lv_obj_set_flex_flow(br_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(br_cont, 10, 0);
    lv_obj_remove_flag(br_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *br_label = lv_label_create(br_cont);
    lv_label_set_text(br_label, "Screen Brightness");
    lv_obj_set_style_text_color(br_label, lv_color_black(), 0);
    
    char br_buf[32];
    snprintf(br_buf, sizeof(br_buf), "%d%%", current_brightness);
    lv_obj_t *br_value = lv_label_create(br_cont);
    lv_label_set_text(br_value, br_buf);
    lv_obj_set_style_text_color(br_value, lv_color_hex(0x4A90D9), 0);
    
    lv_obj_t *br_slider = lv_slider_create(br_cont);
    lv_obj_set_width(br_slider, lv_pct(100));
    lv_slider_set_range(br_slider, 10, 100);
    lv_slider_set_value(br_slider, current_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(br_slider, lv_color_hex(0x4A90D9), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(br_slider, lv_color_hex(0x4A90D9), LV_PART_KNOB);

    lv_obj_add_event_cb(br_slider, [](lv_event_t *e) {
        lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
        lv_obj_t *cont = lv_obj_get_parent(slider);
        lv_obj_t *value_label = lv_obj_get_child(cont, 1);
        
        int32_t value = lv_slider_get_value(slider);
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld%%", (long)value);
        lv_label_set_text(value_label, buf);
        
        // Apply brightness immediately
        hw_backlight_set((uint8_t)value);
        settings_set_brightness((uint8_t)value);
        ESP_LOGI("BRIGHTNESS", "Set to %ld%%", (long)value);
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Info
    lv_obj_t *info = lv_label_create(settings_brightness_page);
    lv_label_set_text(info, "Adjust screen brightness.\nLower values save battery.");
    lv_obj_set_style_text_color(info, lv_color_hex(0x666666), 0);
}

// ============ BLUETOOTH SETTINGS PAGE ============

#include "bluetooth_transfer.h"

static lv_obj_t *settings_bluetooth_page = NULL;
static lv_obj_t *bt_status_label = NULL;
static lv_obj_t *bt_mac_label = NULL;
static lv_obj_t *bt_connected_label = NULL;
static lv_timer_t *bt_status_timer = NULL;

static void bt_status_timer_cb(lv_timer_t *timer) {
    if (!bt_status_label || !bt_mac_label || !bt_connected_label) return;
    
    // Update status
    if (bt_is_ready()) {
        if (bt_is_connected()) {
            lv_label_set_text(bt_status_label, "Connected");
            lv_obj_set_style_text_color(bt_status_label, lv_color_hex(0x00AA00), 0);
            
            char conn_text[64];
            snprintf(conn_text, sizeof(conn_text), "Device: %s", bt_get_connected_device());
            lv_label_set_text(bt_connected_label, conn_text);
        } else {
            lv_label_set_text(bt_status_label, "Advertising...");
            lv_obj_set_style_text_color(bt_status_label, lv_color_hex(0x0066CC), 0);
            lv_label_set_text(bt_connected_label, "Waiting for connection");
        }
        
        const char *mac = bt_get_mac_address();
        if (mac && mac[0]) {
            char mac_text[32];
            snprintf(mac_text, sizeof(mac_text), "MAC: %s", mac);
            lv_label_set_text(bt_mac_label, mac_text);
        }
    } else {
        lv_label_set_text(bt_status_label, "Disabled");
        lv_obj_set_style_text_color(bt_status_label, lv_color_hex(0x888888), 0);
        lv_label_set_text(bt_connected_label, "");
        lv_label_set_text(bt_mac_label, "");
    }
}

void settings_show_bluetooth_page(void)
{
    ESP_LOGI(TAG, "Opening Bluetooth settings");
    
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    
    // Stop previous timer
    if (bt_status_timer) {
        lv_timer_delete(bt_status_timer);
        bt_status_timer = NULL;
    }
    
    if (settings_bluetooth_page && is_valid_child(settings_bluetooth_page)) {
        lv_obj_delete(settings_bluetooth_page);
    }
    settings_bluetooth_page = NULL;
    bt_status_label = NULL;
    bt_mac_label = NULL;
    bt_connected_label = NULL;
    
    if (!app_window) return;

    settings_bluetooth_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_bluetooth_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_bluetooth_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_bluetooth_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_bluetooth_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_bluetooth_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_bluetooth_page, 0, 0);
    lv_obj_set_style_radius(settings_bluetooth_page, 0, 0);
    lv_obj_set_style_pad_all(settings_bluetooth_page, 10, 0);
    lv_obj_set_flex_flow(settings_bluetooth_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_bluetooth_page, 10, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_bluetooth_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { 
        if (bt_status_timer) {
            lv_timer_delete(bt_status_timer);
            bt_status_timer = NULL;
        }
        app_settings_create(); 
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(settings_bluetooth_page);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    
    // BT Enable toggle - Vista style panel
    lv_obj_t *bt_cont = lv_obj_create(settings_bluetooth_page);
    lv_obj_set_size(bt_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(bt_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(bt_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(bt_cont, 1, 0);
    lv_obj_set_style_radius(bt_cont, 4, 0);
    lv_obj_set_style_pad_all(bt_cont, 15, 0);
    lv_obj_remove_flag(bt_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bt_label = lv_label_create(bt_cont);
    lv_label_set_text(bt_label, "Bluetooth");
    lv_obj_set_style_text_color(bt_label, lv_color_black(), 0);
    lv_obj_align(bt_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *bt_switch = lv_switch_create(bt_cont);
    lv_obj_align(bt_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(bt_switch, lv_color_hex(0x4A90D9), LV_PART_INDICATOR | LV_STATE_CHECKED);
    
    system_settings_t *s = settings_get_global();
    if (s->bt_enabled) lv_obj_add_state(bt_switch, LV_STATE_CHECKED);
    
    lv_obj_add_event_cb(bt_switch, [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
        bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        system_settings_t *s = settings_get_global();
        s->bt_enabled = enabled;
        settings_save(s);
        
        if (enabled) {
            ESP_LOGI("BT", "Enabling Bluetooth...");
            int ret = bt_init();
            if (ret != 0) {
                ESP_LOGE("BT", "Failed to init BT: %d", ret);
            }
        } else {
            ESP_LOGI("BT", "Disabling Bluetooth...");
            bt_deinit();
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Status panel
    lv_obj_t *status_cont = lv_obj_create(settings_bluetooth_page);
    lv_obj_set_size(status_cont, lv_pct(100), 90);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(status_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(status_cont, 1, 0);
    lv_obj_set_style_radius(status_cont, 4, 0);
    lv_obj_set_style_pad_all(status_cont, 12, 0);
    lv_obj_remove_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *status_title = lv_label_create(status_cont);
    lv_label_set_text(status_title, "Status:");
    lv_obj_set_style_text_color(status_title, lv_color_hex(0x666666), 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 0, 0);
    
    bt_status_label = lv_label_create(status_cont);
    lv_label_set_text(bt_status_label, bt_is_ready() ? "Ready" : "Disabled");
    lv_obj_set_style_text_color(bt_status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(bt_status_label, LV_ALIGN_TOP_LEFT, 60, 0);
    
    bt_mac_label = lv_label_create(status_cont);
    lv_label_set_text(bt_mac_label, "");
    lv_obj_set_style_text_color(bt_mac_label, lv_color_hex(0x333333), 0);
    lv_obj_align(bt_mac_label, LV_ALIGN_TOP_LEFT, 0, 22);
    
    bt_connected_label = lv_label_create(status_cont);
    lv_label_set_text(bt_connected_label, "");
    lv_obj_set_style_text_color(bt_connected_label, lv_color_hex(0x333333), 0);
    lv_obj_align(bt_connected_label, LV_ALIGN_TOP_LEFT, 0, 44);
    
    // Device name - Vista style panel
    lv_obj_t *name_cont = lv_obj_create(settings_bluetooth_page);
    lv_obj_set_size(name_cont, lv_pct(100), 70);
    lv_obj_set_style_bg_color(name_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(name_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(name_cont, 1, 0);
    lv_obj_set_style_radius(name_cont, 4, 0);
    lv_obj_set_style_pad_all(name_cont, 15, 0);
    lv_obj_remove_flag(name_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *name_label = lv_label_create(name_cont);
    lv_label_set_text(name_label, "Device Name");
    lv_obj_set_style_text_color(name_label, lv_color_hex(0x666666), 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *name_value = lv_label_create(name_cont);
    lv_label_set_text(name_value, s->bt_name);
    lv_obj_set_style_text_color(name_value, lv_color_black(), 0);
    lv_obj_align(name_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    // Info
    lv_obj_t *info = lv_label_create(settings_bluetooth_page);
    lv_label_set_text(info, "Bluetooth via ESP32-C6 (ESP-Hosted).\nFile transfer service available.");
    lv_obj_set_style_text_color(info, lv_color_hex(0x666666), 0);
    lv_obj_set_width(info, lv_pct(100));
    
    // Start status update timer
    bt_status_timer = lv_timer_create(bt_status_timer_cb, 1000, NULL);
    bt_status_timer_cb(NULL);  // Initial update
}

// ============ STORAGE SETTINGS PAGE ============

static lv_obj_t *settings_storage_page = NULL;

// Storage page colors
#define STORAGE_COLOR_PSRAM     0x4A90D9  // Blue - PSRAM used
#define STORAGE_COLOR_IRAM      0xFF8C00  // Orange - Internal RAM
#define STORAGE_COLOR_LITTLEFS  0x00AA00  // Green - LittleFS
#define STORAGE_COLOR_FIRMWARE  0x9932CC  // Purple - Firmware
#define STORAGE_COLOR_FREE      0x90EE90  // Light green - Free
#define STORAGE_COLOR_SDCARD    0x20B2AA  // Teal - SD Card

// Helper to create legend item for storage page
static void create_storage_legend_item(lv_obj_t *parent, uint32_t color, const char *name, const char *value) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 22);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_radius(dot, 6, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x333333), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 18, 0);
    
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, lv_color_hex(0x1A5090), 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
}

// Helper to create storage info panel
static lv_obj_t* create_storage_panel(lv_obj_t *parent, const char *title, const char *info, 
                                       int percent, uint32_t bar_color, int height) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(100), height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title_lbl = lv_label_create(panel);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x1A5090), 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *info_lbl = lv_label_create(panel);
    lv_label_set_text(info_lbl, info);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x666666), 0);
    lv_obj_align(info_lbl, LV_ALIGN_TOP_LEFT, 0, 18);
    
    if (percent >= 0) {
        lv_obj_t *bar = lv_bar_create(panel);
        lv_obj_set_size(bar, lv_pct(100), 10);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_bar_set_value(bar, percent, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xDDDDDD), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_color), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 5, 0);
        lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    }
    
    return panel;
}

void settings_show_storage_page(void)
{
    ESP_LOGI(TAG, "Opening Storage settings");
    
    if (settings_storage_page && is_valid_child(settings_storage_page)) {
        lv_obj_delete(settings_storage_page);
    }
    settings_storage_page = NULL;
    
    if (!app_window) return;
    
    settings_storage_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_storage_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_storage_page, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(settings_storage_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_storage_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_storage_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_storage_page, 0, 0);
    lv_obj_set_style_radius(settings_storage_page, 0, 0);
    lv_obj_set_style_pad_all(settings_storage_page, 8, 0);
    lv_obj_set_flex_flow(settings_storage_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_storage_page, 6, 0);

    // Back button - Vista style (same as WiFi page)
    lv_obj_t *back_btn = lv_obj_create(settings_storage_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { app_settings_create(); }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // ===== Gather all memory info =====
    // LittleFS
    hw_littlefs_info_t lfs_info;
    hw_littlefs_get_info(&lfs_info);
    
    // SD Card
    hw_sdcard_info_t sd_info;
    bool sd_ok = hw_sdcard_get_info(&sd_info);
    
    // PSRAM
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_used = psram_total - psram_free;
    
    // Internal RAM (DRAM)
    size_t iram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t iram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t iram_used = iram_total - iram_free;
    
    // ===== Top row: Pie chart + Legend =====
    lv_obj_t *top_row = lv_obj_create(settings_storage_page);
    lv_obj_set_size(top_row, lv_pct(100), 160);
    lv_obj_set_style_bg_color(top_row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(top_row, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(top_row, 1, 0);
    lv_obj_set_style_radius(top_row, 4, 0);
    lv_obj_set_style_pad_all(top_row, 8, 0);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *mem_title = lv_label_create(top_row);
    lv_label_set_text(mem_title, "Memory Overview");
    lv_obj_set_style_text_color(mem_title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(mem_title, UI_FONT, 0);
    lv_obj_align(mem_title, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Pie chart using arcs (left side)
    int pie_size = 110;
    int pie_x = 60;
    int pie_y = 90;
    
    // Calculate percentages for pie chart (RAM distribution)
    size_t total_ram = psram_total + iram_total;
    int psram_pct = (total_ram > 0) ? (psram_used * 100 / total_ram) : 0;
    int iram_pct = (total_ram > 0) ? (iram_used * 100 / total_ram) : 0;
    int free_pct = 100 - psram_pct - iram_pct;
    
    // Background circle
    lv_obj_t *pie_bg = lv_arc_create(top_row);
    lv_obj_set_size(pie_bg, pie_size, pie_size);
    lv_obj_set_pos(pie_bg, pie_x - pie_size/2, pie_y - pie_size/2 + 10);
    lv_arc_set_rotation(pie_bg, 270);
    lv_arc_set_bg_angles(pie_bg, 0, 360);
    lv_arc_set_value(pie_bg, 0);
    lv_obj_set_style_arc_width(pie_bg, 25, LV_PART_MAIN);
    lv_obj_set_style_arc_color(pie_bg, lv_color_hex(STORAGE_COLOR_FREE), LV_PART_MAIN);
    lv_obj_remove_style(pie_bg, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(pie_bg, LV_OBJ_FLAG_CLICKABLE);
    
    // PSRAM used arc (blue)
    if (psram_pct > 0) {
        lv_obj_t *arc_psram = lv_arc_create(top_row);
        lv_obj_set_size(arc_psram, pie_size, pie_size);
        lv_obj_set_pos(arc_psram, pie_x - pie_size/2, pie_y - pie_size/2 + 10);
        lv_arc_set_rotation(arc_psram, 270);
        lv_arc_set_bg_angles(arc_psram, 0, 0);
        lv_arc_set_angles(arc_psram, 0, (psram_pct * 360) / 100);
        lv_obj_set_style_arc_width(arc_psram, 25, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_psram, lv_color_hex(STORAGE_COLOR_PSRAM), LV_PART_INDICATOR);
        lv_obj_remove_style(arc_psram, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(arc_psram, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Internal RAM arc (orange)
    if (iram_pct > 0) {
        lv_obj_t *arc_iram = lv_arc_create(top_row);
        lv_obj_set_size(arc_iram, pie_size, pie_size);
        lv_obj_set_pos(arc_iram, pie_x - pie_size/2, pie_y - pie_size/2 + 10);
        lv_arc_set_rotation(arc_iram, 270);
        lv_arc_set_bg_angles(arc_iram, 0, 0);
        int start_angle = (psram_pct * 360) / 100;
        int end_angle = start_angle + (iram_pct * 360) / 100;
        lv_arc_set_angles(arc_iram, start_angle, end_angle);
        lv_obj_set_style_arc_width(arc_iram, 25, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_iram, lv_color_hex(STORAGE_COLOR_IRAM), LV_PART_INDICATOR);
        lv_obj_remove_style(arc_iram, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(arc_iram, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Center text
    lv_obj_t *center_lbl = lv_label_create(top_row);
    char center_buf[16];
    snprintf(center_buf, sizeof(center_buf), "%d%%", 100 - free_pct);
    lv_label_set_text(center_lbl, center_buf);
    lv_obj_set_style_text_color(center_lbl, lv_color_hex(0x1A5090), 0);
    lv_obj_set_pos(center_lbl, pie_x - 15, pie_y - 5 + 10);
    
    // Legend (right side)
    lv_obj_t *legend = lv_obj_create(top_row);
    lv_obj_set_size(legend, 320, 130);
    lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, 0, 18);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(legend, 2, 0);
    lv_obj_remove_flag(legend, LV_OBJ_FLAG_SCROLLABLE);
    
    char buf[48];
    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(psram_used / 1024));
    create_storage_legend_item(legend, STORAGE_COLOR_PSRAM, "PSRAM Used", buf);
    
    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(iram_used / 1024));
    create_storage_legend_item(legend, STORAGE_COLOR_IRAM, "Internal RAM", buf);
    
    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)((psram_free + iram_free) / 1024));
    create_storage_legend_item(legend, STORAGE_COLOR_FREE, "Free RAM", buf);
    
    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(lfs_info.used_bytes / 1024));
    create_storage_legend_item(legend, STORAGE_COLOR_LITTLEFS, "LittleFS", buf);
    
    if (sd_ok && sd_info.mounted) {
        snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(sd_info.used_bytes / (1024*1024)));
        create_storage_legend_item(legend, STORAGE_COLOR_SDCARD, "SD Card", buf);
    }
    
    // ===== PSRAM Panel =====
    int psram_pct_bar = (psram_total > 0) ? (psram_used * 100 / psram_total) : 0;
    snprintf(buf, sizeof(buf), "Used: %lu KB / %lu KB (%d%%)", 
             (unsigned long)(psram_used / 1024), (unsigned long)(psram_total / 1024), psram_pct_bar);
    create_storage_panel(settings_storage_page, "PSRAM (External)", buf, psram_pct_bar, STORAGE_COLOR_PSRAM, 55);
    
    // ===== Internal RAM Panel =====
    int iram_pct_bar = (iram_total > 0) ? (iram_used * 100 / iram_total) : 0;
    snprintf(buf, sizeof(buf), "Used: %lu KB / %lu KB (%d%%)", 
             (unsigned long)(iram_used / 1024), (unsigned long)(iram_total / 1024), iram_pct_bar);
    create_storage_panel(settings_storage_page, "Internal RAM (DRAM)", buf, iram_pct_bar, STORAGE_COLOR_IRAM, 55);
    
    // ===== LittleFS Panel =====
    int lfs_pct = (lfs_info.total_bytes > 0) ? (lfs_info.used_bytes * 100 / lfs_info.total_bytes) : 0;
    snprintf(buf, sizeof(buf), "Used: %lu KB / %lu KB (%d%%)", 
             (unsigned long)(lfs_info.used_bytes / 1024), (unsigned long)(lfs_info.total_bytes / 1024), lfs_pct);
    create_storage_panel(settings_storage_page, "LittleFS (Data)", buf, lfs_pct, STORAGE_COLOR_LITTLEFS, 55);
    
    // ===== SD Card Panel =====
    if (sd_ok && sd_info.mounted) {
        int sd_pct = (sd_info.total_bytes > 0) ? (sd_info.used_bytes * 100 / sd_info.total_bytes) : 0;
        snprintf(buf, sizeof(buf), "Used: %lu MB / %lu MB (%d%%)", 
                 (unsigned long)(sd_info.used_bytes / (1024*1024)), 
                 (unsigned long)(sd_info.total_bytes / (1024*1024)), sd_pct);
        create_storage_panel(settings_storage_page, "SD Card", buf, sd_pct, STORAGE_COLOR_SDCARD, 55);
    } else {
        lv_obj_t *sd_panel = lv_obj_create(settings_storage_page);
        lv_obj_set_size(sd_panel, lv_pct(100), 45);
        lv_obj_set_style_bg_color(sd_panel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(sd_panel, lv_color_hex(0x7EB4EA), 0);
        lv_obj_set_style_border_width(sd_panel, 1, 0);
        lv_obj_set_style_radius(sd_panel, 4, 0);
        lv_obj_set_style_pad_all(sd_panel, 8, 0);
        lv_obj_remove_flag(sd_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *sd_title = lv_label_create(sd_panel);
        lv_label_set_text(sd_title, "SD Card");
        lv_obj_set_style_text_color(sd_title, lv_color_hex(0x1A5090), 0);
        lv_obj_align(sd_title, LV_ALIGN_TOP_LEFT, 0, 0);
        
        lv_obj_t *sd_status = lv_label_create(sd_panel);
        lv_label_set_text(sd_status, "Not inserted");
        lv_obj_set_style_text_color(sd_status, lv_color_hex(0xFF6666), 0);
        lv_obj_align(sd_status, LV_ALIGN_TOP_LEFT, 0, 18);
    }
    
    // ===== Free Heap Panel =====
    size_t free_heap = esp_get_free_heap_size();
    snprintf(buf, sizeof(buf), "Available: %lu KB", (unsigned long)(free_heap / 1024));
    lv_obj_t *heap_panel = lv_obj_create(settings_storage_page);
    lv_obj_set_size(heap_panel, lv_pct(100), 40);
    lv_obj_set_style_bg_color(heap_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(heap_panel, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(heap_panel, 1, 0);
    lv_obj_set_style_radius(heap_panel, 4, 0);
    lv_obj_set_style_pad_all(heap_panel, 8, 0);
    lv_obj_remove_flag(heap_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *heap_title = lv_label_create(heap_panel);
    lv_label_set_text(heap_title, "Free Heap (Total)");
    lv_obj_set_style_text_color(heap_title, lv_color_hex(0x1A5090), 0);
    lv_obj_align(heap_title, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *heap_val = lv_label_create(heap_panel);
    lv_label_set_text(heap_val, buf);
    lv_obj_set_style_text_color(heap_val, lv_color_hex(0x00AA00), 0);
    lv_obj_align(heap_val, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ============ ABOUT SETTINGS PAGE ============

static lv_obj_t *settings_about_page = NULL;

// Recovery mode trigger - tap ESP-IDF version 5 times within 3 seconds
static uint8_t recovery_tap_count = 0;
static uint64_t recovery_first_tap_time = 0;
#define RECOVERY_TAP_COUNT_REQUIRED 5
#define RECOVERY_TAP_TIMEOUT_MS 3000

// Forward declaration for recovery confirmation dialog
static void show_recovery_confirmation_dialog(void);

static void recovery_tap_handler(lv_event_t *e)
{
    uint64_t now = esp_timer_get_time() / 1000;  // Convert to ms
    
    // Reset counter if timeout expired
    if (recovery_tap_count > 0 && (now - recovery_first_tap_time) > RECOVERY_TAP_TIMEOUT_MS) {
        recovery_tap_count = 0;
        ESP_LOGI(TAG, "Recovery tap timeout, resetting counter");
    }
    
    // First tap - record time
    if (recovery_tap_count == 0) {
        recovery_first_tap_time = now;
    }
    
    recovery_tap_count++;
    ESP_LOGI(TAG, "Recovery tap count: %d/%d", recovery_tap_count, RECOVERY_TAP_COUNT_REQUIRED);
    
    if (recovery_tap_count >= RECOVERY_TAP_COUNT_REQUIRED) {
        recovery_tap_count = 0;
        ESP_LOGW(TAG, "Recovery mode trigger activated!");
        show_recovery_confirmation_dialog();
    }
}

// Recovery confirmation dialog
static lv_obj_t *recovery_confirm_dialog = NULL;

static void recovery_confirm_yes_cb(lv_event_t *e)
{
    if (recovery_confirm_dialog) {
        lv_obj_delete(recovery_confirm_dialog);
        recovery_confirm_dialog = NULL;
    }
    ESP_LOGW(TAG, "User confirmed - rebooting to Recovery Mode");
    recovery_request_reboot();
}

static void recovery_confirm_no_cb(lv_event_t *e)
{
    if (recovery_confirm_dialog) {
        lv_obj_delete(recovery_confirm_dialog);
        recovery_confirm_dialog = NULL;
    }
    ESP_LOGI(TAG, "User cancelled recovery mode");
}

static void show_recovery_confirmation_dialog(void)
{
    if (recovery_confirm_dialog) {
        lv_obj_delete(recovery_confirm_dialog);
    }
    
    // Create modal dialog
    recovery_confirm_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(recovery_confirm_dialog, 320, 180);
    lv_obj_center(recovery_confirm_dialog);
    lv_obj_set_style_bg_color(recovery_confirm_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(recovery_confirm_dialog, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_border_width(recovery_confirm_dialog, 2, 0);
    lv_obj_set_style_radius(recovery_confirm_dialog, 8, 0);
    lv_obj_set_style_shadow_width(recovery_confirm_dialog, 20, 0);
    lv_obj_set_style_shadow_color(recovery_confirm_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(recovery_confirm_dialog, LV_OPA_40, 0);
    lv_obj_remove_flag(recovery_confirm_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(recovery_confirm_dialog);
    lv_label_set_text(title, "Win Recovery");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Message
    lv_obj_t *msg = lv_label_create(recovery_confirm_dialog);
    lv_label_set_text(msg, "Reboot to Recovery Mode?");
    lv_obj_set_style_text_color(msg, lv_color_black(), 0);
    lv_obj_set_style_text_font(msg, UI_FONT, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);
    
    // Yes button
    lv_obj_t *yes_btn = lv_btn_create(recovery_confirm_dialog);
    lv_obj_set_size(yes_btn, 100, 40);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 30, -15);
    lv_obj_set_style_bg_color(yes_btn, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_radius(yes_btn, 4, 0);
    lv_obj_add_event_cb(yes_btn, recovery_confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_color(yes_label, lv_color_white(), 0);
    lv_obj_center(yes_label);
    
    // No button
    lv_obj_t *no_btn = lv_btn_create(recovery_confirm_dialog);
    lv_obj_set_size(no_btn, 100, 40);
    lv_obj_align(no_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -15);
    lv_obj_set_style_bg_color(no_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(no_btn, 4, 0);
    lv_obj_add_event_cb(no_btn, recovery_confirm_no_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_color(no_label, lv_color_white(), 0);
    lv_obj_center(no_label);
}

void settings_show_about_page(void)
{
    ESP_LOGI(TAG, "Opening About page");
    
    if (settings_about_page && is_valid_child(settings_about_page)) {
        lv_obj_delete(settings_about_page);
    }
    settings_about_page = NULL;
    
    if (!app_window) return;
    
    settings_about_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_about_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_about_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_about_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_about_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_about_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_about_page, 0, 0);
    lv_obj_set_style_radius(settings_about_page, 0, 0);
    lv_obj_set_style_pad_all(settings_about_page, 10, 0);
    lv_obj_set_flex_flow(settings_about_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_about_page, 10, 0);

    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_about_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { app_settings_create(); }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Logo/Title area - Vista style blue header
    lv_obj_t *logo_cont = lv_obj_create(settings_about_page);
    lv_obj_set_size(logo_cont, lv_pct(100), 140);
    lv_obj_set_style_bg_color(logo_cont, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(logo_cont, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(logo_cont, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(logo_cont, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(logo_cont, 1, 0);
    lv_obj_set_style_radius(logo_cont, 4, 0);
    lv_obj_remove_flag(logo_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Logo image
    LV_IMG_DECLARE(img_logo);
    lv_obj_t *logo_img = lv_image_create(logo_cont);
    lv_image_set_src(logo_img, &img_logo);
    lv_obj_align(logo_img, LV_ALIGN_LEFT_MID, 15, 0);
    
    // Text container (right of logo)
    lv_obj_t *os_name = lv_label_create(logo_cont);
    lv_label_set_text(os_name, "WinESP32");
    lv_obj_set_style_text_color(os_name, lv_color_white(), 0);
    lv_obj_set_style_text_font(os_name, UI_FONT, 0);
    lv_obj_align(os_name, LV_ALIGN_LEFT_MID, 110, -35);
    
    lv_obj_t *os_desc = lv_label_create(logo_cont);
    lv_label_set_text(os_desc, "The ESP32 Version of Windows");
    lv_obj_set_style_text_color(os_desc, lv_color_hex(0xCCDDFF), 0);
    lv_obj_align(os_desc, LV_ALIGN_LEFT_MID, 110, -10);
    
    lv_obj_t *os_ver = lv_label_create(logo_cont);
    lv_label_set_text(os_ver, "Version: 1.5.2");
    lv_obj_set_style_text_color(os_ver, lv_color_hex(0xAABBFF), 0);
    lv_obj_align(os_ver, LV_ALIGN_LEFT_MID, 110, 15);
    
    lv_obj_t *os_coder = lv_label_create(logo_cont);
    lv_label_set_text(os_coder, "Coder: @ewinnery");
    lv_obj_set_style_text_color(os_coder, lv_color_hex(0x88AAFF), 0);
    lv_obj_align(os_coder, LV_ALIGN_LEFT_MID, 110, 40);

    // Hardware info - Vista style panel
    lv_obj_t *hw_cont = lv_obj_create(settings_about_page);
    lv_obj_set_size(hw_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(hw_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(hw_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(hw_cont, 1, 0);
    lv_obj_set_style_radius(hw_cont, 4, 0);
    lv_obj_set_style_pad_all(hw_cont, 12, 0);
    lv_obj_set_flex_flow(hw_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hw_cont, 8, 0);
    lv_obj_remove_flag(hw_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Info items
    auto add_info = [&](const char *label, const char *value) {
        lv_obj_t *row = lv_obj_create(hw_cont);
        lv_obj_set_size(row, lv_pct(100), 25);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        
        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_color(val, lv_color_black(), 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
    };
    
    add_info("CPU:", "ESP32-P4 @ 400MHz");
    add_info("WiFi/BT:", "ESP32-C6 (ESP-Hosted)");
    add_info("Display:", "480x800 ST7701S");
    add_info("Touch:", "GT911 Capacitive");
    
    // ESP-IDF version - clickable for recovery mode trigger (5 taps in 3 seconds)
    {
        lv_obj_t *row = lv_obj_create(hw_cont);
        lv_obj_set_size(row, lv_pct(100), 25);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xE0E8F0), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, recovery_tap_handler, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "ESP-IDF:");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, esp_get_idf_version());
        lv_obj_set_style_text_color(val, lv_color_black(), 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_remove_flag(val, LV_OBJ_FLAG_CLICKABLE);
    }
    
    char heap_buf[32];
    snprintf(heap_buf, sizeof(heap_buf), "%lu KB", (unsigned long)(esp_get_free_heap_size() / 1024));
    add_info("Free Heap:", heap_buf);
}


// ============ REGION/LOCATION SETTINGS PAGE ============

#include "cities_data.h"

static lv_obj_t *settings_region_page = NULL;
static int selected_city_index = -1;
static bool showing_russian_cities = true;

static void region_city_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const city_info_t *city = showing_russian_cities ? &russian_cities[idx] : &world_cities[idx];
    
    ESP_LOGI(TAG, "City selected: %s (%.4f, %.4f) TZ=%+d", 
             city->name, city->lat, city->lon, city->tz);
    
    // Save location
    settings_set_location(city->name, city->lat, city->lon, city->tz);
    
    // Refresh page
    settings_show_region_page();
}

void settings_show_region_page(void)
{
    ESP_LOGI(TAG, "Opening Region settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    settings_wallpaper_page = NULL;
    settings_time_page = NULL;
    
    if (settings_region_page && is_valid_child(settings_region_page)) {
        lv_obj_delete(settings_region_page);
    }
    settings_region_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_region_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_region_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_region_page, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(settings_region_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_region_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_region_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_region_page, 0, 0);
    lv_obj_set_style_radius(settings_region_page, 0, 0);
    lv_obj_set_style_pad_all(settings_region_page, 10, 0);
    lv_obj_set_flex_flow(settings_region_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_region_page, 8, 0);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(settings_region_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        app_settings_create();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(settings_region_page);
    lv_label_set_text(title, "Location / Region");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    
    // Current location display - Vista style blue header
    location_settings_t *loc = settings_get_location();
    
    lv_obj_t *current_cont = lv_obj_create(settings_region_page);
    lv_obj_set_size(current_cont, lv_pct(100), 70);
    lv_obj_set_style_bg_color(current_cont, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(current_cont, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(current_cont, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(current_cont, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(current_cont, 1, 0);
    lv_obj_set_style_radius(current_cont, 4, 0);
    lv_obj_set_style_pad_all(current_cont, 12, 0);
    lv_obj_remove_flag(current_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *current_label = lv_label_create(current_cont);
    lv_label_set_text(current_label, "Current Location:");
    lv_obj_set_style_text_color(current_label, lv_color_hex(0xAABBFF), 0);
    lv_obj_align(current_label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *current_city = lv_label_create(current_cont);
    if (loc->valid) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s (UTC%+d)", loc->city_name, loc->timezone);
        lv_label_set_text(current_city, buf);
    } else {
        lv_label_set_text(current_city, "Not set");
    }
    lv_obj_set_style_text_color(current_city, lv_color_white(), 0);
    lv_obj_set_style_text_font(current_city, UI_FONT, 0);
    lv_obj_align(current_city, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    // Region tabs (Russia / World) - Vista style
    lv_obj_t *tabs_cont = lv_obj_create(settings_region_page);
    lv_obj_set_size(tabs_cont, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(tabs_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabs_cont, 0, 0);
    lv_obj_set_style_pad_all(tabs_cont, 0, 0);
    lv_obj_remove_flag(tabs_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tabs_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs_cont, 10, 0);
    
    // Russia tab - Vista style button
    lv_obj_t *russia_btn = lv_obj_create(tabs_cont);
    lv_obj_set_size(russia_btn, 150, 36);
    lv_obj_set_style_bg_color(russia_btn, showing_russian_cities ? lv_color_hex(0x4A90D9) : lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_grad_color(russia_btn, showing_russian_cities ? lv_color_hex(0x2A70B9) : lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_grad_dir(russia_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(russia_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(russia_btn, 1, 0);
    lv_obj_set_style_radius(russia_btn, 4, 0);
    lv_obj_add_flag(russia_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(russia_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(russia_btn, [](lv_event_t *e) {
        showing_russian_cities = true;
        settings_show_region_page();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *russia_label = lv_label_create(russia_btn);
    lv_label_set_text(russia_label, "Russia");
    lv_obj_set_style_text_color(russia_label, lv_color_white(), 0);
    lv_obj_center(russia_label);
    lv_obj_remove_flag(russia_label, LV_OBJ_FLAG_CLICKABLE);
    
    // World tab - Vista style button
    lv_obj_t *world_btn = lv_obj_create(tabs_cont);
    lv_obj_set_size(world_btn, 150, 36);
    lv_obj_set_style_bg_color(world_btn, !showing_russian_cities ? lv_color_hex(0x4A90D9) : lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_grad_color(world_btn, !showing_russian_cities ? lv_color_hex(0x2A70B9) : lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_grad_dir(world_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(world_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(world_btn, 1, 0);
    lv_obj_set_style_radius(world_btn, 4, 0);
    lv_obj_add_flag(world_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(world_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(world_btn, [](lv_event_t *e) {
        showing_russian_cities = false;
        settings_show_region_page();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *world_label = lv_label_create(world_btn);
    lv_label_set_text(world_label, "World");
    lv_obj_set_style_text_color(world_label, lv_color_white(), 0);
    lv_obj_center(world_label);
    lv_obj_remove_flag(world_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Cities list
    lv_obj_t *cities_list = lv_obj_create(settings_region_page);
    lv_obj_set_size(cities_list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cities_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cities_list, 0, 0);
    lv_obj_set_style_pad_all(cities_list, 0, 0);
    lv_obj_set_flex_flow(cities_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cities_list, 5, 0);
    
    // Add cities
    int count = showing_russian_cities ? RUSSIAN_CITIES_COUNT : WORLD_CITIES_COUNT;
    const city_info_t *cities = showing_russian_cities ? russian_cities : world_cities;
    
    for (int i = 0; i < count; i++) {
        // Vista style city item
        lv_obj_t *item = lv_obj_create(cities_list);
        lv_obj_set_size(item, lv_pct(100), 50);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0x7EB4EA), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 10, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD4E4F7), LV_STATE_PRESSED);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Highlight current city
        if (loc->valid && strcmp(cities[i].name, loc->city_name) == 0) {
            lv_obj_set_style_border_color(item, lv_color_hex(0x4A90D9), 0);
            lv_obj_set_style_border_width(item, 2, 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(0xE8F0FF), 0);
        }
        
        // City name
        lv_obj_t *name_label = lv_label_create(item);
        lv_label_set_text(name_label, cities[i].name);
        lv_obj_set_style_text_color(name_label, lv_color_black(), 0);
        lv_obj_set_style_text_font(name_label, UI_FONT, 0);
        lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
        
        // Timezone
        char tz_buf[16];
        snprintf(tz_buf, sizeof(tz_buf), "UTC%+d", cities[i].tz);
        lv_obj_t *tz_label = lv_label_create(item);
        lv_label_set_text(tz_label, tz_buf);
        lv_obj_set_style_text_color(tz_label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(tz_label, UI_FONT, 0);
        lv_obj_align(tz_label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_remove_flag(tz_label, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_add_event_cb(item, region_city_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}


// ============ USER SETTINGS PAGE ============

static lv_obj_t *settings_user_page = NULL;
static lv_obj_t *user_name_textarea = NULL;
static lv_obj_t *user_password_textarea = NULL;
static lv_obj_t *user_keyboard = NULL;
static lv_obj_t *user_input_dialog = NULL;
static lv_obj_t *user_input_textarea = NULL;
static bool user_input_is_password = false;
static bool user_input_is_pin = false;  // For PIN-only input

// Temporary settings (applied on "Apply" button)
static char temp_username[32] = {0};
static uint32_t temp_avatar_color = 0;
static char temp_password[32] = {0};
static char temp_pin[8] = {0};  // Separate PIN storage
static lock_type_t temp_lock_type = LOCK_TYPE_SLIDE;
static bool temp_settings_changed = false;

// Forward declarations for user input dialog
static void show_user_input_dialog(const char *title, const char *current_value, bool is_password);
static void show_pin_input_dialog(const char *title, const char *current_value);
static void user_input_save_clicked(lv_event_t *e);
static void user_input_cancel_clicked(lv_event_t *e);

// Preset avatar colors
static const uint32_t avatar_colors[] = {
    0x4A90D9,  // Blue (default)
    0xE74C3C,  // Red
    0x27AE60,  // Green
    0xF39C12,  // Orange
    0x9B59B6,  // Purple
    0x1ABC9C,  // Teal
    0xE91E63,  // Pink
    0x607D8B,  // Gray
};
#define AVATAR_COLORS_COUNT 8

// Apply all temporary settings
static void user_apply_settings(lv_event_t *e)
{
    ESP_LOGI(TAG, "Applying user settings...");
    
    // Save username if changed
    if (strlen(temp_username) > 0 && strcmp(temp_username, settings_get_username()) != 0) {
        settings_set_username(temp_username);
        ESP_LOGI(TAG, "Username saved: %s", temp_username);
    }
    
    // Save avatar color if changed
    if (temp_avatar_color != settings_get_avatar_color()) {
        settings_set_avatar_color(temp_avatar_color);
        ESP_LOGI(TAG, "Avatar color saved: 0x%06X", (unsigned int)temp_avatar_color);
    }
    
    // Save password or PIN based on lock type
    if (temp_lock_type == LOCK_TYPE_PIN) {
        // Save PIN
        if (strlen(temp_pin) > 0) {
            settings_set_password(temp_pin);
            ESP_LOGI(TAG, "PIN set (%d digits)", (int)strlen(temp_pin));
        }
    } else if (temp_lock_type == LOCK_TYPE_PASSWORD) {
        // Save password
        if (strlen(temp_password) > 0 || settings_has_password()) {
            settings_set_password(temp_password);
            ESP_LOGI(TAG, "Password %s", strlen(temp_password) > 0 ? "set" : "cleared");
        }
    }
    
    // Save lock type
    if (temp_lock_type != settings_get_lock_type()) {
        settings_set_lock_type(temp_lock_type);
        ESP_LOGI(TAG, "Lock type saved: %d", (int)temp_lock_type);
    }
    
    temp_settings_changed = false;
    
    // Refresh Start Menu and Lock Screen with new user profile
    win32_refresh_start_menu_user();
    
    // Refresh page
    settings_show_user_page();
}

// User input dialog with keyboard
static void user_input_cancel_clicked(lv_event_t *e)
{
    if (user_input_dialog) {
        lv_obj_delete(user_input_dialog);
        user_input_dialog = NULL;
        user_input_textarea = NULL;
        user_keyboard = NULL;
    }
}

static void user_input_save_clicked(lv_event_t *e)
{
    if (!user_input_textarea) return;
    
    const char *value = lv_textarea_get_text(user_input_textarea);
    
    if (user_input_is_pin) {
        // Save as PIN
        strncpy(temp_pin, value ? value : "", sizeof(temp_pin) - 1);
        temp_pin[sizeof(temp_pin) - 1] = '\0';
        ESP_LOGI(TAG, "Temp PIN %s (%d digits)", (value && strlen(value) > 0) ? "set" : "cleared", (int)strlen(temp_pin));
    } else if (user_input_is_password) {
        strncpy(temp_password, value ? value : "", sizeof(temp_password) - 1);
        temp_password[sizeof(temp_password) - 1] = '\0';
        ESP_LOGI(TAG, "Temp password %s", (value && strlen(value) > 0) ? "set" : "cleared");
    } else {
        if (value && strlen(value) > 0) {
            strncpy(temp_username, value, sizeof(temp_username) - 1);
            temp_username[sizeof(temp_username) - 1] = '\0';
            ESP_LOGI(TAG, "Temp username: %s", temp_username);
        }
    }
    
    temp_settings_changed = true;
    
    // Close dialog
    if (user_input_dialog) {
        lv_obj_delete(user_input_dialog);
        user_input_dialog = NULL;
        user_input_textarea = NULL;
        user_keyboard = NULL;
    }
    
    // Refresh page
    settings_show_user_page();
}

static void show_user_input_dialog(const char *title, const char *current_value, bool is_password)
{
    user_input_is_password = is_password;
    
    // Delete existing dialog if any
    if (user_input_dialog) {
        lv_obj_delete(user_input_dialog);
    }
    
    // Create fullscreen dialog
    user_input_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(user_input_dialog, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(user_input_dialog, 0, 0);
    lv_obj_set_style_bg_color(user_input_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_width(user_input_dialog, 0, 0);
    lv_obj_set_style_radius(user_input_dialog, 0, 0);
    lv_obj_set_style_pad_all(user_input_dialog, 8, 0);
    lv_obj_remove_flag(user_input_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(user_input_dialog);
    lv_obj_set_size(title_bar, lv_pct(100), 36);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 4, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, UI_FONT, 0);
    lv_obj_center(title_label);
    
    // Input textarea
    user_input_textarea = lv_textarea_create(user_input_dialog);
    lv_obj_set_size(user_input_textarea, SCREEN_WIDTH - 20, 55);
    lv_obj_align(user_input_textarea, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_one_line(user_input_textarea, true);
    lv_textarea_set_password_mode(user_input_textarea, is_password);
    if (current_value && strlen(current_value) > 0) {
        lv_textarea_set_text(user_input_textarea, current_value);
    }
    lv_textarea_set_placeholder_text(user_input_textarea, is_password ? "Enter password/PIN (empty = disable)" : "Enter username");
    lv_obj_set_style_bg_color(user_input_textarea, lv_color_white(), 0);
    lv_obj_set_style_border_color(user_input_textarea, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_width(user_input_textarea, 2, 0);
    lv_obj_set_style_text_font(user_input_textarea, UI_FONT, 0);
    lv_obj_set_style_pad_all(user_input_textarea, 12, 0);
    
    // Controls row
    lv_obj_t *controls_row = lv_obj_create(user_input_dialog);
    lv_obj_set_size(controls_row, SCREEN_WIDTH - 16, 50);
    lv_obj_align(controls_row, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_opa(controls_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls_row, 0, 0);
    lv_obj_set_style_pad_all(controls_row, 0, 0);
    lv_obj_remove_flag(controls_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Show password checkbox (only for password mode)
    if (is_password) {
        lv_obj_t *show_pass_cb = lv_checkbox_create(controls_row);
        lv_checkbox_set_text(show_pass_cb, "Show");
        lv_obj_set_style_text_color(show_pass_cb, lv_color_black(), 0);
        lv_obj_align(show_pass_cb, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(show_pass_cb, [](lv_event_t *e) {
            lv_obj_t *cb = (lv_obj_t *)lv_event_get_target(e);
            bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
            lv_textarea_set_password_mode(user_input_textarea, !checked);
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(controls_row);
    lv_obj_set_size(cancel_btn, 110, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_CENTER, -65, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_add_event_cb(cancel_btn, user_input_cancel_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(cancel_label, UI_FONT, 0);
    lv_obj_center(cancel_label);
    
    // OK button
    lv_obj_t *save_btn = lv_btn_create(controls_row);
    lv_obj_set_size(save_btn, 110, 40);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_add_event_cb(save_btn, user_input_save_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "OK");
    lv_obj_set_style_text_color(save_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(save_label, UI_FONT, 0);
    lv_obj_center(save_label);
    
    // Keyboard
    uint16_t kb_height = settings_get_keyboard_height_px();
    if (kb_height < 136 || kb_height > 700) {
        kb_height = 496;
    }
    
    user_keyboard = lv_keyboard_create(user_input_dialog);
    lv_obj_set_size(user_keyboard, SCREEN_WIDTH, kb_height);
    lv_obj_align(user_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(user_keyboard, user_input_textarea);
    apply_keyboard_theme(user_keyboard);  // Apply theme (uses default font for symbols)
}

// PIN input dialog with numeric keypad
static void show_pin_input_dialog(const char *title, const char *current_value)
{
    user_input_is_password = false;
    user_input_is_pin = true;
    
    // Delete existing dialog if any
    if (user_input_dialog) {
        lv_obj_delete(user_input_dialog);
    }
    
    // Create fullscreen dialog
    user_input_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(user_input_dialog, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(user_input_dialog, 0, 0);
    lv_obj_set_style_bg_color(user_input_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_width(user_input_dialog, 0, 0);
    lv_obj_set_style_radius(user_input_dialog, 0, 0);
    lv_obj_set_style_pad_all(user_input_dialog, 8, 0);
    lv_obj_remove_flag(user_input_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(user_input_dialog);
    lv_obj_set_size(title_bar, lv_pct(100), 36);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 4, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, UI_FONT, 0);
    lv_obj_center(title_label);
    
    // Input textarea (numbers only)
    user_input_textarea = lv_textarea_create(user_input_dialog);
    lv_obj_set_size(user_input_textarea, SCREEN_WIDTH - 20, 55);
    lv_obj_align(user_input_textarea, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_one_line(user_input_textarea, true);
    lv_textarea_set_password_mode(user_input_textarea, true);
    lv_textarea_set_max_length(user_input_textarea, 6);  // Max 6 digits
    lv_textarea_set_accepted_chars(user_input_textarea, "0123456789");
    if (current_value && strlen(current_value) > 0) {
        lv_textarea_set_text(user_input_textarea, current_value);
    }
    lv_textarea_set_placeholder_text(user_input_textarea, "Enter 4-6 digit PIN");
    lv_obj_set_style_bg_color(user_input_textarea, lv_color_white(), 0);
    lv_obj_set_style_border_color(user_input_textarea, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_width(user_input_textarea, 2, 0);
    lv_obj_set_style_text_font(user_input_textarea, UI_FONT, 0);
    lv_obj_set_style_pad_all(user_input_textarea, 12, 0);
    
    // Controls row
    lv_obj_t *controls_row = lv_obj_create(user_input_dialog);
    lv_obj_set_size(controls_row, SCREEN_WIDTH - 16, 50);
    lv_obj_align(controls_row, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_opa(controls_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls_row, 0, 0);
    lv_obj_set_style_pad_all(controls_row, 0, 0);
    lv_obj_remove_flag(controls_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Show PIN checkbox
    lv_obj_t *show_pin_cb = lv_checkbox_create(controls_row);
    lv_checkbox_set_text(show_pin_cb, "Show PIN");
    lv_obj_set_style_text_color(show_pin_cb, lv_color_black(), 0);
    lv_obj_align(show_pin_cb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(show_pin_cb, [](lv_event_t *e) {
        lv_obj_t *cb = (lv_obj_t *)lv_event_get_target(e);
        bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
        lv_textarea_set_password_mode(user_input_textarea, !checked);
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(controls_row);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_CENTER, -55, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_add_event_cb(cancel_btn, user_input_cancel_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(cancel_label, UI_FONT, 0);
    lv_obj_center(cancel_label);
    
    // OK button
    lv_obj_t *save_btn = lv_btn_create(controls_row);
    lv_obj_set_size(save_btn, 100, 40);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_add_event_cb(save_btn, user_input_save_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "OK");
    lv_obj_set_style_text_color(save_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(save_label, UI_FONT, 0);
    lv_obj_center(save_label);
    
    // Numeric keyboard
    user_keyboard = lv_keyboard_create(user_input_dialog);
    lv_obj_set_size(user_keyboard, SCREEN_WIDTH, 400);
    lv_obj_align(user_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(user_keyboard, user_input_textarea);
    lv_keyboard_set_mode(user_keyboard, LV_KEYBOARD_MODE_NUMBER);  // Numeric mode
    apply_keyboard_theme(user_keyboard);  // Apply theme (uses default font for symbols)
}

static void user_color_clicked(lv_event_t *e)
{
    uint32_t color = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    temp_avatar_color = color;
    temp_settings_changed = true;
    ESP_LOGI(TAG, "Temp avatar color: 0x%06X", (unsigned int)color);
    settings_show_user_page();
}

static void user_save_name_clicked(lv_event_t *e)
{
    user_input_is_pin = false;
    // Open keyboard dialog for username
    show_user_input_dialog("Change Username", temp_username, false);
}

static void user_save_password_clicked(lv_event_t *e)
{
    user_input_is_pin = false;
    // Open keyboard dialog for password
    show_user_input_dialog("Set Lock Password", temp_password, true);
}

static void user_save_pin_clicked(lv_event_t *e)
{
    // Open PIN input dialog
    show_pin_input_dialog("Set Lock PIN", temp_pin);
}

static void user_lock_type_clicked(lv_event_t *e)
{
    lock_type_t type = (lock_type_t)(intptr_t)lv_event_get_user_data(e);
    temp_lock_type = type;
    temp_settings_changed = true;
    ESP_LOGI(TAG, "Temp lock type: %d", (int)type);
    settings_show_user_page();
}

static void user_factory_reset_confirm(lv_event_t *e)
{
    ESP_LOGW(TAG, "Factory reset confirmed!");
    settings_factory_reset();
    // Restart device
    esp_restart();
}

static void user_factory_reset_clicked(lv_event_t *e)
{
    // Create confirmation dialog
    lv_obj_t *dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(dialog, 350, 200);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_border_width(dialog, 3, 0);
    lv_obj_set_style_radius(dialog, 8, 0);
    lv_obj_set_style_shadow_width(dialog, 20, 0);
    lv_obj_set_style_shadow_color(dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(dialog, LV_OPA_30, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Warning icon
    lv_obj_t *warn_icon = lv_label_create(dialog);
    lv_label_set_text(warn_icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(warn_icon, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_text_font(warn_icon, UI_FONT, 0);
    lv_obj_align(warn_icon, LV_ALIGN_TOP_MID, 0, 15);
    
    // Title
    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, "Factory Reset");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
    
    // Message
    lv_obj_t *msg = lv_label_create(dialog);
    lv_label_set_text(msg, "All settings will be deleted!\nDevice will restart.");
    lv_obj_set_style_text_color(msg, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(dialog);
    lv_obj_set_size(cancel_btn, 120, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 20, -15);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_user_data(cancel_btn, dialog);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
        lv_obj_t *dlg = (lv_obj_t *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
        lv_obj_delete(dlg);
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_white(), 0);
    lv_obj_center(cancel_label);
    
    // Reset button
    lv_obj_t *reset_btn = lv_btn_create(dialog);
    lv_obj_set_size(reset_btn, 120, 40);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -15);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(reset_btn, 6, 0);
    lv_obj_add_event_cb(reset_btn, user_factory_reset_confirm, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_set_style_text_color(reset_label, lv_color_white(), 0);
    lv_obj_center(reset_label);
}

void settings_show_user_page(void)
{
    ESP_LOGI(TAG, "Opening User settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    settings_wallpaper_page = NULL;
    settings_time_page = NULL;
    user_name_textarea = NULL;
    user_password_textarea = NULL;
    user_keyboard = NULL;
    
    if (settings_user_page && is_valid_child(settings_user_page)) {
        lv_obj_delete(settings_user_page);
    }
    settings_user_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    // Initialize temp settings from current settings (only on first open)
    if (!temp_settings_changed) {
        strncpy(temp_username, settings_get_username(), sizeof(temp_username) - 1);
        temp_avatar_color = settings_get_avatar_color();
        temp_lock_type = settings_get_lock_type();
        memset(temp_password, 0, sizeof(temp_password));
        memset(temp_pin, 0, sizeof(temp_pin));
    }
    
    settings_user_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_user_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_user_page, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(settings_user_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_user_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_user_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_user_page, 0, 0);
    lv_obj_set_style_radius(settings_user_page, 0, 0);
    lv_obj_set_style_pad_all(settings_user_page, 8, 0);
    lv_obj_set_flex_flow(settings_user_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_user_page, 6, 0);
    
    // Top row: Back and Apply buttons
    lv_obj_t *top_row = lv_obj_create(settings_user_page);
    lv_obj_set_size(top_row, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_obj_create(top_row);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { 
        temp_settings_changed = false;
        app_settings_create(); 
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Apply button (green when changes pending)
    lv_obj_t *apply_btn = lv_obj_create(top_row);
    lv_obj_set_size(apply_btn, 90, 32);
    lv_obj_align(apply_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(apply_btn, temp_settings_changed ? lv_color_hex(0x27AE60) : lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(apply_btn, 1, 0);
    lv_obj_set_style_border_color(apply_btn, temp_settings_changed ? lv_color_hex(0x1E8449) : lv_color_hex(0x666666), 0);
    lv_obj_set_style_radius(apply_btn, 4, 0);
    lv_obj_add_flag(apply_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(apply_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(apply_btn, user_apply_settings, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *apply_label = lv_label_create(apply_btn);
    lv_label_set_text(apply_label, LV_SYMBOL_OK " Apply");
    lv_obj_set_style_text_color(apply_label, lv_color_white(), 0);
    lv_obj_center(apply_label);
    lv_obj_remove_flag(apply_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Current user display with avatar (shows temp values)
    uint32_t current_color = temp_avatar_color;
    const char *current_name = temp_username;
    
    lv_obj_t *user_header = lv_obj_create(settings_user_page);
    lv_obj_set_size(user_header, lv_pct(100), 70);
    lv_obj_set_style_bg_color(user_header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(user_header, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(user_header, 1, 0);
    lv_obj_set_style_radius(user_header, 4, 0);
    lv_obj_set_style_pad_all(user_header, 12, 0);
    lv_obj_remove_flag(user_header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Avatar circle
    lv_obj_t *avatar = lv_obj_create(user_header);
    lv_obj_set_size(avatar, 50, 50);
    lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(current_color), 0);
    lv_obj_set_style_border_width(avatar, 2, 0);
    lv_obj_set_style_border_color(avatar, lv_color_white(), 0);
    lv_obj_set_style_radius(avatar, 25, 0);
    lv_obj_set_style_shadow_width(avatar, 4, 0);
    lv_obj_set_style_shadow_color(avatar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(avatar, LV_OPA_30, 0);
    lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Avatar letter
    lv_obj_t *avatar_letter = lv_label_create(avatar);
    char letter[2] = {current_name[0], '\0'};
    if (letter[0] >= 'a' && letter[0] <= 'z') letter[0] -= 32;  // Uppercase
    lv_label_set_text(avatar_letter, letter);
    lv_obj_set_style_text_color(avatar_letter, lv_color_white(), 0);
    lv_obj_set_style_text_font(avatar_letter, UI_FONT, 0);
    lv_obj_center(avatar_letter);
    
    // Username display
    lv_obj_t *name_display = lv_label_create(user_header);
    lv_label_set_text(name_display, current_name);
    lv_obj_set_style_text_color(name_display, lv_color_black(), 0);
    lv_obj_set_style_text_font(name_display, UI_FONT, 0);
    lv_obj_align(name_display, LV_ALIGN_LEFT_MID, 65, -10);
    
    // Password status
    lv_obj_t *pass_status = lv_label_create(user_header);
    lv_label_set_text(pass_status, settings_has_password() ? "Password: Set" : "Password: None");
    lv_obj_set_style_text_color(pass_status, lv_color_hex(0x666666), 0);
    lv_obj_align(pass_status, LV_ALIGN_LEFT_MID, 65, 10);
    
    // Username change button
    lv_obj_t *name_cont = lv_obj_create(settings_user_page);
    lv_obj_set_size(name_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(name_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(name_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(name_cont, 1, 0);
    lv_obj_set_style_radius(name_cont, 4, 0);
    lv_obj_set_style_pad_all(name_cont, 10, 0);
    lv_obj_add_flag(name_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(name_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(name_cont, user_save_name_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *name_label = lv_label_create(name_cont);
    lv_label_set_text(name_label, "Change Username");
    lv_obj_set_style_text_color(name_label, lv_color_hex(0x333333), 0);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_t *name_arrow = lv_label_create(name_cont);
    lv_label_set_text(name_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(name_arrow, lv_color_hex(0x888888), 0);
    lv_obj_align(name_arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(name_arrow, LV_OBJ_FLAG_CLICKABLE);
    
    // Avatar color picker
    lv_obj_t *color_cont = lv_obj_create(settings_user_page);
    lv_obj_set_size(color_cont, lv_pct(100), 80);
    lv_obj_set_style_bg_color(color_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(color_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(color_cont, 1, 0);
    lv_obj_set_style_radius(color_cont, 4, 0);
    lv_obj_set_style_pad_all(color_cont, 10, 0);
    lv_obj_remove_flag(color_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *color_label = lv_label_create(color_cont);
    lv_label_set_text(color_label, "Avatar Color:");
    lv_obj_set_style_text_color(color_label, lv_color_hex(0x333333), 0);
    lv_obj_align(color_label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Color buttons row
    lv_obj_t *colors_row = lv_obj_create(color_cont);
    lv_obj_set_size(colors_row, lv_pct(100), 40);
    lv_obj_align(colors_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(colors_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(colors_row, 0, 0);
    lv_obj_set_style_pad_all(colors_row, 0, 0);
    lv_obj_set_flex_flow(colors_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(colors_row, 8, 0);
    lv_obj_remove_flag(colors_row, LV_OBJ_FLAG_SCROLLABLE);
    
    for (int i = 0; i < AVATAR_COLORS_COUNT; i++) {
        lv_obj_t *color_btn = lv_obj_create(colors_row);
        lv_obj_set_size(color_btn, 36, 36);
        lv_obj_set_style_bg_color(color_btn, lv_color_hex(avatar_colors[i]), 0);
        lv_obj_set_style_border_width(color_btn, avatar_colors[i] == current_color ? 3 : 1, 0);
        lv_obj_set_style_border_color(color_btn, avatar_colors[i] == current_color ? lv_color_hex(0x000000) : lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_radius(color_btn, 18, 0);
        lv_obj_add_flag(color_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(color_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(color_btn, user_color_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)avatar_colors[i]);
    }
    
    // Password change button (for Password lock type)
    lv_obj_t *pass_cont = lv_obj_create(settings_user_page);
    lv_obj_set_size(pass_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(pass_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(pass_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(pass_cont, 1, 0);
    lv_obj_set_style_radius(pass_cont, 4, 0);
    lv_obj_set_style_pad_all(pass_cont, 10, 0);
    lv_obj_add_flag(pass_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pass_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pass_cont, user_save_password_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *pass_label = lv_label_create(pass_cont);
    lv_label_set_text(pass_label, "Set Lock Password");
    lv_obj_set_style_text_color(pass_label, lv_color_hex(0x333333), 0);
    lv_obj_align(pass_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(pass_label, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_t *pass_arrow = lv_label_create(pass_cont);
    lv_label_set_text(pass_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(pass_arrow, lv_color_hex(0x888888), 0);
    lv_obj_align(pass_arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(pass_arrow, LV_OBJ_FLAG_CLICKABLE);
    
    // PIN change button (for PIN lock type)
    lv_obj_t *pin_cont = lv_obj_create(settings_user_page);
    lv_obj_set_size(pin_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(pin_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(pin_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(pin_cont, 1, 0);
    lv_obj_set_style_radius(pin_cont, 4, 0);
    lv_obj_set_style_pad_all(pin_cont, 10, 0);
    lv_obj_add_flag(pin_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pin_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pin_cont, user_save_pin_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *pin_label = lv_label_create(pin_cont);
    lv_label_set_text(pin_label, "Set Lock PIN (4-6 digits)");
    lv_obj_set_style_text_color(pin_label, lv_color_hex(0x333333), 0);
    lv_obj_align(pin_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(pin_label, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_t *pin_arrow = lv_label_create(pin_cont);
    lv_label_set_text(pin_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(pin_arrow, lv_color_hex(0x888888), 0);
    lv_obj_align(pin_arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(pin_arrow, LV_OBJ_FLAG_CLICKABLE);
    
    // Lock type selector
    lv_obj_t *lock_type_cont = lv_obj_create(settings_user_page);
    lv_obj_set_size(lock_type_cont, lv_pct(100), 90);
    lv_obj_set_style_bg_color(lock_type_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(lock_type_cont, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(lock_type_cont, 1, 0);
    lv_obj_set_style_radius(lock_type_cont, 4, 0);
    lv_obj_set_style_pad_all(lock_type_cont, 10, 0);
    lv_obj_remove_flag(lock_type_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *lock_type_label = lv_label_create(lock_type_cont);
    lv_label_set_text(lock_type_label, "Lock Screen Type:");
    lv_obj_set_style_text_color(lock_type_label, lv_color_hex(0x333333), 0);
    lv_obj_align(lock_type_label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Lock type buttons row
    lv_obj_t *lock_btns_row = lv_obj_create(lock_type_cont);
    lv_obj_set_size(lock_btns_row, lv_pct(100), 45);
    lv_obj_align(lock_btns_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(lock_btns_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lock_btns_row, 0, 0);
    lv_obj_set_style_pad_all(lock_btns_row, 0, 0);
    lv_obj_set_flex_flow(lock_btns_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(lock_btns_row, 8, 0);
    lv_obj_remove_flag(lock_btns_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Slide button
    lv_obj_t *slide_btn = lv_btn_create(lock_btns_row);
    lv_obj_set_size(slide_btn, 100, 40);
    lv_obj_set_style_bg_color(slide_btn, temp_lock_type == LOCK_TYPE_SLIDE ? lv_color_hex(0x4A90D9) : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(slide_btn, 6, 0);
    lv_obj_add_event_cb(slide_btn, user_lock_type_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)LOCK_TYPE_SLIDE);
    lv_obj_t *slide_label = lv_label_create(slide_btn);
    lv_label_set_text(slide_label, "Slide");
    lv_obj_set_style_text_color(slide_label, temp_lock_type == LOCK_TYPE_SLIDE ? lv_color_white() : lv_color_hex(0x333333), 0);
    lv_obj_center(slide_label);
    
    // PIN button
    lv_obj_t *pin_btn = lv_btn_create(lock_btns_row);
    lv_obj_set_size(pin_btn, 100, 40);
    lv_obj_set_style_bg_color(pin_btn, temp_lock_type == LOCK_TYPE_PIN ? lv_color_hex(0x4A90D9) : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(pin_btn, 6, 0);
    lv_obj_add_event_cb(pin_btn, user_lock_type_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)LOCK_TYPE_PIN);
    lv_obj_t *pin_btn_label = lv_label_create(pin_btn);
    lv_label_set_text(pin_btn_label, "PIN");
    lv_obj_set_style_text_color(pin_btn_label, temp_lock_type == LOCK_TYPE_PIN ? lv_color_white() : lv_color_hex(0x333333), 0);
    lv_obj_center(pin_btn_label);
    
    // Password button
    lv_obj_t *pass_type_btn = lv_btn_create(lock_btns_row);
    lv_obj_set_size(pass_type_btn, 120, 40);
    lv_obj_set_style_bg_color(pass_type_btn, temp_lock_type == LOCK_TYPE_PASSWORD ? lv_color_hex(0x4A90D9) : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(pass_type_btn, 6, 0);
    lv_obj_add_event_cb(pass_type_btn, user_lock_type_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)LOCK_TYPE_PASSWORD);
    lv_obj_t *pass_type_label = lv_label_create(pass_type_btn);
    lv_label_set_text(pass_type_label, "Password");
    lv_obj_set_style_text_color(pass_type_label, temp_lock_type == LOCK_TYPE_PASSWORD ? lv_color_white() : lv_color_hex(0x333333), 0);
    lv_obj_center(pass_type_label);
    
    // Factory Reset button
    lv_obj_t *reset_btn = lv_btn_create(settings_user_page);
    lv_obj_set_size(reset_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(reset_btn, 6, 0);
    lv_obj_add_event_cb(reset_btn, user_factory_reset_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, LV_SYMBOL_WARNING " Factory Reset");
    lv_obj_set_style_text_color(reset_label, lv_color_white(), 0);
    lv_obj_center(reset_label);
}

// ============ APPS SETTINGS PAGE ============

// App info structure for display
typedef struct {
    const char *name;
    const char *display_name;
    const char *version;
    const char *category;
} app_info_t;

// List of all apps with versions
static const app_info_t app_list[] = {
    {"my_computer", "My PC", "1.0.0", "System"},
    {"recycle_bin", "Trash", "1.0.0", "System"},
    {"calculator", "Calculator", "1.2.0", "Utilities"},
    {"camera", "Camera", "1.1.0", "Media"},
    {"weather", "Weather", "2.0.0", "Internet"},
    {"clock", "Clock", "1.3.0", "Utilities"},
    {"settings", "Settings", "2.1.0", "System"},
    {"notepad", "Notepad", "1.0.0", "Utilities"},
    {"photos", "Photos", "1.0.0", "Media"},
    {"flappy", "Flappy Bird", "1.0.0", "Games"},
    {"paint", "Paint", "1.0.0", "Media"},
    {"console", "Console", "1.5.0", "System"},
    {"voice_recorder", "Voice Recorder", "1.0.0", "Media"},
    {"system_monitor", "Task Manager", "1.2.0", "System"},
    {"snake", "Snake", "1.0.0", "Games"},
};
#define NUM_APPS (sizeof(app_list) / sizeof(app_list[0]))

// System drivers info
static const app_info_t driver_list[] = {
    {"display", "ST7701 Display Driver", "1.0.0", "Display"},
    {"touch", "GT911 Touch Driver", "1.0.0", "Input"},
    {"wifi", "ESP32 WiFi Driver", "5.1.0", "Network"},
    {"bluetooth", "ESP32 BT Driver", "5.1.0", "Network"},
    {"audio", "I2S Audio Driver", "1.0.0", "Audio"},
    {"storage", "SPIFFS Driver", "1.0.0", "Storage"},
};
#define NUM_DRIVERS (sizeof(driver_list) / sizeof(driver_list[0]))

static lv_obj_t *settings_apps_page = NULL;

void settings_show_apps_page(void)
{
    ESP_LOGI(TAG, "Opening Apps settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    
    // Delete existing page only if it's still a valid child
    if (settings_apps_page && is_valid_child(settings_apps_page)) {
        lv_obj_delete(settings_apps_page);
    }
    settings_apps_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_apps_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_apps_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_apps_page, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(settings_apps_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_apps_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_apps_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_apps_page, 0, 0);
    lv_obj_set_style_radius(settings_apps_page, 0, 0);
    lv_obj_set_style_pad_all(settings_apps_page, 10, 0);
    lv_obj_set_flex_flow(settings_apps_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_apps_page, 8, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_obj_create(settings_apps_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { app_settings_create(); }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Header
    lv_obj_t *header = lv_label_create(settings_apps_page);
    lv_label_set_text(header, "Installed Applications");
    lv_obj_set_style_text_color(header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(header, UI_FONT, 0);
    
    // Apps count
    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "%d apps, %d drivers", (int)NUM_APPS, (int)NUM_DRIVERS);
    lv_obj_t *count_label = lv_label_create(settings_apps_page);
    lv_label_set_text(count_label, count_buf);
    lv_obj_set_style_text_color(count_label, lv_color_hex(0x666666), 0);
    
    // Apps list container
    lv_obj_t *apps_list = lv_obj_create(settings_apps_page);
    lv_obj_set_size(apps_list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(apps_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(apps_list, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(apps_list, 1, 0);
    lv_obj_set_style_radius(apps_list, 4, 0);
    lv_obj_set_style_pad_all(apps_list, 8, 0);
    lv_obj_set_flex_flow(apps_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(apps_list, 4, 0);
    
    // Section: Applications
    lv_obj_t *apps_header = lv_label_create(apps_list);
    lv_label_set_text(apps_header, "Applications");
    lv_obj_set_style_text_color(apps_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(apps_header, UI_FONT, 0);
    
    // List apps
    for (int i = 0; i < NUM_APPS; i++) {
        lv_obj_t *item = lv_obj_create(apps_list);
        lv_obj_set_size(item, lv_pct(100), 45);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xF8F8F8), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // App name
        lv_obj_t *name_lbl = lv_label_create(item);
        lv_label_set_text(name_lbl, app_list[i].display_name);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(name_lbl, UI_FONT, 0);
        lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
        
        // Version and category
        char info_buf[64];
        snprintf(info_buf, sizeof(info_buf), "v%s | %s", app_list[i].version, app_list[i].category);
        lv_obj_t *info_lbl = lv_label_create(item);
        lv_label_set_text(info_lbl, info_buf);
        lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
        lv_obj_align(info_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    
    // Section: System Drivers
    lv_obj_t *drivers_header = lv_label_create(apps_list);
    lv_label_set_text(drivers_header, "System Drivers");
    lv_obj_set_style_text_color(drivers_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(drivers_header, UI_FONT, 0);
    
    // List drivers
    for (int i = 0; i < NUM_DRIVERS; i++) {
        lv_obj_t *item = lv_obj_create(apps_list);
        lv_obj_set_size(item, lv_pct(100), 45);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xF0F8FF), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xD0E8F8), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Driver name
        lv_obj_t *name_lbl = lv_label_create(item);
        lv_label_set_text(name_lbl, driver_list[i].display_name);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(name_lbl, UI_FONT, 0);
        lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
        
        // Version and category
        char info_buf[64];
        snprintf(info_buf, sizeof(info_buf), "v%s | %s", driver_list[i].version, driver_list[i].category);
        lv_obj_t *info_lbl = lv_label_create(item);
        lv_label_set_text(info_lbl, info_buf);
        lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
        lv_obj_align(info_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

// ============ TASKBAR SETTINGS PAGE ============

static lv_obj_t *settings_taskbar_page = NULL;

// Pinned apps list (stored in settings)
extern const char* taskbar_pinned_apps[];
extern int taskbar_pinned_count;

void settings_show_taskbar_page(void)
{
    ESP_LOGI(TAG, "Opening Taskbar settings");
    
    // Reset other page pointers
    settings_wifi_page = NULL;
    settings_keyboard_page = NULL;
    
    // Delete existing page only if it's still a valid child
    if (settings_taskbar_page && is_valid_child(settings_taskbar_page)) {
        lv_obj_delete(settings_taskbar_page);
    }
    settings_taskbar_page = NULL;
    
    if (!app_window) {
        ESP_LOGE(TAG, "app_window is NULL!");
        return;
    }
    
    settings_taskbar_page = lv_obj_create(app_window);
    lv_obj_set_size(settings_taskbar_page, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(settings_taskbar_page, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(settings_taskbar_page, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(settings_taskbar_page, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(settings_taskbar_page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(settings_taskbar_page, 0, 0);
    lv_obj_set_style_radius(settings_taskbar_page, 0, 0);
    lv_obj_set_style_pad_all(settings_taskbar_page, 10, 0);
    lv_obj_set_flex_flow(settings_taskbar_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_taskbar_page, 8, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_obj_create(settings_taskbar_page);
    lv_obj_set_size(back_btn, 80, 32);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { app_settings_create(); }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
    lv_obj_center(back_label);
    lv_obj_remove_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Header
    lv_obj_t *header = lv_label_create(settings_taskbar_page);
    lv_label_set_text(header, "Taskbar Settings");
    lv_obj_set_style_text_color(header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(header, UI_FONT, 0);
    
    // Info text
    lv_obj_t *info = lv_label_create(settings_taskbar_page);
    lv_label_set_text(info, "Select apps to pin to taskbar.\nPinned apps appear as quick launch icons.");
    lv_obj_set_style_text_color(info, lv_color_hex(0x666666), 0);
    lv_obj_set_width(info, lv_pct(100));
    
    // Apps list container
    lv_obj_t *apps_list = lv_obj_create(settings_taskbar_page);
    lv_obj_set_size(apps_list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(apps_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(apps_list, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(apps_list, 1, 0);
    lv_obj_set_style_radius(apps_list, 4, 0);
    lv_obj_set_style_pad_all(apps_list, 8, 0);
    lv_obj_set_flex_flow(apps_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(apps_list, 4, 0);
    
    // List apps with checkboxes
    for (int i = 0; i < NUM_APPS; i++) {
        lv_obj_t *item = lv_obj_create(apps_list);
        lv_obj_set_size(item, lv_pct(100), 40);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xF8F8F8), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Checkbox
        lv_obj_t *cb = lv_checkbox_create(item);
        lv_checkbox_set_text(cb, app_list[i].display_name);
        lv_obj_set_style_text_color(cb, lv_color_hex(0x333333), 0);
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, 0, 0);
        
        // Check if app is pinned (placeholder - would need actual settings storage)
        // For now, pin first 3 apps by default
        if (i < 3) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        
        // Store app index for callback
        lv_obj_set_user_data(cb, (void*)(intptr_t)i);
        lv_obj_add_event_cb(cb, [](lv_event_t *e) {
            lv_obj_t *checkbox = (lv_obj_t *)lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(checkbox);
            bool checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
            ESP_LOGI("TASKBAR", "App %d (%s) pinned: %d", idx, app_list[idx].name, checked);
            // TODO: Save to settings
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }
}
