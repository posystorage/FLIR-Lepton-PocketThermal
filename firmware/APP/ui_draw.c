#include "ui_draw.h"
#include "mcu_font.h"
#include "lcd.h"
#include <stddef.h>


extern lv_font_t flir_font_16;

static const uint16_t icon_sd_rows[16] = {
	0x3FE0,0x4010,0x8010,0x8010,0xBFF0,0xA010,0xA010,0xA010,
	0xA010,0xA010,0x8010,0x8010,0x8010,0x8010,0x8010,0xFFF0
};
static const uint16_t icon_usb_rows[16] = {
	0x0180,0x03C0,0x0180,0x0180,0x0DB0,0x1188,0x2184,0x4182,
	0x0180,0x0180,0x0180,0x0180,0x0180,0x03C0,0x0180,0x0000
};
static const uint16_t icon_camera_rows[8] = {0x3C,0x42,0x99,0xA5,0xA5,0x99,0x42,0x3C};
static const uint16_t icon_cancel_rows[8] = {0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81};
static const uint16_t icon_check_rows[8]  = {0x01,0x02,0x04,0x88,0x50,0x20,0x00,0x00};

const ui_bitmap_t ui_icon_sd      = {16u, 16u, icon_sd_rows};
const ui_bitmap_t ui_icon_usb     = {16u, 16u, icon_usb_rows};
const ui_bitmap_t ui_icon_camera  = {8u, 8u, icon_camera_rows};
const ui_bitmap_t ui_icon_cancel  = {8u, 8u, icon_cancel_rows};
const ui_bitmap_t ui_icon_check   = {8u, 8u, icon_check_rows};

static ui_orientation_t g_draw_orientation = UI_ORIENTATION_0;

void ui_draw_set_orientation(ui_orientation_t orientation)
{
	g_draw_orientation = orientation;
	UI_LayoutSetOrientation(orientation);
	Show_MCU_Set_Orientation(orientation);
}

ui_orientation_t ui_draw_get_orientation(void)
{
	return g_draw_orientation;
}

const ui_layout_t *ui_draw_get_layout(void)
{
	return UI_LayoutGet();
}

uint16_t ui_color_to_lcd(uint16_t rgb565)
{
	return (uint16_t)(__REV(rgb565) >> 16);
}

static void ui_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
	const ui_layout_t *layout = UI_LayoutGet();

	if (x >= layout->screen_w || y >= layout->screen_h) {
		return;
	}
	LCD_Begin_UI_Window(UI_OrientationToLcdCode(g_draw_orientation),
	                    x, y, 1u, 1u);
	LCD_Write_DAT16(ui_color_to_lcd(color));
}

void ui_draw_fixed_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t color)
{
	uint32_t count, i;
	uint16_t lcd_color;

	if (w == 0u || h == 0u) {
		return;
	}
	LCD_Begin_Logical_Window(x, y, w, h);
	lcd_color = ui_color_to_lcd(color);
	count = (uint32_t)w * (uint32_t)h;
	for (i = 0u; i < count; i++) {
		LCD_Write_DAT16(lcd_color);
	}
}

void ui_draw_fill_rect(ui_rect_t r, uint16_t color)
{
	int16_t x0, y0, x1, y1;
	uint32_t count, i;
	uint16_t lcd_color;
	const ui_layout_t *layout = UI_LayoutGet();

	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	x0 = r.x;
	y0 = r.y;
	x1 = (int16_t)(r.x + r.w);
	y1 = (int16_t)(r.y + r.h);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > (int16_t)layout->screen_w) x1 = (int16_t)layout->screen_w;
	if (y1 > (int16_t)layout->screen_h) y1 = (int16_t)layout->screen_h;
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	LCD_Begin_UI_Window(UI_OrientationToLcdCode(g_draw_orientation),
	                    (uint16_t)x0, (uint16_t)y0,
	                    (uint16_t)(x1 - x0), (uint16_t)(y1 - y0));
	lcd_color = ui_color_to_lcd(color);
	count = (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0);
	for (i = 0; i < count; i++) {
		LCD_Write_DAT16(lcd_color);
	}
}

void ui_draw_fill_rect_xy(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	ui_rect_t r;
	r.x = (int16_t)x;
	r.y = (int16_t)y;
	r.w = (int16_t)w;
	r.h = (int16_t)h;
	ui_draw_fill_rect(r, color);
}

void ui_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
	ui_draw_fill_rect_xy(x, y, w, 1u, color);
}

void ui_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
	ui_draw_fill_rect_xy(x, y, 1u, h, color);
}

void ui_draw_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
	if (s == NULL) {
		return;
	}
	Show_MCU_Set_Color(fg, bg);
	Show_MCU_Str(x, y, s, &flir_font_16);
}

void ui_draw_text_middle(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
	if (s == NULL) {
		return;
	}
	Show_MCU_Set_Color(fg, bg);
	Show_MCU_Str_Middle(x, y, s, &flir_font_16);
}

uint16_t ui_draw_text_width(const char *s)
{
	return (uint16_t)Show_Str_Get_Width(s, &flir_font_16);
}

void ui_draw_icon(uint16_t x, uint16_t y, const ui_bitmap_t *bmp, uint16_t fg, uint16_t bg)
{
	uint8_t row, col;
	uint16_t px, py;

	if (bmp == NULL || bmp->rows == NULL) {
		return;
	}
	ui_draw_fill_rect_xy(x, y, bmp->w, bmp->h, bg);
	for (row = 0u; row < bmp->h; row++) {
		for (col = 0u; col < bmp->w; col++) {
			if (bmp->rows[row] & (uint16_t)(1u << (bmp->w - 1u - col))) {
				px = (uint16_t)(x + col);
				py = (uint16_t)(y + row);
				ui_draw_pixel(px, py, fg);
			}
		}
	}
}

void ui_draw_bitmap_rot(uint16_t x, uint16_t y, const ui_bitmap_t *bmp, uint16_t fg, uint16_t bg)
{
	uint8_t row;
	uint8_t col;
	uint16_t px;
	uint16_t py;

	if (bmp == NULL || bmp->rows == NULL) {
		return;
	}
	ui_draw_fill_rect_xy(x, y, bmp->w, bmp->h, bg);
	for (row = 0u; row < bmp->h; row++) {
		for (col = 0u; col < bmp->w; col++) {
			if (!(bmp->rows[row] & (uint16_t)(1u << (bmp->w - 1u - col)))) {
				continue;
			}
			/*
			 * Portrait coordinates are rotated by LCD_Begin_UI_Window().
			 * Reverse landscape keeps the same window coordinates, so rotate
			 * the bitmap here to keep directional icons facing the user.
			 */
			if (g_draw_orientation == UI_ORIENTATION_180) {
				px = (uint16_t)(x + bmp->w - 1u - col);
				py = (uint16_t)(y + bmp->h - 1u - row);
			} else {
				px = (uint16_t)(x + col);
				py = (uint16_t)(y + row);
			}
			ui_draw_pixel(px, py, fg);
		}
	}
}

void ui_draw_battery_status(uint16_t x, uint16_t y, uint8_t pct, uint8_t charging,
                            uint16_t fg, uint16_t bg)
{
	uint8_t fill_w;
	uint16_t center_x = (uint16_t)(x + 10u);
	uint16_t center_y = (uint16_t)(y + 8u);
	uint16_t base_center_x;
	uint16_t base_center_y;
	uint16_t base_x;
	uint16_t base_y;

	/*
	 * Convert the logical slot center to the fixed landscape LCD coordinate
	 * system, then draw the battery there without applying UI rotation.
	 */
	if (g_draw_orientation == UI_ORIENTATION_90) {
		base_center_x = center_y;
		base_center_y = center_x;
	} else if (g_draw_orientation == UI_ORIENTATION_270) {
		base_center_x = (uint16_t)(UI_SCREEN_W - center_y);
		base_center_y = (uint16_t)(UI_SCREEN_H - center_x);
	} else {
		base_center_x = center_x;
		base_center_y = (uint16_t)(UI_SCREEN_H - center_y);
	}
	base_x = (uint16_t)(base_center_x - 10u);
	base_y = (uint16_t)(base_center_y - 8u);

	ui_draw_fixed_fill_rect(base_x, base_y, 20u, 16u, bg);
	ui_draw_fixed_fill_rect(base_x, (uint16_t)(base_y + 1u), 18u, 1u, fg);
	ui_draw_fixed_fill_rect(base_x, (uint16_t)(base_y + 14u), 18u, 1u, fg);
	ui_draw_fixed_fill_rect(base_x, (uint16_t)(base_y + 1u), 1u, 14u, fg);
	ui_draw_fixed_fill_rect((uint16_t)(base_x + 17u), (uint16_t)(base_y + 1u),
	                       1u, 14u, fg);
	ui_draw_fixed_fill_rect((uint16_t)(base_x + 18u), (uint16_t)(base_y + 6u),
	                       2u, 4u, fg);
	if (pct > 100u) {
		pct = 100u;
	}
	fill_w = (uint8_t)((uint16_t)pct * 14u / 100u);
	if (fill_w != 0u) {
		ui_draw_fixed_fill_rect((uint16_t)(base_x + 2u), (uint16_t)(base_y + 4u),
		                       fill_w, 8u, fg);
	}
	if (charging) {
		ui_draw_fixed_fill_rect((uint16_t)(base_x + 10u), (uint16_t)(base_y + 2u),
		                       1u, 5u, UI_COLOR_WARN);
		ui_draw_fixed_fill_rect((uint16_t)(base_x + 8u), (uint16_t)(base_y + 7u),
		                       4u, 1u, UI_COLOR_WARN);
		ui_draw_fixed_fill_rect((uint16_t)(base_x + 8u), (uint16_t)(base_y + 7u),
		                       1u, 6u, UI_COLOR_WARN);
	}
}

void ui_draw_sd_status(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
	ui_draw_bitmap_rot(x, y, &ui_icon_sd, fg, bg);
}

void ui_draw_usb_status(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
	ui_draw_bitmap_rot(x, y, &ui_icon_usb, fg, bg);
}
