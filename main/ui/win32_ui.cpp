/**
 * Win32 OS - Main UI Implementation
 * Windows Vista Style Interface with smooth animations
 */

#include "win32_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware/hardware.h"
#include "system_settings.h"
#include "recovery_trigger.h"
#include <time.h>
#include <string.h>

// Include converted assets
#include "assets.h"

// Custom font with Cyrillic support
#define UI_FONT &CodeProVariable

static const char *TAG = "WIN32_UI";

// Animation settings - optimized for smooth 60fps feel
#define ANIM_TIME_DEFAULT   150   // ms - smooth animations
#define ANIM_TIME_FAST      100   // ms - quick but smooth
#define ANIM_TIME_SLOW      200   // ms - deliberate animations

// UI Objects
static lv_obj_t *scr_boot = NULL;
lv_obj_t *scr_desktop = NULL;  // Exported for apps.cpp
static lv_obj_t *scr_lock = NULL;      // Lock screen
static lv_obj_t *scr_aod = NULL;       // Always On Display
static lv_obj_t *taskbar = NULL;
static lv_obj_t *start_button = NULL;
static lv_obj_t *start_menu = NULL;
static lv_obj_t *systray_time = NULL;
static lv_obj_t *systray_wifi = NULL;
static lv_obj_t *systray_battery = NULL;

// Lock screen elements
static lv_obj_t *lock_time_label = NULL;
static lv_obj_t *lock_date_label = NULL;
static lv_obj_t *lock_swipe_hint = NULL;
static lv_obj_t *lock_avatar_cont = NULL;
static lv_obj_t *lock_avatar_letter = NULL;
static lv_obj_t *lock_username_label = NULL;
static lv_obj_t *lock_wallpaper = NULL;  // Lock screen wallpaper (same as desktop)
static lv_obj_t *lock_overlay = NULL;    // Dark overlay for dimming effect
static lv_timer_t *lock_timer = NULL;

// Lock screen unlock UI elements (for different lock types)
static lv_obj_t *lock_slide_container = NULL;   // Slide to unlock container
static lv_obj_t *lock_pin_container = NULL;     // PIN keypad container
static lv_obj_t *lock_password_container = NULL; // Password input container
static lv_obj_t *lock_pin_dots[6] = {NULL};     // PIN dots (up to 6 digits)
static lv_obj_t *lock_pin_error_label = NULL;   // PIN error message
static lv_obj_t *lock_password_textarea = NULL; // Password textarea
static lv_obj_t *lock_password_keyboard = NULL; // Password keyboard
static lv_obj_t *lock_password_error_label = NULL; // Password error message
static char lock_pin_buffer[7] = {0};           // PIN input buffer
static int lock_pin_length = 0;                 // Current PIN length

// Start menu user profile elements
static lv_obj_t *start_menu_avatar = NULL;
static lv_obj_t *start_menu_avatar_letter = NULL;
static lv_obj_t *start_menu_username = NULL;

// AOD elements
static lv_obj_t *aod_time_label = NULL;

// Lock screen recovery trigger - tap top-left corner 3 times within 2 seconds
static uint8_t lock_recovery_tap_count = 0;
static uint64_t lock_recovery_first_tap_time = 0;
#define LOCK_RECOVERY_TAP_COUNT 3
#define LOCK_RECOVERY_TAP_TIMEOUT_MS 2000

// Screen state
typedef enum {
    SCREEN_STATE_AOD,
    SCREEN_STATE_LOCK,
    SCREEN_STATE_DESKTOP
} screen_state_t;
static screen_state_t current_screen_state = SCREEN_STATE_DESKTOP;

// app_window is defined in apps.cpp
extern lv_obj_t *app_window;

// State
static bool start_menu_visible = false;
static app_launch_cb_t app_launch_callback = NULL;

// Forward declarations
static void create_boot_screen(void);
static void create_desktop_screen(void);
static void create_lock_screen(void);
static void create_aod_screen(void);
static void create_taskbar(void);
static void create_start_menu(void);
static void create_desktop_icons(void);
static void create_systray(void);
static void start_button_event_cb(lv_event_t *e);
static void desktop_icon_event_cb(lv_event_t *e);
static void start_menu_item_event_cb(lv_event_t *e);
static void boot_animation_timer_cb(lv_timer_t *timer);
static void close_app_window(void);
static void create_debug_app(void);
static void update_lock_time(void);
static void update_aod_time(void);
static void show_lock_recovery_dialog(void);

// App definitions - English text for ASCII fonts
typedef struct {
    const char *name;
    const char *title;
    const lv_image_dsc_t *icon;
    int grid_x;
    int grid_y;
} app_def_t;

static const app_def_t desktop_apps[] = {
    {"my_computer", "My PC", &img_my_computer, 0, 0},
    {"recycle_bin", "Trash", &img_trashbinempty, 0, 1},
    {"calculator", "Calc", &img_calculator, 0, 2},
    {"camera", "Camera", &img_camera, 0, 3},
    {"weather", "Weather", &img_weather, 1, 0},
    {"clock", "Clock", &img_clock, 1, 1},
    {"settings", "Settings", &img_settings, 1, 2},
    {"notepad", "Notepad", &img_notepad, 1, 3},
    {"photos", "Photos", &img_photoview, 2, 0},
    {"flappy", "Flappy", &img_flappy, 2, 1},
    {"paint", "Paint", &img_paint, 2, 2},
    {"console", "Console", &img_con, 2, 3},
    {"voice_recorder", "Recorder", &img_microphone, 3, 0},
    {"system_monitor", "TaskMgr", &img_taskmgr, 3, 1},
    {"snake", "Snake", &img_snake, 3, 2},
    {"js_ide", "JS IDE", &img_vscode, 3, 3},
    // New games
    {"tetris", "Tetris", &img_tetris, 4, 0},
    {"game2048", "2048", &img_2048, 4, 1},
    {"minesweeper", "Mines", &img_minesweeper, 4, 2},
    {"tictactoe", "TicTac", &img_tictactoe, 4, 3},
    {"memory", "Memory", &img_memory, 5, 0},
};
#define NUM_DESKTOP_APPS (sizeof(desktop_apps) / sizeof(desktop_apps[0]))

void win32_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing Win32 UI");
    
    // Create screens
    create_boot_screen();
    create_desktop_screen();
    create_lock_screen();
    create_aod_screen();
    
    ESP_LOGI(TAG, "UI initialized");
}

void win32_set_app_launch_callback(app_launch_cb_t cb)
{
    app_launch_callback = cb;
}

// ============ BOOT SCREEN (Windows Vista Style Animation) ============

// Boot animation state
static int boot_frame = 0;
static lv_obj_t *boot_anim_img = NULL;
static lv_timer_t *boot_anim_timer = NULL;

static void create_boot_screen(void)
{
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, lv_color_black(), 0);
    
    // Animation image - centered, scaled 2.5x (240x320 -> 600x800)
    boot_anim_img = lv_image_create(scr_boot);
    lv_obj_align(boot_anim_img, LV_ALIGN_CENTER, 0, -60);
    lv_image_set_scale(boot_anim_img, 640);  // 256 = 1x, 640 = 2.5x
    // Will be set in show_boot_screen
    
    // Credits at bottom
    lv_obj_t *powered_by = lv_label_create(scr_boot);
    lv_label_set_text(powered_by, "Powered by ESP32");
    lv_obj_set_style_text_color(powered_by, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(powered_by, UI_FONT, 0);
    lv_obj_align(powered_by, LV_ALIGN_BOTTOM_MID, 0, -50);
    
    lv_obj_t *coded_by = lv_label_create(scr_boot);
    lv_label_set_text(coded_by, "Coded by ewinnery");
    lv_obj_set_style_text_color(coded_by, lv_color_hex(0x666688), 0);
    lv_obj_set_style_text_font(coded_by, UI_FONT, 0);
    lv_obj_align(coded_by, LV_ALIGN_BOTTOM_MID, 0, -25);
}

void win32_show_boot_screen(void)
{
    lv_screen_load(scr_boot);
    boot_frame = 0;
    
    // Set first frame
    if (boot_anim_img && STARTUP_FRAME_COUNT > 0) {
        lv_image_set_src(boot_anim_img, startup_frames[0]);
    }
    
    // Start frame animation timer (~30fps = 33ms per frame)
    boot_anim_timer = lv_timer_create(boot_animation_timer_cb, STARTUP_FRAME_DELAY_MS, NULL);
}

static void boot_animation_timer_cb(lv_timer_t *timer)
{
    boot_frame++;
    
    if (boot_frame < STARTUP_FRAME_COUNT) {
        // Show next frame
        if (boot_anim_img) {
            lv_image_set_src(boot_anim_img, startup_frames[boot_frame]);
        }
    } else {
        // Animation complete - add small delay then show lock screen
        lv_timer_delete(timer);
        boot_anim_timer = NULL;
        boot_frame = 0;
        
        // Small delay before lock screen
        lv_timer_create([](lv_timer_t *t) {
            lv_timer_delete(t);
            win32_show_lock();  // Show lock screen instead of desktop
        }, 500, NULL);
    }
}

void win32_hide_boot_screen(void)
{
    // Called when boot is complete
}

// ============ DESKTOP SCREEN ============

// Wallpaper state
static lv_obj_t *desktop_wallpaper = NULL;
static int current_wallpaper_index = 0;

static void create_desktop_screen(void)
{
    scr_desktop = lv_obj_create(NULL);
    lv_obj_remove_flag(scr_desktop, LV_OBJ_FLAG_SCROLLABLE);
    
    // Wallpaper - stretch to fill entire screen (480x800)
    desktop_wallpaper = lv_image_create(scr_desktop);
    lv_image_set_src(desktop_wallpaper, &img_win7);  // Default to Win7 wallpaper
    lv_obj_set_size(desktop_wallpaper, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_image_set_inner_align(desktop_wallpaper, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_align(desktop_wallpaper, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Create desktop icons
    create_desktop_icons();
    
    // Create taskbar (at bottom)
    create_taskbar();
    
    // Create start menu (hidden initially)
    create_start_menu();
}

// ============ DESKTOP ICONS WITH DRAG & DROP ============

// Drag state for desktop icons
typedef struct {
    const char *app_name;
    int original_grid_x;
    int original_grid_y;
    bool is_dragging;
    lv_point_t drag_start;
} icon_drag_state_t;

static icon_drag_state_t icon_drag_states[NUM_DESKTOP_APPS];
static lv_obj_t *desktop_icon_containers[NUM_DESKTOP_APPS] = {NULL};

// Calculate grid position from screen coordinates
static void screen_to_grid(int screen_x, int screen_y, int8_t *grid_x, int8_t *grid_y)
{
    uint8_t cols = settings_get_desktop_grid_cols();
    uint8_t rows = settings_get_desktop_grid_rows();
    
    *grid_x = (screen_x - DESKTOP_PADDING + ICON_SPACING / 2) / ICON_SPACING;
    *grid_y = (screen_y - DESKTOP_PADDING + ICON_SPACING / 2) / ICON_SPACING;
    
    // Clamp to grid bounds
    if (*grid_x < 0) *grid_x = 0;
    if (*grid_x >= cols) *grid_x = cols - 1;
    if (*grid_y < 0) *grid_y = 0;
    if (*grid_y >= rows) *grid_y = rows - 1;
}

// Check if grid position is occupied by another icon
static bool is_grid_position_occupied(int8_t grid_x, int8_t grid_y, int exclude_index)
{
    for (int i = 0; i < NUM_DESKTOP_APPS; i++) {
        if (i == exclude_index) continue;
        
        int8_t ix, iy;
        if (settings_get_icon_position(desktop_apps[i].name, &ix, &iy)) {
            if (ix == grid_x && iy == grid_y) return true;
        } else {
            // Use default position
            if (desktop_apps[i].grid_x == grid_x && desktop_apps[i].grid_y == grid_y) return true;
        }
    }
    return false;
}

// Double-click detection state
static uint32_t last_click_time[NUM_DESKTOP_APPS] = {0};
static const uint32_t DOUBLE_CLICK_TIME_MS = 400;  // Max time between clicks for double-click
static int selected_icon_index = -1;  // Currently selected icon (-1 = none)

// Clear selection from previously selected icon
static void clear_icon_selection(void)
{
    if (selected_icon_index >= 0 && selected_icon_index < NUM_DESKTOP_APPS) {
        if (desktop_icon_containers[selected_icon_index]) {
            lv_obj_set_style_bg_opa(desktop_icon_containers[selected_icon_index], LV_OPA_TRANSP, 0);
        }
    }
    selected_icon_index = -1;
}

// Desktop icon event handler with drag support and double-click
static void desktop_icon_drag_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *icon_cont = (lv_obj_t *)lv_event_get_target(e);
    int icon_index = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (icon_index < 0 || icon_index >= NUM_DESKTOP_APPS) return;
    
    icon_drag_state_t *state = &icon_drag_states[icon_index];
    
    if (code == LV_EVENT_LONG_PRESSED) {
        // Start drag
        state->is_dragging = true;
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_indev_get_point(indev, &state->drag_start);
        }
        
        // Visual feedback - highlight
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(0x3399FF), 0);
        lv_obj_set_style_bg_opa(icon_cont, LV_OPA_70, 0);
        lv_obj_move_foreground(icon_cont);
        
        ESP_LOGI(TAG, "Started dragging icon: %s", desktop_apps[icon_index].name);
    }
    else if (code == LV_EVENT_PRESSING && state->is_dragging) {
        // Update position while dragging
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            
            // Move icon to follow finger (centered on touch point)
            lv_obj_set_pos(icon_cont, point.x - 35, point.y - 35);
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (state->is_dragging) {
            state->is_dragging = false;
            
            // Get final position
            lv_coord_t final_x = lv_obj_get_x(icon_cont);
            lv_coord_t final_y = lv_obj_get_y(icon_cont);
            
            // Convert to grid position
            int8_t new_grid_x, new_grid_y;
            screen_to_grid(final_x + 35, final_y + 35, &new_grid_x, &new_grid_y);
            
            // Check if position is valid and not occupied
            if (!is_grid_position_occupied(new_grid_x, new_grid_y, icon_index)) {
                // Save new position
                settings_save_icon_position(desktop_apps[icon_index].name, new_grid_x, new_grid_y);
                ESP_LOGI(TAG, "Icon %s moved to grid (%d, %d)", desktop_apps[icon_index].name, new_grid_x, new_grid_y);
            } else {
                // Revert to original position
                int8_t orig_x, orig_y;
                if (!settings_get_icon_position(desktop_apps[icon_index].name, &orig_x, &orig_y)) {
                    orig_x = desktop_apps[icon_index].grid_x;
                    orig_y = desktop_apps[icon_index].grid_y;
                }
                new_grid_x = orig_x;
                new_grid_y = orig_y;
                ESP_LOGI(TAG, "Position occupied, reverting icon %s", desktop_apps[icon_index].name);
            }
            
            // Snap to grid
            int snap_x = DESKTOP_PADDING + new_grid_x * ICON_SPACING;
            int snap_y = DESKTOP_PADDING + new_grid_y * ICON_SPACING;
            lv_obj_set_pos(icon_cont, snap_x, snap_y);
            
            // Reset visual style
            lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
            
            // Reset click time to prevent accidental launch after drag
            last_click_time[icon_index] = 0;
        }
    }
    else if (code == LV_EVENT_CLICKED) {
        // Skip if was dragging
        if (state->is_dragging) {
            state->is_dragging = false;
            return;
        }
        
        // Double-click detection for app launch (like real Windows)
        uint32_t now = lv_tick_get();
        uint32_t last = last_click_time[icon_index];
        
        if (last > 0 && (now - last) < DOUBLE_CLICK_TIME_MS) {
            // Double-click detected - launch app
            const char *app_name = desktop_apps[icon_index].name;
            ESP_LOGI(TAG, "Desktop icon double-clicked: %s", app_name);
            
            // Reset click time and clear selection
            last_click_time[icon_index] = 0;
            clear_icon_selection();
            
            // Hide start menu if open
            if (start_menu_visible) {
                win32_hide_start_menu();
            }
            
            // Launch debug app
            if (strcmp(app_name, "debug") == 0) {
                create_debug_app();
                return;
            }
            
            if (app_launch_callback) {
                app_launch_callback(app_name);
            }
        } else {
            // First click - just select (visual feedback)
            last_click_time[icon_index] = now;
            
            // Clear previous selection
            clear_icon_selection();
            
            // Set new selection highlight
            lv_obj_set_style_bg_color(icon_cont, lv_color_hex(0x3399FF), 0);
            lv_obj_set_style_bg_opa(icon_cont, LV_OPA_40, 0);
            selected_icon_index = icon_index;
        }
    }
}

static void create_desktop_icons(void)
{
    for (int i = 0; i < NUM_DESKTOP_APPS; i++) {
        const app_def_t *app = &desktop_apps[i];
        
        // Initialize drag state
        icon_drag_states[i].app_name = app->name;
        icon_drag_states[i].original_grid_x = app->grid_x;
        icon_drag_states[i].original_grid_y = app->grid_y;
        icon_drag_states[i].is_dragging = false;
        
        // Get saved position or use default
        int8_t grid_x, grid_y;
        if (!settings_get_icon_position(app->name, &grid_x, &grid_y)) {
            grid_x = app->grid_x;
            grid_y = app->grid_y;
        }
        
        // Calculate position
        int x = DESKTOP_PADDING + grid_x * ICON_SPACING;
        int y = DESKTOP_PADDING + grid_y * ICON_SPACING;
        
        // Icon container
        lv_obj_t *icon_cont = lv_obj_create(scr_desktop);
        lv_obj_set_size(icon_cont, 70, 70);
        lv_obj_set_pos(icon_cont, x, y);
        lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(icon_cont, 0, 0);
        lv_obj_set_style_pad_all(icon_cont, 0, 0);
        lv_obj_remove_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(icon_cont, LV_OBJ_FLAG_CLICKABLE);
        
        // Hover/press effect
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(0x3399FF), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(icon_cont, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_set_style_radius(icon_cont, 5, 0);
        
        // Icon image
        lv_obj_t *icon_img = lv_image_create(icon_cont);
        lv_image_set_src(icon_img, app->icon);
        lv_obj_align(icon_img, LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_remove_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
        
        // Icon label
        lv_obj_t *icon_label = lv_label_create(icon_cont);
        lv_label_set_text(icon_label, app->title);
        lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(icon_label, UI_FONT, 0);
        lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(icon_label, 68);
        lv_obj_align(icon_label, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_label_set_long_mode(icon_label, LV_LABEL_LONG_DOT);
        lv_obj_remove_flag(icon_label, LV_OBJ_FLAG_CLICKABLE);
        
        // Event handlers for drag & drop
        lv_obj_add_event_cb(icon_cont, desktop_icon_drag_event_cb, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
        lv_obj_add_event_cb(icon_cont, desktop_icon_drag_event_cb, LV_EVENT_PRESSING, (void*)(intptr_t)i);
        lv_obj_add_event_cb(icon_cont, desktop_icon_drag_event_cb, LV_EVENT_RELEASED, (void*)(intptr_t)i);
        lv_obj_add_event_cb(icon_cont, desktop_icon_drag_event_cb, LV_EVENT_PRESS_LOST, (void*)(intptr_t)i);
        lv_obj_add_event_cb(icon_cont, desktop_icon_drag_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        
        desktop_icon_containers[i] = icon_cont;
    }
}

static void desktop_icon_event_cb(lv_event_t *e)
{
    const char *app_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Desktop icon clicked: %s", app_name);
    
    // Hide start menu if open
    if (start_menu_visible) {
        win32_hide_start_menu();
    }
    
    // Launch debug app
    if (strcmp(app_name, "debug") == 0) {
        create_debug_app();
        return;
    }
    
    if (app_launch_callback) {
        app_launch_callback(app_name);
    }
}

// ============ TASKBAR ============

// Taskbar pinned app icons
static lv_obj_t *pinned_app_icons[3] = {NULL, NULL, NULL};

static void pinned_app_clicked(lv_event_t *e)
{
    const char *app_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Pinned app clicked: %s", app_name);
    
    if (start_menu_visible) {
        win32_hide_start_menu();
    }
    
    if (app_launch_callback) {
        app_launch_callback(app_name);
    }
}

static void create_taskbar(void)
{
    // Get current UI style
    ui_style_t style = settings_get_ui_style();
    
    // Taskbar container
    taskbar = lv_obj_create(scr_desktop);
    lv_obj_set_size(taskbar, SCREEN_WIDTH, TASKBAR_HEIGHT);
    lv_obj_align(taskbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(taskbar, lv_color_hex(COLOR_TASKBAR_BG), 0);
    lv_obj_set_style_bg_opa(taskbar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(taskbar, 0, 0);
    lv_obj_set_style_radius(taskbar, 0, 0);
    lv_obj_set_style_pad_all(taskbar, 0, 0);
    lv_obj_remove_flag(taskbar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Style-specific taskbar appearance
    if (style == UI_STYLE_WINXP) {
        // XP style - blue gradient taskbar
        lv_obj_set_style_bg_color(taskbar, lv_color_hex(0x0A246A), 0);
        lv_obj_set_style_bg_grad_color(taskbar, lv_color_hex(0x3A6EA5), 0);
        lv_obj_set_style_bg_grad_dir(taskbar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(taskbar, LV_OPA_COVER, 0);
    } else if (style == UI_STYLE_WIN11) {
        // Win11 style - semi-transparent with blur effect
        lv_obj_set_style_bg_color(taskbar, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(taskbar, LV_OPA_80, 0);
    }
    
    // Glass effect line at top (Win7 only)
    if (style == UI_STYLE_WIN7) {
        lv_obj_t *glass_line = lv_obj_create(taskbar);
        lv_obj_set_size(glass_line, SCREEN_WIDTH, 2);
        lv_obj_align(glass_line, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(glass_line, lv_color_hex(0x4A7DC4), 0);
        lv_obj_set_style_border_width(glass_line, 0, 0);
        lv_obj_set_style_radius(glass_line, 0, 0);
    }
    
    // Start button position depends on style
    int start_btn_x = 0;
    lv_align_t start_align = LV_ALIGN_CENTER;
    
    if (style == UI_STYLE_WINXP) {
        // XP: Start button in bottom-left corner
        start_align = LV_ALIGN_LEFT_MID;
        start_btn_x = 5;
    }
    // Win7 and Win11: centered (default)
    
    // Start button hitbox
    lv_obj_t *start_hitbox = lv_obj_create(taskbar);
    lv_obj_set_size(start_hitbox, 100, TASKBAR_HEIGHT);
    lv_obj_align(start_hitbox, start_align, start_btn_x, 0);
    lv_obj_set_style_bg_opa(start_hitbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(start_hitbox, 0, 0);
    lv_obj_add_flag(start_hitbox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(start_hitbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(start_hitbox, start_button_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Start button image - style-specific with scaling
    start_button = lv_image_create(start_hitbox);
    if (style == UI_STYLE_WINXP) {
        lv_image_set_src(start_button, &img_start_buttonxp);
        lv_image_set_scale(start_button, 640);  // Scale 2.5x (256 = 1x) - smaller
    } else if (style == UI_STYLE_WIN11) {
        lv_image_set_src(start_button, &img_start_button11);
        lv_image_set_scale(start_button, 192);  // Scale 0.75x (256 = 1x, 192 = 0.75x)
    } else {
        lv_image_set_src(start_button, &img_start_button);  // Win7 default
    }
    lv_obj_center(start_button);
    if (style == UI_STYLE_WINXP) {
        lv_obj_align(start_button, LV_ALIGN_LEFT_MID, 2, 0);  // Shift XP button slightly
    }
    lv_obj_remove_flag(start_button, LV_OBJ_FLAG_CLICKABLE);
    
    // Create pinned app icons (left of start button for Win7/Win11, right for XP)
    // For Win7/Win11: Quick Launch icons appear LEFT of the centered Start button
    // For XP: icons appear RIGHT of the Start button (which is on the left)
    int pinned_start_x;
    if (style == UI_STYLE_WINXP) {
        pinned_start_x = 110;  // Right of XP Start button
    } else {
        // Win7/Win11: Start button is centered, so place icons to the LEFT of center
        // Start button hitbox is 100px wide, centered. Icons go to the left of it.
        pinned_start_x = (SCREEN_WIDTH / 2) - 50 - 145;  // Left of start button (3 icons * 45px + gap)
    }
    
    for (int i = 0; i < 3; i++) {
        const char *app_name = settings_get_pinned_app(i);
        if (app_name && strlen(app_name) > 0) {
            // Find app icon
            const lv_image_dsc_t *icon = NULL;
            for (int j = 0; j < NUM_DESKTOP_APPS; j++) {
                if (strcmp(desktop_apps[j].name, app_name) == 0) {
                    icon = desktop_apps[j].icon;
                    break;
                }
            }
            
            if (icon) {
                lv_obj_t *pinned_btn = lv_obj_create(taskbar);
                lv_obj_set_size(pinned_btn, 40, TASKBAR_HEIGHT - 8);
                lv_obj_align(pinned_btn, LV_ALIGN_LEFT_MID, pinned_start_x + i * 45, 0);
                lv_obj_set_style_bg_opa(pinned_btn, LV_OPA_TRANSP, 0);
                lv_obj_set_style_bg_color(pinned_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
                lv_obj_set_style_bg_opa(pinned_btn, LV_OPA_50, LV_STATE_PRESSED);
                lv_obj_set_style_border_width(pinned_btn, 0, 0);
                lv_obj_set_style_radius(pinned_btn, 4, 0);
                lv_obj_add_flag(pinned_btn, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(pinned_btn, LV_OBJ_FLAG_SCROLLABLE);
                
                lv_obj_t *pinned_icon = lv_image_create(pinned_btn);
                lv_image_set_src(pinned_icon, icon);
                lv_image_set_scale(pinned_icon, 170);  // Scale to ~32px from 48px
                lv_obj_center(pinned_icon);
                lv_obj_remove_flag(pinned_icon, LV_OBJ_FLAG_CLICKABLE);
                
                lv_obj_add_event_cb(pinned_btn, pinned_app_clicked, LV_EVENT_CLICKED, (void*)app_name);
                pinned_app_icons[i] = pinned_btn;
            }
        }
    }
    
    // Create system tray on the right
    create_systray();
}

static void start_button_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Start button clicked");
    win32_toggle_start_menu();
}

// ============ SYSTEM TRAY ============

static void create_systray(void)
{
    // Lock button (left of systray)
    lv_obj_t *lock_btn = lv_obj_create(taskbar);
    lv_obj_set_size(lock_btn, 40, TASKBAR_HEIGHT - 8);
    lv_obj_align(lock_btn, LV_ALIGN_RIGHT_MID, -140, 0);  // Left of systray
    lv_obj_set_style_bg_color(lock_btn, lv_color_hex(COLOR_SYSTRAY_BG), 0);
    lv_obj_set_style_bg_opa(lock_btn, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(lock_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(lock_btn, 0, 0);
    lv_obj_set_style_radius(lock_btn, 4, 0);
    lv_obj_add_flag(lock_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(lock_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(lock_btn, [](lv_event_t *e) {
        ESP_LOGI(TAG, "Lock button pressed");
        win32_power_button_pressed();
    }, LV_EVENT_CLICKED, NULL);
    
    // Lock icon (using power symbol)
    lv_obj_t *lock_icon = lv_label_create(lock_btn);
    lv_label_set_text(lock_icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(lock_icon, lv_color_white(), 0);
    lv_obj_center(lock_icon);
    
    // System tray container (right side of taskbar)
    lv_obj_t *systray = lv_obj_create(taskbar);
    lv_obj_set_size(systray, 130, TASKBAR_HEIGHT - 8);  // Wider to fit icons + time
    lv_obj_align(systray, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(systray, lv_color_hex(COLOR_SYSTRAY_BG), 0);
    lv_obj_set_style_bg_opa(systray, LV_OPA_70, 0);
    lv_obj_set_style_border_width(systray, 0, 0);
    lv_obj_set_style_radius(systray, 4, 0);
    lv_obj_set_style_pad_all(systray, 4, 0);
    lv_obj_set_style_pad_column(systray, 8, 0);  // Add spacing between items
    lv_obj_remove_flag(systray, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(systray, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(systray, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Make systray clickable
    lv_obj_add_flag(systray, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(systray, [](lv_event_t *e) {
        ESP_LOGI(TAG, "System tray clicked");
        system_tray_toggle();
    }, LV_EVENT_CLICKED, NULL);
    
    // WiFi icon
    systray_wifi = lv_image_create(systray);
    lv_image_set_src(systray_wifi, &img_wifi);
    lv_obj_remove_flag(systray_wifi, LV_OBJ_FLAG_CLICKABLE);
    
    // Battery (programmatic)
    systray_battery = lv_obj_create(systray);
    lv_obj_set_size(systray_battery, 22, 12);
    lv_obj_set_style_bg_color(systray_battery, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_border_color(systray_battery, lv_color_white(), 0);
    lv_obj_set_style_border_width(systray_battery, 1, 0);
    lv_obj_set_style_radius(systray_battery, 2, 0);
    lv_obj_set_style_pad_all(systray_battery, 0, 0);
    lv_obj_remove_flag(systray_battery, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(systray_battery, LV_OBJ_FLAG_CLICKABLE);
    
    // Time label
    systray_time = lv_label_create(systray);
    lv_label_set_text(systray_time, "12:00");
    lv_obj_set_style_text_color(systray_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(systray_time, UI_FONT, 0);
    lv_obj_remove_flag(systray_time, LV_OBJ_FLAG_CLICKABLE);
}

void win32_update_time(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    if (systray_time) {
        lv_label_set_text(systray_time, time_str);
    }
}

void win32_update_wifi(bool connected)
{
    if (systray_wifi) {
        lv_obj_set_style_image_recolor(systray_wifi, 
            connected ? lv_color_white() : lv_color_hex(0x666666), 0);
        lv_obj_set_style_image_recolor_opa(systray_wifi, 
            connected ? LV_OPA_TRANSP : LV_OPA_70, 0);
    }
}

void win32_update_battery(uint8_t level, bool charging)
{
    if (systray_battery) {
        lv_color_t color;
        if (level > 50) {
            color = lv_color_hex(0x00AA00);
        } else if (level > 20) {
            color = lv_color_hex(0xFFAA00);
        } else {
            color = lv_color_hex(0xFF0000);
        }
        
        if (charging) {
            color = lv_color_hex(0x00AAFF);
        }
        
        lv_obj_set_style_bg_color(systray_battery, color, 0);
    }
}

// Power menu popup
static lv_obj_t *power_menu_popup = NULL;

static void power_menu_item_cb(lv_event_t *e)
{
    const char *action = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Power action: %s", action);
    
    // Hide power menu
    if (power_menu_popup) {
        lv_obj_add_flag(power_menu_popup, LV_OBJ_FLAG_HIDDEN);
    }
    win32_hide_start_menu();
    
    if (strcmp(action, "sleep") == 0) {
        // Sleep mode - show AOD
        ESP_LOGI(TAG, "Entering sleep mode (AOD)...");
        win32_show_aod();
    } else if (strcmp(action, "lock") == 0) {
        // Lock screen
        ESP_LOGI(TAG, "Locking device...");
        win32_show_lock();
    } else if (strcmp(action, "restart") == 0) {
        ESP_LOGI(TAG, "Restarting...");
        esp_restart();
    } else if (strcmp(action, "shutdown") == 0) {
        // Shutdown - turn off backlight and enter deep sleep
        ESP_LOGI(TAG, "Shutting down...");
        hw_backlight_set(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_deep_sleep_start();
    } else if (strcmp(action, "recovery") == 0) {
        // Reboot to recovery mode
        ESP_LOGW(TAG, "Rebooting to Recovery Mode...");
        recovery_request_reboot();
    }
}

static void show_power_menu(lv_event_t *e)
{
    if (!power_menu_popup) {
        // Create power menu popup
        power_menu_popup = lv_obj_create(scr_desktop);
        lv_obj_set_size(power_menu_popup, 140, 110);
        lv_obj_set_style_bg_color(power_menu_popup, lv_color_hex(0xF5F5F5), 0);
        lv_obj_set_style_border_color(power_menu_popup, lv_color_hex(0x888888), 0);
        lv_obj_set_style_border_width(power_menu_popup, 1, 0);
        lv_obj_set_style_radius(power_menu_popup, 4, 0);
        lv_obj_set_style_shadow_width(power_menu_popup, 8, 0);
        lv_obj_set_style_shadow_color(power_menu_popup, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(power_menu_popup, LV_OPA_30, 0);
        lv_obj_set_style_pad_all(power_menu_popup, 4, 0);
        lv_obj_remove_flag(power_menu_popup, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(power_menu_popup, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(power_menu_popup, 2, 0);
        
        // Menu items
        static const char* items[] = {"Sleep", "Restart", "Shut down"};
        static const char* actions[] = {"sleep", "restart", "shutdown"};
        
        for (int i = 0; i < 3; i++) {
            lv_obj_t *item = lv_obj_create(power_menu_popup);
            lv_obj_set_size(item, lv_pct(100), 30);
            lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x3399FF), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(item, LV_OPA_50, LV_STATE_PRESSED);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_radius(item, 3, 0);
            lv_obj_set_style_pad_left(item, 8, 0);
            lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *label = lv_label_create(item);
            lv_label_set_text(label, items[i]);
            lv_obj_set_style_text_color(label, lv_color_black(), 0);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
            
            lv_obj_add_event_cb(item, power_menu_item_cb, LV_EVENT_CLICKED, (void*)actions[i]);
        }
    }
    
    // Position near shutdown button (adjusted for new menu height 520)
    lv_obj_set_pos(power_menu_popup, (SCREEN_WIDTH - 140) / 2 + 80, SCREEN_HEIGHT - TASKBAR_HEIGHT - 520 + 460);
    lv_obj_remove_flag(power_menu_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(power_menu_popup);
}

// ============ START MENU ============

// Forward declarations for style-specific menu creators
static void create_start_menu_win7(void);
static void create_start_menu_winxp(void);
static void create_start_menu_win11(void);

static void create_start_menu(void)
{
    ui_style_t style = settings_get_ui_style();
    
    switch (style) {
        case UI_STYLE_WINXP:
            create_start_menu_winxp();
            break;
        case UI_STYLE_WIN11:
            create_start_menu_win11();
            break;
        case UI_STYLE_WIN7:
        default:
            create_start_menu_win7();
            break;
    }
}

// Windows 7 style Start menu (default)
static void create_start_menu_win7(void)
{
    // Windows 7 style Start menu - two columns with search bar
    start_menu = lv_obj_create(scr_desktop);
    lv_obj_set_size(start_menu, 380, 520);
    lv_obj_set_pos(start_menu, (SCREEN_WIDTH - 380) / 2, SCREEN_HEIGHT);  // Start off-screen
    lv_obj_set_style_bg_color(start_menu, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(start_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(start_menu, lv_color_hex(0x7EB4EA), 0);
    lv_obj_set_style_border_width(start_menu, 2, 0);
    lv_obj_set_style_radius(start_menu, 6, 0);
    lv_obj_set_style_pad_all(start_menu, 0, 0);
    lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_SCROLLABLE);
    
    // Windows 7 style header with blue gradient and larger avatar
    lv_obj_t *header = lv_obj_create(start_menu);
    lv_obj_set_size(header, lv_pct(100), 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x4A90D9), 0);  // Blue gradient top
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x2A70B9), 0);  // Blue gradient bottom
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 4, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Get user profile from settings
    const char *username = settings_get_username();
    uint32_t avatar_color = settings_get_avatar_color();
    
    // Larger user avatar (Windows 7 style)
    start_menu_avatar = lv_obj_create(header);
    lv_obj_set_size(start_menu_avatar, 44, 44);
    lv_obj_align(start_menu_avatar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(start_menu_avatar, lv_color_hex(avatar_color), 0);
    lv_obj_set_style_border_width(start_menu_avatar, 2, 0);
    lv_obj_set_style_border_color(start_menu_avatar, lv_color_white(), 0);
    lv_obj_set_style_radius(start_menu_avatar, 4, 0);
    lv_obj_set_style_shadow_width(start_menu_avatar, 4, 0);
    lv_obj_set_style_shadow_color(start_menu_avatar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(start_menu_avatar, LV_OPA_30, 0);
    lv_obj_remove_flag(start_menu_avatar, LV_OBJ_FLAG_SCROLLABLE);
    
    start_menu_avatar_letter = lv_label_create(start_menu_avatar);
    char avatar_letter[2] = {username[0], '\0'};
    if (avatar_letter[0] >= 'a' && avatar_letter[0] <= 'z') avatar_letter[0] -= 32;
    lv_label_set_text(start_menu_avatar_letter, avatar_letter);
    lv_obj_set_style_text_color(start_menu_avatar_letter, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_menu_avatar_letter, UI_FONT, 0);
    lv_obj_center(start_menu_avatar_letter);
    
    start_menu_username = lv_label_create(header);
    lv_label_set_text(start_menu_username, username);
    lv_obj_set_style_text_color(start_menu_username, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_menu_username, UI_FONT, 0);
    lv_obj_align(start_menu_username, LV_ALIGN_LEFT_MID, 55, 0);
    
    // Main content area - two columns
    lv_obj_t *main_area = lv_obj_create(start_menu);
    lv_obj_set_size(main_area, lv_pct(100), 350);
    lv_obj_align(main_area, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_remove_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left column - Programs (white background, scrollable)
    lv_obj_t *left_col = lv_obj_create(main_area);
    lv_obj_set_size(left_col, 190, lv_pct(100));
    lv_obj_align(left_col, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(left_col, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(left_col, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_col, 0, 0);
    lv_obj_set_style_border_side(left_col, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(left_col, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_radius(left_col, 0, 0);
    lv_obj_set_style_pad_all(left_col, 4, 0);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left_col, 2, 0);
    lv_obj_add_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(left_col, LV_SCROLLBAR_MODE_AUTO);
    
    // Right column - System items (Windows 7 blue gradient)
    lv_obj_t *right_col = lv_obj_create(main_area);
    lv_obj_set_size(right_col, 190, lv_pct(100));
    lv_obj_align(right_col, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(right_col, lv_color_hex(0xD4E4F7), 0);  // Light blue top
    lv_obj_set_style_bg_grad_color(right_col, lv_color_hex(0xE8F0F8), 0);  // Lighter blue bottom
    lv_obj_set_style_bg_grad_dir(right_col, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_radius(right_col, 0, 0);
    lv_obj_set_style_pad_all(right_col, 6, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right_col, 2, 0);
    lv_obj_remove_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add apps to left column
    for (int i = 0; i < NUM_DESKTOP_APPS; i++) {
        const app_def_t *app = &desktop_apps[i];
        
        lv_obj_t *item = lv_obj_create(left_col);
        lv_obj_set_size(item, lv_pct(100), 32);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD4E4F7), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 3, 0);
        lv_obj_set_style_pad_left(item, 6, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Icon (scaled to ~24px from 48px)
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, app->icon);
        lv_image_set_scale(icon, 128);  // 50% scale
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        // Label
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, app->title);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_set_style_text_font(label, UI_FONT, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_add_event_cb(item, start_menu_item_event_cb, LV_EVENT_CLICKED, (void*)app->name);
    }
    
    // Right column items - Windows 7 system shortcuts (7 items)
    static const char* right_items[] = {"Documents", "Pictures", "Games", "Computer", "Settings", "Programs", "Help"};
    static const char* right_names[] = {"folder_documents", "folder_pictures", "folder_games", "my_computer", "settings", "default_programs", "help"};
    static const lv_image_dsc_t* right_icons[] = {&img_folder, &img_photoview, &img_folder, &img_my_computer, &img_settings, &img_settings, &img_information};
    
    for (int i = 0; i < 7; i++) {
        lv_obj_t *item = lv_obj_create(right_col);
        lv_obj_set_size(item, lv_pct(100), 34);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xB8D4F0), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 3, 0);
        lv_obj_set_style_pad_left(item, 6, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        // Icon (scaled to ~24px from 48px)
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, right_icons[i]);
        lv_image_set_scale(icon, 128);  // 50% scale
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, right_items[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(0x1A3A5C), 0);  // Dark blue text
        lv_obj_set_style_text_font(label, UI_FONT, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_add_event_cb(item, start_menu_item_event_cb, LV_EVENT_CLICKED, (void*)right_names[i]);
    }
    
    // Bottom bar with power buttons (Windows 7 style) - NO search bar
    lv_obj_t *bottom_bar = lv_obj_create(start_menu);
    lv_obj_set_size(bottom_bar, lv_pct(100), 50);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0xD4E4F7), 0);
    lv_obj_set_style_bg_grad_color(bottom_bar, lv_color_hex(0xE8F0F8), 0);
    lv_obj_set_style_bg_grad_dir(bottom_bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bottom_bar, 1, 0);
    lv_obj_set_style_border_color(bottom_bar, lv_color_hex(0xB8D4F0), 0);
    lv_obj_set_style_border_side(bottom_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 6, 0);
    lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Sleep button (blue)
    lv_obj_t *sleep_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(sleep_btn, 80, 36);
    lv_obj_set_style_bg_color(sleep_btn, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_bg_grad_color(sleep_btn, lv_color_hex(0x2A70B9), 0);
    lv_obj_set_style_bg_grad_dir(sleep_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(sleep_btn, 4, 0);
    lv_obj_set_style_border_width(sleep_btn, 1, 0);
    lv_obj_set_style_border_color(sleep_btn, lv_color_hex(0x1A5090), 0);
    lv_obj_add_event_cb(sleep_btn, power_menu_item_cb, LV_EVENT_CLICKED, (void*)"sleep");
    
    lv_obj_t *sleep_label = lv_label_create(sleep_btn);
    lv_label_set_text(sleep_label, "Sleep");
    lv_obj_set_style_text_color(sleep_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(sleep_label, UI_FONT, 0);
    lv_obj_center(sleep_label);
    
    // Lock button (yellow/orange)
    lv_obj_t *lock_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(lock_btn, 80, 36);
    lv_obj_set_style_bg_color(lock_btn, lv_color_hex(0xF0A030), 0);
    lv_obj_set_style_bg_grad_color(lock_btn, lv_color_hex(0xD08020), 0);
    lv_obj_set_style_bg_grad_dir(lock_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(lock_btn, 4, 0);
    lv_obj_set_style_border_width(lock_btn, 1, 0);
    lv_obj_set_style_border_color(lock_btn, lv_color_hex(0xA06010), 0);
    lv_obj_add_event_cb(lock_btn, power_menu_item_cb, LV_EVENT_CLICKED, (void*)"lock");
    
    lv_obj_t *lock_label = lv_label_create(lock_btn);
    lv_label_set_text(lock_label, "Lock");
    lv_obj_set_style_text_color(lock_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(lock_label, UI_FONT, 0);
    lv_obj_center(lock_label);
    
    // Shutdown button (red)
    lv_obj_t *shutdown_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(shutdown_btn, 100, 36);
    lv_obj_set_style_bg_color(shutdown_btn, lv_color_hex(0xE85D04), 0);
    lv_obj_set_style_bg_grad_color(shutdown_btn, lv_color_hex(0xC84A00), 0);
    lv_obj_set_style_bg_grad_dir(shutdown_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(shutdown_btn, 4, 0);
    lv_obj_set_style_border_width(shutdown_btn, 1, 0);
    lv_obj_set_style_border_color(shutdown_btn, lv_color_hex(0xA03800), 0);
    lv_obj_add_event_cb(shutdown_btn, power_menu_item_cb, LV_EVENT_CLICKED, (void*)"shutdown");
    
    lv_obj_t *shutdown_label = lv_label_create(shutdown_btn);
    lv_label_set_text(shutdown_label, "Shutdown");
    lv_obj_set_style_text_color(shutdown_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(shutdown_label, UI_FONT, 0);
    lv_obj_center(shutdown_label);
}

// Windows XP style Start menu
static void create_start_menu_winxp(void)
{
    // XP style - classic two-column menu with green header
    start_menu = lv_obj_create(scr_desktop);
    lv_obj_set_size(start_menu, 380, 520);
    lv_obj_set_pos(start_menu, 5, SCREEN_HEIGHT);  // Start off-screen, left-aligned
    lv_obj_set_style_bg_color(start_menu, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(start_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(start_menu, lv_color_hex(0x0A246A), 0);
    lv_obj_set_style_border_width(start_menu, 3, 0);
    lv_obj_set_style_radius(start_menu, 0, 0);  // XP has sharp corners
    lv_obj_set_style_pad_all(start_menu, 0, 0);
    lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_SCROLLABLE);
    
    // XP style header - blue gradient with user info
    lv_obj_t *header = lv_obj_create(start_menu);
    lv_obj_set_size(header, lv_pct(100), 55);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0A246A), 0);
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x3A6EA5), 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 8, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    const char *username = settings_get_username();
    uint32_t avatar_color = settings_get_avatar_color();
    
    // User avatar (XP style - square)
    start_menu_avatar = lv_obj_create(header);
    lv_obj_set_size(start_menu_avatar, 40, 40);
    lv_obj_align(start_menu_avatar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(start_menu_avatar, lv_color_hex(avatar_color), 0);
    lv_obj_set_style_border_width(start_menu_avatar, 2, 0);
    lv_obj_set_style_border_color(start_menu_avatar, lv_color_white(), 0);
    lv_obj_set_style_radius(start_menu_avatar, 0, 0);  // Square for XP
    lv_obj_remove_flag(start_menu_avatar, LV_OBJ_FLAG_SCROLLABLE);
    
    start_menu_avatar_letter = lv_label_create(start_menu_avatar);
    char avatar_letter[2] = {username[0], '\0'};
    if (avatar_letter[0] >= 'a' && avatar_letter[0] <= 'z') avatar_letter[0] -= 32;
    lv_label_set_text(start_menu_avatar_letter, avatar_letter);
    lv_obj_set_style_text_color(start_menu_avatar_letter, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_menu_avatar_letter, UI_FONT, 0);
    lv_obj_center(start_menu_avatar_letter);
    
    start_menu_username = lv_label_create(header);
    lv_label_set_text(start_menu_username, username);
    lv_obj_set_style_text_color(start_menu_username, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_menu_username, UI_FONT, 0);
    lv_obj_align(start_menu_username, LV_ALIGN_LEFT_MID, 50, 0);
    
    // Main content area - two columns
    lv_obj_t *main_area = lv_obj_create(start_menu);
    lv_obj_set_size(main_area, lv_pct(100), 360);
    lv_obj_align(main_area, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);
    lv_obj_remove_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left column - Programs (white)
    lv_obj_t *left_col = lv_obj_create(main_area);
    lv_obj_set_size(left_col, 190, lv_pct(100));
    lv_obj_align(left_col, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(left_col, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(left_col, 1, 0);
    lv_obj_set_style_border_color(left_col, lv_color_hex(0x0A246A), 0);
    lv_obj_set_style_border_side(left_col, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(left_col, 0, 0);
    lv_obj_set_style_pad_all(left_col, 4, 0);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left_col, 2, 0);
    lv_obj_add_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(left_col, LV_SCROLLBAR_MODE_AUTO);
    
    // Right column - XP blue gradient
    lv_obj_t *right_col = lv_obj_create(main_area);
    lv_obj_set_size(right_col, 190, lv_pct(100));
    lv_obj_align(right_col, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(right_col, lv_color_hex(0xD3E5FA), 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_radius(right_col, 0, 0);
    lv_obj_set_style_pad_all(right_col, 6, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right_col, 2, 0);
    lv_obj_remove_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add apps to left column
    for (int i = 0; i < NUM_DESKTOP_APPS; i++) {
        const app_def_t *app = &desktop_apps[i];
        
        lv_obj_t *item = lv_obj_create(left_col);
        lv_obj_set_size(item, lv_pct(100), 32);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x316AC5), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_pad_left(item, 6, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, app->icon);
        lv_image_set_scale(icon, 128);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, app->title);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_PRESSED);
        lv_obj_set_style_text_font(label, UI_FONT, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_add_event_cb(item, start_menu_item_event_cb, LV_EVENT_CLICKED, (void*)app->name);
    }
    
    // Right column items
    static const char* right_items[] = {"My Computer", "Documents", "Pictures", "Settings", "Help"};
    static const char* right_names[] = {"my_computer", "folder_documents", "folder_pictures", "settings", "help"};
    static const lv_image_dsc_t* right_icons[] = {&img_my_computer, &img_folder, &img_photoview, &img_settings, &img_information};
    
    for (int i = 0; i < 5; i++) {
        lv_obj_t *item = lv_obj_create(right_col);
        lv_obj_set_size(item, lv_pct(100), 34);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x316AC5), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_pad_left(item, 6, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, right_icons[i]);
        lv_image_set_scale(icon, 128);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, right_items[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(0x0A246A), 0);
        lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_PRESSED);
        lv_obj_set_style_text_font(label, UI_FONT, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_add_event_cb(item, start_menu_item_event_cb, LV_EVENT_CLICKED, (void*)right_names[i]);
    }
    
    // Bottom bar - XP orange gradient
    lv_obj_t *bottom_bar = lv_obj_create(start_menu);
    lv_obj_set_size(bottom_bar, lv_pct(100), 50);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x0A246A), 0);
    lv_obj_set_style_bg_grad_color(bottom_bar, lv_color_hex(0x3A6EA5), 0);
    lv_obj_set_style_bg_grad_dir(bottom_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 6, 0);
    lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Log Off button
    lv_obj_t *logoff_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(logoff_btn, 90, 36);
    lv_obj_set_style_bg_color(logoff_btn, lv_color_hex(0xD4A017), 0);
    lv_obj_set_style_radius(logoff_btn, 0, 0);
    lv_obj_add_event_cb(logoff_btn, power_menu_item_cb, LV_EVENT_CLICKED, (void*)"lock");
    
    lv_obj_t *logoff_label = lv_label_create(logoff_btn);
    lv_label_set_text(logoff_label, "Log Off");
    lv_obj_set_style_text_color(logoff_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(logoff_label, UI_FONT, 0);
    lv_obj_center(logoff_label);
    
    // Shutdown button
    lv_obj_t *shutdown_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(shutdown_btn, 100, 36);
    lv_obj_set_style_bg_color(shutdown_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(shutdown_btn, 0, 0);
    lv_obj_add_event_cb(shutdown_btn, power_menu_item_cb, LV_EVENT_CLICKED, (void*)"shutdown");
    
    lv_obj_t *shutdown_label = lv_label_create(shutdown_btn);
    lv_label_set_text(shutdown_label, "Shut Down");
    lv_obj_set_style_text_color(shutdown_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(shutdown_label, UI_FONT, 0);
    lv_obj_center(shutdown_label);
}

// Windows 11 style Start menu
static void create_start_menu_win11(void)
{
    // Win11 style - centered, rounded, semi-transparent with grid of apps
    start_menu = lv_obj_create(scr_desktop);
    lv_obj_set_size(start_menu, 420, 540);
    lv_obj_set_pos(start_menu, (SCREEN_WIDTH - 420) / 2, SCREEN_HEIGHT);  // Centered
    lv_obj_set_style_bg_color(start_menu, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(start_menu, LV_OPA_80, 0);  // Fluent transparency
    lv_obj_set_style_border_color(start_menu, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_width(start_menu, 1, 0);
    lv_obj_set_style_radius(start_menu, 12, 0);  // Rounded corners
    lv_obj_set_style_pad_all(start_menu, 15, 0);
    lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_SCROLLABLE);
    
    // Search bar at top
    lv_obj_t *search_bar = lv_obj_create(start_menu);
    lv_obj_set_size(search_bar, lv_pct(100), 40);
    lv_obj_align(search_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(search_bar, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_color(search_bar, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(search_bar, 1, 0);
    lv_obj_set_style_radius(search_bar, 6, 0);
    lv_obj_set_style_pad_left(search_bar, 15, 0);
    lv_obj_remove_flag(search_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *search_icon = lv_label_create(search_bar);
    lv_label_set_text(search_icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(search_icon, lv_color_hex(0x888888), 0);
    lv_obj_align(search_icon, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t *search_text = lv_label_create(search_bar);
    lv_label_set_text(search_text, "Type to search");
    lv_obj_set_style_text_color(search_text, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(search_text, UI_FONT, 0);
    lv_obj_align(search_text, LV_ALIGN_LEFT_MID, 25, 0);
    
    // "Pinned" section header
    lv_obj_t *pinned_header = lv_label_create(start_menu);
    lv_label_set_text(pinned_header, "Pinned");
    lv_obj_set_style_text_color(pinned_header, lv_color_white(), 0);
    lv_obj_set_style_text_font(pinned_header, UI_FONT, 0);
    lv_obj_align(pinned_header, LV_ALIGN_TOP_LEFT, 5, 50);
    
    // App grid container
    lv_obj_t *app_grid = lv_obj_create(start_menu);
    lv_obj_set_size(app_grid, lv_pct(100), 360);
    lv_obj_align(app_grid, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_opa(app_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app_grid, 0, 0);
    lv_obj_set_style_pad_all(app_grid, 5, 0);
    lv_obj_set_flex_flow(app_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(app_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(app_grid, 8, 0);
    lv_obj_set_style_pad_column(app_grid, 8, 0);
    lv_obj_add_flag(app_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(app_grid, LV_SCROLLBAR_MODE_AUTO);
    
    // Add apps as grid items (Win11 style - icon with label below)
    for (int i = 0; i < NUM_DESKTOP_APPS; i++) {
        const app_def_t *app = &desktop_apps[i];
        
        lv_obj_t *item = lv_obj_create(app_grid);
        lv_obj_set_size(item, 60, 70);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x404040), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 6, 0);
        lv_obj_set_style_pad_all(item, 4, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *icon = lv_image_create(item);
        lv_image_set_src(icon, app->icon);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, app->title);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, UI_FONT, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(label, 56);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_add_event_cb(item, start_menu_item_event_cb, LV_EVENT_CLICKED, (void*)app->name);
    }
    
    // Bottom bar with user avatar and power button
    lv_obj_t *bottom_bar = lv_obj_create(start_menu);
    lv_obj_set_size(bottom_bar, lv_pct(100), 50);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_bar, 1, 0);
    lv_obj_set_style_border_color(bottom_bar, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_side(bottom_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 8, 0);
    lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    const char *username = settings_get_username();
    uint32_t avatar_color = settings_get_avatar_color();
    
    // User avatar (Win11 style - circular)
    start_menu_avatar = lv_obj_create(bottom_bar);
    lv_obj_set_size(start_menu_avatar, 36, 36);
    lv_obj_align(start_menu_avatar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(start_menu_avatar, lv_color_hex(avatar_color), 0);
    lv_obj_set_style_border_width(start_menu_avatar, 0, 0);
    lv_obj_set_style_radius(start_menu_avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(start_menu_avatar, LV_OBJ_FLAG_SCROLLABLE);
    
    start_menu_avatar_letter = lv_label_create(start_menu_avatar);
    char avatar_letter[2] = {username[0], '\0'};
    if (avatar_letter[0] >= 'a' && avatar_letter[0] <= 'z') avatar_letter[0] -= 32;
    lv_label_set_text(start_menu_avatar_letter, avatar_letter);
    lv_obj_set_style_text_color(start_menu_avatar_letter, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_menu_avatar_letter, UI_FONT, 0);
    lv_obj_center(start_menu_avatar_letter);
    
    start_menu_username = lv_label_create(bottom_bar);
    lv_label_set_text(start_menu_username, username);
    lv_obj_set_style_text_color(start_menu_username, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_menu_username, UI_FONT, 0);
    lv_obj_align(start_menu_username, LV_ALIGN_LEFT_MID, 45, 0);
    
    // Power button (Win11 style)
    lv_obj_t *power_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(power_btn, 36, 36);
    lv_obj_align(power_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(power_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(power_btn, lv_color_hex(0x404040), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(power_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(power_btn, 6, 0);
    lv_obj_add_event_cb(power_btn, power_menu_item_cb, LV_EVENT_CLICKED, (void*)"shutdown");
    
    lv_obj_t *power_icon = lv_label_create(power_btn);
    lv_label_set_text(power_icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(power_icon, lv_color_white(), 0);
    lv_obj_center(power_icon);
}

static void start_menu_item_event_cb(lv_event_t *e)
{
    const char *app_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Start menu item clicked: %s", app_name);
    
    win32_hide_start_menu();
    
    if (strcmp(app_name, "debug") == 0) {
        create_debug_app();
        return;
    }
    
    // Handle folder shortcuts - open My Computer with path
    if (strcmp(app_name, "folder_documents") == 0) {
        if (app_launch_callback) {
            app_launch_callback("my_computer_documents");
        }
        return;
    }
    if (strcmp(app_name, "folder_pictures") == 0) {
        if (app_launch_callback) {
            app_launch_callback("my_computer_pictures");
        }
        return;
    }
    if (strcmp(app_name, "folder_games") == 0) {
        if (app_launch_callback) {
            app_launch_callback("my_computer_games");
        }
        return;
    }
    
    // Handle special apps
    if (strcmp(app_name, "default_programs") == 0) {
        if (app_launch_callback) {
            app_launch_callback("default_programs");
        }
        return;
    }
    if (strcmp(app_name, "help") == 0) {
        if (app_launch_callback) {
            app_launch_callback("help");
        }
        return;
    }
    
    if (app_launch_callback) {
        app_launch_callback(app_name);
    }
}

void win32_toggle_start_menu(void)
{
    if (start_menu_visible) {
        win32_hide_start_menu();
    } else {
        win32_show_start_menu();
    }
}

void win32_show_start_menu(void)
{
    if (start_menu && !start_menu_visible) {
        // Get menu height based on style
        ui_style_t style = settings_get_ui_style();
        int menu_height = (style == UI_STYLE_WIN11) ? 540 : 520;
        
        // Position at bottom, animate up
        int target_y = SCREEN_HEIGHT - TASKBAR_HEIGHT - menu_height;
        lv_obj_set_y(start_menu, SCREEN_HEIGHT);
        lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
        
        // CRITICAL: Move start menu to foreground (above all icons and apps)
        lv_obj_move_foreground(start_menu);
        
        // Smooth slide-up animation with longer duration
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, start_menu);
        lv_anim_set_values(&a, SCREEN_HEIGHT, target_y);
        lv_anim_set_duration(&a, 220);  // Slightly longer for smoother feel
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);  // Smooth deceleration
        lv_anim_start(&a);
        
        start_menu_visible = true;
        ESP_LOGI(TAG, "Start menu shown");
    }
}

void win32_hide_start_menu(void)
{
    if (start_menu && start_menu_visible) {
        // Smooth slide-down animation
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, start_menu);
        lv_anim_set_values(&a, lv_obj_get_y(start_menu), SCREEN_HEIGHT);
        lv_anim_set_duration(&a, 180);  // Quick but smooth
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);  // Smooth acceleration
        lv_anim_set_completed_cb(&a, [](lv_anim_t *a) {
            lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
        });
        lv_anim_start(&a);
        
        start_menu_visible = false;
        ESP_LOGI(TAG, "Start menu hidden");
    }
}

bool win32_is_start_menu_visible(void)
{
    return start_menu_visible;
}

// Refresh start menu user profile (call after settings change)
void win32_refresh_start_menu_user(void)
{
    if (start_menu_avatar) {
        uint32_t avatar_color = settings_get_avatar_color();
        lv_obj_set_style_bg_color(start_menu_avatar, lv_color_hex(avatar_color), 0);
    }
    if (start_menu_avatar_letter) {
        const char *username = settings_get_username();
        char letter[2] = {username[0], '\0'};
        if (letter[0] >= 'a' && letter[0] <= 'z') letter[0] -= 32;
        lv_label_set_text(start_menu_avatar_letter, letter);
    }
    if (start_menu_username) {
        lv_label_set_text(start_menu_username, settings_get_username());
    }
    ESP_LOGI(TAG, "Start menu user profile refreshed");
}

void win32_show_desktop(void)
{
    ESP_LOGI(TAG, "Showing desktop");
    // Use simple screen load without animation to reduce underruns
    lv_screen_load(scr_desktop);
    
    // Set screen state and restore brightness
    current_screen_state = SCREEN_STATE_DESKTOP;
    hw_backlight_set(settings_get_brightness());
    
    // Start time update timer
    static lv_timer_t *time_timer = NULL;
    if (!time_timer) {
        time_timer = lv_timer_create([](lv_timer_t *t) {
            win32_update_time();
        }, 1000, NULL);
    }
    
    // Initial updates
    win32_update_time();
    win32_update_wifi(false);
    win32_update_battery(75, false);
}

// ============ DEBUG APP ============

static void close_app_window(void)
{
    if (app_window) {
        lv_obj_delete(app_window);
        app_window = NULL;
    }
}

// Touch test state for debug app
static lv_obj_t *touch_canvas = NULL;
static lv_obj_t *touch_info_label = NULL;

static void debug_touch_draw_cb(lv_event_t *e)
{
    lv_obj_t *canvas = (lv_obj_t *)lv_event_get_target(e);
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    
    // Get canvas absolute position on screen
    lv_area_t canvas_area;
    lv_obj_get_coords(canvas, &canvas_area);
    
    // Calculate relative position inside canvas
    int32_t rel_x = point.x - canvas_area.x1;
    int32_t rel_y = point.y - canvas_area.y1;
    
    // Only draw if inside canvas bounds
    if (rel_x >= 2 && rel_x < lv_obj_get_width(canvas) - 2 && 
        rel_y >= 2 && rel_y < lv_obj_get_height(canvas) - 2) {
        // Create small circle at touch point
        lv_obj_t *dot = lv_obj_create(canvas);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_pos(dot, rel_x - 3, rel_y - 3);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void create_debug_app(void)
{
    ESP_LOGI(TAG, "Opening Debug app");
    
    // Close existing window
    if (app_window) {
        lv_obj_delete(app_window);
        app_window = NULL;
    }
    
    // Create window
    app_window = lv_obj_create(scr_desktop);
    lv_obj_set_size(app_window, SCREEN_WIDTH - 20, SCREEN_HEIGHT - TASKBAR_HEIGHT - 20);
    lv_obj_align(app_window, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(app_window, lv_color_hex(COLOR_WINDOW_BG), 0);
    lv_obj_set_style_border_color(app_window, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(app_window, 2, 0);
    lv_obj_set_style_radius(app_window, 8, 0);
    lv_obj_set_style_pad_all(app_window, 0, 0);
    lv_obj_remove_flag(app_window, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(app_window);
    lv_obj_set_size(title_bar, lv_pct(100), 32);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_left(title_bar, 10, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(title_bar);
    lv_label_set_text(title, "Debug - System Info");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 28, 22);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_radius(close_btn, 3, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        close_app_window();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_center(close_label);
    
    // Content area with scroll - positioned below title bar
    lv_obj_t *content = lv_obj_create(app_window);
    lv_obj_set_size(content, lv_pct(100), SCREEN_HEIGHT - TASKBAR_HEIGHT - 20 - 32 - 4);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 6, 0);
    
    // Get system info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    
    // Create info labels
    auto add_info = [&](const char *text) {
        lv_obj_t *lbl = lv_label_create(content);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_set_width(lbl, lv_pct(100));
    };
    
    char buf[128];
    
    add_info("=== CHIP INFO ===");
    snprintf(buf, sizeof(buf), "Chip: ESP32-P4 rev %d.%d", chip_info.revision / 100, chip_info.revision % 100);
    add_info(buf);
    snprintf(buf, sizeof(buf), "Cores: %d", chip_info.cores);
    add_info(buf);
    
    add_info("");
    add_info("=== MEMORY ===");
    snprintf(buf, sizeof(buf), "Free Heap: %lu KB", free_heap / 1024);
    add_info(buf);
    snprintf(buf, sizeof(buf), "Min Free Heap: %lu KB", min_heap / 1024);
    add_info(buf);
    snprintf(buf, sizeof(buf), "PSRAM Total: %lu MB", total_psram / (1024*1024));
    add_info(buf);
    snprintf(buf, sizeof(buf), "PSRAM Free: %lu MB", free_psram / (1024*1024));
    add_info(buf);
    
    add_info("");
    add_info("=== DISPLAY ===");
    snprintf(buf, sizeof(buf), "Resolution: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
    add_info(buf);
    add_info("Driver: ST7701S (MIPI-DSI)");
    add_info("Touch: GT911 (I2C)");
    
    add_info("");
    add_info("=== LVGL ===");
    snprintf(buf, sizeof(buf), "Version: %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    add_info(buf);
    
    add_info("");
    add_info("=== TOUCH TEST ===");
    
    // Touch info label
    touch_info_label = lv_label_create(content);
    lv_label_set_text(touch_info_label, "Touch: --- | State: ---");
    lv_obj_set_style_text_color(touch_info_label, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_text_font(touch_info_label, UI_FONT, 0);
    lv_obj_set_width(touch_info_label, lv_pct(100));
    
    // Touch test canvas (draw area)
    touch_canvas = lv_obj_create(content);
    lv_obj_set_size(touch_canvas, lv_pct(100), 120);
    lv_obj_set_style_bg_color(touch_canvas, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_border_color(touch_canvas, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(touch_canvas, 2, 0);
    lv_obj_set_style_radius(touch_canvas, 4, 0);
    lv_obj_add_flag(touch_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch_canvas, LV_OBJ_FLAG_SCROLLABLE);
    
    // Canvas label
    lv_obj_t *canvas_hint = lv_label_create(touch_canvas);
    lv_label_set_text(canvas_hint, "Draw here to test touch");
    lv_obj_set_style_text_color(canvas_hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(canvas_hint, UI_FONT, 0);
    lv_obj_align(canvas_hint, LV_ALIGN_TOP_MID, 0, 5);
    
    // Touch events for canvas
    lv_obj_add_event_cb(touch_canvas, [](lv_event_t *e) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        char buf[64];
        snprintf(buf, sizeof(buf), "Touch: X=%ld Y=%ld | PRESSED", (long)point.x, (long)point.y);
        if (touch_info_label) lv_label_set_text(touch_info_label, buf);
    }, LV_EVENT_PRESSED, NULL);
    
    lv_obj_add_event_cb(touch_canvas, [](lv_event_t *e) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        char buf[64];
        snprintf(buf, sizeof(buf), "Touch: X=%ld Y=%ld | PRESSING", (long)point.x, (long)point.y);
        if (touch_info_label) lv_label_set_text(touch_info_label, buf);
    }, LV_EVENT_PRESSING, NULL);
    
    lv_obj_add_event_cb(touch_canvas, debug_touch_draw_cb, LV_EVENT_PRESSING, NULL);
    
    lv_obj_add_event_cb(touch_canvas, [](lv_event_t *e) {
        if (touch_info_label) lv_label_set_text(touch_info_label, "Touch: --- | RELEASED");
    }, LV_EVENT_RELEASED, NULL);
    
    // Button row
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_set_size(btn_row, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Clear canvas button
    lv_obj_t *clear_btn = lv_btn_create(btn_row);
    lv_obj_set_size(clear_btn, 90, 36);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(clear_btn, 4, 0);
    
    lv_obj_t *clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_set_style_text_color(clear_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(clear_label, UI_FONT, 0);
    lv_obj_center(clear_label);
    
    lv_obj_add_event_cb(clear_btn, [](lv_event_t *e) {
        // Delete all children except hint label
        if (touch_canvas) {
            uint32_t child_cnt = lv_obj_get_child_count(touch_canvas);
            for (int i = child_cnt - 1; i >= 1; i--) {
                lv_obj_t *child = lv_obj_get_child(touch_canvas, i);
                lv_obj_delete(child);
            }
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Refresh button
    lv_obj_t *refresh_btn = lv_btn_create(btn_row);
    lv_obj_set_size(refresh_btn, 90, 36);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x0054E3), 0);
    lv_obj_set_style_radius(refresh_btn, 4, 0);
    
    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_set_style_text_color(refresh_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(refresh_label, UI_FONT, 0);
    lv_obj_center(refresh_label);
    
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t *e) {
        close_app_window();
        create_debug_app();
    }, LV_EVENT_CLICKED, NULL);
    
    // Color test button
    lv_obj_t *color_btn = lv_btn_create(btn_row);
    lv_obj_set_size(color_btn, 90, 36);
    lv_obj_set_style_bg_color(color_btn, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_radius(color_btn, 4, 0);
    
    lv_obj_t *color_label = lv_label_create(color_btn);
    lv_label_set_text(color_label, "Colors");
    lv_obj_set_style_text_color(color_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(color_label, UI_FONT, 0);
    lv_obj_center(color_label);
    
    lv_obj_add_event_cb(color_btn, [](lv_event_t *e) {
        if (touch_canvas) {
            static int color_idx = 0;
            uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF, 0x000000};
            lv_obj_set_style_bg_color(touch_canvas, lv_color_hex(colors[color_idx]), 0);
            color_idx = (color_idx + 1) % 8;
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Interface tests section
    add_info("");
    add_info("=== INTERFACE TESTS ===");
    
    // Test buttons row
    lv_obj_t *test_row = lv_obj_create(content);
    lv_obj_set_size(test_row, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(test_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(test_row, 0, 0);
    lv_obj_set_style_pad_all(test_row, 0, 0);
    lv_obj_set_flex_flow(test_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(test_row, 8, 0);
    lv_obj_remove_flag(test_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Backlight test button
    lv_obj_t *bl_btn = lv_btn_create(test_row);
    lv_obj_set_size(bl_btn, 90, 36);
    lv_obj_set_style_bg_color(bl_btn, lv_color_hex(0xFF8800), 0);
    lv_obj_set_style_radius(bl_btn, 4, 0);
    
    lv_obj_t *bl_label = lv_label_create(bl_btn);
    lv_label_set_text(bl_label, "BL Test");
    lv_obj_set_style_text_color(bl_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl_label, UI_FONT, 0);
    lv_obj_center(bl_label);
    
    static int bl_level = 100;
    lv_obj_add_event_cb(bl_btn, [](lv_event_t *e) {
        bl_level = (bl_level == 100) ? 20 : (bl_level == 20) ? 50 : 100;
        hw_backlight_set(bl_level);
        ESP_LOGI(TAG, "Backlight test: %d%%", bl_level);
    }, LV_EVENT_CLICKED, NULL);
    
    // Stress test button
    lv_obj_t *stress_btn = lv_btn_create(test_row);
    lv_obj_set_size(stress_btn, 90, 36);
    lv_obj_set_style_bg_color(stress_btn, lv_color_hex(0xAA0000), 0);
    lv_obj_set_style_radius(stress_btn, 4, 0);
    
    lv_obj_t *stress_label = lv_label_create(stress_btn);
    lv_label_set_text(stress_label, "Stress");
    lv_obj_set_style_text_color(stress_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(stress_label, UI_FONT, 0);
    lv_obj_center(stress_label);
    
    lv_obj_add_event_cb(stress_btn, [](lv_event_t *e) {
        if (touch_canvas) {
            for (int i = 0; i < 30; i++) {
                lv_obj_t *dot = lv_obj_create(touch_canvas);
                lv_obj_set_size(dot, 10, 10);
                lv_obj_set_pos(dot, esp_random() % 380, esp_random() % 100);
                lv_obj_set_style_bg_color(dot, lv_color_hex(esp_random() & 0xFFFFFF), 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            }
            ESP_LOGI(TAG, "Stress test: created 30 objects");
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Lock test button
    lv_obj_t *lock_btn = lv_btn_create(test_row);
    lv_obj_set_size(lock_btn, 90, 36);
    lv_obj_set_style_bg_color(lock_btn, lv_color_hex(0x6600AA), 0);
    lv_obj_set_style_radius(lock_btn, 4, 0);
    
    lv_obj_t *lock_label = lv_label_create(lock_btn);
    lv_label_set_text(lock_label, "Lock");
    lv_obj_set_style_text_color(lock_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(lock_label, UI_FONT, 0);
    lv_obj_center(lock_label);
    
    lv_obj_add_event_cb(lock_btn, [](lv_event_t *e) {
        close_app_window();
        win32_lock_device();
    }, LV_EVENT_CLICKED, NULL);
}

// ============ WALLPAPER MANAGEMENT ============

void win32_set_wallpaper(int index)
{
    if (index < 0 || index >= WALLPAPER_COUNT) {
        ESP_LOGW(TAG, "Invalid wallpaper index: %d", index);
        return;
    }
    
    current_wallpaper_index = index;
    
    // Update desktop wallpaper
    if (desktop_wallpaper) {
        lv_image_set_src(desktop_wallpaper, wallpapers[index].image);
        // Stretch to fill entire screen
        lv_obj_set_size(desktop_wallpaper, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_image_set_inner_align(desktop_wallpaper, LV_IMAGE_ALIGN_STRETCH);
        ESP_LOGI(TAG, "Wallpaper changed to: %s", wallpapers[index].name);
    }
    
    // Update lock screen wallpaper (same as desktop)
    if (lock_wallpaper) {
        lv_image_set_src(lock_wallpaper, wallpapers[index].image);
        lv_obj_set_size(lock_wallpaper, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_image_set_inner_align(lock_wallpaper, LV_IMAGE_ALIGN_STRETCH);
    }
}

int win32_get_wallpaper_index(void)
{
    return current_wallpaper_index;
}

int win32_get_wallpaper_count(void)
{
    return WALLPAPER_COUNT;
}

// ============ LOCK SCREEN (iPhone Style) ============

static void lock_timer_cb(lv_timer_t *timer)
{
    update_lock_time();
    update_aod_time();
}

static void update_lock_time(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (lock_time_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lv_label_set_text(lock_time_label, buf);
    }
    
    if (lock_date_label) {
        char buf[64];
        static const char* days_en[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char* months_en[] = {"January", "February", "March", "April", "May", "June",
                                          "July", "August", "September", "October", "November", "December"};
        snprintf(buf, sizeof(buf), "%s, %s %d",
                 days_en[timeinfo.tm_wday], months_en[timeinfo.tm_mon], timeinfo.tm_mday);
        lv_label_set_text(lock_date_label, buf);
    }
}

static void update_aod_time(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (aod_time_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lv_label_set_text(aod_time_label, buf);
    }
}

// Lock screen slider state
static lv_obj_t *lock_slider_bar = NULL;
static lv_obj_t *lock_slider_handle = NULL;
static int32_t lock_slider_start_y = 0;
static bool lock_slider_dragging = false;

static void lock_slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *handle = (lv_obj_t *)lv_event_get_target(e);
    
    if (code == LV_EVENT_PRESSED) {
        lock_slider_dragging = true;
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        lock_slider_start_y = point.x;  // Use X for horizontal slide
        // Visual feedback - highlight
        lv_obj_set_style_bg_color(handle, lv_color_hex(0xFFFFFF), 0);
    }
    else if (code == LV_EVENT_PRESSING && lock_slider_dragging) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        int32_t delta_x = point.x - lock_slider_start_y;
        
        // Move handle right (max 120px within bar)
        if (delta_x > 0 && delta_x < 120) {
            lv_obj_set_x(handle, 7 + delta_x);
            lv_obj_set_style_opa(handle, LV_OPA_COVER - (delta_x * 150 / 120), 0);
        }
        
        // Unlock threshold - 80px slide right
        if (delta_x >= 80) {
            lock_slider_dragging = false;
            win32_show_desktop();
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        lock_slider_dragging = false;
        // Reset handle position
        lv_obj_set_x(handle, 7);
        lv_obj_set_style_opa(handle, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(handle, lv_color_hex(0xDDDDDD), 0);
    }
}

// Forward declarations for lock screen
static void lock_pin_key_clicked(lv_event_t *e);
static void lock_pin_backspace_clicked(lv_event_t *e);
static void lock_password_submit_clicked(lv_event_t *e);
static void lock_update_pin_dots(void);
static void lock_check_pin(void);
static void lock_check_password(void);

// PIN keypad key click handler
static void lock_pin_key_clicked(lv_event_t *e)
{
    int digit = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (lock_pin_length < 6) {
        lock_pin_buffer[lock_pin_length] = '0' + digit;
        lock_pin_length++;
        lock_pin_buffer[lock_pin_length] = '\0';
        lock_update_pin_dots();
        
        // Auto-check when PIN reaches expected length (4 or 6 digits)
        if (lock_pin_length >= 4) {
            lock_check_pin();
        }
    }
}

// PIN backspace handler
static void lock_pin_backspace_clicked(lv_event_t *e)
{
    if (lock_pin_length > 0) {
        lock_pin_length--;
        lock_pin_buffer[lock_pin_length] = '\0';
        lock_update_pin_dots();
        
        // Clear error message
        if (lock_pin_error_label) {
            lv_label_set_text(lock_pin_error_label, "");
        }
    }
}

// Update PIN dots display
static void lock_update_pin_dots(void)
{
    for (int i = 0; i < 6; i++) {
        if (lock_pin_dots[i]) {
            if (i < lock_pin_length) {
                lv_obj_set_style_bg_color(lock_pin_dots[i], lv_color_white(), 0);
            } else {
                lv_obj_set_style_bg_color(lock_pin_dots[i], lv_color_hex(0x555555), 0);
            }
        }
    }
}

// Check PIN and unlock if correct
static void lock_check_pin(void)
{
    if (settings_check_password(lock_pin_buffer)) {
        // Correct PIN - unlock
        ESP_LOGI(TAG, "PIN correct - unlocking");
        lock_pin_length = 0;
        memset(lock_pin_buffer, 0, sizeof(lock_pin_buffer));
        lock_update_pin_dots();
        win32_show_desktop();
    } else if (lock_pin_length >= 6) {
        // Wrong PIN after max digits
        ESP_LOGW(TAG, "Wrong PIN");
        if (lock_pin_error_label) {
            lv_label_set_text(lock_pin_error_label, "Wrong PIN");
        }
        // Clear PIN after short delay
        lv_timer_create([](lv_timer_t *t) {
            lock_pin_length = 0;
            memset(lock_pin_buffer, 0, sizeof(lock_pin_buffer));
            lock_update_pin_dots();
            if (lock_pin_error_label) {
                lv_label_set_text(lock_pin_error_label, "");
            }
            lv_timer_delete(t);
        }, 1000, NULL);
    }
}

// Password submit handler
static void lock_password_submit_clicked(lv_event_t *e)
{
    lock_check_password();
}

// Check password and unlock if correct
static void lock_check_password(void)
{
    if (!lock_password_textarea) return;
    
    const char *password = lv_textarea_get_text(lock_password_textarea);
    
    if (settings_check_password(password)) {
        // Correct password - unlock
        ESP_LOGI(TAG, "Password correct - unlocking");
        lv_textarea_set_text(lock_password_textarea, "");
        win32_show_desktop();
    } else {
        // Wrong password
        ESP_LOGW(TAG, "Wrong password");
        if (lock_password_error_label) {
            lv_label_set_text(lock_password_error_label, "Wrong password");
        }
        lv_textarea_set_text(lock_password_textarea, "");
        // Clear error after delay
        lv_timer_create([](lv_timer_t *t) {
            if (lock_password_error_label) {
                lv_label_set_text(lock_password_error_label, "");
            }
            lv_timer_delete(t);
        }, 2000, NULL);
    }
}

static void create_lock_screen(void)
{
    scr_lock = lv_obj_create(NULL);
    lv_obj_set_size(scr_lock, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_remove_flag(scr_lock, LV_OBJ_FLAG_SCROLLABLE);
    
    // Wallpaper (same as desktop, will be updated)
    lock_wallpaper = lv_image_create(scr_lock);
    lv_image_set_src(lock_wallpaper, &img_win7);  // Default to Win7 wallpaper
    lv_obj_set_size(lock_wallpaper, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_image_set_inner_align(lock_wallpaper, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_align(lock_wallpaper, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Dark overlay for dimming effect
    lock_overlay = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(lock_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(lock_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lock_overlay, LV_OPA_40, 0);
    lv_obj_set_style_border_width(lock_overlay, 0, 0);
    lv_obj_remove_flag(lock_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    // Recovery mode trigger - invisible 50x50 area in top-left corner
    // Tap 3 times within 2 seconds to trigger recovery mode
    lv_obj_t *recovery_trigger = lv_obj_create(scr_lock);
    lv_obj_set_size(recovery_trigger, 50, 50);
    lv_obj_align(recovery_trigger, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(recovery_trigger, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(recovery_trigger, 0, 0);
    lv_obj_add_flag(recovery_trigger, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(recovery_trigger, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(recovery_trigger, [](lv_event_t *e) {
        uint64_t now = esp_timer_get_time() / 1000;  // Convert to ms
        
        // Reset counter if timeout expired
        if (lock_recovery_tap_count > 0 && (now - lock_recovery_first_tap_time) > LOCK_RECOVERY_TAP_TIMEOUT_MS) {
            lock_recovery_tap_count = 0;
            ESP_LOGI(TAG, "Lock recovery tap timeout, resetting counter");
        }
        
        // First tap - record time
        if (lock_recovery_tap_count == 0) {
            lock_recovery_first_tap_time = now;
        }
        
        lock_recovery_tap_count++;
        ESP_LOGI(TAG, "Lock recovery tap count: %d/%d", lock_recovery_tap_count, LOCK_RECOVERY_TAP_COUNT);
        
        if (lock_recovery_tap_count >= LOCK_RECOVERY_TAP_COUNT) {
            lock_recovery_tap_count = 0;
            ESP_LOGW(TAG, "Lock screen recovery trigger activated!");
            show_lock_recovery_dialog();
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Time label (large, centered) - iPhone style
    lock_time_label = lv_label_create(scr_lock);
    lv_label_set_text(lock_time_label, "12:00");
    lv_obj_set_style_text_color(lock_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(lock_time_label, UI_FONT, 0);
    lv_obj_set_style_transform_scale(lock_time_label, 768, 0);  // 3x scale for much larger text
    lv_obj_align(lock_time_label, LV_ALIGN_TOP_MID, 0, 60);
    
    // Date label below time
    lock_date_label = lv_label_create(scr_lock);
    lv_label_set_text(lock_date_label, "Sunday, December 21");
    lv_obj_set_style_text_color(lock_date_label, lv_color_hex(0xdddddd), 0);
    lv_obj_set_style_text_font(lock_date_label, UI_FONT, 0);
    lv_obj_align(lock_date_label, LV_ALIGN_TOP_MID, 0, 150);
    
    // User avatar container (higher position)
    lock_avatar_cont = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_avatar_cont, 80, 80);
    lv_obj_align(lock_avatar_cont, LV_ALIGN_TOP_MID, 0, 190);
    uint32_t avatar_color = settings_get_avatar_color();
    lv_obj_set_style_bg_color(lock_avatar_cont, lv_color_hex(avatar_color), 0);
    lv_obj_set_style_bg_grad_color(lock_avatar_cont, lv_color_hex(avatar_color > 0x202020 ? avatar_color - 0x202020 : 0), 0);
    lv_obj_set_style_bg_grad_dir(lock_avatar_cont, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(lock_avatar_cont, 3, 0);
    lv_obj_set_style_border_color(lock_avatar_cont, lv_color_white(), 0);
    lv_obj_set_style_radius(lock_avatar_cont, 8, 0);
    lv_obj_set_style_shadow_width(lock_avatar_cont, 20, 0);
    lv_obj_set_style_shadow_color(lock_avatar_cont, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(lock_avatar_cont, LV_OPA_50, 0);
    lv_obj_remove_flag(lock_avatar_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // User icon inside avatar - first letter of username
    lock_avatar_letter = lv_label_create(lock_avatar_cont);
    const char *username_str = settings_get_username();
    char letter[2] = {username_str[0], '\0'};
    if (letter[0] >= 'a' && letter[0] <= 'z') letter[0] -= 32;  // Uppercase
    lv_label_set_text(lock_avatar_letter, letter);
    lv_obj_set_style_text_color(lock_avatar_letter, lv_color_white(), 0);
    lv_obj_set_style_text_font(lock_avatar_letter, UI_FONT, 0);
    lv_obj_set_style_transform_scale(lock_avatar_letter, 320, 0);  // 1.25x scale
    lv_obj_center(lock_avatar_letter);
    
    // Username label
    lock_username_label = lv_label_create(scr_lock);
    lv_label_set_text(lock_username_label, username_str);
    lv_obj_set_style_text_color(lock_username_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(lock_username_label, UI_FONT, 0);
    lv_obj_align(lock_username_label, LV_ALIGN_TOP_MID, 0, 280);
    
    // ============ SLIDE TO UNLOCK CONTAINER ============
    lock_slide_container = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_slide_container, SCREEN_WIDTH, 120);
    lv_obj_align(lock_slide_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_opa(lock_slide_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lock_slide_container, 0, 0);
    lv_obj_remove_flag(lock_slide_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // iPhone-style unlock slider bar at bottom - HORIZONTAL slide to unlock
    lock_slider_bar = lv_obj_create(lock_slide_container);
    lv_obj_set_size(lock_slider_bar, 280, 60);
    lv_obj_align(lock_slider_bar, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(lock_slider_bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(lock_slider_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(lock_slider_bar, 2, 0);
    lv_obj_set_style_border_color(lock_slider_bar, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(lock_slider_bar, 30, 0);
    lv_obj_set_style_pad_all(lock_slider_bar, 0, 0);
    lv_obj_remove_flag(lock_slider_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Slider handle (draggable) - square handle on left
    lock_slider_handle = lv_obj_create(lock_slider_bar);
    lv_obj_set_size(lock_slider_handle, 50, 46);
    lv_obj_set_pos(lock_slider_handle, 7, 7);
    lv_obj_set_style_bg_color(lock_slider_handle, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_border_width(lock_slider_handle, 0, 0);
    lv_obj_set_style_radius(lock_slider_handle, 23, 0);
    lv_obj_set_style_shadow_width(lock_slider_handle, 10, 0);
    lv_obj_set_style_shadow_color(lock_slider_handle, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(lock_slider_handle, LV_OPA_40, 0);
    lv_obj_add_flag(lock_slider_handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(lock_slider_handle, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *arrow = lv_label_create(lock_slider_handle);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(arrow, UI_FONT, 0);
    lv_obj_center(arrow);
    
    lv_obj_t *slide_text = lv_label_create(lock_slider_bar);
    lv_label_set_text(slide_text, "slide to unlock");
    lv_obj_set_style_text_color(slide_text, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(slide_text, UI_FONT, 0);
    lv_obj_align(slide_text, LV_ALIGN_CENTER, 30, 0);
    
    lv_obj_add_event_cb(lock_slider_handle, lock_slider_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(lock_slider_handle, lock_slider_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(lock_slider_handle, lock_slider_event_cb, LV_EVENT_RELEASED, NULL);
    
    lock_swipe_hint = lv_label_create(lock_slide_container);
    lv_label_set_text(lock_swipe_hint, "");
    lv_obj_set_style_text_color(lock_swipe_hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lock_swipe_hint, UI_FONT, 0);
    lv_obj_align(lock_swipe_hint, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    // ============ PIN KEYPAD CONTAINER ============
    lock_pin_container = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_pin_container, SCREEN_WIDTH, 480);
    lv_obj_align(lock_pin_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(lock_pin_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lock_pin_container, 0, 0);
    lv_obj_remove_flag(lock_pin_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lock_pin_container, LV_OBJ_FLAG_HIDDEN);
    
    // PIN dots row (larger dots)
    lv_obj_t *pin_dots_row = lv_obj_create(lock_pin_container);
    lv_obj_set_size(pin_dots_row, 240, 40);
    lv_obj_align(pin_dots_row, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_opa(pin_dots_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pin_dots_row, 0, 0);
    lv_obj_set_flex_flow(pin_dots_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pin_dots_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pin_dots_row, 20, 0);
    lv_obj_remove_flag(pin_dots_row, LV_OBJ_FLAG_SCROLLABLE);
    
    for (int i = 0; i < 6; i++) {
        lock_pin_dots[i] = lv_obj_create(pin_dots_row);
        lv_obj_set_size(lock_pin_dots[i], 20, 20);
        lv_obj_set_style_radius(lock_pin_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(lock_pin_dots[i], lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(lock_pin_dots[i], 2, 0);
        lv_obj_set_style_border_color(lock_pin_dots[i], lv_color_hex(0x888888), 0);
        lv_obj_remove_flag(lock_pin_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // PIN error label
    lock_pin_error_label = lv_label_create(lock_pin_container);
    lv_label_set_text(lock_pin_error_label, "");
    lv_obj_set_style_text_color(lock_pin_error_label, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(lock_pin_error_label, UI_FONT, 0);
    lv_obj_align(lock_pin_error_label, LV_ALIGN_TOP_MID, 0, 55);
    
    // PIN keypad grid (3x4) - larger buttons
    lv_obj_t *pin_keypad = lv_obj_create(lock_pin_container);
    lv_obj_set_size(pin_keypad, 320, 340);
    lv_obj_align(pin_keypad, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_opa(pin_keypad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pin_keypad, 0, 0);
    lv_obj_set_style_pad_all(pin_keypad, 10, 0);
    lv_obj_set_layout(pin_keypad, LV_LAYOUT_GRID);
    static int32_t col_dsc[] = {90, 90, 90, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {75, 75, 75, 75, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(pin_keypad, col_dsc, row_dsc);
    lv_obj_remove_flag(pin_keypad, LV_OBJ_FLAG_SCROLLABLE);
    
    const char *pin_keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "<"};
    for (int i = 0; i < 12; i++) {
        int row = i / 3;
        int col = i % 3;
        
        if (strlen(pin_keys[i]) == 0) continue;  // Skip empty cell
        
        lv_obj_t *btn = lv_button_create(pin_keypad);
        lv_obj_set_size(btn, 80, 65);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_CENTER, col, 1, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x666666), 0);
        
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, pin_keys[i]);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, UI_FONT, 0);
        lv_obj_set_style_transform_scale(lbl, 320, 0);  // 1.25x scale for larger digits
        lv_obj_center(lbl);
        
        if (strcmp(pin_keys[i], "<") == 0) {
            lv_obj_add_event_cb(btn, lock_pin_backspace_clicked, LV_EVENT_CLICKED, NULL);
        } else {
            int digit = pin_keys[i][0] - '0';
            lv_obj_add_event_cb(btn, lock_pin_key_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)digit);
        }
    }
    
    // ============ PASSWORD INPUT CONTAINER ============
    lock_password_container = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_password_container, SCREEN_WIDTH, 350);
    lv_obj_align(lock_password_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(lock_password_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lock_password_container, 0, 0);
    lv_obj_remove_flag(lock_password_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lock_password_container, LV_OBJ_FLAG_HIDDEN);
    
    // Password textarea
    lock_password_textarea = lv_textarea_create(lock_password_container);
    lv_obj_set_size(lock_password_textarea, 280, 45);
    lv_obj_align(lock_password_textarea, LV_ALIGN_TOP_MID, 0, 5);
    lv_textarea_set_password_mode(lock_password_textarea, true);
    lv_textarea_set_placeholder_text(lock_password_textarea, "Enter password");
    lv_textarea_set_one_line(lock_password_textarea, true);
    lv_obj_set_style_text_font(lock_password_textarea, UI_FONT, 0);
    lv_obj_set_style_bg_color(lock_password_textarea, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_color(lock_password_textarea, lv_color_white(), 0);
    lv_obj_set_style_border_color(lock_password_textarea, lv_color_hex(0x555555), 0);
    
    // Password error label
    lock_password_error_label = lv_label_create(lock_password_container);
    lv_label_set_text(lock_password_error_label, "");
    lv_obj_set_style_text_color(lock_password_error_label, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(lock_password_error_label, UI_FONT, 0);
    lv_obj_align(lock_password_error_label, LV_ALIGN_TOP_MID, 0, 55);
    
    // Password keyboard
    lock_password_keyboard = lv_keyboard_create(lock_password_container);
    lv_obj_set_size(lock_password_keyboard, SCREEN_WIDTH - 20, 220);
    lv_obj_align(lock_password_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(lock_password_keyboard, lock_password_textarea);
    lv_obj_set_style_bg_color(lock_password_keyboard, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(lock_password_keyboard, lv_color_hex(0x333333), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(lock_password_keyboard, lv_color_hex(0x555555), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lock_password_keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(lock_password_keyboard, UI_FONT, LV_PART_ITEMS);
    
    // Handle Enter key on keyboard
    lv_obj_add_event_cb(lock_password_keyboard, [](lv_event_t *e) {
        uint32_t btn_id = lv_keyboard_get_selected_button(lock_password_keyboard);
        const char *txt = lv_buttonmatrix_get_button_text(lock_password_keyboard, btn_id);
        if (txt && strcmp(txt, LV_SYMBOL_OK) == 0) {
            lock_check_password();
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Create timer for updating time
    if (!lock_timer) {
        lock_timer = lv_timer_create(lock_timer_cb, 1000, NULL);
    }
    
    update_lock_time();
    ESP_LOGI(TAG, "Lock screen created");
}

// ============ AOD (Always On Display) ============

static void aod_tap_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "AOD tapped - showing lock screen");
    win32_show_lock();
}

static void create_aod_screen(void)
{
    scr_aod = lv_obj_create(NULL);
    lv_obj_set_size(scr_aod, SCREEN_WIDTH, SCREEN_HEIGHT);
    
    // Pure black background for OLED power saving
    lv_obj_set_style_bg_color(scr_aod, lv_color_black(), 0);
    lv_obj_remove_flag(scr_aod, LV_OBJ_FLAG_SCROLLABLE);
    
    // Make entire screen clickable
    lv_obj_add_flag(scr_aod, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_aod, aod_tap_cb, LV_EVENT_CLICKED, NULL);
    
    // Time label (large, dim for AOD)
    aod_time_label = lv_label_create(scr_aod);
    lv_label_set_text(aod_time_label, "12:00");
    lv_obj_set_style_text_color(aod_time_label, lv_color_hex(0x444444), 0);  // Dim but visible
    lv_obj_set_style_text_font(aod_time_label, UI_FONT, 0);
    lv_obj_set_style_transform_scale(aod_time_label, 512, 0);  // 2x scale for larger text
    lv_obj_center(aod_time_label);
    
    // Tap hint below time
    lv_obj_t *aod_hint = lv_label_create(scr_aod);
    lv_label_set_text(aod_hint, "Tap to wake");
    lv_obj_set_style_text_color(aod_hint, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(aod_hint, UI_FONT, 0);
    lv_obj_align(aod_hint, LV_ALIGN_CENTER, 0, 50);
    
    update_aod_time();
    ESP_LOGI(TAG, "AOD screen created");
}

// ============ LOCK SCREEN RECOVERY DIALOG ============

static lv_obj_t *lock_recovery_dialog = NULL;

static void lock_recovery_yes_cb(lv_event_t *e)
{
    if (lock_recovery_dialog) {
        lv_obj_delete(lock_recovery_dialog);
        lock_recovery_dialog = NULL;
    }
    ESP_LOGW(TAG, "User confirmed - rebooting to Recovery Mode from lock screen");
    recovery_request_reboot();
}

static void lock_recovery_no_cb(lv_event_t *e)
{
    if (lock_recovery_dialog) {
        lv_obj_delete(lock_recovery_dialog);
        lock_recovery_dialog = NULL;
    }
    ESP_LOGI(TAG, "User cancelled recovery mode from lock screen");
}

static void show_lock_recovery_dialog(void)
{
    if (lock_recovery_dialog) {
        lv_obj_delete(lock_recovery_dialog);
    }
    
    // Create modal dialog on lock screen
    lock_recovery_dialog = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_recovery_dialog, 320, 180);
    lv_obj_center(lock_recovery_dialog);
    lv_obj_set_style_bg_color(lock_recovery_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(lock_recovery_dialog, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_border_width(lock_recovery_dialog, 2, 0);
    lv_obj_set_style_radius(lock_recovery_dialog, 8, 0);
    lv_obj_set_style_shadow_width(lock_recovery_dialog, 20, 0);
    lv_obj_set_style_shadow_color(lock_recovery_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(lock_recovery_dialog, LV_OPA_40, 0);
    lv_obj_remove_flag(lock_recovery_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(lock_recovery_dialog);
    lv_label_set_text(title, "Win Recovery");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Message
    lv_obj_t *msg = lv_label_create(lock_recovery_dialog);
    lv_label_set_text(msg, "Reboot to Recovery Mode?");
    lv_obj_set_style_text_color(msg, lv_color_black(), 0);
    lv_obj_set_style_text_font(msg, UI_FONT, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);
    
    // Yes button
    lv_obj_t *yes_btn = lv_btn_create(lock_recovery_dialog);
    lv_obj_set_size(yes_btn, 100, 40);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 30, -15);
    lv_obj_set_style_bg_color(yes_btn, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_radius(yes_btn, 4, 0);
    lv_obj_add_event_cb(yes_btn, lock_recovery_yes_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_color(yes_label, lv_color_white(), 0);
    lv_obj_center(yes_label);
    
    // No button
    lv_obj_t *no_btn = lv_btn_create(lock_recovery_dialog);
    lv_obj_set_size(no_btn, 100, 40);
    lv_obj_align(no_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -15);
    lv_obj_set_style_bg_color(no_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(no_btn, 4, 0);
    lv_obj_add_event_cb(no_btn, lock_recovery_no_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_color(no_label, lv_color_white(), 0);
    lv_obj_center(no_label);
}

// ============ PUBLIC RECOVERY DIALOG (called from main.cpp) ============

static lv_obj_t *main_recovery_dialog = NULL;

static void main_recovery_yes_cb(lv_event_t *e)
{
    if (main_recovery_dialog) {
        lv_obj_delete(main_recovery_dialog);
        main_recovery_dialog = NULL;
    }
    ESP_LOGW(TAG, "User confirmed - rebooting to Recovery Mode from BOOT button");
    recovery_request_reboot();
}

static void main_recovery_no_cb(lv_event_t *e)
{
    if (main_recovery_dialog) {
        lv_obj_delete(main_recovery_dialog);
        main_recovery_dialog = NULL;
    }
    ESP_LOGI(TAG, "User cancelled recovery mode from BOOT button");
}

void win32_show_recovery_dialog(void)
{
    // Delete existing dialog if any
    if (main_recovery_dialog) {
        lv_obj_delete(main_recovery_dialog);
    }
    
    // Create modal dialog on current screen
    main_recovery_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(main_recovery_dialog, 320, 180);
    lv_obj_center(main_recovery_dialog);
    lv_obj_set_style_bg_color(main_recovery_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(main_recovery_dialog, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_border_width(main_recovery_dialog, 2, 0);
    lv_obj_set_style_radius(main_recovery_dialog, 8, 0);
    lv_obj_set_style_shadow_width(main_recovery_dialog, 20, 0);
    lv_obj_set_style_shadow_color(main_recovery_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(main_recovery_dialog, LV_OPA_40, 0);
    lv_obj_remove_flag(main_recovery_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(main_recovery_dialog);
    
    // Title
    lv_obj_t *title = lv_label_create(main_recovery_dialog);
    lv_label_set_text(title, "Win Recovery");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Message
    lv_obj_t *msg = lv_label_create(main_recovery_dialog);
    lv_label_set_text(msg, "Reboot to Recovery Mode?");
    lv_obj_set_style_text_color(msg, lv_color_black(), 0);
    lv_obj_set_style_text_font(msg, UI_FONT, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);
    
    // Yes button
    lv_obj_t *yes_btn = lv_btn_create(main_recovery_dialog);
    lv_obj_set_size(yes_btn, 100, 40);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 30, -15);
    lv_obj_set_style_bg_color(yes_btn, lv_color_hex(0x0078D4), 0);
    lv_obj_set_style_radius(yes_btn, 4, 0);
    lv_obj_add_event_cb(yes_btn, main_recovery_yes_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_color(yes_label, lv_color_white(), 0);
    lv_obj_center(yes_label);
    
    // No button
    lv_obj_t *no_btn = lv_btn_create(main_recovery_dialog);
    lv_obj_set_size(no_btn, 100, 40);
    lv_obj_align(no_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -15);
    lv_obj_set_style_bg_color(no_btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(no_btn, 4, 0);
    lv_obj_add_event_cb(no_btn, main_recovery_no_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_color(no_label, lv_color_white(), 0);
    lv_obj_center(no_label);
}

// ============ SCREEN STATE MANAGEMENT ============

void win32_show_lock(void)
{
    if (scr_lock) {
        // Update user profile on lock screen
        if (lock_avatar_cont) {
            uint32_t avatar_color = settings_get_avatar_color();
            lv_obj_set_style_bg_color(lock_avatar_cont, lv_color_hex(avatar_color), 0);
            lv_obj_set_style_bg_grad_color(lock_avatar_cont, lv_color_hex(avatar_color > 0x202020 ? avatar_color - 0x202020 : 0), 0);
        }
        if (lock_avatar_letter) {
            const char *username_str = settings_get_username();
            char letter[2] = {username_str[0], '\0'};
            if (letter[0] >= 'a' && letter[0] <= 'z') letter[0] -= 32;
            lv_label_set_text(lock_avatar_letter, letter);
        }
        if (lock_username_label) {
            lv_label_set_text(lock_username_label, settings_get_username());
        }
        
        // Hide all lock type containers first
        if (lock_slide_container) lv_obj_add_flag(lock_slide_container, LV_OBJ_FLAG_HIDDEN);
        if (lock_pin_container) lv_obj_add_flag(lock_pin_container, LV_OBJ_FLAG_HIDDEN);
        if (lock_password_container) lv_obj_add_flag(lock_password_container, LV_OBJ_FLAG_HIDDEN);
        
        // Show appropriate container based on lock type
        lock_type_t lock_type = settings_get_lock_type();
        switch (lock_type) {
            case LOCK_TYPE_PIN:
                if (lock_pin_container) {
                    lv_obj_remove_flag(lock_pin_container, LV_OBJ_FLAG_HIDDEN);
                    // Reset PIN input
                    lock_pin_length = 0;
                    memset(lock_pin_buffer, 0, sizeof(lock_pin_buffer));
                    lock_update_pin_dots();
                    if (lock_pin_error_label) lv_label_set_text(lock_pin_error_label, "");
                }
                ESP_LOGI(TAG, "Lock screen: PIN mode");
                break;
            case LOCK_TYPE_PASSWORD:
                if (lock_password_container) {
                    lv_obj_remove_flag(lock_password_container, LV_OBJ_FLAG_HIDDEN);
                    // Reset password input
                    if (lock_password_textarea) lv_textarea_set_text(lock_password_textarea, "");
                    if (lock_password_error_label) lv_label_set_text(lock_password_error_label, "");
                }
                ESP_LOGI(TAG, "Lock screen: Password mode");
                break;
            case LOCK_TYPE_SLIDE:
            default:
                if (lock_slide_container) {
                    lv_obj_remove_flag(lock_slide_container, LV_OBJ_FLAG_HIDDEN);
                    // Reset slider position
                    if (lock_slider_handle) lv_obj_set_x(lock_slider_handle, 7);
                }
                ESP_LOGI(TAG, "Lock screen: Slide mode");
                break;
        }
        
        update_lock_time();
        lv_screen_load(scr_lock);
        current_screen_state = SCREEN_STATE_LOCK;
        hw_backlight_set(80);  // Normal brightness
        ESP_LOGI(TAG, "Showing lock screen");
    }
}

void win32_show_aod(void)
{
    if (scr_aod) {
        update_aod_time();
        lv_screen_load(scr_aod);
        current_screen_state = SCREEN_STATE_AOD;
        hw_backlight_set(10);  // Very dim for AOD
        ESP_LOGI(TAG, "Showing AOD");
    }
}

// win32_show_desktop() is defined earlier in the file (line ~837)

void win32_lock_device(void)
{
    // Close any open apps first
    if (app_window) {
        close_app_window();
    }
    
    // Hide start menu if visible
    if (start_menu_visible) {
        win32_hide_start_menu();
    }
    
    // Show AOD (screen "off" state)
    win32_show_aod();
}

bool win32_is_locked(void)
{
    return current_screen_state != SCREEN_STATE_DESKTOP;
}

// Handle power button press
void win32_power_button_pressed(void)
{
    switch (current_screen_state) {
        case SCREEN_STATE_DESKTOP:
            // Desktop -> AOD (lock device)
            win32_lock_device();
            break;
        case SCREEN_STATE_AOD:
            // AOD -> Lock screen
            win32_show_lock();
            break;
        case SCREEN_STATE_LOCK:
            // Lock -> AOD (turn off screen)
            win32_show_aod();
            break;
    }
}
