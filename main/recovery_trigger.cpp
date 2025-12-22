/**
 * Win Recovery - Recovery Mode Trigger Implementation
 * Uses NVS to persist data across reboot (RTC_DATA_ATTR doesn't work on ESP32-P4)
 */

#include "recovery_trigger.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RECOVERY";

// NVS namespace and keys
#define NVS_NAMESPACE "recovery"
#define NVS_KEY_MAGIC "magic"
#define NVS_KEY_MODE "mode"
#define NVS_KEY_BOOT_COUNT "boot_cnt"

// Local cache
static uint32_t cached_magic = 0;
static uint8_t cached_mode = 0;
static uint32_t cached_boot_count = 0;
static bool cache_loaded = false;

static void load_from_nvs(void)
{
    if (cache_loaded) return;
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_u32(handle, NVS_KEY_MAGIC, &cached_magic);
        nvs_get_u8(handle, NVS_KEY_MODE, &cached_mode);
        nvs_get_u32(handle, NVS_KEY_BOOT_COUNT, &cached_boot_count);
        nvs_close(handle);
    }
    cache_loaded = true;
}

static void save_magic_to_nvs(uint32_t magic)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u32(handle, NVS_KEY_MAGIC, magic);
        nvs_commit(handle);
        nvs_close(handle);
        cached_magic = magic;
    }
}

bool recovery_check_flag(void)
{
    load_from_nvs();
    bool is_recovery = (cached_magic == RECOVERY_MAGIC);
    ESP_LOGI(TAG, "Recovery flag check: %s (magic=0x%08lX)", 
             is_recovery ? "SET" : "NOT SET", 
             (unsigned long)cached_magic);
    return is_recovery;
}

void recovery_request_reboot(void)
{
    ESP_LOGW(TAG, "Recovery mode requested - setting flag and rebooting");
    save_magic_to_nvs(RECOVERY_MAGIC);
    // Small delay to ensure NVS write completes
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

void recovery_clear_flag(void)
{
    ESP_LOGI(TAG, "Clearing recovery flag");
    save_magic_to_nvs(0);
}

recovery_display_mode_t recovery_get_preferred_mode(void)
{
    load_from_nvs();
    if (cached_mode > RECOVERY_MODE_CONSOLE) {
        return RECOVERY_MODE_SELECT;
    }
    return (recovery_display_mode_t)cached_mode;
}

void recovery_set_preferred_mode(recovery_display_mode_t mode)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_MODE, (uint8_t)mode);
        nvs_commit(handle);
        nvs_close(handle);
        cached_mode = (uint8_t)mode;
    }
    ESP_LOGI(TAG, "Preferred mode set to: %d", mode);
}

uint32_t recovery_get_boot_count(void)
{
    load_from_nvs();
    return cached_boot_count;
}

void recovery_increment_boot_count(void)
{
    load_from_nvs();
    cached_boot_count++;
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u32(handle, NVS_KEY_BOOT_COUNT, cached_boot_count);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Boot count: %lu", (unsigned long)cached_boot_count);
}
