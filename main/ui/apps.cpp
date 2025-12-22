/**
 * Win32 OS - Test Applications
 * Calculator, Clock, Weather, Settings, Notepad, Camera
 */

#include "win32_ui.h"
#include "hardware/hardware.h"
#include "system_settings.h"
#include "bluetooth_transfer.h"
#include "assets.h"
#include "duktape_esp32.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_littlefs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

// Custom font with Cyrillic support
#define UI_FONT &CodeProVariable

static const char *TAG = "APPS";

// External references from win32_ui.cpp
extern lv_obj_t *scr_desktop;

// App window (shared - used by settings_extended.cpp)
lv_obj_t *app_window = NULL;

// Calculator state
static double calc_value = 0;
static double calc_operand = 0;
static char calc_operator = 0;
static bool calc_new_input = true;
static lv_obj_t *calc_display = NULL;
static lv_obj_t *calc_content = NULL;

// Clock state
static lv_obj_t *clock_time_label = NULL;
static lv_obj_t *clock_date_label = NULL;
static lv_timer_t *clock_timer = NULL;
static lv_obj_t *clock_content = NULL;
static lv_obj_t *stopwatch_label = NULL;
static lv_obj_t *timer_label = NULL;

// Notepad state
static lv_obj_t *notepad_textarea = NULL;

// My Computer state
static char mycomp_current_path[128] = "";
static lv_obj_t *mycomp_content = NULL;
static lv_obj_t *mycomp_path_label = NULL;

// Game timer (Flappy Bird) - forward declaration
static lv_timer_t *game_timer = NULL;

// Weather app state (declared early for close_app_window)
static lv_obj_t *weather_content = NULL;
static lv_obj_t *weather_location_label = NULL;
static lv_obj_t *weather_temp_label = NULL;
static lv_obj_t *weather_condition_label = NULL;
static lv_obj_t *weather_feels_label = NULL;
static lv_obj_t *weather_status_label = NULL;
static lv_obj_t *weather_wind_label = NULL;
static lv_obj_t *weather_humidity_label = NULL;
static lv_obj_t *weather_pressure_label = NULL;
static lv_obj_t *weather_forecast_temps_hi[5] = {NULL};
static lv_obj_t *weather_forecast_temps_lo[5] = {NULL};
static lv_obj_t *weather_forecast_days[5] = {NULL};
static bool weather_fetching = false;

// Forward declarations
static void close_app_window(void);
static lv_obj_t* create_app_window(const char* title);
static void recorder_cleanup(void);
static void sysmon_cleanup(void);
static void snake_cleanup(void);
static void js_cleanup(void);
static void tetris_cleanup(void);
static void game2048_cleanup(void);
static void minesweeper_cleanup(void);
static void tictactoe_cleanup(void);
static void memory_cleanup(void);

// Toast notification helper
static void show_notification(const char* text, uint32_t duration_ms)
{
    // Create toast container
    lv_obj_t *toast = lv_obj_create(lv_screen_active());
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x2A4A7A), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0x80C0FF), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_pad_all(toast, 12, 0);
    lv_obj_set_style_shadow_width(toast, 10, 0);
    lv_obj_set_style_shadow_color(toast, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(toast, LV_OPA_50, 0);
    lv_obj_remove_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *label = lv_label_create(toast);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, UI_FONT, 0);
    lv_obj_center(label);
    
    // Auto-delete after duration
    lv_obj_delete_delayed(toast, duration_ms);
}

// ============ COMMON WINDOW CREATION ============

static void close_app_window(void)
{
    if (clock_timer) {
        lv_timer_delete(clock_timer);
        clock_timer = NULL;
    }
    if (game_timer) {
        lv_timer_delete(game_timer);
        game_timer = NULL;
    }
    
    // Cleanup new apps
    recorder_cleanup();
    sysmon_cleanup();
    snake_cleanup();
    js_cleanup();
    tetris_cleanup();
    game2048_cleanup();
    minesweeper_cleanup();
    tictactoe_cleanup();
    memory_cleanup();
    
    if (app_window) {
        lv_obj_delete(app_window);
        app_window = NULL;
    }
    calc_display = NULL;
    calc_content = NULL;
    clock_time_label = NULL;
    clock_date_label = NULL;
    clock_content = NULL;
    stopwatch_label = NULL;
    timer_label = NULL;
    notepad_textarea = NULL;
    mycomp_content = NULL;
    mycomp_path_label = NULL;
    
    // Reset weather pointers to prevent crash on async callback
    weather_content = NULL;
    weather_location_label = NULL;
    weather_temp_label = NULL;
    weather_condition_label = NULL;
    weather_feels_label = NULL;
    weather_wind_label = NULL;
    weather_humidity_label = NULL;
    weather_pressure_label = NULL;
    weather_status_label = NULL;
    for (int i = 0; i < 5; i++) {
        weather_forecast_days[i] = NULL;
        weather_forecast_temps_hi[i] = NULL;
        weather_forecast_temps_lo[i] = NULL;
    }
    
    // Reset settings page pointers since they were children of app_window
    settings_reset_pages();
}

static lv_obj_t* create_app_window(const char* title)
{
    close_app_window();
    
    app_window = lv_obj_create(scr_desktop);
    lv_obj_set_size(app_window, SCREEN_WIDTH - 10, SCREEN_HEIGHT - TASKBAR_HEIGHT - 10);
    lv_obj_align(app_window, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(app_window, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(app_window, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(app_window, 2, 0);
    lv_obj_set_style_radius(app_window, 8, 0);
    lv_obj_set_style_pad_all(app_window, 0, 0);
    lv_obj_remove_flag(app_window, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar - fixed height
    lv_obj_t *title_bar = lv_obj_create(app_window);
    lv_obj_set_size(title_bar, lv_pct(100), 32);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 6, 0);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(title_bar, 10, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, UI_FONT, 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 32, 26);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(close_btn, 3, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        close_app_window();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_center(close_label);
    
    return app_window;
}


// ============ CALCULATOR APP (XP Luna Style + Scientific Mode + Unit Converter) ============

static int calc_mode = 0;  // 0=standard, 1=scientific, 2=converter
static lv_obj_t *calc_mode_btn = NULL;
static bool calc_has_decimal = false;
static double calc_decimal_place = 0.1;
static lv_obj_t *calc_expression_label = NULL;  // Shows expression like "123 + 456 ="
static char calc_expression[64] = "";  // Expression string

// Unit converter state
static int conv_category = 0;  // 0=length, 1=weight, 2=temp, 3=data
static int conv_from_unit = 0;
static int conv_to_unit = 1;
static lv_obj_t *conv_from_dropdown = NULL;
static lv_obj_t *conv_to_dropdown = NULL;
static lv_obj_t *conv_result_label = NULL;
static lv_obj_t *conv_input_ta = NULL;

static void calc_update_display(void)
{
    if (calc_display) {
        char buf[32];
        if (calc_value == (int64_t)calc_value && calc_value < 1e12 && calc_value > -1e12) {
            snprintf(buf, sizeof(buf), "%lld", (int64_t)calc_value);
        } else {
            snprintf(buf, sizeof(buf), "%.8g", calc_value);
        }
        lv_label_set_text(calc_display, buf);
    }
    
    // Update expression label
    if (calc_expression_label) {
        lv_label_set_text(calc_expression_label, calc_expression);
    }
}

static void calc_btn_event_cb(lv_event_t *e)
{
    const char *txt = (const char *)lv_event_get_user_data(e);
    
    if (txt[0] >= '0' && txt[0] <= '9') {
        int digit = txt[0] - '0';
        if (calc_new_input) {
            calc_value = digit;
            calc_new_input = false;
            calc_has_decimal = false;
            calc_decimal_place = 0.1;
        } else if (calc_has_decimal) {
            calc_value = calc_value + digit * calc_decimal_place;
            calc_decimal_place *= 0.1;
        } else {
            calc_value = calc_value * 10 + digit;
        }
    } else if (txt[0] == '.') {
        if (!calc_has_decimal) {
            calc_has_decimal = true;
            calc_new_input = false;
        }
    } else if (txt[0] == 'C') {
        calc_value = 0;
        calc_operand = 0;
        calc_operator = 0;
        calc_new_input = true;
        calc_has_decimal = false;
        calc_expression[0] = '\0';  // Clear expression
    } else if (strcmp(txt, "CE") == 0) {
        calc_value = 0;
        calc_new_input = true;
        calc_has_decimal = false;
    } else if (strcmp(txt, "+-") == 0) {
        calc_value = -calc_value;
    } else if (strcmp(txt, "sqrt") == 0) {
        if (calc_value >= 0) calc_value = sqrt(calc_value);
        calc_new_input = true;
    } else if (strcmp(txt, "sin") == 0) {
        calc_value = sin(calc_value * M_PI / 180.0);
        calc_new_input = true;
    } else if (strcmp(txt, "cos") == 0) {
        calc_value = cos(calc_value * M_PI / 180.0);
        calc_new_input = true;
    } else if (strcmp(txt, "tan") == 0) {
        calc_value = tan(calc_value * M_PI / 180.0);
        calc_new_input = true;
    } else if (strcmp(txt, "log") == 0) {
        if (calc_value > 0) calc_value = log10(calc_value);
        calc_new_input = true;
    } else if (strcmp(txt, "ln") == 0) {
        if (calc_value > 0) calc_value = log(calc_value);
        calc_new_input = true;
    } else if (strcmp(txt, "x^2") == 0) {
        calc_value = calc_value * calc_value;
        calc_new_input = true;
    } else if (strcmp(txt, "1/x") == 0) {
        if (calc_value != 0) calc_value = 1.0 / calc_value;
        calc_new_input = true;
    } else if (strcmp(txt, "pi") == 0) {
        calc_value = M_PI;
        calc_new_input = true;
    } else if (txt[0] == '%') {
        calc_value = calc_value / 100.0;
        calc_new_input = true;
    } else if (txt[0] == '=') {
        if (calc_operator) {
            // Build expression string
            char val_str[16];
            if (calc_value == (int64_t)calc_value) {
                snprintf(val_str, sizeof(val_str), "%lld", (int64_t)calc_value);
            } else {
                snprintf(val_str, sizeof(val_str), "%.4g", calc_value);
            }
            size_t len = strlen(calc_expression);
            snprintf(calc_expression + len, sizeof(calc_expression) - len, " %s =", val_str);
            
            switch (calc_operator) {
                case '+': calc_value = calc_operand + calc_value; break;
                case '-': calc_value = calc_operand - calc_value; break;
                case '*': calc_value = calc_operand * calc_value; break;
                case '/': 
                    if (calc_value != 0) calc_value = calc_operand / calc_value;
                    else { lv_label_set_text(calc_display, "Error"); return; }
                    break;
                case '^': calc_value = pow(calc_operand, calc_value); break;
            }
            calc_operator = 0;
            calc_new_input = true;
        }
    } else if (txt[0] == '+' || txt[0] == '-' || txt[0] == '*' || txt[0] == '/' || txt[0] == '^') {
        if (calc_operator && !calc_new_input) {
            switch (calc_operator) {
                case '+': calc_operand = calc_operand + calc_value; break;
                case '-': calc_operand = calc_operand - calc_value; break;
                case '*': calc_operand = calc_operand * calc_value; break;
                case '/': 
                    if (calc_value != 0) calc_operand = calc_operand / calc_value;
                    break;
                case '^': calc_operand = pow(calc_operand, calc_value); break;
            }
            calc_value = calc_operand;
        } else {
            calc_operand = calc_value;
        }
        
        // Build expression string
        char val_str[16];
        if (calc_operand == (int64_t)calc_operand) {
            snprintf(val_str, sizeof(val_str), "%lld", (int64_t)calc_operand);
        } else {
            snprintf(val_str, sizeof(val_str), "%.4g", calc_operand);
        }
        snprintf(calc_expression, sizeof(calc_expression), "%s %c", val_str, txt[0]);
        
        calc_operator = txt[0];
        calc_new_input = true;
    }
    
    calc_update_display();
}

// Create XP Luna gradient button style
static lv_obj_t* create_calc_btn(lv_obj_t *parent, const char *label_txt, int x, int y, int w, int h, uint32_t color_top, uint32_t color_bot)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    
    // XP Luna gradient effect (3D raised button)
    lv_obj_set_style_bg_color(btn, lv_color_hex(color_top), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(color_bot), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x003C9D), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 2, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 1, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    
    // Pressed state - darker
    lv_obj_set_style_bg_color(btn, lv_color_hex(color_bot), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(color_top), LV_STATE_PRESSED);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, label_txt);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, UI_FONT, 0);
    lv_obj_center(label);
    
    lv_obj_add_event_cb(btn, calc_btn_event_cb, LV_EVENT_CLICKED, (void*)label_txt);
    
    return btn;
}

static void calc_rebuild_ui(void);

static void calc_mode_toggle_cb(lv_event_t *e)
{
    calc_mode = (calc_mode + 1) % 3;  // Cycle: Standard -> Scientific -> Converter
    calc_rebuild_ui();
}

// Unit conversion data
static const char *conv_categories[] = {"Length", "Weight", "Temp", "Data"};
static const char *conv_length_units[] = {"mm", "cm", "m", "km", "in", "ft", "yd", "mi"};
static const double conv_length_to_m[] = {0.001, 0.01, 1.0, 1000.0, 0.0254, 0.3048, 0.9144, 1609.34};
static const char *conv_weight_units[] = {"mg", "g", "kg", "oz", "lb"};
static const double conv_weight_to_g[] = {0.001, 1.0, 1000.0, 28.3495, 453.592};
static const char *conv_data_units[] = {"B", "KB", "MB", "GB", "TB"};
static const double conv_data_to_b[] = {1.0, 1024.0, 1048576.0, 1073741824.0, 1099511627776.0};

static void conv_do_conversion(void) {
    if (!conv_input_ta || !conv_result_label) return;
    
    const char *input_str = lv_textarea_get_text(conv_input_ta);
    double input_val = atof(input_str);
    double result = 0;
    
    if (conv_category == 0) {  // Length
        double in_meters = input_val * conv_length_to_m[conv_from_unit];
        result = in_meters / conv_length_to_m[conv_to_unit];
    } else if (conv_category == 1) {  // Weight
        double in_grams = input_val * conv_weight_to_g[conv_from_unit];
        result = in_grams / conv_weight_to_g[conv_to_unit];
    } else if (conv_category == 2) {  // Temperature
        // Convert to Celsius first, then to target
        double celsius;
        if (conv_from_unit == 0) celsius = input_val;  // C
        else if (conv_from_unit == 1) celsius = (input_val - 32) * 5.0 / 9.0;  // F
        else celsius = input_val - 273.15;  // K
        
        if (conv_to_unit == 0) result = celsius;  // C
        else if (conv_to_unit == 1) result = celsius * 9.0 / 5.0 + 32;  // F
        else result = celsius + 273.15;  // K
    } else if (conv_category == 3) {  // Data
        double in_bytes = input_val * conv_data_to_b[conv_from_unit];
        result = in_bytes / conv_data_to_b[conv_to_unit];
    }
    
    char buf[64];
    if (result == (int64_t)result && result < 1e12 && result > -1e12) {
        snprintf(buf, sizeof(buf), "= %lld", (int64_t)result);
    } else {
        snprintf(buf, sizeof(buf), "= %.6g", result);
    }
    lv_label_set_text(conv_result_label, buf);
}

static void calc_rebuild_ui(void)
{
    if (!calc_content) return;
    
    // Clear content except display (first child)
    uint32_t child_cnt = lv_obj_get_child_count(calc_content);
    for (int i = child_cnt - 1; i >= 1; i--) {  // Keep first child (display)
        lv_obj_t *child = lv_obj_get_child(calc_content, i);
        lv_obj_delete(child);
    }
    
    // Reset converter pointers
    conv_from_dropdown = NULL;
    conv_to_dropdown = NULL;
    conv_result_label = NULL;
    conv_input_ta = NULL;
    
    int content_w = SCREEN_WIDTH - 10 - 16;  // Window width minus padding
    int start_y = 105;  // Below display (90px) + gap
    
    // Mode names for button
    static const char *mode_names[] = {"Scientific", "Converter", "Standard"};
    
    // Mode toggle button (below display, right side)
    calc_mode_btn = lv_btn_create(calc_content);
    lv_obj_set_size(calc_mode_btn, 90, 28);
    lv_obj_align(calc_mode_btn, LV_ALIGN_TOP_RIGHT, 0, 95);
    lv_obj_set_style_bg_color(calc_mode_btn, lv_color_hex(0x4A7DC4), 0);
    lv_obj_set_style_radius(calc_mode_btn, 4, 0);
    lv_obj_add_event_cb(calc_mode_btn, calc_mode_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mode_lbl = lv_label_create(calc_mode_btn);
    lv_label_set_text(mode_lbl, mode_names[calc_mode]);
    lv_obj_set_style_text_color(mode_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(mode_lbl, UI_FONT, 0);
    lv_obj_center(mode_lbl);
    
    start_y = 130;
    
    if (calc_mode == 1) {
        // Scientific mode
        int btn_w = (content_w - 5 * 4) / 6;
        int btn_h = 48;
        int gap = 4;
        int cols = 6;
        
        static const char* sci_btns[] = {
            "sin", "cos", "tan", "C", "CE", "/",
            "log", "ln", "x^2", "7", "8", "9",
            "sqrt", "1/x", "^", "4", "5", "6",
            "pi", "+-", "%", "1", "2", "3",
            "(", ")", ".", "0", "=", "*",
            NULL, NULL, NULL, NULL, "+", "-"
        };
        
        for (int i = 0; i < 36; i++) {
            if (sci_btns[i] == NULL) continue;
            int row = i / cols;
            int col = i % cols;
            int x = col * (btn_w + gap);
            int y = start_y + row * (btn_h + gap);
            
            uint32_t c1, c2;
            const char *txt = sci_btns[i];
            
            if (strcmp(txt, "C") == 0 || strcmp(txt, "CE") == 0) {
                c1 = 0xCC4444; c2 = 0x992222;
            } else if (strcmp(txt, "=") == 0) {
                c1 = 0x00AA00; c2 = 0x007700;
            } else if (txt[0] >= '0' && txt[0] <= '9') {
                c1 = 0x5588CC; c2 = 0x3366AA;
            } else if (strcmp(txt, "+") == 0 || strcmp(txt, "-") == 0 || 
                       strcmp(txt, "*") == 0 || strcmp(txt, "/") == 0 || strcmp(txt, "^") == 0) {
                c1 = 0xFF8800; c2 = 0xCC6600;
            } else {
                c1 = 0x6699CC; c2 = 0x4477AA;
            }
            create_calc_btn(calc_content, txt, x, y, btn_w, btn_h, c1, c2);
        }
    } else if (calc_mode == 2) {
        // Unit Converter mode
        // Category selector
        lv_obj_t *cat_label = lv_label_create(calc_content);
        lv_label_set_text(cat_label, "Category:");
        lv_obj_set_style_text_color(cat_label, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(cat_label, UI_FONT, 0);
        lv_obj_align(cat_label, LV_ALIGN_TOP_LEFT, 0, start_y);
        
        // Category buttons
        for (int i = 0; i < 4; i++) {
            lv_obj_t *cat_btn = lv_btn_create(calc_content);
            lv_obj_set_size(cat_btn, 100, 35);
            lv_obj_set_pos(cat_btn, i * 105, start_y + 25);
            lv_obj_set_style_bg_color(cat_btn, lv_color_hex(conv_category == i ? 0x4A7DC4 : 0x888888), 0);
            lv_obj_set_style_radius(cat_btn, 4, 0);
            
            lv_obj_t *cat_lbl = lv_label_create(cat_btn);
            lv_label_set_text(cat_lbl, conv_categories[i]);
            lv_obj_set_style_text_color(cat_lbl, lv_color_white(), 0);
            lv_obj_set_style_text_font(cat_lbl, UI_FONT, 0);
            lv_obj_center(cat_lbl);
            
            int cat_idx = i;
            lv_obj_add_event_cb(cat_btn, [](lv_event_t *e) {
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                conv_category = idx;
                conv_from_unit = 0;
                conv_to_unit = 1;
                calc_rebuild_ui();
            }, LV_EVENT_CLICKED, (void*)(intptr_t)cat_idx);
        }
        
        start_y += 80;
        
        // Input field
        lv_obj_t *from_label = lv_label_create(calc_content);
        lv_label_set_text(from_label, "From:");
        lv_obj_set_style_text_color(from_label, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(from_label, UI_FONT, 0);
        lv_obj_align(from_label, LV_ALIGN_TOP_LEFT, 0, start_y);
        
        conv_input_ta = lv_textarea_create(calc_content);
        lv_obj_set_size(conv_input_ta, 200, 40);
        lv_obj_align(conv_input_ta, LV_ALIGN_TOP_LEFT, 50, start_y - 5);
        lv_textarea_set_one_line(conv_input_ta, true);
        lv_textarea_set_text(conv_input_ta, "1");
        lv_obj_set_style_text_font(conv_input_ta, UI_FONT, 0);
        lv_obj_add_event_cb(conv_input_ta, [](lv_event_t *e) {
            conv_do_conversion();
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        // From unit dropdown
        conv_from_dropdown = lv_dropdown_create(calc_content);
        lv_obj_set_size(conv_from_dropdown, 150, 40);
        lv_obj_align(conv_from_dropdown, LV_ALIGN_TOP_LEFT, 260, start_y - 5);
        lv_obj_set_style_text_font(conv_from_dropdown, UI_FONT, 0);
        
        // Set dropdown options based on category
        if (conv_category == 0) {
            lv_dropdown_set_options(conv_from_dropdown, "mm\ncm\nm\nkm\nin\nft\nyd\nmi");
        } else if (conv_category == 1) {
            lv_dropdown_set_options(conv_from_dropdown, "mg\ng\nkg\noz\nlb");
        } else if (conv_category == 2) {
            lv_dropdown_set_options(conv_from_dropdown, "C\nF\nK");
        } else {
            lv_dropdown_set_options(conv_from_dropdown, "B\nKB\nMB\nGB\nTB");
        }
        lv_dropdown_set_selected(conv_from_dropdown, conv_from_unit);
        lv_obj_add_event_cb(conv_from_dropdown, [](lv_event_t *e) {
            conv_from_unit = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
            conv_do_conversion();
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        start_y += 60;
        
        // To unit
        lv_obj_t *to_label = lv_label_create(calc_content);
        lv_label_set_text(to_label, "To:");
        lv_obj_set_style_text_color(to_label, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(to_label, UI_FONT, 0);
        lv_obj_align(to_label, LV_ALIGN_TOP_LEFT, 0, start_y);
        
        conv_to_dropdown = lv_dropdown_create(calc_content);
        lv_obj_set_size(conv_to_dropdown, 150, 40);
        lv_obj_align(conv_to_dropdown, LV_ALIGN_TOP_LEFT, 260, start_y - 5);
        lv_obj_set_style_text_font(conv_to_dropdown, UI_FONT, 0);
        
        if (conv_category == 0) {
            lv_dropdown_set_options(conv_to_dropdown, "mm\ncm\nm\nkm\nin\nft\nyd\nmi");
        } else if (conv_category == 1) {
            lv_dropdown_set_options(conv_to_dropdown, "mg\ng\nkg\noz\nlb");
        } else if (conv_category == 2) {
            lv_dropdown_set_options(conv_to_dropdown, "C\nF\nK");
        } else {
            lv_dropdown_set_options(conv_to_dropdown, "B\nKB\nMB\nGB\nTB");
        }
        lv_dropdown_set_selected(conv_to_dropdown, conv_to_unit);
        lv_obj_add_event_cb(conv_to_dropdown, [](lv_event_t *e) {
            conv_to_unit = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
            conv_do_conversion();
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        start_y += 60;
        
        // Result
        conv_result_label = lv_label_create(calc_content);
        lv_label_set_text(conv_result_label, "= 0");
        lv_obj_set_style_text_color(conv_result_label, lv_color_hex(0x00AA00), 0);
        lv_obj_set_style_text_font(conv_result_label, UI_FONT, 0);
        lv_obj_align(conv_result_label, LV_ALIGN_TOP_LEFT, 50, start_y);
        
        // Swap button
        lv_obj_t *swap_btn = lv_btn_create(calc_content);
        lv_obj_set_size(swap_btn, 100, 40);
        lv_obj_align(swap_btn, LV_ALIGN_TOP_LEFT, 260, start_y - 5);
        lv_obj_set_style_bg_color(swap_btn, lv_color_hex(0xFF8800), 0);
        lv_obj_set_style_radius(swap_btn, 4, 0);
        
        lv_obj_t *swap_lbl = lv_label_create(swap_btn);
        lv_label_set_text(swap_lbl, "Swap");
        lv_obj_set_style_text_color(swap_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(swap_lbl, UI_FONT, 0);
        lv_obj_center(swap_lbl);
        
        lv_obj_add_event_cb(swap_btn, [](lv_event_t *e) {
            int tmp = conv_from_unit;
            conv_from_unit = conv_to_unit;
            conv_to_unit = tmp;
            calc_rebuild_ui();
            conv_do_conversion();
        }, LV_EVENT_CLICKED, NULL);
        
        // Do initial conversion
        conv_do_conversion();
        
    } else {
        // Standard mode
        int btn_w = (content_w - 3 * 6) / 4;
        int btn_h = 70;
        int gap = 6;
        
        static const char* std_btns[] = {
            "C", "CE", "%", "/",
            "7", "8", "9", "*",
            "4", "5", "6", "-",
            "1", "2", "3", "+",
            "+-", "0", ".", "="
        };
        
        for (int i = 0; i < 20; i++) {
            int row = i / 4;
            int col = i % 4;
            int x = col * (btn_w + gap);
            int y = start_y + row * (btn_h + gap);
            
            uint32_t c1, c2;
            const char *txt = std_btns[i];
            
            if (strcmp(txt, "C") == 0 || strcmp(txt, "CE") == 0) {
                c1 = 0xCC4444; c2 = 0x992222;
            } else if (strcmp(txt, "=") == 0) {
                c1 = 0x00AA00; c2 = 0x007700;
            } else if (txt[0] >= '0' && txt[0] <= '9') {
                c1 = 0x5588CC; c2 = 0x3366AA;
            } else {
                c1 = 0xFF8800; c2 = 0xCC6600;
            }
            create_calc_btn(calc_content, txt, x, y, btn_w, btn_h, c1, c2);
        }
    }
}

void app_calculator_create(void)
{
    ESP_LOGI(TAG, "Opening Calculator");
    create_app_window("Calculator");
    
    // Reset calculator state
    calc_value = 0;
    calc_operand = 0;
    calc_operator = 0;
    calc_new_input = true;
    calc_has_decimal = false;
    calc_mode = 0;  // Start in standard mode
    calc_expression[0] = '\0';
    calc_expression_label = NULL;
    
    // Content area - fullscreen style
    calc_content = lv_obj_create(app_window);
    lv_obj_set_size(calc_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(calc_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(calc_content, lv_color_hex(0xD4D0C8), 0);  // XP window bg
    lv_obj_set_style_border_width(calc_content, 0, 0);
    lv_obj_set_style_pad_all(calc_content, 8, 0);
    lv_obj_set_style_radius(calc_content, 0, 0);
    lv_obj_remove_flag(calc_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Phone-style display with expression + result (taller display)
    lv_obj_t *display_bg = lv_obj_create(calc_content);
    lv_obj_set_size(display_bg, lv_pct(100), 90);  // Taller for expression + result
    lv_obj_align(display_bg, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(display_bg, lv_color_hex(0x1A2A1A), 0);
    lv_obj_set_style_border_color(display_bg, lv_color_hex(0x003300), 0);
    lv_obj_set_style_border_width(display_bg, 2, 0);
    lv_obj_set_style_radius(display_bg, 4, 0);
    lv_obj_set_style_shadow_width(display_bg, 4, 0);
    lv_obj_set_style_shadow_ofs_x(display_bg, 2, 0);
    lv_obj_set_style_shadow_ofs_y(display_bg, 2, 0);
    lv_obj_set_style_shadow_color(display_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(display_bg, LV_OPA_50, 0);
    lv_obj_remove_flag(display_bg, LV_OBJ_FLAG_SCROLLABLE);
    
    // Expression label (smaller, gray text at top)
    calc_expression_label = lv_label_create(display_bg);
    lv_label_set_text(calc_expression_label, "");
    lv_obj_set_style_text_color(calc_expression_label, lv_color_hex(0x88AA88), 0);
    lv_obj_set_style_text_font(calc_expression_label, UI_FONT, 0);
    lv_obj_align(calc_expression_label, LV_ALIGN_TOP_RIGHT, -12, 8);
    
    // Result label (larger, bright green)
    calc_display = lv_label_create(display_bg);
    lv_label_set_text(calc_display, "0");
    lv_obj_set_style_text_color(calc_display, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(calc_display, UI_FONT, 0);
    lv_obj_align(calc_display, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    
    // Build buttons
    calc_rebuild_ui();
}


// ============ CLOCK APP (Swipe between modes) ============

// Clock modes: 0=Clock, 1=Alarm, 2=Timer, 3=Stopwatch
static int clock_mode = 0;
static lv_obj_t *clock_mode_dots[4] = {NULL};

// Stopwatch state
static bool stopwatch_running = false;
static int64_t stopwatch_start_time = 0;
static int64_t stopwatch_elapsed = 0;
static int64_t lap_times[10] = {0};  // Store up to 10 laps
static int lap_count = 0;

// Clock hands (for analog clock)
static lv_obj_t *clock_hour_hand = NULL;
static lv_obj_t *clock_minute_hand = NULL;
static lv_obj_t *clock_second_hand = NULL;
static lv_obj_t *clock_face_obj = NULL;

// Timer state
static int timer_seconds = 300;  // 5 minutes default
static bool timer_running = false;
static int64_t timer_start_time = 0;
static int timer_remaining = 0;

// Alarm state
struct AlarmData {
    int hour;
    int minute;
    bool enabled;
    char name[32];
};
static AlarmData alarms[5] = {
    {7, 0, true, "Wake up"},
    {8, 30, false, "Meeting"},
    {12, 0, false, "Lunch"},
    {0, 0, false, ""},
    {0, 0, false, ""}
};
static int alarm_count = 3;
static bool alarm_edit_mode = false;
static int alarm_edit_hour = 7;
static int alarm_edit_minute = 0;

static void clock_rebuild_content(void);

static void clock_timer_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (clock_mode == 0 && clock_time_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", 
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        lv_label_set_text(clock_time_label, buf);
    }
    
    if (clock_mode == 0 && clock_date_label) {
        char buf[32];
        static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        snprintf(buf, sizeof(buf), "%s, %s %d, %d",
                 days[timeinfo.tm_wday], months[timeinfo.tm_mon],
                 timeinfo.tm_mday, timeinfo.tm_year + 1900);
        lv_label_set_text(clock_date_label, buf);
    }
    
    // Update clock hands
    if (clock_mode == 0 && clock_face_obj && lv_obj_is_valid(clock_face_obj)) {
        int hour = timeinfo.tm_hour % 12;
        int min = timeinfo.tm_min;
        int sec = timeinfo.tm_sec;
        
        // Calculate angles (0 degrees = 12 o'clock, clockwise)
        int hour_angle = (hour * 30 + min / 2);  // 30 deg per hour + minute offset
        int min_angle = min * 6;  // 6 deg per minute
        int sec_angle = sec * 6;  // 6 deg per second
        
        if (clock_hour_hand && lv_obj_is_valid(clock_hour_hand)) {
            lv_obj_set_style_transform_rotation(clock_hour_hand, hour_angle * 10, 0);
        }
        if (clock_minute_hand && lv_obj_is_valid(clock_minute_hand)) {
            lv_obj_set_style_transform_rotation(clock_minute_hand, min_angle * 10, 0);
        }
        if (clock_second_hand && lv_obj_is_valid(clock_second_hand)) {
            lv_obj_set_style_transform_rotation(clock_second_hand, sec_angle * 10, 0);
        }
    }
    
    // Update stopwatch
    if (clock_mode == 3 && stopwatch_running && stopwatch_label) {
        int64_t current = esp_timer_get_time() / 1000;
        int64_t total_ms = stopwatch_elapsed + (current - stopwatch_start_time);
        int mins = (total_ms / 60000) % 60;
        int secs = (total_ms / 1000) % 60;
        int ms = (total_ms / 10) % 100;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d.%02d", mins, secs, ms);
        lv_label_set_text(stopwatch_label, buf);
    }
    
    // Update timer
    if (clock_mode == 2 && timer_running && timer_label) {
        int64_t current = esp_timer_get_time() / 1000;
        int elapsed_secs = (current - timer_start_time) / 1000;
        timer_remaining = timer_seconds - elapsed_secs;
        if (timer_remaining <= 0) {
            timer_remaining = 0;
            timer_running = false;
        }
        int mins = timer_remaining / 60;
        int secs = timer_remaining % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
        lv_label_set_text(timer_label, buf);
    }
}

static void clock_swipe_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    
    if (dir == LV_DIR_LEFT) {
        clock_mode = (clock_mode + 1) % 4;
        clock_rebuild_content();
    } else if (dir == LV_DIR_RIGHT) {
        clock_mode = (clock_mode + 3) % 4;  // -1 mod 4
        clock_rebuild_content();
    }
}

// Helper to create Win7 style button for Clock app
static lv_obj_t* create_clock_win7_btn(lv_obj_t *parent, const char *text, int w, int h, uint32_t color)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(color - 0x101010), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color - 0x202020), LV_STATE_PRESSED);
    
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, UI_FONT, 0);
    lv_obj_center(lbl);
    return btn;
}

static void clock_rebuild_content(void)
{
    if (!clock_content) return;
    
    // Clear content
    lv_obj_clean(clock_content);
    clock_time_label = NULL;
    clock_date_label = NULL;
    stopwatch_label = NULL;
    timer_label = NULL;
    clock_hour_hand = NULL;
    clock_minute_hand = NULL;
    clock_second_hand = NULL;
    clock_face_obj = NULL;
    
    // ===== Mode tabs at top (Win7 style) =====
    static const char* mode_titles[] = {"Clock", "Alarm", "Timer", "Stopwatch"};
    
    lv_obj_t *tabs_bar = lv_obj_create(clock_content);
    lv_obj_set_size(tabs_bar, lv_pct(100), 32);
    lv_obj_align(tabs_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(tabs_bar, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(tabs_bar, 0, 0);
    lv_obj_set_style_border_side(tabs_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(tabs_bar, lv_color_hex(0xAAAAAA), 0);
    lv_obj_remove_flag(tabs_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(tabs_bar, 5, 0);
    
    int tab_x = 5;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *tab = lv_btn_create(tabs_bar);
        lv_obj_set_size(tab, 100, 28);
        lv_obj_set_pos(tab, tab_x, 2);
        lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        
        if (i == clock_mode) {
            lv_obj_set_style_bg_color(tab, lv_color_white(), 0);
            lv_obj_set_style_border_color(tab, lv_color_hex(0xAAAAAA), 0);
            lv_obj_set_style_border_width(tab, 1, 0);
            lv_obj_set_style_border_side(tab, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
        } else {
            lv_obj_set_style_bg_color(tab, lv_color_hex(0xE0E0E0), 0);
            lv_obj_set_style_border_width(tab, 0, 0);
        }
        lv_obj_set_style_radius(tab, 0, 0);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        
        lv_obj_t *tab_lbl = lv_label_create(tab);
        lv_label_set_text(tab_lbl, mode_titles[i]);
        lv_obj_set_style_text_color(tab_lbl, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(tab_lbl, UI_FONT, 0);
        lv_obj_center(tab_lbl);
        lv_obj_remove_flag(tab_lbl, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_set_user_data(tab, (void*)(intptr_t)i);
        lv_obj_add_event_cb(tab, [](lv_event_t *e) {
            lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            clock_mode = idx;
            clock_rebuild_content();
        }, LV_EVENT_CLICKED, NULL);
        
        tab_x += 102;
    }
    
    if (clock_mode == 0) {
        // ===== CLOCK MODE - Win7 Style =====
        lv_obj_t *main_panel = lv_obj_create(clock_content);
        lv_obj_set_size(main_panel, lv_pct(100) - 20, 520);
        lv_obj_align(main_panel, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_bg_color(main_panel, lv_color_white(), 0);
        lv_obj_set_style_border_color(main_panel, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(main_panel, 1, 0);
        lv_obj_set_style_radius(main_panel, 4, 0);
        lv_obj_set_style_pad_all(main_panel, 15, 0);
        lv_obj_remove_flag(main_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        // Digital time - large
        clock_time_label = lv_label_create(main_panel);
        lv_label_set_text(clock_time_label, "00:00:00");
        lv_obj_set_style_text_color(clock_time_label, lv_color_hex(0x1A5090), 0);
        lv_obj_set_style_text_font(clock_time_label, UI_FONT, 0);
        lv_obj_align(clock_time_label, LV_ALIGN_TOP_MID, 0, 10);
        
        // Date
        clock_date_label = lv_label_create(main_panel);
        lv_label_set_text(clock_date_label, "Loading...");
        lv_obj_set_style_text_color(clock_date_label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(clock_date_label, UI_FONT, 0);
        lv_obj_align(clock_date_label, LV_ALIGN_TOP_MID, 0, 45);
        
        // Analog clock face - Win7 style (light with blue accents)
        lv_obj_t *clock_face = lv_obj_create(main_panel);
        clock_face_obj = clock_face;
        lv_obj_set_size(clock_face, 280, 280);
        lv_obj_align(clock_face, LV_ALIGN_CENTER, 0, 40);
        lv_obj_set_style_bg_color(clock_face, lv_color_hex(0xFAFAFA), 0);
        lv_obj_set_style_border_color(clock_face, lv_color_hex(0x4A90D9), 0);
        lv_obj_set_style_border_width(clock_face, 3, 0);
        lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(clock_face, 10, 0);
        lv_obj_set_style_shadow_color(clock_face, lv_color_hex(0x888888), 0);
        lv_obj_set_style_shadow_opa(clock_face, LV_OPA_30, 0);
        lv_obj_remove_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);
        
        // Hour markers
        for (int i = 0; i < 12; i++) {
            lv_obj_t *marker = lv_label_create(clock_face);
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", i == 0 ? 12 : i);
            lv_label_set_text(marker, buf);
            lv_obj_set_style_text_color(marker, lv_color_hex(0x333333), 0);
            lv_obj_set_style_text_font(marker, UI_FONT, 0);
            int angle = i * 30 - 90;
            int x = 105 * cos(angle * 3.14159 / 180);
            int y = 105 * sin(angle * 3.14159 / 180);
            lv_obj_align(marker, LV_ALIGN_CENTER, x, y);
        }
        
        // Hour hand (short, thick, dark blue)
        clock_hour_hand = lv_obj_create(clock_face);
        lv_obj_set_size(clock_hour_hand, 6, 60);
        lv_obj_set_style_bg_color(clock_hour_hand, lv_color_hex(0x1A3A5C), 0);
        lv_obj_set_style_border_width(clock_hour_hand, 0, 0);
        lv_obj_set_style_radius(clock_hour_hand, 3, 0);
        lv_obj_align(clock_hour_hand, LV_ALIGN_CENTER, 0, -30);
        lv_obj_set_style_transform_pivot_x(clock_hour_hand, 3, 0);
        lv_obj_set_style_transform_pivot_y(clock_hour_hand, 57, 0);
        lv_obj_remove_flag(clock_hour_hand, LV_OBJ_FLAG_SCROLLABLE);
        
        // Minute hand (longer, medium, blue)
        clock_minute_hand = lv_obj_create(clock_face);
        lv_obj_set_size(clock_minute_hand, 4, 85);
        lv_obj_set_style_bg_color(clock_minute_hand, lv_color_hex(0x4A90D9), 0);
        lv_obj_set_style_border_width(clock_minute_hand, 0, 0);
        lv_obj_set_style_radius(clock_minute_hand, 2, 0);
        lv_obj_align(clock_minute_hand, LV_ALIGN_CENTER, 0, -42);
        lv_obj_set_style_transform_pivot_x(clock_minute_hand, 2, 0);
        lv_obj_set_style_transform_pivot_y(clock_minute_hand, 82, 0);
        lv_obj_remove_flag(clock_minute_hand, LV_OBJ_FLAG_SCROLLABLE);
        
        // Second hand (longest, thin, red)
        clock_second_hand = lv_obj_create(clock_face);
        lv_obj_set_size(clock_second_hand, 2, 95);
        lv_obj_set_style_bg_color(clock_second_hand, lv_color_hex(0xCC3333), 0);
        lv_obj_set_style_border_width(clock_second_hand, 0, 0);
        lv_obj_set_style_radius(clock_second_hand, 1, 0);
        lv_obj_align(clock_second_hand, LV_ALIGN_CENTER, 0, -47);
        lv_obj_set_style_transform_pivot_x(clock_second_hand, 1, 0);
        lv_obj_set_style_transform_pivot_y(clock_second_hand, 92, 0);
        lv_obj_remove_flag(clock_second_hand, LV_OBJ_FLAG_SCROLLABLE);
        
        // Center dot (on top of hands)
        lv_obj_t *center = lv_obj_create(clock_face);
        lv_obj_set_size(center, 12, 12);
        lv_obj_center(center);
        lv_obj_set_style_bg_color(center, lv_color_hex(0x4A90D9), 0);
        lv_obj_set_style_border_width(center, 0, 0);
        lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(center, LV_OBJ_FLAG_SCROLLABLE);
        
    } else if (clock_mode == 1) {
        // ===== ALARM MODE - Win7 Style =====
        lv_obj_t *alarm_panel = lv_obj_create(clock_content);
        lv_obj_set_size(alarm_panel, lv_pct(100) - 20, 500);
        lv_obj_align(alarm_panel, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_bg_color(alarm_panel, lv_color_white(), 0);
        lv_obj_set_style_border_color(alarm_panel, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(alarm_panel, 1, 0);
        lv_obj_set_style_radius(alarm_panel, 4, 0);
        lv_obj_set_style_pad_all(alarm_panel, 15, 0);
        lv_obj_remove_flag(alarm_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        if (alarm_edit_mode) {
            // Alarm creation/edit mode
            lv_obj_t *edit_header = lv_label_create(alarm_panel);
            lv_label_set_text(edit_header, LV_SYMBOL_BELL " New Alarm");
            lv_obj_set_style_text_color(edit_header, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(edit_header, UI_FONT, 0);
            lv_obj_align(edit_header, LV_ALIGN_TOP_LEFT, 0, 0);
            
            // Time picker
            lv_obj_t *picker_row = lv_obj_create(alarm_panel);
            lv_obj_set_size(picker_row, lv_pct(100), 150);
            lv_obj_align(picker_row, LV_ALIGN_TOP_MID, 0, 40);
            lv_obj_set_style_bg_opa(picker_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(picker_row, 0, 0);
            lv_obj_set_flex_flow(picker_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(picker_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_remove_flag(picker_row, LV_OBJ_FLAG_SCROLLABLE);
            
            // Hour column
            lv_obj_t *hour_col = lv_obj_create(picker_row);
            lv_obj_set_size(hour_col, 100, 140);
            lv_obj_set_style_bg_opa(hour_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(hour_col, 0, 0);
            lv_obj_remove_flag(hour_col, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *hour_up = create_clock_win7_btn(hour_col, "+", 60, 35, 0x4A90D9);
            lv_obj_align(hour_up, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_add_event_cb(hour_up, [](lv_event_t *e) {
                alarm_edit_hour = (alarm_edit_hour + 1) % 24;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            char hour_buf[8];
            snprintf(hour_buf, sizeof(hour_buf), "%02d", alarm_edit_hour);
            lv_obj_t *hour_lbl = lv_label_create(hour_col);
            lv_label_set_text(hour_lbl, hour_buf);
            lv_obj_set_style_text_color(hour_lbl, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(hour_lbl, UI_FONT, 0);
            lv_obj_align(hour_lbl, LV_ALIGN_CENTER, 0, 0);
            
            lv_obj_t *hour_down = create_clock_win7_btn(hour_col, "-", 60, 35, 0x4A90D9);
            lv_obj_align(hour_down, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(hour_down, [](lv_event_t *e) {
                alarm_edit_hour = (alarm_edit_hour + 23) % 24;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *colon = lv_label_create(picker_row);
            lv_label_set_text(colon, ":");
            lv_obj_set_style_text_color(colon, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(colon, UI_FONT, 0);
            
            // Minute column
            lv_obj_t *min_col = lv_obj_create(picker_row);
            lv_obj_set_size(min_col, 100, 140);
            lv_obj_set_style_bg_opa(min_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(min_col, 0, 0);
            lv_obj_remove_flag(min_col, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *min_up = create_clock_win7_btn(min_col, "+", 60, 35, 0x4A90D9);
            lv_obj_align(min_up, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_add_event_cb(min_up, [](lv_event_t *e) {
                alarm_edit_minute = (alarm_edit_minute + 5) % 60;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            char min_buf[8];
            snprintf(min_buf, sizeof(min_buf), "%02d", alarm_edit_minute);
            lv_obj_t *min_lbl = lv_label_create(min_col);
            lv_label_set_text(min_lbl, min_buf);
            lv_obj_set_style_text_color(min_lbl, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(min_lbl, UI_FONT, 0);
            lv_obj_align(min_lbl, LV_ALIGN_CENTER, 0, 0);
            
            lv_obj_t *min_down = create_clock_win7_btn(min_col, "-", 60, 35, 0x4A90D9);
            lv_obj_align(min_down, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(min_down, [](lv_event_t *e) {
                alarm_edit_minute = (alarm_edit_minute + 55) % 60;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            // Buttons
            lv_obj_t *btn_row = lv_obj_create(alarm_panel);
            lv_obj_set_size(btn_row, lv_pct(100), 50);
            lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn_row, 0, 0);
            lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *save_btn = create_clock_win7_btn(btn_row, "Save", 120, 40, 0x44AA44);
            lv_obj_add_event_cb(save_btn, [](lv_event_t *e) {
                if (alarm_count < 5) {
                    alarms[alarm_count].hour = alarm_edit_hour;
                    alarms[alarm_count].minute = alarm_edit_minute;
                    alarms[alarm_count].enabled = true;
                    snprintf(alarms[alarm_count].name, sizeof(alarms[alarm_count].name), "Alarm %d", alarm_count + 1);
                    alarm_count++;
                }
                alarm_edit_mode = false;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *cancel_btn = create_clock_win7_btn(btn_row, "Cancel", 120, 40, 0x6A6A6A);
            lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
                alarm_edit_mode = false;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
        } else {
            // Alarm list mode
            lv_obj_t *alarm_header = lv_label_create(alarm_panel);
            lv_label_set_text(alarm_header, LV_SYMBOL_BELL " Alarms");
            lv_obj_set_style_text_color(alarm_header, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(alarm_header, UI_FONT, 0);
            lv_obj_align(alarm_header, LV_ALIGN_TOP_LEFT, 0, 0);
            
            // Alarm list (scrollable)
            lv_obj_t *alarm_list = lv_obj_create(alarm_panel);
            lv_obj_set_size(alarm_list, lv_pct(100), 380);
            lv_obj_align(alarm_list, LV_ALIGN_TOP_MID, 0, 30);
            lv_obj_set_style_bg_opa(alarm_list, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(alarm_list, 0, 0);
            lv_obj_set_flex_flow(alarm_list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(alarm_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_row(alarm_list, 8, 0);
            
            for (int i = 0; i < alarm_count; i++) {
                lv_obj_t *alarm_card = lv_obj_create(alarm_list);
                lv_obj_set_size(alarm_card, lv_pct(100), 65);
                lv_obj_set_style_bg_color(alarm_card, lv_color_hex(0xF8F8F8), 0);
                lv_obj_set_style_border_color(alarm_card, lv_color_hex(0xDDDDDD), 0);
                lv_obj_set_style_border_width(alarm_card, 1, 0);
                lv_obj_set_style_radius(alarm_card, 4, 0);
                lv_obj_set_style_pad_all(alarm_card, 10, 0);
                lv_obj_remove_flag(alarm_card, LV_OBJ_FLAG_SCROLLABLE);
                
                char time_str[8];
                snprintf(time_str, sizeof(time_str), "%02d:%02d", alarms[i].hour, alarms[i].minute);
                lv_obj_t *time_lbl = lv_label_create(alarm_card);
                lv_label_set_text(time_lbl, time_str);
                lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x333333), 0);
                lv_obj_set_style_text_font(time_lbl, UI_FONT, 0);
                lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 0, -10);
                
                lv_obj_t *name_lbl = lv_label_create(alarm_card);
                lv_label_set_text(name_lbl, alarms[i].name);
                lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x888888), 0);
                lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, 12);
                
                lv_obj_t *toggle = lv_switch_create(alarm_card);
                lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -5, 0);
                lv_obj_set_style_bg_color(toggle, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
                lv_obj_set_style_bg_color(toggle, lv_color_hex(0x4A90D9), LV_PART_INDICATOR | LV_STATE_CHECKED);
                if (alarms[i].enabled) lv_obj_add_state(toggle, LV_STATE_CHECKED);
                
                lv_obj_set_user_data(toggle, (void*)(intptr_t)i);
                lv_obj_add_event_cb(toggle, [](lv_event_t *e) {
                    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
                    int idx = (int)(intptr_t)lv_obj_get_user_data(sw);
                    alarms[idx].enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
                }, LV_EVENT_VALUE_CHANGED, NULL);
            }
            
            // Add alarm button
            lv_obj_t *add_btn = create_clock_win7_btn(alarm_panel, "+ Add Alarm", 160, 40, 0x4A90D9);
            lv_obj_align(add_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_add_event_cb(add_btn, [](lv_event_t *e) {
                if (alarm_count < 5) {
                    alarm_edit_mode = true;
                    alarm_edit_hour = 7;
                    alarm_edit_minute = 0;
                    clock_rebuild_content();
                }
            }, LV_EVENT_CLICKED, NULL);
        }
        
    } else if (clock_mode == 2) {
        // ===== TIMER MODE - Win7 Style =====
        lv_obj_t *timer_panel = lv_obj_create(clock_content);
        lv_obj_set_size(timer_panel, lv_pct(100) - 20, 500);
        lv_obj_align(timer_panel, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_bg_color(timer_panel, lv_color_white(), 0);
        lv_obj_set_style_border_color(timer_panel, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(timer_panel, 1, 0);
        lv_obj_set_style_radius(timer_panel, 4, 0);
        lv_obj_set_style_pad_all(timer_panel, 15, 0);
        lv_obj_remove_flag(timer_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        // Custom time picker (when not running)
        if (!timer_running) {
            lv_obj_t *picker_row = lv_obj_create(timer_panel);
            lv_obj_set_size(picker_row, lv_pct(100), 100);
            lv_obj_align(picker_row, LV_ALIGN_TOP_MID, 0, 10);
            lv_obj_set_style_bg_opa(picker_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(picker_row, 0, 0);
            lv_obj_set_flex_flow(picker_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(picker_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_remove_flag(picker_row, LV_OBJ_FLAG_SCROLLABLE);
            
            // Minutes column
            lv_obj_t *min_col = lv_obj_create(picker_row);
            lv_obj_set_size(min_col, 100, 90);
            lv_obj_set_style_bg_opa(min_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(min_col, 0, 0);
            lv_obj_remove_flag(min_col, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *min_up = create_clock_win7_btn(min_col, "+", 50, 30, 0x4A90D9);
            lv_obj_align(min_up, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_add_event_cb(min_up, [](lv_event_t *e) {
                timer_seconds += 60;
                if (timer_seconds > 5999) timer_seconds = 5999;
                timer_remaining = timer_seconds;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            char min_buf[16];
            snprintf(min_buf, sizeof(min_buf), "%02d", (timer_seconds / 60) % 100);
            lv_obj_t *min_lbl = lv_label_create(min_col);
            lv_label_set_text(min_lbl, min_buf);
            lv_obj_set_style_text_color(min_lbl, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(min_lbl, UI_FONT, 0);
            lv_obj_align(min_lbl, LV_ALIGN_CENTER, 0, 0);
            
            lv_obj_t *min_down = create_clock_win7_btn(min_col, "-", 50, 30, 0x4A90D9);
            lv_obj_align(min_down, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(min_down, [](lv_event_t *e) {
                if (timer_seconds >= 60) timer_seconds -= 60;
                timer_remaining = timer_seconds;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *min_txt = lv_label_create(picker_row);
            lv_label_set_text(min_txt, "min");
            lv_obj_set_style_text_color(min_txt, lv_color_hex(0x666666), 0);
            
            // Seconds column
            lv_obj_t *sec_col = lv_obj_create(picker_row);
            lv_obj_set_size(sec_col, 100, 90);
            lv_obj_set_style_bg_opa(sec_col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(sec_col, 0, 0);
            lv_obj_remove_flag(sec_col, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *sec_up = create_clock_win7_btn(sec_col, "+", 50, 30, 0x4A90D9);
            lv_obj_align(sec_up, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_add_event_cb(sec_up, [](lv_event_t *e) {
                int secs = timer_seconds % 60;
                int mins = timer_seconds / 60;
                secs += 10;
                if (secs >= 60) { secs = 0; mins++; }
                if (mins > 99) mins = 99;
                timer_seconds = mins * 60 + secs;
                timer_remaining = timer_seconds;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            char sec_buf[8];
            snprintf(sec_buf, sizeof(sec_buf), "%02d", timer_seconds % 60);
            lv_obj_t *sec_lbl = lv_label_create(sec_col);
            lv_label_set_text(sec_lbl, sec_buf);
            lv_obj_set_style_text_color(sec_lbl, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(sec_lbl, UI_FONT, 0);
            lv_obj_align(sec_lbl, LV_ALIGN_CENTER, 0, 0);
            
            lv_obj_t *sec_down = create_clock_win7_btn(sec_col, "-", 50, 30, 0x4A90D9);
            lv_obj_align(sec_down, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(sec_down, [](lv_event_t *e) {
                int secs = timer_seconds % 60;
                int mins = timer_seconds / 60;
                if (secs >= 10) secs -= 10;
                else if (mins > 0) { mins--; secs = 50; }
                timer_seconds = mins * 60 + secs;
                timer_remaining = timer_seconds;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *sec_txt = lv_label_create(picker_row);
            lv_label_set_text(sec_txt, "sec");
            lv_obj_set_style_text_color(sec_txt, lv_color_hex(0x666666), 0);
        } else {
            // Timer display when running
            timer_label = lv_label_create(timer_panel);
            char buf[16];
            int mins = timer_remaining / 60;
            int secs = timer_remaining % 60;
            snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
            lv_label_set_text(timer_label, buf);
            lv_obj_set_style_text_color(timer_label, lv_color_hex(0x1A5090), 0);
            lv_obj_set_style_text_font(timer_label, UI_FONT, 0);
            lv_obj_align(timer_label, LV_ALIGN_TOP_MID, 0, 30);
        }
        
        // Circular progress arc
        lv_obj_t *arc = lv_arc_create(timer_panel);
        lv_obj_set_size(arc, 200, 200);
        lv_obj_align(arc, LV_ALIGN_CENTER, 0, 20);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_range(arc, 0, timer_seconds > 0 ? timer_seconds : 1);
        lv_arc_set_value(arc, timer_running ? timer_remaining : timer_seconds);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x4A90D9), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        
        // Preset buttons
        lv_obj_t *presets = lv_obj_create(timer_panel);
        lv_obj_set_size(presets, lv_pct(100), 45);
        lv_obj_align(presets, LV_ALIGN_BOTTOM_MID, 0, -70);
        lv_obj_set_style_bg_opa(presets, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(presets, 0, 0);
        lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(presets, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(presets, LV_OBJ_FLAG_SCROLLABLE);
        
        static const char* preset_labels[] = {"1m", "5m", "10m", "15m"};
        static const int preset_secs[] = {60, 300, 600, 900};
        for (int i = 0; i < 4; i++) {
            lv_obj_t *btn = create_clock_win7_btn(presets, preset_labels[i], 80, 35, 0x6A6A6A);
            lv_obj_set_user_data(btn, (void*)(intptr_t)preset_secs[i]);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
                int secs = (int)(intptr_t)lv_obj_get_user_data(b);
                timer_seconds = secs;
                timer_remaining = secs;
                timer_running = false;
                clock_rebuild_content();
            }, LV_EVENT_CLICKED, NULL);
        }
        
        // Control buttons
        lv_obj_t *controls = lv_obj_create(timer_panel);
        lv_obj_set_size(controls, 260, 50);
        lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(controls, 0, 0);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *start_btn = create_clock_win7_btn(controls, timer_running ? "Stop" : "Start", 110, 40, 
                                                    timer_running ? 0xCC4444 : 0x44AA44);
        lv_obj_add_event_cb(start_btn, [](lv_event_t *e) {
            if (timer_running) {
                timer_running = false;
            } else if (timer_seconds > 0) {
                timer_running = true;
                timer_start_time = esp_timer_get_time() / 1000;
                if (timer_remaining <= 0) timer_remaining = timer_seconds;
            }
            clock_rebuild_content();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *reset_btn = create_clock_win7_btn(controls, "Reset", 110, 40, 0x6A6A6A);
        lv_obj_add_event_cb(reset_btn, [](lv_event_t *e) {
            timer_running = false;
            timer_remaining = timer_seconds;
            clock_rebuild_content();
        }, LV_EVENT_CLICKED, NULL);
        
    } else if (clock_mode == 3) {
        // ===== STOPWATCH MODE - Win7 Style =====
        lv_obj_t *sw_panel = lv_obj_create(clock_content);
        lv_obj_set_size(sw_panel, lv_pct(100) - 20, 500);
        lv_obj_align(sw_panel, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_bg_color(sw_panel, lv_color_white(), 0);
        lv_obj_set_style_border_color(sw_panel, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(sw_panel, 1, 0);
        lv_obj_set_style_radius(sw_panel, 4, 0);
        lv_obj_set_style_pad_all(sw_panel, 15, 0);
        lv_obj_remove_flag(sw_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        // Stopwatch display - large
        stopwatch_label = lv_label_create(sw_panel);
        int64_t total_ms = stopwatch_elapsed;
        if (stopwatch_running) {
            int64_t current = esp_timer_get_time() / 1000;
            total_ms += (current - stopwatch_start_time);
        }
        int sw_mins = (total_ms / 60000) % 60;
        int sw_secs = (total_ms / 1000) % 60;
        int sw_ms = (total_ms / 10) % 100;
        char sw_buf[16];
        snprintf(sw_buf, sizeof(sw_buf), "%02d:%02d.%02d", sw_mins, sw_secs, sw_ms);
        lv_label_set_text(stopwatch_label, sw_buf);
        lv_obj_set_style_text_color(stopwatch_label, lv_color_hex(0x1A5090), 0);
        lv_obj_set_style_text_font(stopwatch_label, UI_FONT, 0);
        lv_obj_align(stopwatch_label, LV_ALIGN_TOP_MID, 0, 10);
        
        // Lap times list (scrollable)
        lv_obj_t *lap_list = lv_obj_create(sw_panel);
        lv_obj_set_size(lap_list, lv_pct(100), 280);
        lv_obj_align(lap_list, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_set_style_bg_color(lap_list, lv_color_hex(0xF8F8F8), 0);
        lv_obj_set_style_border_color(lap_list, lv_color_hex(0xDDDDDD), 0);
        lv_obj_set_style_border_width(lap_list, 1, 0);
        lv_obj_set_style_radius(lap_list, 4, 0);
        lv_obj_set_style_pad_all(lap_list, 8, 0);
        lv_obj_set_flex_flow(lap_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(lap_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        if (lap_count == 0) {
            lv_obj_t *no_laps = lv_label_create(lap_list);
            lv_label_set_text(no_laps, "No laps recorded");
            lv_obj_set_style_text_color(no_laps, lv_color_hex(0x888888), 0);
            lv_obj_set_style_text_font(no_laps, UI_FONT, 0);
        } else {
            for (int i = lap_count - 1; i >= 0; i--) {
                lv_obj_t *lap_row = lv_obj_create(lap_list);
                lv_obj_set_size(lap_row, lv_pct(100), 30);
                lv_obj_set_style_bg_opa(lap_row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(lap_row, 0, 0);
                lv_obj_set_style_pad_all(lap_row, 2, 0);
                lv_obj_remove_flag(lap_row, LV_OBJ_FLAG_SCROLLABLE);
                
                char lap_num[16];
                snprintf(lap_num, sizeof(lap_num), "Lap %d:", i + 1);
                lv_obj_t *num_lbl = lv_label_create(lap_row);
                lv_label_set_text(num_lbl, lap_num);
                lv_obj_set_style_text_color(num_lbl, lv_color_hex(0x666666), 0);
                lv_obj_set_style_text_font(num_lbl, UI_FONT, 0);
                lv_obj_align(num_lbl, LV_ALIGN_LEFT_MID, 0, 0);
                
                int64_t lap_ms = lap_times[i];
                int l_mins = (lap_ms / 60000) % 60;
                int l_secs = (lap_ms / 1000) % 60;
                int l_ms = (lap_ms / 10) % 100;
                char lap_time_str[16];
                snprintf(lap_time_str, sizeof(lap_time_str), "%02d:%02d.%02d", l_mins, l_secs, l_ms);
                lv_obj_t *time_lbl = lv_label_create(lap_row);
                lv_label_set_text(time_lbl, lap_time_str);
                lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x1A5090), 0);
                lv_obj_set_style_text_font(time_lbl, UI_FONT, 0);
                lv_obj_align(time_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
            }
        }
        
        // Control buttons
        lv_obj_t *controls = lv_obj_create(sw_panel);
        lv_obj_set_size(controls, 350, 55);
        lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(controls, 0, 0);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *start_btn = create_clock_win7_btn(controls, stopwatch_running ? "Stop" : "Start", 100, 45,
                                                    stopwatch_running ? 0xCC4444 : 0x44AA44);
        lv_obj_add_event_cb(start_btn, [](lv_event_t *e) {
            if (stopwatch_running) {
                int64_t current = esp_timer_get_time() / 1000;
                stopwatch_elapsed += (current - stopwatch_start_time);
                stopwatch_running = false;
            } else {
                stopwatch_start_time = esp_timer_get_time() / 1000;
                stopwatch_running = true;
            }
            clock_rebuild_content();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lap_btn = create_clock_win7_btn(controls, "Lap", 100, 45, 0x4A90D9);
        lv_obj_add_event_cb(lap_btn, [](lv_event_t *e) {
            if (stopwatch_running && lap_count < 10) {
                int64_t current = esp_timer_get_time() / 1000;
                int64_t total_ms = stopwatch_elapsed + (current - stopwatch_start_time);
                lap_times[lap_count] = total_ms;
                lap_count++;
                clock_rebuild_content();
            }
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *reset_btn = create_clock_win7_btn(controls, "Reset", 100, 45, 0x6A6A6A);
        lv_obj_add_event_cb(reset_btn, [](lv_event_t *e) {
            stopwatch_running = false;
            stopwatch_elapsed = 0;
            lap_count = 0;
            for (int i = 0; i < 10; i++) lap_times[i] = 0;
            clock_rebuild_content();
        }, LV_EVENT_CLICKED, NULL);
    }
    
    // Trigger initial time update
    clock_timer_cb(NULL);
}

void app_clock_create(void)
{
    ESP_LOGI(TAG, "Opening Clock");
    create_app_window("Date and Time");
    
    // Reset state
    clock_mode = 0;
    
    // Content area - Win7 light style background
    clock_content = lv_obj_create(app_window);
    lv_obj_set_size(clock_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(clock_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(clock_content, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(clock_content, 0, 0);
    lv_obj_set_style_radius(clock_content, 0, 0);
    lv_obj_remove_flag(clock_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Enable gesture detection for swipe
    lv_obj_add_flag(clock_content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_content, clock_swipe_cb, LV_EVENT_GESTURE, NULL);
    
    // Build initial content
    clock_rebuild_content();
    
    // Start timer (100ms for smooth stopwatch)
    clock_timer = lv_timer_create(clock_timer_cb, 100, NULL);
}

// ============ WEATHER APP (Aero Glass Style) ============

// Helper to create Aero Glass panel
static lv_obj_t* create_aero_glass_panel(lv_obj_t *parent, int w, int h, int x, int y, lv_align_t align)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_align(panel, align, x, y);
    
    // Aero Glass effect - semi-transparent with gradient
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x4080C0), 0);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x1A3A5C), 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
    
    // Glass border glow
    lv_obj_set_style_border_color(panel, lv_color_hex(0x80C0FF), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, 0);
    
    // Soft shadow
    lv_obj_set_style_shadow_width(panel, 20, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 5, 0);
    
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    
    return panel;
}

// Weather API includes
#include "weather_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void weather_update_ui(void);

// Callback for lv_async_call
static void weather_async_update_cb(void *arg)
{
    (void)arg;
    weather_update_ui();
}

static void weather_fetch_task(void *arg)
{
    location_settings_t *loc = settings_get_location();
    weather_data_t data;
    
    if (!loc || !loc->valid) {
        ESP_LOGW(TAG, "No location set, using Moscow");
        weather_api_fetch(55.7558, 37.6173, &data);
    } else {
        weather_api_fetch(loc->latitude, loc->longitude, &data);
    }
    
    weather_fetching = false;
    
    // Update UI from main thread
    lv_async_call(weather_async_update_cb, NULL);
    
    vTaskDelete(NULL);
}

static void weather_refresh_clicked(lv_event_t *e)
{
    if (weather_fetching) return;
    
    if (weather_status_label) {
        lv_label_set_text(weather_status_label, "Fetching weather data...");
    }
    
    weather_fetching = true;
    xTaskCreate(weather_fetch_task, "weather_fetch", 8192, NULL, 5, NULL);
}

static void weather_update_ui(void)
{
    // Safety check - UI may have been closed before async callback
    if (!weather_content || !lv_obj_is_valid(weather_content)) {
        ESP_LOGW(TAG, "Weather UI closed, skipping update");
        return;
    }
    
    weather_data_t *data = weather_api_get_cached();
    
    if (!data || !data->valid) {
        if (weather_status_label && lv_obj_is_valid(weather_status_label)) {
            lv_label_set_text(weather_status_label, "Failed to fetch weather");
        }
        return;
    }
    
    if (weather_location_label && lv_obj_is_valid(weather_location_label)) {
        lv_label_set_text(weather_location_label, data->city_name);
    }
    
    if (weather_temp_label && lv_obj_is_valid(weather_temp_label)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f°", data->current.temperature);
        lv_label_set_text(weather_temp_label, buf);
    }
    
    if (weather_condition_label && lv_obj_is_valid(weather_condition_label)) {
        lv_label_set_text(weather_condition_label, weather_code_to_string(data->current.weather_code));
    }
    
    if (weather_feels_label && lv_obj_is_valid(weather_feels_label)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Feels like %.0f°C", data->current.apparent_temperature);
        lv_label_set_text(weather_feels_label, buf);
    }
    
    if (weather_wind_label && lv_obj_is_valid(weather_wind_label)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f km/h", data->current.wind_speed);
        lv_label_set_text(weather_wind_label, buf);
    }
    
    if (weather_humidity_label && lv_obj_is_valid(weather_humidity_label)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f%%", data->current.humidity);
        lv_label_set_text(weather_humidity_label, buf);
    }
    
    if (weather_pressure_label && lv_obj_is_valid(weather_pressure_label)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f hPa", data->current.pressure);
        lv_label_set_text(weather_pressure_label, buf);
    }
    
    for (int i = 0; i < 5 && i < data->daily_count; i++) {
        if (weather_forecast_days[i] && lv_obj_is_valid(weather_forecast_days[i])) {
            lv_label_set_text(weather_forecast_days[i], data->daily[i].day_name);
        }
        if (weather_forecast_temps_hi[i] && lv_obj_is_valid(weather_forecast_temps_hi[i])) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%.0f°", data->daily[i].temp_max);
            lv_label_set_text(weather_forecast_temps_hi[i], buf);
        }
        if (weather_forecast_temps_lo[i] && lv_obj_is_valid(weather_forecast_temps_lo[i])) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%.0f°", data->daily[i].temp_min);
            lv_label_set_text(weather_forecast_temps_lo[i], buf);
        }
    }
    
    if (weather_status_label && lv_obj_is_valid(weather_status_label)) {
        time_t now = time(NULL);
        int mins_ago = (now - data->fetch_time) / 60;
        char buf[64];
        if (mins_ago < 1) {
            snprintf(buf, sizeof(buf), "Updated just now");
        } else {
            snprintf(buf, sizeof(buf), "Updated %d min ago", mins_ago);
        }
        lv_label_set_text(weather_status_label, buf);
    }
}

void app_weather_create(void)
{
    ESP_LOGI(TAG, "Opening Weather");
    create_app_window("Weather");
    
    weather_api_init();
    
    // Vista style content area - light blue gradient like Settings
    weather_content = lv_obj_create(app_window);
    lv_obj_set_size(weather_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(weather_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(weather_content, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(weather_content, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(weather_content, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(weather_content, 0, 0);
    lv_obj_set_style_radius(weather_content, 0, 0);
    lv_obj_set_style_pad_all(weather_content, 10, 0);
    lv_obj_set_flex_flow(weather_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(weather_content, 8, 0);
    lv_obj_remove_flag(weather_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header bar with location and refresh
    lv_obj_t *header = lv_obj_create(weather_content);
    lv_obj_set_size(header, lv_pct(100), 45);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, 6, 0);
    lv_obj_set_style_pad_left(header, 12, 0);
    lv_obj_set_style_pad_right(header, 8, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *loc_icon = lv_label_create(header);
    lv_label_set_text(loc_icon, LV_SYMBOL_GPS);
    lv_obj_set_style_text_color(loc_icon, lv_color_hex(0x4A90D9), 0);
    lv_obj_align(loc_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    weather_location_label = lv_label_create(header);
    location_settings_t *loc = settings_get_location();
    lv_label_set_text(weather_location_label, loc && loc->valid ? loc->city_name : "Not set");
    lv_obj_set_style_text_color(weather_location_label, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(weather_location_label, UI_FONT, 0);
    lv_obj_align(weather_location_label, LV_ALIGN_LEFT_MID, 25, 0);
    
    // Vista style refresh button
    lv_obj_t *refresh_btn = lv_btn_create(header);
    lv_obj_set_size(refresh_btn, 80, 32);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(refresh_btn, lv_color_hex(0xD0E8F8), 0);
    lv_obj_set_style_bg_grad_dir(refresh_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(refresh_btn, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(refresh_btn, 1, 0);
    lv_obj_set_style_radius(refresh_btn, 4, 0);
    lv_obj_set_style_shadow_width(refresh_btn, 0, 0);
    lv_obj_add_event_cb(refresh_btn, weather_refresh_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, LV_SYMBOL_REFRESH " Update");
    lv_obj_set_style_text_color(refresh_lbl, lv_color_hex(0x1A5090), 0);
    lv_obj_center(refresh_lbl);
    
    // Current weather panel - white with blue border
    lv_obj_t *current_panel = lv_obj_create(weather_content);
    lv_obj_set_size(current_panel, lv_pct(100), 200);
    lv_obj_set_style_bg_color(current_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(current_panel, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(current_panel, 1, 0);
    lv_obj_set_style_radius(current_panel, 6, 0);
    lv_obj_set_style_pad_all(current_panel, 15, 0);
    lv_obj_remove_flag(current_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    // Weather icon area (left side) - use actual weather icon
    lv_obj_t *icon_area = lv_obj_create(current_panel);
    lv_obj_set_size(icon_area, 100, 100);
    lv_obj_align(icon_area, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(icon_area, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_border_color(icon_area, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(icon_area, 1, 0);
    lv_obj_set_style_radius(icon_area, 50, 0);
    lv_obj_remove_flag(icon_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Use actual weather icon image
    lv_obj_t *weather_icon = lv_image_create(icon_area);
    lv_image_set_src(weather_icon, &img_weather);
    lv_obj_center(weather_icon);
    
    // Temperature (center-right)
    weather_temp_label = lv_label_create(current_panel);
    lv_label_set_text(weather_temp_label, "--°");
    lv_obj_set_style_text_color(weather_temp_label, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(weather_temp_label, UI_FONT, 0);
    lv_obj_align(weather_temp_label, LV_ALIGN_CENTER, 40, -30);
    
    // Condition
    weather_condition_label = lv_label_create(current_panel);
    lv_label_set_text(weather_condition_label, "Loading...");
    lv_obj_set_style_text_color(weather_condition_label, lv_color_hex(0x4A6080), 0);
    lv_obj_set_style_text_font(weather_condition_label, UI_FONT, 0);
    lv_obj_align(weather_condition_label, LV_ALIGN_CENTER, 40, 10);
    
    // Feels like
    weather_feels_label = lv_label_create(current_panel);
    lv_label_set_text(weather_feels_label, "Feels like --°C");
    lv_obj_set_style_text_color(weather_feels_label, lv_color_hex(0x6080A0), 0);
    lv_obj_align(weather_feels_label, LV_ALIGN_CENTER, 40, 40);
    
    // Details panel - white with blue border
    lv_obj_t *details_panel = lv_obj_create(weather_content);
    lv_obj_set_size(details_panel, lv_pct(100), 80);
    lv_obj_set_style_bg_color(details_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(details_panel, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(details_panel, 1, 0);
    lv_obj_set_style_radius(details_panel, 6, 0);
    lv_obj_remove_flag(details_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(details_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(details_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(details_panel, 10, 0);
    
    // Wind detail
    lv_obj_t *wind_box = lv_obj_create(details_panel);
    lv_obj_set_size(wind_box, 120, 60);
    lv_obj_set_style_bg_color(wind_box, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_border_color(wind_box, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(wind_box, 1, 0);
    lv_obj_set_style_radius(wind_box, 4, 0);
    lv_obj_remove_flag(wind_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *wind_lbl = lv_label_create(wind_box);
    lv_label_set_text(wind_lbl, "Wind");
    lv_obj_set_style_text_color(wind_lbl, lv_color_hex(0x6080A0), 0);
    lv_obj_align(wind_lbl, LV_ALIGN_TOP_MID, 0, 5);
    
    weather_wind_label = lv_label_create(wind_box);
    lv_label_set_text(weather_wind_label, "-- km/h");
    lv_obj_set_style_text_color(weather_wind_label, lv_color_hex(0x1A5090), 0);
    lv_obj_align(weather_wind_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // Humidity detail
    lv_obj_t *hum_box = lv_obj_create(details_panel);
    lv_obj_set_size(hum_box, 120, 60);
    lv_obj_set_style_bg_color(hum_box, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_border_color(hum_box, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(hum_box, 1, 0);
    lv_obj_set_style_radius(hum_box, 4, 0);
    lv_obj_remove_flag(hum_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *hum_lbl = lv_label_create(hum_box);
    lv_label_set_text(hum_lbl, "Humidity");
    lv_obj_set_style_text_color(hum_lbl, lv_color_hex(0x6080A0), 0);
    lv_obj_align(hum_lbl, LV_ALIGN_TOP_MID, 0, 5);
    
    weather_humidity_label = lv_label_create(hum_box);
    lv_label_set_text(weather_humidity_label, "--%");
    lv_obj_set_style_text_color(weather_humidity_label, lv_color_hex(0x1A5090), 0);
    lv_obj_align(weather_humidity_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // Pressure detail
    lv_obj_t *pres_box = lv_obj_create(details_panel);
    lv_obj_set_size(pres_box, 120, 60);
    lv_obj_set_style_bg_color(pres_box, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_border_color(pres_box, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(pres_box, 1, 0);
    lv_obj_set_style_radius(pres_box, 4, 0);
    lv_obj_remove_flag(pres_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *pres_lbl = lv_label_create(pres_box);
    lv_label_set_text(pres_lbl, "Pressure");
    lv_obj_set_style_text_color(pres_lbl, lv_color_hex(0x6080A0), 0);
    lv_obj_align(pres_lbl, LV_ALIGN_TOP_MID, 0, 5);
    
    weather_pressure_label = lv_label_create(pres_box);
    lv_label_set_text(weather_pressure_label, "-- hPa");
    lv_obj_set_style_text_color(weather_pressure_label, lv_color_hex(0x1A5090), 0);
    lv_obj_align(weather_pressure_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // Forecast panel - white with blue border
    lv_obj_t *forecast = lv_obj_create(weather_content);
    lv_obj_set_size(forecast, lv_pct(100), 200);
    lv_obj_set_style_bg_color(forecast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(forecast, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(forecast, 1, 0);
    lv_obj_set_style_radius(forecast, 6, 0);
    lv_obj_set_style_pad_all(forecast, 10, 0);
    lv_obj_remove_flag(forecast, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *forecast_title = lv_label_create(forecast);
    lv_label_set_text(forecast_title, "5-Day Forecast");
    lv_obj_set_style_text_color(forecast_title, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(forecast_title, UI_FONT, 0);
    lv_obj_align(forecast_title, LV_ALIGN_TOP_LEFT, 5, 0);
    
    // Forecast cards container
    lv_obj_t *cards_row = lv_obj_create(forecast);
    lv_obj_set_size(cards_row, lv_pct(100), 150);
    lv_obj_align(cards_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cards_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cards_row, 0, 0);
    lv_obj_set_flex_flow(cards_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cards_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(cards_row, LV_OBJ_FLAG_SCROLLABLE);
    
    for (int i = 0; i < 5; i++) {
        lv_obj_t *day_card = lv_obj_create(cards_row);
        lv_obj_set_size(day_card, 75, 130);
        lv_obj_set_style_bg_color(day_card, lv_color_hex(0xE8F4FC), 0);
        lv_obj_set_style_border_color(day_card, lv_color_hex(0xB0D0E8), 0);
        lv_obj_set_style_border_width(day_card, 1, 0);
        lv_obj_set_style_radius(day_card, 6, 0);
        lv_obj_remove_flag(day_card, LV_OBJ_FLAG_SCROLLABLE);
        
        weather_forecast_days[i] = lv_label_create(day_card);
        lv_label_set_text(weather_forecast_days[i], "---");
        lv_obj_set_style_text_color(weather_forecast_days[i], lv_color_hex(0x4A6080), 0);
        lv_obj_align(weather_forecast_days[i], LV_ALIGN_TOP_MID, 0, 8);
        
        // Use actual weather icon image (scaled down)
        lv_obj_t *w_icon = lv_image_create(day_card);
        lv_image_set_src(w_icon, &img_weather);
        lv_image_set_scale(w_icon, 160);  // Scale to ~32px from 48px
        lv_obj_align(w_icon, LV_ALIGN_CENTER, 0, -5);
        
        weather_forecast_temps_hi[i] = lv_label_create(day_card);
        lv_label_set_text(weather_forecast_temps_hi[i], "--°");
        lv_obj_set_style_text_color(weather_forecast_temps_hi[i], lv_color_hex(0x1A5090), 0);
        lv_obj_align(weather_forecast_temps_hi[i], LV_ALIGN_BOTTOM_MID, 0, -30);
        
        weather_forecast_temps_lo[i] = lv_label_create(day_card);
        lv_label_set_text(weather_forecast_temps_lo[i], "--°");
        lv_obj_set_style_text_color(weather_forecast_temps_lo[i], lv_color_hex(0x6080A0), 0);
        lv_obj_align(weather_forecast_temps_lo[i], LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    
    // Status bar - Vista style
    lv_obj_t *status_bar = lv_obj_create(weather_content);
    lv_obj_set_size(status_bar, lv_pct(100), 30);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_border_color(status_bar, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_radius(status_bar, 4, 0);
    lv_obj_set_style_pad_left(status_bar, 10, 0);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *status_icon = lv_label_create(status_bar);
    lv_label_set_text(status_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(status_icon, lv_color_hex(0x4A90D9), 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    weather_status_label = lv_label_create(status_bar);
    lv_label_set_text(weather_status_label, "Tap Update to load weather");
    lv_obj_set_style_text_color(weather_status_label, lv_color_hex(0x4A6080), 0);
    lv_obj_align(weather_status_label, LV_ALIGN_LEFT_MID, 25, 0);
    
    if (weather_api_cache_valid()) {
        weather_update_ui();
    } else if (loc && loc->valid) {
        weather_refresh_clicked(NULL);
    }
}


// ============ SETTINGS APP (Aero Glass Style) ============

// Aero Glass colors
#define AERO_BG_DARK        0x0A1628
#define AERO_BG_LIGHT       0x1A2A40
#define AERO_PANEL_BG       0x2050A0
#define AERO_BORDER_GLOW    0x80C0FF
#define AERO_ITEM_BG        0x2A4A7A

// Helper to create Aero Glass setting item with icon
static lv_obj_t* create_aero_setting_item(lv_obj_t *parent, const char* name, 
                                          const lv_image_dsc_t *icon, lv_event_cb_t click_cb)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 55);
    
    // Aero Glass panel style
    lv_obj_set_style_bg_color(item, lv_color_hex(AERO_ITEM_BG), 0);
    lv_obj_set_style_bg_grad_color(item, lv_color_hex(0x1A3050), 0);
    lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_70, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(AERO_BORDER_GLOW), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_opa(item, LV_OPA_40, 0);
    lv_obj_set_style_radius(item, 10, 0);
    lv_obj_set_style_pad_left(item, 12, 0);
    lv_obj_set_style_pad_right(item, 12, 0);
    lv_obj_set_style_shadow_width(item, 8, 0);
    lv_obj_set_style_shadow_color(item, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(item, LV_OPA_20, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    
    // Pressed state - brighter
    lv_obj_set_style_bg_color(item, lv_color_hex(0x3A6AAA), LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(item, LV_OPA_80, LV_STATE_PRESSED);
    
    // Icon (48x48 scaled to 32x32)
    if (icon) {
        lv_obj_t *icon_img = lv_image_create(item);
        lv_image_set_src(icon_img, icon);
        lv_image_set_scale(icon_img, 170);  // ~32px from 48px
        lv_obj_align(icon_img, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Label
    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, UI_FONT, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, icon ? 42 : 0, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    
    // Arrow
    lv_obj_t *arrow = lv_label_create(item);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x80C0FF), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
    
    if (click_cb) {
        lv_obj_add_event_cb(item, click_cb, LV_EVENT_CLICKED, NULL);
    }
    
    return item;
}

// Helper to create section header
static lv_obj_t* create_aero_section_header(lv_obj_t *parent, const char* text)
{
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, text);
    lv_obj_set_style_text_color(header, lv_color_hex(0x60B0FF), 0);
    lv_obj_set_style_text_font(header, UI_FONT, 0);
    return header;
}

// Helper to create Vista Control Panel sidebar category
static lv_obj_t* create_cp_sidebar_item(lv_obj_t *parent, const char* title, lv_event_cb_t click_cb, void *user_data = NULL)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 28);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xD8ECFC), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 2, 0);
    lv_obj_set_style_pad_left(item, 8, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_t *title_lbl = lv_label_create(item);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x0066CC), 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(title_lbl, LV_OBJ_FLAG_CLICKABLE);
    
    if (click_cb) {
        lv_obj_add_event_cb(item, click_cb, LV_EVENT_CLICKED, user_data);
    }
    
    return item;
}

// Helper to create Vista Control Panel main area item (full width)
static lv_obj_t* create_cp_main_item(lv_obj_t *parent, const char* title, const char* desc, 
                                      const lv_image_dsc_t *icon, lv_event_cb_t click_cb)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 55);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xD8ECFC), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(item, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_radius(item, 4, 0);
    lv_obj_set_style_pad_all(item, 8, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    
    // Icon
    if (icon) {
        lv_obj_t *icon_img = lv_image_create(item);
        lv_image_set_src(icon_img, icon);
        lv_obj_align(icon_img, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Title - blue link style
    lv_obj_t *title_lbl = lv_label_create(item);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x0066CC), 0);
    lv_obj_set_style_text_font(title_lbl, UI_FONT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 52, 2);
    lv_obj_remove_flag(title_lbl, LV_OBJ_FLAG_CLICKABLE);
    
    // Description
    lv_obj_t *desc_lbl = lv_label_create(item);
    lv_label_set_text(desc_lbl, desc);
    lv_obj_set_style_text_color(desc_lbl, lv_color_hex(0x404040), 0);
    lv_obj_align(desc_lbl, LV_ALIGN_TOP_LEFT, 52, 22);
    lv_obj_remove_flag(desc_lbl, LV_OBJ_FLAG_CLICKABLE);
    
    if (click_cb) {
        lv_obj_add_event_cb(item, click_cb, LV_EVENT_CLICKED, NULL);
    }
    
    return item;
}

void app_settings_create(void)
{
    ESP_LOGI(TAG, "Opening Settings");
    create_app_window("Control Panel");
    
    // Content area - Vista Control Panel style (white/light blue)
    lv_obj_t *content = lv_obj_create(app_window);
    lv_obj_set_size(content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(content, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(content, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(content, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Navigation bar at top
    lv_obj_t *navbar = lv_obj_create(content);
    lv_obj_set_size(navbar, lv_pct(100), 40);
    lv_obj_align(navbar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(navbar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(navbar, lv_color_hex(0xD0E8F8), 0);
    lv_obj_set_style_bg_grad_dir(navbar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(navbar, lv_color_hex(0xA0C8E8), 0);
    lv_obj_set_style_border_width(navbar, 1, 0);
    lv_obj_set_style_border_side(navbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(navbar, 0, 0);
    lv_obj_set_style_pad_left(navbar, 10, 0);
    lv_obj_remove_flag(navbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Settings icon and title
    lv_obj_t *cp_icon = lv_image_create(navbar);
    lv_image_set_src(cp_icon, &img_settings);
    lv_image_set_scale(cp_icon, 128);
    lv_obj_align(cp_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *cp_title = lv_label_create(navbar);
    lv_label_set_text(cp_title, "Control Panel");
    lv_obj_set_style_text_color(cp_title, lv_color_black(), 0);
    lv_obj_set_style_text_font(cp_title, UI_FONT, 0);
    lv_obj_align(cp_title, LV_ALIGN_LEFT_MID, 35, 0);
    
    // Main area with sidebar
    lv_obj_t *main_area = lv_obj_create(content);
    lv_obj_set_size(main_area, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4 - 40);
    lv_obj_align(main_area, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_remove_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left sidebar - Vista style with categories
    lv_obj_t *sidebar = lv_obj_create(main_area);
    lv_obj_set_size(sidebar, 140, lv_pct(100));
    lv_obj_align(sidebar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(sidebar, lv_color_hex(0xD8ECF8), 0);
    lv_obj_set_style_bg_grad_dir(sidebar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 8, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sidebar, 4, 0);
    
    // Sidebar header - Categories
    lv_obj_t *sb_header = lv_label_create(sidebar);
    lv_label_set_text(sb_header, "Categories");
    lv_obj_set_style_text_color(sb_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(sb_header, UI_FONT, 0);
    
    // Separator
    lv_obj_t *sep1 = lv_obj_create(sidebar);
    lv_obj_set_size(sep1, lv_pct(100), 1);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    
    // Category items in sidebar
    create_cp_sidebar_item(sidebar, LV_SYMBOL_SETTINGS " System", [](lv_event_t *e) {
        settings_show_about_page();
    });
    
    create_cp_sidebar_item(sidebar, LV_SYMBOL_WIFI " Network", [](lv_event_t *e) {
        settings_show_wifi_page();
    });
    
    create_cp_sidebar_item(sidebar, LV_SYMBOL_KEYBOARD " Hardware", [](lv_event_t *e) {
        settings_show_keyboard_page();
    });
    
    create_cp_sidebar_item(sidebar, LV_SYMBOL_IMAGE " Personalize", [](lv_event_t *e) {
        settings_show_wallpaper_page();
    });
    
    create_cp_sidebar_item(sidebar, LV_SYMBOL_BELL " Clock", [](lv_event_t *e) {
        settings_show_time_page();
    });
    
    create_cp_sidebar_item(sidebar, LV_SYMBOL_LIST " Apps", [](lv_event_t *e) {
        settings_show_apps_page();
    });
    
    // Separator
    lv_obj_t *sep2 = lv_obj_create(sidebar);
    lv_obj_set_size(sep2, lv_pct(100), 1);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    
    // Quick links header
    lv_obj_t *quick_header = lv_label_create(sidebar);
    lv_label_set_text(quick_header, "Quick Links");
    lv_obj_set_style_text_color(quick_header, lv_color_hex(0x1A5090), 0);
    
    create_cp_sidebar_item(sidebar, "Brightness", [](lv_event_t *e) {
        settings_show_brightness_page();
    });
    
    create_cp_sidebar_item(sidebar, "Storage", [](lv_event_t *e) {
        settings_show_storage_page();
    });
    
    create_cp_sidebar_item(sidebar, "Region", [](lv_event_t *e) {
        settings_show_region_page();
    });
    
    // Main content area - white background with all settings
    lv_obj_t *settings_area = lv_obj_create(main_area);
    lv_obj_set_size(settings_area, 320, lv_pct(100));
    lv_obj_align(settings_area, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(settings_area, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(settings_area, 0, 0);
    lv_obj_set_style_radius(settings_area, 0, 0);
    lv_obj_set_style_pad_all(settings_area, 10, 0);
    lv_obj_set_flex_flow(settings_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_area, 8, 0);
    
    // All Settings header
    lv_obj_t *all_header = lv_label_create(settings_area);
    lv_label_set_text(all_header, "All Settings");
    lv_obj_set_style_text_color(all_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(all_header, UI_FONT, 0);
    
    // All settings items (full list)
    create_cp_main_item(settings_area, "WiFi", "Wireless network connections", &img_network, [](lv_event_t *e) {
        settings_show_wifi_page();
    });
    
    create_cp_main_item(settings_area, "Bluetooth", "Bluetooth devices", &img_ethernet, [](lv_event_t *e) {
        settings_show_bluetooth_page();
    });
    
    create_cp_main_item(settings_area, "Brightness", "Screen brightness", &img_accessibility, [](lv_event_t *e) {
        settings_show_brightness_page();
    });
    
    create_cp_main_item(settings_area, "Keyboard", "Keyboard settings", &img_settings, [](lv_event_t *e) {
        settings_show_keyboard_page();
    });
    
    create_cp_main_item(settings_area, "Personalize", "UI style, wallpaper", &img_personalization, [](lv_event_t *e) {
        settings_show_wallpaper_page();
    });
    
    create_cp_main_item(settings_area, "Date & Time", "Clock and timezone", &img_clock, [](lv_event_t *e) {
        settings_show_time_page();
    });
    
    create_cp_main_item(settings_area, "Region", "Location settings", &img_weather, [](lv_event_t *e) {
        settings_show_region_page();
    });
    
    create_cp_main_item(settings_area, "Storage", "Disk space info", &img_folder, [](lv_event_t *e) {
        settings_show_storage_page();
    });
    
    create_cp_main_item(settings_area, "User", "Profile and password", &img_user, [](lv_event_t *e) {
        settings_show_user_page();
    });
    
    create_cp_main_item(settings_area, "Apps", "Installed applications", &img_folder, [](lv_event_t *e) {
        settings_show_apps_page();
    });
    
    // About always at the bottom
    create_cp_main_item(settings_area, "About", "Device information", &img_my_computer, [](lv_event_t *e) {
        settings_show_about_page();
    });
}

// ============ NOTEPAD APP (Vista Style - like screenshot) ============

// Helper to create Vista menu item
static lv_obj_t* create_notepad_menu_item(lv_obj_t *parent, const char *text)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_height(item, 22);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x91C9F7), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_set_style_pad_left(item, 6, 0);
    lv_obj_set_style_pad_right(item, 6, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, UI_FONT, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    
    return item;
}

void app_notepad_create(void)
{
    ESP_LOGI(TAG, "Opening Notepad");
    create_app_window("Untitled - Notepad");
    
    // Calculate content height
    int content_height = SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4;
    
    // Content area - Vista Aero Glass gradient background
    lv_obj_t *content = lv_obj_create(app_window);
    lv_obj_set_size(content, lv_pct(100), content_height);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue to white
    lv_obj_set_style_bg_color(content, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(content, lv_color_hex(0xF0F6FC), 0);
    lv_obj_set_style_bg_grad_dir(content, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Vista-style Menu bar (File, Edit, Format, View, Help)
    lv_obj_t *menubar = lv_obj_create(content);
    lv_obj_set_size(menubar, lv_pct(100), 22);
    lv_obj_align(menubar, LV_ALIGN_TOP_LEFT, 0, 0);
    // Light gradient for menu bar
    lv_obj_set_style_bg_color(menubar, lv_color_hex(0xF5F9FD), 0);
    lv_obj_set_style_bg_grad_color(menubar, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(menubar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(menubar, lv_color_hex(0xB8D4F0), 0);
    lv_obj_set_style_border_width(menubar, 0, 0);
    lv_obj_set_style_border_side(menubar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(menubar, 0, 0);
    lv_obj_set_style_pad_left(menubar, 5, 0);
    lv_obj_remove_flag(menubar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(menubar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(menubar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(menubar, 0, 0);
    
    // Menu items
    static const char* menu_items[] = {"File", "Edit", "Format", "View", "Help"};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *item = create_notepad_menu_item(menubar, menu_items[i]);
        (void)item;
    }
    
    // Text area container - white with Vista-style border
    int textarea_height = content_height - 22 - 4; // menubar + padding
    
    lv_obj_t *textarea_frame = lv_obj_create(content);
    lv_obj_set_size(textarea_frame, lv_pct(100) - 8, textarea_height);
    lv_obj_align(textarea_frame, LV_ALIGN_TOP_LEFT, 4, 24);
    // Vista-style inset border
    lv_obj_set_style_bg_color(textarea_frame, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(textarea_frame, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(textarea_frame, 1, 0);
    lv_obj_set_style_radius(textarea_frame, 0, 0);
    lv_obj_set_style_pad_all(textarea_frame, 1, 0);
    // Inner shadow effect
    lv_obj_set_style_shadow_width(textarea_frame, 3, 0);
    lv_obj_set_style_shadow_color(textarea_frame, lv_color_hex(0xA0C0E0), 0);
    lv_obj_set_style_shadow_opa(textarea_frame, LV_OPA_30, 0);
    lv_obj_set_style_shadow_spread(textarea_frame, -2, 0);
    lv_obj_remove_flag(textarea_frame, LV_OBJ_FLAG_SCROLLABLE);
    
    // Actual textarea - pure white
    notepad_textarea = lv_textarea_create(textarea_frame);
    lv_obj_set_size(notepad_textarea, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(notepad_textarea, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(notepad_textarea, 0, 0);
    lv_obj_set_style_radius(notepad_textarea, 0, 0);
    lv_obj_set_style_text_color(notepad_textarea, lv_color_black(), 0);
    lv_obj_set_style_text_font(notepad_textarea, UI_FONT, 0);
    lv_obj_set_style_pad_all(notepad_textarea, 4, 0);
    lv_textarea_set_placeholder_text(notepad_textarea, "");
    lv_textarea_set_text(notepad_textarea, "");
    
    // Create keyboard on the active screen
    uint16_t kb_height = settings_get_keyboard_height_px();
    ESP_LOGI(TAG, "Notepad keyboard height from settings: %dpx", kb_height);
    if (kb_height < 136 || kb_height > 700) {
        kb_height = 496;  // Fallback to 62%
    }
    
    lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(kb, SCREEN_WIDTH, kb_height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, notepad_textarea);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    apply_keyboard_theme(kb);  // Apply theme (uses default font for symbols)
    
    // Show keyboard on focus
    lv_obj_add_event_cb(notepad_textarea, [](lv_event_t *e) {
        lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, kb);
    
    lv_obj_add_event_cb(notepad_textarea, [](lv_event_t *e) {
        lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_DEFOCUSED, kb);
}

// ============ CAMERA APP (Aero Glass Style) ============

// Camera preview state
static lv_obj_t *camera_preview_canvas = NULL;
static lv_obj_t *camera_status_label = NULL;
static uint8_t *camera_preview_buf = NULL;
static uint8_t *camera_frame_buf = NULL;  // Double buffer for thread safety
static bool camera_app_active = false;
static volatile bool camera_new_frame = false;  // Flag for new frame available
static lv_timer_t *camera_update_timer = NULL;
static volatile uint32_t camera_frame_count = 0;  // Debug counter

// Camera zoom and resolution state
static int camera_digital_zoom = 100;  // 100 = 1x, 200 = 2x, etc.
static int camera_resolution_idx = 0;  // 0 = full, 1 = medium, 2 = small
static lv_obj_t *camera_zoom_label = NULL;
static lv_obj_t *camera_res_label = NULL;

// Preview dimensions (scaled down from camera resolution)
#define PREVIEW_WIDTH  360
#define PREVIEW_HEIGHT 270  // 4:3 aspect ratio

// Camera frame callback - called from camera stream task (Core 1)
// IMPORTANT: Do NOT call any LVGL functions here! This runs on Core 1.
static void camera_frame_cb(uint8_t *data, uint16_t width, uint16_t height, void *user_data)
{
    if (!camera_app_active || camera_frame_buf == NULL || data == NULL) {
        return;
    }
    
    // Skip if previous frame not yet consumed (avoid overwriting)
    if (camera_new_frame) {
        return;
    }
    
    // Apply digital zoom - crop center of image
    int zoom_factor = camera_digital_zoom;
    int crop_width = (width * 100) / zoom_factor;
    int crop_height = (height * 100) / zoom_factor;
    int crop_x = (width - crop_width) / 2;
    int crop_y = (height - crop_height) / 2;
    
    // Scale and copy frame to frame buffer (not preview buffer - that's for LVGL)
    // Use integer math for speed
    uint32_t scale_x_fixed = (crop_width << 16) / PREVIEW_WIDTH;
    uint32_t scale_y_fixed = (crop_height << 16) / PREVIEW_HEIGHT;
    
    uint16_t *src = (uint16_t*)data;
    uint16_t *dst = (uint16_t*)camera_frame_buf;
    
    for (int y = 0; y < PREVIEW_HEIGHT; y++) {
        uint32_t src_y = crop_y + ((y * scale_y_fixed) >> 16);
        if (src_y >= height) src_y = height - 1;
        uint16_t *src_row = src + src_y * width;
        
        for (int x = 0; x < PREVIEW_WIDTH; x++) {
            uint32_t src_x = crop_x + ((x * scale_x_fixed) >> 16);
            if (src_x >= width) src_x = width - 1;
            
            dst[y * PREVIEW_WIDTH + x] = src_row[src_x];
        }
    }
    
    // Signal that new frame is ready (atomic write)
    camera_frame_count++;
    camera_new_frame = true;
}

// Timer callback to update preview from LVGL thread (Core 0)
// This is the ONLY place where LVGL functions should be called for camera
static void camera_update_timer_cb(lv_timer_t *timer)
{
    if (!camera_app_active) {
        return;
    }
    
    // Check if new frame available
    if (!camera_new_frame) {
        return;
    }
    
    if (camera_preview_buf != NULL && camera_frame_buf != NULL && camera_preview_canvas != NULL) {
        // Copy frame buffer to preview buffer (safe - both on Core 0 context now)
        memcpy(camera_preview_buf, camera_frame_buf, PREVIEW_WIDTH * PREVIEW_HEIGHT * 2);
        
        // Clear flag AFTER copy
        camera_new_frame = false;
        
        // Invalidate canvas to trigger redraw - safe here in LVGL thread
        lv_obj_invalidate(camera_preview_canvas);
    }
}

// Cleanup camera when app closes
static void camera_app_cleanup(void)
{
    ESP_LOGI("CAMERA", "Cleaning up camera app");
    camera_app_active = false;
    
    // Reset zoom and resolution
    camera_digital_zoom = 100;
    camera_resolution_idx = 0;
    camera_zoom_label = NULL;
    camera_res_label = NULL;
    
    // Stop update timer
    if (camera_update_timer != NULL) {
        lv_timer_delete(camera_update_timer);
        camera_update_timer = NULL;
    }
    
    // Stop camera stream
    if (hw_camera_is_streaming()) {
        hw_camera_stop_stream();
    }
    
    // Free buffers
    if (camera_preview_buf != NULL) {
        free(camera_preview_buf);
        camera_preview_buf = NULL;
    }
    if (camera_frame_buf != NULL) {
        free(camera_frame_buf);
        camera_frame_buf = NULL;
    }
    
    camera_preview_canvas = NULL;
    camera_status_label = NULL;
    camera_new_frame = false;
}

void app_camera_create(void)
{
    ESP_LOGI(TAG, "Opening Camera");
    create_app_window("Camera");
    
    // Content area - dark background for camera (fullscreen-like)
    lv_obj_t *content = lv_obj_create(app_window);
    lv_obj_set_size(content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add cleanup on window delete
    lv_obj_add_event_cb(app_window, [](lv_event_t *e) {
        camera_app_cleanup();
    }, LV_EVENT_DELETE, NULL);
    
    // Viewfinder - simple frame
    lv_obj_t *viewfinder_frame = lv_obj_create(content);
    lv_obj_set_size(viewfinder_frame, PREVIEW_WIDTH + 8, PREVIEW_HEIGHT + 8);
    lv_obj_align(viewfinder_frame, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(viewfinder_frame, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_color(viewfinder_frame, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_width(viewfinder_frame, 2, 0);
    lv_obj_set_style_radius(viewfinder_frame, 4, 0);
    lv_obj_set_style_pad_all(viewfinder_frame, 2, 0);
    lv_obj_remove_flag(viewfinder_frame, LV_OBJ_FLAG_SCROLLABLE);
    
    // Allocate buffers (RGB565 = 2 bytes per pixel)
    size_t buf_size = PREVIEW_WIDTH * PREVIEW_HEIGHT * 2;
    
    camera_preview_buf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    camera_frame_buf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (camera_preview_buf == NULL || camera_frame_buf == NULL) {
        ESP_LOGE("CAMERA", "Failed to allocate buffers in PSRAM, trying internal");
        if (camera_preview_buf == NULL) camera_preview_buf = (uint8_t*)malloc(buf_size);
        if (camera_frame_buf == NULL) camera_frame_buf = (uint8_t*)malloc(buf_size);
    }
    
    if (camera_preview_buf != NULL && camera_frame_buf != NULL) {
        // Clear buffers to dark color
        memset(camera_preview_buf, 0x00, buf_size);
        memset(camera_frame_buf, 0x00, buf_size);
        
        // Create canvas for camera preview
        camera_preview_canvas = lv_canvas_create(viewfinder_frame);
        lv_canvas_set_buffer(camera_preview_canvas, camera_preview_buf, PREVIEW_WIDTH, PREVIEW_HEIGHT, LV_COLOR_FORMAT_RGB565);
        lv_obj_center(camera_preview_canvas);
    }
    
    // Status text (shown when camera not streaming)
    camera_status_label = lv_label_create(viewfinder_frame);
    lv_label_set_text(camera_status_label, "Starting camera...");
    lv_obj_set_style_text_color(camera_status_label, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(camera_status_label, UI_FONT, 0);
    lv_obj_center(camera_status_label);
    
    // Initialize camera and start streaming
    camera_app_active = true;
    camera_new_frame = false;
    
    if (!hw_camera_is_ready()) {
        lv_label_set_text(camera_status_label, "Initializing...");
        esp_err_t ret = hw_camera_init();
        if (ret != ESP_OK) {
            lv_label_set_text(camera_status_label, "Camera init failed");
            ESP_LOGE("CAMERA", "Failed to initialize camera");
        }
    }
    
    if (hw_camera_is_ready()) {
        // Hide status label when streaming
        lv_obj_add_flag(camera_status_label, LV_OBJ_FLAG_HIDDEN);
        
        // Create update timer (runs in LVGL thread)
        camera_update_timer = lv_timer_create(camera_update_timer_cb, 50, NULL);  // 20 FPS max
        
        // Start streaming
        esp_err_t ret = hw_camera_start_stream(camera_frame_cb, NULL);
        if (ret != ESP_OK) {
            lv_obj_remove_flag(camera_status_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(camera_status_label, "Stream failed");
            ESP_LOGE("CAMERA", "Failed to start camera stream");
            if (camera_update_timer) {
                lv_timer_delete(camera_update_timer);
                camera_update_timer = NULL;
            }
        } else {
            ESP_LOGI("CAMERA", "Camera streaming started");
        }
    }
    
    // Aero Glass Controls panel
    lv_obj_t *controls = lv_obj_create(content);
    lv_obj_set_size(controls, 440, 140);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(controls, lv_color_hex(0x2050A0), 0);
    lv_obj_set_style_bg_grad_color(controls, lv_color_hex(0x102040), 0);
    lv_obj_set_style_bg_grad_dir(controls, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(controls, LV_OPA_60, 0);
    lv_obj_set_style_border_color(controls, lv_color_hex(0x4080C0), 0);
    lv_obj_set_style_border_width(controls, 1, 0);
    lv_obj_set_style_border_opa(controls, LV_OPA_50, 0);
    lv_obj_set_style_radius(controls, 15, 0);
    lv_obj_set_style_shadow_width(controls, 20, 0);
    lv_obj_set_style_shadow_color(controls, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(controls, LV_OPA_40, 0);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    
    // Capture button (big circle with glow)
    lv_obj_t *capture_glow = lv_obj_create(controls);
    lv_obj_set_size(capture_glow, 90, 90);
    lv_obj_align(capture_glow, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(capture_glow, lv_color_hex(0x60A0E0), 0);
    lv_obj_set_style_bg_opa(capture_glow, LV_OPA_20, 0);
    lv_obj_set_style_border_width(capture_glow, 0, 0);
    lv_obj_set_style_radius(capture_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(capture_glow, 25, 0);
    lv_obj_set_style_shadow_color(capture_glow, lv_color_hex(0x4090E0), 0);
    lv_obj_set_style_shadow_opa(capture_glow, LV_OPA_50, 0);
    lv_obj_remove_flag(capture_glow, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *capture_btn = lv_obj_create(capture_glow);
    lv_obj_set_size(capture_btn, 70, 70);
    lv_obj_center(capture_btn);
    lv_obj_set_style_bg_color(capture_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_color(capture_btn, lv_color_hex(0xE0E8F0), 0);
    lv_obj_set_style_bg_grad_dir(capture_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(capture_btn, lv_color_hex(0x80C0FF), 0);
    lv_obj_set_style_border_width(capture_btn, 3, 0);
    lv_obj_set_style_radius(capture_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(capture_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(capture_btn, lv_color_hex(0xD0D8E0), LV_STATE_PRESSED);
    lv_obj_remove_flag(capture_btn, LV_OBJ_FLAG_SCROLLABLE);
    
    // Capture button click handler - saves as BMP (LVGL can display it)
    lv_obj_add_event_cb(capture_btn, [](lv_event_t *e) {
        ESP_LOGI("CAMERA", "Capture button clicked!");
        
        if (!hw_camera_is_ready()) {
            ESP_LOGW("CAMERA", "Camera not ready");
            return;
        }
        
        // Capture current frame from buffer (don't stop streaming)
        if (camera_frame_buf != NULL) {
            // Ensure photos directory exists
            struct stat st;
            if (stat("/littlefs/photos", &st) != 0) {
                mkdir("/littlefs/photos", 0755);
                ESP_LOGI("CAMERA", "Created /littlefs/photos directory");
            }
            
            // Generate filename with timestamp
            time_t now;
            time(&now);
            char filename[64];
            snprintf(filename, sizeof(filename), "/littlefs/photos/IMG_%ld.bmp", (long)now);
            
            // Save as BMP file (LVGL can display BMP natively)
            // BMP requires row padding to 4-byte boundary
            int row_size = PREVIEW_WIDTH * 3;
            int padding = (4 - (row_size % 4)) % 4;
            int padded_row_size = row_size + padding;
            uint32_t image_size = padded_row_size * PREVIEW_HEIGHT;
            
            FILE *f = fopen(filename, "wb");
            if (f) {
                // BMP Header (54 bytes)
                uint32_t file_size = 54 + image_size;
                uint32_t data_offset = 54;
                uint32_t header_size = 40;
                uint16_t planes = 1;
                uint16_t bpp = 24;  // 24-bit RGB
                uint32_t compression = 0;
                
                // BMP File Header
                fputc('B', f); fputc('M', f);
                fwrite(&file_size, 4, 1, f);
                uint32_t reserved = 0;
                fwrite(&reserved, 4, 1, f);
                fwrite(&data_offset, 4, 1, f);
                
                // DIB Header (BITMAPINFOHEADER)
                fwrite(&header_size, 4, 1, f);
                int32_t width = PREVIEW_WIDTH;
                int32_t height = PREVIEW_HEIGHT;  // Positive = bottom-up (standard BMP)
                fwrite(&width, 4, 1, f);
                fwrite(&height, 4, 1, f);
                fwrite(&planes, 2, 1, f);
                fwrite(&bpp, 2, 1, f);
                fwrite(&compression, 4, 1, f);
                fwrite(&image_size, 4, 1, f);
                uint32_t ppm = 2835;  // 72 DPI
                fwrite(&ppm, 4, 1, f);
                fwrite(&ppm, 4, 1, f);
                uint32_t colors = 0;
                fwrite(&colors, 4, 1, f);
                fwrite(&colors, 4, 1, f);
                
                // Pixel data - convert RGB565 to RGB888 (bottom-up order)
                uint16_t *src = (uint16_t*)camera_frame_buf;
                uint8_t pad_bytes[3] = {0, 0, 0};
                for (int y = PREVIEW_HEIGHT - 1; y >= 0; y--) {  // Bottom to top
                    for (int x = 0; x < PREVIEW_WIDTH; x++) {
                        uint16_t pixel = src[y * PREVIEW_WIDTH + x];
                        // RGB565 to RGB888
                        uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                        uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                        uint8_t b = (pixel & 0x1F) << 3;
                        // BMP stores BGR
                        fputc(b, f);
                        fputc(g, f);
                        fputc(r, f);
                    }
                    // Write padding bytes
                    if (padding > 0) {
                        fwrite(pad_bytes, 1, padding, f);
                    }
                }
                fclose(f);
                ESP_LOGI("CAMERA", "Photo saved: %s", filename);
                
                // Show toast notification
                show_notification("Photo saved!", 2000);
            } else {
                ESP_LOGE("CAMERA", "Failed to save photo");
            }
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Inner circle
    lv_obj_t *inner = lv_obj_create(capture_btn);
    lv_obj_set_size(inner, 50, 50);
    lv_obj_center(inner);
    lv_obj_set_style_bg_color(inner, lv_color_white(), 0);
    lv_obj_set_style_border_width(inner, 0, 0);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    
    // Gallery button - Aero Glass style
    lv_obj_t *gallery_btn = lv_obj_create(controls);
    lv_obj_set_size(gallery_btn, 55, 55);
    lv_obj_align(gallery_btn, LV_ALIGN_LEFT_MID, 35, 0);
    lv_obj_set_style_bg_color(gallery_btn, lv_color_hex(0x3060A0), 0);
    lv_obj_set_style_bg_grad_color(gallery_btn, lv_color_hex(0x204070), 0);
    lv_obj_set_style_bg_grad_dir(gallery_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(gallery_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(gallery_btn, lv_color_hex(0x6090C0), 0);
    lv_obj_set_style_border_width(gallery_btn, 1, 0);
    lv_obj_set_style_border_opa(gallery_btn, LV_OPA_60, 0);
    lv_obj_set_style_radius(gallery_btn, 10, 0);
    lv_obj_add_flag(gallery_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(gallery_btn, lv_color_hex(0x4080C0), LV_STATE_PRESSED);
    lv_obj_remove_flag(gallery_btn, LV_OBJ_FLAG_SCROLLABLE);
    
    // Gallery button click handler - open Photo Viewer
    lv_obj_add_event_cb(gallery_btn, [](lv_event_t *e) {
        ESP_LOGI("CAMERA", "Gallery button clicked - opening Photo Viewer");
        app_launch("photos");
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *gallery_icon = lv_image_create(gallery_btn);
    lv_image_set_src(gallery_icon, &img_photoview);
    lv_image_set_scale(gallery_icon, 180);  // Scale to ~35px
    lv_obj_center(gallery_icon);
    lv_obj_remove_flag(gallery_icon, LV_OBJ_FLAG_CLICKABLE);
    
    // Switch camera button - Aero Glass style
    lv_obj_t *switch_btn = lv_obj_create(controls);
    lv_obj_set_size(switch_btn, 55, 55);
    lv_obj_align(switch_btn, LV_ALIGN_RIGHT_MID, -35, 0);
    lv_obj_set_style_bg_color(switch_btn, lv_color_hex(0x3060A0), 0);
    lv_obj_set_style_bg_grad_color(switch_btn, lv_color_hex(0x204070), 0);
    lv_obj_set_style_bg_grad_dir(switch_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(switch_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(switch_btn, lv_color_hex(0x6090C0), 0);
    lv_obj_set_style_border_width(switch_btn, 1, 0);
    lv_obj_set_style_border_opa(switch_btn, LV_OPA_60, 0);
    lv_obj_set_style_radius(switch_btn, 10, 0);
    lv_obj_add_flag(switch_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(switch_btn, lv_color_hex(0x4080C0), LV_STATE_PRESSED);
    lv_obj_remove_flag(switch_btn, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *switch_label = lv_label_create(switch_btn);
    lv_label_set_text(switch_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(switch_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(switch_label, UI_FONT, 0);
    lv_obj_center(switch_label);
    lv_obj_remove_flag(switch_label, LV_OBJ_FLAG_CLICKABLE);
    
    // Top info bar - zoom and resolution display
    lv_obj_t *info_bar = lv_obj_create(content);
    lv_obj_set_size(info_bar, 440, 35);
    lv_obj_align(info_bar, LV_ALIGN_TOP_MID, 0, PREVIEW_HEIGHT + 25);
    lv_obj_set_style_bg_color(info_bar, lv_color_hex(0x1A2A4A), 0);
    lv_obj_set_style_bg_opa(info_bar, LV_OPA_70, 0);
    lv_obj_set_style_border_width(info_bar, 0, 0);
    lv_obj_set_style_radius(info_bar, 8, 0);
    lv_obj_set_flex_flow(info_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(info_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Zoom out button
    lv_obj_t *zoom_out_btn = lv_btn_create(info_bar);
    lv_obj_set_size(zoom_out_btn, 40, 28);
    lv_obj_set_style_bg_color(zoom_out_btn, lv_color_hex(0x3060A0), 0);
    lv_obj_set_style_radius(zoom_out_btn, 5, 0);
    lv_obj_t *zoom_out_lbl = lv_label_create(zoom_out_btn);
    lv_label_set_text(zoom_out_lbl, "-");
    lv_obj_set_style_text_color(zoom_out_lbl, lv_color_white(), 0);
    lv_obj_center(zoom_out_lbl);
    lv_obj_add_event_cb(zoom_out_btn, [](lv_event_t *e) {
        if (camera_digital_zoom > 100) {
            camera_digital_zoom -= 25;
            if (camera_zoom_label) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1fx", camera_digital_zoom / 100.0);
                lv_label_set_text(camera_zoom_label, buf);
            }
            ESP_LOGI("CAMERA", "Zoom: %d%%", camera_digital_zoom);
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Zoom label
    camera_zoom_label = lv_label_create(info_bar);
    lv_label_set_text(camera_zoom_label, "1.0x");
    lv_obj_set_style_text_color(camera_zoom_label, lv_color_white(), 0);
    
    // Zoom in button
    lv_obj_t *zoom_in_btn = lv_btn_create(info_bar);
    lv_obj_set_size(zoom_in_btn, 40, 28);
    lv_obj_set_style_bg_color(zoom_in_btn, lv_color_hex(0x3060A0), 0);
    lv_obj_set_style_radius(zoom_in_btn, 5, 0);
    lv_obj_t *zoom_in_lbl = lv_label_create(zoom_in_btn);
    lv_label_set_text(zoom_in_lbl, "+");
    lv_obj_set_style_text_color(zoom_in_lbl, lv_color_white(), 0);
    lv_obj_center(zoom_in_lbl);
    lv_obj_add_event_cb(zoom_in_btn, [](lv_event_t *e) {
        if (camera_digital_zoom < 400) {
            camera_digital_zoom += 25;
            if (camera_zoom_label) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1fx", camera_digital_zoom / 100.0);
                lv_label_set_text(camera_zoom_label, buf);
            }
            ESP_LOGI("CAMERA", "Zoom: %d%%", camera_digital_zoom);
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Separator
    lv_obj_t *sep = lv_obj_create(info_bar);
    lv_obj_set_size(sep, 2, 20);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x4080C0), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 1, 0);
    
    // Resolution label
    static const char* res_names[] = {"Full", "Med", "Low"};
    camera_res_label = lv_label_create(info_bar);
    lv_label_set_text(camera_res_label, res_names[camera_resolution_idx]);
    lv_obj_set_style_text_color(camera_res_label, lv_color_white(), 0);
    
    // Resolution cycle button
    lv_obj_t *res_btn = lv_btn_create(info_bar);
    lv_obj_set_size(res_btn, 50, 28);
    lv_obj_set_style_bg_color(res_btn, lv_color_hex(0x508050), 0);
    lv_obj_set_style_radius(res_btn, 5, 0);
    lv_obj_t *res_btn_lbl = lv_label_create(res_btn);
    lv_label_set_text(res_btn_lbl, "Res");
    lv_obj_set_style_text_color(res_btn_lbl, lv_color_white(), 0);
    lv_obj_center(res_btn_lbl);
    lv_obj_add_event_cb(res_btn, [](lv_event_t *e) {
        camera_resolution_idx = (camera_resolution_idx + 1) % 3;
        static const char* res_names[] = {"Full", "Med", "Low"};
        if (camera_res_label) {
            lv_label_set_text(camera_res_label, res_names[camera_resolution_idx]);
        }
        ESP_LOGI("CAMERA", "Resolution: %s", res_names[camera_resolution_idx]);
        // Note: Actual resolution change would require camera reconfiguration
        // For now this just changes the label - full implementation would need hw_camera_set_resolution()
    }, LV_EVENT_CLICKED, NULL);
}

// ============ MY COMPUTER APP (File Browser) ============

static void mycomp_show_root(void);
static void mycomp_browse_path(const char *path);

static void mycomp_back_clicked(lv_event_t *e)
{
    if (strlen(mycomp_current_path) == 0) {
        return;  // Already at root
    }
    
    // Go up one level
    char *last_slash = strrchr(mycomp_current_path, '/');
    if (last_slash && last_slash != mycomp_current_path) {
        *last_slash = '\0';
        mycomp_browse_path(mycomp_current_path);
    } else {
        // Back to root
        mycomp_current_path[0] = '\0';
        mycomp_show_root();
    }
}

// Create folder/file dialog
static lv_obj_t *create_item_dialog = NULL;
static lv_obj_t *create_item_textarea = NULL;
static bool create_item_is_folder = true;

static void create_item_ok_cb(lv_event_t *e)
{
    if (!create_item_textarea || !create_item_dialog) return;
    
    const char *name = lv_textarea_get_text(create_item_textarea);
    if (!name || strlen(name) == 0) {
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(create_item_dialog);
        if (kb) lv_obj_delete(kb);
        lv_obj_delete(create_item_dialog);
        create_item_dialog = NULL;
        return;
    }
    
    // Build full path
    char new_path[384];
    snprintf(new_path, sizeof(new_path), "%s/%s", mycomp_current_path, name);
    
    int result = -1;
    if (create_item_is_folder) {
        result = mkdir(new_path, 0755);
        if (result == 0) {
            ESP_LOGI(TAG, "Created folder: %s", new_path);
        } else {
            ESP_LOGE(TAG, "Failed to create folder: %s (errno=%d)", new_path, errno);
        }
    } else {
        // Create empty file
        FILE *f = fopen(new_path, "w");
        if (f) {
            fclose(f);
            result = 0;
            ESP_LOGI(TAG, "Created file: %s", new_path);
        } else {
            ESP_LOGE(TAG, "Failed to create file: %s (errno=%d)", new_path, errno);
        }
    }
    
    // Refresh view
    if (result == 0 && strlen(mycomp_current_path) > 0) {
        mycomp_browse_path(mycomp_current_path);
    }
    
    lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(create_item_dialog);
    if (kb) lv_obj_delete(kb);
    lv_obj_delete(create_item_dialog);
    create_item_dialog = NULL;
    create_item_textarea = NULL;
}

static void create_item_cancel_cb(lv_event_t *e)
{
    if (create_item_dialog) {
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(create_item_dialog);
        if (kb) lv_obj_delete(kb);
        lv_obj_delete(create_item_dialog);
        create_item_dialog = NULL;
        create_item_textarea = NULL;
    }
}

static void show_create_item_dialog(bool is_folder)
{
    create_item_is_folder = is_folder;
    
    create_item_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(create_item_dialog, 380, 200);
    lv_obj_center(create_item_dialog);
    lv_obj_set_style_bg_color(create_item_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(create_item_dialog, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(create_item_dialog, 2, 0);
    lv_obj_set_style_radius(create_item_dialog, 8, 0);
    lv_obj_set_style_pad_all(create_item_dialog, 15, 0);
    lv_obj_remove_flag(create_item_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(create_item_dialog);
    lv_label_set_text(title, is_folder ? "New Folder" : "New File");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *name_lbl = lv_label_create(create_item_dialog);
    lv_label_set_text(name_lbl, "Name:");
    lv_obj_set_style_text_color(name_lbl, lv_color_black(), 0);
    lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 0, 35);
    
    create_item_textarea = lv_textarea_create(create_item_dialog);
    lv_obj_set_size(create_item_textarea, 340, 40);
    lv_obj_align(create_item_textarea, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_one_line(create_item_textarea, true);
    lv_textarea_set_placeholder_text(create_item_textarea, is_folder ? "Folder name" : "File name.txt");
    lv_obj_set_style_border_color(create_item_textarea, lv_color_hex(0x7EB4EA), 0);
    
    // Create keyboard with settings (size and theme)
    uint16_t kb_height = settings_get_keyboard_height_px();
    if (kb_height < 136 || kb_height > 700) {
        kb_height = 496;  // Fallback to 62%
    }
    
    lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(kb, SCREEN_WIDTH, kb_height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, create_item_textarea);
    lv_obj_set_user_data(create_item_dialog, kb);
    apply_keyboard_theme(kb);  // Apply system keyboard theme (color)
    
    lv_obj_t *ok_btn = lv_btn_create(create_item_dialog);
    lv_obj_set_size(ok_btn, 100, 40);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(ok_btn, 6, 0);
    lv_obj_add_event_cb(ok_btn, create_item_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "Create");
    lv_obj_set_style_text_color(ok_lbl, lv_color_white(), 0);
    lv_obj_center(ok_lbl);
    
    lv_obj_t *cancel_btn = lv_btn_create(create_item_dialog);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_add_event_cb(cancel_btn, create_item_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
    lv_obj_center(cancel_lbl);
}

static void mycomp_new_folder_cb(lv_event_t *e)
{
    show_create_item_dialog(true);
}

static void mycomp_new_file_cb(lv_event_t *e)
{
    show_create_item_dialog(false);
}

// "Open With" dialog state
static lv_obj_t *open_with_dialog = NULL;
static char pending_file_path[384] = {0};

static void open_with_dialog_close(void)
{
    if (open_with_dialog) {
        lv_obj_delete(open_with_dialog);
        open_with_dialog = NULL;
    }
}

static void open_with_notepad_clicked(lv_event_t *e)
{
    open_with_dialog_close();
    
    // Open file in notepad
    ESP_LOGI(TAG, "Opening in Notepad: %s", pending_file_path);
    
    // Read file content
    FILE *f = fopen(pending_file_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", pending_file_path);
        return;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Limit to 4KB for notepad
    if (size > 4096) size = 4096;
    
    char *content = (char *)malloc(size + 1);
    if (!content) {
        fclose(f);
        return;
    }
    
    size_t read_size = fread(content, 1, size, f);
    content[read_size] = '\0';
    fclose(f);
    
    // Create notepad with file content
    app_notepad_create();
    
    // Set content to textarea (notepad_textarea is file-scope static)
    if (notepad_textarea) {
        lv_textarea_set_text(notepad_textarea, content);
    }
    
    free(content);
}

static void open_with_info_clicked(lv_event_t *e)
{
    open_with_dialog_close();
    
    // Show file info dialog
    struct stat st;
    if (stat(pending_file_path, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat file: %s", pending_file_path);
        return;
    }
    
    // Create info dialog
    lv_obj_t *info_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(info_dialog, 400, 300);
    lv_obj_center(info_dialog);
    lv_obj_set_style_bg_color(info_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(info_dialog, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(info_dialog, 2, 0);
    lv_obj_set_style_radius(info_dialog, 8, 0);
    lv_obj_set_style_pad_all(info_dialog, 15, 0);
    lv_obj_remove_flag(info_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(info_dialog);
    lv_label_set_text(title, "File Properties");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    // File name
    const char *filename = strrchr(pending_file_path, '/');
    filename = filename ? filename + 1 : pending_file_path;
    
    lv_obj_t *name_lbl = lv_label_create(info_dialog);
    char name_buf[400];
    snprintf(name_buf, sizeof(name_buf), "Name: %.380s", filename);
    lv_label_set_text(name_lbl, name_buf);
    lv_obj_set_style_text_color(name_lbl, lv_color_black(), 0);
    lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 0, 40);
    
    // Path
    lv_obj_t *path_lbl = lv_label_create(info_dialog);
    char path_buf[400];
    snprintf(path_buf, sizeof(path_buf), "Path: %.380s", pending_file_path);
    lv_label_set_text(path_lbl, path_buf);
    lv_obj_set_style_text_color(path_lbl, lv_color_hex(0x666666), 0);
    lv_obj_set_width(path_lbl, 360);
    lv_label_set_long_mode(path_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(path_lbl, LV_ALIGN_TOP_LEFT, 0, 70);
    
    // Size
    lv_obj_t *size_lbl = lv_label_create(info_dialog);
    char size_buf[64];
    if (st.st_size < 1024) {
        snprintf(size_buf, sizeof(size_buf), "Size: %ld bytes", (long)st.st_size);
    } else if (st.st_size < 1024 * 1024) {
        snprintf(size_buf, sizeof(size_buf), "Size: %.1f KB", st.st_size / 1024.0);
    } else {
        snprintf(size_buf, sizeof(size_buf), "Size: %.1f MB", st.st_size / (1024.0 * 1024.0));
    }
    lv_label_set_text(size_lbl, size_buf);
    lv_obj_set_style_text_color(size_lbl, lv_color_black(), 0);
    lv_obj_align(size_lbl, LV_ALIGN_TOP_LEFT, 0, 130);
    
    // Type
    const char *ext = strrchr(filename, '.');
    const char *type_str = "Unknown";
    if (ext) {
        if (strcasecmp(ext, ".txt") == 0) type_str = "Text Document";
        else if (strcasecmp(ext, ".cfg") == 0 || strcasecmp(ext, ".ini") == 0) type_str = "Configuration File";
        else if (strcasecmp(ext, ".log") == 0) type_str = "Log File";
        else if (strcasecmp(ext, ".json") == 0) type_str = "JSON File";
        else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) type_str = "JPEG Image";
        else if (strcasecmp(ext, ".png") == 0) type_str = "PNG Image";
        else if (strcasecmp(ext, ".bmp") == 0) type_str = "Bitmap Image";
        else if (strcasecmp(ext, ".mp3") == 0) type_str = "MP3 Audio";
        else if (strcasecmp(ext, ".wav") == 0) type_str = "WAV Audio";
    }
    
    lv_obj_t *type_lbl = lv_label_create(info_dialog);
    char type_buf[64];
    snprintf(type_buf, sizeof(type_buf), "Type: %s", type_str);
    lv_label_set_text(type_lbl, type_buf);
    lv_obj_set_style_text_color(type_lbl, lv_color_black(), 0);
    lv_obj_align(type_lbl, LV_ALIGN_TOP_LEFT, 0, 160);
    
    // OK button
    lv_obj_t *ok_btn = lv_btn_create(info_dialog);
    lv_obj_set_size(ok_btn, 100, 40);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(ok_btn, 6, 0);
    lv_obj_add_event_cb(ok_btn, [](lv_event_t *e) {
        lv_obj_t *dialog = lv_obj_get_parent((lv_obj_t *)lv_event_get_target(e));
        lv_obj_delete(dialog);
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "OK");
    lv_obj_set_style_text_color(ok_lbl, lv_color_white(), 0);
    lv_obj_center(ok_lbl);
}

// ============ RECYCLE BIN (TRASH) ============
#define TRASH_PATH "/littlefs/.trash"

static void ensure_trash_exists(void)
{
    struct stat st;
    if (stat(TRASH_PATH, &st) != 0) {
        mkdir(TRASH_PATH, 0755);
        ESP_LOGI(TAG, "Created trash folder: %s", TRASH_PATH);
    }
}

static int count_trash_items(void)
{
    DIR *dir = opendir(TRASH_PATH);
    if (!dir) return 0;
    
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

static bool move_to_trash(const char *filepath)
{
    ensure_trash_exists();
    
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    // Limit filename to prevent buffer overflow
    char safe_filename[128];
    strncpy(safe_filename, filename, sizeof(safe_filename) - 1);
    safe_filename[sizeof(safe_filename) - 1] = '\0';
    
    char trash_path[256];
    snprintf(trash_path, sizeof(trash_path), "%s/%.100s", TRASH_PATH, safe_filename);
    
    // If file exists in trash, add timestamp
    struct stat st;
    if (stat(trash_path, &st) == 0) {
        time_t now;
        time(&now);
        snprintf(trash_path, sizeof(trash_path), "%s/%ld_%.80s", TRASH_PATH, (long)now, safe_filename);
    }
    
    int result = rename(filepath, trash_path);
    if (result == 0) {
        ESP_LOGI(TAG, "Moved to trash: %s", safe_filename);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to move to trash: %s (errno=%d)", safe_filename, errno);
        return false;
    }
}

static void empty_trash(void)
{
    DIR *dir = opendir(TRASH_PATH);
    if (!dir) return;
    
    struct dirent *entry;
    char path[384];
    int deleted = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", TRASH_PATH, entry->d_name);
        if (remove(path) == 0) {
            deleted++;
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Emptied trash: %d files deleted", deleted);
}

// ============ FILE OPERATIONS ============

// Delete confirmation dialog
static lv_obj_t *delete_confirm_dialog = NULL;

static void delete_confirm_yes_cb(lv_event_t *e)
{
    if (delete_confirm_dialog) {
        lv_obj_delete(delete_confirm_dialog);
        delete_confirm_dialog = NULL;
    }
    
    // Move to trash instead of permanent delete
    if (move_to_trash(pending_file_path)) {
        // Refresh current directory
        if (strlen(mycomp_current_path) > 0) {
            mycomp_browse_path(mycomp_current_path);
        } else {
            mycomp_show_root();
        }
    }
}

static void delete_confirm_no_cb(lv_event_t *e)
{
    if (delete_confirm_dialog) {
        lv_obj_delete(delete_confirm_dialog);
        delete_confirm_dialog = NULL;
    }
}

static void show_delete_confirm(const char *filepath)
{
    strncpy(pending_file_path, filepath, sizeof(pending_file_path) - 1);
    pending_file_path[sizeof(pending_file_path) - 1] = '\0';
    
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    delete_confirm_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(delete_confirm_dialog, 350, 180);
    lv_obj_center(delete_confirm_dialog);
    lv_obj_set_style_bg_color(delete_confirm_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(delete_confirm_dialog, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(delete_confirm_dialog, 2, 0);
    lv_obj_set_style_radius(delete_confirm_dialog, 8, 0);
    lv_obj_set_style_pad_all(delete_confirm_dialog, 15, 0);
    lv_obj_remove_flag(delete_confirm_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(delete_confirm_dialog);
    lv_label_set_text(title, "Move to Recycle Bin?");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *msg = lv_label_create(delete_confirm_dialog);
    char msg_buf[200];
    snprintf(msg_buf, sizeof(msg_buf), "Move \"%.100s\" to Recycle Bin?", filename);
    lv_label_set_text(msg, msg_buf);
    lv_obj_set_style_text_color(msg, lv_color_black(), 0);
    lv_obj_set_width(msg, 300);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 40);
    
    lv_obj_t *yes_btn = lv_btn_create(delete_confirm_dialog);
    lv_obj_set_size(yes_btn, 100, 40);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_set_style_bg_color(yes_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(yes_btn, 6, 0);
    lv_obj_add_event_cb(yes_btn, delete_confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yes_lbl = lv_label_create(yes_btn);
    lv_label_set_text(yes_lbl, "Yes");
    lv_obj_set_style_text_color(yes_lbl, lv_color_white(), 0);
    lv_obj_center(yes_lbl);
    
    lv_obj_t *no_btn = lv_btn_create(delete_confirm_dialog);
    lv_obj_set_size(no_btn, 100, 40);
    lv_obj_align(no_btn, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_set_style_bg_color(no_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(no_btn, 6, 0);
    lv_obj_add_event_cb(no_btn, delete_confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *no_lbl = lv_label_create(no_btn);
    lv_label_set_text(no_lbl, "No");
    lv_obj_set_style_text_color(no_lbl, lv_color_white(), 0);
    lv_obj_center(no_lbl);
}

// Rename dialog
static lv_obj_t *rename_dialog = NULL;
static lv_obj_t *rename_textarea = NULL;

static void rename_ok_cb(lv_event_t *e)
{
    if (!rename_textarea || !rename_dialog) return;
    
    const char *new_name = lv_textarea_get_text(rename_textarea);
    if (!new_name || strlen(new_name) == 0) {
        // Delete keyboard first
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(rename_dialog);
        if (kb) lv_obj_delete(kb);
        lv_obj_delete(rename_dialog);
        rename_dialog = NULL;
        return;
    }
    
    // Build new path
    char new_path[384];
    char *last_slash = strrchr(pending_file_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - pending_file_path + 1;
        strncpy(new_path, pending_file_path, dir_len);
        new_path[dir_len] = '\0';
        strncat(new_path, new_name, sizeof(new_path) - dir_len - 1);
    } else {
        strncpy(new_path, new_name, sizeof(new_path) - 1);
    }
    
    int result = rename(pending_file_path, new_path);
    if (result == 0) {
        ESP_LOGI(TAG, "Renamed: %s -> %s", pending_file_path, new_path);
        if (strlen(mycomp_current_path) > 0) {
            mycomp_browse_path(mycomp_current_path);
        }
    } else {
        ESP_LOGE(TAG, "Failed to rename: %s (errno=%d)", pending_file_path, errno);
    }
    
    // Delete keyboard first
    lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(rename_dialog);
    if (kb) lv_obj_delete(kb);
    lv_obj_delete(rename_dialog);
    rename_dialog = NULL;
    rename_textarea = NULL;
}

static void rename_cancel_cb(lv_event_t *e)
{
    if (rename_dialog) {
        // Delete keyboard first
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(rename_dialog);
        if (kb) lv_obj_delete(kb);
        lv_obj_delete(rename_dialog);
        rename_dialog = NULL;
        rename_textarea = NULL;
    }
}

static void show_rename_dialog(const char *filepath)
{
    strncpy(pending_file_path, filepath, sizeof(pending_file_path) - 1);
    pending_file_path[sizeof(pending_file_path) - 1] = '\0';
    
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    rename_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(rename_dialog, 380, 180);
    lv_obj_align(rename_dialog, LV_ALIGN_TOP_MID, 0, 50);  // Position at top to leave room for keyboard
    lv_obj_set_style_bg_color(rename_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(rename_dialog, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(rename_dialog, 2, 0);
    lv_obj_set_style_radius(rename_dialog, 8, 0);
    lv_obj_set_style_pad_all(rename_dialog, 15, 0);
    lv_obj_remove_flag(rename_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(rename_dialog);
    lv_label_set_text(title, "Rename");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *lbl = lv_label_create(rename_dialog);
    lv_label_set_text(lbl, "New name:");
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 30);
    
    rename_textarea = lv_textarea_create(rename_dialog);
    lv_obj_set_size(rename_textarea, 340, 40);
    lv_obj_align(rename_textarea, LV_ALIGN_TOP_MID, 0, 55);
    lv_textarea_set_one_line(rename_textarea, true);
    lv_textarea_set_text(rename_textarea, filename);
    
    lv_obj_t *ok_btn = lv_btn_create(rename_dialog);
    lv_obj_set_size(ok_btn, 100, 40);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_LEFT, 30, 0);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(ok_btn, 6, 0);
    lv_obj_add_event_cb(ok_btn, rename_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, "OK");
    lv_obj_set_style_text_color(ok_lbl, lv_color_white(), 0);
    lv_obj_center(ok_lbl);
    
    lv_obj_t *cancel_btn = lv_btn_create(rename_dialog);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -30, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_add_event_cb(cancel_btn, rename_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
    lv_obj_center(cancel_lbl);
    
    // Create keyboard for text input
    int kb_height = 280;
    lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(kb, SCREEN_WIDTH, kb_height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, rename_textarea);
    apply_keyboard_theme(kb);  // Apply theme
    
    // Store keyboard reference for cleanup
    lv_obj_set_user_data(rename_dialog, kb);
}

static void show_open_with_dialog(const char *filepath)
{
    // Store path
    strncpy(pending_file_path, filepath, sizeof(pending_file_path) - 1);
    pending_file_path[sizeof(pending_file_path) - 1] = '\0';
    
    // Close existing dialog
    open_with_dialog_close();
    
    // Get filename
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    // Create dialog
    open_with_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(open_with_dialog, 380, 350);
    lv_obj_center(open_with_dialog);
    lv_obj_set_style_bg_color(open_with_dialog, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(open_with_dialog, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(open_with_dialog, 2, 0);
    lv_obj_set_style_radius(open_with_dialog, 8, 0);
    lv_obj_set_style_pad_all(open_with_dialog, 15, 0);
    lv_obj_remove_flag(open_with_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(open_with_dialog);
    lv_obj_set_size(title_bar, lv_pct(100), 36);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 4, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(title_bar);
    lv_label_set_text(title, "Open With...");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_center(title);
    
    // File name
    lv_obj_t *file_lbl = lv_label_create(open_with_dialog);
    char file_buf[128];
    snprintf(file_buf, sizeof(file_buf), "File: %.100s", filename);
    lv_label_set_text(file_lbl, file_buf);
    lv_obj_set_style_text_color(file_lbl, lv_color_black(), 0);
    lv_obj_align(file_lbl, LV_ALIGN_TOP_LEFT, 0, 35);
    lv_obj_set_width(file_lbl, 340);
    lv_label_set_long_mode(file_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    
    // Apps list
    lv_obj_t *apps_label = lv_label_create(open_with_dialog);
    lv_label_set_text(apps_label, "Choose application:");
    lv_obj_set_style_text_color(apps_label, lv_color_hex(0x666666), 0);
    lv_obj_align(apps_label, LV_ALIGN_TOP_LEFT, 0, 60);
    
    // Helper to create app button
    auto create_app_btn = [&](const char *name, const char *icon_text, uint32_t color, 
                              int y_offset, lv_event_cb_t cb) {
        lv_obj_t *btn = lv_obj_create(open_with_dialog);
        lv_obj_set_size(btn, lv_pct(100), 50);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, y_offset);
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_left(btn, 10, 0);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE8E8FF), LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
        
        // Icon
        lv_obj_t *icon = lv_obj_create(btn);
        lv_obj_set_size(icon, 35, 35);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(icon, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_set_style_radius(icon, 4, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *icon_lbl = lv_label_create(icon);
        lv_label_set_text(icon_lbl, icon_text);
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);
        
        // Name
        lv_obj_t *name_lbl = lv_label_create(btn);
        lv_label_set_text(name_lbl, name);
        lv_obj_set_style_text_color(name_lbl, lv_color_black(), 0);
        lv_obj_set_style_text_font(name_lbl, UI_FONT, 0);
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 50, 0);
        lv_obj_remove_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE);
    };
    
    // Notepad option
    create_app_btn("Notepad", "TXT", 0x0054E3, 85, open_with_notepad_clicked);
    
    // Properties option
    create_app_btn("Properties", "i", 0x888888, 145, open_with_info_clicked);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(open_with_dialog);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
        open_with_dialog_close();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
    lv_obj_center(cancel_lbl);
}

// Context menu for files/folders
static lv_obj_t *context_menu = NULL;
static char context_menu_path[384] = {0};
static bool context_menu_is_dir = false;

static void context_menu_close(void)
{
    if (context_menu) {
        lv_obj_delete(context_menu);
        context_menu = NULL;
    }
}

static void context_open_cb(lv_event_t *e)
{
    context_menu_close();
    if (context_menu_is_dir) {
        strncpy(mycomp_current_path, context_menu_path, sizeof(mycomp_current_path) - 1);
        mycomp_current_path[sizeof(mycomp_current_path) - 1] = '\0';
        mycomp_browse_path(context_menu_path);
    } else {
        show_open_with_dialog(context_menu_path);
    }
}

static void context_rename_cb(lv_event_t *e)
{
    context_menu_close();
    show_rename_dialog(context_menu_path);
}

static void context_delete_cb(lv_event_t *e)
{
    context_menu_close();
    show_delete_confirm(context_menu_path);
}

static void context_properties_cb(lv_event_t *e)
{
    context_menu_close();
    strncpy(pending_file_path, context_menu_path, sizeof(pending_file_path) - 1);
    pending_file_path[sizeof(pending_file_path) - 1] = '\0';
    open_with_info_clicked(NULL);
}

static void show_context_menu(const char *path, bool is_dir, lv_coord_t x, lv_coord_t y)
{
    context_menu_close();
    
    strncpy(context_menu_path, path, sizeof(context_menu_path) - 1);
    context_menu_is_dir = is_dir;
    
    context_menu = lv_obj_create(lv_screen_active());
    lv_obj_set_size(context_menu, 150, is_dir ? 130 : 170);
    lv_obj_set_pos(context_menu, x, y);
    lv_obj_set_style_bg_color(context_menu, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(context_menu, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(context_menu, 1, 0);
    lv_obj_set_style_radius(context_menu, 4, 0);
    lv_obj_set_style_shadow_width(context_menu, 8, 0);
    lv_obj_set_style_shadow_color(context_menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(context_menu, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(context_menu, 4, 0);
    lv_obj_set_flex_flow(context_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(context_menu, 2, 0);
    lv_obj_remove_flag(context_menu, LV_OBJ_FLAG_SCROLLABLE);
    
    // Helper to create menu item
    auto create_menu_item = [](lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
        lv_obj_t *item = lv_obj_create(parent);
        lv_obj_set_size(item, lv_pct(100), 32);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD0E8FF), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 3, 0);
        lv_obj_set_style_pad_left(item, 10, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    };
    
    create_menu_item(context_menu, "Open", context_open_cb);
    create_menu_item(context_menu, "Rename", context_rename_cb);
    create_menu_item(context_menu, "Delete", context_delete_cb);
    if (!is_dir) {
        create_menu_item(context_menu, "Properties", context_properties_cb);
    }
    
    // Close menu when clicking outside
    lv_obj_add_event_cb(lv_screen_active(), [](lv_event_t *e) {
        lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
        if (context_menu && target != context_menu && !lv_obj_has_flag_any(target, LV_OBJ_FLAG_CLICKABLE)) {
            context_menu_close();
        }
    }, LV_EVENT_CLICKED, NULL);
}

static void mycomp_item_clicked(lv_event_t *e)
{
    const char *path = (const char *)lv_event_get_user_data(e);
    if (!path) return;
    
    struct stat st;
    bool is_dir = false;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        is_dir = true;
    }
    
    // Get click position for context menu
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    
    // Show context menu instead of direct action
    show_context_menu(path, is_dir, point.x, point.y);
}

static void mycomp_drive_clicked(lv_event_t *e)
{
    const char *base_path = (const char *)lv_event_get_user_data(e);
    if (!base_path) return;
    
    strncpy(mycomp_current_path, base_path, sizeof(mycomp_current_path) - 1);
    mycomp_browse_path(base_path);
}

static void mycomp_browse_path(const char *path)
{
    if (!mycomp_content) return;
    
    // Clear content
    lv_obj_clean(mycomp_content);
    
    // Vista-style Navigation bar with Aero Glass
    lv_obj_t *navbar = lv_obj_create(mycomp_content);
    lv_obj_set_size(navbar, lv_pct(100), 45);
    // Vista Aero gradient
    lv_obj_set_style_bg_color(navbar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(navbar, lv_color_hex(0xD0E8F8), 0);
    lv_obj_set_style_bg_grad_dir(navbar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(navbar, lv_color_hex(0xA0C8E8), 0);
    lv_obj_set_style_border_width(navbar, 1, 0);
    lv_obj_set_style_border_side(navbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(navbar, 0, 0);
    lv_obj_set_style_pad_all(navbar, 5, 0);
    lv_obj_remove_flag(navbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button - Vista style
    lv_obj_t *back_btn = lv_obj_create(navbar);
    lv_obj_set_size(back_btn, 36, 32);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(back_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(back_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x3A80C9), LV_STATE_PRESSED);
    lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, mycomp_back_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_arrow = lv_label_create(back_btn);
    lv_label_set_text(back_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_arrow, lv_color_white(), 0);
    lv_obj_center(back_arrow);
    lv_obj_remove_flag(back_arrow, LV_OBJ_FLAG_CLICKABLE);
    
    // Address bar - Vista style
    lv_obj_t *address_bar = lv_obj_create(navbar);
    lv_obj_set_size(address_bar, 280, 32);
    lv_obj_align(address_bar, LV_ALIGN_LEFT_MID, 45, 0);
    lv_obj_set_style_bg_color(address_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(address_bar, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(address_bar, 1, 0);
    lv_obj_set_style_radius(address_bar, 3, 0);
    lv_obj_set_style_pad_left(address_bar, 8, 0);
    lv_obj_remove_flag(address_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Folder icon in address bar
    lv_obj_t *folder_icon = lv_image_create(address_bar);
    lv_image_set_src(folder_icon, &img_folder);
    lv_image_set_scale(folder_icon, 96);  // Small icon
    lv_obj_align(folder_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Path label
    mycomp_path_label = lv_label_create(address_bar);
    lv_label_set_text(mycomp_path_label, path);
    lv_obj_set_style_text_color(mycomp_path_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(mycomp_path_label, UI_FONT, 0);
    lv_obj_align(mycomp_path_label, LV_ALIGN_LEFT_MID, 28, 0);
    lv_label_set_long_mode(mycomp_path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(mycomp_path_label, 220);
    
    // New Folder button
    lv_obj_t *new_folder_btn = lv_obj_create(navbar);
    lv_obj_set_size(new_folder_btn, 32, 32);
    lv_obj_align(new_folder_btn, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_set_style_bg_color(new_folder_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(new_folder_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(new_folder_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(new_folder_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(new_folder_btn, 1, 0);
    lv_obj_set_style_radius(new_folder_btn, 4, 0);
    lv_obj_add_flag(new_folder_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(new_folder_btn, lv_color_hex(0x3A80C9), LV_STATE_PRESSED);
    lv_obj_remove_flag(new_folder_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(new_folder_btn, mycomp_new_folder_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *folder_plus = lv_label_create(new_folder_btn);
    lv_label_set_text(folder_plus, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_color(folder_plus, lv_color_white(), 0);
    lv_obj_center(folder_plus);
    lv_obj_remove_flag(folder_plus, LV_OBJ_FLAG_CLICKABLE);
    
    // New File button
    lv_obj_t *new_file_btn = lv_obj_create(navbar);
    lv_obj_set_size(new_file_btn, 32, 32);
    lv_obj_align(new_file_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(new_file_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(new_file_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(new_file_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(new_file_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(new_file_btn, 1, 0);
    lv_obj_set_style_radius(new_file_btn, 4, 0);
    lv_obj_add_flag(new_file_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(new_file_btn, lv_color_hex(0x3A80C9), LV_STATE_PRESSED);
    lv_obj_remove_flag(new_file_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(new_file_btn, mycomp_new_file_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *file_plus = lv_label_create(new_file_btn);
    lv_label_set_text(file_plus, LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(file_plus, lv_color_white(), 0);
    lv_obj_center(file_plus);
    lv_obj_remove_flag(file_plus, LV_OBJ_FLAG_CLICKABLE);
    
    // Main area with sidebar and file list
    lv_obj_t *main_area = lv_obj_create(mycomp_content);
    lv_obj_set_size(main_area, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4 - 55);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_remove_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left sidebar - Vista Favorites Links style
    lv_obj_t *sidebar = lv_obj_create(main_area);
    lv_obj_set_size(sidebar, 140, lv_pct(100));
    lv_obj_align(sidebar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(sidebar, lv_color_hex(0xD8ECF8), 0);
    lv_obj_set_style_bg_grad_dir(sidebar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 8, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sidebar, 4, 0);
    
    // Sidebar header
    lv_obj_t *fav_header = lv_label_create(sidebar);
    lv_label_set_text(fav_header, "Favorite Links");
    lv_obj_set_style_text_color(fav_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(fav_header, UI_FONT, 0);
    
    // Sidebar items
    static const char* sidebar_items[] = {"Documents", "Pictures", "Music", "Desktop"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(sidebar);
        lv_obj_set_size(item, lv_pct(100), 28);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xC8E0F8), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 3, 0);
        lv_obj_set_style_pad_left(item, 4, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, &img_folder);
        lv_image_set_scale(icon, 96);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, sidebar_items[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1A5090), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 22, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // File list container - white background
    lv_obj_t *file_list = lv_obj_create(main_area);
    lv_obj_set_size(file_list, 320, lv_pct(100));
    lv_obj_align(file_list, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(file_list, 0, 0);
    lv_obj_set_style_radius(file_list, 0, 0);
    lv_obj_set_style_pad_all(file_list, 5, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(file_list, 2, 0);
    
    // Column headers
    lv_obj_t *header_row = lv_obj_create(file_list);
    lv_obj_set_size(header_row, lv_pct(100), 24);
    lv_obj_set_style_bg_color(header_row, lv_color_hex(0xF0F8FF), 0);
    lv_obj_set_style_border_color(header_row, lv_color_hex(0xD0E0F0), 0);
    lv_obj_set_style_border_width(header_row, 1, 0);
    lv_obj_set_style_border_side(header_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(header_row, 0, 0);
    lv_obj_set_style_pad_left(header_row, 8, 0);
    lv_obj_remove_flag(header_row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *name_hdr = lv_label_create(header_row);
    lv_label_set_text(name_hdr, "Name");
    lv_obj_set_style_text_color(name_hdr, lv_color_hex(0x404040), 0);
    lv_obj_align(name_hdr, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *size_hdr = lv_label_create(header_row);
    lv_label_set_text(size_hdr, "Size");
    lv_obj_set_style_text_color(size_hdr, lv_color_hex(0x404040), 0);
    lv_obj_align(size_hdr, LV_ALIGN_RIGHT_MID, -10, 0);
    
    // Read directory
    DIR *dir = opendir(path);
    if (!dir) {
        lv_obj_t *err = lv_label_create(file_list);
        lv_label_set_text(err, "Cannot open directory");
        lv_obj_set_style_text_color(err, lv_color_hex(0xCC0000), 0);
        return;
    }
    
    struct dirent *entry;
    int file_count = 0;
    
    // Static storage for paths (max 20 items, 384 bytes each for long paths)
    static char item_paths[20][384];
    
    while ((entry = readdir(dir)) != NULL && file_count < 20) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path (truncate if too long)
        int written = snprintf(item_paths[file_count], sizeof(item_paths[0]), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(item_paths[0])) {
            continue;  // Skip if path too long
        }
        
        // Get file info
        struct stat st;
        bool is_dir = false;
        size_t file_size = 0;
        if (stat(item_paths[file_count], &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            file_size = st.st_size;
        }
        
        // Create item - Vista style row
        lv_obj_t *item = lv_obj_create(file_list);
        lv_obj_set_size(item, lv_pct(100), 32);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD8ECFC), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 2, 0);
        lv_obj_set_style_pad_left(item, 8, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, mycomp_item_clicked, LV_EVENT_CLICKED, item_paths[file_count]);
        
        // Icon (folder or file with proper icon)
        lv_obj_t *icon = lv_image_create(item);
        if (is_dir) {
            lv_image_set_src(icon, &img_folder);
            lv_image_set_scale(icon, 112);  // ~22px from 48px
        } else {
            // Determine file type by extension
            const char *ext = strrchr(entry->d_name, '.');
            bool is_image = false;
            if (ext) {
                if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
                    strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".bmp") == 0) {
                    is_image = true;
                }
            }
            if (is_image) {
                lv_image_set_src(icon, &img_photo);
            } else {
                lv_image_set_src(icon, &img_file);
            }
            // img_file and img_photo are 24x24, no scaling needed
        }
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        // Name
        lv_obj_t *name = lv_label_create(item);
        lv_label_set_text(name, entry->d_name);
        lv_obj_set_style_text_color(name, lv_color_black(), 0);
        lv_obj_set_style_text_font(name, UI_FONT, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, 0);
        lv_obj_set_width(name, 180);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
        
        // Size (for files)
        if (!is_dir) {
            lv_obj_t *size_lbl = lv_label_create(item);
            char size_str[16];
            if (file_size < 1024) {
                snprintf(size_str, sizeof(size_str), "%zu B", file_size);
            } else if (file_size < 1024 * 1024) {
                snprintf(size_str, sizeof(size_str), "%.1f KB", file_size / 1024.0);
            } else {
                snprintf(size_str, sizeof(size_str), "%.1f MB", file_size / (1024.0 * 1024.0));
            }
            lv_label_set_text(size_lbl, size_str);
            lv_obj_set_style_text_color(size_lbl, lv_color_hex(0x606060), 0);
            lv_obj_align(size_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
            lv_obj_remove_flag(size_lbl, LV_OBJ_FLAG_CLICKABLE);
        }
        
        file_count++;
    }
    closedir(dir);
    
    if (file_count == 0) {
        lv_obj_t *empty = lv_label_create(file_list);
        lv_label_set_text(empty, "(Empty folder)");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x888888), 0);
    }
}

static void mycomp_show_root(void)
{
    if (!mycomp_content) return;
    
    mycomp_current_path[0] = '\0';
    
    // Clear content
    lv_obj_clean(mycomp_content);
    
    // Vista-style Navigation bar
    lv_obj_t *navbar = lv_obj_create(mycomp_content);
    lv_obj_set_size(navbar, lv_pct(100), 45);
    lv_obj_set_style_bg_color(navbar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(navbar, lv_color_hex(0xD0E8F8), 0);
    lv_obj_set_style_bg_grad_dir(navbar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(navbar, lv_color_hex(0xA0C8E8), 0);
    lv_obj_set_style_border_width(navbar, 1, 0);
    lv_obj_set_style_border_side(navbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(navbar, 0, 0);
    lv_obj_set_style_pad_all(navbar, 5, 0);
    lv_obj_remove_flag(navbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Computer icon and title in navbar
    lv_obj_t *comp_icon = lv_image_create(navbar);
    lv_image_set_src(comp_icon, &img_my_computer);
    lv_image_set_scale(comp_icon, 128);
    lv_obj_align(comp_icon, LV_ALIGN_LEFT_MID, 5, 0);
    
    lv_obj_t *comp_title = lv_label_create(navbar);
    lv_label_set_text(comp_title, "Computer");
    lv_obj_set_style_text_color(comp_title, lv_color_black(), 0);
    lv_obj_set_style_text_font(comp_title, UI_FONT, 0);
    lv_obj_align(comp_title, LV_ALIGN_LEFT_MID, 40, 0);
    
    // Main area with sidebar and drives
    lv_obj_t *main_area = lv_obj_create(mycomp_content);
    lv_obj_set_size(main_area, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4 - 55);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_remove_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left sidebar - Vista style
    lv_obj_t *sidebar = lv_obj_create(main_area);
    lv_obj_set_size(sidebar, 140, lv_pct(100));
    lv_obj_align(sidebar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xE8F4FC), 0);
    lv_obj_set_style_bg_grad_color(sidebar, lv_color_hex(0xD8ECF8), 0);
    lv_obj_set_style_bg_grad_dir(sidebar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(0xB0D0E8), 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 8, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sidebar, 4, 0);
    
    // Sidebar header
    lv_obj_t *fav_header = lv_label_create(sidebar);
    lv_label_set_text(fav_header, "Favorite Links");
    lv_obj_set_style_text_color(fav_header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(fav_header, UI_FONT, 0);
    
    // Sidebar items
    static const char* sidebar_items[] = {"Documents", "Pictures", "Music", "Desktop"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(sidebar);
        lv_obj_set_size(item, lv_pct(100), 28);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xC8E0F8), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 3, 0);
        lv_obj_set_style_pad_left(item, 4, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, &img_folder);
        lv_image_set_scale(icon, 96);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, sidebar_items[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1A5090), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 22, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Drives area - white background
    lv_obj_t *drives_area = lv_obj_create(main_area);
    lv_obj_set_size(drives_area, 320, lv_pct(100));
    lv_obj_align(drives_area, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(drives_area, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(drives_area, 0, 0);
    lv_obj_set_style_radius(drives_area, 0, 0);
    lv_obj_set_style_pad_all(drives_area, 10, 0);
    lv_obj_set_flex_flow(drives_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(drives_area, 8, 0);
    
    // Header
    lv_obj_t *header = lv_label_create(drives_area);
    lv_label_set_text(header, "Hard Disk Drives");
    lv_obj_set_style_text_color(header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(header, UI_FONT, 0);
    
    // Helper to create Vista-style drive item
    auto create_drive = [](lv_obj_t *parent, const char* name, const char* size_info, 
                          const lv_image_dsc_t *icon_img, int used_percent, const char *base_path) -> lv_obj_t* {
        lv_obj_t *item = lv_obj_create(parent);
        lv_obj_set_size(item, lv_pct(100), 65);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD8ECFC), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        
        // Drive icon
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, icon_img);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, -5);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        // Name
        lv_obj_t *name_label = lv_label_create(item);
        lv_label_set_text(name_label, name);
        lv_obj_set_style_text_color(name_label, lv_color_black(), 0);
        lv_obj_set_style_text_font(name_label, UI_FONT, 0);
        lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 55, 0);
        lv_obj_remove_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
        
        // Usage bar - Vista blue style
        if (used_percent >= 0) {
            lv_obj_t *bar = lv_bar_create(item);
            lv_obj_set_size(bar, 200, 14);
            lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 55, 22);
            lv_bar_set_range(bar, 0, 100);
            lv_bar_set_value(bar, used_percent, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
            lv_obj_set_style_border_color(bar, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
            lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
            // Vista blue gradient for indicator
            uint32_t bar_color = used_percent < 90 ? 0x4A90D9 : 0xD94A4A;
            lv_obj_set_style_bg_color(bar, lv_color_hex(bar_color), LV_PART_INDICATOR);
            lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
        }
        
        // Size info
        lv_obj_t *size_label = lv_label_create(item);
        lv_label_set_text(size_label, size_info);
        lv_obj_set_style_text_color(size_label, lv_color_hex(0x606060), 0);
        lv_obj_set_style_text_font(size_label, UI_FONT, 0);
        lv_obj_align(size_label, LV_ALIGN_TOP_LEFT, 55, 40);
        lv_obj_remove_flag(size_label, LV_OBJ_FLAG_CLICKABLE);
        
        return item;
    };
    
    // LittleFS (Internal Flash)
    char littlefs_info[64];
    int littlefs_percent = -1;
    if (hw_littlefs_is_mounted()) {
        hw_littlefs_info_t lfs;
        if (hw_littlefs_get_info(&lfs) == ESP_OK) {
            size_t free_kb = (lfs.total_bytes - lfs.used_bytes) / 1024;
            size_t total_kb = lfs.total_bytes / 1024;
            littlefs_percent = (lfs.used_bytes * 100) / lfs.total_bytes;
            snprintf(littlefs_info, sizeof(littlefs_info), "%zu KB free of %zu KB", free_kb, total_kb);
        } else {
            snprintf(littlefs_info, sizeof(littlefs_info), "Error reading info");
        }
    } else {
        snprintf(littlefs_info, sizeof(littlefs_info), "Not mounted");
    }
    lv_obj_t *littlefs_item = create_drive(drives_area, "Local Disk (C:)", littlefs_info, &img_my_computer, littlefs_percent, "/littlefs");
    if (hw_littlefs_is_mounted()) {
        static const char *littlefs_path = "/littlefs";
        lv_obj_add_event_cb(littlefs_item, mycomp_drive_clicked, LV_EVENT_CLICKED, (void*)littlefs_path);
    }
    
    // SD Card
    char sdcard_info[64];
    int sdcard_percent = -1;
    if (hw_sdcard_is_mounted()) {
        hw_sdcard_info_t sd;
        if (hw_sdcard_get_info(&sd)) {
            sdcard_percent = (sd.used_bytes * 100) / sd.total_bytes;
            if (sd.total_bytes > 1024*1024*1024) {
                snprintf(sdcard_info, sizeof(sdcard_info), "%.1f GB free of %.1f GB",
                         sd.free_bytes / (1024.0*1024*1024), sd.total_bytes / (1024.0*1024*1024));
            } else {
                snprintf(sdcard_info, sizeof(sdcard_info), "%llu MB free of %llu MB",
                         (unsigned long long)(sd.free_bytes / (1024*1024)), 
                         (unsigned long long)(sd.total_bytes / (1024*1024)));
            }
        } else {
            snprintf(sdcard_info, sizeof(sdcard_info), "Error reading card");
        }
        lv_obj_t *sd_item = create_drive(drives_area, "SD Card (D:)", sdcard_info, &img_folder, sdcard_percent, "/sdcard");
        static const char *sdcard_path = "/sdcard";
        lv_obj_add_event_cb(sd_item, mycomp_drive_clicked, LV_EVENT_CLICKED, (void*)sdcard_path);
    } else {
        create_drive(drives_area, "SD Card (D:)", "Not inserted", &img_folder, -1, NULL);
    }
}

void app_my_computer_create(void)
{
    ESP_LOGI(TAG, "Opening My Computer");
    create_app_window("Computer");
    
    // Reset state
    mycomp_current_path[0] = '\0';
    mycomp_path_label = NULL;
    
    // Content area - Vista Aero Glass gradient background
    mycomp_content = lv_obj_create(app_window);
    lv_obj_set_size(mycomp_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(mycomp_content, LV_ALIGN_TOP_LEFT, 0, 32);
    // Vista Aero gradient - light blue
    lv_obj_set_style_bg_color(mycomp_content, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(mycomp_content, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(mycomp_content, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(mycomp_content, 0, 0);
    lv_obj_set_style_radius(mycomp_content, 0, 0);
    lv_obj_set_style_pad_all(mycomp_content, 0, 0);
    lv_obj_set_flex_flow(mycomp_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mycomp_content, 0, 0);
    
    // Show root view (drives)
    mycomp_show_root();
}


// ============ PHOTO VIEWER APP ============

static lv_obj_t *photo_content = NULL;
static lv_obj_t *photo_image = NULL;
static lv_obj_t *photo_filename_label = NULL;
static char photo_current_path[128] = "";
static char photo_files[20][64];  // Max 20 files
static int photo_file_count = 0;
static int photo_current_index = 0;

// Store full paths for "All" mode (recursive search)
static char photo_full_paths[20][256];
static bool photo_all_mode = false;

// Photo viewer state - zoom and rotation
static int photo_zoom_level = 100;  // 100 = 100%, range 50-300
static int photo_rotation = 0;      // 0, 90, 180, 270 degrees
static char photo_current_full_path[256] = "";  // Current photo full path for info

static void photo_load_image(const char *path);
static void photo_scan_directory(const char *dir_path);
static void photo_apply_transform(void);

static void photo_prev_cb(lv_event_t *e)
{
    if (photo_file_count > 0) {
        photo_current_index = (photo_current_index - 1 + photo_file_count) % photo_file_count;
        if (photo_all_mode) {
            photo_load_image(photo_full_paths[photo_current_index]);
        } else {
            char full_path[192];
            snprintf(full_path, sizeof(full_path), "%s/%s", photo_current_path, photo_files[photo_current_index]);
            photo_load_image(full_path);
        }
    }
}

static void photo_next_cb(lv_event_t *e)
{
    if (photo_file_count > 0) {
        photo_current_index = (photo_current_index + 1) % photo_file_count;
        if (photo_all_mode) {
            photo_load_image(photo_full_paths[photo_current_index]);
        } else {
            char full_path[192];
            snprintf(full_path, sizeof(full_path), "%s/%s", photo_current_path, photo_files[photo_current_index]);
            photo_load_image(full_path);
        }
    }
}

static void photo_apply_transform(void)
{
    if (!photo_image) return;
    
    // Apply zoom
    int scale = (photo_zoom_level * 256) / 100;  // LVGL uses 256 = 100%
    lv_image_set_scale(photo_image, scale);
    
    // Apply rotation
    lv_image_set_rotation(photo_image, photo_rotation * 10);  // LVGL uses 0.1 degree units
}

static void photo_load_image(const char *path)
{
    if (!photo_image) return;
    
    // Save current path
    snprintf(photo_current_full_path, sizeof(photo_current_full_path), "%s", path);
    
    // Reset zoom and rotation for new image
    photo_zoom_level = 100;
    photo_rotation = 0;
    
    // Try to load image using LVGL file system
    // Requires CONFIG_LV_USE_FS_POSIX=y and CONFIG_LV_FS_POSIX_LETTER='A' in sdkconfig
    char lv_path[256];
    snprintf(lv_path, sizeof(lv_path), "A:%s", path);  // LVGL file system prefix
    
    ESP_LOGI(TAG, "Loading image: %s", lv_path);
    lv_image_set_src(photo_image, lv_path);
    
    // Reset transform
    photo_apply_transform();
    lv_image_set_src(photo_image, lv_path);
    
    // Update filename label
    if (photo_filename_label && photo_file_count > 0) {
        const char *filename = strrchr(path, '/');
        filename = filename ? filename + 1 : path;
        char buf[80];
        snprintf(buf, sizeof(buf), "%s (%d/%d)", filename, 
                 photo_current_index + 1, photo_file_count);
        lv_label_set_text(photo_filename_label, buf);
    }
}

static void photo_scan_directory(const char *dir_path)
{
    photo_file_count = 0;
    strncpy(photo_current_path, dir_path, sizeof(photo_current_path) - 1);
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open directory: %s", dir_path);
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && photo_file_count < 20) {
        const char *name = entry->d_name;
        int len = strlen(name);
        
        // Check for image extensions (.jpg, .png, .bmp, .jpeg)
        if (len > 4) {
            const char *ext4 = name + len - 4;
            const char *ext5 = (len > 5) ? name + len - 5 : NULL;
            if (strcasecmp(ext4, ".jpg") == 0 || strcasecmp(ext4, ".png") == 0 ||
                strcasecmp(ext4, ".bmp") == 0 || (ext5 && strcasecmp(ext5, ".jpeg") == 0)) {
                strncpy(photo_files[photo_file_count], name, 63);
                photo_files[photo_file_count][63] = '\0';
                photo_file_count++;
            }
        }
    }
    closedir(dir);
    
    ESP_LOGI(TAG, "Found %d images in %s", photo_file_count, dir_path);
}

static void photo_scan_recursive(const char *dir_path, int depth)
{
    if (depth > 2 || photo_file_count >= 20) return;  // Limit depth and count
    
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && photo_file_count < 20) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Limit filename length to prevent overflow
        char safe_name[64];
        strncpy(safe_name, entry->d_name, sizeof(safe_name) - 1);
        safe_name[sizeof(safe_name) - 1] = '\0';
        
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%.128s/%.64s", dir_path, safe_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Recurse into subdirectory
                photo_scan_recursive(full_path, depth + 1);
            } else {
                // Check for image extensions (.jpg, .png, .bmp, .jpeg)
                int len = strlen(safe_name);
                if (len > 4) {
                    const char *ext4 = safe_name + len - 4;
                    const char *ext5 = (len > 5) ? safe_name + len - 5 : NULL;
                    if (strcasecmp(ext4, ".jpg") == 0 || strcasecmp(ext4, ".png") == 0 ||
                        strcasecmp(ext4, ".bmp") == 0 || (ext5 && strcasecmp(ext5, ".jpeg") == 0)) {
                        strncpy(photo_files[photo_file_count], safe_name, 63);
                        photo_files[photo_file_count][63] = '\0';
                        snprintf(photo_full_paths[photo_file_count], sizeof(photo_full_paths[0]), "%s", full_path);
                        photo_file_count++;
                    }
                }
            }
        }
    }
    closedir(dir);
}

static void photo_scan_all(void)
{
    photo_file_count = 0;
    photo_all_mode = true;
    photo_current_path[0] = '\0';
    
    // Scan multiple directories
    static const char *search_dirs[] = {
        "/littlefs",
        "/littlefs/photos",
        "/sdcard",
        "/sdcard/DCIM",
        "/sdcard/photos"
    };
    
    for (int i = 0; i < 5 && photo_file_count < 20; i++) {
        photo_scan_recursive(search_dirs[i], 0);
    }
    
    ESP_LOGI(TAG, "Found %d images in all directories", photo_file_count);
}

static void photo_all_source_cb(lv_event_t *e)
{
    photo_scan_all();
    photo_current_index = 0;
    
    if (photo_file_count > 0) {
        photo_load_image(photo_full_paths[0]);
    } else {
        if (photo_filename_label) {
            lv_label_set_text(photo_filename_label, "No images found");
        }
    }
}

static void photo_source_cb(lv_event_t *e)
{
    const char *source = (const char *)lv_event_get_user_data(e);
    photo_all_mode = false;  // Reset all mode
    photo_scan_directory(source);
    photo_current_index = 0;
    
    if (photo_file_count > 0) {
        char full_path[192];
        snprintf(full_path, sizeof(full_path), "%s/%s", photo_current_path, photo_files[0]);
        photo_load_image(full_path);
    } else {
        if (photo_filename_label) {
            lv_label_set_text(photo_filename_label, "No images found");
        }
    }
}

// Zoom in callback
static void photo_zoom_in_cb(lv_event_t *e)
{
    if (photo_zoom_level < 300) {
        photo_zoom_level += 25;
        photo_apply_transform();
        ESP_LOGI(TAG, "Zoom: %d%%", photo_zoom_level);
    }
}

// Zoom out callback
static void photo_zoom_out_cb(lv_event_t *e)
{
    if (photo_zoom_level > 50) {
        photo_zoom_level -= 25;
        photo_apply_transform();
        ESP_LOGI(TAG, "Zoom: %d%%", photo_zoom_level);
    }
}

// Rotate callback
static void photo_rotate_cb(lv_event_t *e)
{
    photo_rotation = (photo_rotation + 90) % 360;
    photo_apply_transform();
    ESP_LOGI(TAG, "Rotation: %d°", photo_rotation);
}

// Info callback - show photo information
static void photo_info_cb(lv_event_t *e)
{
    if (photo_current_full_path[0] == '\0') return;
    
    struct stat st;
    if (stat(photo_current_full_path, &st) != 0) {
        if (photo_filename_label) {
            lv_label_set_text(photo_filename_label, "Cannot get file info");
        }
        return;
    }
    
    const char *filename = strrchr(photo_current_full_path, '/');
    filename = filename ? filename + 1 : photo_current_full_path;
    
    // Format file size
    char size_str[32];
    if (st.st_size >= 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.1f MB", st.st_size / (1024.0 * 1024.0));
    } else if (st.st_size >= 1024) {
        snprintf(size_str, sizeof(size_str), "%.1f KB", st.st_size / 1024.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%lu B", (unsigned long)st.st_size);
    }
    
    // Show info in filename label
    if (photo_filename_label) {
        char buf[256];
        // Truncate filename if too long
        char short_name[32];
        if (strlen(filename) > 20) {
            snprintf(short_name, sizeof(short_name), "%.17s...", filename);
        } else {
            snprintf(short_name, sizeof(short_name), "%s", filename);
        }
        snprintf(buf, sizeof(buf), "%s | %s | Zoom:%d%% | Rot:%d", 
                 short_name, size_str, photo_zoom_level, photo_rotation);
        lv_label_set_text(photo_filename_label, buf);
    }
}

// Bluetooth share callback
static void photo_bt_share_cb(lv_event_t *e)
{
    if (photo_file_count == 0) {
        ESP_LOGW(TAG, "No photo to share");
        return;
    }
    
    // Get current photo path
    char full_path[256];
    if (photo_all_mode) {
        snprintf(full_path, sizeof(full_path), "%s", photo_full_paths[photo_current_index]);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", photo_current_path, photo_files[photo_current_index]);
    }
    
    ESP_LOGI(TAG, "Sharing via Bluetooth: %s", full_path);
    
    // Check if BT is ready
    if (!bt_is_ready()) {
        ESP_LOGI(TAG, "Initializing Bluetooth...");
        if (bt_init() != 0) {
            ESP_LOGE(TAG, "Failed to init Bluetooth");
            if (photo_filename_label) {
                lv_label_set_text(photo_filename_label, "BT init failed");
            }
            return;
        }
    }
    
    // Check if connected
    if (!bt_is_connected()) {
        // Start advertising and show status
        bt_start_advertising();
        if (photo_filename_label) {
            char buf[80];
            snprintf(buf, sizeof(buf), "BT: %s - waiting...", bt_get_device_name());
            lv_label_set_text(photo_filename_label, buf);
        }
        return;
    }
    
    // Send file
    int ret = bt_send_file(full_path, NULL);
    if (ret == 0) {
        if (photo_filename_label) {
            lv_label_set_text(photo_filename_label, "Sending via BT...");
        }
    } else {
        ESP_LOGE(TAG, "Failed to send file: %d", ret);
        if (photo_filename_label) {
            lv_label_set_text(photo_filename_label, "BT send failed");
        }
    }
}

void app_photo_viewer_create(void)
{
    ESP_LOGI(TAG, "Opening Photo Viewer");
    create_app_window("Photo Viewer");
    
    // Main content - Windows Photo Viewer style (light blue/white)
    photo_content = lv_obj_create(app_window);
    lv_obj_set_size(photo_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(photo_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(photo_content, lv_color_hex(0xF0F0F0), 0);  // Light gray background
    lv_obj_set_style_border_width(photo_content, 0, 0);
    lv_obj_set_style_pad_all(photo_content, 0, 0);
    lv_obj_remove_flag(photo_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Top toolbar - Windows Vista style gradient
    lv_obj_t *toolbar = lv_obj_create(photo_content);
    lv_obj_set_size(toolbar, lv_pct(100), 40);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_color(toolbar, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_dir(toolbar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_color(toolbar, lv_color_hex(0xB8D4F0), 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(toolbar, 0, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(toolbar, 10, 0);
    lv_obj_set_style_pad_column(toolbar, 8, 0);
    lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // LittleFS button
    lv_obj_t *lfs_btn = lv_btn_create(toolbar);
    lv_obj_set_size(lfs_btn, 90, 28);
    lv_obj_set_style_bg_color(lfs_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(lfs_btn, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_bg_grad_dir(lfs_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(lfs_btn, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(lfs_btn, 1, 0);
    lv_obj_set_style_radius(lfs_btn, 3, 0);
    lv_obj_t *lfs_lbl = lv_label_create(lfs_btn);
    lv_label_set_text(lfs_lbl, "LittleFS");
    lv_obj_set_style_text_color(lfs_lbl, lv_color_black(), 0);
    lv_obj_center(lfs_lbl);
    lv_obj_add_event_cb(lfs_btn, photo_source_cb, LV_EVENT_CLICKED, (void*)"/littlefs");
    
    // SD Card button
    lv_obj_t *sd_btn = lv_btn_create(toolbar);
    lv_obj_set_size(sd_btn, 90, 28);
    lv_obj_set_style_bg_color(sd_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(sd_btn, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_bg_grad_dir(sd_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(sd_btn, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(sd_btn, 1, 0);
    lv_obj_set_style_radius(sd_btn, 3, 0);
    lv_obj_t *sd_lbl = lv_label_create(sd_btn);
    lv_label_set_text(sd_lbl, "SD Card");
    lv_obj_set_style_text_color(sd_lbl, lv_color_black(), 0);
    lv_obj_center(sd_lbl);
    lv_obj_add_event_cb(sd_btn, photo_source_cb, LV_EVENT_CLICKED, (void*)"/sdcard");
    
    // All button - search all directories
    lv_obj_t *all_btn = lv_btn_create(toolbar);
    lv_obj_set_size(all_btn, 60, 28);
    lv_obj_set_style_bg_color(all_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(all_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(all_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(all_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_border_width(all_btn, 1, 0);
    lv_obj_set_style_radius(all_btn, 3, 0);
    lv_obj_t *all_lbl = lv_label_create(all_btn);
    lv_label_set_text(all_lbl, "All");
    lv_obj_set_style_text_color(all_lbl, lv_color_white(), 0);
    lv_obj_center(all_lbl);
    lv_obj_add_event_cb(all_btn, photo_all_source_cb, LV_EVENT_CLICKED, NULL);
    
    // Bluetooth Share button
    lv_obj_t *bt_btn = lv_btn_create(toolbar);
    lv_obj_set_size(bt_btn, 36, 28);
    lv_obj_set_style_bg_color(bt_btn, lv_color_hex(0x0082FC), 0);  // Bluetooth blue
    lv_obj_set_style_bg_grad_color(bt_btn, lv_color_hex(0x0062CC), 0);
    lv_obj_set_style_bg_grad_dir(bt_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(bt_btn, lv_color_hex(0x0052AC), 0);
    lv_obj_set_style_border_width(bt_btn, 1, 0);
    lv_obj_set_style_radius(bt_btn, 3, 0);
    lv_obj_t *bt_lbl = lv_label_create(bt_btn);
    lv_label_set_text(bt_lbl, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(bt_lbl, lv_color_white(), 0);
    lv_obj_center(bt_lbl);
    lv_obj_add_event_cb(bt_btn, photo_bt_share_cb, LV_EVENT_CLICKED, NULL);
    
    // Zoom In button
    lv_obj_t *zoomin_btn = lv_btn_create(toolbar);
    lv_obj_set_size(zoomin_btn, 32, 28);
    lv_obj_set_style_bg_color(zoomin_btn, lv_color_hex(0x50A050), 0);
    lv_obj_set_style_bg_grad_color(zoomin_btn, lv_color_hex(0x308030), 0);
    lv_obj_set_style_bg_grad_dir(zoomin_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(zoomin_btn, lv_color_hex(0x206020), 0);
    lv_obj_set_style_border_width(zoomin_btn, 1, 0);
    lv_obj_set_style_radius(zoomin_btn, 3, 0);
    lv_obj_t *zoomin_lbl = lv_label_create(zoomin_btn);
    lv_label_set_text(zoomin_lbl, "+");
    lv_obj_set_style_text_color(zoomin_lbl, lv_color_white(), 0);
    lv_obj_center(zoomin_lbl);
    lv_obj_add_event_cb(zoomin_btn, photo_zoom_in_cb, LV_EVENT_CLICKED, NULL);
    
    // Zoom Out button
    lv_obj_t *zoomout_btn = lv_btn_create(toolbar);
    lv_obj_set_size(zoomout_btn, 32, 28);
    lv_obj_set_style_bg_color(zoomout_btn, lv_color_hex(0xA05050), 0);
    lv_obj_set_style_bg_grad_color(zoomout_btn, lv_color_hex(0x803030), 0);
    lv_obj_set_style_bg_grad_dir(zoomout_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(zoomout_btn, lv_color_hex(0x602020), 0);
    lv_obj_set_style_border_width(zoomout_btn, 1, 0);
    lv_obj_set_style_radius(zoomout_btn, 3, 0);
    lv_obj_t *zoomout_lbl = lv_label_create(zoomout_btn);
    lv_label_set_text(zoomout_lbl, "-");
    lv_obj_set_style_text_color(zoomout_lbl, lv_color_white(), 0);
    lv_obj_center(zoomout_lbl);
    lv_obj_add_event_cb(zoomout_btn, photo_zoom_out_cb, LV_EVENT_CLICKED, NULL);
    
    // Rotate button
    lv_obj_t *rotate_btn = lv_btn_create(toolbar);
    lv_obj_set_size(rotate_btn, 32, 28);
    lv_obj_set_style_bg_color(rotate_btn, lv_color_hex(0x9050A0), 0);
    lv_obj_set_style_bg_grad_color(rotate_btn, lv_color_hex(0x703080), 0);
    lv_obj_set_style_bg_grad_dir(rotate_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(rotate_btn, lv_color_hex(0x502060), 0);
    lv_obj_set_style_border_width(rotate_btn, 1, 0);
    lv_obj_set_style_radius(rotate_btn, 3, 0);
    lv_obj_t *rotate_lbl = lv_label_create(rotate_btn);
    lv_label_set_text(rotate_lbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rotate_lbl, lv_color_white(), 0);
    lv_obj_center(rotate_lbl);
    lv_obj_add_event_cb(rotate_btn, photo_rotate_cb, LV_EVENT_CLICKED, NULL);
    
    // Info button
    lv_obj_t *info_btn = lv_btn_create(toolbar);
    lv_obj_set_size(info_btn, 32, 28);
    lv_obj_set_style_bg_color(info_btn, lv_color_hex(0x5080A0), 0);
    lv_obj_set_style_bg_grad_color(info_btn, lv_color_hex(0x306080), 0);
    lv_obj_set_style_bg_grad_dir(info_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(info_btn, lv_color_hex(0x204060), 0);
    lv_obj_set_style_border_width(info_btn, 1, 0);
    lv_obj_set_style_radius(info_btn, 3, 0);
    lv_obj_t *info_lbl = lv_label_create(info_btn);
    lv_label_set_text(info_lbl, "i");
    lv_obj_set_style_text_color(info_lbl, lv_color_white(), 0);
    lv_obj_center(info_lbl);
    lv_obj_add_event_cb(info_btn, photo_info_cb, LV_EVENT_CLICKED, NULL);
    
    // Image display area - white with shadow
    lv_obj_t *img_frame = lv_obj_create(photo_content);
    lv_obj_set_size(img_frame, lv_pct(95), 500);
    lv_obj_align(img_frame, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(img_frame, lv_color_white(), 0);
    lv_obj_set_style_border_color(img_frame, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(img_frame, 1, 0);
    lv_obj_set_style_radius(img_frame, 0, 0);
    lv_obj_set_style_shadow_width(img_frame, 8, 0);
    lv_obj_set_style_shadow_color(img_frame, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(img_frame, LV_OPA_20, 0);
    lv_obj_remove_flag(img_frame, LV_OBJ_FLAG_SCROLLABLE);
    
    photo_image = lv_image_create(img_frame);
    lv_obj_center(photo_image);
    lv_image_set_inner_align(photo_image, LV_IMAGE_ALIGN_CENTER);
    
    // Filename label
    photo_filename_label = lv_label_create(photo_content);
    lv_label_set_text(photo_filename_label, "Select source");
    lv_obj_set_style_text_color(photo_filename_label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(photo_filename_label, UI_FONT, 0);
    lv_obj_align(photo_filename_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    
    // Bottom navigation bar - Windows Vista style blue gradient
    lv_obj_t *nav_bar = lv_obj_create(photo_content);
    lv_obj_set_size(nav_bar, lv_pct(100), 50);
    lv_obj_align(nav_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav_bar, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(nav_bar, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(nav_bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(nav_bar, 0, 0);
    lv_obj_set_style_radius(nav_bar, 0, 0);
    lv_obj_set_flex_flow(nav_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(nav_bar, 30, 0);
    lv_obj_remove_flag(nav_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Prev button - Vista style
    lv_obj_t *prev_btn = lv_btn_create(nav_bar);
    lv_obj_set_size(prev_btn, 80, 36);
    lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0x5BA0E0), 0);
    lv_obj_set_style_bg_grad_color(prev_btn, lv_color_hex(0x3080C0), 0);
    lv_obj_set_style_bg_grad_dir(prev_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(prev_btn, lv_color_hex(0x2060A0), 0);
    lv_obj_set_style_border_width(prev_btn, 1, 0);
    lv_obj_set_style_radius(prev_btn, 4, 0);
    lv_obj_t *prev_lbl = lv_label_create(prev_btn);
    lv_label_set_text(prev_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(prev_lbl, lv_color_white(), 0);
    lv_obj_center(prev_lbl);
    lv_obj_add_event_cb(prev_btn, photo_prev_cb, LV_EVENT_CLICKED, NULL);
    
    // Center icon (photo icon)
    lv_obj_t *center_icon = lv_image_create(nav_bar);
    lv_image_set_src(center_icon, &img_photoview);
    
    // Next button - Vista style
    lv_obj_t *next_btn = lv_btn_create(nav_bar);
    lv_obj_set_size(next_btn, 80, 36);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x5BA0E0), 0);
    lv_obj_set_style_bg_grad_color(next_btn, lv_color_hex(0x3080C0), 0);
    lv_obj_set_style_bg_grad_dir(next_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(next_btn, lv_color_hex(0x2060A0), 0);
    lv_obj_set_style_border_width(next_btn, 1, 0);
    lv_obj_set_style_radius(next_btn, 4, 0);
    lv_obj_t *next_lbl = lv_label_create(next_btn);
    lv_label_set_text(next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(next_lbl, lv_color_white(), 0);
    lv_obj_center(next_lbl);
    lv_obj_add_event_cb(next_btn, photo_next_cb, LV_EVENT_CLICKED, NULL);
}


// ============ FLAPPY BIRD GAME ============

static lv_obj_t *game_content = NULL;
static lv_obj_t *bird_obj = NULL;
static lv_obj_t *pipe_top[3] = {NULL};
static lv_obj_t *pipe_bot[3] = {NULL};
static lv_obj_t *score_label = NULL;
static lv_obj_t *game_over_label = NULL;
// game_timer declared at top of file

static int bird_y = 300;
static int bird_velocity = 0;
static int pipe_x[3] = {500, 700, 900};
static int pipe_gap_y[3] = {250, 300, 200};
static int game_score = 0;
static bool game_running = false;
static bool game_over = false;

#define BIRD_SIZE 30
#define PIPE_WIDTH 60
#define PIPE_GAP 150
#define GRAVITY 2
#define JUMP_FORCE -18
#define PIPE_SPEED 5
#define GAME_AREA_HEIGHT 650

static void game_reset(void);
static void game_update(lv_timer_t *timer);

static void game_tap_cb(lv_event_t *e)
{
    if (game_over) {
        game_reset();
        return;
    }
    
    if (!game_running) {
        game_running = true;
        if (game_over_label) {
            lv_obj_add_flag(game_over_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    bird_velocity = JUMP_FORCE;
}

static void game_reset(void)
{
    bird_y = 300;
    bird_velocity = 0;
    game_score = 0;
    game_running = false;
    game_over = false;
    
    pipe_x[0] = 500;
    pipe_x[1] = 750;
    pipe_gap_y[0] = 250;
    pipe_gap_y[1] = 300;
    
    if (score_label) {
        lv_label_set_text(score_label, "0");
    }
    if (game_over_label) {
        lv_label_set_text(game_over_label, "Tap to Start");
        lv_obj_remove_flag(game_over_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (bird_obj) {
        lv_obj_set_pos(bird_obj, 100, bird_y);
    }
    
    // Reset pipes (only 2)
    for (int i = 0; i < 2; i++) {
        if (pipe_top[i]) {
            lv_obj_set_pos(pipe_top[i], pipe_x[i], pipe_gap_y[i] - PIPE_GAP/2 - 400);
        }
        if (pipe_bot[i]) {
            lv_obj_set_pos(pipe_bot[i], pipe_x[i], pipe_gap_y[i] + PIPE_GAP/2);
        }
    }
}

static void game_update(lv_timer_t *timer)
{
    if (!game_running || game_over || !game_content) return;
    
    // Update bird physics
    bird_velocity += GRAVITY;
    bird_y += bird_velocity;
    
    // Clamp bird position
    if (bird_y < 0) bird_y = 0;
    if (bird_y > GAME_AREA_HEIGHT - BIRD_SIZE) {
        bird_y = GAME_AREA_HEIGHT - BIRD_SIZE;
        game_over = true;
    }
    
    if (bird_obj) {
        lv_obj_set_y(bird_obj, bird_y);
    }
    
    // Update pipes (only 2 pipes to reduce load)
    for (int i = 0; i < 2; i++) {
        pipe_x[i] -= PIPE_SPEED;
        
        // Reset pipe when off screen
        if (pipe_x[i] < -PIPE_WIDTH) {
            pipe_x[i] = 500;
            pipe_gap_y[i] = 150 + (rand() % 300);
            game_score++;
            
            if (score_label) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", game_score);
                lv_label_set_text(score_label, buf);
            }
        }
        
        // Update pipe positions (simple position update, no expensive operations)
        if (pipe_top[i]) {
            lv_obj_set_pos(pipe_top[i], pipe_x[i], pipe_gap_y[i] - PIPE_GAP/2 - 400);
        }
        if (pipe_bot[i]) {
            lv_obj_set_pos(pipe_bot[i], pipe_x[i], pipe_gap_y[i] + PIPE_GAP/2);
        }
        
        // Collision detection
        int bird_x = 100;
        if (pipe_x[i] < bird_x + BIRD_SIZE && pipe_x[i] + PIPE_WIDTH > bird_x) {
            int gap_top = pipe_gap_y[i] - PIPE_GAP/2;
            int gap_bot = pipe_gap_y[i] + PIPE_GAP/2;
            
            if (bird_y < gap_top || bird_y + BIRD_SIZE > gap_bot) {
                game_over = true;
            }
        }
    }
    
    if (game_over && game_over_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Game Over! Score: %d", game_score);
        lv_label_set_text(game_over_label, buf);
        lv_obj_remove_flag(game_over_label, LV_OBJ_FLAG_HIDDEN);
        game_running = false;
    }
}

void app_flappy_create(void)
{
    ESP_LOGI(TAG, "Opening Flappy Bird");
    create_app_window("Flappy Bird");
    
    game_content = lv_obj_create(app_window);
    lv_obj_set_size(game_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(game_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_border_width(game_content, 0, 0);
    lv_obj_set_style_pad_all(game_content, 0, 0);
    lv_obj_remove_flag(game_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(game_content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(game_content, game_tap_cb, LV_EVENT_CLICKED, NULL);
    
    // Background image
    lv_obj_t *bg = lv_image_create(game_content);
    lv_image_set_src(bg, &img_flappy_background);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_image_set_inner_align(bg, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_align(bg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    
    // Ground
    lv_obj_t *ground = lv_obj_create(game_content);
    lv_obj_set_size(ground, lv_pct(100), 50);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0xDED895), 0);
    lv_obj_set_style_border_width(ground, 0, 0);
    lv_obj_remove_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ground, LV_OBJ_FLAG_CLICKABLE);
    
    // Create only 2 pipes with simple colored rectangles (no rotation = no watchdog)
    for (int i = 0; i < 2; i++) {
        int gap_y = pipe_gap_y[i];
        
        // Top pipe (simple green rectangle)
        pipe_top[i] = lv_obj_create(game_content);
        lv_obj_set_size(pipe_top[i], PIPE_WIDTH, 400);
        lv_obj_set_pos(pipe_top[i], pipe_x[i], gap_y - PIPE_GAP/2 - 400);
        lv_obj_set_style_bg_color(pipe_top[i], lv_color_hex(0x73BF2E), 0);
        lv_obj_set_style_border_color(pipe_top[i], lv_color_hex(0x558B2F), 0);
        lv_obj_set_style_border_width(pipe_top[i], 3, 0);
        lv_obj_set_style_radius(pipe_top[i], 0, 0);
        lv_obj_remove_flag(pipe_top[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pipe_top[i], LV_OBJ_FLAG_CLICKABLE);
        
        // Bottom pipe (simple green rectangle)
        pipe_bot[i] = lv_obj_create(game_content);
        lv_obj_set_size(pipe_bot[i], PIPE_WIDTH, 400);
        lv_obj_set_pos(pipe_bot[i], pipe_x[i], gap_y + PIPE_GAP/2);
        lv_obj_set_style_bg_color(pipe_bot[i], lv_color_hex(0x73BF2E), 0);
        lv_obj_set_style_border_color(pipe_bot[i], lv_color_hex(0x558B2F), 0);
        lv_obj_set_style_border_width(pipe_bot[i], 3, 0);
        lv_obj_set_style_radius(pipe_bot[i], 0, 0);
        lv_obj_remove_flag(pipe_bot[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pipe_bot[i], LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Bird sprite (using flappy icon)
    bird_obj = lv_image_create(game_content);
    lv_image_set_src(bird_obj, &img_flappy);
    lv_obj_set_pos(bird_obj, 100, bird_y);
    lv_obj_remove_flag(bird_obj, LV_OBJ_FLAG_CLICKABLE);
    
    // Score label
    score_label = lv_label_create(game_content);
    lv_label_set_text(score_label, "0");
    lv_obj_set_style_text_color(score_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(score_label, UI_FONT, 0);
    lv_obj_align(score_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Game over / start label
    game_over_label = lv_label_create(game_content);
    lv_label_set_text(game_over_label, "Tap to Start");
    lv_obj_set_style_text_color(game_over_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(game_over_label, UI_FONT, 0);
    lv_obj_center(game_over_label);
    
    // Start game timer (15 FPS - slower to avoid watchdog on ESP32)
    game_timer = lv_timer_create(game_update, 66, NULL);
    
    game_reset();
}


// ============ RECYCLE BIN APP ============

static lv_obj_t *trash_content = NULL;

static void trash_item_restore_cb(lv_event_t *e)
{
    const char *filename = (const char *)lv_event_get_user_data(e);
    if (!filename) return;
    
    char trash_path[384];
    snprintf(trash_path, sizeof(trash_path), "%s/%s", TRASH_PATH, filename);
    
    // Restore to littlefs root
    char restore_path[384];
    snprintf(restore_path, sizeof(restore_path), "/littlefs/%s", filename);
    
    if (rename(trash_path, restore_path) == 0) {
        ESP_LOGI(TAG, "Restored: %s", filename);
        // Refresh trash view
        app_recycle_bin_create();
    } else {
        ESP_LOGE(TAG, "Failed to restore: %s", filename);
    }
}

static void trash_item_delete_cb(lv_event_t *e)
{
    const char *filename = (const char *)lv_event_get_user_data(e);
    if (!filename) return;
    
    char trash_path[384];
    snprintf(trash_path, sizeof(trash_path), "%s/%s", TRASH_PATH, filename);
    
    if (remove(trash_path) == 0) {
        ESP_LOGI(TAG, "Permanently deleted: %s", filename);
        // Refresh trash view
        app_recycle_bin_create();
    } else {
        ESP_LOGE(TAG, "Failed to delete: %s", filename);
    }
}

static void trash_empty_all_cb(lv_event_t *e)
{
    empty_trash();
    // Refresh trash view
    app_recycle_bin_create();
}

void app_recycle_bin_create(void)
{
    ESP_LOGI(TAG, "Opening Recycle Bin");
    create_app_window("Recycle Bin");
    
    ensure_trash_exists();
    
    // Content area
    trash_content = lv_obj_create(app_window);
    lv_obj_set_size(trash_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(trash_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(trash_content, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(trash_content, 0, 0);
    lv_obj_set_style_pad_all(trash_content, 0, 0);
    lv_obj_set_flex_flow(trash_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(trash_content, 0, 0);
    
    // Toolbar
    lv_obj_t *toolbar = lv_obj_create(trash_content);
    lv_obj_set_size(toolbar, lv_pct(100), 50);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_color(toolbar, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_dir(toolbar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_color(toolbar, lv_color_hex(0xB8D4F0), 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(toolbar, 0, 0);
    lv_obj_set_style_pad_left(toolbar, 10, 0);
    lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Trash icon
    lv_obj_t *trash_icon = lv_image_create(toolbar);
    int trash_count = count_trash_items();
    lv_image_set_src(trash_icon, trash_count > 0 ? &img_trashbinfull : &img_trashbinempty);
    lv_obj_align(trash_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(toolbar);
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Recycle Bin (%d items)", trash_count);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 55, 0);
    
    // Empty Trash button
    if (trash_count > 0) {
        lv_obj_t *empty_btn = lv_btn_create(toolbar);
        lv_obj_set_size(empty_btn, 120, 35);
        lv_obj_align(empty_btn, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_bg_color(empty_btn, lv_color_hex(0xCC4444), 0);
        lv_obj_set_style_radius(empty_btn, 4, 0);
        lv_obj_add_event_cb(empty_btn, trash_empty_all_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *empty_lbl = lv_label_create(empty_btn);
        lv_label_set_text(empty_lbl, "Empty Trash");
        lv_obj_set_style_text_color(empty_lbl, lv_color_white(), 0);
        lv_obj_center(empty_lbl);
    }
    
    // File list
    lv_obj_t *file_list = lv_obj_create(trash_content);
    lv_obj_set_size(file_list, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4 - 60);
    lv_obj_set_style_bg_color(file_list, lv_color_white(), 0);
    lv_obj_set_style_border_width(file_list, 0, 0);
    lv_obj_set_style_pad_all(file_list, 8, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(file_list, 4, 0);
    
    // List trash items
    DIR *dir = opendir(TRASH_PATH);
    if (!dir) {
        lv_obj_t *empty_lbl = lv_label_create(file_list);
        lv_label_set_text(empty_lbl, "Recycle Bin is empty");
        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x888888), 0);
        return;
    }
    
    struct dirent *entry;
    static char trash_filenames[20][256];  // Store filenames for callbacks (d_name max is 255)
    int file_idx = 0;
    
    while ((entry = readdir(dir)) != NULL && file_idx < 20) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Use snprintf instead of strncpy to avoid truncation warning
        snprintf(trash_filenames[file_idx], sizeof(trash_filenames[0]), "%s", entry->d_name);
        
        // Create item row
        lv_obj_t *item = lv_obj_create(file_list);
        lv_obj_set_size(item, lv_pct(100), 45);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xF8F8F8), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_left(item, 10, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // File icon
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, &img_file);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        
        // Filename
        lv_obj_t *name = lv_label_create(item);
        lv_label_set_text(name, entry->d_name);
        lv_obj_set_style_text_color(name, lv_color_black(), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_set_width(name, 200);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        
        // Restore button
        lv_obj_t *restore_btn = lv_btn_create(item);
        lv_obj_set_size(restore_btn, 70, 30);
        lv_obj_align(restore_btn, LV_ALIGN_RIGHT_MID, -80, 0);
        lv_obj_set_style_bg_color(restore_btn, lv_color_hex(0x4A90D9), 0);
        lv_obj_set_style_radius(restore_btn, 4, 0);
        lv_obj_add_event_cb(restore_btn, trash_item_restore_cb, LV_EVENT_CLICKED, trash_filenames[file_idx]);
        
        lv_obj_t *restore_lbl = lv_label_create(restore_btn);
        lv_label_set_text(restore_lbl, "Restore");
        lv_obj_set_style_text_color(restore_lbl, lv_color_white(), 0);
        lv_obj_center(restore_lbl);
        
        // Delete button
        lv_obj_t *del_btn = lv_btn_create(item);
        lv_obj_set_size(del_btn, 60, 30);
        lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xCC4444), 0);
        lv_obj_set_style_radius(del_btn, 4, 0);
        lv_obj_add_event_cb(del_btn, trash_item_delete_cb, LV_EVENT_CLICKED, trash_filenames[file_idx]);
        
        lv_obj_t *del_lbl = lv_label_create(del_btn);
        lv_label_set_text(del_lbl, "Delete");
        lv_obj_set_style_text_color(del_lbl, lv_color_white(), 0);
        lv_obj_center(del_lbl);
        
        file_idx++;
    }
    closedir(dir);
    
    if (file_idx == 0) {
        lv_obj_t *empty_lbl = lv_label_create(file_list);
        lv_label_set_text(empty_lbl, "Recycle Bin is empty");
        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x888888), 0);
    }
}

// ============ PAINT APP ============

// Paint state
static lv_obj_t *paint_canvas = NULL;
static int paint_brush_size = 8;
static uint32_t paint_color = 0x000000;  // Black
static int paint_tool = 0;  // 0=brush, 1=line, 2=rect, 3=circle, 4=fill
static int32_t paint_start_x = 0;
static int32_t paint_start_y = 0;
static bool paint_drawing = false;
static lv_obj_t *paint_preview = NULL;  // Preview shape while drawing

static void paint_draw_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *canvas = (lv_obj_t *)lv_event_get_target(e);
    
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    
    // Get canvas absolute position
    lv_area_t canvas_area;
    lv_obj_get_coords(canvas, &canvas_area);
    
    int32_t rel_x = point.x - canvas_area.x1;
    int32_t rel_y = point.y - canvas_area.y1;
    
    if (code == LV_EVENT_PRESSED) {
        paint_start_x = rel_x;
        paint_start_y = rel_y;
        paint_drawing = true;
        
        // For brush tool, start drawing immediately
        if (paint_tool == 0) {
            lv_obj_t *dot = lv_obj_create(canvas);
            lv_obj_set_size(dot, paint_brush_size, paint_brush_size);
            lv_obj_set_pos(dot, rel_x - paint_brush_size/2, rel_y - paint_brush_size/2);
            lv_obj_set_style_bg_color(dot, lv_color_hex(paint_color), 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        }
        // For fill tool, fill entire canvas
        else if (paint_tool == 4) {
            lv_obj_set_style_bg_color(canvas, lv_color_hex(paint_color), 0);
            paint_drawing = false;
        }
    }
    else if (code == LV_EVENT_PRESSING && paint_drawing) {
        // Brush tool - continuous drawing
        if (paint_tool == 0) {
            if (rel_x >= 0 && rel_x < lv_obj_get_width(canvas) && 
                rel_y >= 0 && rel_y < lv_obj_get_height(canvas)) {
                lv_obj_t *dot = lv_obj_create(canvas);
                lv_obj_set_size(dot, paint_brush_size, paint_brush_size);
                lv_obj_set_pos(dot, rel_x - paint_brush_size/2, rel_y - paint_brush_size/2);
                lv_obj_set_style_bg_color(dot, lv_color_hex(paint_color), 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }
    else if (code == LV_EVENT_RELEASED && paint_drawing) {
        paint_drawing = false;
        
        int32_t w = rel_x - paint_start_x;
        int32_t h = rel_y - paint_start_y;
        
        // Line tool
        if (paint_tool == 1) {
            // Draw line using multiple dots
            int steps = (abs(w) > abs(h)) ? abs(w) : abs(h);
            if (steps < 1) steps = 1;
            for (int i = 0; i <= steps; i += 2) {
                int32_t x = paint_start_x + (w * i / steps);
                int32_t y = paint_start_y + (h * i / steps);
                lv_obj_t *dot = lv_obj_create(canvas);
                lv_obj_set_size(dot, paint_brush_size, paint_brush_size);
                lv_obj_set_pos(dot, x - paint_brush_size/2, y - paint_brush_size/2);
                lv_obj_set_style_bg_color(dot, lv_color_hex(paint_color), 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            }
        }
        // Rectangle tool
        else if (paint_tool == 2) {
            int32_t x = (w > 0) ? paint_start_x : rel_x;
            int32_t y = (h > 0) ? paint_start_y : rel_y;
            lv_obj_t *rect = lv_obj_create(canvas);
            lv_obj_set_size(rect, abs(w), abs(h));
            lv_obj_set_pos(rect, x, y);
            lv_obj_set_style_bg_color(rect, lv_color_hex(paint_color), 0);
            lv_obj_set_style_border_width(rect, 0, 0);
            lv_obj_set_style_radius(rect, 0, 0);
            lv_obj_remove_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(rect, LV_OBJ_FLAG_CLICKABLE);
        }
        // Circle tool
        else if (paint_tool == 3) {
            int32_t radius = (int32_t)sqrt(w*w + h*h);
            lv_obj_t *circle = lv_obj_create(canvas);
            lv_obj_set_size(circle, radius * 2, radius * 2);
            lv_obj_set_pos(circle, paint_start_x - radius, paint_start_y - radius);
            lv_obj_set_style_bg_color(circle, lv_color_hex(paint_color), 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

void app_paint_create(void)
{
    close_app_window();
    
    app_window = lv_obj_create(scr_desktop);
    lv_obj_set_size(app_window, SCREEN_WIDTH - 10, SCREEN_HEIGHT - TASKBAR_HEIGHT - 10);
    lv_obj_align(app_window, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(app_window, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_color(app_window, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(app_window, 2, 0);
    lv_obj_set_style_radius(app_window, 6, 0);
    lv_obj_set_style_pad_all(app_window, 0, 0);
    lv_obj_remove_flag(app_window, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(app_window);
    lv_obj_set_size(title_bar, lv_pct(100), 28);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_left(title_bar, 8, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(title_bar);
    lv_label_set_text(title, "Paint");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 24, 20);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(close_btn, 3, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) { close_app_window(); }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_center(close_label);
    
    // Toolbar - scrollable horizontally
    lv_obj_t *toolbar = lv_obj_create(app_window);
    lv_obj_set_size(toolbar, lv_pct(100), 50);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(toolbar, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(toolbar, 0, 0);
    lv_obj_set_style_pad_all(toolbar, 4, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(toolbar, 4, 0);
    lv_obj_set_scroll_dir(toolbar, LV_DIR_HOR);  // Enable horizontal scroll
    lv_obj_set_scrollbar_mode(toolbar, LV_SCROLLBAR_MODE_AUTO);
    // Scrollable by default - don't remove flag
    
    // Tool buttons
    static const char* tool_names[] = {"Brush", "Line", "Rect", "Circle", "Fill"};
    static lv_obj_t *tool_btns[5];
    
    for (int i = 0; i < 5; i++) {
        tool_btns[i] = lv_btn_create(toolbar);
        lv_obj_set_size(tool_btns[i], 50, 36);
        lv_obj_set_style_bg_color(tool_btns[i], (i == paint_tool) ? lv_color_hex(0x0054E3) : lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_radius(tool_btns[i], 4, 0);
        
        lv_obj_t *lbl = lv_label_create(tool_btns[i]);
        lv_label_set_text(lbl, tool_names[i]);
        lv_obj_set_style_text_color(lbl, (i == paint_tool) ? lv_color_white() : lv_color_black(), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_center(lbl);
        
        lv_obj_add_event_cb(tool_btns[i], [](lv_event_t *e) {
            int tool_idx = (int)(intptr_t)lv_event_get_user_data(e);
            paint_tool = tool_idx;
            // Update button colors
            lv_obj_t *parent = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
            for (int j = 0; j < 5; j++) {
                lv_obj_t *btn = lv_obj_get_child(parent, j);
                lv_obj_set_style_bg_color(btn, (j == paint_tool) ? lv_color_hex(0x0054E3) : lv_color_hex(0xCCCCCC), 0);
                lv_obj_t *lbl = lv_obj_get_child(btn, 0);
                lv_obj_set_style_text_color(lbl, (j == paint_tool) ? lv_color_white() : lv_color_black(), 0);
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    
    // Brush size slider
    lv_obj_t *size_cont = lv_obj_create(toolbar);
    lv_obj_set_size(size_cont, 80, 36);
    lv_obj_set_style_bg_opa(size_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(size_cont, 0, 0);
    lv_obj_set_style_pad_all(size_cont, 0, 0);
    lv_obj_remove_flag(size_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *size_lbl = lv_label_create(size_cont);
    lv_label_set_text(size_lbl, "Size:");
    lv_obj_set_style_text_font(size_lbl, UI_FONT, 0);
    lv_obj_align(size_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *size_slider = lv_slider_create(size_cont);
    lv_obj_set_size(size_slider, 70, 12);
    lv_obj_align(size_slider, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_slider_set_range(size_slider, 2, 30);
    lv_slider_set_value(size_slider, paint_brush_size, LV_ANIM_OFF);
    lv_obj_add_event_cb(size_slider, [](lv_event_t *e) {
        paint_brush_size = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Color buttons
    static uint32_t colors[] = {0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    for (int i = 0; i < 8; i++) {
        lv_obj_t *color_btn = lv_obj_create(toolbar);
        lv_obj_set_size(color_btn, 28, 28);
        lv_obj_set_style_bg_color(color_btn, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_border_width(color_btn, (colors[i] == paint_color) ? 3 : 1, 0);
        lv_obj_set_style_border_color(color_btn, (colors[i] == paint_color) ? lv_color_hex(0x0054E3) : lv_color_hex(0x888888), 0);
        lv_obj_set_style_radius(color_btn, 4, 0);
        lv_obj_add_flag(color_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(color_btn, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_add_event_cb(color_btn, [](lv_event_t *e) {
            uint32_t color = (uint32_t)(intptr_t)lv_event_get_user_data(e);
            paint_color = color;
            // Update border on all color buttons
            lv_obj_t *parent = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
            uint32_t child_cnt = lv_obj_get_child_count(parent);
            for (uint32_t j = 0; j < child_cnt; j++) {
                lv_obj_t *child = lv_obj_get_child(parent, j);
                if (lv_obj_get_width(child) == 28) {  // Color button
                    lv_color_t bg = lv_obj_get_style_bg_color(child, 0);
                    uint32_t c = (lv_color_to_u32(bg) & 0xFFFFFF);
                    lv_obj_set_style_border_width(child, (c == paint_color) ? 3 : 1, 0);
                    lv_obj_set_style_border_color(child, (c == paint_color) ? lv_color_hex(0x0054E3) : lv_color_hex(0x888888), 0);
                }
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)colors[i]);
    }
    
    // Clear button
    lv_obj_t *clear_btn = lv_btn_create(toolbar);
    lv_obj_set_size(clear_btn, 50, 36);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(clear_btn, 4, 0);
    
    lv_obj_t *clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, "Clear");
    lv_obj_set_style_text_color(clear_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(clear_lbl, UI_FONT, 0);
    lv_obj_center(clear_lbl);
    
    lv_obj_add_event_cb(clear_btn, [](lv_event_t *e) {
        if (paint_canvas) {
            // Delete all children (drawn objects)
            lv_obj_clean(paint_canvas);
            lv_obj_set_style_bg_color(paint_canvas, lv_color_white(), 0);
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Canvas area
    paint_canvas = lv_obj_create(app_window);
    lv_obj_set_size(paint_canvas, SCREEN_WIDTH - 20, SCREEN_HEIGHT - TASKBAR_HEIGHT - 100);
    lv_obj_align(paint_canvas, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_color(paint_canvas, lv_color_white(), 0);
    lv_obj_set_style_border_width(paint_canvas, 1, 0);
    lv_obj_set_style_border_color(paint_canvas, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(paint_canvas, 0, 0);
    lv_obj_set_style_pad_all(paint_canvas, 0, 0);
    lv_obj_add_flag(paint_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(paint_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add drawing events
    lv_obj_add_event_cb(paint_canvas, paint_draw_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(paint_canvas, paint_draw_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(paint_canvas, paint_draw_cb, LV_EVENT_RELEASED, NULL);
    
    ESP_LOGI(TAG, "Paint app created");
}


// ============ MY COMPUTER WITH PATH ============

void app_my_computer_open_path(const char *folder_name)
{
    ESP_LOGI(TAG, "Opening My Computer with folder: %s", folder_name);
    
    // Create the folder if it doesn't exist
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/littlefs/%s", folder_name);
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        // Create folder
        mkdir(full_path, 0755);
        ESP_LOGI(TAG, "Created folder: %s", full_path);
    }
    
    // Open My Computer
    app_my_computer_create();
    
    // Navigate to the folder - use snprintf instead of strncpy
    snprintf(mycomp_current_path, sizeof(mycomp_current_path), "%s", full_path);
    mycomp_browse_path(full_path);
}


// ============ DEFAULT PROGRAMS APP ============

void app_default_programs_create(void)
{
    ESP_LOGI(TAG, "Opening Default Programs");
    create_app_window("Programs");
    
    // Content area
    lv_obj_t *content = lv_obj_create(app_window);
    lv_obj_set_size(content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(content, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(content, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 6, 0);
    
    // Header
    lv_obj_t *header = lv_label_create(content);
    lv_label_set_text(header, "Installed Programs");
    lv_obj_set_style_text_color(header, lv_color_hex(0x1A5090), 0);
    lv_obj_set_style_text_font(header, UI_FONT, 0);
    
    // Programs list with estimated sizes
    static const struct {
        const char *name;
        const char *size;
        const lv_image_dsc_t *icon;
    } programs[] = {
        {"Calculator", "~50 KB", &img_calculator},
        {"Camera", "~80 KB", &img_camera},
        {"Clock", "~45 KB", &img_clock},
        {"Flappy Bird", "~60 KB", &img_flappy},
        {"My Computer", "~70 KB", &img_my_computer},
        {"Notepad", "~40 KB", &img_notepad},
        {"Paint", "~55 KB", &img_paint},
        {"Photo Viewer", "~65 KB", &img_photoview},
        {"Settings", "~90 KB", &img_settings},
        {"Weather", "~75 KB", &img_weather},
    };
    
    // Scrollable list
    lv_obj_t *list = lv_obj_create(content);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 4, 0);
    lv_obj_set_style_pad_all(list, 5, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    
    for (int i = 0; i < 10; i++) {
        lv_obj_t *item = lv_obj_create(list);
        lv_obj_set_size(item, lv_pct(100), 50);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_border_color(item, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Icon
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, programs[i].icon);
        lv_image_set_scale(icon, 160);  // ~32px from 48px
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        
        // Name
        lv_obj_t *name = lv_label_create(item);
        lv_label_set_text(name, programs[i].name);
        lv_obj_set_style_text_color(name, lv_color_black(), 0);
        lv_obj_set_style_text_font(name, UI_FONT, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 45, -8);
        
        // Size
        lv_obj_t *size = lv_label_create(item);
        lv_label_set_text(size, programs[i].size);
        lv_obj_set_style_text_color(size, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(size, UI_FONT, 0);
        lv_obj_align(size, LV_ALIGN_LEFT_MID, 45, 10);
    }
    
    // Total size
    lv_obj_t *total = lv_label_create(content);
    lv_label_set_text(total, "Total: ~630 KB (estimated)");
    lv_obj_set_style_text_color(total, lv_color_hex(0x666666), 0);
}


// ============ HELP AND SUPPORT APP ============

void app_help_create(void)
{
    ESP_LOGI(TAG, "Opening Help and Support");
    create_app_window("Help and Support");
    
    // Content area
    lv_obj_t *content = lv_obj_create(app_window);
    lv_obj_set_size(content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(content, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(content, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 15, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 10, 0);
    
    // Header with icon
    lv_obj_t *header_cont = lv_obj_create(content);
    lv_obj_set_size(header_cont, lv_pct(100), 60);
    lv_obj_set_style_bg_color(header_cont, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(header_cont, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(header_cont, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(header_cont, 0, 0);
    lv_obj_set_style_radius(header_cont, 6, 0);
    lv_obj_set_style_pad_all(header_cont, 10, 0);
    lv_obj_remove_flag(header_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *header_icon = lv_image_create(header_cont);
    lv_image_set_src(header_icon, &img_information);
    lv_obj_align(header_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *header_text = lv_label_create(header_cont);
    lv_label_set_text(header_text, "WinEsp32 Help Center");
    lv_obj_set_style_text_color(header_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(header_text, UI_FONT, 0);
    lv_obj_align(header_text, LV_ALIGN_LEFT_MID, 55, 0);
    
    // Scrollable help content
    lv_obj_t *help_scroll = lv_obj_create(content);
    lv_obj_set_size(help_scroll, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(help_scroll, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(help_scroll, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(help_scroll, 1, 0);
    lv_obj_set_style_radius(help_scroll, 4, 0);
    lv_obj_set_style_pad_all(help_scroll, 12, 0);
    lv_obj_set_flex_flow(help_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(help_scroll, 12, 0);
    
    // Help sections
    auto add_section = [&](const char *title, const char *text) {
        lv_obj_t *section = lv_obj_create(help_scroll);
        lv_obj_set_size(section, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xF8F8F8), 0);
        lv_obj_set_style_border_color(section, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_border_width(section, 1, 0);
        lv_obj_set_style_radius(section, 4, 0);
        lv_obj_set_style_pad_all(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(section, 5, 0);
        lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *title_lbl = lv_label_create(section);
        lv_label_set_text(title_lbl, title);
        lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x1A5090), 0);
        lv_obj_set_style_text_font(title_lbl, UI_FONT, 0);
        
        lv_obj_t *text_lbl = lv_label_create(section);
        lv_label_set_text(text_lbl, text);
        lv_obj_set_style_text_color(text_lbl, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(text_lbl, UI_FONT, 0);
        lv_obj_set_width(text_lbl, lv_pct(100));
        lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_WRAP);
    };
    
    add_section("Device Info",
        "ESP32-P4 based PDA\n"
        "Display: 4.8\" 480x800 IPS\n"
        "Touch: GT911 Capacitive\n"
        "Storage: LittleFS on Flash");
    
    add_section("Getting Started",
        "1. Tap Start button to open menu\n"
        "2. Select apps from left column\n"
        "3. Use right column for folders\n"
        "4. Swipe slider to unlock screen");
    
    add_section("Power Management",
        "Sleep: Dims screen (AOD mode)\n"
        "Lock: Shows lock screen\n"
        "Shutdown: Turns off display\n"
        "Tap AOD to wake device");
    
    add_section("WiFi Setup",
        "1. Open Settings app\n"
        "2. Tap WiFi section\n"
        "3. Scan for networks\n"
        "4. Enter password to connect");
    
    add_section("File Management",
        "Use My Computer to browse files.\n"
        "Long press for context menu.\n"
        "Deleted files go to Recycle Bin.");
}

// ============ APP LAUNCHER ============

void app_launch(const char* app_name)
{
    ESP_LOGI(TAG, "Launching app: %s", app_name);
    
    if (strcmp(app_name, "calculator") == 0) {
        app_calculator_create();
    } else if (strcmp(app_name, "clock") == 0) {
        app_clock_create();
    } else if (strcmp(app_name, "weather") == 0) {
        app_weather_create();
    } else if (strcmp(app_name, "settings") == 0) {
        app_settings_create();
    } else if (strcmp(app_name, "notepad") == 0) {
        app_notepad_create();
    } else if (strcmp(app_name, "camera") == 0) {
        app_camera_create();
    } else if (strcmp(app_name, "my_computer") == 0) {
        app_my_computer_create();
    } else if (strcmp(app_name, "photos") == 0) {
        app_photo_viewer_create();
    } else if (strcmp(app_name, "flappy") == 0) {
        app_flappy_create();
    } else if (strcmp(app_name, "recycle_bin") == 0) {
        app_recycle_bin_create();
    } else if (strcmp(app_name, "paint") == 0) {
        app_paint_create();
    } else if (strcmp(app_name, "console") == 0) {
        app_console_create();
    } else if (strcmp(app_name, "default_programs") == 0) {
        app_default_programs_create();
    } else if (strcmp(app_name, "help") == 0) {
        app_help_create();
    } else if (strcmp(app_name, "voice_recorder") == 0) {
        app_voice_recorder_create();
    } else if (strcmp(app_name, "system_monitor") == 0) {
        app_system_monitor_create();
    } else if (strcmp(app_name, "snake") == 0) {
        app_snake_create();
    } else if (strcmp(app_name, "js_ide") == 0) {
        app_js_ide_create();
    } else if (strcmp(app_name, "tetris") == 0) {
        app_tetris_create();
    } else if (strcmp(app_name, "game2048") == 0) {
        app_2048_create();
    } else if (strcmp(app_name, "minesweeper") == 0) {
        app_minesweeper_create();
    } else if (strcmp(app_name, "tictactoe") == 0) {
        app_tictactoe_create();
    } else if (strcmp(app_name, "memory") == 0) {
        app_memory_create();
    } else if (strcmp(app_name, "my_computer_documents") == 0) {
        app_my_computer_open_path("Documents");
    } else if (strcmp(app_name, "my_computer_pictures") == 0) {
        app_my_computer_open_path("Pictures");
    } else if (strcmp(app_name, "my_computer_games") == 0) {
        app_my_computer_open_path("Games");
    } else {
        ESP_LOGW(TAG, "Unknown app: %s", app_name);
    }
}

// ============ CONSOLE APP ============

static lv_obj_t *console_output = NULL;
static lv_obj_t *console_input = NULL;
static lv_obj_t *console_keyboard = NULL;
static lv_obj_t *console_window = NULL;  // For fullscreen mode
static char console_buffer[8192] = {0};
static int console_buffer_len = 0;
static char console_cwd[128] = "/littlefs";  // Current working directory
static bool console_fullscreen = false;

// Console color scheme (can be changed with 'color' command)
static uint32_t console_bg_color = 0x0C0C0C;
static uint32_t console_text_color = 0x00FF00;
static uint32_t console_prompt_color = 0xFFFF00;

// Color presets (like Windows CMD)
static const uint32_t color_presets[][2] = {
    {0x0C0C0C, 0x00FF00},  // 0: Black/Green (default)
    {0x0C0C0C, 0xFFFFFF},  // 1: Black/White
    {0x000080, 0xFFFF00},  // 2: Navy/Yellow
    {0x000000, 0x00FFFF},  // 3: Black/Cyan
    {0x800000, 0xFFFFFF},  // 4: Maroon/White
    {0x008000, 0xFFFFFF},  // 5: Green/White
    {0x000080, 0xFFFFFF},  // 6: Navy/White
    {0x0C0C0C, 0xFF0000},  // 7: Black/Red
    {0x1A1A2E, 0x00FF00},  // 8: Dark Blue/Green (Matrix)
    {0x282A36, 0xF8F8F2},  // 9: Dracula theme
};

// Forward declarations
static void app_console_create_fullscreen(void);

static void console_print(const char *text)
{
    if (!text || !console_output) return;
    
    int len = strlen(text);
    if (console_buffer_len + len < sizeof(console_buffer) - 1) {
        strcat(console_buffer, text);
        console_buffer_len += len;
    } else {
        // Buffer full, shift content
        int shift = len + 512;
        if (shift < console_buffer_len) {
            memmove(console_buffer, console_buffer + shift, console_buffer_len - shift);
            console_buffer_len -= shift;
            console_buffer[console_buffer_len] = '\0';
            strcat(console_buffer, text);
            console_buffer_len += len;
        }
    }
    
    lv_textarea_set_text(console_output, console_buffer);
    lv_textarea_set_cursor_pos(console_output, LV_TEXTAREA_CURSOR_LAST);
}

static void console_clear(void)
{
    console_buffer[0] = '\0';
    console_buffer_len = 0;
    if (console_output) {
        lv_textarea_set_text(console_output, "");
    }
}

static void console_fastfetch(void)
{
    // ESP32 ASCII art logo (clean, no ANSI codes)
    console_print("\n");
    console_print("   ______  _____ _____  ____  ___  \n");
    console_print("  |  ____|/ ____|  __ \\|___ \\|__ \\ \n");
    console_print("  | |__  | (___ | |__) | __) |  ) |\n");
    console_print("  |  __|  \\___ \\|  ___/ |__ <  / / \n");
    console_print("  | |____ ____) | |     ___) |/ /_ \n");
    console_print("  |______|_____/|_|    |____/|____|\n");
    console_print("         P4 - Win32 OS\n");
    console_print("\n");
    
    // Get system info
    char buf[256];
    
    // Username (no ANSI codes - LVGL doesn't support them)
    console_print("  user@esp32\n");
    console_print("  ----------------\n");
    
    // OS
    console_print("  OS: Win32 OS (ESP32-P4)\n");
    
    // Kernel
    snprintf(buf, sizeof(buf), "  Kernel: ESP-IDF %s\n", esp_get_idf_version());
    console_print(buf);
    
    // Uptime
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t uptime_sec = uptime_ms / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    snprintf(buf, sizeof(buf), "  Uptime: %luh %lum\n", (unsigned long)hours, (unsigned long)mins);
    console_print(buf);
    
    // Shell
    console_print("  Shell: win32sh 1.0\n");
    
    // Resolution
    console_print("  Resolution: 480x800\n");
    
    // CPU
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    snprintf(buf, sizeof(buf), "  CPU: ESP32-P4 (%d cores)\n", chip_info.cores);
    console_print(buf);
    
    // Memory
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    snprintf(buf, sizeof(buf), "  Memory: %luKB / %luKB\n", 
             (unsigned long)(total_heap - free_heap) / 1024,
             (unsigned long)total_heap / 1024);
    console_print(buf);
    
    // PSRAM
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    snprintf(buf, sizeof(buf), "  PSRAM: %luMB / %luMB\n",
             (unsigned long)(total_psram - free_psram) / (1024*1024),
             (unsigned long)total_psram / (1024*1024));
    console_print(buf);
    
    console_print("\n");
}

static void console_cmd_help(void)
{
    console_print(
        "Win32 Console Commands:\n"
        "\n"
        "=== File Operations ===\n"
        "  ls/dir [path]    - List directory\n"
        "  cd <path>        - Change directory\n"
        "  pwd              - Print working directory\n"
        "  cat/type <file>  - Show file content\n"
        "  touch <file>     - Create empty file\n"
        "  rm/del <file>    - Delete file\n"
        "  mkdir <dir>      - Create directory\n"
        "  rmdir <dir>      - Remove directory\n"
        "  mv/ren <s> <d>   - Move/rename file\n"
        "  cp/copy <s> <d>  - Copy file\n"
        "  echo <text> > f  - Write text to file\n"
        "\n"
        "=== System Info ===\n"
        "  fastfetch        - Show system info\n"
        "  free             - Show memory info\n"
        "  uptime           - Show uptime\n"
        "  df               - Show disk usage\n"
        "  ps               - List tasks\n"
        "  whoami           - Show current user\n"
        "  hostname         - Show hostname\n"
        "  date             - Show date/time\n"
        "\n"
        "=== Network ===\n"
        "  ping <host>      - Ping host\n"
        "  curl <url>       - HTTP GET request\n"
        "  ifconfig         - Show network info\n"
        "  wifi             - Show WiFi status\n"
        "\n"
        "=== Console ===\n"
        "  clear/cls        - Clear screen\n"
        "  color <0-9>      - Change color scheme\n"
        "  fscreen          - Toggle fullscreen\n"
        "  history          - Show command history\n"
        "  reboot           - Reboot system\n"
        "  exit             - Close console\n"
        "  help             - Show this help\n"
    );
}

// Helper to build full path safely
static void console_build_path(char *dest, size_t dest_size, const char *path)
{
    if (!path || strlen(path) == 0) {
        strncpy(dest, console_cwd, dest_size - 1);
    } else if (path[0] == '/') {
        strncpy(dest, path, dest_size - 1);
    } else {
        snprintf(dest, dest_size, "%.60s/%.60s", console_cwd, path);
    }
    dest[dest_size - 1] = '\0';
}

static void console_cmd_ls(const char *path)
{
    char dir_path[160];
    console_build_path(dir_path, sizeof(dir_path), path);
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Cannot open: %.120s\n", dir_path);
        console_print(buf);
        return;
    }
    
    char buf[320];
    snprintf(buf, sizeof(buf), " Directory of %.120s\n\n", dir_path);
    console_print(buf);
    
    int file_count = 0, dir_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            snprintf(buf, sizeof(buf), "  <DIR>     %.100s\n", entry->d_name);
            dir_count++;
        } else {
            // Try to get file size
            char full_path[320];
            snprintf(full_path, sizeof(full_path), "%.150s/%.100s", dir_path, entry->d_name);
            struct stat st;
            if (stat(full_path, &st) == 0) {
                snprintf(buf, sizeof(buf), "  %8ld  %.100s\n", (long)st.st_size, entry->d_name);
            } else {
                snprintf(buf, sizeof(buf), "            %.100s\n", entry->d_name);
            }
            file_count++;
        }
        console_print(buf);
    }
    closedir(dir);
    
    snprintf(buf, sizeof(buf), "\n  %d File(s), %d Dir(s)\n", file_count, dir_count);
    console_print(buf);
}

static void console_cmd_cat(const char *filepath)
{
    if (!filepath || strlen(filepath) == 0) {
        console_print("Usage: cat <filename>\n");
        return;
    }
    
    char full_path[160];
    console_build_path(full_path, sizeof(full_path), filepath);
    
    FILE *f = fopen(full_path, "r");
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Cannot open file: %.160s\n", full_path);
        console_print(buf);
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        console_print(line);
    }
    fclose(f);
    console_print("\n");
}

// ===== NEW FILE COMMANDS =====

static void console_cmd_cd(const char *path)
{
    if (!path || strlen(path) == 0) {
        console_print(console_cwd);
        console_print("\n");
        return;
    }
    
    char new_path[160];
    if (strcmp(path, "..") == 0) {
        // Go up one level
        char *last_slash = strrchr(console_cwd, '/');
        if (last_slash && last_slash != console_cwd) {
            size_t len = last_slash - console_cwd;
            if (len >= sizeof(new_path)) len = sizeof(new_path) - 1;
            memcpy(new_path, console_cwd, len);
            new_path[len] = '\0';
        } else {
            strcpy(new_path, "/littlefs");
        }
    } else {
        console_build_path(new_path, sizeof(new_path), path);
    }
    
    // Verify directory exists
    DIR *dir = opendir(new_path);
    if (dir) {
        closedir(dir);
        strncpy(console_cwd, new_path, sizeof(console_cwd) - 1);
        console_cwd[sizeof(console_cwd) - 1] = '\0';
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Directory not found: %.160s\n", new_path);
        console_print(buf);
    }
}

static void console_cmd_touch(const char *filename)
{
    if (!filename || strlen(filename) == 0) {
        console_print("Usage: touch <filename>\n");
        return;
    }
    
    char full_path[160];
    console_build_path(full_path, sizeof(full_path), filename);
    
    FILE *f = fopen(full_path, "a");
    if (f) {
        fclose(f);
        console_print("File created.\n");
    } else {
        console_print("Error creating file.\n");
    }
}

static void console_cmd_rm(const char *filename)
{
    if (!filename || strlen(filename) == 0) {
        console_print("Usage: rm <filename>\n");
        return;
    }
    
    char full_path[160];
    console_build_path(full_path, sizeof(full_path), filename);
    
    if (remove(full_path) == 0) {
        console_print("File deleted.\n");
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Error deleting: %.160s\n", full_path);
        console_print(buf);
    }
}

static void console_cmd_mkdir(const char *dirname)
{
    if (!dirname || strlen(dirname) == 0) {
        console_print("Usage: mkdir <dirname>\n");
        return;
    }
    
    char full_path[160];
    console_build_path(full_path, sizeof(full_path), dirname);
    
    if (mkdir(full_path, 0755) == 0) {
        console_print("Directory created.\n");
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Error creating directory: %.160s\n", full_path);
        console_print(buf);
    }
}

static void console_cmd_rmdir(const char *dirname)
{
    if (!dirname || strlen(dirname) == 0) {
        console_print("Usage: rmdir <dirname>\n");
        return;
    }
    
    char full_path[160];
    console_build_path(full_path, sizeof(full_path), dirname);
    
    if (rmdir(full_path) == 0) {
        console_print("Directory removed.\n");
    } else {
        console_print("Error: Directory not empty or not found.\n");
    }
}

static void console_cmd_mv(const char *args)
{
    if (!args || strlen(args) == 0) {
        console_print("Usage: mv <source> <dest>\n");
        return;
    }
    
    char arg_buf[128];
    strncpy(arg_buf, args, sizeof(arg_buf) - 1);
    arg_buf[sizeof(arg_buf) - 1] = '\0';
    
    char *space = strchr(arg_buf, ' ');
    if (!space) {
        console_print("Usage: mv <source> <dest>\n");
        return;
    }
    *space = '\0';
    char *src = arg_buf;
    char *dst = space + 1;
    while (*dst == ' ') dst++;
    
    char src_path[160], dst_path[160];
    console_build_path(src_path, sizeof(src_path), src);
    console_build_path(dst_path, sizeof(dst_path), dst);
    
    if (rename(src_path, dst_path) == 0) {
        console_print("File moved/renamed.\n");
    } else {
        console_print("Error moving file.\n");
    }
}

static void console_cmd_cp(const char *args)
{
    if (!args || strlen(args) == 0) {
        console_print("Usage: cp <source> <dest>\n");
        return;
    }
    
    char arg_buf[128];
    strncpy(arg_buf, args, sizeof(arg_buf) - 1);
    arg_buf[sizeof(arg_buf) - 1] = '\0';
    
    char *space = strchr(arg_buf, ' ');
    if (!space) {
        console_print("Usage: cp <source> <dest>\n");
        return;
    }
    *space = '\0';
    char *src = arg_buf;
    char *dst = space + 1;
    while (*dst == ' ') dst++;
    
    char src_path[160], dst_path[160];
    console_build_path(src_path, sizeof(src_path), src);
    console_build_path(dst_path, sizeof(dst_path), dst);
    
    FILE *fsrc = fopen(src_path, "rb");
    if (!fsrc) {
        console_print("Error: Cannot open source file.\n");
        return;
    }
    
    FILE *fdst = fopen(dst_path, "wb");
    if (!fdst) {
        fclose(fsrc);
        console_print("Error: Cannot create destination file.\n");
        return;
    }
    
    char buf[512];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        fwrite(buf, 1, bytes, fdst);
    }
    
    fclose(fsrc);
    fclose(fdst);
    console_print("File copied.\n");
}

static void console_cmd_echo(const char *args)
{
    if (!args || strlen(args) == 0) {
        console_print("\n");
        return;
    }
    
    // Check for redirection
    const char *redirect = strstr(args, ">");
    if (redirect) {
        char text[128];
        size_t text_len = redirect - args;
        if (text_len >= sizeof(text)) text_len = sizeof(text) - 1;
        memcpy(text, args, text_len);
        text[text_len] = '\0';
        // Trim trailing spaces
        while (text_len > 0 && text[text_len-1] == ' ') text[--text_len] = '\0';
        
        const char *filename = redirect + 1;
        while (*filename == ' ' || *filename == '>') filename++;
        
        bool append = (*(redirect+1) == '>');
        if (append) filename++;
        while (*filename == ' ') filename++;
        
        char full_path[160];
        console_build_path(full_path, sizeof(full_path), filename);
        
        FILE *f = fopen(full_path, append ? "a" : "w");
        if (f) {
            fprintf(f, "%s\n", text);
            fclose(f);
            console_print("Written to file.\n");
        } else {
            console_print("Error writing to file.\n");
        }
    } else {
        console_print(args);
        console_print("\n");
    }
}

// ===== SYSTEM INFO COMMANDS =====

static void console_cmd_df(void)
{
    char buf[256];
    
    // LittleFS info
    size_t total = 0, used = 0;
    esp_littlefs_info("littlefs", &total, &used);
    
    console_print("Filesystem      Size      Used     Avail  Use%\n");
    snprintf(buf, sizeof(buf), "/littlefs    %6luKB  %6luKB  %6luKB  %3lu%%\n",
             (unsigned long)total/1024, (unsigned long)used/1024,
             (unsigned long)(total-used)/1024,
             total > 0 ? (unsigned long)(used * 100 / total) : 0);
    console_print(buf);
}

static void console_cmd_ps(void)
{
    char buf[256];
    console_print("PID  Name                 State    Stack\n");
    console_print("---  -------------------  -------  -----\n");
    
    TaskStatus_t *task_array;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    task_array = (TaskStatus_t*)pvPortMalloc(task_count * sizeof(TaskStatus_t));
    
    if (task_array) {
        task_count = uxTaskGetSystemState(task_array, task_count, NULL);
        for (UBaseType_t i = 0; i < task_count && i < 15; i++) {
            const char *state;
            switch (task_array[i].eCurrentState) {
                case eRunning: state = "Running"; break;
                case eReady: state = "Ready"; break;
                case eBlocked: state = "Blocked"; break;
                case eSuspended: state = "Suspend"; break;
                case eDeleted: state = "Deleted"; break;
                default: state = "Unknown"; break;
            }
            snprintf(buf, sizeof(buf), "%3lu  %-19.19s  %-7s  %5lu\n",
                     (unsigned long)task_array[i].xTaskNumber,
                     task_array[i].pcTaskName,
                     state,
                     (unsigned long)task_array[i].usStackHighWaterMark);
            console_print(buf);
        }
        vPortFree(task_array);
    }
}

static void console_cmd_date(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buf[64];
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y\n", &timeinfo);
    console_print(buf);
}

static void console_cmd_whoami(void)
{
    console_print("admin\n");
}

static void console_cmd_hostname(void)
{
    console_print("esp32-win32\n");
}

// ===== NETWORK COMMANDS =====

static void console_cmd_ifconfig(void)
{
    char buf[256];
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        console_print("No network interface found.\n");
        return;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        console_print("wlan0:\n");
        snprintf(buf, sizeof(buf), "  inet %d.%d.%d.%d\n",
                 IP2STR(&ip_info.ip));
        console_print(buf);
        snprintf(buf, sizeof(buf), "  netmask %d.%d.%d.%d\n",
                 IP2STR(&ip_info.netmask));
        console_print(buf);
        snprintf(buf, sizeof(buf), "  gateway %d.%d.%d.%d\n",
                 IP2STR(&ip_info.gw));
        console_print(buf);
        
        uint8_t mac[6];
        esp_netif_get_mac(netif, mac);
        snprintf(buf, sizeof(buf), "  ether %02x:%02x:%02x:%02x:%02x:%02x\n",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        console_print(buf);
    } else {
        console_print("  Not connected\n");
    }
}

static void console_cmd_wifi(void)
{
    char buf[256];
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        console_print("WiFi Status: Connected\n");
        snprintf(buf, sizeof(buf), "  SSID: %s\n", ap_info.ssid);
        console_print(buf);
        snprintf(buf, sizeof(buf), "  RSSI: %d dBm\n", ap_info.rssi);
        console_print(buf);
        snprintf(buf, sizeof(buf), "  Channel: %d\n", ap_info.primary);
        console_print(buf);
    } else {
        console_print("WiFi Status: Not connected\n");
    }
}

static void console_cmd_ping(const char *host)
{
    if (!host || strlen(host) == 0) {
        console_print("Usage: ping <hostname or IP>\n");
        return;
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), "PING %s:\n", host);
    console_print(buf);
    
    // Resolve hostname
    struct addrinfo hints = {}, *res;
    hints.ai_family = AF_INET;
    
    int err = getaddrinfo(host, NULL, &hints, &res);
    if (err != 0 || res == NULL) {
        snprintf(buf, sizeof(buf), "Could not resolve hostname: %s\n", host);
        console_print(buf);
        return;
    }
    
    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    char ip_str[16];
    inet_ntoa_r(*addr, ip_str, sizeof(ip_str));
    freeaddrinfo(res);
    
    snprintf(buf, sizeof(buf), "Resolved to: %s\n", ip_str);
    console_print(buf);
    
    // Simple TCP connect test (not real ICMP ping, but works without raw sockets)
    for (int i = 0; i < 3; i++) {
        int64_t start = esp_timer_get_time();
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            console_print("Socket error\n");
            return;
        }
        
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(80);
        inet_pton(AF_INET, ip_str, &dest_addr.sin_addr);
        
        // Set timeout
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        int result = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        int64_t elapsed = (esp_timer_get_time() - start) / 1000;
        
        close(sock);
        
        if (result == 0) {
            snprintf(buf, sizeof(buf), "Reply from %s: time=%lldms\n", ip_str, elapsed);
        } else {
            snprintf(buf, sizeof(buf), "Request timeout for %s\n", ip_str);
        }
        console_print(buf);
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void console_cmd_curl(const char *url)
{
    if (!url || strlen(url) == 0) {
        console_print("Usage: curl <url>\n");
        return;
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), "Fetching: %s\n", url);
    console_print(buf);
    
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 5000;
    config.buffer_size = 1024;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        console_print("Error: Failed to init HTTP client\n");
        return;
    }
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        snprintf(buf, sizeof(buf), "Error: Connection failed (%s)\n", esp_err_to_name(err));
        console_print(buf);
        esp_http_client_cleanup(client);
        return;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    
    snprintf(buf, sizeof(buf), "HTTP %d, Content-Length: %d\n\n", status, content_length);
    console_print(buf);
    
    // Read response (limited to avoid buffer overflow)
    char response[512];
    int total_read = 0;
    int read_len;
    
    while ((read_len = esp_http_client_read(client, response, sizeof(response) - 1)) > 0 && total_read < 2048) {
        response[read_len] = '\0';
        console_print(response);
        total_read += read_len;
    }
    
    if (total_read >= 2048) {
        console_print("\n... (truncated)\n");
    }
    
    console_print("\n");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

// ===== CONSOLE COMMANDS =====

static void console_cmd_color(const char *arg)
{
    if (!arg || strlen(arg) == 0) {
        console_print("Color schemes:\n");
        console_print("  0: Black/Green (default)\n");
        console_print("  1: Black/White\n");
        console_print("  2: Navy/Yellow\n");
        console_print("  3: Black/Cyan\n");
        console_print("  4: Maroon/White\n");
        console_print("  5: Green/White\n");
        console_print("  6: Navy/White\n");
        console_print("  7: Black/Red\n");
        console_print("  8: Matrix (Dark Blue/Green)\n");
        console_print("  9: Dracula\n");
        console_print("Usage: color <0-9>\n");
        return;
    }
    
    int scheme = atoi(arg);
    if (scheme >= 0 && scheme <= 9) {
        console_bg_color = color_presets[scheme][0];
        console_text_color = color_presets[scheme][1];
        console_prompt_color = (scheme == 0) ? 0xFFFF00 : console_text_color;
        
        // Apply colors
        if (console_output) {
            lv_obj_set_style_bg_color(console_output, lv_color_hex(console_bg_color), 0);
            lv_obj_set_style_text_color(console_output, lv_color_hex(console_text_color), 0);
        }
        if (console_input) {
            lv_obj_set_style_text_color(console_input, lv_color_hex(console_prompt_color), 0);
            lv_obj_set_style_border_color(console_input, lv_color_hex(console_text_color), 0);
        }
        if (console_window) {
            lv_obj_set_style_bg_color(console_window, lv_color_hex(console_bg_color), 0);
        }
        
        char buf[64];
        snprintf(buf, sizeof(buf), "Color scheme %d applied.\n", scheme);
        console_print(buf);
    } else {
        console_print("Invalid color scheme. Use 0-9.\n");
    }
}

static void console_cmd_free(void)
{
    char buf[256];
    
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    console_print("Memory Info:\n");
    snprintf(buf, sizeof(buf), "  Heap:  %8lu / %8lu bytes (%lu%% used)\n",
             (unsigned long)(total_heap - free_heap),
             (unsigned long)total_heap,
             (unsigned long)((total_heap - free_heap) * 100 / total_heap));
    console_print(buf);
    
    snprintf(buf, sizeof(buf), "  PSRAM: %8lu / %8lu bytes (%lu%% used)\n",
             (unsigned long)(total_psram - free_psram),
             (unsigned long)total_psram,
             (unsigned long)((total_psram - free_psram) * 100 / total_psram));
    console_print(buf);
}

static void console_cmd_uptime(void)
{
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t uptime_sec = uptime_ms / 1000;
    uint32_t days = uptime_sec / 86400;
    uint32_t hours = (uptime_sec % 86400) / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    uint32_t secs = uptime_sec % 60;
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Uptime: %lud %02lu:%02lu:%02lu\n",
             (unsigned long)days, (unsigned long)hours, 
             (unsigned long)mins, (unsigned long)secs);
    console_print(buf);
}

static void console_process_cmd(const char *cmd)
{
    if (!cmd) return;
    
    // Skip leading whitespace
    while (*cmd == ' ') cmd++;
    if (strlen(cmd) == 0) return;
    
    // Parse command and arguments
    char cmd_buf[256];
    strncpy(cmd_buf, cmd, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    
    char *space = strchr(cmd_buf, ' ');
    char *arg = NULL;
    if (space) {
        *space = '\0';
        arg = space + 1;
        while (*arg == ' ') arg++;
        if (strlen(arg) == 0) arg = NULL;
    }
    
    // Convert command to lowercase for comparison
    for (char *p = cmd_buf; *p; p++) *p = tolower(*p);
    
    // === File Operations ===
    if (strcmp(cmd_buf, "help") == 0 || strcmp(cmd_buf, "?") == 0) {
        console_cmd_help();
    } else if (strcmp(cmd_buf, "clear") == 0 || strcmp(cmd_buf, "cls") == 0) {
        console_clear();
    } else if (strcmp(cmd_buf, "ls") == 0 || strcmp(cmd_buf, "dir") == 0) {
        console_cmd_ls(arg);
    } else if (strcmp(cmd_buf, "cd") == 0) {
        console_cmd_cd(arg);
    } else if (strcmp(cmd_buf, "pwd") == 0) {
        console_print(console_cwd);
        console_print("\n");
    } else if (strcmp(cmd_buf, "cat") == 0 || strcmp(cmd_buf, "type") == 0) {
        console_cmd_cat(arg);
    } else if (strcmp(cmd_buf, "touch") == 0) {
        console_cmd_touch(arg);
    } else if (strcmp(cmd_buf, "rm") == 0 || strcmp(cmd_buf, "del") == 0) {
        console_cmd_rm(arg);
    } else if (strcmp(cmd_buf, "mkdir") == 0 || strcmp(cmd_buf, "md") == 0) {
        console_cmd_mkdir(arg);
    } else if (strcmp(cmd_buf, "rmdir") == 0 || strcmp(cmd_buf, "rd") == 0) {
        console_cmd_rmdir(arg);
    } else if (strcmp(cmd_buf, "mv") == 0 || strcmp(cmd_buf, "ren") == 0 || strcmp(cmd_buf, "move") == 0) {
        console_cmd_mv(arg);
    } else if (strcmp(cmd_buf, "cp") == 0 || strcmp(cmd_buf, "copy") == 0) {
        console_cmd_cp(arg);
    } else if (strcmp(cmd_buf, "echo") == 0) {
        console_cmd_echo(arg);
    }
    // === System Info ===
    else if (strcmp(cmd_buf, "fastfetch") == 0 || strcmp(cmd_buf, "neofetch") == 0) {
        console_fastfetch();
    } else if (strcmp(cmd_buf, "free") == 0) {
        console_cmd_free();
    } else if (strcmp(cmd_buf, "uptime") == 0) {
        console_cmd_uptime();
    } else if (strcmp(cmd_buf, "df") == 0) {
        console_cmd_df();
    } else if (strcmp(cmd_buf, "ps") == 0) {
        console_cmd_ps();
    } else if (strcmp(cmd_buf, "date") == 0) {
        console_cmd_date();
    } else if (strcmp(cmd_buf, "whoami") == 0) {
        console_cmd_whoami();
    } else if (strcmp(cmd_buf, "hostname") == 0) {
        console_cmd_hostname();
    }
    // === Network ===
    else if (strcmp(cmd_buf, "ifconfig") == 0 || strcmp(cmd_buf, "ipconfig") == 0) {
        console_cmd_ifconfig();
    } else if (strcmp(cmd_buf, "wifi") == 0) {
        console_cmd_wifi();
    } else if (strcmp(cmd_buf, "ping") == 0) {
        console_cmd_ping(arg);
    } else if (strcmp(cmd_buf, "curl") == 0 || strcmp(cmd_buf, "wget") == 0) {
        console_cmd_curl(arg);
    }
    // === Console ===
    else if (strcmp(cmd_buf, "color") == 0) {
        console_cmd_color(arg);
    } else if (strcmp(cmd_buf, "fscreen") == 0 || strcmp(cmd_buf, "fullscreen") == 0) {
        console_fullscreen = !console_fullscreen;
        if (console_fullscreen) {
            // Save buffer before closing
            char *saved_buffer = (char*)heap_caps_malloc(sizeof(console_buffer), MALLOC_CAP_SPIRAM);
            if (saved_buffer) {
                memcpy(saved_buffer, console_buffer, sizeof(console_buffer));
            }
            int saved_len = console_buffer_len;
            
            // Clear pointers before closing to avoid crash
            console_output = NULL;
            console_input = NULL;
            console_keyboard = NULL;
            console_window = NULL;
            
            close_app_window();
            
            // Restore buffer
            if (saved_buffer) {
                memcpy(console_buffer, saved_buffer, sizeof(console_buffer));
                heap_caps_free(saved_buffer);
            }
            console_buffer_len = saved_len;
            
            app_console_create_fullscreen();
        } else {
            // Exiting fullscreen - save buffer
            char *saved_buffer = (char*)heap_caps_malloc(sizeof(console_buffer), MALLOC_CAP_SPIRAM);
            if (saved_buffer) {
                memcpy(saved_buffer, console_buffer, sizeof(console_buffer));
            }
            int saved_len = console_buffer_len;
            
            // Clear ALL pointers FIRST before any deletion
            lv_obj_t *window_to_delete = console_window;
            console_output = NULL;
            console_input = NULL;
            console_keyboard = NULL;
            console_window = NULL;
            app_window = NULL;  // Important: clear app_window too since it points to console_window
            
            // Small delay to let LVGL finish processing
            lv_task_handler();
            
            // Now safely delete the fullscreen window
            if (window_to_delete) {
                lv_obj_delete(window_to_delete);
            }
            
            // Restore buffer
            if (saved_buffer) {
                memcpy(console_buffer, saved_buffer, sizeof(console_buffer));
                heap_caps_free(saved_buffer);
            }
            console_buffer_len = saved_len;
            
            // Create normal windowed console
            app_console_create();
        }
    } else if (strcmp(cmd_buf, "reboot") == 0 || strcmp(cmd_buf, "restart") == 0) {
        console_print("Rebooting...\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(cmd_buf, "exit") == 0 || strcmp(cmd_buf, "quit") == 0) {
        // If in fullscreen, exit to windowed first, then close
        if (console_fullscreen) {
            // Clear ALL pointers FIRST
            lv_obj_t *window_to_delete = console_window;
            console_output = NULL;
            console_input = NULL;
            console_keyboard = NULL;
            console_window = NULL;
            app_window = NULL;
            console_fullscreen = false;
            
            // Let LVGL finish processing
            lv_task_handler();
            
            // Delete fullscreen window
            if (window_to_delete) {
                lv_obj_delete(window_to_delete);
            }
        } else {
            // Normal windowed mode - just close
            console_output = NULL;
            console_input = NULL;
            console_keyboard = NULL;
            console_window = NULL;
            close_app_window();
        }
    } else if (strcmp(cmd_buf, "ver") == 0 || strcmp(cmd_buf, "version") == 0) {
        console_print("Win32 Console v2.0\n");
        char buf[64];
        snprintf(buf, sizeof(buf), "ESP-IDF %s\n", esp_get_idf_version());
        console_print(buf);
    } else {
        char buf[192];
        snprintf(buf, sizeof(buf), "'%.100s' is not recognized as a command.\n", cmd_buf);
        console_print(buf);
        console_print("Type 'help' for available commands.\n");
    }
}

static void console_input_cb(lv_event_t *e)
{
    // Safety check - if console_input was cleared, ignore callback
    if (!console_input) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(console_input);
        if (txt && strlen(txt) > 0) {
            // Echo command
            char echo[128];
            snprintf(echo, sizeof(echo), "> %s\n", txt);
            console_print(echo);
            
            // Process command
            console_process_cmd(txt);
            
            // Clear input (check again as command might have closed console)
            if (console_input) {
                lv_textarea_set_text(console_input, "");
            }
        }
    }
}

void app_console_create(void)
{
    ESP_LOGI(TAG, "Opening Console");
    close_app_window();
    
    // Reset console state (but keep buffer if switching modes)
    if (!console_fullscreen) {
        console_buffer[0] = '\0';
        console_buffer_len = 0;
    }
    console_window = NULL;
    
    app_window = lv_obj_create(scr_desktop);
    lv_obj_set_size(app_window, SCREEN_WIDTH - 10, SCREEN_HEIGHT - TASKBAR_HEIGHT - 10);
    lv_obj_align(app_window, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(app_window, lv_color_hex(console_bg_color), 0);
    lv_obj_set_style_border_color(app_window, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(app_window, 2, 0);
    lv_obj_set_style_radius(app_window, 8, 0);
    lv_obj_set_style_pad_all(app_window, 0, 0);
    lv_obj_remove_flag(app_window, LV_OBJ_FLAG_SCROLLABLE);
    console_window = app_window;
    
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(app_window);
    lv_obj_set_size(title_bar, lv_pct(100), 32);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_left(title_bar, 10, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Console - Administrator");
    lv_obj_set_style_text_color(title_label, lv_color_hex(console_text_color), 0);
    lv_obj_set_style_text_font(title_label, UI_FONT, 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 32, 26);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(close_btn, 3, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        console_fullscreen = false;
        close_app_window();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_center(close_label);
    
    // Keyboard height from settings (get before output to calculate sizes)
    uint16_t kb_height = settings_get_keyboard_height_px();
    if (kb_height < 136 || kb_height > 700) kb_height = 135;
    int16_t input_offset = -(kb_height + 10);
    int16_t output_height = SCREEN_HEIGHT - TASKBAR_HEIGHT - kb_height - 95;
    
    // Console output area (scrollable)
    console_output = lv_textarea_create(app_window);
    lv_obj_set_size(console_output, SCREEN_WIDTH - 30, output_height);
    lv_obj_align(console_output, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_set_style_bg_color(console_output, lv_color_hex(console_bg_color), 0);
    lv_obj_set_style_text_color(console_output, lv_color_hex(console_text_color), 0);
    lv_obj_set_style_text_font(console_output, UI_FONT, 0);
    lv_obj_set_style_border_width(console_output, 0, 0);
    lv_obj_set_style_pad_all(console_output, 5, 0);
    lv_textarea_set_cursor_click_pos(console_output, false);
    lv_obj_add_flag(console_output, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(console_output, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(console_output, LV_SCROLLBAR_MODE_AUTO);
    
    // Input area
    console_input = lv_textarea_create(app_window);
    lv_obj_set_size(console_input, SCREEN_WIDTH - 30, 35);
    lv_obj_align(console_input, LV_ALIGN_BOTTOM_LEFT, 10, input_offset);
    lv_obj_set_style_bg_color(console_input, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_text_color(console_input, lv_color_hex(console_prompt_color), 0);
    lv_obj_set_style_text_font(console_input, UI_FONT, 0);
    lv_obj_set_style_border_color(console_input, lv_color_hex(console_text_color), 0);
    lv_obj_set_style_border_width(console_input, 1, 0);
    lv_textarea_set_placeholder_text(console_input, "> Enter command...");
    lv_textarea_set_one_line(console_input, true);
    lv_obj_add_event_cb(console_input, console_input_cb, LV_EVENT_READY, NULL);
    
    // Keyboard (uses theme and height from settings)
    console_keyboard = lv_keyboard_create(app_window);
    lv_obj_set_size(console_keyboard, SCREEN_WIDTH - 20, kb_height);
    lv_obj_align(console_keyboard, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_keyboard_set_textarea(console_keyboard, console_input);
    apply_keyboard_theme(console_keyboard);
    
    // Show startup message
    console_print("Win32 Console v2.0 [Administrator]\n");
    console_print("Type 'help' for available commands.\n\n");
    console_fastfetch();
}

// Fullscreen console (like recovery mode)
static void app_console_create_fullscreen(void)
{
    ESP_LOGI(TAG, "Opening Fullscreen Console");
    
    // Don't reset buffer when switching to fullscreen
    console_window = lv_obj_create(lv_screen_active());
    lv_obj_set_size(console_window, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(console_window, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(console_window, lv_color_hex(console_bg_color), 0);
    lv_obj_set_style_border_width(console_window, 0, 0);
    lv_obj_set_style_radius(console_window, 0, 0);
    lv_obj_set_style_pad_all(console_window, 0, 0);
    lv_obj_remove_flag(console_window, LV_OBJ_FLAG_SCROLLABLE);
    
    app_window = console_window;
    
    // Keyboard height from settings
    uint16_t kb_height = settings_get_keyboard_height_px();
    if (kb_height < 136 || kb_height > 700) kb_height = 135;
    int16_t input_offset = -(kb_height + 10);
    int16_t output_height = SCREEN_HEIGHT - kb_height - 55;
    
    // Console output area (larger in fullscreen, scrollable)
    console_output = lv_textarea_create(console_window);
    lv_obj_set_size(console_output, SCREEN_WIDTH - 20, output_height);
    lv_obj_align(console_output, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(console_output, lv_color_hex(console_bg_color), 0);
    lv_obj_set_style_text_color(console_output, lv_color_hex(console_text_color), 0);
    lv_obj_set_style_text_font(console_output, UI_FONT, 0);
    lv_obj_set_style_border_width(console_output, 0, 0);
    lv_obj_set_style_pad_all(console_output, 8, 0);
    lv_textarea_set_cursor_click_pos(console_output, false);
    lv_obj_add_flag(console_output, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(console_output, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(console_output, LV_SCROLLBAR_MODE_AUTO);
    
    // Restore buffer content
    if (console_buffer_len > 0) {
        lv_textarea_set_text(console_output, console_buffer);
        lv_textarea_set_cursor_pos(console_output, LV_TEXTAREA_CURSOR_LAST);
    }
    
    // Input area
    console_input = lv_textarea_create(console_window);
    lv_obj_set_size(console_input, SCREEN_WIDTH - 20, 35);
    lv_obj_align(console_input, LV_ALIGN_BOTTOM_LEFT, 10, input_offset);
    lv_obj_set_style_bg_color(console_input, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_text_color(console_input, lv_color_hex(console_prompt_color), 0);
    lv_obj_set_style_text_font(console_input, UI_FONT, 0);
    lv_obj_set_style_border_color(console_input, lv_color_hex(console_text_color), 0);
    lv_obj_set_style_border_width(console_input, 1, 0);
    lv_textarea_set_placeholder_text(console_input, "> Enter command...");
    lv_textarea_set_one_line(console_input, true);
    lv_obj_add_event_cb(console_input, console_input_cb, LV_EVENT_READY, NULL);
    
    // Keyboard (uses theme and height from settings)
    console_keyboard = lv_keyboard_create(console_window);
    lv_obj_set_size(console_keyboard, SCREEN_WIDTH - 10, kb_height);
    lv_obj_align(console_keyboard, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_keyboard_set_textarea(console_keyboard, console_input);
    apply_keyboard_theme(console_keyboard);
    
    console_print("\n[FULLSCREEN MODE] Type 'fscreen' or 'exit' to return.\n\n");
}


// ============ VOICE RECORDER APP ============

static lv_obj_t *recorder_content = NULL;
static lv_obj_t *recorder_status_label = NULL;
static lv_obj_t *recorder_time_label = NULL;
static lv_obj_t *recorder_waveform = NULL;
static lv_timer_t *recorder_timer = NULL;
static bool recorder_is_recording = false;
static int64_t recorder_start_time = 0;
static FILE *recorder_file = NULL;
static char recorder_filename[64] = "";

// WAV header structure
typedef struct {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // 16 for PCM
    uint16_t audio_format;  // 1 for PCM
    uint16_t num_channels;  // 1 for mono
    uint32_t sample_rate;   // 16000
    uint32_t byte_rate;     // sample_rate * num_channels * bits/8
    uint16_t block_align;   // num_channels * bits/8
    uint16_t bits_per_sample; // 16
    char data[4];           // "data"
    uint32_t data_size;     // Audio data size
} wav_header_t;

static void recorder_write_wav_header(FILE *f, uint32_t data_size) {
    wav_header_t header;
    memcpy(header.riff, "RIFF", 4);
    header.file_size = data_size + 36;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;
    header.num_channels = 1;
    header.sample_rate = 16000;
    header.byte_rate = 16000 * 1 * 2;
    header.block_align = 2;
    header.bits_per_sample = 16;
    memcpy(header.data, "data", 4);
    header.data_size = data_size;
    
    fseek(f, 0, SEEK_SET);
    fwrite(&header, sizeof(wav_header_t), 1, f);
}

static void recorder_timer_cb(lv_timer_t *timer) {
    if (!recorder_is_recording || !recorder_time_label) return;
    
    int64_t elapsed = (esp_timer_get_time() - recorder_start_time) / 1000000;
    int mins = elapsed / 60;
    int secs = elapsed % 60;
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    lv_label_set_text(recorder_time_label, buf);
    
    // Simple waveform animation
    if (recorder_waveform) {
        static int wave_phase = 0;
        wave_phase = (wave_phase + 1) % 20;
        // Redraw waveform with random heights
        lv_obj_invalidate(recorder_waveform);
    }
}

static void recorder_start(void) {
    if (recorder_is_recording) return;
    
    // Create recordings directory
    mkdir("/littlefs/recordings", 0755);
    
    // Generate filename with timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(recorder_filename, sizeof(recorder_filename), 
             "/littlefs/recordings/rec_%04d%02d%02d_%02d%02d%02d.wav",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    recorder_file = fopen(recorder_filename, "wb");
    if (!recorder_file) {
        ESP_LOGE(TAG, "Failed to create recording file");
        if (recorder_status_label) {
            lv_label_set_text(recorder_status_label, "Error: Cannot create file");
        }
        return;
    }
    
    // Write placeholder WAV header (will update at end)
    wav_header_t header = {0};
    fwrite(&header, sizeof(wav_header_t), 1, recorder_file);
    
    recorder_is_recording = true;
    recorder_start_time = esp_timer_get_time();
    
    if (recorder_status_label) {
        lv_label_set_text(recorder_status_label, "Recording...");
        lv_obj_set_style_text_color(recorder_status_label, lv_color_hex(0xFF4444), 0);
    }
    
    ESP_LOGI(TAG, "Recording started: %s", recorder_filename);
}

static void recorder_stop(void) {
    if (!recorder_is_recording) return;
    
    recorder_is_recording = false;
    
    if (recorder_file) {
        // Get file size and update WAV header
        long data_size = ftell(recorder_file) - sizeof(wav_header_t);
        if (data_size > 0) {
            recorder_write_wav_header(recorder_file, data_size);
        }
        fclose(recorder_file);
        recorder_file = NULL;
        
        ESP_LOGI(TAG, "Recording saved: %s (%ld bytes)", recorder_filename, data_size);
    }
    
    if (recorder_status_label) {
        lv_label_set_text(recorder_status_label, "Saved!");
        lv_obj_set_style_text_color(recorder_status_label, lv_color_hex(0x44FF44), 0);
    }
}

static void recorder_cleanup(void) {
    if (recorder_is_recording) {
        recorder_stop();
    }
    if (recorder_timer) {
        lv_timer_delete(recorder_timer);
        recorder_timer = NULL;
    }
    recorder_content = NULL;
    recorder_status_label = NULL;
    recorder_time_label = NULL;
    recorder_waveform = NULL;
}

void app_voice_recorder_create(void) {
    ESP_LOGI(TAG, "Opening Voice Recorder");
    create_app_window("Voice Recorder");
    
    recorder_cleanup();
    
    recorder_content = lv_obj_create(app_window);
    lv_obj_set_size(recorder_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(recorder_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(recorder_content, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_width(recorder_content, 0, 0);
    lv_obj_set_style_pad_all(recorder_content, 20, 0);
    lv_obj_remove_flag(recorder_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(recorder_content);
    lv_label_set_text(title, "Voice Recorder");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Time display (large)
    recorder_time_label = lv_label_create(recorder_content);
    lv_label_set_text(recorder_time_label, "00:00");
    lv_obj_set_style_text_color(recorder_time_label, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_text_font(recorder_time_label, UI_FONT, 0);
    lv_obj_align(recorder_time_label, LV_ALIGN_TOP_MID, 0, 80);
    
    // Status label
    recorder_status_label = lv_label_create(recorder_content);
    lv_label_set_text(recorder_status_label, "Ready");
    lv_obj_set_style_text_color(recorder_status_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(recorder_status_label, UI_FONT, 0);
    lv_obj_align(recorder_status_label, LV_ALIGN_TOP_MID, 0, 140);
    
    // Waveform visualization area
    recorder_waveform = lv_obj_create(recorder_content);
    lv_obj_set_size(recorder_waveform, 400, 100);
    lv_obj_align(recorder_waveform, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(recorder_waveform, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_color(recorder_waveform, lv_color_hex(0x333366), 0);
    lv_obj_set_style_border_width(recorder_waveform, 1, 0);
    lv_obj_set_style_radius(recorder_waveform, 8, 0);
    lv_obj_remove_flag(recorder_waveform, LV_OBJ_FLAG_SCROLLABLE);
    
    // Waveform bars
    for (int i = 0; i < 40; i++) {
        lv_obj_t *bar = lv_obj_create(recorder_waveform);
        lv_obj_set_size(bar, 6, 20 + (rand() % 60));
        lv_obj_set_pos(bar, 10 + i * 9, 50 - (10 + rand() % 30));
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);
    }
    
    // Record button (large, red)
    lv_obj_t *record_btn = lv_btn_create(recorder_content);
    lv_obj_set_size(record_btn, 120, 120);
    lv_obj_align(record_btn, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_style_bg_color(record_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(record_btn, 60, 0);
    lv_obj_set_style_shadow_width(record_btn, 20, 0);
    lv_obj_set_style_shadow_color(record_btn, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_shadow_opa(record_btn, LV_OPA_50, 0);
    
    lv_obj_t *rec_icon = lv_label_create(record_btn);
    lv_label_set_text(rec_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(rec_icon, lv_color_white(), 0);
    lv_obj_center(rec_icon);

    lv_obj_add_event_cb(record_btn, [](lv_event_t *e) {
        if (recorder_is_recording) {
            recorder_stop();
            lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xCC0000), 0);
        } else {
            recorder_start();
            lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x00CC00), 0);
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Open recordings folder button
    lv_obj_t *folder_btn = lv_btn_create(recorder_content);
    lv_obj_set_size(folder_btn, 150, 45);
    lv_obj_align(folder_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(folder_btn, lv_color_hex(0x2A4A7A), 0);
    lv_obj_set_style_radius(folder_btn, 8, 0);
    
    lv_obj_t *folder_lbl = lv_label_create(folder_btn);
    lv_label_set_text(folder_lbl, "Recordings");
    lv_obj_set_style_text_color(folder_lbl, lv_color_white(), 0);
    lv_obj_center(folder_lbl);
    
    lv_obj_add_event_cb(folder_btn, [](lv_event_t *e) {
        app_my_computer_open_path("recordings");
    }, LV_EVENT_CLICKED, NULL);
    
    // Start timer for time update
    recorder_timer = lv_timer_create(recorder_timer_cb, 100, NULL);
}


// ============ SYSTEM MONITOR APP ============

static lv_obj_t *sysmon_content = NULL;
static lv_timer_t *sysmon_timer = NULL;
static lv_obj_t *sysmon_cpu_bar = NULL;
static lv_obj_t *sysmon_ram_bar = NULL;
static lv_obj_t *sysmon_cpu_label = NULL;
static lv_obj_t *sysmon_ram_label = NULL;
static lv_obj_t *sysmon_heap_label = NULL;
static lv_obj_t *sysmon_wifi_label = NULL;
static lv_obj_t *sysmon_uptime_label = NULL;
static lv_obj_t *sysmon_tasks_label = NULL;
static lv_obj_t *sysmon_task_list = NULL;
static int sysmon_view_mode = 0;  // 0=overview, 1=processes

// Protected task names that cannot be killed
static const char* protected_tasks[] = {
    "main", "IDLE", "IDLE0", "IDLE1", "Tmr Svc", "lvgl", "ipc0", "ipc1",
    "esp_timer", "wifi", "sys_evt", "tiT", "async_tcp", NULL
};

static bool is_protected_task(const char* name) {
    for (int i = 0; protected_tasks[i] != NULL; i++) {
        if (strstr(name, protected_tasks[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static const char* task_state_str(eTaskState state) {
    switch (state) {
        case eRunning: return "Run";
        case eReady: return "Ready";
        case eBlocked: return "Block";
        case eSuspended: return "Susp";
        case eDeleted: return "Del";
        default: return "?";
    }
}

static void sysmon_kill_task_cb(lv_event_t *e) {
    TaskHandle_t task = (TaskHandle_t)lv_event_get_user_data(e);
    if (task) {
        char name[configMAX_TASK_NAME_LEN];
        strncpy(name, pcTaskGetName(task), sizeof(name) - 1);
        
        if (is_protected_task(name)) {
            show_notification("Cannot kill system task!", 2000);
            return;
        }
        
        ESP_LOGW(TAG, "Killing task: %s", name);
        vTaskDelete(task);
        show_notification("Task terminated", 1500);
    }
}

static void sysmon_update_task_list(void) {
    if (!sysmon_task_list) return;
    
    // Clear existing items
    lv_obj_clean(sysmon_task_list);
    
    // Get task count
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count > 30) task_count = 30;  // Limit
    
    TaskStatus_t *task_array = (TaskStatus_t*)malloc(task_count * sizeof(TaskStatus_t));
    if (!task_array) return;
    
    uint32_t total_runtime;
    UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
    
    // Create header
    lv_obj_t *header = lv_obj_create(sysmon_task_list);
    lv_obj_set_size(header, lv_pct(100), 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1A2A4A), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 5, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *h1 = lv_label_create(header);
    lv_label_set_text(h1, "Name");
    lv_obj_set_style_text_color(h1, lv_color_hex(0x00AAFF), 0);
    lv_obj_set_style_text_font(h1, UI_FONT, 0);
    lv_obj_align(h1, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *h2 = lv_label_create(header);
    lv_label_set_text(h2, "State");
    lv_obj_set_style_text_color(h2, lv_color_hex(0x00AAFF), 0);
    lv_obj_set_style_text_font(h2, UI_FONT, 0);
    lv_obj_align(h2, LV_ALIGN_LEFT_MID, 140, 0);
    
    lv_obj_t *h3 = lv_label_create(header);
    lv_label_set_text(h3, "Stack");
    lv_obj_set_style_text_color(h3, lv_color_hex(0x00AAFF), 0);
    lv_obj_set_style_text_font(h3, UI_FONT, 0);
    lv_obj_align(h3, LV_ALIGN_LEFT_MID, 210, 0);
    
    lv_obj_t *h4 = lv_label_create(header);
    lv_label_set_text(h4, "Pri");
    lv_obj_set_style_text_color(h4, lv_color_hex(0x00AAFF), 0);
    lv_obj_set_style_text_font(h4, UI_FONT, 0);
    lv_obj_align(h4, LV_ALIGN_LEFT_MID, 280, 0);
    
    // Create task rows
    for (int i = 0; i < actual_count; i++) {
        bool is_protected = is_protected_task(task_array[i].pcTaskName);
        
        lv_obj_t *row = lv_obj_create(sysmon_task_list);
        lv_obj_set_size(row, lv_pct(100), 35);
        lv_obj_set_style_bg_color(row, lv_color_hex(is_protected ? 0x1A1A2E : 0x0A1A0A), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x333366), LV_PART_MAIN);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        
        // Task name
        lv_obj_t *name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, task_array[i].pcTaskName);
        lv_obj_set_style_text_color(name_lbl, is_protected ? lv_color_hex(0x888888) : lv_color_white(), 0);
        lv_obj_set_style_text_font(name_lbl, UI_FONT, 0);
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, 0);
        
        // State
        lv_obj_t *state_lbl = lv_label_create(row);
        lv_label_set_text(state_lbl, task_state_str(task_array[i].eCurrentState));
        uint32_t state_color = 0xFFFFFF;
        if (task_array[i].eCurrentState == eRunning) state_color = 0x00FF00;
        else if (task_array[i].eCurrentState == eBlocked) state_color = 0xFFAA00;
        else if (task_array[i].eCurrentState == eSuspended) state_color = 0xFF4444;
        lv_obj_set_style_text_color(state_lbl, lv_color_hex(state_color), 0);
        lv_obj_set_style_text_font(state_lbl, UI_FONT, 0);
        lv_obj_align(state_lbl, LV_ALIGN_LEFT_MID, 140, 0);
        
        // Stack high water mark
        char stack_buf[16];
        snprintf(stack_buf, sizeof(stack_buf), "%d", (int)task_array[i].usStackHighWaterMark);
        lv_obj_t *stack_lbl = lv_label_create(row);
        lv_label_set_text(stack_lbl, stack_buf);
        lv_obj_set_style_text_color(stack_lbl, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(stack_lbl, UI_FONT, 0);
        lv_obj_align(stack_lbl, LV_ALIGN_LEFT_MID, 210, 0);
        
        // Priority
        char pri_buf[8];
        snprintf(pri_buf, sizeof(pri_buf), "%d", (int)task_array[i].uxCurrentPriority);
        lv_obj_t *pri_lbl = lv_label_create(row);
        lv_label_set_text(pri_lbl, pri_buf);
        lv_obj_set_style_text_color(pri_lbl, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(pri_lbl, UI_FONT, 0);
        lv_obj_align(pri_lbl, LV_ALIGN_LEFT_MID, 280, 0);
        
        // Kill button (only for non-protected tasks)
        if (!is_protected) {
            lv_obj_t *kill_btn = lv_btn_create(row);
            lv_obj_set_size(kill_btn, 60, 25);
            lv_obj_align(kill_btn, LV_ALIGN_RIGHT_MID, -5, 0);
            lv_obj_set_style_bg_color(kill_btn, lv_color_hex(0xCC3333), 0);
            lv_obj_set_style_radius(kill_btn, 4, 0);
            lv_obj_add_event_cb(kill_btn, sysmon_kill_task_cb, LV_EVENT_CLICKED, (void*)task_array[i].xHandle);
            
            lv_obj_t *kill_lbl = lv_label_create(kill_btn);
            lv_label_set_text(kill_lbl, "End");
            lv_obj_set_style_text_color(kill_lbl, lv_color_white(), 0);
            lv_obj_set_style_text_font(kill_lbl, UI_FONT, 0);
            lv_obj_center(kill_lbl);
        }
    }
    
    free(task_array);
}

static void sysmon_timer_cb(lv_timer_t *timer) {
    if (!sysmon_content) return;
    
    // Get heap info
    size_t free_heap = esp_get_free_heap_size();
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t min_heap = esp_get_minimum_free_heap_size();
    
    // Calculate RAM usage percentage
    int ram_percent = 100 - (free_heap * 100 / total_heap);
    
    // Simulated CPU usage (based on LVGL task time)
    static int cpu_percent = 0;
    cpu_percent = 10 + (rand() % 30);  // Simulated 10-40%
    
    // Update bars
    if (sysmon_cpu_bar) {
        lv_bar_set_value(sysmon_cpu_bar, cpu_percent, LV_ANIM_ON);
    }
    if (sysmon_ram_bar) {
        lv_bar_set_value(sysmon_ram_bar, ram_percent, LV_ANIM_ON);
    }
    
    // Update labels
    char buf[64];
    
    if (sysmon_cpu_label) {
        snprintf(buf, sizeof(buf), "CPU: %d%%", cpu_percent);
        lv_label_set_text(sysmon_cpu_label, buf);
    }
    
    if (sysmon_ram_label) {
        snprintf(buf, sizeof(buf), "RAM: %d%% (%dKB free)", ram_percent, (int)(free_heap / 1024));
        lv_label_set_text(sysmon_ram_label, buf);
    }
    
    if (sysmon_heap_label) {
        snprintf(buf, sizeof(buf), "Min Free: %dKB | Total: %dKB", 
                 (int)(min_heap / 1024), (int)(total_heap / 1024));
        lv_label_set_text(sysmon_heap_label, buf);
    }
    
    if (sysmon_uptime_label) {
        int64_t uptime_sec = esp_timer_get_time() / 1000000;
        int hours = uptime_sec / 3600;
        int mins = (uptime_sec % 3600) / 60;
        int secs = uptime_sec % 60;
        snprintf(buf, sizeof(buf), "Uptime: %02d:%02d:%02d", hours, mins, secs);
        lv_label_set_text(sysmon_uptime_label, buf);
    }
    
    if (sysmon_wifi_label) {
        if (system_wifi_is_connected()) {
            snprintf(buf, sizeof(buf), "WiFi: %s", system_wifi_get_ssid());
        } else {
            snprintf(buf, sizeof(buf), "WiFi: Disconnected");
        }
        lv_label_set_text(sysmon_wifi_label, buf);
    }
    
    if (sysmon_tasks_label) {
        snprintf(buf, sizeof(buf), "Tasks: %d", (int)uxTaskGetNumberOfTasks());
        lv_label_set_text(sysmon_tasks_label, buf);
    }
    
    // Update task list if in process view
    if (sysmon_view_mode == 1 && sysmon_task_list) {
        sysmon_update_task_list();
    }
}

static void sysmon_cleanup(void) {
    if (sysmon_timer) {
        lv_timer_delete(sysmon_timer);
        sysmon_timer = NULL;
    }
    sysmon_content = NULL;
    sysmon_cpu_bar = NULL;
    sysmon_ram_bar = NULL;
    sysmon_cpu_label = NULL;
    sysmon_ram_label = NULL;
    sysmon_heap_label = NULL;
    sysmon_wifi_label = NULL;
    sysmon_uptime_label = NULL;
    sysmon_tasks_label = NULL;
    sysmon_task_list = NULL;
    sysmon_view_mode = 0;
}

static void sysmon_tab_cb(lv_event_t *e);

void app_system_monitor_create(void) {
    ESP_LOGI(TAG, "Opening System Monitor");
    create_app_window("Task Manager");
    
    sysmon_cleanup();
    
    // Windows 7 style Task Manager
    // Light gray background like Win7
    sysmon_content = lv_obj_create(app_window);
    lv_obj_set_size(sysmon_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(sysmon_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(sysmon_content, lv_color_hex(0xF0F0F0), 0);  // Win7 light gray
    lv_obj_set_style_border_width(sysmon_content, 0, 0);
    lv_obj_set_style_pad_all(sysmon_content, 0, 0);
    lv_obj_remove_flag(sysmon_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Win7 style tab bar (like classic tabs)
    lv_obj_t *tab_bar = lv_obj_create(sysmon_content);
    lv_obj_set_size(tab_bar, lv_pct(100), 28);
    lv_obj_align(tab_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);
    lv_obj_remove_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Tab names like Win7: Applications, Processes, Services, Performance
    static const char* tab_names[] = {"Applications", "Processes", "Performance"};
    int tab_widths[] = {95, 80, 95};
    int tab_x = 5;
    
    for (int i = 0; i < 3; i++) {
        lv_obj_t *tab = lv_btn_create(tab_bar);
        lv_obj_set_size(tab, tab_widths[i], 24);
        lv_obj_set_pos(tab, tab_x, 2);
        
        // Win7 tab style - selected tab is white, others are gray
        if (i == sysmon_view_mode) {
            lv_obj_set_style_bg_color(tab, lv_color_white(), 0);
            lv_obj_set_style_border_color(tab, lv_color_hex(0xAAAAAA), 0);
            lv_obj_set_style_border_width(tab, 1, 0);
            lv_obj_set_style_border_side(tab, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
        } else {
            lv_obj_set_style_bg_color(tab, lv_color_hex(0xE0E0E0), 0);
            lv_obj_set_style_border_width(tab, 0, 0);
        }
        lv_obj_set_style_radius(tab, 0, 0);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        
        // Store tab index in user_data for callback
        lv_obj_set_user_data(tab, (void*)(intptr_t)i);
        lv_obj_add_event_cb(tab, [](lv_event_t *e) {
            lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            ESP_LOGI("TASKMGR", "Tab clicked: %d", idx);
            sysmon_view_mode = idx;
            app_system_monitor_create();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *tab_lbl = lv_label_create(tab);
        lv_label_set_text(tab_lbl, tab_names[i]);
        lv_obj_set_style_text_color(tab_lbl, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(tab_lbl, UI_FONT, 0);
        lv_obj_center(tab_lbl);
        lv_obj_remove_flag(tab_lbl, LV_OBJ_FLAG_CLICKABLE);
        
        tab_x += tab_widths[i] + 2;
    }
    
    // Content area below tabs
    lv_obj_t *content_area = lv_obj_create(sysmon_content);
    lv_obj_set_size(content_area, lv_pct(100) - 10, SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4 - 60);
    lv_obj_align(content_area, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(content_area, lv_color_white(), 0);
    lv_obj_set_style_border_color(content_area, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(content_area, 1, 0);
    lv_obj_set_style_pad_all(content_area, 8, 0);
    lv_obj_set_style_radius(content_area, 0, 0);
    lv_obj_remove_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Status bar at bottom (Win7 style)
    lv_obj_t *status_bar = lv_obj_create(sysmon_content);
    lv_obj_set_size(status_bar, lv_pct(100), 24);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_border_side(status_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_left(status_bar, 10, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Status bar labels
    sysmon_tasks_label = lv_label_create(status_bar);
    lv_label_set_text(sysmon_tasks_label, "Processes: 0");
    lv_obj_set_style_text_color(sysmon_tasks_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(sysmon_tasks_label, UI_FONT, 0);
    lv_obj_align(sysmon_tasks_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    sysmon_cpu_label = lv_label_create(status_bar);
    lv_label_set_text(sysmon_cpu_label, "CPU Usage: 0%");
    lv_obj_set_style_text_color(sysmon_cpu_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(sysmon_cpu_label, UI_FONT, 0);
    lv_obj_align(sysmon_cpu_label, LV_ALIGN_CENTER, 0, 0);
    
    sysmon_ram_label = lv_label_create(status_bar);
    lv_label_set_text(sysmon_ram_label, "Physical Memory: 0%");
    lv_obj_set_style_text_color(sysmon_ram_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(sysmon_ram_label, UI_FONT, 0);
    lv_obj_align(sysmon_ram_label, LV_ALIGN_RIGHT_MID, -10, 0);
    
    int y_pos = 5;
    
    if (sysmon_view_mode == 0) {
        // Applications tab - show running apps
        lv_obj_t *header = lv_obj_create(content_area);
        lv_obj_set_size(header, lv_pct(100), 24);
        lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0xE8E8E8), 0);
        lv_obj_set_style_border_width(header, 1, 0);
        lv_obj_set_style_border_color(header, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_pad_left(header, 5, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *col1 = lv_label_create(header);
        lv_label_set_text(col1, "Task");
        lv_obj_set_style_text_color(col1, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(col1, UI_FONT, 0);
        lv_obj_align(col1, LV_ALIGN_LEFT_MID, 0, 0);
        
        lv_obj_t *col2 = lv_label_create(header);
        lv_label_set_text(col2, "Status");
        lv_obj_set_style_text_color(col2, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(col2, UI_FONT, 0);
        lv_obj_align(col2, LV_ALIGN_RIGHT_MID, -10, 0);
        
        // App list
        lv_obj_t *app_list = lv_obj_create(content_area);
        lv_obj_set_size(app_list, lv_pct(100), lv_pct(100) - 60);
        lv_obj_align(app_list, LV_ALIGN_TOP_LEFT, 0, 28);
        lv_obj_set_style_bg_color(app_list, lv_color_white(), 0);
        lv_obj_set_style_border_width(app_list, 0, 0);
        lv_obj_set_style_pad_all(app_list, 5, 0);
        lv_obj_set_flex_flow(app_list, LV_FLEX_FLOW_COLUMN);
        
        // Show current app if any
        if (app_window) {
            lv_obj_t *row = lv_obj_create(app_list);
            lv_obj_set_size(row, lv_pct(100), 28);
            lv_obj_set_style_bg_color(row, lv_color_hex(0xCCE8FF), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_left(row, 5, 0);
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *name = lv_label_create(row);
            lv_label_set_text(name, "Task Manager");
            lv_obj_set_style_text_color(name, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(name, UI_FONT, 0);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
            
            lv_obj_t *status = lv_label_create(row);
            lv_label_set_text(status, "Running");
            lv_obj_set_style_text_color(status, lv_color_hex(0x008000), 0);
            lv_obj_set_style_text_font(status, UI_FONT, 0);
            lv_obj_align(status, LV_ALIGN_RIGHT_MID, -10, 0);
        }
        
        // End Task button
        lv_obj_t *end_btn = lv_btn_create(content_area);
        lv_obj_set_size(end_btn, 90, 28);
        lv_obj_align(end_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(end_btn, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_color(end_btn, lv_color_hex(0xCCE8FF), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(end_btn, lv_color_hex(0x707070), 0);
        lv_obj_set_style_border_width(end_btn, 1, 0);
        lv_obj_set_style_radius(end_btn, 3, 0);
        lv_obj_set_style_shadow_width(end_btn, 0, 0);
        
        lv_obj_t *end_lbl = lv_label_create(end_btn);
        lv_label_set_text(end_lbl, "End Task");
        lv_obj_set_style_text_color(end_lbl, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(end_lbl, UI_FONT, 0);
        lv_obj_center(end_lbl);
        
    } else if (sysmon_view_mode == 1) {
        // Processes tab - show FreeRTOS tasks
        lv_obj_t *header = lv_obj_create(content_area);
        lv_obj_set_size(header, lv_pct(100), 24);
        lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0xE8E8E8), 0);
        lv_obj_set_style_border_width(header, 1, 0);
        lv_obj_set_style_border_color(header, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_pad_left(header, 5, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *col_name = lv_label_create(header);
        lv_label_set_text(col_name, "Image Name");
        lv_obj_set_style_text_color(col_name, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(col_name, UI_FONT, 0);
        lv_obj_align(col_name, LV_ALIGN_LEFT_MID, 0, 0);
        
        lv_obj_t *col_status = lv_label_create(header);
        lv_label_set_text(col_status, "Status");
        lv_obj_set_style_text_color(col_status, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(col_status, UI_FONT, 0);
        lv_obj_align(col_status, LV_ALIGN_LEFT_MID, 180, 0);
        
        lv_obj_t *col_mem = lv_label_create(header);
        lv_label_set_text(col_mem, "Memory");
        lv_obj_set_style_text_color(col_mem, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(col_mem, UI_FONT, 0);
        lv_obj_align(col_mem, LV_ALIGN_RIGHT_MID, -10, 0);
        
        // Task list
        sysmon_task_list = lv_obj_create(content_area);
        lv_obj_set_size(sysmon_task_list, lv_pct(100), lv_pct(100) - 60);
        lv_obj_align(sysmon_task_list, LV_ALIGN_TOP_LEFT, 0, 28);
        lv_obj_set_style_bg_color(sysmon_task_list, lv_color_white(), 0);
        lv_obj_set_style_border_width(sysmon_task_list, 0, 0);
        lv_obj_set_style_pad_all(sysmon_task_list, 2, 0);
        lv_obj_set_flex_flow(sysmon_task_list, LV_FLEX_FLOW_COLUMN);
        
        sysmon_update_task_list();
        
        // End Process button
        lv_obj_t *end_btn = lv_btn_create(content_area);
        lv_obj_set_size(end_btn, 100, 28);
        lv_obj_align(end_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(end_btn, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_color(end_btn, lv_color_hex(0xCCE8FF), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(end_btn, lv_color_hex(0x707070), 0);
        lv_obj_set_style_border_width(end_btn, 1, 0);
        lv_obj_set_style_radius(end_btn, 3, 0);
        lv_obj_set_style_shadow_width(end_btn, 0, 0);
        
        lv_obj_t *end_lbl = lv_label_create(end_btn);
        lv_label_set_text(end_lbl, "End Process");
        lv_obj_set_style_text_color(end_lbl, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(end_lbl, UI_FONT, 0);
        lv_obj_center(end_lbl);
        
    } else {
        // Performance tab - CPU and Memory graphs
        
        // CPU section
        lv_obj_t *cpu_title = lv_label_create(content_area);
        lv_label_set_text(cpu_title, "CPU Usage");
        lv_obj_set_style_text_color(cpu_title, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(cpu_title, UI_FONT, 0);
        lv_obj_align(cpu_title, LV_ALIGN_TOP_LEFT, 5, y_pos);
        
        y_pos += 25;
        
        // Win7 green progress bar
        sysmon_cpu_bar = lv_bar_create(content_area);
        lv_obj_set_size(sysmon_cpu_bar, lv_pct(95), 22);
        lv_obj_align(sysmon_cpu_bar, LV_ALIGN_TOP_MID, 0, y_pos);
        lv_bar_set_range(sysmon_cpu_bar, 0, 100);
        lv_obj_set_style_bg_color(sysmon_cpu_bar, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_color(sysmon_cpu_bar, lv_color_hex(0x76B900), LV_PART_INDICATOR);
        lv_obj_set_style_border_color(sysmon_cpu_bar, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(sysmon_cpu_bar, 1, 0);
        lv_obj_set_style_radius(sysmon_cpu_bar, 0, 0);
        lv_obj_set_style_radius(sysmon_cpu_bar, 0, LV_PART_INDICATOR);
        
        y_pos += 45;
        
        // Memory section
        lv_obj_t *mem_title = lv_label_create(content_area);
        lv_label_set_text(mem_title, "Physical Memory");
        lv_obj_set_style_text_color(mem_title, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(mem_title, UI_FONT, 0);
        lv_obj_align(mem_title, LV_ALIGN_TOP_LEFT, 5, y_pos);
        
        y_pos += 25;
        
        sysmon_ram_bar = lv_bar_create(content_area);
        lv_obj_set_size(sysmon_ram_bar, lv_pct(95), 22);
        lv_obj_align(sysmon_ram_bar, LV_ALIGN_TOP_MID, 0, y_pos);
        lv_bar_set_range(sysmon_ram_bar, 0, 100);
        lv_obj_set_style_bg_color(sysmon_ram_bar, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_color(sysmon_ram_bar, lv_color_hex(0x76B900), LV_PART_INDICATOR);
        lv_obj_set_style_border_color(sysmon_ram_bar, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_border_width(sysmon_ram_bar, 1, 0);
        lv_obj_set_style_radius(sysmon_ram_bar, 0, 0);
        lv_obj_set_style_radius(sysmon_ram_bar, 0, LV_PART_INDICATOR);
        
        y_pos += 45;
        
        // System info
        sysmon_heap_label = lv_label_create(content_area);
        lv_label_set_text(sysmon_heap_label, "Total: 0 KB  |  Available: 0 KB");
        lv_obj_set_style_text_color(sysmon_heap_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(sysmon_heap_label, UI_FONT, 0);
        lv_obj_align(sysmon_heap_label, LV_ALIGN_TOP_LEFT, 5, y_pos);
        
        y_pos += 35;
        
        // Uptime
        sysmon_uptime_label = lv_label_create(content_area);
        lv_label_set_text(sysmon_uptime_label, "Up Time: 0:00:00:00");
        lv_obj_set_style_text_color(sysmon_uptime_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(sysmon_uptime_label, UI_FONT, 0);
        lv_obj_align(sysmon_uptime_label, LV_ALIGN_TOP_LEFT, 5, y_pos);
        
        y_pos += 30;
        
        // WiFi status
        sysmon_wifi_label = lv_label_create(content_area);
        lv_label_set_text(sysmon_wifi_label, "Network: Disconnected");
        lv_obj_set_style_text_color(sysmon_wifi_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(sysmon_wifi_label, UI_FONT, 0);
        lv_obj_align(sysmon_wifi_label, LV_ALIGN_TOP_LEFT, 5, y_pos);
    }
    
    // Start update timer
    sysmon_timer = lv_timer_create(sysmon_timer_cb, 1000, NULL);
    sysmon_timer_cb(NULL);
}


// ============ SNAKE GAME APP ============

#define SNAKE_GRID_SIZE 20
#define SNAKE_CELL_W 22
#define SNAKE_CELL_H 22
#define SNAKE_MAX_LEN 100

static lv_obj_t *snake_content = NULL;
static lv_obj_t *snake_canvas = NULL;
static lv_obj_t *snake_score_label = NULL;
static lv_timer_t *snake_timer = NULL;
static bool snake_game_over = false;
static int snake_score = 0;
static int snake_dir = 0;  // 0=right, 1=down, 2=left, 3=up
static int snake_next_dir = 0;
static int snake_len = 3;
static int snake_x[SNAKE_MAX_LEN];
static int snake_y[SNAKE_MAX_LEN];
static int food_x = 10;
static int food_y = 10;

static void snake_spawn_food(void) {
    bool valid = false;
    while (!valid) {
        food_x = rand() % SNAKE_GRID_SIZE;
        food_y = rand() % SNAKE_GRID_SIZE;
        valid = true;
        for (int i = 0; i < snake_len; i++) {
            if (snake_x[i] == food_x && snake_y[i] == food_y) {
                valid = false;
                break;
            }
        }
    }
}

static void snake_reset(void) {
    snake_len = 3;
    snake_dir = 0;
    snake_next_dir = 0;
    snake_score = 0;
    snake_game_over = false;
    
    // Start in center
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = SNAKE_GRID_SIZE / 2 - i;
        snake_y[i] = SNAKE_GRID_SIZE / 2;
    }
    
    snake_spawn_food();
    
    if (snake_score_label) {
        lv_label_set_text(snake_score_label, "Score: 0");
    }
}

static void snake_draw(void) {
    if (!snake_canvas) return;
    
    // Clear canvas
    lv_obj_clean(snake_canvas);
    
    // Draw grid background - Win7 white style
    lv_obj_set_style_bg_color(snake_canvas, lv_color_white(), 0);
    
    // Draw food (Win7 red/orange)
    lv_obj_t *food = lv_obj_create(snake_canvas);
    lv_obj_set_size(food, SNAKE_CELL_W - 2, SNAKE_CELL_H - 2);
    lv_obj_set_pos(food, food_x * SNAKE_CELL_W + 1, food_y * SNAKE_CELL_H + 1);
    lv_obj_set_style_bg_color(food, lv_color_hex(0xE74C3C), 0);  // Win7 red
    lv_obj_set_style_border_width(food, 0, 0);
    lv_obj_set_style_radius(food, SNAKE_CELL_W / 2, 0);
    lv_obj_remove_flag(food, LV_OBJ_FLAG_SCROLLABLE);

    // Draw snake - Win7 Aero blue style
    for (int i = 0; i < snake_len; i++) {
        lv_obj_t *seg = lv_obj_create(snake_canvas);
        lv_obj_set_size(seg, SNAKE_CELL_W - 2, SNAKE_CELL_H - 2);
        lv_obj_set_pos(seg, snake_x[i] * SNAKE_CELL_W + 1, snake_y[i] * SNAKE_CELL_H + 1);
        
        // Head is brighter blue, body is darker
        uint32_t color = (i == 0) ? 0x4A90D9 : 0x3A80C9;  // Vista Aero blue
        lv_obj_set_style_bg_color(seg, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(seg, lv_color_hex(0x2A70B9), 0);
        lv_obj_set_style_border_width(seg, 1, 0);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // Game over overlay - Win7 style
    if (snake_game_over) {
        lv_obj_t *overlay = lv_obj_create(snake_canvas);
        lv_obj_set_size(overlay, SNAKE_GRID_SIZE * SNAKE_CELL_W, SNAKE_GRID_SIZE * SNAKE_CELL_H);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *go_label = lv_label_create(overlay);
        lv_label_set_text(go_label, "GAME OVER\nTap to restart");
        lv_obj_set_style_text_color(go_label, lv_color_hex(0xE74C3C), 0);
        lv_obj_set_style_text_font(go_label, UI_FONT, 0);
        lv_obj_set_style_text_align(go_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(go_label);
    }
}

static void snake_timer_cb(lv_timer_t *timer) {
    if (snake_game_over || !snake_canvas) return;
    
    // Apply direction change
    snake_dir = snake_next_dir;
    
    // Calculate new head position
    int new_x = snake_x[0];
    int new_y = snake_y[0];
    
    switch (snake_dir) {
        case 0: new_x++; break;  // Right
        case 1: new_y++; break;  // Down
        case 2: new_x--; break;  // Left
        case 3: new_y--; break;  // Up
    }
    
    // Wrap around
    if (new_x < 0) new_x = SNAKE_GRID_SIZE - 1;
    if (new_x >= SNAKE_GRID_SIZE) new_x = 0;
    if (new_y < 0) new_y = SNAKE_GRID_SIZE - 1;
    if (new_y >= SNAKE_GRID_SIZE) new_y = 0;

    // Check self collision
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == new_x && snake_y[i] == new_y) {
            snake_game_over = true;
            snake_draw();
            return;
        }
    }
    
    // Check food collision
    bool ate_food = (new_x == food_x && new_y == food_y);
    
    // Move snake
    if (!ate_food) {
        // Shift body
        for (int i = snake_len - 1; i > 0; i--) {
            snake_x[i] = snake_x[i - 1];
            snake_y[i] = snake_y[i - 1];
        }
    } else {
        // Grow snake
        if (snake_len < SNAKE_MAX_LEN) {
            for (int i = snake_len; i > 0; i--) {
                snake_x[i] = snake_x[i - 1];
                snake_y[i] = snake_y[i - 1];
            }
            snake_len++;
        }
        snake_score += 10;
        
        char buf[32];
        snprintf(buf, sizeof(buf), "Score: %d", snake_score);
        if (snake_score_label) {
            lv_label_set_text(snake_score_label, buf);
        }
        
        snake_spawn_food();
    }
    
    snake_x[0] = new_x;
    snake_y[0] = new_y;
    
    snake_draw();
}

static void snake_touch_cb(lv_event_t *e) {
    if (snake_game_over) {
        snake_reset();
        snake_draw();
        return;
    }
    
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    
    // Get canvas position
    lv_area_t area;
    lv_obj_get_coords(snake_canvas, &area);
    
    int touch_x = point.x - area.x1;
    int touch_y = point.y - area.y1;
    
    int canvas_w = SNAKE_GRID_SIZE * SNAKE_CELL_W;
    int canvas_h = SNAKE_GRID_SIZE * SNAKE_CELL_H;
    
    // Determine direction based on touch quadrant
    int center_x = canvas_w / 2;
    int center_y = canvas_h / 2;
    
    int dx = touch_x - center_x;
    int dy = touch_y - center_y;

    // Determine new direction (can't reverse)
    if (abs(dx) > abs(dy)) {
        // Horizontal
        if (dx > 0 && snake_dir != 2) snake_next_dir = 0;  // Right
        else if (dx < 0 && snake_dir != 0) snake_next_dir = 2;  // Left
    } else {
        // Vertical
        if (dy > 0 && snake_dir != 3) snake_next_dir = 1;  // Down
        else if (dy < 0 && snake_dir != 1) snake_next_dir = 3;  // Up
    }
}

static void snake_cleanup(void) {
    if (snake_timer) {
        lv_timer_delete(snake_timer);
        snake_timer = NULL;
    }
    snake_content = NULL;
    snake_canvas = NULL;
    snake_score_label = NULL;
}

static void snake_dpad_cb(lv_event_t *e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (snake_game_over) {
        snake_reset();
        snake_draw();
        return;
    }
    
    // Prevent reversing direction
    if ((dir == 0 && snake_dir != 2) ||  // Right, not going left
        (dir == 1 && snake_dir != 3) ||  // Down, not going up
        (dir == 2 && snake_dir != 0) ||  // Left, not going right
        (dir == 3 && snake_dir != 1)) {  // Up, not going down
        snake_next_dir = dir;
    }
}

void app_snake_create(void) {
    ESP_LOGI(TAG, "Opening Snake Game");
    create_app_window("Snake");
    
    snake_cleanup();
    
    // Windows 7 style content area
    snake_content = lv_obj_create(app_window);
    lv_obj_set_size(snake_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(snake_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(snake_content, lv_color_hex(0xF0F0F0), 0);  // Win7 light gray
    lv_obj_set_style_border_width(snake_content, 0, 0);
    lv_obj_set_style_pad_all(snake_content, 10, 0);
    lv_obj_remove_flag(snake_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Score label - Win7 style
    snake_score_label = lv_label_create(snake_content);
    lv_label_set_text(snake_score_label, "Score: 0");
    lv_obj_set_style_text_color(snake_score_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(snake_score_label, UI_FONT, 0);
    lv_obj_align(snake_score_label, LV_ALIGN_TOP_LEFT, 10, 5);
    
    // Game canvas - Win7 style with border (centered horizontally)
    snake_canvas = lv_obj_create(snake_content);
    lv_obj_set_size(snake_canvas, SNAKE_GRID_SIZE * SNAKE_CELL_W, SNAKE_GRID_SIZE * SNAKE_CELL_H);
    lv_obj_align(snake_canvas, LV_ALIGN_TOP_MID, 0, 35);  // Centered
    lv_obj_set_style_bg_color(snake_canvas, lv_color_white(), 0);  // White game area
    lv_obj_set_style_border_color(snake_canvas, lv_color_hex(0xAAAAAA), 0);  // Win7 gray border
    lv_obj_set_style_border_width(snake_canvas, 1, 0);
    lv_obj_set_style_radius(snake_canvas, 0, 0);
    lv_obj_set_style_pad_all(snake_canvas, 0, 0);
    lv_obj_remove_flag(snake_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(snake_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(snake_canvas, snake_touch_cb, LV_EVENT_CLICKED, NULL);
    
    // D-pad controls BELOW the game canvas - Win7 Aero style
    int dpad_y = 35 + SNAKE_GRID_SIZE * SNAKE_CELL_H + 15;  // Below canvas
    int btn_size = 50;
    int btn_gap = 4;
    
    // D-pad container with Win7 style (centered)
    lv_obj_t *dpad_cont = lv_obj_create(snake_content);
    lv_obj_set_size(dpad_cont, btn_size * 3 + btn_gap * 2 + 10, btn_size * 3 + btn_gap * 2 + 10);
    lv_obj_align(dpad_cont, LV_ALIGN_TOP_MID, 0, dpad_y);
    lv_obj_set_style_bg_color(dpad_cont, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_border_color(dpad_cont, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(dpad_cont, 1, 0);
    lv_obj_set_style_radius(dpad_cont, 4, 0);
    lv_obj_set_style_pad_all(dpad_cont, 5, 0);
    lv_obj_remove_flag(dpad_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Win7 Aero blue button colors
    uint32_t btn_color = 0x4A90D9;      // Vista Aero blue
    uint32_t btn_pressed = 0x2A70B9;    // Darker blue when pressed
    uint32_t btn_border = 0x3A80C9;     // Border color
    
    // Up button
    lv_obj_t *btn_up = lv_btn_create(dpad_cont);
    lv_obj_set_size(btn_up, btn_size, btn_size);
    lv_obj_set_pos(btn_up, btn_size + btn_gap, 0);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(btn_color), 0);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(btn_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_up, lv_color_hex(btn_border), 0);
    lv_obj_set_style_border_width(btn_up, 1, 0);
    lv_obj_set_style_radius(btn_up, 4, 0);
    lv_obj_set_style_shadow_width(btn_up, 2, 0);
    lv_obj_set_style_shadow_color(btn_up, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_up, LV_OPA_20, 0);
    lv_obj_add_event_cb(btn_up, snake_dpad_cb, LV_EVENT_CLICKED, (void*)(intptr_t)3);
    
    lv_obj_t *up_lbl = lv_label_create(btn_up);
    lv_label_set_text(up_lbl, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(up_lbl, lv_color_white(), 0);
    lv_obj_center(up_lbl);
    
    // Left button
    lv_obj_t *btn_left = lv_btn_create(dpad_cont);
    lv_obj_set_size(btn_left, btn_size, btn_size);
    lv_obj_set_pos(btn_left, 0, btn_size + btn_gap);
    lv_obj_set_style_bg_color(btn_left, lv_color_hex(btn_color), 0);
    lv_obj_set_style_bg_color(btn_left, lv_color_hex(btn_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_left, lv_color_hex(btn_border), 0);
    lv_obj_set_style_border_width(btn_left, 1, 0);
    lv_obj_set_style_radius(btn_left, 4, 0);
    lv_obj_set_style_shadow_width(btn_left, 2, 0);
    lv_obj_set_style_shadow_color(btn_left, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_left, LV_OPA_20, 0);
    lv_obj_add_event_cb(btn_left, snake_dpad_cb, LV_EVENT_CLICKED, (void*)(intptr_t)2);
    
    lv_obj_t *left_lbl = lv_label_create(btn_left);
    lv_label_set_text(left_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(left_lbl, lv_color_white(), 0);
    lv_obj_center(left_lbl);
    
    // Right button
    lv_obj_t *btn_right = lv_btn_create(dpad_cont);
    lv_obj_set_size(btn_right, btn_size, btn_size);
    lv_obj_set_pos(btn_right, (btn_size + btn_gap) * 2, btn_size + btn_gap);
    lv_obj_set_style_bg_color(btn_right, lv_color_hex(btn_color), 0);
    lv_obj_set_style_bg_color(btn_right, lv_color_hex(btn_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_right, lv_color_hex(btn_border), 0);
    lv_obj_set_style_border_width(btn_right, 1, 0);
    lv_obj_set_style_radius(btn_right, 4, 0);
    lv_obj_set_style_shadow_width(btn_right, 2, 0);
    lv_obj_set_style_shadow_color(btn_right, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_right, LV_OPA_20, 0);
    lv_obj_add_event_cb(btn_right, snake_dpad_cb, LV_EVENT_CLICKED, (void*)(intptr_t)0);
    
    lv_obj_t *right_lbl = lv_label_create(btn_right);
    lv_label_set_text(right_lbl, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(right_lbl, lv_color_white(), 0);
    lv_obj_center(right_lbl);
    
    // Down button
    lv_obj_t *btn_down = lv_btn_create(dpad_cont);
    lv_obj_set_size(btn_down, btn_size, btn_size);
    lv_obj_set_pos(btn_down, btn_size + btn_gap, (btn_size + btn_gap) * 2);
    lv_obj_set_style_bg_color(btn_down, lv_color_hex(btn_color), 0);
    lv_obj_set_style_bg_color(btn_down, lv_color_hex(btn_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_down, lv_color_hex(btn_border), 0);
    lv_obj_set_style_border_width(btn_down, 1, 0);
    lv_obj_set_style_radius(btn_down, 4, 0);
    lv_obj_set_style_shadow_width(btn_down, 2, 0);
    lv_obj_set_style_shadow_color(btn_down, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_down, LV_OPA_20, 0);
    lv_obj_add_event_cb(btn_down, snake_dpad_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    
    lv_obj_t *down_lbl = lv_label_create(btn_down);
    lv_label_set_text(down_lbl, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(down_lbl, lv_color_white(), 0);
    lv_obj_center(down_lbl);
    
    // Initialize game
    snake_reset();
    snake_draw();
    
    // Start game timer (150ms = moderate speed)
    snake_timer = lv_timer_create(snake_timer_cb, 150, NULL);
}

// ============ JAVASCRIPT IDE APP (VSCode 2022 Style) ============

static duk_esp32_t *js_duk = NULL;
static lv_obj_t *js_editor = NULL;
static lv_obj_t *js_console = NULL;
static lv_obj_t *js_keyboard = NULL;
static lv_obj_t *js_console_panel = NULL;
static lv_obj_t *js_content = NULL;
static lv_obj_t *js_sidebar = NULL;
static lv_obj_t *js_statusbar = NULL;
static bool js_console_expanded = true;
static char js_console_buffer[4096] = {0};
static int js_console_len = 0;

// VSCode 2022 Dark theme colors
#define VSCODE_BG           0x1E1E1E
#define VSCODE_SIDEBAR      0x252526
#define VSCODE_ACTIVITYBAR  0x333333
#define VSCODE_EDITOR_BG    0x1E1E1E
#define VSCODE_CONSOLE_BG   0x1E1E1E
#define VSCODE_TITLEBAR     0x323233
#define VSCODE_TAB_ACTIVE   0x1E1E1E
#define VSCODE_TAB_INACTIVE 0x2D2D2D
#define VSCODE_ACCENT       0x007ACC
#define VSCODE_TEXT         0xD4D4D4
#define VSCODE_TEXT_DIM     0x858585
#define VSCODE_COMMENT      0x6A9955
#define VSCODE_KEYWORD      0x569CD6
#define VSCODE_STRING       0xCE9178
#define VSCODE_NUMBER       0xB5CEA8
#define VSCODE_BORDER       0x3C3C3C
#define VSCODE_STATUSBAR    0x007ACC

static void js_console_print(const char *msg) {
    if (!msg) return;
    int len = strlen(msg);
    if (js_console_len + len + 2 < sizeof(js_console_buffer) - 1) {
        strcat(js_console_buffer, msg);
        strcat(js_console_buffer, "\n");
        js_console_len += len + 1;
    } else {
        // Shift buffer
        int shift = len + 512;
        if (shift < js_console_len) {
            memmove(js_console_buffer, js_console_buffer + shift, js_console_len - shift);
            js_console_len -= shift;
        }
        strcat(js_console_buffer, msg);
        strcat(js_console_buffer, "\n");
        js_console_len = strlen(js_console_buffer);
    }
    if (js_console) {
        lv_label_set_text(js_console, js_console_buffer);
        lv_obj_t *parent = lv_obj_get_parent(js_console);
        if (parent) lv_obj_scroll_to_y(parent, LV_COORD_MAX, LV_ANIM_ON);
    }
}

static void js_run_code(void) {
    if (!js_editor || !js_duk) return;
    
    const char *code = lv_textarea_get_text(js_editor);
    if (!code || strlen(code) == 0) {
        js_console_print("[!] No code to run");
        return;
    }
    
    js_console_print(">>> Running...");
    
    // Use duk_esp32_eval API
    char *result = duk_esp32_eval(js_duk, code);
    if (result) {
        char buf[256];
        snprintf(buf, sizeof(buf), "=> %s", result);
        js_console_print(buf);
        free(result);
    } else {
        const char *err = duk_esp32_get_error(js_duk);
        if (err) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[ERROR] %s", err);
            js_console_print(buf);
        } else {
            js_console_print("=> undefined");
        }
    }
}

static void js_clear_console(void) {
    js_console_buffer[0] = '\0';
    js_console_len = 0;
    if (js_console) {
        lv_label_set_text(js_console, "Console cleared.\n");
    }
}

static void js_toggle_console(void) {
    js_console_expanded = !js_console_expanded;
    if (js_console_panel) {
        if (js_console_expanded) {
            lv_obj_remove_flag(js_console_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(js_console_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void js_keyboard_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (js_keyboard) {
            lv_obj_add_flag(js_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void js_editor_focus_cb(lv_event_t *e) {
    if (js_keyboard) {
        lv_obj_remove_flag(js_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(js_keyboard, js_editor);
    }
}

static void js_cleanup(void) {
    if (js_duk) {
        duk_esp32_cleanup(js_duk);
        js_duk = NULL;
    }
    js_editor = NULL;
    js_console = NULL;
    js_keyboard = NULL;
    js_console_panel = NULL;
    js_content = NULL;
    js_sidebar = NULL;
    js_statusbar = NULL;
}

void app_js_ide_create(void) {
    ESP_LOGI(TAG, "Opening JavaScript IDE");
    create_app_window("Visual Studio Code");
    
    // Cleanup previous instance
    js_cleanup();
    
    // Initialize Duktape
    js_duk = duk_esp32_init();
    if (!js_duk) {
        ESP_LOGE(TAG, "Failed to create Duktape context");
        show_notification("Failed to init JS engine", 2000);
        return;
    }
    
    // Set console callback
    duk_esp32_set_console_callback(js_duk, js_console_print);
    
    // Clear console buffer
    js_console_buffer[0] = '\0';
    js_console_len = 0;
    js_console_expanded = true;
    
    // Main content - VSCode dark theme
    js_content = lv_obj_create(app_window);
    lv_obj_set_size(js_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32);
    lv_obj_align(js_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(js_content, lv_color_hex(VSCODE_BG), 0);
    lv_obj_set_style_border_width(js_content, 0, 0);
    lv_obj_set_style_pad_all(js_content, 0, 0);
    lv_obj_set_style_radius(js_content, 0, 0);
    lv_obj_remove_flag(js_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Activity Bar (left strip with icons) - 48px wide
    lv_obj_t *activity_bar = lv_obj_create(js_content);
    lv_obj_set_size(activity_bar, 48, lv_pct(100));
    lv_obj_align(activity_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(activity_bar, lv_color_hex(VSCODE_ACTIVITYBAR), 0);
    lv_obj_set_style_border_width(activity_bar, 0, 0);
    lv_obj_set_style_radius(activity_bar, 0, 0);
    lv_obj_set_style_pad_all(activity_bar, 4, 0);
    lv_obj_remove_flag(activity_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(activity_bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(activity_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(activity_bar, 4, 0);
    
    // Activity bar icons (using default font for symbols)
    const char *activity_icons[] = {LV_SYMBOL_FILE, LV_SYMBOL_DIRECTORY, LV_SYMBOL_SETTINGS};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *icon_btn = lv_obj_create(activity_bar);
        lv_obj_set_size(icon_btn, 40, 40);
        lv_obj_set_style_bg_opa(icon_btn, i == 0 ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(icon_btn, lv_color_hex(VSCODE_TEXT_DIM), 0);
        lv_obj_set_style_border_width(icon_btn, 0, 0);
        lv_obj_set_style_radius(icon_btn, 4, 0);
        lv_obj_remove_flag(icon_btn, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *icon_lbl = lv_label_create(icon_btn);
        lv_label_set_text(icon_lbl, activity_icons[i]);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(i == 0 ? VSCODE_TEXT : VSCODE_TEXT_DIM), 0);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(icon_lbl);
    }
    
    // Sidebar (Explorer) - 100px wide
    js_sidebar = lv_obj_create(js_content);
    lv_obj_set_size(js_sidebar, 100, lv_pct(100));
    lv_obj_align(js_sidebar, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_set_style_bg_color(js_sidebar, lv_color_hex(VSCODE_SIDEBAR), 0);
    lv_obj_set_style_border_width(js_sidebar, 0, 0);
    lv_obj_set_style_border_side(js_sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(js_sidebar, lv_color_hex(VSCODE_BORDER), 0);
    lv_obj_set_style_border_width(js_sidebar, 1, 0);
    lv_obj_set_style_radius(js_sidebar, 0, 0);
    lv_obj_set_style_pad_all(js_sidebar, 8, 0);
    lv_obj_remove_flag(js_sidebar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Explorer header
    lv_obj_t *explorer_hdr = lv_label_create(js_sidebar);
    lv_label_set_text(explorer_hdr, "EXPLORER");
    lv_obj_set_style_text_color(explorer_hdr, lv_color_hex(VSCODE_TEXT_DIM), 0);
    lv_obj_set_style_text_font(explorer_hdr, UI_FONT, 0);
    lv_obj_align(explorer_hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // File tree item
    lv_obj_t *file_item = lv_obj_create(js_sidebar);
    lv_obj_set_size(file_item, 84, 24);
    lv_obj_align(file_item, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_obj_set_style_bg_color(file_item, lv_color_hex(VSCODE_ACCENT), 0);
    lv_obj_set_style_bg_opa(file_item, LV_OPA_30, 0);
    lv_obj_set_style_border_width(file_item, 0, 0);
    lv_obj_set_style_radius(file_item, 2, 0);
    lv_obj_set_style_pad_all(file_item, 2, 0);
    lv_obj_remove_flag(file_item, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *file_icon = lv_label_create(file_item);
    lv_label_set_text(file_icon, LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(file_icon, lv_color_hex(0xE8AB53), 0);
    lv_obj_set_style_text_font(file_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(file_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *file_name = lv_label_create(file_item);
    lv_label_set_text(file_name, "main.js");
    lv_obj_set_style_text_color(file_name, lv_color_hex(VSCODE_TEXT), 0);
    lv_obj_set_style_text_font(file_name, UI_FONT, 0);
    lv_obj_align(file_name, LV_ALIGN_LEFT_MID, 18, 0);
    
    // Editor area (right of sidebar)
    int editor_x = 48 + 100;  // Activity bar + Sidebar
    int editor_w = SCREEN_WIDTH - 10 - editor_x;
    int terminal_h = 150;
    int statusbar_h = 22;
    int tabs_h = 35;
    int editor_h = SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - terminal_h - statusbar_h - tabs_h;
    
    // Tabs bar
    lv_obj_t *tabs_bar = lv_obj_create(js_content);
    lv_obj_set_size(tabs_bar, editor_w, tabs_h);
    lv_obj_set_pos(tabs_bar, editor_x, 0);
    lv_obj_set_style_bg_color(tabs_bar, lv_color_hex(VSCODE_TITLEBAR), 0);
    lv_obj_set_style_border_width(tabs_bar, 0, 0);
    lv_obj_set_style_radius(tabs_bar, 0, 0);
    lv_obj_set_style_pad_all(tabs_bar, 0, 0);
    lv_obj_remove_flag(tabs_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tabs_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    
    // Active tab
    lv_obj_t *tab_active = lv_obj_create(tabs_bar);
    lv_obj_set_size(tab_active, 100, 30);
    lv_obj_set_style_bg_color(tab_active, lv_color_hex(VSCODE_TAB_ACTIVE), 0);
    lv_obj_set_style_border_width(tab_active, 0, 0);
    lv_obj_set_style_border_side(tab_active, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(tab_active, lv_color_hex(VSCODE_ACCENT), 0);
    lv_obj_set_style_border_width(tab_active, 2, 0);
    lv_obj_set_style_radius(tab_active, 0, 0);
    lv_obj_set_style_pad_left(tab_active, 8, 0);
    lv_obj_remove_flag(tab_active, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *tab_icon = lv_label_create(tab_active);
    lv_label_set_text(tab_icon, LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(tab_icon, lv_color_hex(0xE8AB53), 0);
    lv_obj_set_style_text_font(tab_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(tab_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *tab_label = lv_label_create(tab_active);
    lv_label_set_text(tab_label, "main.js");
    lv_obj_set_style_text_color(tab_label, lv_color_hex(VSCODE_TEXT), 0);
    lv_obj_set_style_text_font(tab_label, UI_FONT, 0);
    lv_obj_align(tab_label, LV_ALIGN_LEFT_MID, 18, 0);
    
    // Run button in tabs bar (right side)
    lv_obj_t *run_btn = lv_btn_create(tabs_bar);
    lv_obj_set_size(run_btn, 70, 28);
    lv_obj_set_style_bg_color(run_btn, lv_color_hex(0x388E3C), 0);
    lv_obj_set_style_bg_color(run_btn, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
    lv_obj_set_style_radius(run_btn, 4, 0);
    lv_obj_add_event_cb(run_btn, [](lv_event_t *e) { js_run_code(); }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *run_lbl = lv_label_create(run_btn);
    lv_label_set_text(run_lbl, LV_SYMBOL_PLAY " Run");
    lv_obj_set_style_text_color(run_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(run_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(run_lbl);
    
    // Editor textarea
    js_editor = lv_textarea_create(js_content);
    lv_obj_set_size(js_editor, editor_w, editor_h);
    lv_obj_set_pos(js_editor, editor_x, tabs_h);
    lv_obj_set_style_bg_color(js_editor, lv_color_hex(VSCODE_EDITOR_BG), 0);
    lv_obj_set_style_text_color(js_editor, lv_color_hex(VSCODE_TEXT), 0);
    lv_obj_set_style_text_font(js_editor, UI_FONT, 0);
    lv_obj_set_style_border_width(js_editor, 0, 0);
    lv_obj_set_style_radius(js_editor, 0, 0);
    lv_textarea_set_placeholder_text(js_editor, "// Enter JavaScript code here...");
    lv_textarea_set_text(js_editor, "// Hello World\nprint('Hello from ESP32!');\n");
    lv_obj_add_event_cb(js_editor, js_editor_focus_cb, LV_EVENT_FOCUSED, NULL);
    
    // Terminal panel
    js_console_panel = lv_obj_create(js_content);
    lv_obj_set_size(js_console_panel, editor_w, terminal_h);
    lv_obj_set_pos(js_console_panel, editor_x, tabs_h + editor_h);
    lv_obj_set_style_bg_color(js_console_panel, lv_color_hex(VSCODE_CONSOLE_BG), 0);
    lv_obj_set_style_border_color(js_console_panel, lv_color_hex(VSCODE_BORDER), 0);
    lv_obj_set_style_border_width(js_console_panel, 1, 0);
    lv_obj_set_style_border_side(js_console_panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(js_console_panel, 0, 0);
    lv_obj_set_style_pad_all(js_console_panel, 0, 0);
    lv_obj_remove_flag(js_console_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    // Terminal tabs (PROBLEMS, OUTPUT, TERMINAL)
    lv_obj_t *term_tabs = lv_obj_create(js_console_panel);
    lv_obj_set_size(term_tabs, lv_pct(100), 28);
    lv_obj_align(term_tabs, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(term_tabs, lv_color_hex(VSCODE_TITLEBAR), 0);
    lv_obj_set_style_border_width(term_tabs, 0, 0);
    lv_obj_set_style_radius(term_tabs, 0, 0);
    lv_obj_set_style_pad_all(term_tabs, 4, 0);
    lv_obj_remove_flag(term_tabs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(term_tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(term_tabs, 16, 0);
    
    const char *term_tab_names[] = {"PROBLEMS", "OUTPUT", "TERMINAL"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *ttab = lv_label_create(term_tabs);
        lv_label_set_text(ttab, term_tab_names[i]);
        lv_obj_set_style_text_color(ttab, lv_color_hex(i == 2 ? VSCODE_TEXT : VSCODE_TEXT_DIM), 0);
        lv_obj_set_style_text_font(ttab, UI_FONT, 0);
    }
    
    // Console output (scrollable)
    lv_obj_t *console_scroll = lv_obj_create(js_console_panel);
    lv_obj_set_size(console_scroll, lv_pct(100), terminal_h - 28);
    lv_obj_align(console_scroll, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(console_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(console_scroll, 0, 0);
    lv_obj_set_style_pad_all(console_scroll, 8, 0);
    lv_obj_add_flag(console_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(console_scroll, LV_DIR_VER);
    
    js_console = lv_label_create(console_scroll);
    lv_label_set_text(js_console, "");
    lv_obj_set_style_text_color(js_console, lv_color_hex(0x4EC9B0), 0);
    lv_obj_set_style_text_font(js_console, UI_FONT, 0);
    lv_obj_set_width(js_console, lv_pct(100));
    lv_label_set_long_mode(js_console, LV_LABEL_LONG_WRAP);
    
    // Status bar (blue, at bottom)
    js_statusbar = lv_obj_create(js_content);
    lv_obj_set_size(js_statusbar, lv_pct(100), statusbar_h);
    lv_obj_align(js_statusbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(js_statusbar, lv_color_hex(VSCODE_STATUSBAR), 0);
    lv_obj_set_style_border_width(js_statusbar, 0, 0);
    lv_obj_set_style_radius(js_statusbar, 0, 0);
    lv_obj_set_style_pad_left(js_statusbar, 8, 0);
    lv_obj_set_style_pad_right(js_statusbar, 8, 0);
    lv_obj_remove_flag(js_statusbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Status bar items
    lv_obj_t *status_branch = lv_label_create(js_statusbar);
    lv_label_set_text(status_branch, LV_SYMBOL_DIRECTORY " main");
    lv_obj_set_style_text_color(status_branch, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_branch, &lv_font_montserrat_14, 0);
    lv_obj_align(status_branch, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *status_lang = lv_label_create(js_statusbar);
    lv_label_set_text(status_lang, "JavaScript");
    lv_obj_set_style_text_color(status_lang, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_lang, UI_FONT, 0);
    lv_obj_align(status_lang, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Keyboard (hidden by default)
    js_keyboard = lv_keyboard_create(js_content);
    lv_obj_set_size(js_keyboard, lv_pct(100), 220);
    lv_obj_align(js_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(js_keyboard, js_editor);
    lv_obj_add_event_cb(js_keyboard, js_keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(js_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    js_console_print("JavaScript IDE Ready.");
    js_console_print("Use print() or console.log() for output.");
}

// ============ TETRIS GAME ============

#define TETRIS_COLS 10
#define TETRIS_ROWS 20
#define TETRIS_CELL 32  // Larger cells for better visibility

static lv_obj_t *tetris_content = NULL;
static lv_obj_t *tetris_canvas = NULL;
static lv_obj_t *tetris_score_label = NULL;
static lv_obj_t *tetris_level_label = NULL;
static lv_obj_t *tetris_lines_label = NULL;
static lv_obj_t *tetris_next_preview = NULL;
static lv_obj_t *tetris_info_panel = NULL;
static lv_timer_t *tetris_timer = NULL;
static uint8_t tetris_board[TETRIS_ROWS][TETRIS_COLS] = {0};
static int tetris_score = 0;
static int tetris_level = 1;
static int tetris_lines = 0;
static bool tetris_game_over = false;

// Current piece state
static int tetris_piece_type = 0;
static int tetris_piece_rot = 0;
static int tetris_piece_x = 0;
static int tetris_piece_y = 0;
static int tetris_next_piece = 0;

// Tetromino shapes (4 rotations each)
static const uint8_t tetris_shapes[7][4][4][4] = {
    // I
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    // O
    {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
    // T
    {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    // S
    {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
     {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    // Z
    {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
    // J
    {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}},
    // L
    {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
     {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}}
};

static const uint32_t tetris_colors[7] = {
    0x00FFFF, 0xFFFF00, 0x800080, 0x00FF00, 0xFF0000, 0x0000FF, 0xFF8000
};

static bool tetris_check_collision(int px, int py, int rot) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetris_shapes[tetris_piece_type][rot][y][x]) {
                int nx = px + x;
                int ny = py + y;
                if (nx < 0 || nx >= TETRIS_COLS || ny >= TETRIS_ROWS) return true;
                if (ny >= 0 && tetris_board[ny][nx]) return true;
            }
        }
    }
    return false;
}

static void tetris_lock_piece(void) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetris_shapes[tetris_piece_type][tetris_piece_rot][y][x]) {
                int ny = tetris_piece_y + y;
                int nx = tetris_piece_x + x;
                if (ny >= 0 && ny < TETRIS_ROWS && nx >= 0 && nx < TETRIS_COLS) {
                    tetris_board[ny][nx] = tetris_piece_type + 1;
                }
            }
        }
    }
}

static void tetris_clear_lines(void) {
    int lines = 0;
    for (int y = TETRIS_ROWS - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TETRIS_COLS; x++) {
            if (!tetris_board[y][x]) { full = false; break; }
        }
        if (full) {
            lines++;
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < TETRIS_COLS; x++) {
                    tetris_board[yy][x] = tetris_board[yy-1][x];
                }
            }
            for (int x = 0; x < TETRIS_COLS; x++) tetris_board[0][x] = 0;
            y++;
        }
    }
    if (lines > 0) {
        int points[] = {0, 100, 300, 500, 800};
        tetris_score += points[lines] * tetris_level;
        tetris_lines += lines;
        tetris_level = 1 + tetris_lines / 10;
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", tetris_score);
        if (tetris_score_label) lv_label_set_text(tetris_score_label, buf);
        snprintf(buf, sizeof(buf), "%d", tetris_level);
        if (tetris_level_label) lv_label_set_text(tetris_level_label, buf);
        snprintf(buf, sizeof(buf), "%d", tetris_lines);
        if (tetris_lines_label) lv_label_set_text(tetris_lines_label, buf);
    }
}

// Forward declaration
static void tetris_draw_next_piece(void);

static void tetris_spawn_piece(void) {
    tetris_piece_type = tetris_next_piece;
    tetris_next_piece = esp_random() % 7;
    tetris_piece_rot = 0;
    tetris_piece_x = TETRIS_COLS / 2 - 2;
    tetris_piece_y = 0;
    
    tetris_draw_next_piece();
    
    if (tetris_check_collision(tetris_piece_x, tetris_piece_y, tetris_piece_rot)) {
        tetris_game_over = true;
    }
}

static void tetris_reset(void) {
    memset(tetris_board, 0, sizeof(tetris_board));
    tetris_score = 0;
    tetris_level = 1;
    tetris_lines = 0;
    tetris_game_over = false;
    tetris_next_piece = esp_random() % 7;
    tetris_spawn_piece();
    if (tetris_score_label) lv_label_set_text(tetris_score_label, "0");
    if (tetris_level_label) lv_label_set_text(tetris_level_label, "1");
    if (tetris_lines_label) lv_label_set_text(tetris_lines_label, "0");
    tetris_draw_next_piece();
}

static void tetris_draw_next_piece(void) {
    if (!tetris_next_preview) return;
    lv_obj_clean(tetris_next_preview);
    
    int cell_size = 18;  // Larger preview cells
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetris_shapes[tetris_next_piece][0][y][x]) {
                lv_obj_t *cell = lv_obj_create(tetris_next_preview);
                lv_obj_set_size(cell, cell_size - 2, cell_size - 2);
                lv_obj_set_pos(cell, x * cell_size + 6, y * cell_size + 6);
                lv_obj_set_style_bg_color(cell, lv_color_hex(tetris_colors[tetris_next_piece]), 0);
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_radius(cell, 2, 0);
                lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            }
        }
    }
}

static void tetris_draw(void) {
    if (!tetris_canvas) return;
    
    lv_obj_clean(tetris_canvas);
    
    // Draw board
    for (int y = 0; y < TETRIS_ROWS; y++) {
        for (int x = 0; x < TETRIS_COLS; x++) {
            if (tetris_board[y][x]) {
                lv_obj_t *cell = lv_obj_create(tetris_canvas);
                lv_obj_set_size(cell, TETRIS_CELL - 2, TETRIS_CELL - 2);
                lv_obj_set_pos(cell, x * TETRIS_CELL + 1, y * TETRIS_CELL + 1);
                lv_obj_set_style_bg_color(cell, lv_color_hex(tetris_colors[tetris_board[y][x] - 1]), 0);
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_radius(cell, 2, 0);
                lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            }
        }
    }
    
    // Draw ghost piece (preview where piece will land)
    if (!tetris_game_over) {
        int ghost_y = tetris_piece_y;
        while (!tetris_check_collision(tetris_piece_x, ghost_y + 1, tetris_piece_rot)) {
            ghost_y++;
        }
        if (ghost_y > tetris_piece_y) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    if (tetris_shapes[tetris_piece_type][tetris_piece_rot][y][x]) {
                        int px = (tetris_piece_x + x) * TETRIS_CELL + 1;
                        int py = (ghost_y + y) * TETRIS_CELL + 1;
                        if (py >= 0) {
                            lv_obj_t *cell = lv_obj_create(tetris_canvas);
                            lv_obj_set_size(cell, TETRIS_CELL - 2, TETRIS_CELL - 2);
                            lv_obj_set_pos(cell, px, py);
                            lv_obj_set_style_bg_opa(cell, LV_OPA_30, 0);
                            lv_obj_set_style_bg_color(cell, lv_color_hex(tetris_colors[tetris_piece_type]), 0);
                            lv_obj_set_style_border_width(cell, 1, 0);
                            lv_obj_set_style_border_color(cell, lv_color_hex(tetris_colors[tetris_piece_type]), 0);
                            lv_obj_set_style_border_opa(cell, LV_OPA_50, 0);
                            lv_obj_set_style_radius(cell, 2, 0);
                            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
                        }
                    }
                }
            }
        }
    }
    
    // Draw current piece
    if (!tetris_game_over) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (tetris_shapes[tetris_piece_type][tetris_piece_rot][y][x]) {
                    int px = (tetris_piece_x + x) * TETRIS_CELL + 1;
                    int py = (tetris_piece_y + y) * TETRIS_CELL + 1;
                    if (py >= 0) {
                        lv_obj_t *cell = lv_obj_create(tetris_canvas);
                        lv_obj_set_size(cell, TETRIS_CELL - 2, TETRIS_CELL - 2);
                        lv_obj_set_pos(cell, px, py);
                        lv_obj_set_style_bg_color(cell, lv_color_hex(tetris_colors[tetris_piece_type]), 0);
                        lv_obj_set_style_border_width(cell, 1, 0);
                        lv_obj_set_style_border_color(cell, lv_color_hex(0xFFFFFF), 0);
                        lv_obj_set_style_radius(cell, 2, 0);
                        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
                    }
                }
            }
        }
    }
    
    // Game over overlay
    if (tetris_game_over) {
        lv_obj_t *overlay = lv_obj_create(tetris_canvas);
        lv_obj_set_size(overlay, TETRIS_COLS * TETRIS_CELL, TETRIS_ROWS * TETRIS_CELL);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, [](lv_event_t *e) {
            tetris_reset();
            tetris_draw();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *go_label = lv_label_create(overlay);
        lv_label_set_text(go_label, "GAME OVER\nTap to restart");
        lv_obj_set_style_text_color(go_label, lv_color_hex(0xE74C3C), 0);
        lv_obj_set_style_text_font(go_label, UI_FONT, 0);
        lv_obj_set_style_text_align(go_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(go_label);
    }
}

static void tetris_timer_cb(lv_timer_t *timer) {
    if (tetris_game_over || !tetris_canvas) return;
    
    if (!tetris_check_collision(tetris_piece_x, tetris_piece_y + 1, tetris_piece_rot)) {
        tetris_piece_y++;
    } else {
        tetris_lock_piece();
        tetris_clear_lines();
        tetris_spawn_piece();
    }
    tetris_draw();
    
    // Adjust speed based on level
    int delay = 500 - (tetris_level - 1) * 40;
    if (delay < 100) delay = 100;
    lv_timer_set_period(timer, delay);
}

static void tetris_touch_cb(lv_event_t *e) {
    if (tetris_game_over) {
        tetris_reset();
        tetris_draw();
        return;
    }
    
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    lv_area_t area;
    lv_obj_get_coords(tetris_canvas, &area);
    
    int touch_x = point.x - area.x1;
    int canvas_w = TETRIS_COLS * TETRIS_CELL;
    
    if (touch_x < canvas_w / 3) {
        // Left
        if (!tetris_check_collision(tetris_piece_x - 1, tetris_piece_y, tetris_piece_rot)) {
            tetris_piece_x--;
        }
    } else if (touch_x > canvas_w * 2 / 3) {
        // Right
        if (!tetris_check_collision(tetris_piece_x + 1, tetris_piece_y, tetris_piece_rot)) {
            tetris_piece_x++;
        }
    } else {
        // Center - rotate
        int new_rot = (tetris_piece_rot + 1) % 4;
        if (!tetris_check_collision(tetris_piece_x, tetris_piece_y, new_rot)) {
            tetris_piece_rot = new_rot;
        }
    }
    tetris_draw();
}

static void tetris_btn_cb(lv_event_t *e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    if (tetris_game_over) { tetris_reset(); tetris_draw(); return; }
    
    switch (action) {
        case 0: // Left
            if (!tetris_check_collision(tetris_piece_x - 1, tetris_piece_y, tetris_piece_rot))
                tetris_piece_x--;
            break;
        case 1: // Right
            if (!tetris_check_collision(tetris_piece_x + 1, tetris_piece_y, tetris_piece_rot))
                tetris_piece_x++;
            break;
        case 2: // Rotate
            { int nr = (tetris_piece_rot + 1) % 4;
              if (!tetris_check_collision(tetris_piece_x, tetris_piece_y, nr))
                  tetris_piece_rot = nr; }
            break;
        case 3: // Drop
            while (!tetris_check_collision(tetris_piece_x, tetris_piece_y + 1, tetris_piece_rot))
                tetris_piece_y++;
            tetris_lock_piece();
            tetris_clear_lines();
            tetris_spawn_piece();
            break;
    }
    tetris_draw();
}

static void tetris_cleanup(void) {
    if (tetris_timer) { lv_timer_delete(tetris_timer); tetris_timer = NULL; }
    tetris_content = NULL;
    tetris_canvas = NULL;
    tetris_score_label = NULL;
    tetris_level_label = NULL;
    tetris_lines_label = NULL;
    tetris_next_preview = NULL;
    tetris_info_panel = NULL;
}

void app_tetris_create(void) {
    ESP_LOGI(TAG, "Opening Tetris");
    create_app_window("Tetris");
    tetris_cleanup();
    
    tetris_content = lv_obj_create(app_window);
    lv_obj_set_size(tetris_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(tetris_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(tetris_content, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_width(tetris_content, 0, 0);
    lv_obj_set_style_pad_all(tetris_content, 5, 0);
    lv_obj_remove_flag(tetris_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Game canvas (left side) - centered vertically
    int canvas_w = TETRIS_COLS * TETRIS_CELL;
    int canvas_h = TETRIS_ROWS * TETRIS_CELL;
    
    tetris_canvas = lv_obj_create(tetris_content);
    lv_obj_set_size(tetris_canvas, canvas_w, canvas_h);
    lv_obj_align(tetris_canvas, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(tetris_canvas, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_border_color(tetris_canvas, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_width(tetris_canvas, 2, 0);
    lv_obj_set_style_radius(tetris_canvas, 4, 0);
    lv_obj_set_style_pad_all(tetris_canvas, 0, 0);
    lv_obj_remove_flag(tetris_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tetris_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tetris_canvas, tetris_touch_cb, LV_EVENT_CLICKED, NULL);
    
    // Info panel (right side) - classic Tetris style
    int info_x = canvas_w + 25;
    int info_w = SCREEN_WIDTH - canvas_w - 45;
    
    tetris_info_panel = lv_obj_create(tetris_content);
    lv_obj_set_size(tetris_info_panel, info_w, canvas_h);
    lv_obj_align(tetris_info_panel, LV_ALIGN_LEFT_MID, info_x, 0);
    lv_obj_set_style_bg_color(tetris_info_panel, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_border_color(tetris_info_panel, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_border_width(tetris_info_panel, 2, 0);
    lv_obj_set_style_radius(tetris_info_panel, 8, 0);
    lv_obj_set_style_pad_all(tetris_info_panel, 8, 0);
    lv_obj_remove_flag(tetris_info_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    // NEXT piece section with box
    lv_obj_t *next_box = lv_obj_create(tetris_info_panel);
    lv_obj_set_size(next_box, info_w - 20, 110);
    lv_obj_align(next_box, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(next_box, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_border_color(next_box, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(next_box, 1, 0);
    lv_obj_set_style_radius(next_box, 4, 0);
    lv_obj_remove_flag(next_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *next_title = lv_label_create(next_box);
    lv_label_set_text(next_title, "NEXT");
    lv_obj_set_style_text_color(next_title, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_text_font(next_title, UI_FONT, 0);
    lv_obj_align(next_title, LV_ALIGN_TOP_MID, 0, 5);
    
    tetris_next_preview = lv_obj_create(next_box);
    lv_obj_set_size(tetris_next_preview, 80, 70);
    lv_obj_align(tetris_next_preview, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(tetris_next_preview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tetris_next_preview, 0, 0);
    lv_obj_set_style_pad_all(tetris_next_preview, 0, 0);
    lv_obj_remove_flag(tetris_next_preview, LV_OBJ_FLAG_SCROLLABLE);
    
    // SCORE section with box
    lv_obj_t *score_box = lv_obj_create(tetris_info_panel);
    lv_obj_set_size(score_box, info_w - 20, 60);
    lv_obj_align(score_box, LV_ALIGN_TOP_MID, 0, 125);
    lv_obj_set_style_bg_color(score_box, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_border_color(score_box, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(score_box, 1, 0);
    lv_obj_set_style_radius(score_box, 4, 0);
    lv_obj_remove_flag(score_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *score_title = lv_label_create(score_box);
    lv_label_set_text(score_title, "SCORE");
    lv_obj_set_style_text_color(score_title, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_text_font(score_title, UI_FONT, 0);
    lv_obj_align(score_title, LV_ALIGN_TOP_MID, 0, 5);
    
    tetris_score_label = lv_label_create(score_box);
    lv_label_set_text(tetris_score_label, "0");
    lv_obj_set_style_text_color(tetris_score_label, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_text_font(tetris_score_label, UI_FONT, 0);
    lv_obj_align(tetris_score_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // LEVEL section with box
    lv_obj_t *level_box = lv_obj_create(tetris_info_panel);
    lv_obj_set_size(level_box, (info_w - 30) / 2, 55);
    lv_obj_align(level_box, LV_ALIGN_TOP_LEFT, 5, 195);
    lv_obj_set_style_bg_color(level_box, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_border_color(level_box, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(level_box, 1, 0);
    lv_obj_set_style_radius(level_box, 4, 0);
    lv_obj_remove_flag(level_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *level_title = lv_label_create(level_box);
    lv_label_set_text(level_title, "LVL");
    lv_obj_set_style_text_color(level_title, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_text_font(level_title, UI_FONT, 0);
    lv_obj_align(level_title, LV_ALIGN_TOP_MID, 0, 5);
    
    tetris_level_label = lv_label_create(level_box);
    lv_label_set_text(tetris_level_label, "1");
    lv_obj_set_style_text_color(tetris_level_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(tetris_level_label, UI_FONT, 0);
    lv_obj_align(tetris_level_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // LINES section with box
    lv_obj_t *lines_box = lv_obj_create(tetris_info_panel);
    lv_obj_set_size(lines_box, (info_w - 30) / 2, 55);
    lv_obj_align(lines_box, LV_ALIGN_TOP_RIGHT, -5, 195);
    lv_obj_set_style_bg_color(lines_box, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_border_color(lines_box, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(lines_box, 1, 0);
    lv_obj_set_style_radius(lines_box, 4, 0);
    lv_obj_remove_flag(lines_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *lines_title = lv_label_create(lines_box);
    lv_label_set_text(lines_title, "LINE");
    lv_obj_set_style_text_color(lines_title, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_text_font(lines_title, UI_FONT, 0);
    lv_obj_align(lines_title, LV_ALIGN_TOP_MID, 0, 5);
    
    tetris_lines_label = lv_label_create(lines_box);
    lv_label_set_text(tetris_lines_label, "0");
    lv_obj_set_style_text_color(tetris_lines_label, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(tetris_lines_label, UI_FONT, 0);
    lv_obj_align(tetris_lines_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    
    // Control buttons - compact layout
    int btn_w = (info_w - 30) / 2;
    int btn_h = 50;
    int btn_y = 265;
    
    // Left button
    lv_obj_t *btn_left = lv_btn_create(tetris_info_panel);
    lv_obj_set_size(btn_left, btn_w, btn_h);
    lv_obj_align(btn_left, LV_ALIGN_TOP_LEFT, 5, btn_y);
    lv_obj_set_style_bg_color(btn_left, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_radius(btn_left, 6, 0);
    lv_obj_add_event_cb(btn_left, tetris_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)0);
    lv_obj_t *lbl_left = lv_label_create(btn_left);
    lv_label_set_text(lbl_left, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lbl_left, lv_color_white(), 0);
    lv_obj_center(lbl_left);
    
    // Right button
    lv_obj_t *btn_right = lv_btn_create(tetris_info_panel);
    lv_obj_set_size(btn_right, btn_w, btn_h);
    lv_obj_align(btn_right, LV_ALIGN_TOP_RIGHT, -5, btn_y);
    lv_obj_set_style_bg_color(btn_right, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_radius(btn_right, 6, 0);
    lv_obj_add_event_cb(btn_right, tetris_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    lv_obj_t *lbl_right = lv_label_create(btn_right);
    lv_label_set_text(lbl_right, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(lbl_right, lv_color_white(), 0);
    lv_obj_center(lbl_right);
    
    // Rotate button
    lv_obj_t *btn_rot = lv_btn_create(tetris_info_panel);
    lv_obj_set_size(btn_rot, btn_w, btn_h);
    lv_obj_align(btn_rot, LV_ALIGN_TOP_LEFT, 5, btn_y + btn_h + 8);
    lv_obj_set_style_bg_color(btn_rot, lv_color_hex(0x9B59B6), 0);
    lv_obj_set_style_radius(btn_rot, 6, 0);
    lv_obj_add_event_cb(btn_rot, tetris_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)2);
    lv_obj_t *lbl_rot = lv_label_create(btn_rot);
    lv_label_set_text(lbl_rot, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(lbl_rot, lv_color_white(), 0);
    lv_obj_center(lbl_rot);
    
    // Drop button
    lv_obj_t *btn_drop = lv_btn_create(tetris_info_panel);
    lv_obj_set_size(btn_drop, btn_w, btn_h);
    lv_obj_align(btn_drop, LV_ALIGN_TOP_RIGHT, -5, btn_y + btn_h + 8);
    lv_obj_set_style_bg_color(btn_drop, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_radius(btn_drop, 6, 0);
    lv_obj_add_event_cb(btn_drop, tetris_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)3);
    lv_obj_t *lbl_drop = lv_label_create(btn_drop);
    lv_label_set_text(lbl_drop, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_drop, lv_color_white(), 0);
    lv_obj_center(lbl_drop);
    
    tetris_reset();
    tetris_draw();
    tetris_timer = lv_timer_create(tetris_timer_cb, 500, NULL);
}


// ============ 2048 GAME ============

#define G2048_SIZE 4
#define G2048_CELL 95  // Optimized for 480px width (4*95=380 + padding)

static lv_obj_t *g2048_content = NULL;
static lv_obj_t *g2048_canvas = NULL;
static lv_obj_t *g2048_score_label = NULL;
static uint16_t g2048_board[G2048_SIZE][G2048_SIZE] = {0};
static int g2048_score = 0;
static bool g2048_game_over = false;
static bool g2048_won = false;
static lv_point_t g2048_swipe_start;

static const uint32_t g2048_colors[] = {
    0xCDC1B4, 0xEEE4DA, 0xEDE0C8, 0xF2B179, 0xF59563,
    0xF67C5F, 0xF65E3B, 0xEDCF72, 0xEDCC61, 0xEDC850,
    0xEDC53F, 0xEDC22E, 0x3C3A32
};

static void g2048_add_tile(void) {
    int empty[16][2], cnt = 0;
    for (int y = 0; y < G2048_SIZE; y++) {
        for (int x = 0; x < G2048_SIZE; x++) {
            if (!g2048_board[y][x]) { empty[cnt][0] = y; empty[cnt][1] = x; cnt++; }
        }
    }
    if (cnt > 0) {
        int idx = esp_random() % cnt;
        g2048_board[empty[idx][0]][empty[idx][1]] = (esp_random() % 10 < 9) ? 2 : 4;
    }
}

static bool g2048_can_move(void) {
    for (int y = 0; y < G2048_SIZE; y++) {
        for (int x = 0; x < G2048_SIZE; x++) {
            if (!g2048_board[y][x]) return true;
            if (x < G2048_SIZE - 1 && g2048_board[y][x] == g2048_board[y][x+1]) return true;
            if (y < G2048_SIZE - 1 && g2048_board[y][x] == g2048_board[y+1][x]) return true;
        }
    }
    return false;
}

static void g2048_reset(void) {
    memset(g2048_board, 0, sizeof(g2048_board));
    g2048_score = 0;
    g2048_game_over = false;
    g2048_won = false;
    g2048_add_tile();
    g2048_add_tile();
    if (g2048_score_label) lv_label_set_text(g2048_score_label, "Score: 0");
}

static void g2048_draw(void) {
    if (!g2048_canvas) return;
    lv_obj_clean(g2048_canvas);
    
    for (int y = 0; y < G2048_SIZE; y++) {
        for (int x = 0; x < G2048_SIZE; x++) {
            lv_obj_t *cell = lv_obj_create(g2048_canvas);
            lv_obj_set_size(cell, G2048_CELL - 8, G2048_CELL - 8);
            lv_obj_set_pos(cell, x * G2048_CELL + 4, y * G2048_CELL + 4);
            
            int val = g2048_board[y][x];
            int ci = 0;
            if (val > 0) { int v = val; while (v > 1) { ci++; v >>= 1; } }
            if (ci > 12) ci = 12;
            
            lv_obj_set_style_bg_color(cell, lv_color_hex(g2048_colors[ci]), 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_radius(cell, 6, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            
            if (val > 0) {
                lv_obj_t *lbl = lv_label_create(cell);
                char buf[8]; snprintf(buf, sizeof(buf), "%d", val);
                lv_label_set_text(lbl, buf);
                lv_obj_set_style_text_color(lbl, lv_color_hex(val <= 4 ? 0x776E65 : 0xF9F6F2), 0);
                lv_obj_set_style_text_font(lbl, UI_FONT, 0);
                lv_obj_center(lbl);
            }
        }
    }
    
    if (g2048_game_over || g2048_won) {
        lv_obj_t *overlay = lv_obj_create(g2048_canvas);
        lv_obj_set_size(overlay, G2048_SIZE * G2048_CELL, G2048_SIZE * G2048_CELL);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, [](lv_event_t *e) {
            if (g2048_won) {
                g2048_won = false;  // Continue playing after winning
            } else {
                g2048_reset();  // Restart on game over
            }
            g2048_draw();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lbl = lv_label_create(overlay);
        lv_label_set_text(lbl, g2048_won ? "YOU WIN!\nTap to continue" : "GAME OVER\nTap to restart");
        lv_obj_set_style_text_color(lbl, lv_color_hex(g2048_won ? 0x00FF00 : 0xE74C3C), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

static bool g2048_move(int dx, int dy) {
    bool moved = false;
    bool merged[G2048_SIZE][G2048_SIZE] = {0};
    
    int startX = dx > 0 ? G2048_SIZE - 2 : (dx < 0 ? 1 : 0);
    int endX = dx > 0 ? -1 : (dx < 0 ? G2048_SIZE : G2048_SIZE);
    int stepX = dx > 0 ? -1 : 1;
    
    int startY = dy > 0 ? G2048_SIZE - 2 : (dy < 0 ? 1 : 0);
    int endY = dy > 0 ? -1 : (dy < 0 ? G2048_SIZE : G2048_SIZE);
    int stepY = dy > 0 ? -1 : 1;
    
    for (int y = startY; y != endY; y += stepY) {
        for (int x = startX; x != endX; x += stepX) {
            if (!g2048_board[y][x]) continue;
            
            int nx = x, ny = y;
            while (true) {
                int tx = nx + dx, ty = ny + dy;
                if (tx < 0 || tx >= G2048_SIZE || ty < 0 || ty >= G2048_SIZE) break;
                if (g2048_board[ty][tx] == 0) { nx = tx; ny = ty; }
                else if (g2048_board[ty][tx] == g2048_board[y][x] && !merged[ty][tx]) {
                    nx = tx; ny = ty; break;
                } else break;
            }
            
            if (nx != x || ny != y) {
                if (g2048_board[ny][nx] == g2048_board[y][x]) {
                    g2048_board[ny][nx] *= 2;
                    g2048_score += g2048_board[ny][nx];
                    merged[ny][nx] = true;
                    if (g2048_board[ny][nx] == 2048) g2048_won = true;
                } else {
                    g2048_board[ny][nx] = g2048_board[y][x];
                }
                g2048_board[y][x] = 0;
                moved = true;
            }
        }
    }
    return moved;
}

static void g2048_swipe_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_active(), &g2048_swipe_start);
    } else if (code == LV_EVENT_RELEASED) {
        if (g2048_game_over) { g2048_reset(); g2048_draw(); return; }
        if (g2048_won) { g2048_won = false; g2048_draw(); return; }
        
        lv_point_t end;
        lv_indev_get_point(lv_indev_active(), &end);
        
        int dx = end.x - g2048_swipe_start.x;
        int dy = end.y - g2048_swipe_start.y;
        
        bool moved = false;
        if (abs(dx) > abs(dy) && abs(dx) > 30) {
            moved = g2048_move(dx > 0 ? 1 : -1, 0);
        } else if (abs(dy) > 30) {
            moved = g2048_move(0, dy > 0 ? 1 : -1);
        }
        
        if (moved) {
            g2048_add_tile();
            char buf[32]; snprintf(buf, sizeof(buf), "Score: %d", g2048_score);
            if (g2048_score_label) lv_label_set_text(g2048_score_label, buf);
            if (!g2048_can_move()) g2048_game_over = true;
        }
        g2048_draw();
    }
}

static void game2048_cleanup(void) {
    g2048_content = NULL;
    g2048_canvas = NULL;
    g2048_score_label = NULL;
}

void app_2048_create(void) {
    ESP_LOGI(TAG, "Opening 2048");
    create_app_window("2048");
    game2048_cleanup();
    
    g2048_content = lv_obj_create(app_window);
    lv_obj_set_size(g2048_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(g2048_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(g2048_content, lv_color_hex(0xFAF8EF), 0);
    lv_obj_set_style_border_width(g2048_content, 0, 0);
    lv_obj_set_style_pad_all(g2048_content, 10, 0);
    lv_obj_remove_flag(g2048_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(g2048_content);
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_color(title, lv_color_hex(0x776E65), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);
    
    g2048_score_label = lv_label_create(g2048_content);
    lv_label_set_text(g2048_score_label, "Score: 0");
    lv_obj_set_style_text_color(g2048_score_label, lv_color_hex(0x776E65), 0);
    lv_obj_set_style_text_font(g2048_score_label, UI_FONT, 0);
    lv_obj_align(g2048_score_label, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    lv_obj_t *hint = lv_label_create(g2048_content);
    lv_label_set_text(hint, "Swipe to move");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_text_font(hint, UI_FONT, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 30);
    
    // New Game button
    lv_obj_t *new_btn = lv_btn_create(g2048_content);
    lv_obj_set_size(new_btn, 100, 35);
    lv_obj_align(new_btn, LV_ALIGN_TOP_RIGHT, -10, 50);
    lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x8F7A66), 0);
    lv_obj_set_style_radius(new_btn, 6, 0);
    lv_obj_add_event_cb(new_btn, [](lv_event_t *e) {
        g2048_reset();
        g2048_draw();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *new_lbl = lv_label_create(new_btn);
    lv_label_set_text(new_lbl, "New");
    lv_obj_set_style_text_color(new_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(new_lbl, UI_FONT, 0);
    lv_obj_center(new_lbl);
    
    g2048_canvas = lv_obj_create(g2048_content);
    lv_obj_set_size(g2048_canvas, G2048_SIZE * G2048_CELL + 16, G2048_SIZE * G2048_CELL + 16);
    lv_obj_align(g2048_canvas, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(g2048_canvas, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_border_width(g2048_canvas, 0, 0);
    lv_obj_set_style_radius(g2048_canvas, 8, 0);
    lv_obj_set_style_pad_all(g2048_canvas, 8, 0);
    lv_obj_remove_flag(g2048_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g2048_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g2048_canvas, g2048_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g2048_canvas, g2048_swipe_cb, LV_EVENT_RELEASED, NULL);
    
    g2048_reset();
    g2048_draw();
}


// ============ MINESWEEPER GAME ============

#define MINE_ROWS 9
#define MINE_COLS 9
#define MINE_COUNT 10
#define MINE_CELL 42

static lv_obj_t *mine_content = NULL;
static lv_obj_t *mine_canvas = NULL;
static lv_obj_t *mine_status_label = NULL;
static int8_t mine_board[MINE_ROWS][MINE_COLS];  // -1 = mine, 0-8 = adjacent count
static uint8_t mine_revealed[MINE_ROWS][MINE_COLS];  // 0=hidden, 1=revealed, 2=flagged
static bool mine_game_over = false;
static bool mine_won = false;
static int mine_flags = 0;

static void mine_reset(void) {
    memset(mine_board, 0, sizeof(mine_board));
    memset(mine_revealed, 0, sizeof(mine_revealed));
    mine_game_over = false;
    mine_won = false;
    mine_flags = 0;
    
    // Place mines
    int placed = 0;
    while (placed < MINE_COUNT) {
        int r = esp_random() % MINE_ROWS;
        int c = esp_random() % MINE_COLS;
        if (mine_board[r][c] != -1) {
            mine_board[r][c] = -1;
            placed++;
        }
    }
    
    // Calculate adjacent counts
    for (int r = 0; r < MINE_ROWS; r++) {
        for (int c = 0; c < MINE_COLS; c++) {
            if (mine_board[r][c] == -1) continue;
            int cnt = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int nr = r + dr, nc = c + dc;
                    if (nr >= 0 && nr < MINE_ROWS && nc >= 0 && nc < MINE_COLS) {
                        if (mine_board[nr][nc] == -1) cnt++;
                    }
                }
            }
            mine_board[r][c] = cnt;
        }
    }
    
    char buf[32]; snprintf(buf, sizeof(buf), "Mines: %d", MINE_COUNT - mine_flags);
    if (mine_status_label) lv_label_set_text(mine_status_label, buf);
}

static void mine_reveal(int r, int c);

static void mine_draw(void) {
    if (!mine_canvas) return;
    lv_obj_clean(mine_canvas);
    
    static const uint32_t num_colors[] = {
        0x0000FF, 0x008000, 0xFF0000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080
    };
    
    for (int r = 0; r < MINE_ROWS; r++) {
        for (int c = 0; c < MINE_COLS; c++) {
            lv_obj_t *cell = lv_btn_create(mine_canvas);
            lv_obj_set_size(cell, MINE_CELL - 2, MINE_CELL - 2);
            lv_obj_set_pos(cell, c * MINE_CELL + 1, r * MINE_CELL + 1);
            lv_obj_set_style_radius(cell, 2, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            
            if (mine_revealed[r][c] == 1) {
                // Revealed
                lv_obj_set_style_bg_color(cell, lv_color_hex(0xD0D0D0), 0);
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xA0A0A0), 0);
                
                if (mine_board[r][c] == -1) {
                    lv_obj_t *lbl = lv_label_create(cell);
                    lv_label_set_text(lbl, "*");
                    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF0000), 0);
                    lv_obj_set_style_text_font(lbl, UI_FONT, 0);
                    lv_obj_center(lbl);
                } else if (mine_board[r][c] > 0) {
                    lv_obj_t *lbl = lv_label_create(cell);
                    char buf[8]; snprintf(buf, sizeof(buf), "%d", mine_board[r][c]);
                    lv_label_set_text(lbl, buf);
                    lv_obj_set_style_text_color(lbl, lv_color_hex(num_colors[mine_board[r][c] - 1]), 0);
                    lv_obj_set_style_text_font(lbl, UI_FONT, 0);
                    lv_obj_center(lbl);
                }
            } else if (mine_revealed[r][c] == 2) {
                // Flagged
                lv_obj_set_style_bg_color(cell, lv_color_hex(0xC0C0C0), 0);
                lv_obj_set_style_border_width(cell, 2, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0x808080), 0);
                
                lv_obj_t *lbl = lv_label_create(cell);
                lv_label_set_text(lbl, LV_SYMBOL_WARNING);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF0000), 0);
                lv_obj_center(lbl);
            } else {
                // Hidden
                lv_obj_set_style_bg_color(cell, lv_color_hex(0xC0C0C0), 0);
                lv_obj_set_style_bg_grad_color(cell, lv_color_hex(0xA0A0A0), 0);
                lv_obj_set_style_bg_grad_dir(cell, LV_GRAD_DIR_VER, 0);
                lv_obj_set_style_border_width(cell, 2, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_border_side(cell, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT), 0);
            }
            
            // Store position in user data
            int pos = r * MINE_COLS + c;
            lv_obj_add_event_cb(cell, [](lv_event_t *e) {
                if (mine_game_over || mine_won) { mine_reset(); mine_draw(); return; }
                int p = (int)(intptr_t)lv_event_get_user_data(e);
                int r = p / MINE_COLS, c = p % MINE_COLS;
                if (mine_revealed[r][c] == 0) {
                    mine_reveal(r, c);
                    mine_draw();
                }
            }, LV_EVENT_CLICKED, (void*)(intptr_t)pos);
            
            lv_obj_add_event_cb(cell, [](lv_event_t *e) {
                if (mine_game_over || mine_won) return;
                int p = (int)(intptr_t)lv_event_get_user_data(e);
                int r = p / MINE_COLS, c = p % MINE_COLS;
                if (mine_revealed[r][c] == 0) {
                    mine_revealed[r][c] = 2;
                    mine_flags++;
                } else if (mine_revealed[r][c] == 2) {
                    mine_revealed[r][c] = 0;
                    mine_flags--;
                }
                char buf[32]; snprintf(buf, sizeof(buf), "Mines: %d", MINE_COUNT - mine_flags);
                if (mine_status_label) lv_label_set_text(mine_status_label, buf);
                mine_draw();
            }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)pos);
        }
    }
    
    if (mine_game_over || mine_won) {
        lv_obj_t *overlay = lv_obj_create(mine_canvas);
        lv_obj_set_size(overlay, MINE_COLS * MINE_CELL, MINE_ROWS * MINE_CELL);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, [](lv_event_t *e) {
            mine_reset();
            mine_draw();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lbl = lv_label_create(overlay);
        lv_label_set_text(lbl, mine_won ? "YOU WIN!\nTap to restart" : "BOOM!\nTap to restart");
        lv_obj_set_style_text_color(lbl, lv_color_hex(mine_won ? 0x00FF00 : 0xE74C3C), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

static void mine_reveal(int r, int c) {
    if (r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS) return;
    if (mine_revealed[r][c] != 0) return;
    
    mine_revealed[r][c] = 1;
    
    if (mine_board[r][c] == -1) {
        mine_game_over = true;
        // Reveal all mines
        for (int i = 0; i < MINE_ROWS; i++) {
            for (int j = 0; j < MINE_COLS; j++) {
                if (mine_board[i][j] == -1) mine_revealed[i][j] = 1;
            }
        }
        return;
    }
    
    // Check win
    int hidden = 0;
    for (int i = 0; i < MINE_ROWS; i++) {
        for (int j = 0; j < MINE_COLS; j++) {
            if (mine_revealed[i][j] == 0 && mine_board[i][j] != -1) hidden++;
        }
    }
    if (hidden == 0) { mine_won = true; return; }
    
    // Flood fill for empty cells
    if (mine_board[r][c] == 0) {
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                mine_reveal(r + dr, c + dc);
            }
        }
    }
}

static void minesweeper_cleanup(void) {
    mine_content = NULL;
    mine_canvas = NULL;
    mine_status_label = NULL;
}

void app_minesweeper_create(void) {
    ESP_LOGI(TAG, "Opening Minesweeper");
    create_app_window("Minesweeper");
    minesweeper_cleanup();
    
    mine_content = lv_obj_create(app_window);
    lv_obj_set_size(mine_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(mine_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(mine_content, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_border_width(mine_content, 0, 0);
    lv_obj_set_style_pad_all(mine_content, 10, 0);
    lv_obj_remove_flag(mine_content, LV_OBJ_FLAG_SCROLLABLE);
    
    mine_status_label = lv_label_create(mine_content);
    lv_label_set_text(mine_status_label, "Mines: 10");
    lv_obj_set_style_text_color(mine_status_label, lv_color_hex(0x000080), 0);
    lv_obj_set_style_text_font(mine_status_label, UI_FONT, 0);
    lv_obj_align(mine_status_label, LV_ALIGN_TOP_LEFT, 10, 5);
    
    // New Game button
    lv_obj_t *new_btn = lv_btn_create(mine_content);
    lv_obj_set_size(new_btn, 80, 32);
    lv_obj_align(new_btn, LV_ALIGN_TOP_RIGHT, -10, 2);
    lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x808080), 0);
    lv_obj_set_style_radius(new_btn, 4, 0);
    lv_obj_add_event_cb(new_btn, [](lv_event_t *e) {
        mine_reset();
        mine_draw();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *new_lbl = lv_label_create(new_btn);
    lv_label_set_text(new_lbl, "New");
    lv_obj_set_style_text_color(new_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(new_lbl, UI_FONT, 0);
    lv_obj_center(new_lbl);
    
    lv_obj_t *hint = lv_label_create(mine_content);
    lv_label_set_text(hint, "Long press = flag");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x606060), 0);
    lv_obj_set_style_text_font(hint, UI_FONT, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 5);
    
    mine_canvas = lv_obj_create(mine_content);
    lv_obj_set_size(mine_canvas, MINE_COLS * MINE_CELL, MINE_ROWS * MINE_CELL);
    lv_obj_align(mine_canvas, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(mine_canvas, lv_color_hex(0x808080), 0);
    lv_obj_set_style_border_width(mine_canvas, 3, 0);
    lv_obj_set_style_border_color(mine_canvas, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(mine_canvas, 0, 0);
    lv_obj_set_style_pad_all(mine_canvas, 0, 0);
    lv_obj_remove_flag(mine_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    mine_reset();
    mine_draw();
}


// ============ TIC-TAC-TOE GAME ============

#define TTT_SIZE 3
#define TTT_CELL 140  // Larger cells for better touch

static lv_obj_t *ttt_content = NULL;
static lv_obj_t *ttt_canvas = NULL;
static lv_obj_t *ttt_status_label = NULL;
static int8_t ttt_board[TTT_SIZE][TTT_SIZE];  // 0=empty, 1=X, 2=O
static int ttt_turn = 1;  // 1=X, 2=O
static bool ttt_game_over = false;
static int ttt_winner = 0;  // 0=none, 1=X, 2=O, 3=draw

static int ttt_check_winner(void) {
    // Check rows and cols
    for (int i = 0; i < TTT_SIZE; i++) {
        if (ttt_board[i][0] && ttt_board[i][0] == ttt_board[i][1] && ttt_board[i][1] == ttt_board[i][2])
            return ttt_board[i][0];
        if (ttt_board[0][i] && ttt_board[0][i] == ttt_board[1][i] && ttt_board[1][i] == ttt_board[2][i])
            return ttt_board[0][i];
    }
    // Check diagonals
    if (ttt_board[0][0] && ttt_board[0][0] == ttt_board[1][1] && ttt_board[1][1] == ttt_board[2][2])
        return ttt_board[0][0];
    if (ttt_board[0][2] && ttt_board[0][2] == ttt_board[1][1] && ttt_board[1][1] == ttt_board[2][0])
        return ttt_board[0][2];
    
    // Check draw
    bool full = true;
    for (int r = 0; r < TTT_SIZE; r++) {
        for (int c = 0; c < TTT_SIZE; c++) {
            if (!ttt_board[r][c]) full = false;
        }
    }
    return full ? 3 : 0;
}

static void ttt_reset(void) {
    memset(ttt_board, 0, sizeof(ttt_board));
    ttt_turn = 1;
    ttt_game_over = false;
    ttt_winner = 0;
    if (ttt_status_label) lv_label_set_text(ttt_status_label, "X's turn");
}

static void ttt_draw(void) {
    if (!ttt_canvas) return;
    lv_obj_clean(ttt_canvas);
    
    for (int r = 0; r < TTT_SIZE; r++) {
        for (int c = 0; c < TTT_SIZE; c++) {
            lv_obj_t *cell = lv_btn_create(ttt_canvas);
            lv_obj_set_size(cell, TTT_CELL - 6, TTT_CELL - 6);
            lv_obj_set_pos(cell, c * TTT_CELL + 3, r * TTT_CELL + 3);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0xF0F0F0), 0);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0xE0E0E0), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(cell, 2, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0x4A90D9), 0);
            lv_obj_set_style_radius(cell, 8, 0);
            lv_obj_set_style_shadow_width(cell, 4, 0);
            lv_obj_set_style_shadow_color(cell, lv_color_hex(0x000000), 0);
            lv_obj_set_style_shadow_opa(cell, LV_OPA_20, 0);
            
            if (ttt_board[r][c]) {
                lv_obj_t *lbl = lv_label_create(cell);
                lv_label_set_text(lbl, ttt_board[r][c] == 1 ? "X" : "O");
                lv_obj_set_style_text_color(lbl, lv_color_hex(ttt_board[r][c] == 1 ? 0x2980B9 : 0xE74C3C), 0);
                lv_obj_set_style_text_font(lbl, UI_FONT, 0);
                lv_obj_center(lbl);
            }
            
            int pos = r * TTT_SIZE + c;
            lv_obj_add_event_cb(cell, [](lv_event_t *e) {
                if (ttt_game_over) { ttt_reset(); ttt_draw(); return; }
                int p = (int)(intptr_t)lv_event_get_user_data(e);
                int r = p / TTT_SIZE, c = p % TTT_SIZE;
                
                if (ttt_board[r][c] == 0) {
                    ttt_board[r][c] = ttt_turn;
                    ttt_winner = ttt_check_winner();
                    
                    if (ttt_winner) {
                        ttt_game_over = true;
                        if (ttt_winner == 3) {
                            if (ttt_status_label) lv_label_set_text(ttt_status_label, "Draw!");
                        } else {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%c wins!", ttt_winner == 1 ? 'X' : 'O');
                            if (ttt_status_label) lv_label_set_text(ttt_status_label, buf);
                        }
                    } else {
                        ttt_turn = ttt_turn == 1 ? 2 : 1;
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%c's turn", ttt_turn == 1 ? 'X' : 'O');
                        if (ttt_status_label) lv_label_set_text(ttt_status_label, buf);
                    }
                    ttt_draw();
                }
            }, LV_EVENT_CLICKED, (void*)(intptr_t)pos);
        }
    }
    
    if (ttt_game_over) {
        lv_obj_t *overlay = lv_obj_create(ttt_canvas);
        lv_obj_set_size(overlay, TTT_SIZE * TTT_CELL, TTT_SIZE * TTT_CELL);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, [](lv_event_t *e) {
            ttt_reset();
            ttt_draw();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lbl = lv_label_create(overlay);
        char buf[32];
        if (ttt_winner == 3) snprintf(buf, sizeof(buf), "DRAW!\nTap to restart");
        else snprintf(buf, sizeof(buf), "%c WINS!\nTap to restart", ttt_winner == 1 ? 'X' : 'O');
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(ttt_winner == 1 ? 0x3498DB : (ttt_winner == 2 ? 0xE74C3C : 0xFFFFFF)), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

static void tictactoe_cleanup(void) {
    ttt_content = NULL;
    ttt_canvas = NULL;
    ttt_status_label = NULL;
}

void app_tictactoe_create(void) {
    ESP_LOGI(TAG, "Opening Tic-Tac-Toe");
    create_app_window("Tic-Tac-Toe");
    tictactoe_cleanup();
    
    ttt_content = lv_obj_create(app_window);
    lv_obj_set_size(ttt_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(ttt_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(ttt_content, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_border_width(ttt_content, 0, 0);
    lv_obj_set_style_pad_all(ttt_content, 10, 0);
    lv_obj_remove_flag(ttt_content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(ttt_content);
    lv_label_set_text(title, "Tic-Tac-Toe");
    lv_obj_set_style_text_color(title, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);
    
    ttt_status_label = lv_label_create(ttt_content);
    lv_label_set_text(ttt_status_label, "X's turn");
    lv_obj_set_style_text_color(ttt_status_label, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_text_font(ttt_status_label, UI_FONT, 0);
    lv_obj_align(ttt_status_label, LV_ALIGN_TOP_MID, 0, 5);
    
    // New Game button
    lv_obj_t *new_btn = lv_btn_create(ttt_content);
    lv_obj_set_size(new_btn, 80, 32);
    lv_obj_align(new_btn, LV_ALIGN_TOP_RIGHT, -10, 2);
    lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_radius(new_btn, 6, 0);
    lv_obj_add_event_cb(new_btn, [](lv_event_t *e) {
        ttt_reset();
        ttt_draw();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *new_lbl = lv_label_create(new_btn);
    lv_label_set_text(new_lbl, "New");
    lv_obj_set_style_text_color(new_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(new_lbl, UI_FONT, 0);
    lv_obj_center(new_lbl);
    
    ttt_canvas = lv_obj_create(ttt_content);
    lv_obj_set_size(ttt_canvas, TTT_SIZE * TTT_CELL, TTT_SIZE * TTT_CELL);
    lv_obj_align(ttt_canvas, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(ttt_canvas, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_border_width(ttt_canvas, 0, 0);
    lv_obj_set_style_radius(ttt_canvas, 12, 0);
    lv_obj_set_style_pad_all(ttt_canvas, 0, 0);
    lv_obj_remove_flag(ttt_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    ttt_reset();
    ttt_draw();
}


// ============ MEMORY MATCH GAME ============

#define MEM_ROWS 4
#define MEM_COLS 4
#define MEM_PAIRS 8
#define MEM_CELL 100  // Larger cells for icons

static lv_obj_t *mem_content = NULL;
static lv_obj_t *mem_canvas = NULL;
static lv_obj_t *mem_status_label = NULL;
static lv_obj_t *mem_moves_label = NULL;
static uint8_t mem_board[MEM_ROWS][MEM_COLS];  // Card values (0-7, pairs)
static uint8_t mem_revealed[MEM_ROWS][MEM_COLS];  // 0=hidden, 1=revealed, 2=matched
static int mem_first_r = -1, mem_first_c = -1;
static int mem_second_r = -1, mem_second_c = -1;
static int mem_moves = 0;
static int mem_matched = 0;
static bool mem_checking = false;
static lv_timer_t *mem_timer = NULL;

// Use system icons for memory game
static const lv_image_dsc_t *mem_icons[] = {
    &img_calculator, &img_camera, &img_weather, &img_clock,
    &img_settings, &img_notepad, &img_paint, &img_folder
};

static const uint32_t mem_colors[] = {
    0xE74C3C, 0x3498DB, 0x2ECC71, 0xF39C12,
    0x9B59B6, 0x1ABC9C, 0xE91E63, 0x00BCD4
};

static void mem_draw(void);

static void mem_reset(void) {
    memset(mem_revealed, 0, sizeof(mem_revealed));
    mem_first_r = mem_first_c = -1;
    mem_second_r = mem_second_c = -1;
    mem_moves = 0;
    mem_matched = 0;
    mem_checking = false;
    
    // Create pairs
    uint8_t cards[MEM_ROWS * MEM_COLS];
    for (int i = 0; i < MEM_PAIRS; i++) {
        cards[i * 2] = i;
        cards[i * 2 + 1] = i;
    }
    
    // Shuffle
    for (int i = MEM_ROWS * MEM_COLS - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        uint8_t tmp = cards[i];
        cards[i] = cards[j];
        cards[j] = tmp;
    }
    
    // Fill board
    for (int r = 0; r < MEM_ROWS; r++) {
        for (int c = 0; c < MEM_COLS; c++) {
            mem_board[r][c] = cards[r * MEM_COLS + c];
        }
    }
    
    if (mem_moves_label) lv_label_set_text(mem_moves_label, "Moves: 0");
    if (mem_status_label) lv_label_set_text(mem_status_label, "Find all pairs!");
}

static void mem_timer_cb(lv_timer_t *timer) {
    lv_timer_delete(timer);
    mem_timer = NULL;
    
    if (mem_board[mem_first_r][mem_first_c] == mem_board[mem_second_r][mem_second_c]) {
        // Match!
        mem_revealed[mem_first_r][mem_first_c] = 2;
        mem_revealed[mem_second_r][mem_second_c] = 2;
        mem_matched++;
        
        if (mem_matched == MEM_PAIRS) {
            char buf[32];
            snprintf(buf, sizeof(buf), "You won in %d moves!", mem_moves);
            if (mem_status_label) lv_label_set_text(mem_status_label, buf);
        }
    } else {
        // No match - hide
        mem_revealed[mem_first_r][mem_first_c] = 0;
        mem_revealed[mem_second_r][mem_second_c] = 0;
    }
    
    mem_first_r = mem_first_c = -1;
    mem_second_r = mem_second_c = -1;
    mem_checking = false;
    mem_draw();
}

static void mem_draw(void) {
    if (!mem_canvas) return;
    lv_obj_clean(mem_canvas);
    
    for (int r = 0; r < MEM_ROWS; r++) {
        for (int c = 0; c < MEM_COLS; c++) {
            lv_obj_t *card = lv_btn_create(mem_canvas);
            lv_obj_set_size(card, MEM_CELL - 8, MEM_CELL - 8);
            lv_obj_set_pos(card, c * MEM_CELL + 4, r * MEM_CELL + 4);
            lv_obj_set_style_radius(card, 12, 0);
            lv_obj_set_style_shadow_width(card, 6, 0);
            lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
            
            if (mem_revealed[r][c] == 0) {
                // Hidden - show card back with gradient
                lv_obj_set_style_bg_color(card, lv_color_hex(0x3498DB), 0);
                lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x2980B9), 0);
                lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
                lv_obj_set_style_border_width(card, 3, 0);
                lv_obj_set_style_border_color(card, lv_color_hex(0x1A5276), 0);
                
                lv_obj_t *lbl = lv_label_create(card);
                lv_label_set_text(lbl, "?");
                lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
                lv_obj_set_style_text_font(lbl, UI_FONT, 0);
                lv_obj_center(lbl);
            } else {
                // Revealed or matched - show icon
                int val = mem_board[r][c];
                lv_obj_set_style_bg_color(card, lv_color_hex(mem_revealed[r][c] == 2 ? 0x27AE60 : 0xFFFFFF), 0);
                lv_obj_set_style_border_width(card, 3, 0);
                lv_obj_set_style_border_color(card, lv_color_hex(mem_colors[val]), 0);
                
                // Use system icon instead of symbol
                lv_obj_t *icon = lv_image_create(card);
                lv_image_set_src(icon, mem_icons[val]);
                lv_image_set_scale(icon, 200);  // Scale to fit card (48px -> ~38px)
                lv_obj_center(icon);
            }
            
            int pos = r * MEM_COLS + c;
            lv_obj_add_event_cb(card, [](lv_event_t *e) {
                if (mem_checking) return;
                if (mem_matched == MEM_PAIRS) { mem_reset(); mem_draw(); return; }
                
                int p = (int)(intptr_t)lv_event_get_user_data(e);
                int r = p / MEM_COLS, c = p % MEM_COLS;
                
                if (mem_revealed[r][c] != 0) return;
                
                mem_revealed[r][c] = 1;
                
                if (mem_first_r < 0) {
                    mem_first_r = r;
                    mem_first_c = c;
                } else {
                    mem_second_r = r;
                    mem_second_c = c;
                    mem_moves++;
                    
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Moves: %d", mem_moves);
                    if (mem_moves_label) lv_label_set_text(mem_moves_label, buf);
                    
                    mem_checking = true;
                    mem_timer = lv_timer_create(mem_timer_cb, 800, NULL);
                }
                
                mem_draw();
            }, LV_EVENT_CLICKED, (void*)(intptr_t)pos);
        }
    }
    
    // Win overlay
    if (mem_matched == MEM_PAIRS) {
        lv_obj_t *overlay = lv_obj_create(mem_canvas);
        lv_obj_set_size(overlay, MEM_COLS * MEM_CELL, MEM_ROWS * MEM_CELL);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, [](lv_event_t *e) {
            mem_reset();
            mem_draw();
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *lbl = lv_label_create(overlay);
        char buf[48];
        snprintf(buf, sizeof(buf), "YOU WIN!\n%d moves\nTap to restart", mem_moves);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

static void memory_cleanup(void) {
    if (mem_timer) { lv_timer_delete(mem_timer); mem_timer = NULL; }
    mem_content = NULL;
    mem_canvas = NULL;
    mem_status_label = NULL;
    mem_moves_label = NULL;
}

void app_memory_create(void) {
    ESP_LOGI(TAG, "Opening Memory Match");
    create_app_window("Memory Match");
    memory_cleanup();
    
    mem_content = lv_obj_create(app_window);
    lv_obj_set_size(mem_content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 10 - 32 - 4);
    lv_obj_align(mem_content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(mem_content, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_border_width(mem_content, 0, 0);
    lv_obj_set_style_pad_all(mem_content, 10, 0);
    lv_obj_remove_flag(mem_content, LV_OBJ_FLAG_SCROLLABLE);
    
    mem_status_label = lv_label_create(mem_content);
    lv_label_set_text(mem_status_label, "Find all pairs!");
    lv_obj_set_style_text_color(mem_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(mem_status_label, UI_FONT, 0);
    lv_obj_align(mem_status_label, LV_ALIGN_TOP_LEFT, 10, 5);
    
    // New Game button
    lv_obj_t *new_btn = lv_btn_create(mem_content);
    lv_obj_set_size(new_btn, 80, 32);
    lv_obj_align(new_btn, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_radius(new_btn, 6, 0);
    lv_obj_add_event_cb(new_btn, [](lv_event_t *e) {
        mem_reset();
        mem_draw();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *new_lbl = lv_label_create(new_btn);
    lv_label_set_text(new_lbl, "New");
    lv_obj_set_style_text_color(new_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(new_lbl, UI_FONT, 0);
    lv_obj_center(new_lbl);
    
    mem_moves_label = lv_label_create(mem_content);
    lv_label_set_text(mem_moves_label, "Moves: 0");
    lv_obj_set_style_text_color(mem_moves_label, lv_color_hex(0xBDC3C7), 0);
    lv_obj_set_style_text_font(mem_moves_label, UI_FONT, 0);
    lv_obj_align(mem_moves_label, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    mem_canvas = lv_obj_create(mem_content);
    lv_obj_set_size(mem_canvas, MEM_COLS * MEM_CELL, MEM_ROWS * MEM_CELL);
    lv_obj_align(mem_canvas, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(mem_canvas, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_border_width(mem_canvas, 0, 0);
    lv_obj_set_style_radius(mem_canvas, 12, 0);
    lv_obj_set_style_pad_all(mem_canvas, 0, 0);
    lv_obj_remove_flag(mem_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    mem_reset();
    mem_draw();
}
