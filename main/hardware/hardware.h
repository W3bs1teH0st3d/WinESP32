/**
 * Win32 OS - Hardware Abstraction Layer
 * Real hardware control: Backlight, Battery, Storage, Camera
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============ BACKLIGHT CONTROL ============
// Based on JC4880P443C schematic - GPIO5 is LCD_BL (backlight enable/PWM)

/**
 * Initialize backlight PWM control
 * @return ESP_OK on success
 */
esp_err_t hw_backlight_init(void);

/**
 * Set backlight brightness
 * @param percent Brightness 0-100 (0 = off, 100 = max)
 */
void hw_backlight_set(uint8_t percent);

/**
 * Get current backlight brightness
 * @return Current brightness 0-100
 */
uint8_t hw_backlight_get(void);

// ============ BATTERY MONITORING ============
// Note: JC4880P443C may not have battery ADC - check schematic
// If no battery circuit, we'll use mock data

typedef struct {
    uint8_t level;      // 0-100%
    bool charging;      // true if charging
    uint16_t voltage_mv; // Voltage in millivolts
    bool valid;         // true if battery hardware detected
} hw_battery_info_t;

/**
 * Initialize battery monitoring
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if no battery hardware
 */
esp_err_t hw_battery_init(void);

/**
 * Get battery status
 * @param info Pointer to battery info structure
 */
void hw_battery_get_info(hw_battery_info_t *info);

// ============ LITTLEFS STORAGE ============

typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    bool mounted;
} hw_littlefs_info_t;

/**
 * Initialize LittleFS filesystem
 * @return ESP_OK on success
 */
esp_err_t hw_littlefs_init(void);

/**
 * Get LittleFS storage info
 * @param info Pointer to info structure
 * @return ESP_OK on success
 */
esp_err_t hw_littlefs_get_info(hw_littlefs_info_t *info);

/**
 * Check if LittleFS is mounted
 * @return true if mounted
 */
bool hw_littlefs_is_mounted(void);

// ============ SD CARD ============

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    bool mounted;
} hw_sdcard_info_t;

/**
 * Initialize SD card (SDMMC interface)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no card
 */
esp_err_t hw_sdcard_init(void);

/**
 * Check if SD card is mounted
 * @return true if mounted
 */
bool hw_sdcard_is_mounted(void);

/**
 * Get SD card info
 * @param info Pointer to info structure
 * @return true on success
 */
bool hw_sdcard_get_info(hw_sdcard_info_t *info);

/**
 * Unmount SD card safely
 */
void hw_sdcard_unmount(void);

// ============ CAMERA (OV02C10) ============

/**
 * Camera frame callback type
 * @param data Frame data (RGB565)
 * @param width Frame width
 * @param height Frame height
 * @param user_data User data passed to callback
 */
typedef void (*hw_camera_frame_cb_t)(uint8_t *data, uint16_t width, uint16_t height, void *user_data);

/**
 * Initialize camera
 * @return ESP_OK on success
 */
esp_err_t hw_camera_init(void);

/**
 * Check if camera is initialized
 * @return true if initialized
 */
bool hw_camera_is_ready(void);

/**
 * Check if camera stream is running
 * @return true if streaming
 */
bool hw_camera_is_streaming(void);

/**
 * Start camera streaming with callback
 * @param callback Frame callback function
 * @param user_data User data for callback
 * @return ESP_OK on success
 */
esp_err_t hw_camera_start_stream(hw_camera_frame_cb_t callback, void *user_data);

/**
 * Stop camera streaming
 */
void hw_camera_stop_stream(void);

/**
 * Get camera frame buffer (blocking)
 * @param width Output width
 * @param height Output height
 * @param data Output data pointer (RGB565)
 * @return ESP_OK on success
 */
esp_err_t hw_camera_get_frame(uint16_t *width, uint16_t *height, uint8_t **data);

/**
 * Release camera frame buffer
 */
void hw_camera_release_frame(void);

/**
 * Capture photo to file
 * @param path File path to save (JPEG)
 * @return ESP_OK on success
 */
esp_err_t hw_camera_capture_to_file(const char *path);

/**
 * Deinitialize camera and release resources
 */
void hw_camera_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_H
