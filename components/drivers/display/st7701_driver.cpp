#include "st7701_driver.h"
#include "st7701_lcd.h"
#include "esp_log.h"

static const char *TAG = "ST7701";

static st7701_lcd *lcd_instance = nullptr;
static bsp_lcd_handles_t bsp_handles;

esp_err_t st7701_init(st7701_lcd_handles_t *handles)
{
    ESP_LOGI(TAG, "Initializing ST7701 display driver");
    
    if (handles == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create ST7701 LCD instance
    lcd_instance = new st7701_lcd(-1);  // No reset pin
    if (lcd_instance == NULL) {
        ESP_LOGE(TAG, "Failed to create ST7701 instance");
        return ESP_ERR_NO_MEM;
    }

    // Initialize LCD
    lcd_instance->begin();
    
    // Get handles from BSP
    lcd_instance->get_handle(&bsp_handles);
    
    // Copy handles to output (convert bsp_lcd_handles_t to st7701_lcd_handles_t)
    handles->mipi_dsi_bus = bsp_handles.mipi_dsi_bus;
    handles->io = bsp_handles.io;
    handles->panel = bsp_handles.panel;
    handles->control = bsp_handles.control;
    
    ESP_LOGI(TAG, "ST7701 display driver initialized successfully");
    ESP_LOGI(TAG, "Resolution: %dx%d", lcd_instance->width(), lcd_instance->height());
    
    return ESP_OK;
}

esp_err_t st7701_set_backlight(uint8_t level)
{
    if (lcd_instance != NULL) {
        lcd_instance->example_bsp_set_lcd_backlight(level);
    }
    return ESP_OK;
}

esp_lcd_panel_handle_t st7701_get_panel_handle(st7701_lcd_handles_t *handles)
{
    return handles ? handles->panel : NULL;
}
