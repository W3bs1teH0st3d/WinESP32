/**
 * Custom Fonts for Win32 OS
 * CodePro Variable with Cyrillic support
 */

#ifndef FONTS_H
#define FONTS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Declare custom font (defined in CodeProVariable.c)
LV_FONT_DECLARE(CodeProVariable);

// Main UI font - use this everywhere instead of lv_font_montserrat_14
#define UI_FONT_DEFAULT &CodeProVariable

#ifdef __cplusplus
}
#endif

#endif // FONTS_H
