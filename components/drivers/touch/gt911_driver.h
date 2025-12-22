#ifndef GT911_DRIVER_H
#define GT911_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_touch.h"  // From espressif__esp_lcd_touch managed component
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Touch configuration
#define TP_I2C_SDA 7
#define TP_I2C_SCL 8
#define TP_I2C_NUM I2C_NUM_0

/**
 * @brief Initialize GT911 touch driver
 * 
 * @param tp_handle Pointer to store touch handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gt911_init(esp_lcd_touch_handle_t *tp_handle);

/**
 * @brief Read touch coordinates
 * 
 * @param tp_handle Touch handle
 * @param x Pointer to store X coordinate
 * @param y Pointer to store Y coordinate
 * @param pressed Pointer to store touch state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gt911_read_touch(esp_lcd_touch_handle_t tp_handle, uint16_t *x, uint16_t *y, bool *pressed);

/**
 * @brief Get the I2C bus handle used by the touch driver
 * This can be shared with other I2C devices (like camera)
 * 
 * @return i2c_master_bus_handle_t The I2C bus handle, or NULL if not initialized
 */
i2c_master_bus_handle_t gt911_get_i2c_handle(void);

#ifdef __cplusplus
}
#endif

#endif // GT911_DRIVER_H
