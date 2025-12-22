#include "gt911_driver.h"
#include "gt911_touch.h"
#include "esp_log.h"

static const char *TAG = "GT911";

static gt911_touch *touch_instance = nullptr;

i2c_master_bus_handle_t gt911_get_i2c_handle(void)
{
    if (touch_instance == nullptr) {
        return nullptr;
    }
    return touch_instance->get_i2c_handle();
}

esp_err_t gt911_init(esp_lcd_touch_handle_t *tp_handle)
{
    ESP_LOGI(TAG, "Initializing GT911 touch driver");
    
    if (tp_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create GT911 touch instance
    touch_instance = new gt911_touch(TP_I2C_SDA, TP_I2C_SCL, -1, -1);
    if (touch_instance == NULL) {
        ESP_LOGE(TAG, "Failed to create GT911 instance");
        return ESP_ERR_NO_MEM;
    }

    // Initialize touch
    touch_instance->begin();
    
    // Set rotation to match display (portrait mode)
    touch_instance->set_rotation(0);
    
    // Return the actual esp_lcd_touch_handle_t for use with esp_lvgl_port
    *tp_handle = touch_instance->get_handle();
    
    if (*tp_handle == NULL) {
        ESP_LOGE(TAG, "GT911 handle is NULL - initialization failed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GT911 touch driver initialized successfully");
    
    return ESP_OK;
}

esp_err_t gt911_read_touch(esp_lcd_touch_handle_t tp_handle, uint16_t *x, uint16_t *y, bool *pressed)
{
    if (tp_handle == NULL || x == NULL || y == NULL || pressed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (touch_instance == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Read touch coordinates
    *pressed = touch_instance->getTouch(x, y);
    
    return ESP_OK;
}
