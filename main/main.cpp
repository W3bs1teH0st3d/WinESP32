#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "esp_lvgl_port.h"
#include "ui/win32_ui.h"
#include "hardware/hardware.h"
#include "system_settings.h"
#include "boot_button.h"
#include "recovery_trigger.h"
#include "recovery_ui.h"

static const char *TAG = "Win32";

// App launch handler
static void on_app_launch(const char* app_name)
{
    ESP_LOGI(TAG, "Launching app: %s", app_name);
    app_launch(app_name);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "   Win32 OS - ESP32-P4");
    ESP_LOGI(TAG, "   Windows Vista Style PDA");
    ESP_LOGI(TAG, "=================================");
    
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Print memory info
    ESP_LOGI(TAG, "Free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // Initialize hardware abstraction layer
    ESP_LOGI(TAG, "Initializing hardware");
    hw_backlight_init();
    hw_battery_init();
    hw_littlefs_init();
    hw_sdcard_init();  // Will fail gracefully if no card
    
    // Initialize BOOT button
    ESP_LOGI(TAG, "Initializing BOOT button");
    boot_button_init();
    
    // Check if BOOT button held at startup - enter recovery mode
    if (boot_button_check_held_at_boot()) {
        ESP_LOGW(TAG, "BOOT button held at startup - entering Recovery Mode");
        recovery_request_reboot();
    }
    
    // Initialize system settings (after LittleFS)
    ESP_LOGI(TAG, "Initializing system settings");
    settings_init();
    
    // Initialize LVGL port (display + touch + LVGL)
    ESP_LOGI(TAG, "Initializing LVGL port");
    ret = my_lvgl_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL port: %s", esp_err_to_name(ret));
        return;
    }
    
    // Check if recovery mode was requested (via RTC flag)
    if (recovery_check_flag()) {
        ESP_LOGW(TAG, "Recovery flag set - entering Recovery Mode");
        if (lvgl_port_lock(0)) {
            recovery_ui_start();
            lvgl_port_unlock();
        }
        
        // Recovery mode main loop
        while (recovery_ui_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // Poll BOOT button for navigation
            boot_button_event_t btn_event = boot_button_get_event();
            if (btn_event != BOOT_BTN_NONE) {
                if (lvgl_port_lock(100)) {
                    recovery_ui_handle_button(btn_event);
                    lvgl_port_unlock();
                }
            }
        }
        // If we exit recovery, we'll reboot
        return;
    }
    
    // Normal boot - increment boot counter
    recovery_increment_boot_count();
    
    // Initialize Win32 UI
    ESP_LOGI(TAG, "Initializing Win32 UI");
    if (lvgl_port_lock(0)) {
        win32_ui_init();
        win32_set_app_launch_callback(on_app_launch);
        win32_show_boot_screen();
        lvgl_port_unlock();
    }
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi");
    system_wifi_init();
    
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "   Win32 OS Started!");
    ESP_LOGI(TAG, "=================================");
    
    // Main loop - monitor memory, update battery, and handle BOOT button
    int counter = 0;
    int button_poll_counter = 0;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(50));  // Poll every 50ms for responsive button handling
        
        // Poll BOOT button for events
        boot_button_event_t btn_event = boot_button_get_event();
        if (btn_event != BOOT_BTN_NONE) {
            if (lvgl_port_lock(100)) {
                switch (btn_event) {
                    case BOOT_BTN_SINGLE:
                        // Single press - toggle AOD/wake
                        ESP_LOGI(TAG, "BOOT single press - power button action");
                        win32_power_button_pressed();
                        break;
                    case BOOT_BTN_TRIPLE:
                        // Triple press - show recovery confirmation
                        ESP_LOGW(TAG, "BOOT triple press - recovery trigger!");
                        // Show recovery dialog (will be handled by UI)
                        win32_show_recovery_dialog();
                        break;
                    case BOOT_BTN_LONG:
                        // Long press - reserved for future use
                        ESP_LOGI(TAG, "BOOT long press - reserved");
                        break;
                    default:
                        break;
                }
                lvgl_port_unlock();
            }
        }
        
        button_poll_counter++;
        
        // Every 5 seconds (100 * 50ms = 5000ms)
        if (button_poll_counter >= 100) {
            button_poll_counter = 0;
            
            // Update battery status in UI
            hw_battery_info_t batt;
            hw_battery_get_info(&batt);
            if (lvgl_port_lock(100)) {
                win32_update_battery(batt.level, batt.charging);
                lvgl_port_unlock();
            }
            
            if (counter % 6 == 0) {
                ESP_LOGI(TAG, "System running... Free heap: %u KB, Free PSRAM: %u KB",
                         (unsigned int)(esp_get_free_heap_size() / 1024),
                         (unsigned int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            }
            counter++;
        }
    }
}
