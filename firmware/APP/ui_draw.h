#ifndef _UI_DRAW_H_
#define _UI_DRAW_H_

#include <stdint.h>
#include "ui_icons.h"

#define UI_SCREEN_W        432u
#define UI_SCREEN_H        240u
#define UI_PORTRAIT_W      240u
#define UI_PORTRAIT_H      432u
#define UI_LUT_X             0u
#define UI_LUT_Y             0u
#define UI_LUT_W            24u
#define UI_LUT_H           240u
#define UI_IMAGE_X          24u
#define UI_IMAGE_Y           0u
#define UI_IMAGE_W         320u
#define UI_IMAGE_H         240u
#define UI_PANEL_X         344u
#define UI_PANEL_Y           0u
#define UI_PANEL_W          88u
#define UI_PANEL_H         240u

#define UI_COLOR_BG        0x0841u
#define UI_COLOR_PANEL     0x1082u
#define UI_COLOR_TEXT      0xFFFFu
#define UI_COLOR_DIM       0x8410u
#define UI_COLOR_OK        0x07E0u
#define UI_COLOR_WARN      0xFFE0u
#define UI_COLOR_ERR       0xF800u
#define UI_COLOR_SELECT    0x39E7u

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} ui_rect_t;

typedef struct {
    ui_rect_t lut;
    ui_rect_t image;
    ui_rect_t panel;
    uint16_t screen_w;
    uint16_t screen_h;
} ui_layout_t;

uint16_t ui_color_to_lcd(uint16_t rgb565);
void ui_draw_set_orientation(uint8_t orient);
uint8_t ui_draw_get_orientation(void);
const ui_layout_t *ui_draw_get_layout(void);
void ui_draw_fill_rect(ui_rect_t r, uint16_t color);
void ui_draw_fill_rect_xy(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ui_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void ui_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void ui_draw_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
void ui_draw_text_middle(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
void ui_draw_icon(uint16_t x, uint16_t y, const ui_bitmap_t *bmp, uint16_t fg, uint16_t bg);
void ui_draw_bitmap_rot(uint16_t x, uint16_t y, const ui_bitmap_t *bmp, uint16_t fg, uint16_t bg);
void ui_draw_battery_status(uint16_t x, uint16_t y, uint8_t pct, uint8_t charging,
                            uint16_t fg, uint16_t bg);
void ui_draw_sd_status(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg);
void ui_draw_usb_status(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg);
void ui_draw_temp_mark(uint16_t x, uint16_t y, uint8_t point_id, uint16_t color);
void ui_draw_lut_body(void);
void ui_draw_lut_values(void);
void UI_DrawThermalFrameRgb565(uint8_t gray[240][320]);
void UI_DrawImageOverlay(void);

#endif
