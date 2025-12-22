/**
 * LVGL Port for ESP32-P4 with ST7701 MIPI-DSI Display
 * Uses official esp_lvgl_port component with avoid_tearing for smooth animations
 */

#include "lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "LVGL_PORT";

// Static variables
static st7701_lcd_handles_t lcd_handles;
static esp_lcd_touch_handle_t touch_handle;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

esp_err_t my_lvgl_port_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL port with esp_lvgl_port (avoid_tearing)");

    // Step 1: Initialize display driver
    ESP_LOGI(TAG, "Initializing display driver");
    esp_err_t ret = st7701_init(&lcd_handles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Initialize touch driver
    ESP_LOGI(TAG, "Initializing touch driver");
    ret = gt911_init(&touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 3: Initialize esp_lvgl_port with increased stack size
    ESP_LOGI(TAG, "Initializing esp_lvgl_port");
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 12288;  // Increase stack for JPEG decoder
    ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize lvgl_port: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 4: Add display using lvgl_port_add_disp_dsi with avoid_tearing
    ESP_LOGI(TAG, "Adding display with avoid_tearing");
    
    // Buffer size - full screen for DIRECT mode
    size_t buffer_size = LCD_H_RES * LCD_V_RES;
    
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_handles.io,
        .panel_handle = lcd_handles.panel,
        .control_handle = NULL,
        .buffer_size = buffer_size,
        .double_buffer = true,
        .trans_size = 0,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = true,  // DIRECT mode for smooth animations
        }
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = true,  // KEY: avoid tearing for smooth animations!
        }
    };

    lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to add display");
        return ESP_FAIL;
    }

    // Step 5: Add touch input
    ESP_LOGI(TAG, "Adding touch input");
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };

    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (lvgl_touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to add touch input");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL port initialized successfully");
    ESP_LOGI(TAG, "Display: %dx%d, avoid_tearing: ON, direct_mode: ON", LCD_H_RES, LCD_V_RES);
    
    return ESP_OK;
}

bool my_lvgl_port_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void my_lvgl_port_unlock(void)
{
    lvgl_port_unlock();
}
