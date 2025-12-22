#ifndef ST7701_DRIVER_H
#define ST7701_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

// LCD configuration
#define LCD_H_RES 480
#define LCD_V_RES 800
#define LCD_BIT_PER_PIXEL 16

// LCD handles structure
typedef struct {
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_handle_t control;
} st7701_lcd_handles_t;

/**
 * @brief Initialize ST7701 display driver
 * 
 * @param handles Pointer to store LCD handles
 * @return esp_err_t ESP_OK on success
 */
esp_err_t st7701_init(st7701_lcd_handles_t *handles);

/**
 * @brief Set LCD backlight level
 * 
 * @param level Brightness level (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t st7701_set_backlight(uint8_t level);

/**
 * @brief Get LCD panel handle
 * 
 * @param handles LCD handles structure
 * @return esp_lcd_panel_handle_t Panel handle
 */
esp_lcd_panel_handle_t st7701_get_panel_handle(st7701_lcd_handles_t *handles);

#ifdef __cplusplus
}
#endif

#endif // ST7701_DRIVER_H
