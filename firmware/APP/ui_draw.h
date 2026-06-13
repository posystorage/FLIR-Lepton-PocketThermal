#ifndef _UI_DRAW_H_
#define _UI_DRAW_H_

#include <stdint.h>
#include "ui_icons.h"
#include "ui_layout.h"

#define UI_COLOR_BG        0x0841u
#define UI_COLOR_PANEL     0x1082u
#define UI_COLOR_TEXT      0xFFFFu
#define UI_COLOR_DIM       0x8410u
#define UI_COLOR_OK        0x07E0u
#define UI_COLOR_WARN      0xFFE0u
#define UI_COLOR_ERR       0xF800u
#define UI_COLOR_SELECT    0x39E7u

uint16_t ui_color_to_lcd(uint16_t rgb565);
void ui_draw_set_orientation(ui_orientation_t orientation);
ui_orientation_t ui_draw_get_orientation(void);
const ui_layout_t *ui_draw_get_layout(void);
void ui_draw_fill_rect(ui_rect_t r, uint16_t color);
void ui_draw_fill_rect_xy(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ui_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void ui_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void ui_draw_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
void ui_draw_text_middle(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
uint16_t ui_draw_text_width(const char *s);
void ui_draw_icon(uint16_t x, uint16_t y, const ui_bitmap_t *bmp, uint16_t fg, uint16_t bg);
void ui_draw_bitmap_rot(uint16_t x, uint16_t y, const ui_bitmap_t *bmp, uint16_t fg, uint16_t bg);
void ui_draw_battery_status(uint16_t x, uint16_t y, uint8_t pct, uint8_t charging,
                            uint16_t fg, uint16_t bg);
void ui_draw_sd_status(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg);
void ui_draw_usb_status(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg);
void ui_draw_fixed_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t color);

#endif
