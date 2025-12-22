/**
 * Duktape ESP32 Wrapper for ESP-IDF
 * Simplified interface for JavaScript execution
 */

#ifndef DUKTAPE_ESP32_H
#define DUKTAPE_ESP32_H

#include "duktape.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Console output callback type
typedef void (*duk_console_callback_t)(const char *msg);

// Duktape context wrapper
typedef struct {
    duk_context *ctx;
    duk_console_callback_t console_cb;
    char last_error[512];
} duk_esp32_t;

// Initialize Duktape context
duk_esp32_t* duk_esp32_init(void);

// Cleanup Duktape context
void duk_esp32_cleanup(duk_esp32_t *duk);

// Set console.log callback
void duk_esp32_set_console_callback(duk_esp32_t *duk, duk_console_callback_t cb);

// Execute JavaScript code
// Returns result as string (caller must free) or NULL on error
char* duk_esp32_eval(duk_esp32_t *duk, const char *code);

// Get last error message
const char* duk_esp32_get_error(duk_esp32_t *duk);

// Run garbage collection
void duk_esp32_gc(duk_esp32_t *duk);

#ifdef __cplusplus
}
#endif

#endif // DUKTAPE_ESP32_H
