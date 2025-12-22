/**
 * Win Recovery - Recovery UI Implementation
 * Windows Recovery Environment style interface with Console fallback
 */

#include "recovery_ui.h"
#include "recovery_trigger.h"
#include "recovery_sysinfo.h"
#include "boot_button.h"
#include "ui/fonts.h"
#include "hardware/hardware.h"
#include "system_settings.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "RecoveryUI";

// Screen dimensions
#define SCREEN_WIDTH    480
#define SCREEN_HEIGHT   800

// UI Mode layout
#define TILE_WIDTH      200
#define TILE_HEIGHT     90
#define TILE_MARGIN     15
#define TILE_COLS       2
#define TILE_ROWS       4
#define HEADER_HEIGHT   60
#define STATUS_HEIGHT   40

// Console mode layout
#define CONSOLE_LINE_HEIGHT     20
#define CONSOLE_MAX_LINES       30
#define CONSOLE_INPUT_HEIGHT    40

// State
static bool g_recovery_active = false;
static recovery_display_mode_t g_current_mode = RECOVERY_MODE_SELECT;
static int g_selected_tile = 0;
static lv_obj_t *g_recovery_screen = NULL;
static lv_obj_t *g_mode_select_cont = NULL;
static lv_obj_t *g_ui_mode_cont = NULL;
static lv_obj_t *g_console_cont = NULL;
static lv_obj_t *g_tiles[TILE_COUNT] = {NULL};
static lv_obj_t *g_console_output = NULL;
static lv_obj_t *g_console_input = NULL;
static lv_obj_t *g_keyboard = NULL;

// Tile definitions - using text icons since LV_SYMBOL requires special font
static const recovery_tile_info_t g_tile_info[TILE_COUNT] = {
    {"[R]",  "Reboot System",      "Restart normally"},
    {"[C]",  "Command Prompt",     "Switch to console"},
    {"[W]",  "Wipe User Data",     "Clear settings"},
    {"[S]",  "Startup Settings",   "Change boot options"},
    {"[L]",  "Reset Lock Screen",  "Remove PIN/password"},
    {"[D]",  "System Diagnostics", "Memory, display test"},
    {"[F]",  "Factory Reset",      "Erase all data"},
    {"[P]",  "Power Off",          "Shut down device"},
};

// Forward declarations
static void create_mode_select_screen(void);
static void create_ui_mode_screen(void);
static void create_console_screen(void);
static void update_tile_selection(int old_sel, int new_sel);
static void execute_tile_action(recovery_tile_t tile);
static void show_confirmation_dialog(const char *title, const char *msg, lv_event_cb_t confirm_cb);

// Console command handlers
static void cmd_help(void);
static void cmd_sysinfo(void);
static void cmd_reboot(void);
static void cmd_bootloader(void);
static void cmd_wipe_data(void);
static void cmd_wipe_wifi(void);
static void cmd_wipe_lock(void);
static void cmd_factory(void);
static void cmd_log(void);
static void cmd_partitions(void);
static void cmd_memtest(void);
static void cmd_displaytest(void);
static void cmd_sdtest(void);
static void cmd_poweroff(void);
static void cmd_ui(void);

//=============================================================================
// Mode Select Screen
//=============================================================================

static void mode_select_ui_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "UI Mode selected");
    recovery_ui_set_mode(RECOVERY_MODE_UI);
}

static void mode_select_console_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Console Mode selected");
    recovery_ui_set_mode(RECOVERY_MODE_CONSOLE);
}

static void create_mode_select_screen(void)
{
    if (g_mode_select_cont) {
        lv_obj_del(g_mode_select_cont);
    }
    
    g_mode_select_cont = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_mode_select_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(g_mode_select_cont, lv_color_hex(RECOVERY_COLOR_BG), 0);
    lv_obj_set_style_border_width(g_mode_select_cont, 0, 0);
    lv_obj_set_style_radius(g_mode_select_cont, 0, 0);
    lv_obj_set_style_pad_all(g_mode_select_cont, 0, 0);
    lv_obj_clear_flag(g_mode_select_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(g_mode_select_cont);
    lv_label_set_text(title, "Win Recovery");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, UI_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(g_mode_select_cont);
    lv_label_set_text(subtitle, "Choose recovery mode");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(subtitle, UI_FONT_DEFAULT, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 120);
    
    // UI Mode tile
    lv_obj_t *ui_tile = lv_obj_create(g_mode_select_cont);
    lv_obj_set_size(ui_tile, 180, 140);
    lv_obj_align(ui_tile, LV_ALIGN_CENTER, -100, 0);
    lv_obj_set_style_bg_color(ui_tile, lv_color_hex(RECOVERY_COLOR_TILE), 0);
    lv_obj_set_style_radius(ui_tile, 8, 0);
    lv_obj_set_style_shadow_width(ui_tile, 10, 0);
    lv_obj_set_style_shadow_opa(ui_tile, LV_OPA_30, 0);
    lv_obj_add_flag(ui_tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_tile, mode_select_ui_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ui_icon = lv_label_create(ui_tile);
    lv_label_set_text(ui_icon, "[UI]");
    lv_obj_set_style_text_font(ui_icon, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(ui_icon, lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
    lv_obj_align(ui_icon, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *ui_label = lv_label_create(ui_tile);
    lv_label_set_text(ui_label, "UI Mode");
    lv_obj_set_style_text_font(ui_label, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(ui_label, lv_color_hex(RECOVERY_COLOR_TEXT_TITLE), 0);
    lv_obj_align(ui_label, LV_ALIGN_CENTER, 0, 10);
    
    lv_obj_t *ui_desc = lv_label_create(ui_tile);
    lv_label_set_text(ui_desc, "Windows-style\ntile interface");
    lv_obj_set_style_text_font(ui_desc, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(ui_desc, lv_color_hex(RECOVERY_COLOR_TEXT_DESC), 0);
    lv_obj_set_style_text_align(ui_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_desc, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    // Console Mode tile
    lv_obj_t *console_tile = lv_obj_create(g_mode_select_cont);
    lv_obj_set_size(console_tile, 180, 140);
    lv_obj_align(console_tile, LV_ALIGN_CENTER, 100, 0);
    lv_obj_set_style_bg_color(console_tile, lv_color_hex(RECOVERY_COLOR_TILE), 0);
    lv_obj_set_style_radius(console_tile, 8, 0);
    lv_obj_set_style_shadow_width(console_tile, 10, 0);
    lv_obj_set_style_shadow_opa(console_tile, LV_OPA_30, 0);
    lv_obj_add_flag(console_tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(console_tile, mode_select_console_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *con_icon = lv_label_create(console_tile);
    lv_label_set_text(con_icon, "[>_]");
    lv_obj_set_style_text_font(con_icon, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(con_icon, lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
    lv_obj_align(con_icon, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *con_label = lv_label_create(console_tile);
    lv_label_set_text(con_label, "Console Mode");
    lv_obj_set_style_text_font(con_label, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(con_label, lv_color_hex(RECOVERY_COLOR_TEXT_TITLE), 0);
    lv_obj_align(con_label, LV_ALIGN_CENTER, 0, 10);
    
    lv_obj_t *con_desc = lv_label_create(console_tile);
    lv_label_set_text(con_desc, "Text-based\nminimal render");
    lv_obj_set_style_text_font(con_desc, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(con_desc, lv_color_hex(RECOVERY_COLOR_TEXT_DESC), 0);
    lv_obj_set_style_text_align(con_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(con_desc, LV_ALIGN_BOTTOM_MID, 0, -15);
    
    // Hint at bottom
    lv_obj_t *hint = lv_label_create(g_mode_select_cont);
    lv_label_set_text(hint, "Tap to select or use BOOT button");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(hint, UI_FONT_DEFAULT, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);
}


//=============================================================================
// UI Mode Screen (WinRE Style)
//=============================================================================

static void tile_click_cb(lv_event_t *e)
{
    int tile_idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Tile clicked: %d", tile_idx);
    execute_tile_action((recovery_tile_t)tile_idx);
}

static void create_ui_mode_screen(void)
{
    if (g_ui_mode_cont) {
        lv_obj_del(g_ui_mode_cont);
    }
    
    g_ui_mode_cont = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_ui_mode_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(g_ui_mode_cont, lv_color_hex(RECOVERY_COLOR_BG), 0);
    lv_obj_set_style_border_width(g_ui_mode_cont, 0, 0);
    lv_obj_set_style_radius(g_ui_mode_cont, 0, 0);
    lv_obj_set_style_pad_all(g_ui_mode_cont, 0, 0);
    lv_obj_clear_flag(g_ui_mode_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_obj_create(g_ui_mode_cont);
    lv_obj_set_size(header, SCREEN_WIDTH, HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x005A9E), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_label_create(header);
    lv_label_set_text(back_btn, "<");
    lv_obj_set_style_text_color(back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(back_btn, UI_FONT_DEFAULT, 0);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 15, 0);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Win Recovery");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, UI_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 45, 0);
    
    // Tiles container
    int start_y = HEADER_HEIGHT + 20;
    int tile_start_x = (SCREEN_WIDTH - (TILE_WIDTH * 2 + TILE_MARGIN)) / 2;
    
    for (int i = 0; i < TILE_COUNT; i++) {
        int row = i / TILE_COLS;
        int col = i % TILE_COLS;
        int x = tile_start_x + col * (TILE_WIDTH + TILE_MARGIN);
        int y = start_y + row * (TILE_HEIGHT + TILE_MARGIN);
        
        lv_obj_t *tile = lv_obj_create(g_ui_mode_cont);
        lv_obj_set_size(tile, TILE_WIDTH, TILE_HEIGHT);
        lv_obj_set_pos(tile, x, y);
        lv_obj_set_style_bg_color(tile, lv_color_hex(RECOVERY_COLOR_TILE), 0);
        lv_obj_set_style_radius(tile, 4, 0);
        lv_obj_set_style_shadow_width(tile, 5, 0);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_20, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, tile_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        
        // Selection highlight (hidden by default)
        if (i == g_selected_tile) {
            lv_obj_set_style_border_width(tile, 3, 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
        }
        
        g_tiles[i] = tile;
        
        // Icon
        lv_obj_t *icon = lv_label_create(tile);
        lv_label_set_text(icon, g_tile_info[i].icon);
        lv_obj_set_style_text_font(icon, UI_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 15, -10);
        
        // Title
        lv_obj_t *lbl_title = lv_label_create(tile);
        lv_label_set_text(lbl_title, g_tile_info[i].title);
        lv_obj_set_style_text_font(lbl_title, UI_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(RECOVERY_COLOR_TEXT_TITLE), 0);
        lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 50, -10);
        
        // Description
        lv_obj_t *lbl_desc = lv_label_create(tile);
        lv_label_set_text(lbl_desc, g_tile_info[i].desc);
        lv_obj_set_style_text_font(lbl_desc, UI_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(lbl_desc, lv_color_hex(RECOVERY_COLOR_TEXT_DESC), 0);
        lv_obj_align(lbl_desc, LV_ALIGN_LEFT_MID, 50, 15);
    }
    
    // Status bar at bottom
    lv_obj_t *status = lv_obj_create(g_ui_mode_cont);
    lv_obj_set_size(status, SCREEN_WIDTH, STATUS_HEIGHT);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(0x005A9E), 0);
    lv_obj_set_style_border_width(status, 0, 0);
    lv_obj_set_style_radius(status, 0, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    
    // Get system info for status bar
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    char status_text[128];
    char heap_str[16], psram_str[16];
    recovery_format_bytes(info.free_heap, heap_str, sizeof(heap_str));
    recovery_format_bytes(info.free_psram, psram_str, sizeof(psram_str));
    snprintf(status_text, sizeof(status_text), "%s | Heap: %s | PSRAM: %s",
             info.chip_model, heap_str, psram_str);
    
    lv_obj_t *status_lbl = lv_label_create(status);
    lv_label_set_text(status_lbl, status_text);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(status_lbl, UI_FONT_DEFAULT, 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 0);
}

static void update_tile_selection(int old_sel, int new_sel)
{
    if (old_sel >= 0 && old_sel < TILE_COUNT && g_tiles[old_sel]) {
        lv_obj_set_style_border_width(g_tiles[old_sel], 0, 0);
    }
    if (new_sel >= 0 && new_sel < TILE_COUNT && g_tiles[new_sel]) {
        lv_obj_set_style_border_width(g_tiles[new_sel], 3, 0);
        lv_obj_set_style_border_color(g_tiles[new_sel], lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
    }
}


//=============================================================================
// Console Mode Screen
//=============================================================================

static char g_console_buffer[4096] = {0};
static int g_console_buffer_len = 0;

static void console_input_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(g_console_input);
        if (txt && strlen(txt) > 0) {
            // Echo command
            char echo[128];
            snprintf(echo, sizeof(echo), "> %s\n", txt);
            recovery_console_print(echo);
            
            // Process command
            recovery_console_process_cmd(txt);
            
            // Clear input
            lv_textarea_set_text(g_console_input, "");
        }
    }
}

static void create_console_screen(void)
{
    if (g_console_cont) {
        lv_obj_del(g_console_cont);
    }
    
    g_console_cont = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_console_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(g_console_cont, lv_color_hex(CONSOLE_COLOR_BG), 0);
    lv_obj_set_style_border_width(g_console_cont, 0, 0);
    lv_obj_set_style_radius(g_console_cont, 0, 0);
    lv_obj_set_style_pad_all(g_console_cont, 0, 0);
    lv_obj_clear_flag(g_console_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_label_create(g_console_cont);
    lv_label_set_text(header, "Win Recovery Console v1.0");
    lv_obj_set_style_text_color(header, lv_color_hex(CONSOLE_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(header, UI_FONT_DEFAULT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 10, 10);
    
    // System info line
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    char info_line[128];
    snprintf(info_line, sizeof(info_line), "%s | %luMB PSRAM | %luMB Flash",
             info.chip_model, (unsigned long)info.psram_size_mb, 
             (unsigned long)info.flash_size_mb);
    
    lv_obj_t *info_lbl = lv_label_create(g_console_cont);
    lv_label_set_text(info_lbl, info_line);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(CONSOLE_COLOR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(info_lbl, UI_FONT_DEFAULT, 0);
    lv_obj_align(info_lbl, LV_ALIGN_TOP_LEFT, 10, 35);
    
    // Separator
    lv_obj_t *sep = lv_label_create(g_console_cont);
    lv_label_set_text(sep, "────────────────────────────────────────────");
    lv_obj_set_style_text_color(sep, lv_color_hex(CONSOLE_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(sep, UI_FONT_DEFAULT, 0);
    lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 10, 55);
    
    // Output area (scrollable)
    g_console_output = lv_textarea_create(g_console_cont);
    lv_obj_set_size(g_console_output, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 210);
    lv_obj_align(g_console_output, LV_ALIGN_TOP_LEFT, 10, 80);
    lv_obj_set_style_bg_color(g_console_output, lv_color_hex(CONSOLE_COLOR_BG), 0);
    lv_obj_set_style_text_color(g_console_output, lv_color_hex(CONSOLE_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(g_console_output, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_border_width(g_console_output, 0, 0);
    lv_obj_set_style_pad_all(g_console_output, 5, 0);
    lv_textarea_set_text(g_console_output, "Type 'help' for available commands\n\n");
    lv_obj_clear_flag(g_console_output, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_console_output, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    
    // Input area
    g_console_input = lv_textarea_create(g_console_cont);
    lv_obj_set_size(g_console_input, SCREEN_WIDTH - 20, CONSOLE_INPUT_HEIGHT);
    lv_obj_align(g_console_input, LV_ALIGN_BOTTOM_LEFT, 10, -160);
    lv_obj_set_style_bg_color(g_console_input, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(g_console_input, lv_color_hex(CONSOLE_COLOR_PROMPT), 0);
    lv_obj_set_style_text_font(g_console_input, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_border_color(g_console_input, lv_color_hex(CONSOLE_COLOR_TEXT), 0);
    lv_obj_set_style_border_width(g_console_input, 1, 0);
    lv_textarea_set_placeholder_text(g_console_input, "> Enter command...");
    lv_textarea_set_one_line(g_console_input, true);
    lv_obj_add_event_cb(g_console_input, console_input_cb, LV_EVENT_READY, NULL);
    
    // Keyboard (compact, dark theme)
    g_keyboard = lv_keyboard_create(g_console_cont);
    lv_obj_set_size(g_keyboard, SCREEN_WIDTH, 135);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(g_keyboard, g_console_input);
    lv_obj_set_style_bg_color(g_keyboard, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(g_keyboard, lv_color_hex(0x2A2A2A), LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_keyboard, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_border_width(g_keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(g_keyboard, 4, LV_PART_ITEMS);
    
    // Clear buffer
    g_console_buffer[0] = '\0';
    g_console_buffer_len = 0;
}

void recovery_console_init(void)
{
    g_console_buffer[0] = '\0';
    g_console_buffer_len = 0;
}

void recovery_console_print(const char *text)
{
    if (!text || !g_console_output) return;
    
    // Append to buffer
    int len = strlen(text);
    if (g_console_buffer_len + len < sizeof(g_console_buffer) - 1) {
        strcat(g_console_buffer, text);
        g_console_buffer_len += len;
    } else {
        // Buffer full, shift content
        int shift = len + 256;
        if (shift < g_console_buffer_len) {
            memmove(g_console_buffer, g_console_buffer + shift, g_console_buffer_len - shift);
            g_console_buffer_len -= shift;
            g_console_buffer[g_console_buffer_len] = '\0';
            strcat(g_console_buffer, text);
            g_console_buffer_len += len;
        }
    }
    
    lv_textarea_set_text(g_console_output, g_console_buffer);
    // Scroll to bottom
    lv_textarea_set_cursor_pos(g_console_output, LV_TEXTAREA_CURSOR_LAST);
}

void recovery_console_print_color(const char *text, uint32_t color)
{
    // LVGL textarea doesn't support mixed colors, just print normally
    recovery_console_print(text);
}

void recovery_console_clear(void)
{
    g_console_buffer[0] = '\0';
    g_console_buffer_len = 0;
    if (g_console_output) {
        lv_textarea_set_text(g_console_output, "");
    }
}


//=============================================================================
// Console Commands
//=============================================================================

void recovery_console_process_cmd(const char *cmd)
{
    if (!cmd) return;
    
    // Skip leading whitespace
    while (*cmd == ' ') cmd++;
    
    if (strlen(cmd) == 0) return;
    
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "sysinfo") == 0) {
        cmd_sysinfo();
    } else if (strcmp(cmd, "reboot") == 0) {
        cmd_reboot();
    } else if (strcmp(cmd, "bootloader") == 0) {
        cmd_bootloader();
    } else if (strcmp(cmd, "wipe data") == 0) {
        cmd_wipe_data();
    } else if (strcmp(cmd, "wipe wifi") == 0) {
        cmd_wipe_wifi();
    } else if (strcmp(cmd, "wipe lock") == 0) {
        cmd_wipe_lock();
    } else if (strcmp(cmd, "factory") == 0) {
        cmd_factory();
    } else if (strcmp(cmd, "log") == 0) {
        cmd_log();
    } else if (strcmp(cmd, "partitions") == 0) {
        cmd_partitions();
    } else if (strcmp(cmd, "memtest") == 0) {
        cmd_memtest();
    } else if (strcmp(cmd, "displaytest") == 0) {
        cmd_displaytest();
    } else if (strcmp(cmd, "sdtest") == 0) {
        cmd_sdtest();
    } else if (strcmp(cmd, "poweroff") == 0) {
        cmd_poweroff();
    } else if (strcmp(cmd, "ui") == 0) {
        cmd_ui();
    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        recovery_console_clear();
    } else {
        recovery_console_print("Unknown command. Type 'help' for list.\n");
    }
}

static void cmd_help(void)
{
    recovery_console_print(
        "Available commands:\n"
        "  help        - Show this help\n"
        "  sysinfo     - Display system information\n"
        "  reboot      - Reboot to normal mode\n"
        "  bootloader  - Reboot to USB download mode\n"
        "  wipe data   - Wipe user data (LittleFS)\n"
        "  wipe wifi   - Clear WiFi settings\n"
        "  wipe lock   - Reset lock screen\n"
        "  factory     - Factory reset (wipe all)\n"
        "  log         - View system log\n"
        "  partitions  - Show partition table\n"
        "  memtest     - Run memory test\n"
        "  displaytest - Run display test\n"
        "  sdtest      - Test SD card\n"
        "  poweroff    - Shut down device\n"
        "  ui          - Switch to UI mode\n"
        "  clear       - Clear console\n"
    );
}

static void cmd_sysinfo(void)
{
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    char buf[512];
    char heap_str[16], psram_str[16], lfs_total[16], lfs_used[16];
    
    recovery_format_bytes(info.free_heap, heap_str, sizeof(heap_str));
    recovery_format_bytes(info.free_psram, psram_str, sizeof(psram_str));
    recovery_format_bytes(info.littlefs_total, lfs_total, sizeof(lfs_total));
    recovery_format_bytes(info.littlefs_used, lfs_used, sizeof(lfs_used));
    
    snprintf(buf, sizeof(buf),
        "System Information:\n"
        "  Chip: %s rev %d.%d\n"
        "  Cores: %d\n"
        "  Flash: %lu MB\n"
        "  PSRAM: %lu MB (Free: %s)\n"
        "  Heap Free: %s\n"
        "  LittleFS: %s / %s\n"
        "  SD Card: %s\n"
        "  WiFi MAC: %s\n"
        "  Reset: %s\n"
        "  IDF: %s\n"
        "  Build: %s %s\n",
        info.chip_model, info.chip_revision / 100, info.chip_revision % 100,
        info.cores,
        (unsigned long)info.flash_size_mb,
        (unsigned long)info.psram_size_mb, psram_str,
        heap_str,
        lfs_used, lfs_total,
        info.sd_mounted ? info.sd_type : "Not inserted",
        info.wifi_mac_str,
        recovery_get_reset_reason_str(info.reset_reason),
        info.idf_version,
        info.compile_date, info.compile_time
    );
    
    recovery_console_print(buf);
}

static void cmd_reboot(void)
{
    recovery_console_print("Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    recovery_clear_flag();
    esp_restart();
}

static void cmd_bootloader(void)
{
    recovery_console_print("Rebooting to USB download mode...\n");
    recovery_console_print("Hold BOOT button during reset to enter bootloader.\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // On ESP32-P4, we can use REG_WRITE to set boot mode
    // For now, just restart - user needs to hold BOOT button
    recovery_clear_flag();
    esp_restart();
}

static void cmd_wipe_data(void)
{
    recovery_console_print("Wiping user data...\n");
    
    // Unmount and format LittleFS
    esp_littlefs_format("storage");
    
    recovery_console_print("User data wiped successfully.\n");
}

static void cmd_wipe_wifi(void)
{
    recovery_console_print("Clearing WiFi settings...\n");
    
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
        recovery_console_print("WiFi settings cleared.\n");
    } else {
        recovery_console_print("Failed to clear WiFi settings.\n");
    }
}

static void cmd_wipe_lock(void)
{
    recovery_console_print("Resetting lock screen...\n");
    
    // Clear NVS settings
    nvs_handle_t nvs;
    if (nvs_open("settings", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "lock_enabled");
        nvs_erase_key(nvs, "lock_pin");
        nvs_erase_key(nvs, "lock_type");
        nvs_commit(nvs);
        nvs_close(nvs);
        recovery_console_print("NVS lock settings cleared.\n");
    }
    
    // Remove system config file to reset all user settings including password
    if (remove("/littlefs/system.cfg") == 0) {
        recovery_console_print("System config file removed.\n");
    } else {
        recovery_console_print("Config file not found or already removed.\n");
    }
    
    recovery_console_print("Lock screen reset complete.\n");
    recovery_console_print("Reboot to apply changes.\n");
}

static void cmd_factory(void)
{
    recovery_console_print("WARNING: This will erase ALL data!\n");
    recovery_console_print("Performing factory reset...\n");
    
    // Wipe LittleFS
    esp_littlefs_format("storage");
    
    // Wipe NVS
    nvs_flash_erase();
    nvs_flash_init();
    
    recovery_console_print("Factory reset complete. Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void cmd_log(void)
{
    recovery_console_print("=== System Event Log ===\n\n");
    
    // Get system info
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    char buf[256];
    
    // Boot count
    uint32_t boot_count = recovery_get_boot_count();
    snprintf(buf, sizeof(buf), "Boot count: %lu\n", (unsigned long)boot_count);
    recovery_console_print(buf);
    
    // Last reset reason
    snprintf(buf, sizeof(buf), "Last reset: %s\n", 
             recovery_get_reset_reason_str(info.reset_reason));
    recovery_console_print(buf);
    
    // Uptime (approximate from tick count)
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t uptime_sec = uptime_ms / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    uint32_t secs = uptime_sec % 60;
    snprintf(buf, sizeof(buf), "Uptime: %02lu:%02lu:%02lu\n", 
             (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    recovery_console_print(buf);
    
    // Memory status
    recovery_console_print("\n--- Memory Status ---\n");
    char heap_str[16], min_heap_str[16], psram_str[16];
    recovery_format_bytes(info.free_heap, heap_str, sizeof(heap_str));
    recovery_format_bytes(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), min_heap_str, sizeof(min_heap_str));
    recovery_format_bytes(info.free_psram, psram_str, sizeof(psram_str));
    
    snprintf(buf, sizeof(buf), "Heap free: %s\n", heap_str);
    recovery_console_print(buf);
    snprintf(buf, sizeof(buf), "Heap min:  %s\n", min_heap_str);
    recovery_console_print(buf);
    snprintf(buf, sizeof(buf), "PSRAM free: %s\n", psram_str);
    recovery_console_print(buf);
    
    // Storage status
    recovery_console_print("\n--- Storage Status ---\n");
    char lfs_used[16], lfs_total[16];
    recovery_format_bytes(info.littlefs_used, lfs_used, sizeof(lfs_used));
    recovery_format_bytes(info.littlefs_total, lfs_total, sizeof(lfs_total));
    snprintf(buf, sizeof(buf), "LittleFS: %s / %s\n", lfs_used, lfs_total);
    recovery_console_print(buf);
    
    if (info.sd_mounted) {
        char sd_free_str[16], sd_total_str[16];
        recovery_format_bytes(info.sd_free, sd_free_str, sizeof(sd_free_str));
        recovery_format_bytes(info.sd_total, sd_total_str, sizeof(sd_total_str));
        snprintf(buf, sizeof(buf), "SD Card:  %s free / %s total (%s)\n", sd_free_str, sd_total_str, info.sd_type);
        recovery_console_print(buf);
    } else {
        recovery_console_print("SD Card:  Not mounted\n");
    }
    
    // WiFi status
    recovery_console_print("\n--- Network ---\n");
    snprintf(buf, sizeof(buf), "WiFi MAC: %s\n", info.wifi_mac_str);
    recovery_console_print(buf);
    
    // Build info
    recovery_console_print("\n--- Build Info ---\n");
    snprintf(buf, sizeof(buf), "IDF: %s\n", info.idf_version);
    recovery_console_print(buf);
    snprintf(buf, sizeof(buf), "Built: %s %s\n", info.compile_date, info.compile_time);
    recovery_console_print(buf);
    
    recovery_console_print("\nNote: Full logs available via serial monitor.\n");
}

static void cmd_partitions(void)
{
    char buf[1024];
    int count = recovery_get_partition_info(buf, sizeof(buf));
    recovery_console_print(buf);
    
    char summary[64];
    snprintf(summary, sizeof(summary), "\nTotal: %d partitions\n", count);
    recovery_console_print(summary);
}

static void cmd_memtest(void)
{
    recovery_console_print("Running memory test...\n");
    
    // Test PSRAM
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t test_size = psram_free > 1024*1024 ? 1024*1024 : psram_free / 2;
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Testing %lu bytes of PSRAM...\n", (unsigned long)test_size);
    recovery_console_print(buf);
    
    uint8_t *test_buf = (uint8_t*)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM);
    if (!test_buf) {
        recovery_console_print("FAIL: Could not allocate test buffer\n");
        return;
    }
    
    // Write pattern
    for (size_t i = 0; i < test_size; i++) {
        test_buf[i] = (uint8_t)(i & 0xFF);
    }
    
    // Verify pattern
    bool pass = true;
    for (size_t i = 0; i < test_size; i++) {
        if (test_buf[i] != (uint8_t)(i & 0xFF)) {
            pass = false;
            snprintf(buf, sizeof(buf), "FAIL at offset %lu: expected %02X, got %02X\n",
                     (unsigned long)i, (uint8_t)(i & 0xFF), test_buf[i]);
            recovery_console_print(buf);
            break;
        }
    }
    
    heap_caps_free(test_buf);
    
    if (pass) {
        recovery_console_print("PASS: Memory test completed successfully\n");
    }
}

static void cmd_displaytest(void)
{
    recovery_console_print("Running display test...\n");
    recovery_console_print("Colors: Red, Green, Blue, White, Black\n");
    
    // Create fullscreen color overlay
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    
    uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000};
    const char *names[] = {"Red", "Green", "Blue", "White", "Black"};
    
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(overlay, lv_color_hex(colors[i]), 0);
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    
    lv_obj_del(overlay);
    recovery_console_print("Display test complete.\n");
}

static void cmd_sdtest(void)
{
    recovery_console_print("Testing SD card...\n");
    
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    if (!info.sd_mounted) {
        recovery_console_print("FAIL: No SD card detected\n");
        recovery_console_print("Insert SD card and try again.\n");
        return;
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), "SD Card detected: %s\n", info.sd_type);
    recovery_console_print(buf);
    
    char size_str[16];
    recovery_format_bytes(info.sd_total, size_str, sizeof(size_str));
    snprintf(buf, sizeof(buf), "Capacity: %s\n", size_str);
    recovery_console_print(buf);
    
    // Try to write and read a test file
    recovery_console_print("Testing read/write...\n");
    
    FILE *f = fopen("/sdcard/recovery_test.tmp", "w");
    if (f) {
        const char *test_data = "Win Recovery SD Test 12345";
        fprintf(f, "%s", test_data);
        fclose(f);
        
        // Read back
        f = fopen("/sdcard/recovery_test.tmp", "r");
        if (f) {
            char read_buf[64] = {0};
            fgets(read_buf, sizeof(read_buf), f);
            fclose(f);
            
            // Delete test file
            remove("/sdcard/recovery_test.tmp");
            
            if (strcmp(read_buf, test_data) == 0) {
                recovery_console_print("PASS: SD card read/write OK\n");
            } else {
                recovery_console_print("FAIL: Data mismatch\n");
            }
        } else {
            recovery_console_print("FAIL: Could not read test file\n");
        }
    } else {
        recovery_console_print("FAIL: Could not write test file\n");
        recovery_console_print("SD card may be read-only or full.\n");
    }
}

static void cmd_poweroff(void)
{
    recovery_console_print("Shutting down...\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    // Turn off backlight and enter deep sleep
    hw_backlight_set(0);
    esp_deep_sleep_start();
}

static void cmd_ui(void)
{
    recovery_ui_set_mode(RECOVERY_MODE_UI);
}


//=============================================================================
// Tile Actions
//=============================================================================

static lv_obj_t *g_confirm_dialog = NULL;
static lv_event_cb_t g_confirm_callback = NULL;

static void confirm_yes_cb(lv_event_t *e)
{
    if (g_confirm_dialog) {
        lv_obj_del(g_confirm_dialog);
        g_confirm_dialog = NULL;
    }
    if (g_confirm_callback) {
        g_confirm_callback(e);
    }
}

static void confirm_no_cb(lv_event_t *e)
{
    if (g_confirm_dialog) {
        lv_obj_del(g_confirm_dialog);
        g_confirm_dialog = NULL;
    }
}

static void show_confirmation_dialog(const char *title, const char *msg, lv_event_cb_t confirm_cb)
{
    g_confirm_callback = confirm_cb;
    
    g_confirm_dialog = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_confirm_dialog, 350, 200);
    lv_obj_center(g_confirm_dialog);
    lv_obj_set_style_bg_color(g_confirm_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(g_confirm_dialog, 8, 0);
    lv_obj_set_style_shadow_width(g_confirm_dialog, 20, 0);
    lv_obj_set_style_shadow_opa(g_confirm_dialog, LV_OPA_50, 0);
    lv_obj_clear_flag(g_confirm_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *lbl_title = lv_label_create(g_confirm_dialog);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x000000), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *lbl_msg = lv_label_create(g_confirm_dialog);
    lv_label_set_text(lbl_msg, msg);
    lv_obj_set_style_text_font(lbl_msg, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_msg, 300);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, -10);
    
    // Yes button
    lv_obj_t *btn_yes = lv_btn_create(g_confirm_dialog);
    lv_obj_set_size(btn_yes, 100, 40);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_yes, confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, "Yes");
    lv_obj_set_style_text_color(lbl_yes, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_yes);
    
    // No button
    lv_obj_t *btn_no = lv_btn_create(g_confirm_dialog);
    lv_obj_set_size(btn_no, 100, 40);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x888888), 0);
    lv_obj_add_event_cb(btn_no, confirm_no_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "No");
    lv_obj_set_style_text_color(lbl_no, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_no);
}

static void do_wipe_data(lv_event_t *e)
{
    esp_littlefs_format("storage");
    ESP_LOGI(TAG, "User data wiped");
    // Show success message
    if (g_confirm_dialog) {
        lv_obj_del(g_confirm_dialog);
        g_confirm_dialog = NULL;
    }
    show_confirmation_dialog("Success", "User data wiped.\nReboot to apply changes.", NULL);
}

static void do_factory_reset(lv_event_t *e)
{
    esp_littlefs_format("storage");
    nvs_flash_erase();
    nvs_flash_init();
    ESP_LOGI(TAG, "Factory reset complete, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void do_reset_lock(lv_event_t *e)
{
    ESP_LOGI(TAG, "Resetting lock screen...");
    
    // Method 1: Clear NVS settings namespace (legacy)
    nvs_handle_t nvs;
    if (nvs_open("settings", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "lock_enabled");
        nvs_erase_key(nvs, "lock_pin");
        nvs_erase_key(nvs, "lock_type");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    
    // Method 2: Modify system.cfg file directly
    // Read current settings, clear password, save back
    FILE *f = fopen("/littlefs/system.cfg", "rb");
    if (f) {
        // Get file size
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (size > 0 && size < 4096) {
            uint8_t *buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (buf) {
                fread(buf, 1, size, f);
                fclose(f);
                
                // The settings structure has user profile with password
                // We need to find and clear the password field
                // For safety, we'll just delete the file and let system recreate with defaults
                // Or we can try to patch it
                
                // Simple approach: delete the config file
                // System will recreate with defaults on next boot
                remove("/littlefs/system.cfg");
                
                heap_caps_free(buf);
                ESP_LOGI(TAG, "Lock screen reset - config file removed");
            } else {
                fclose(f);
            }
        } else {
            fclose(f);
        }
    }
    
    // Show success message
    if (g_confirm_dialog) {
        lv_obj_del(g_confirm_dialog);
        g_confirm_dialog = NULL;
    }
    show_confirmation_dialog("Success", "Lock screen reset.\nReboot to apply changes.", NULL);
}

// Startup Settings screen
static lv_obj_t *g_startup_settings_screen = NULL;

static void startup_settings_back_cb(lv_event_t *e)
{
    if (g_startup_settings_screen) {
        lv_obj_del(g_startup_settings_screen);
        g_startup_settings_screen = NULL;
    }
}

static void startup_settings_item_cb(lv_event_t *e)
{
    const char *action = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Startup setting: %s", action);
    
    if (strcmp(action, "normal") == 0) {
        // Normal boot - just reboot
        recovery_clear_flag();
        esp_restart();
    } else if (strcmp(action, "safe") == 0) {
        // Safe mode - set flag and reboot
        // For now, just reboot normally
        recovery_clear_flag();
        esp_restart();
    } else if (strcmp(action, "bootloader") == 0) {
        // Bootloader mode
        cmd_bootloader();
    }
}

static void show_startup_settings(void)
{
    g_startup_settings_screen = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_startup_settings_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(g_startup_settings_screen, lv_color_hex(RECOVERY_COLOR_BG), 0);
    lv_obj_set_style_border_width(g_startup_settings_screen, 0, 0);
    lv_obj_set_style_radius(g_startup_settings_screen, 0, 0);
    lv_obj_clear_flag(g_startup_settings_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_obj_create(g_startup_settings_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x005A9E), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x004080), 0);
    lv_obj_add_event_cb(back_btn, startup_settings_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "<");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_label);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Startup Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, UI_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Options
    static const char *options[] = {
        "1. Normal Boot",
        "2. Safe Mode",
        "3. USB Download Mode"
    };
    static const char *descs[] = {
        "Start Windows normally",
        "Start with minimal drivers",
        "Enter bootloader for flashing"
    };
    static const char *actions[] = {"normal", "safe", "bootloader"};
    
    int y = 100;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *item = lv_obj_create(g_startup_settings_screen);
        lv_obj_set_size(item, SCREEN_WIDTH - 40, 80);
        lv_obj_set_pos(item, 20, y);
        lv_obj_set_style_bg_color(item, lv_color_hex(RECOVERY_COLOR_TILE), 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(item, startup_settings_item_cb, LV_EVENT_CLICKED, (void*)actions[i]);
        
        lv_obj_t *opt_label = lv_label_create(item);
        lv_label_set_text(opt_label, options[i]);
        lv_obj_set_style_text_color(opt_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(opt_label, UI_FONT_DEFAULT, 0);
        lv_obj_align(opt_label, LV_ALIGN_TOP_LEFT, 15, 15);
        
        lv_obj_t *desc_label = lv_label_create(item);
        lv_label_set_text(desc_label, descs[i]);
        lv_obj_set_style_text_color(desc_label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(desc_label, UI_FONT_DEFAULT, 0);
        lv_obj_align(desc_label, LV_ALIGN_BOTTOM_LEFT, 15, -15);
        
        y += 95;
    }
    
    // Hint
    lv_obj_t *hint = lv_label_create(g_startup_settings_screen);
    lv_label_set_text(hint, "Select boot option or tap Back");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(hint, UI_FONT_DEFAULT, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);
}

// Diagnostics screen
static lv_obj_t *g_diagnostics_screen = NULL;
static lv_obj_t *g_diag_result_dialog = NULL;
static lv_obj_t *g_diag_result_text = NULL;

static void diag_result_close_cb(lv_event_t *e)
{
    if (g_diag_result_dialog) {
        lv_obj_del(g_diag_result_dialog);
        g_diag_result_dialog = NULL;
        g_diag_result_text = NULL;
    }
}

static void show_diag_result(const char *title, const char *result)
{
    g_diag_result_dialog = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_diag_result_dialog, SCREEN_WIDTH - 40, 500);
    lv_obj_center(g_diag_result_dialog);
    lv_obj_set_style_bg_color(g_diag_result_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(g_diag_result_dialog, 8, 0);
    lv_obj_set_style_shadow_width(g_diag_result_dialog, 20, 0);
    lv_obj_set_style_shadow_opa(g_diag_result_dialog, LV_OPA_50, 0);
    lv_obj_clear_flag(g_diag_result_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *lbl_title = lv_label_create(g_diag_result_dialog);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x000000), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Scrollable result area
    g_diag_result_text = lv_textarea_create(g_diag_result_dialog);
    lv_obj_set_size(g_diag_result_text, SCREEN_WIDTH - 80, 380);
    lv_obj_align(g_diag_result_text, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(g_diag_result_text, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_text_color(g_diag_result_text, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(g_diag_result_text, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_border_width(g_diag_result_text, 1, 0);
    lv_obj_set_style_border_color(g_diag_result_text, lv_color_hex(0xCCCCCC), 0);
    lv_textarea_set_text(g_diag_result_text, result);
    lv_obj_clear_flag(g_diag_result_text, LV_OBJ_FLAG_CLICKABLE);
    
    // Close button
    lv_obj_t *btn_close = lv_btn_create(g_diag_result_dialog);
    lv_obj_set_size(btn_close, 100, 40);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(RECOVERY_COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_close, diag_result_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "OK");
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_close);
}

static void diagnostics_back_cb(lv_event_t *e)
{
    if (g_diagnostics_screen) {
        lv_obj_del(g_diagnostics_screen);
        g_diagnostics_screen = NULL;
    }
}

static void run_memtest_ui(void)
{
    char result[512];
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t test_size = psram_free > 1024*1024 ? 1024*1024 : psram_free / 2;
    
    snprintf(result, sizeof(result), "Testing %lu bytes of PSRAM...\n\n", (unsigned long)test_size);
    
    uint8_t *test_buf = (uint8_t*)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM);
    if (!test_buf) {
        strcat(result, "FAIL: Could not allocate test buffer\n");
        show_diag_result("Memory Test", result);
        return;
    }
    
    // Write pattern
    for (size_t i = 0; i < test_size; i++) {
        test_buf[i] = (uint8_t)(i & 0xFF);
    }
    
    // Verify pattern
    bool pass = true;
    for (size_t i = 0; i < test_size; i++) {
        if (test_buf[i] != (uint8_t)(i & 0xFF)) {
            pass = false;
            char err[128];
            snprintf(err, sizeof(err), "FAIL at offset %lu:\nExpected %02X, got %02X\n",
                     (unsigned long)i, (uint8_t)(i & 0xFF), test_buf[i]);
            strcat(result, err);
            break;
        }
    }
    
    heap_caps_free(test_buf);
    
    if (pass) {
        strcat(result, "PASS: Memory test completed successfully!\n\n");
        
        char info[128];
        char heap_str[16], psram_str[16];
        recovery_format_bytes(heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_str, sizeof(heap_str));
        recovery_format_bytes(heap_caps_get_free_size(MALLOC_CAP_SPIRAM), psram_str, sizeof(psram_str));
        snprintf(info, sizeof(info), "Free Heap: %s\nFree PSRAM: %s\n", heap_str, psram_str);
        strcat(result, info);
    }
    
    show_diag_result("Memory Test", result);
}

static void run_sysinfo_ui(void)
{
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    char result[1024];
    char heap_str[16], psram_str[16], lfs_total[16], lfs_used[16];
    
    recovery_format_bytes(info.free_heap, heap_str, sizeof(heap_str));
    recovery_format_bytes(info.free_psram, psram_str, sizeof(psram_str));
    recovery_format_bytes(info.littlefs_total, lfs_total, sizeof(lfs_total));
    recovery_format_bytes(info.littlefs_used, lfs_used, sizeof(lfs_used));
    
    snprintf(result, sizeof(result),
        "Chip: %s rev %d.%d\n"
        "Cores: %d\n"
        "Flash: %lu MB\n"
        "PSRAM: %lu MB (Free: %s)\n"
        "Heap Free: %s\n"
        "LittleFS: %s / %s\n"
        "SD Card: %s\n"
        "WiFi MAC: %s\n"
        "Reset: %s\n"
        "IDF: %s\n"
        "Build: %s %s\n",
        info.chip_model, info.chip_revision / 100, info.chip_revision % 100,
        info.cores,
        (unsigned long)info.flash_size_mb,
        (unsigned long)info.psram_size_mb, psram_str,
        heap_str,
        lfs_used, lfs_total,
        info.sd_mounted ? info.sd_type : "Not inserted",
        info.wifi_mac_str,
        recovery_get_reset_reason_str(info.reset_reason),
        info.idf_version,
        info.compile_date, info.compile_time
    );
    
    show_diag_result("System Information", result);
}

static void run_sdtest_ui(void)
{
    char result[512];
    recovery_sysinfo_t info;
    recovery_get_sysinfo(&info);
    
    if (!info.sd_mounted) {
        snprintf(result, sizeof(result), 
            "FAIL: No SD card detected\n\n"
            "Insert SD card and try again.\n");
        show_diag_result("SD Card Test", result);
        return;
    }
    
    char size_str[16];
    recovery_format_bytes(info.sd_total, size_str, sizeof(size_str));
    snprintf(result, sizeof(result), "SD Card: %s\nCapacity: %s\n\nTesting read/write...\n", 
             info.sd_type, size_str);
    
    // Try to write and read a test file
    FILE *f = fopen("/sdcard/recovery_test.tmp", "w");
    if (f) {
        const char *test_data = "Win Recovery SD Test 12345";
        fprintf(f, "%s", test_data);
        fclose(f);
        
        // Read back
        f = fopen("/sdcard/recovery_test.tmp", "r");
        if (f) {
            char read_buf[64] = {0};
            fgets(read_buf, sizeof(read_buf), f);
            fclose(f);
            
            // Delete test file
            remove("/sdcard/recovery_test.tmp");
            
            if (strcmp(read_buf, test_data) == 0) {
                strcat(result, "\nPASS: SD card read/write OK\n");
            } else {
                strcat(result, "\nFAIL: Data mismatch\n");
            }
        } else {
            strcat(result, "\nFAIL: Could not read test file\n");
        }
    } else {
        strcat(result, "\nFAIL: Could not write test file\nSD card may be read-only or full.\n");
    }
    
    show_diag_result("SD Card Test", result);
}

static void diagnostics_item_cb(lv_event_t *e)
{
    const char *action = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Diagnostic: %s", action);
    
    // Close diagnostics screen first
    if (g_diagnostics_screen) {
        lv_obj_del(g_diagnostics_screen);
        g_diagnostics_screen = NULL;
    }
    
    if (strcmp(action, "memtest") == 0) {
        run_memtest_ui();
    } else if (strcmp(action, "displaytest") == 0) {
        // Display test runs fullscreen, no dialog needed
        cmd_displaytest();
    } else if (strcmp(action, "sdtest") == 0) {
        run_sdtest_ui();
    } else if (strcmp(action, "sysinfo") == 0) {
        run_sysinfo_ui();
    }
}

static void show_diagnostics(void)
{
    g_diagnostics_screen = lv_obj_create(g_recovery_screen);
    lv_obj_set_size(g_diagnostics_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(g_diagnostics_screen, lv_color_hex(RECOVERY_COLOR_BG), 0);
    lv_obj_set_style_border_width(g_diagnostics_screen, 0, 0);
    lv_obj_set_style_radius(g_diagnostics_screen, 0, 0);
    lv_obj_clear_flag(g_diagnostics_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_obj_create(g_diagnostics_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x005A9E), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x004080), 0);
    lv_obj_add_event_cb(back_btn, diagnostics_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "<");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_label);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "System Diagnostics");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, UI_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Options
    static const char *options[] = {
        "Memory Test",
        "Display Test",
        "SD Card Test",
        "System Info"
    };
    static const char *descs[] = {
        "Test PSRAM read/write",
        "Test display colors",
        "Test SD card read/write",
        "View hardware info"
    };
    static const char *actions[] = {"memtest", "displaytest", "sdtest", "sysinfo"};
    
    int y = 80;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(g_diagnostics_screen);
        lv_obj_set_size(item, SCREEN_WIDTH - 40, 70);
        lv_obj_set_pos(item, 20, y);
        lv_obj_set_style_bg_color(item, lv_color_hex(RECOVERY_COLOR_TILE), 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(item, diagnostics_item_cb, LV_EVENT_CLICKED, (void*)actions[i]);
        
        lv_obj_t *opt_label = lv_label_create(item);
        lv_label_set_text(opt_label, options[i]);
        lv_obj_set_style_text_color(opt_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(opt_label, UI_FONT_DEFAULT, 0);
        lv_obj_align(opt_label, LV_ALIGN_TOP_LEFT, 15, 12);
        
        lv_obj_t *desc_label = lv_label_create(item);
        lv_label_set_text(desc_label, descs[i]);
        lv_obj_set_style_text_color(desc_label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(desc_label, UI_FONT_DEFAULT, 0);
        lv_obj_align(desc_label, LV_ALIGN_BOTTOM_LEFT, 15, -12);
        
        y += 85;
    }
}

static void execute_tile_action(recovery_tile_t tile)
{
    switch (tile) {
        case TILE_REBOOT:
            ESP_LOGI(TAG, "Rebooting...");
            recovery_clear_flag();
            esp_restart();
            break;
            
        case TILE_CONSOLE:
            recovery_ui_set_mode(RECOVERY_MODE_CONSOLE);
            break;
            
        case TILE_WIPE_DATA:
            show_confirmation_dialog("Wipe User Data",
                "This will erase all user settings\nand files. Continue?",
                do_wipe_data);
            break;
            
        case TILE_STARTUP_SETTINGS:
            show_startup_settings();
            break;
            
        case TILE_RESET_LOCK:
            show_confirmation_dialog("Reset Lock Screen",
                "This will remove PIN/password.\nContinue?",
                do_reset_lock);
            break;
            
        case TILE_DIAGNOSTICS:
            show_diagnostics();
            break;
            
        case TILE_FACTORY_RESET:
            show_confirmation_dialog("Factory Reset",
                "This will ERASE ALL DATA!\nDevice will be reset to factory state.\nContinue?",
                do_factory_reset);
            break;
            
        case TILE_POWER_OFF:
            ESP_LOGI(TAG, "Powering off...");
            hw_backlight_set(0);
            esp_deep_sleep_start();
            break;
            
        default:
            break;
    }
}

//=============================================================================
// Public API
//=============================================================================

void recovery_ui_start(void)
{
    ESP_LOGI(TAG, "Starting Recovery UI");
    
    g_recovery_active = true;
    g_selected_tile = 0;
    
    // Clear recovery flag to prevent boot loop
    recovery_clear_flag();
    
    // Create recovery screen
    g_recovery_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_recovery_screen, lv_color_hex(RECOVERY_COLOR_BG), 0);
    lv_scr_load(g_recovery_screen);
    
    // Check preferred mode
    recovery_display_mode_t preferred = recovery_get_preferred_mode();
    if (preferred == RECOVERY_MODE_UI || preferred == RECOVERY_MODE_CONSOLE) {
        g_current_mode = preferred;
    } else {
        g_current_mode = RECOVERY_MODE_SELECT;
    }
    
    // Show appropriate screen
    if (g_current_mode == RECOVERY_MODE_SELECT) {
        create_mode_select_screen();
    } else if (g_current_mode == RECOVERY_MODE_UI) {
        create_ui_mode_screen();
    } else {
        create_console_screen();
    }
    
    ESP_LOGI(TAG, "Recovery UI started in mode %d", g_current_mode);
}

void recovery_ui_set_mode(recovery_display_mode_t mode)
{
    if (mode == g_current_mode) return;
    
    ESP_LOGI(TAG, "Switching to mode %d", mode);
    
    // Hide current screen
    if (g_mode_select_cont) {
        lv_obj_add_flag(g_mode_select_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui_mode_cont) {
        lv_obj_add_flag(g_ui_mode_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_console_cont) {
        lv_obj_add_flag(g_console_cont, LV_OBJ_FLAG_HIDDEN);
    }
    
    g_current_mode = mode;
    recovery_set_preferred_mode(mode);
    
    // Show new screen
    switch (mode) {
        case RECOVERY_MODE_SELECT:
            create_mode_select_screen();
            break;
        case RECOVERY_MODE_UI:
            create_ui_mode_screen();
            break;
        case RECOVERY_MODE_CONSOLE:
            create_console_screen();
            break;
    }
}

recovery_display_mode_t recovery_ui_get_mode(void)
{
    return g_current_mode;
}

void recovery_ui_handle_button(int event)
{
    boot_button_event_t btn = (boot_button_event_t)event;
    
    if (g_current_mode == RECOVERY_MODE_SELECT) {
        // In mode select, single press toggles selection, long press confirms
        static int mode_selection = 0;  // 0 = UI, 1 = Console
        
        if (btn == BOOT_BTN_SINGLE) {
            mode_selection = 1 - mode_selection;
            // TODO: Update visual selection
        } else if (btn == BOOT_BTN_LONG) {
            if (mode_selection == 0) {
                recovery_ui_set_mode(RECOVERY_MODE_UI);
            } else {
                recovery_ui_set_mode(RECOVERY_MODE_CONSOLE);
            }
        }
    } else if (g_current_mode == RECOVERY_MODE_UI) {
        // In UI mode, single press moves selection, long press activates
        if (btn == BOOT_BTN_SINGLE) {
            int old_sel = g_selected_tile;
            g_selected_tile = (g_selected_tile + 1) % TILE_COUNT;
            update_tile_selection(old_sel, g_selected_tile);
        } else if (btn == BOOT_BTN_LONG) {
            execute_tile_action((recovery_tile_t)g_selected_tile);
        }
    }
    // Console mode uses keyboard, button not needed
}

bool recovery_ui_is_active(void)
{
    return g_recovery_active;
}

void recovery_ui_exit_and_reboot(void)
{
    ESP_LOGI(TAG, "Exiting recovery, rebooting...");
    g_recovery_active = false;
    recovery_clear_flag();
    esp_restart();
}
