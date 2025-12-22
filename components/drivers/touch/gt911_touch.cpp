#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"
#include "gt911_touch.h"

#define CONFIG_LCD_HRES 480
#define CONFIG_LCD_VRES 800

// I2C configuration for GT911
#define I2C_MASTER_SCL_IO       8
#define I2C_MASTER_SDA_IO       7
#define I2C_MASTER_FREQ_HZ      400000

static const char *TAG = "GT911_TOUCH";

static esp_lcd_panel_io_handle_t tp_io_handle = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

// Global getter for I2C bus handle (for sharing with camera)
i2c_master_bus_handle_t gt911_touch::get_i2c_handle()
{
    return i2c_bus_handle;
}

gt911_touch::gt911_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin)
{
    _sda = sda_pin;
    _scl = scl_pin;
    _rst = rst_pin;
    _int = int_pin;
    _tp_handle = NULL;
}

void gt911_touch::begin()
{
    // Create I2C master bus
    ESP_LOGI(TAG, "Initializing I2C master bus");
    
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)_sda,
        .scl_io_num = (gpio_num_t)_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C master bus created successfully");

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    
    ESP_LOGI(TAG, "Initialize touch IO (I2C)");
    ret = esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        return;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = CONFIG_LCD_HRES,
        .y_max = CONFIG_LCD_VRES,
        .rst_gpio_num = (gpio_num_t)_rst,
        .int_gpio_num = (gpio_num_t)_int,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller GT911");
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &_tp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GT911: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "GT911 touch controller initialized successfully");
}

bool gt911_touch::getTouch(uint16_t *x, uint16_t *y)
{
    if (_tp_handle == NULL) {
        return false;
    }
    
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;
    
    esp_lcd_touch_read_data(_tp_handle);
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(_tp_handle, x, y, touch_strength, &touch_cnt, 1);

    return touchpad_pressed;
}

void gt911_touch::set_rotation(uint8_t r){
switch(r){
    case 0:
        esp_lcd_touch_set_swap_xy(_tp_handle, false);   
        esp_lcd_touch_set_mirror_x(_tp_handle, false);
        esp_lcd_touch_set_mirror_y(_tp_handle, false);
        break;
    case 1:
        esp_lcd_touch_set_swap_xy(_tp_handle, false);
        esp_lcd_touch_set_mirror_x(_tp_handle, true);
        esp_lcd_touch_set_mirror_y(_tp_handle, true);
        break;
    case 2:
        esp_lcd_touch_set_swap_xy(_tp_handle, false);   
        esp_lcd_touch_set_mirror_x(_tp_handle, false);
        esp_lcd_touch_set_mirror_y(_tp_handle, false);
        break;
    case 3:
        esp_lcd_touch_set_swap_xy(_tp_handle, false);   
        esp_lcd_touch_set_mirror_x(_tp_handle, true);
        esp_lcd_touch_set_mirror_y(_tp_handle, true);
        break;
    }

}