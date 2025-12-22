#ifndef _GT911_TOUCH_H
#define _GT911_TOUCH_H
#include <stdio.h>
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

class gt911_touch
{
public:
    gt911_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin = -1, int8_t int_pin = -1);

    void begin();
    bool getTouch(uint16_t *x, uint16_t *y);
    void set_rotation(uint8_t r);
    
    // Get the actual esp_lcd_touch handle for use with esp_lvgl_port
    esp_lcd_touch_handle_t get_handle() { return _tp_handle; }
    
    // Get the I2C bus handle for sharing with other devices (camera)
    i2c_master_bus_handle_t get_i2c_handle();

private:
    int8_t _sda, _scl, _rst, _int;
    esp_lcd_touch_handle_t _tp_handle = NULL;
};

#endif
