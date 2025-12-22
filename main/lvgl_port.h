#ifndef MY_LVGL_PORT_H
#define MY_LVGL_PORT_H

#include "esp_err.h"
#include "lvgl.h"
#include "st7701_driver.h"
#include "gt911_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL with display and touch drivers
 * Uses esp_lvgl_port with avoid_tearing for smooth animations
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_lvgl_port_init(void);

/**
 * @brief Lock LVGL mutex (wrapper for lvgl_port_lock)
 * 
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return true if locked successfully
 */
bool my_lvgl_port_lock(uint32_t timeout_ms);

/**
 * @brief Unlock LVGL mutex (wrapper for lvgl_port_unlock)
 */
void my_lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif

#endif // MY_LVGL_PORT_H
