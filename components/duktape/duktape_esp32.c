/**
 * Duktape ESP32 Wrapper Implementation for ESP-IDF
 */

#include "duktape_esp32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DUKTAPE";

// Store console callback globally (accessed from native functions)
static duk_console_callback_t g_console_cb = NULL;

// Native console.log implementation
static duk_ret_t native_console_log(duk_context *ctx) {
    int n = duk_get_top(ctx);
    for (int i = 0; i < n; i++) {
        const char *str = duk_safe_to_string(ctx, i);
        if (g_console_cb) {
            g_console_cb(str);
        } else {
            ESP_LOGI(TAG, "%s", str);
        }
    }
    return 0;
}

// Native print function (alias for console.log)
static duk_ret_t native_print(duk_context *ctx) {
    return native_console_log(ctx);
}

// Native millis() - returns milliseconds since boot
static duk_ret_t native_millis(duk_context *ctx) {
    int64_t ms = esp_timer_get_time() / 1000;
    duk_push_number(ctx, (double)ms);
    return 1;
}

// Native delay(ms) - blocking delay
static duk_ret_t native_delay(duk_context *ctx) {
    int ms = duk_require_int(ctx, 0);
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    return 0;
}

// Setup global objects and functions
static void duk_setup_globals(duk_context *ctx) {
    // Create console object
    duk_push_object(ctx);
    
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "log");
    
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "info");
    
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "warn");
    
    duk_push_c_function(ctx, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "error");
    
    duk_put_global_string(ctx, "console");
    
    // Global functions
    duk_push_c_function(ctx, native_print, DUK_VARARGS);
    duk_put_global_string(ctx, "print");
    
    duk_push_c_function(ctx, native_millis, 0);
    duk_put_global_string(ctx, "millis");
    
    duk_push_c_function(ctx, native_delay, 1);
    duk_put_global_string(ctx, "delay");
}

duk_esp32_t* duk_esp32_init(void) {
    duk_esp32_t *duk = calloc(1, sizeof(duk_esp32_t));
    if (!duk) {
        ESP_LOGE(TAG, "Failed to allocate context wrapper");
        return NULL;
    }
    
    // Create Duktape heap
    duk->ctx = duk_create_heap_default();
    if (!duk->ctx) {
        ESP_LOGE(TAG, "Failed to create Duktape heap");
        free(duk);
        return NULL;
    }
    
    // Setup global objects
    duk_setup_globals(duk->ctx);
    
    ESP_LOGI(TAG, "Duktape initialized");
    return duk;
}

void duk_esp32_cleanup(duk_esp32_t *duk) {
    if (!duk) return;
    
    if (duk->ctx) {
        duk_destroy_heap(duk->ctx);
    }
    free(duk);
    g_console_cb = NULL;
    ESP_LOGI(TAG, "Duktape cleaned up");
}

void duk_esp32_set_console_callback(duk_esp32_t *duk, duk_console_callback_t cb) {
    if (duk) {
        duk->console_cb = cb;
        g_console_cb = cb;
    }
}

char* duk_esp32_eval(duk_esp32_t *duk, const char *code) {
    if (!duk || !duk->ctx || !code) {
        return NULL;
    }
    
    duk->last_error[0] = '\0';
    
    // Evaluate code with protected call
    duk_push_string(duk->ctx, code);
    
    if (duk_peval(duk->ctx) != 0) {
        // Error occurred
        const char *err = duk_safe_to_string(duk->ctx, -1);
        snprintf(duk->last_error, sizeof(duk->last_error), "%s", err);
        duk_pop(duk->ctx);
        return NULL;
    }
    
    // Get result as string
    char *result = NULL;
    if (!duk_is_undefined(duk->ctx, -1)) {
        const char *str = duk_safe_to_string(duk->ctx, -1);
        if (str) {
            result = strdup(str);
        }
    }
    
    duk_pop(duk->ctx);
    return result;
}

const char* duk_esp32_get_error(duk_esp32_t *duk) {
    if (!duk) return "No context";
    return duk->last_error[0] ? duk->last_error : NULL;
}

void duk_esp32_gc(duk_esp32_t *duk) {
    if (duk && duk->ctx) {
        duk_gc(duk->ctx, 0);
    }
}
