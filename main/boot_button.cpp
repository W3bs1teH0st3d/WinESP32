/**
 * Win Recovery - BOOT Button Handler Implementation
 * State machine for detecting single/triple/long press on GPIO0
 */

#include "boot_button.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BOOT_BTN";

// Button state machine
typedef struct {
    uint64_t last_press_time;       // Time of last press (us)
    uint64_t press_start_time;      // Time when current press started (us)
    uint8_t press_count;            // Number of presses in sequence
    bool is_pressed;                // Current button state
    bool was_pressed;               // Previous button state (for edge detection)
    bool long_press_fired;          // Long press event already fired
    boot_button_event_t pending_event;  // Event waiting to be returned
} boot_button_state_t;

static boot_button_state_t btn_state = {0};
static bool initialized = false;

void boot_button_init(void)
{
    if (initialized) return;
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO0: %s", esp_err_to_name(ret));
        return;
    }
    
    boot_button_reset_state();
    initialized = true;
    ESP_LOGI(TAG, "BOOT button initialized on GPIO%d", BOOT_BUTTON_GPIO);
}

bool boot_button_is_pressed(void)
{
    // GPIO0 is active LOW (pressed = 0)
    return gpio_get_level((gpio_num_t)BOOT_BUTTON_GPIO) == 0;
}

boot_button_event_t boot_button_get_event(void)
{
    if (!initialized) {
        boot_button_init();
    }
    
    uint64_t now = esp_timer_get_time();
    bool pressed = boot_button_is_pressed();
    
    // Return pending event if any
    if (btn_state.pending_event != BOOT_BTN_NONE) {
        boot_button_event_t evt = btn_state.pending_event;
        btn_state.pending_event = BOOT_BTN_NONE;
        return evt;
    }
    
    // Detect press edge (was not pressed, now pressed)
    if (pressed && !btn_state.was_pressed) {
        // Debounce check
        if ((now - btn_state.last_press_time) > (BOOT_BTN_DEBOUNCE_MS * 1000)) {
            btn_state.press_start_time = now;
            btn_state.is_pressed = true;
            btn_state.long_press_fired = false;
            
            // Check if this is part of a multi-press sequence
            if ((now - btn_state.last_press_time) < (BOOT_BTN_MULTI_PRESS_MS * 1000)) {
                btn_state.press_count++;
            } else {
                btn_state.press_count = 1;
            }
            
            btn_state.last_press_time = now;
            ESP_LOGD(TAG, "Press detected, count=%d", btn_state.press_count);
        }
    }
    
    // Detect release edge (was pressed, now not pressed)
    if (!pressed && btn_state.was_pressed) {
        btn_state.is_pressed = false;
        
        // If long press was already fired, don't fire another event
        if (btn_state.long_press_fired) {
            btn_state.press_count = 0;
            btn_state.long_press_fired = false;
        }
    }
    
    // Check for long press while button is held
    if (btn_state.is_pressed && !btn_state.long_press_fired) {
        uint64_t hold_time = now - btn_state.press_start_time;
        if (hold_time > (BOOT_BTN_LONG_PRESS_MS * 1000)) {
            btn_state.long_press_fired = true;
            btn_state.press_count = 0;
            btn_state.was_pressed = pressed;
            ESP_LOGI(TAG, "Long press detected");
            return BOOT_BTN_LONG;
        }
    }
    
    // Check for multi-press timeout (button released and timeout expired)
    if (!btn_state.is_pressed && btn_state.press_count > 0) {
        uint64_t since_last = now - btn_state.last_press_time;
        if (since_last > (BOOT_BTN_MULTI_PRESS_MS * 1000)) {
            boot_button_event_t evt = BOOT_BTN_NONE;
            
            switch (btn_state.press_count) {
                case 1:
                    evt = BOOT_BTN_SINGLE;
                    ESP_LOGI(TAG, "Single press detected");
                    break;
                case 2:
                    evt = BOOT_BTN_DOUBLE;
                    ESP_LOGI(TAG, "Double press detected");
                    break;
                case 3:
                default:
                    evt = BOOT_BTN_TRIPLE;
                    ESP_LOGI(TAG, "Triple press detected (count=%d)", btn_state.press_count);
                    break;
            }
            
            btn_state.press_count = 0;
            btn_state.was_pressed = pressed;
            return evt;
        }
    }
    
    btn_state.was_pressed = pressed;
    return BOOT_BTN_NONE;
}

bool boot_button_check_held_at_boot(void)
{
    if (!initialized) {
        boot_button_init();
    }
    
    // Check if button is pressed at boot
    if (!boot_button_is_pressed()) {
        return false;
    }
    
    ESP_LOGI(TAG, "BOOT button held at startup, waiting...");
    
    // Wait and check if held for LONG_PRESS_MS
    uint64_t start = esp_timer_get_time();
    while (boot_button_is_pressed()) {
        uint64_t elapsed = esp_timer_get_time() - start;
        if (elapsed > (BOOT_BTN_LONG_PRESS_MS * 1000)) {
            ESP_LOGW(TAG, "BOOT button held for >1s at boot - recovery trigger!");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "BOOT button released before threshold");
    return false;
}

void boot_button_reset_state(void)
{
    btn_state.last_press_time = 0;
    btn_state.press_start_time = 0;
    btn_state.press_count = 0;
    btn_state.is_pressed = false;
    btn_state.was_pressed = false;
    btn_state.long_press_fired = false;
    btn_state.pending_event = BOOT_BTN_NONE;
    ESP_LOGD(TAG, "Button state reset");
}
