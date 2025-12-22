/**
 * Win Recovery - System Information Module
 * Implementation of system info collection
 */

#include "recovery_sysinfo.h"
#include <string.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_littlefs.h"
#include "sdmmc_cmd.h"
#include "hardware/hardware.h"

static const char *TAG __attribute__((unused)) = "RecoverySysInfo";

void recovery_get_sysinfo(recovery_sysinfo_t *info)
{
    if (!info) return;
    memset(info, 0, sizeof(recovery_sysinfo_t));
    
    // Chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    const char *chip_model_str = "Unknown";
    switch (chip_info.model) {
        case CHIP_ESP32:   chip_model_str = "ESP32"; break;
        case CHIP_ESP32S2: chip_model_str = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_model_str = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_model_str = "ESP32-C3"; break;
        case CHIP_ESP32C2: chip_model_str = "ESP32-C2"; break;
        case CHIP_ESP32C6: chip_model_str = "ESP32-C6"; break;
        case CHIP_ESP32H2: chip_model_str = "ESP32-H2"; break;
        case CHIP_ESP32P4: chip_model_str = "ESP32-P4"; break;
        default: chip_model_str = "ESP32-Unknown"; break;
    }
    snprintf(info->chip_model, sizeof(info->chip_model), "%s", chip_model_str);
    info->chip_revision = chip_info.revision;
    info->cores = chip_info.cores;
    
    // CPU frequency (ESP32-P4 runs at 400MHz)
    info->cpu_freq_mhz = 400;  // Default for ESP32-P4
    
    // Flash size - use CONFIG value or default
    #ifdef CONFIG_ESPTOOLPY_FLASHSIZE_16MB
    info->flash_size_mb = 16;
    #elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
    info->flash_size_mb = 8;
    #elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_4MB)
    info->flash_size_mb = 4;
    #else
    info->flash_size_mb = 16;  // Default for ESP32-P4
    #endif
    
    // PSRAM info
    info->total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    info->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    info->psram_size_mb = info->total_psram / (1024 * 1024);
    
    // Heap info
    info->total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    info->free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    info->free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    info->min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    // LittleFS info
    size_t total_bytes = 0, used_bytes = 0;
    if (esp_littlefs_info("storage", &total_bytes, &used_bytes) == ESP_OK) {
        info->littlefs_mounted = true;
        info->littlefs_total = total_bytes;
        info->littlefs_used = used_bytes;
    } else {
        info->littlefs_mounted = false;
    }
    
    // SD Card info
    hw_sdcard_info_t sd_info;
    if (hw_sdcard_get_info(&sd_info) && sd_info.mounted) {
        info->sd_mounted = true;
        info->sd_total = sd_info.total_bytes;
        info->sd_free = sd_info.free_bytes;
        snprintf(info->sd_type, sizeof(info->sd_type), "SD");
    } else {
        info->sd_mounted = false;
        snprintf(info->sd_type, sizeof(info->sd_type), "Not inserted");
    }
    
    // WiFi MAC address
    esp_read_mac(info->wifi_mac, ESP_MAC_WIFI_STA);
    snprintf(info->wifi_mac_str, sizeof(info->wifi_mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             info->wifi_mac[0], info->wifi_mac[1], info->wifi_mac[2],
             info->wifi_mac[3], info->wifi_mac[4], info->wifi_mac[5]);
    
    // Reset reason
    info->reset_reason = esp_reset_reason();
    
    // Uptime
    info->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);
    
    // Version info
    snprintf(info->idf_version, sizeof(info->idf_version), "%s", esp_get_idf_version());
    
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        snprintf(info->app_version, sizeof(info->app_version), "%s", app_desc->version);
        snprintf(info->compile_date, sizeof(info->compile_date), "%s", app_desc->date);
        snprintf(info->compile_time, sizeof(info->compile_time), "%s", app_desc->time);
    }
}

const char* recovery_get_reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "Unknown";
        case ESP_RST_POWERON:   return "Power On";
        case ESP_RST_EXT:       return "External Reset";
        case ESP_RST_SW:        return "Software Reset";
        case ESP_RST_PANIC:     return "Exception/Panic";
        case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT:  return "Task Watchdog";
        case ESP_RST_WDT:       return "Other Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep Wake";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO Reset";
        default:                return "Unknown";
    }
}

char* recovery_format_bytes(uint64_t bytes, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return buffer;
    
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", (double)bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%llu B", (unsigned long long)bytes);
    }
    return buffer;
}

int recovery_get_partition_info(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return 0;
    
    buffer[0] = '\0';
    int count = 0;
    size_t offset = 0;
    
    // Header
    offset += snprintf(buffer + offset, buffer_size - offset,
                       "%-16s %-8s %-10s %-10s\n",
                       "Name", "Type", "Offset", "Size");
    offset += snprintf(buffer + offset, buffer_size - offset,
                       "------------------------------------------------\n");
    
    // Iterate all partitions
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, 
                                                      ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        if (part) {
            const char *type_str = "unknown";
            if (part->type == ESP_PARTITION_TYPE_APP) {
                type_str = "app";
            } else if (part->type == ESP_PARTITION_TYPE_DATA) {
                type_str = "data";
            }
            
            char size_str[16];
            recovery_format_bytes(part->size, size_str, sizeof(size_str));
            
            offset += snprintf(buffer + offset, buffer_size - offset,
                               "%-16s %-8s 0x%08lx %-10s\n",
                               part->label, type_str, 
                               (unsigned long)part->address, size_str);
            count++;
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    
    return count;
}
