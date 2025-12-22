/**
 * Win32 OS - System Tray Panel (Vista Style)
 * Quick settings panel with WiFi, Brightness, Battery, Date/Time
 * 
 * WiFi: Uses ESP-Hosted for ESP32-C6 co-processor communication
 */

#include "win32_ui.h"
#include "hardware/hardware.h"
#include "system_settings.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include <string.h>
#include <time.h>

// ESP-Hosted WiFi headers (always available with esp_hosted component)
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// SNTP for time synchronization
#include "esp_sntp.h"

// Custom font with Cyrillic support
#include "assets.h"
#define UI_FONT &CodeProVariable

// Enable real WiFi mode
#define WIFI_REAL_MODE 1

static const char *TAG = "SYSTRAY";
extern lv_obj_t *scr_desktop;

static lv_obj_t *systray_panel = NULL;
static bool systray_visible = false;
static bool wifi_initialized = false;
static bool wifi_connected = false;
static char connected_ssid[33] = {0};

// Flags for thread-safe UI updates (set from event handler, read from LVGL timer)
static volatile bool wifi_ui_update_needed = false;
static volatile bool wifi_ui_connected_state = false;

// Scan results storage
#define MAX_SCAN_RESULTS 20
static wifi_ap_info_t scan_results[MAX_SCAN_RESULTS];
static uint16_t scan_result_count = 0;

// UI elements that need updating
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *battery_label = NULL;
static lv_obj_t *battery_bar = NULL;

// ============ REAL WIFI IMPLEMENTATION (ESP-Hosted) ============

static esp_netif_t *sta_netif = NULL;
static EventGroupHandle_t wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Disconnect reason to string
static const char* wifi_disconnect_reason_str(uint8_t reason) {
    switch (reason) {
        case 1: return "UNSPECIFIED";
        case 2: return "AUTH_EXPIRE";
        case 3: return "AUTH_LEAVE";
        case 4: return "ASSOC_EXPIRE";
        case 5: return "ASSOC_TOOMANY";
        case 6: return "NOT_AUTHED";
        case 7: return "NOT_ASSOCED";
        case 8: return "ASSOC_LEAVE";
        case 9: return "ASSOC_NOT_AUTHED";
        case 10: return "DISASSOC_PWRCAP_BAD";
        case 11: return "DISASSOC_SUPCHAN_BAD";
        case 12: return "BSS_TRANSITION_DISASSOC";
        case 13: return "IE_INVALID";
        case 14: return "MIC_FAILURE";
        case 15: return "4WAY_HANDSHAKE_TIMEOUT (wrong password?)";
        case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
        case 17: return "IE_IN_4WAY_DIFFERS";
        case 18: return "GROUP_CIPHER_INVALID";
        case 19: return "PAIRWISE_CIPHER_INVALID";
        case 20: return "AKMP_INVALID";
        case 21: return "UNSUPP_RSN_IE_VERSION";
        case 22: return "INVALID_RSN_IE_CAP";
        case 23: return "802_1X_AUTH_FAILED";
        case 24: return "CIPHER_SUITE_REJECTED";
        case 200: return "BEACON_TIMEOUT";
        case 201: return "NO_AP_FOUND";
        case 202: return "AUTH_FAIL";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        case 205: return "CONNECTION_FAIL";
        case 206: return "AP_TSF_RESET";
        case 207: return "ROAMING";
        default: return "UNKNOWN";
    }
}

static uint8_t last_disconnect_reason = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                last_disconnect_reason = event->reason;
                ESP_LOGW(TAG, "WiFi disconnected! Reason: %d (%s)", 
                         event->reason, wifi_disconnect_reason_str(event->reason));
                ESP_LOGW(TAG, "  SSID: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
                         event->ssid,
                         event->bssid[0], event->bssid[1], event->bssid[2],
                         event->bssid[3], event->bssid[4], event->bssid[5]);
                
                wifi_connected = false;
                connected_ssid[0] = '\0';
                if (wifi_event_group) {
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                // Set flag for UI update (will be processed in LVGL thread)
                wifi_ui_update_needed = true;
                wifi_ui_connected_state = false;
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
                ESP_LOGI(TAG, "WiFi connected to AP! SSID: %s, Channel: %d", 
                         event->ssid, event->channel);
                break;
            }
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi scan done");
                break;
            default:
                ESP_LOGD(TAG, "WiFi event: %ld", (long)event_id);
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "  Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        wifi_connected = true;
        if (wifi_event_group) {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
        // Set flag for UI update (will be processed in LVGL thread)
        wifi_ui_update_needed = true;
        wifi_ui_connected_state = true;
    }
}

uint8_t system_wifi_get_last_error(void) {
    return last_disconnect_reason;
}

const char* system_wifi_get_error_string(uint8_t reason) {
    return wifi_disconnect_reason_str(reason);
}

// ============ SNTP TIME SYNCHRONIZATION ============

static bool sntp_initialized = false;

static void sntp_sync_time(void) {
    if (sntp_initialized) {
        ESP_LOGI(TAG, "SNTP already initialized, restarting...");
        esp_sntp_stop();
    }
    
    ESP_LOGI(TAG, "Initializing SNTP for time sync...");
    
    // Apply timezone BEFORE SNTP init so time is displayed correctly
    int8_t tz_offset = settings_get_timezone();
    char tz_str[32];
    if (tz_offset >= 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC-%d", tz_offset);
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC+%d", -tz_offset);
    }
    setenv("TZ", tz_str, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set: %s (UTC%+d)", tz_str, tz_offset);
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_init();
    sntp_initialized = true;
    
    // Non-blocking: just start SNTP and return
    // Time will sync in background, no need to wait
    ESP_LOGI(TAG, "SNTP started (non-blocking), time will sync in background");
}

int system_wifi_init(void) {
    if (wifi_initialized) return 0;
    
    ESP_LOGI(TAG, "Initializing WiFi (ESP-Hosted mode)");
    
    // Initialize TCP/IP stack
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(ret));
        return -1;
    }
    
    // Create default event loop if not exists
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return -1;
    }
    
    // Small delay to ensure SDIO is ready
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Create WiFi station interface
    sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        ESP_LOGE(TAG, "Failed to create WiFi STA netif");
        return -1;
    }
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    // Add retry logic for SDIO initialization
    int retry_count = 0;
    const int max_retries = 3;
    
    while (retry_count < max_retries) {
        ret = esp_wifi_init(&cfg);
        if (ret == ESP_OK) {
            break;
        }
        
        ESP_LOGW(TAG, "WiFi init failed (attempt %d/%d): %s", 
                 retry_count + 1, max_retries, esp_err_to_name(ret));
        
        // Wait before retry - SDIO might need more time
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi after %d attempts: %s", max_retries, esp_err_to_name(ret));
        // Cleanup netif on failure
        if (sta_netif) {
            esp_netif_destroy(sta_netif);
            sta_netif = NULL;
        }
        return -1;
    }
    
    // Create event group for synchronization
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        esp_wifi_deinit();
        if (sta_netif) {
            esp_netif_destroy(sta_netif);
            sta_netif = NULL;
        }
        return -1;
    }
    
    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi initialized successfully (ESP-Hosted)");
    return 0;
}

int system_wifi_scan(wifi_ap_info_t *ap_records, uint16_t *ap_count) {
    if (!wifi_initialized) {
        system_wifi_init();
    }
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // Configure scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = { .min = 100, .max = 300 }
        }
    };
    
    // Start scan (blocking)
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        return -1;
    }
    
    // Get scan results
    uint16_t num_aps = MAX_SCAN_RESULTS;
    wifi_ap_record_t ap_list[MAX_SCAN_RESULTS];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&num_aps, ap_list));
    
    // Convert to our format
    uint16_t count = (num_aps < *ap_count) ? num_aps : *ap_count;
    for (int i = 0; i < count; i++) {
        memcpy(ap_records[i].ssid, ap_list[i].ssid, 32);
        ap_records[i].ssid[32] = '\0';
        ap_records[i].rssi = ap_list[i].rssi;
        ap_records[i].authmode = (ap_list[i].authmode != WIFI_AUTH_OPEN) ? 1 : 0;
    }
    *ap_count = count;
    
    ESP_LOGI(TAG, "Found %d networks", count);
    return 0;
}


int system_wifi_connect(const char *ssid, const char *password) {
    if (!wifi_initialized) {
        int ret = system_wifi_init();
        if (ret != 0) {
            ESP_LOGE(TAG, "WiFi init failed!");
            return -1;
        }
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    ESP_LOGI(TAG, "  Password length: %d", password ? (int)strlen(password) : 0);
    ESP_LOGI(TAG, "========================================");
    
    // Reset last error
    last_disconnect_reason = 0;
    
    // Disconnect if already connected
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Configure WiFi
    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config));
    
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    
    int pass_len = password ? (int)strlen(password) : 0;
    ESP_LOGI(TAG, "  Password provided: %s (len=%d)", pass_len > 0 ? "YES" : "NO", pass_len);
    
    if (pass_len >= 8) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        ESP_LOGI(TAG, "  Auth mode: WPA/WPA2/WPA3 (password set)");
    } else if (pass_len > 0) {
        ESP_LOGW(TAG, "  Password too short (%d chars), need at least 8 for WPA!", pass_len);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        ESP_LOGI(TAG, "  Auth mode: Trying anyway...");
    } else {
        ESP_LOGI(TAG, "  Auth mode: OPEN (no password)");
    }
    
    // Don't set threshold - let it auto-detect
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    
    // PMF settings for WPA3 compatibility
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Set scan method to all channels
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return -1;
    }
    ESP_LOGI(TAG, "WiFi config set successfully");
    
    // Clear event bits
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Connect
    ESP_LOGI(TAG, "Calling esp_wifi_connect()...");
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return -1;
    }
    
    // Wait for connection (with timeout)
    ESP_LOGI(TAG, "Waiting for connection (timeout: 15s)...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(15000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        strncpy(connected_ssid, ssid, sizeof(connected_ssid) - 1);
        wifi_connected = true;
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "SUCCESS! Connected to: %s", ssid);
        ESP_LOGI(TAG, "========================================");
        
        // Sync time via SNTP
        sntp_sync_time();
        
        // Update UI
        if (wifi_status_label) {
            lv_label_set_text(wifi_status_label, connected_ssid);
        }
        win32_update_wifi(true);
        
        // Save credentials to LittleFS (new system)
        settings_save_wifi(ssid, password ? password : "");
        
        // Save credentials to NVS (legacy backup)
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "ssid", ssid);
            nvs_set_str(nvs, "pass", password ? password : "");
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "Credentials saved to NVS");
        }
        
        return 0;
    } else {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "FAILED to connect to: %s", ssid);
        ESP_LOGE(TAG, "  Last disconnect reason: %d (%s)", 
                 last_disconnect_reason, wifi_disconnect_reason_str(last_disconnect_reason));
        if (last_disconnect_reason == 15 || last_disconnect_reason == 204) {
            ESP_LOGE(TAG, "  >>> LIKELY WRONG PASSWORD! <<<");
        } else if (last_disconnect_reason == 201) {
            ESP_LOGE(TAG, "  >>> AP NOT FOUND - check SSID <<<");
        }
        ESP_LOGE(TAG, "========================================");
        return -1;
    }
}

// ============ COMMON WIFI FUNCTIONS ============

bool system_wifi_is_connected(void) { return wifi_connected; }
const char* system_wifi_get_ssid(void) { return connected_ssid; }

// Public function to resync time (call after timezone change)
void system_time_resync(void) {
    if (wifi_connected) {
        sntp_sync_time();
    } else {
        // Just apply timezone without SNTP
        int8_t tz_offset = settings_get_timezone();
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
}

// ============ SYSTEM TRAY UI (SIDE PANEL - Windows 10 Style) ============

// Side panel dimensions
#define SYSTRAY_PANEL_WIDTH 320

static void update_datetime_display(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (time_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lv_label_set_text(time_label, buf);
    }
    
    if (date_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d.%02d.%04d", 
                 timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        lv_label_set_text(date_label, buf);
    }
}

static void systray_wifi_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "WiFi tile clicked - opening WiFi settings");
    system_tray_hide();
    // First open Settings app, then show WiFi page
    app_launch("settings");
    // Small delay to let app_window be created, then show WiFi page
    lv_timer_create([](lv_timer_t *t) {
        settings_show_wifi_page();
        lv_timer_delete(t);
    }, 100, NULL);
}

static void systray_bt_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Bluetooth tile clicked - opening Bluetooth settings");
    system_tray_hide();
    // First open Settings app, then show Bluetooth page
    app_launch("settings");
    lv_timer_create([](lv_timer_t *t) {
        settings_show_bluetooth_page();
        lv_timer_delete(t);
    }, 100, NULL);
}

static void systray_settings_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Settings tile clicked");
    system_tray_hide();
    app_launch("settings");
}

static void systray_brightness_changed(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    ESP_LOGI(TAG, "Brightness changed to %ld%%", (long)value);
    hw_backlight_set((uint8_t)value);
}

// Tile references
static lv_obj_t *wifi_tile = NULL;
static lv_obj_t *bt_tile = NULL;
static lv_obj_t *settings_tile = NULL;

// Windows 10 style tile (square with icon and text)
static lv_obj_t* create_win10_tile(lv_obj_t *parent, int x, int y, int w, int h, 
                                    const char *text, bool active, lv_event_cb_t cb) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_bg_color(tile, active ? lv_color_hex(0x0078D4) : lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x4D4D4D), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 2, 0);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    
    if (cb) {
        lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);
    }
    
    // Text at bottom
    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, UI_FONT, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    return tile;
}

static void create_systray_panel(void) {
    if (systray_panel) return;
    
    ESP_LOGI(TAG, "Creating Win10 style side panel...");
    
    // Create timer for thread-safe WiFi UI updates
    static lv_timer_t *wifi_ui_timer = NULL;
    if (!wifi_ui_timer) {
        wifi_ui_timer = lv_timer_create([](lv_timer_t *t) {
            if (wifi_ui_update_needed) {
                wifi_ui_update_needed = false;
                bool connected = wifi_ui_connected_state;
                
                // Update taskbar WiFi icon
                win32_update_wifi(connected);
                
                // Update systray WiFi status label
                if (wifi_status_label) {
                    if (connected && connected_ssid[0]) {
                        lv_label_set_text(wifi_status_label, connected_ssid);
                    } else {
                        lv_label_set_text(wifi_status_label, "Not connected");
                    }
                }
                
                ESP_LOGI(TAG, "WiFi UI updated: %s", connected ? "connected" : "disconnected");
            }
        }, 500, NULL);  // Check every 500ms
    }
    
    // Side panel from RIGHT - Windows 10 Action Center style
    systray_panel = lv_obj_create(scr_desktop);
    lv_obj_set_size(systray_panel, SYSTRAY_PANEL_WIDTH, SCREEN_HEIGHT - TASKBAR_HEIGHT);
    lv_obj_set_pos(systray_panel, SCREEN_WIDTH - SYSTRAY_PANEL_WIDTH, 0);  // Position at right
    lv_obj_set_style_bg_color(systray_panel, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_bg_opa(systray_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(systray_panel, 0, 0);
    lv_obj_set_style_radius(systray_panel, 0, 0);
    lv_obj_set_style_pad_all(systray_panel, 8, 0);
    lv_obj_add_flag(systray_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(systray_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    // Tile size (2 columns)
    int tile_w = 145;
    int tile_h = 70;
    int gap = 6;
    int start_y = 10;
    
    // Row 1: WiFi, Bluetooth
    wifi_tile = create_win10_tile(systray_panel, 0, start_y, tile_w, tile_h, 
                                   "WiFi", wifi_connected, systray_wifi_clicked);
    bt_tile = create_win10_tile(systray_panel, tile_w + gap, start_y, tile_w, tile_h, 
                                 "Bluetooth", false, systray_bt_clicked);
    
    // Row 2: Settings (full width)
    settings_tile = create_win10_tile(systray_panel, 0, start_y + tile_h + gap, 
                                       tile_w * 2 + gap, tile_h, 
                                       "All Settings", false, systray_settings_clicked);
    
    int y_pos = start_y + (tile_h + gap) * 2 + 15;
    
    // Brightness section
    lv_obj_t *bright_icon = lv_label_create(systray_panel);
    lv_label_set_text(bright_icon, LV_SYMBOL_IMAGE);  // Sun icon
    lv_obj_set_style_text_color(bright_icon, lv_color_white(), 0);
    lv_obj_set_pos(bright_icon, 5, y_pos + 5);
    
    lv_obj_t *bright_slider = lv_slider_create(systray_panel);
    lv_obj_set_size(bright_slider, SYSTRAY_PANEL_WIDTH - 50, 25);
    lv_obj_set_pos(bright_slider, 35, y_pos);
    lv_slider_set_range(bright_slider, 20, 100);
    lv_slider_set_value(bright_slider, hw_backlight_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bright_slider, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bright_slider, lv_color_hex(0x0078D4), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bright_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(bright_slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(bright_slider, systray_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    y_pos += 45;
    
    // Bottom info area
    lv_obj_t *info_area = lv_obj_create(systray_panel);
    lv_obj_set_size(info_area, SYSTRAY_PANEL_WIDTH - 16, 100);
    lv_obj_set_pos(info_area, 0, SCREEN_HEIGHT - TASKBAR_HEIGHT - 120);
    lv_obj_set_style_bg_color(info_area, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(info_area, 0, 0);
    lv_obj_set_style_radius(info_area, 4, 0);
    lv_obj_set_style_pad_all(info_area, 10, 0);
    lv_obj_remove_flag(info_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Time (right side)
    time_label = lv_label_create(info_area);
    lv_label_set_text(time_label, "12:00");
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, UI_FONT, 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    
    // Date
    date_label = lv_label_create(info_area);
    lv_label_set_text(date_label, "21.12.2025");
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(date_label, UI_FONT, 0);
    lv_obj_align(date_label, LV_ALIGN_TOP_RIGHT, 0, 22);
    
    // Battery
    hw_battery_info_t batt_info;
    hw_battery_get_info(&batt_info);
    
    battery_label = lv_label_create(info_area);
    char batt_text[32];
    snprintf(batt_text, sizeof(batt_text), "Battery: %d%%", batt_info.level);
    lv_label_set_text(battery_label, batt_text);
    lv_obj_set_style_text_color(battery_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(battery_label, UI_FONT, 0);
    lv_obj_align(battery_label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Battery bar
    battery_bar = lv_bar_create(info_area);
    lv_obj_set_size(battery_bar, 120, 10);
    lv_obj_align(battery_bar, LV_ALIGN_TOP_LEFT, 0, 25);
    lv_bar_set_range(battery_bar, 0, 100);
    lv_bar_set_value(battery_bar, batt_info.level, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    uint32_t batt_color = batt_info.level > 50 ? 0x00AA00 : (batt_info.level > 20 ? 0xFFAA00 : 0xCC0000);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(batt_color), LV_PART_INDICATOR);
    
    // WiFi status
    wifi_status_label = lv_label_create(info_area);
    if (wifi_connected && connected_ssid[0]) {
        lv_label_set_text(wifi_status_label, connected_ssid);
    } else {
        lv_label_set_text(wifi_status_label, "Not connected");
    }
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(wifi_status_label, UI_FONT, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    update_datetime_display();
    ESP_LOGI(TAG, "Win10 style side panel created");
}

// ============ PUBLIC API ============

void system_tray_toggle(void) {
    if (systray_visible) {
        system_tray_hide();
    } else {
        system_tray_show();
    }
}

void system_tray_show(void) {
    if (!systray_panel) create_systray_panel();
    if (!systray_visible) {
        update_datetime_display();
        
        // Update WiFi tile state
        if (wifi_tile) {
            lv_obj_set_style_bg_color(wifi_tile, wifi_connected ? lv_color_hex(0x0078D4) : lv_color_hex(0x3D3D3D), 0);
        }
        
        // Update battery
        hw_battery_info_t batt_info;
        hw_battery_get_info(&batt_info);
        if (battery_label) {
            char batt_text[32];
            snprintf(batt_text, sizeof(batt_text), "Battery: %d%%", batt_info.level);
            lv_label_set_text(battery_label, batt_text);
        }
        if (battery_bar) {
            lv_bar_set_value(battery_bar, batt_info.level, LV_ANIM_OFF);
        }
        
        // Position off-screen to the right, then animate in
        lv_obj_set_x(systray_panel, SCREEN_WIDTH);
        lv_obj_remove_flag(systray_panel, LV_OBJ_FLAG_HIDDEN);
        
        // Move panel above all other overlays (like start menu)
        lv_obj_move_foreground(systray_panel);
        
        // Smooth slide-in animation from right
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, systray_panel);
        lv_anim_set_values(&a, SCREEN_WIDTH, SCREEN_WIDTH - SYSTRAY_PANEL_WIDTH);
        lv_anim_set_duration(&a, 200);  // 200ms for smooth feel
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
        
        systray_visible = true;
    }
}

void system_tray_hide(void) {
    if (systray_panel && systray_visible) {
        // Smooth slide-out animation to right
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, systray_panel);
        lv_anim_set_values(&a, lv_obj_get_x(systray_panel), SCREEN_WIDTH);
        lv_anim_set_duration(&a, 150);  // Slightly faster hide
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&a, [](lv_anim_t *anim) {
            if (systray_panel) {
                lv_obj_add_flag(systray_panel, LV_OBJ_FLAG_HIDDEN);
            }
        });
        lv_anim_start(&a);
        
        systray_visible = false;
    }
}

bool system_tray_is_visible(void) { return systray_visible; }
