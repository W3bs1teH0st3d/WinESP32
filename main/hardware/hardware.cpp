/**
 * Win32 OS - Hardware Abstraction Layer Implementation
 * Real hardware control for ESP32-P4 JC4880P443C board
 */

#include "hardware.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "HARDWARE";

// ============ PIN DEFINITIONS (from JC4880P443C schematic) ============
#define PIN_LCD_BACKLIGHT   GPIO_NUM_23  // LCD_BL - backlight control (from demo firmware)
#define PIN_SD_CMD          GPIO_NUM_43  // SD_CMD
#define PIN_SD_CLK          GPIO_NUM_44  // SD_CLK
#define PIN_SD_D0           GPIO_NUM_39  // SD_D0
#define PIN_SD_D1           GPIO_NUM_40  // SD_D1
#define PIN_SD_D2           GPIO_NUM_41  // SD_D2
#define PIN_SD_D3           GPIO_NUM_42  // SD_D3
#define PIN_SD_DET          GPIO_NUM_21  // SD card detect (active low)

// LEDC configuration for backlight PWM (use timer 1 like demo firmware)
#define LEDC_TIMER          LEDC_TIMER_1
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT  // 0-1023
#define LEDC_FREQUENCY      5000  // 5kHz PWM

// State
static uint8_t current_brightness = 80;
static bool backlight_initialized = false;
static bool littlefs_mounted = false;
static bool sdcard_mounted = false;
static sdmmc_card_t *sdcard = NULL;

// ============ BACKLIGHT CONTROL ============

esp_err_t hw_backlight_init(void)
{
    if (backlight_initialized) return ESP_OK;
    
    ESP_LOGI(TAG, "Initializing backlight PWM on GPIO%d", PIN_LCD_BACKLIGHT);
    
    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t channel_conf = {
        .gpio_num = PIN_LCD_BACKLIGHT,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Load saved brightness from NVS
    nvs_handle_t nvs;
    if (nvs_open("hw_config", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t saved_brightness;
        if (nvs_get_u8(nvs, "brightness", &saved_brightness) == ESP_OK) {
            current_brightness = saved_brightness;
            ESP_LOGI(TAG, "Loaded brightness from NVS: %d%%", current_brightness);
        }
        nvs_close(nvs);
    }
    
    backlight_initialized = true;
    hw_backlight_set(current_brightness);
    
    ESP_LOGI(TAG, "Backlight initialized, brightness: %d%%", current_brightness);
    return ESP_OK;
}

void hw_backlight_set(uint8_t percent)
{
    if (!backlight_initialized) {
        ESP_LOGW(TAG, "Backlight not initialized, initializing now...");
        hw_backlight_init();
    }
    
    // Clamp to 10-100% (never fully off for safety)
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    
    current_brightness = percent;
    
    // Convert percent to duty cycle (0-1023)
    uint32_t duty = (percent * 1023) / 100;
    
    ESP_LOGI(TAG, "Setting backlight to %d%% (duty: %lu)", percent, (unsigned long)duty);
    
    esp_err_t ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Save to NVS
    nvs_handle_t nvs;
    if (nvs_open("hw_config", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "brightness", percent);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    
    ESP_LOGD(TAG, "Backlight set to %d%% (duty: %lu)", percent, (unsigned long)duty);
}

uint8_t hw_backlight_get(void)
{
    return current_brightness;
}

// ============ BATTERY MONITORING ============
// JC4880P443C board has MX1.25 2P lithium battery interface
// Battery ADC pin is not documented - using mock data with USB power detection

#include "driver/gpio.h"

// USB power detection - when USB is connected, we're "charging"
// Note: The actual battery ADC pin is unknown from documentation
// If you find the ADC pin, update PIN_BATTERY_ADC below
#define PIN_BATTERY_ADC     GPIO_NUM_NC  // Unknown - needs schematic analysis
#define PIN_USB_VBUS        GPIO_NUM_NC  // USB VBUS detection (if available)

static bool battery_initialized = false;
static hw_battery_info_t battery_info = {
    .level = 85,
    .charging = true,  // Assume USB powered = charging
    .voltage_mv = 4100,
    .valid = false  // No real battery ADC detected
};

esp_err_t hw_battery_init(void)
{
    if (battery_initialized) return ESP_OK;
    
    ESP_LOGI(TAG, "Battery monitoring: JC4880P443C has battery interface");
    ESP_LOGI(TAG, "Battery ADC pin not documented - using simulated data");
    ESP_LOGI(TAG, "Board is USB powered - showing as charging");
    
    // The board has a battery connector but the ADC pin for voltage
    // measurement is not documented. When the actual pin is found,
    // implement ADC reading here.
    
    // For now, assume USB powered = charging at high level
    battery_info.charging = true;
    battery_info.level = 85;
    battery_info.voltage_mv = 4100;
    battery_info.valid = false;  // Mark as simulated
    
    battery_initialized = true;
    return ESP_OK;  // Return OK even though it's simulated
}

void hw_battery_get_info(hw_battery_info_t *info)
{
    if (!info) return;
    
    // Return current battery info
    *info = battery_info;
    
    // Simulate battery behavior based on "charging" state
    static int tick = 0;
    tick++;
    
    if (battery_info.charging) {
        // Charging - slowly increase level
        if (tick % 120 == 0 && battery_info.level < 100) {
            battery_info.level++;
            battery_info.voltage_mv = 3300 + (battery_info.level * 9);
        }
    } else {
        // Discharging - slowly decrease level
        if (tick % 60 == 0 && battery_info.level > 10) {
            battery_info.level--;
            battery_info.voltage_mv = 3300 + (battery_info.level * 9);
        }
    }
    
    // Update output
    *info = battery_info;
}


// ============ LITTLEFS STORAGE ============

esp_err_t hw_littlefs_init(void)
{
    if (littlefs_mounted) return ESP_OK;
    
    ESP_LOGI(TAG, "Initializing LittleFS");
    
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false
    };
    
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format LittleFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    littlefs_mounted = true;
    
    size_t total, used;
    ret = esp_littlefs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted: %zu KB total, %zu KB used", total/1024, used/1024);
    }
    
    // Create default directories
    mkdir("/littlefs/notes", 0755);
    mkdir("/littlefs/photos", 0755);
    mkdir("/littlefs/config", 0755);
    
    return ESP_OK;
}

esp_err_t hw_littlefs_get_info(hw_littlefs_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    
    info->mounted = littlefs_mounted;
    info->total_bytes = 0;
    info->used_bytes = 0;
    
    if (!littlefs_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return esp_littlefs_info("storage", &info->total_bytes, &info->used_bytes);
}

bool hw_littlefs_is_mounted(void)
{
    return littlefs_mounted;
}

// ============ SD CARD ============

esp_err_t hw_sdcard_init(void)
{
    if (sdcard_mounted) return ESP_OK;
    
    ESP_LOGI(TAG, "Initializing SD card (SDMMC 4-bit mode)");
    
    // Check card detect pin first
    gpio_config_t det_conf = {
        .pin_bit_mask = (1ULL << PIN_SD_DET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&det_conf);
    
    // Card detect is active low
    if (gpio_get_level(PIN_SD_DET) == 1) {
        ESP_LOGW(TAG, "No SD card detected");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Configure SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40MHz
    
    // Configure slot
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // 4-bit mode
    slot_config.clk = PIN_SD_CLK;
    slot_config.cmd = PIN_SD_CMD;
    slot_config.d0 = PIN_SD_D0;
    slot_config.d1 = PIN_SD_D1;
    slot_config.d2 = PIN_SD_D2;
    slot_config.d3 = PIN_SD_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    // Mount filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &sdcard);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SD card filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    sdcard_mounted = true;
    
    // Print card info
    sdmmc_card_print_info(stdout, sdcard);
    ESP_LOGI(TAG, "SD card mounted at /sdcard");
    
    return ESP_OK;
}

bool hw_sdcard_is_mounted(void)
{
    return sdcard_mounted;
}

bool hw_sdcard_get_info(hw_sdcard_info_t *info)
{
    if (!info) return false;
    
    info->mounted = sdcard_mounted;
    info->total_bytes = 0;
    info->used_bytes = 0;
    info->free_bytes = 0;
    
    if (!sdcard_mounted || !sdcard) {
        return false;
    }
    
    // Calculate total size from card info
    info->total_bytes = (uint64_t)sdcard->csd.capacity * sdcard->csd.sector_size;
    
    // Get free space using FATFS
    FATFS *fs;
    DWORD fre_clust;
    if (f_getfree("/sdcard", &fre_clust, &fs) == FR_OK) {
        uint64_t free_sectors = fre_clust * fs->csize;
        info->free_bytes = free_sectors * sdcard->csd.sector_size;
        info->used_bytes = info->total_bytes - info->free_bytes;
    }
    
    return true;
}

void hw_sdcard_unmount(void)
{
    if (sdcard_mounted) {
        esp_vfs_fat_sdcard_unmount("/sdcard", sdcard);
        sdcard = NULL;
        sdcard_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

// ============ CAMERA (OV02C10 via MIPI-CSI using V4L2) ============
// Based on esp_video component - uses V4L2 API
// Camera shares I2C bus with touch controller (GPIO 7/8)

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"
#include "gt911_driver.h"  // For getting shared I2C bus handle

// Camera configuration - matches JC4880P443C board
#define CAM_I2C_PORT        0             // I2C port 0
#define CAM_I2C_SCL_PIN     GPIO_NUM_8    // Shared with touch controller
#define CAM_I2C_SDA_PIN     GPIO_NUM_7    // Shared with touch controller
#define CAM_I2C_FREQ        100000        // 100kHz
#define CAM_RESET_PIN       GPIO_NUM_NC   // Not used on this board
#define CAM_PWDN_PIN        GPIO_NUM_NC   // Not used on this board
#define CAM_BUF_COUNT       2
#define CAM_WIDTH           480           // Scaled for display
#define CAM_HEIGHT          800           // Scaled for display

static bool camera_initialized = false;
static bool camera_video_initialized = false;
static int camera_fd = -1;
static uint8_t *camera_buffers[CAM_BUF_COUNT] = {NULL};
static size_t camera_buf_size = 0;
static uint32_t camera_width = 0;
static uint32_t camera_height = 0;
static struct v4l2_buffer current_v4l2_buf;
static bool frame_acquired = false;

esp_err_t hw_camera_init(void)
{
    if (camera_initialized) return ESP_OK;
    
    ESP_LOGI(TAG, "Initializing camera (OV02C10 via MIPI-CSI)");
    ESP_LOGI(TAG, "Camera I2C: SCL=%d, SDA=%d, Freq=%d", CAM_I2C_SCL_PIN, CAM_I2C_SDA_PIN, CAM_I2C_FREQ);
    
    // Suppress ISP/IPA debug spam that can freeze UI
    esp_log_level_set("esp_ipa", ESP_LOG_WARN);
    esp_log_level_set("esp_isp", ESP_LOG_WARN);
    esp_log_level_set("isp_pipeline", ESP_LOG_WARN);
    esp_log_level_set("esp_video", ESP_LOG_WARN);
    esp_log_level_set("cam_sensor", ESP_LOG_INFO);
    
    // Initialize esp_video only once
    if (!camera_video_initialized) {
        // Get the existing I2C bus handle from touch driver
        i2c_master_bus_handle_t i2c_handle = gt911_get_i2c_handle();
        
        ESP_LOGI(TAG, "I2C handle from touch driver: %p", (void*)i2c_handle);
        
        // Initialize esp_video with CSI config
        // IMPORTANT: Must set init_sccb = false when reusing existing I2C bus
        esp_video_init_csi_config_t csi_config = {
            .sccb_config = {
                .init_sccb = false,  // Always false - we either reuse touch I2C or it fails
                .i2c_config = {
                    .port = CAM_I2C_PORT,
                    .scl_pin = CAM_I2C_SCL_PIN,
                    .sda_pin = CAM_I2C_SDA_PIN,
                },
                .freq = CAM_I2C_FREQ,
            },
            .reset_pin = CAM_RESET_PIN,
            .pwdn_pin = CAM_PWDN_PIN,
        };
        
        // We MUST have an existing I2C bus from touch driver
        if (i2c_handle != NULL) {
            ESP_LOGI(TAG, "Using existing I2C bus from touch driver (handle=%p)", (void*)i2c_handle);
            csi_config.sccb_config.i2c_handle = i2c_handle;
        } else {
            ESP_LOGE(TAG, "No I2C bus from touch driver! Touch must be initialized first.");
            ESP_LOGE(TAG, "Camera cannot initialize without shared I2C bus.");
            return ESP_ERR_INVALID_STATE;
        }
        
        esp_video_init_config_t cam_config = {
            .csi = &csi_config,
        };
        
        ESP_LOGI(TAG, "Calling esp_video_init with init_sccb=%d", csi_config.sccb_config.init_sccb);
        esp_err_t ret = esp_video_init(&cam_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(ret));
            ESP_LOGE(TAG, "Camera sensor may not be detected");
            return ret;
        }
        camera_video_initialized = true;
        ESP_LOGI(TAG, "esp_video initialized successfully");
    }
    
    // Open video device
    ESP_LOGI(TAG, "Opening camera device: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    camera_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (camera_fd < 0) {
        ESP_LOGE(TAG, "Failed to open camera device: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        ESP_LOGE(TAG, "errno: %d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    
    // Query capabilities
    struct v4l2_capability cap;
    if (ioctl(camera_fd, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "Failed to query camera capabilities");
        close(camera_fd);
        camera_fd = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Camera: %s, driver: %s", cap.card, cap.driver);
    
    // Get current format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGE(TAG, "Failed to get camera format");
        close(camera_fd);
        camera_fd = -1;
        return ESP_FAIL;
    }
    
    camera_width = fmt.fmt.pix.width;
    camera_height = fmt.fmt.pix.height;
    ESP_LOGI(TAG, "Camera resolution: %lux%lu", (unsigned long)camera_width, (unsigned long)camera_height);
    
    // Set RGB565 format for display compatibility
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(camera_fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "Failed to set RGB565 format, using default");
    }
    
    // Request buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = CAM_BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(camera_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "Failed to request camera buffers");
        close(camera_fd);
        camera_fd = -1;
        return ESP_FAIL;
    }
    
    // Map buffers
    for (int i = 0; i < CAM_BUF_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(camera_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "Failed to query buffer %d", i);
            close(camera_fd);
            camera_fd = -1;
            return ESP_FAIL;
        }
        
        camera_buffers[i] = (uint8_t*)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, 
                                            MAP_SHARED, camera_fd, buf.m.offset);
        if (camera_buffers[i] == NULL) {
            ESP_LOGE(TAG, "Failed to mmap buffer %d", i);
            close(camera_fd);
            camera_fd = -1;
            return ESP_FAIL;
        }
        camera_buf_size = buf.length;
        
        // Queue buffer
        if (ioctl(camera_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "Failed to queue buffer %d", i);
            close(camera_fd);
            camera_fd = -1;
            return ESP_FAIL;
        }
    }
    
    // Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "Failed to start camera stream");
        close(camera_fd);
        camera_fd = -1;
        return ESP_FAIL;
    }
    
    camera_initialized = true;
    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

bool hw_camera_is_ready(void)
{
    return camera_initialized;
}

// ============ CAMERA STREAMING ============

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static TaskHandle_t camera_stream_task_handle = NULL;
static hw_camera_frame_cb_t camera_frame_callback = NULL;
static void *camera_callback_user_data = NULL;
static volatile bool camera_streaming = false;
static SemaphoreHandle_t camera_stream_mutex = NULL;

static void camera_stream_task(void *arg)
{
    ESP_LOGI(TAG, "Camera stream task started");
    
    while (camera_streaming) {
        uint16_t w, h;
        uint8_t *data;
        
        esp_err_t ret = hw_camera_get_frame(&w, &h, &data);
        if (ret == ESP_OK && camera_frame_callback != NULL) {
            // Call the callback with frame data
            camera_frame_callback(data, w, h, camera_callback_user_data);
            hw_camera_release_frame();
        } else {
            // Small delay on error to prevent tight loop
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        // Yield to other tasks and feed watchdog
        // Limit to ~15 FPS to reduce CPU load
        vTaskDelay(pdMS_TO_TICKS(66));
    }
    
    ESP_LOGI(TAG, "Camera stream task stopped");
    camera_stream_task_handle = NULL;
    vTaskDelete(NULL);
}

bool hw_camera_is_streaming(void)
{
    return camera_streaming;
}

esp_err_t hw_camera_start_stream(hw_camera_frame_cb_t callback, void *user_data)
{
    if (!camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (camera_streaming) {
        ESP_LOGW(TAG, "Camera already streaming");
        return ESP_OK;
    }
    
    if (callback == NULL) {
        ESP_LOGE(TAG, "Callback is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    camera_frame_callback = callback;
    camera_callback_user_data = user_data;
    camera_streaming = true;
    
    // Create stream task on core 1 to not block UI on core 0
    BaseType_t result = xTaskCreatePinnedToCore(
        camera_stream_task,
        "cam_stream",
        4096,
        NULL,
        5,  // Priority
        &camera_stream_task_handle,
        1   // Core 1
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera stream task");
        camera_streaming = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Camera streaming started");
    return ESP_OK;
}

void hw_camera_stop_stream(void)
{
    if (!camera_streaming) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping camera stream...");
    camera_streaming = false;
    
    // Wait for task to finish
    int timeout = 100;  // 1 second timeout
    while (camera_stream_task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }
    
    camera_frame_callback = NULL;
    camera_callback_user_data = NULL;
    ESP_LOGI(TAG, "Camera stream stopped");
}

esp_err_t hw_camera_get_frame(uint16_t *width, uint16_t *height, uint8_t **data)
{
    if (!camera_initialized || camera_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Dequeue buffer
    memset(&current_v4l2_buf, 0, sizeof(current_v4l2_buf));
    current_v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    current_v4l2_buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(camera_fd, VIDIOC_DQBUF, &current_v4l2_buf) != 0) {
        ESP_LOGE(TAG, "Failed to dequeue camera buffer");
        return ESP_FAIL;
    }
    
    *width = (uint16_t)camera_width;
    *height = (uint16_t)camera_height;
    *data = camera_buffers[current_v4l2_buf.index];
    frame_acquired = true;
    
    return ESP_OK;
}

void hw_camera_release_frame(void)
{
    if (!camera_initialized || camera_fd < 0 || !frame_acquired) {
        return;
    }
    
    // Re-queue buffer
    if (ioctl(camera_fd, VIDIOC_QBUF, &current_v4l2_buf) != 0) {
        ESP_LOGE(TAG, "Failed to re-queue camera buffer");
    }
    frame_acquired = false;
}

esp_err_t hw_camera_capture_to_file(const char *path)
{
    if (!camera_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint16_t w, h;
    uint8_t *data;
    
    esp_err_t ret = hw_camera_get_frame(&w, &h, &data);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Save as raw RGB565 for now (JPEG encoding requires esp_jpeg)
    FILE *f = fopen(path, "wb");
    if (f) {
        // Write simple header: width, height
        fwrite(&w, sizeof(w), 1, f);
        fwrite(&h, sizeof(h), 1, f);
        fwrite(data, 1, camera_buf_size, f);
        fclose(f);
        ESP_LOGI(TAG, "Captured frame to %s (%dx%d)", path, w, h);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        ret = ESP_FAIL;
    }
    
    hw_camera_release_frame();
    return ret;
}

void hw_camera_deinit(void)
{
    // Stop streaming first
    hw_camera_stop_stream();
    
    if (camera_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(camera_fd, VIDIOC_STREAMOFF, &type);
        close(camera_fd);
        camera_fd = -1;
    }
    camera_initialized = false;
    ESP_LOGI(TAG, "Camera deinitialized");
}
