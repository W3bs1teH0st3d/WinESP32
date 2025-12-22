/**
 * Win Recovery - System Information Module
 * Collects hardware and system info for recovery mode display
 */

#ifndef RECOVERY_SYSINFO_H
#define RECOVERY_SYSINFO_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// System information structure
typedef struct {
    // Chip info
    char chip_model[32];
    uint8_t chip_revision;
    uint8_t cores;
    uint32_t cpu_freq_mhz;
    
    // Memory info
    uint32_t flash_size_mb;
    uint32_t psram_size_mb;
    size_t free_heap;
    size_t total_heap;
    size_t free_psram;
    size_t total_psram;
    size_t free_internal;
    size_t min_free_heap;
    
    // Storage info
    size_t littlefs_total;
    size_t littlefs_used;
    bool littlefs_mounted;
    
    // SD Card info
    bool sd_mounted;
    uint64_t sd_total;
    uint64_t sd_free;
    char sd_type[16];
    
    // Network info
    uint8_t wifi_mac[6];
    char wifi_mac_str[18];
    
    // System info
    esp_reset_reason_t reset_reason;
    uint32_t uptime_seconds;
    char idf_version[32];
    char app_version[32];
    char compile_date[32];
    char compile_time[16];
} recovery_sysinfo_t;

/**
 * Get comprehensive system information
 * @param info Pointer to structure to fill
 */
void recovery_get_sysinfo(recovery_sysinfo_t *info);

/**
 * Get human-readable reset reason string
 * @param reason Reset reason enum value
 * @return String description of reset reason
 */
const char* recovery_get_reset_reason_str(esp_reset_reason_t reason);

/**
 * Format bytes to human-readable string (KB, MB, GB)
 * @param bytes Number of bytes
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Pointer to buffer
 */
char* recovery_format_bytes(uint64_t bytes, char *buffer, size_t buffer_size);

/**
 * Get partition information as formatted string
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of partitions found
 */
int recovery_get_partition_info(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // RECOVERY_SYSINFO_H
