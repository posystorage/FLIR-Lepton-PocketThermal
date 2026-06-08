#include "ui_draw.h"
#include "mcu_font.h"
#include "ui_logo.h"
#include "lcd.h"
#include "color_palette.h"
#include "temp_measure.h"
#include <stddef.h>
#include <stdio.h>
#include <limits.h>


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

static uint8_t g_draw_orient = 1u;
static ui_layout_t g_layout = {
	{UI_LUT_X, UI_LUT_Y, UI_LUT_W, UI_LUT_H},
	{UI_IMAGE_X, UI_IMAGE_Y, UI_IMAGE_W, UI_IMAGE_H},
	{UI_PANEL_X, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H},
	UI_SCREEN_W,
	UI_SCREEN_H
};

#define UI_TEMP_SOURCE_W      80u
#define UI_TEMP_SOURCE_H      60u
#define UI_TEMP_SCALE          4u

void ui_draw_set_orientation(uint8_t orient)
{
	g_draw_orient = orient;
	if (orient == 0u) {
		g_layout.lut.x = 408;
		g_layout.lut.y = 0;
		g_layout.lut.w = 24;
		g_layout.lut.h = 240;
		g_layout.panel.x = 0;
		g_layout.panel.y = 0;
		g_layout.panel.w = 88;
		g_layout.panel.h = 240;
		g_layout.screen_w = UI_SCREEN_W;
		g_layout.screen_h = UI_SCREEN_H;
	} else if (orient == 2u) {
		g_layout.lut.x = 0;
		g_layout.lut.y = 0;
		g_layout.lut.w = 240;
		g_layout.lut.h = 24;
		g_layout.panel.x = 0;
		g_layout.panel.y = 344;
		g_layout.panel.w = 240;
		g_layout.panel.h = 88;
		g_layout.screen_w = UI_PORTRAIT_W;
		g_layout.screen_h = UI_PORTRAIT_H;
	} else if (orient == 3u) {
		g_layout.lut.x = 0;
		g_layout.lut.y = 408;
		g_layout.lut.w = 240;
		g_layout.lut.h = 24;
		g_layout.panel.x = 0;
		g_layout.panel.y = 0;
		g_layout.panel.w = 240;
		g_layout.panel.h = 88;
		g_layout.screen_w = UI_PORTRAIT_W;
		g_layout.screen_h = UI_PORTRAIT_H;
	} else {
		g_layout.lut.x = 0;
		g_layout.lut.y = 0;
		g_layout.lut.w = 24;
		g_layout.lut.h = 240;
		g_layout.panel.x = 344;
		g_layout.panel.y = 0;
		g_layout.panel.w = 88;
		g_layout.panel.h = 240;
		g_layout.screen_w = UI_SCREEN_W;
		g_layout.screen_h = UI_SCREEN_H;
	}
	g_layout.image.x = UI_IMAGE_X;
	g_layout.image.y = UI_IMAGE_Y;
	g_layout.image.w = UI_IMAGE_W;
	g_layout.image.h = UI_IMAGE_H;
	Show_MCU_Set_Orientation(orient);
}

uint8_t ui_draw_get_orientation(void)
{
	return g_draw_orient;
}

const ui_layout_t *ui_draw_get_layout(void)
{
	return &g_layout;
}

uint16_t ui_color_to_lcd(uint16_t rgb565)
{
	return (uint16_t)(__REV(rgb565) >> 16);
}

static void ui_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
	if (x >= g_layout.screen_w || y >= g_layout.screen_h) {
		return;
	}
	LCD_Begin_UI_Window(g_draw_orient, x, y, 1u, 1u);
	LCD_Write_DAT16(ui_color_to_lcd(color));
}

static void ui_draw_base_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
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

static void ui_draw_fill_rect_lcd(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t lcd_color)
{
	uint32_t count, i;

	if (w == 0u || h == 0u) {
		return;
	}
	LCD_Begin_UI_Window(g_draw_orient, x, y, w, h);
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

	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	x0 = r.x;
	y0 = r.y;
	x1 = (int16_t)(r.x + r.w);
	y1 = (int16_t)(r.y + r.h);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > (int16_t)g_layout.screen_w) x1 = (int16_t)g_layout.screen_w;
	if (y1 > (int16_t)g_layout.screen_h) y1 = (int16_t)g_layout.screen_h;
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	LCD_Begin_UI_Window(g_draw_orient, (uint16_t)x0, (uint16_t)y0,
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
			if (g_draw_orient == 0u) {
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
	if (g_draw_orient == 2u) {
		base_center_x = center_y;
		base_center_y = center_x;
	} else if (g_draw_orient == 3u) {
		base_center_x = (uint16_t)(UI_SCREEN_W - center_y);
		base_center_y = (uint16_t)(UI_SCREEN_H - center_x);
	} else {
		base_center_x = center_x;
		base_center_y = (uint16_t)(UI_SCREEN_H - center_y);
	}
	base_x = (uint16_t)(base_center_x - 10u);
	base_y = (uint16_t)(base_center_y - 8u);

	ui_draw_base_fill_rect(base_x, base_y, 20u, 16u, bg);
	ui_draw_base_fill_rect(base_x, (uint16_t)(base_y + 1u), 18u, 1u, fg);
	ui_draw_base_fill_rect(base_x, (uint16_t)(base_y + 14u), 18u, 1u, fg);
	ui_draw_base_fill_rect(base_x, (uint16_t)(base_y + 1u), 1u, 14u, fg);
	ui_draw_base_fill_rect((uint16_t)(base_x + 17u), (uint16_t)(base_y + 1u),
	                       1u, 14u, fg);
	ui_draw_base_fill_rect((uint16_t)(base_x + 18u), (uint16_t)(base_y + 6u),
	                       2u, 4u, fg);
	if (pct > 100u) {
		pct = 100u;
	}
	fill_w = (uint8_t)((uint16_t)pct * 14u / 100u);
	if (fill_w != 0u) {
		ui_draw_base_fill_rect((uint16_t)(base_x + 2u), (uint16_t)(base_y + 4u),
		                       fill_w, 8u, fg);
	}
	if (charging) {
		ui_draw_base_fill_rect((uint16_t)(base_x + 10u), (uint16_t)(base_y + 2u),
		                       1u, 5u, UI_COLOR_WARN);
		ui_draw_base_fill_rect((uint16_t)(base_x + 8u), (uint16_t)(base_y + 7u),
		                       4u, 1u, UI_COLOR_WARN);
		ui_draw_base_fill_rect((uint16_t)(base_x + 8u), (uint16_t)(base_y + 7u),
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

void ui_draw_temp_mark(uint16_t x, uint16_t y, uint8_t point_id, uint16_t color)
{
	if (point_id == (uint8_t)TEMP_POINT_MAX) {
		ui_draw_hline((uint16_t)(x + 2u), y, 5u, color);
		ui_draw_hline((uint16_t)(x + 1u), (uint16_t)(y + 1u), 7u, color);
		ui_draw_vline((uint16_t)(x + 4u), (uint16_t)(y + 2u), 6u, color);
	} else if (point_id == (uint8_t)TEMP_POINT_MIN) {
		ui_draw_vline((uint16_t)(x + 4u), y, 6u, color);
		ui_draw_hline((uint16_t)(x + 1u), (uint16_t)(y + 6u), 7u, color);
		ui_draw_hline((uint16_t)(x + 2u), (uint16_t)(y + 7u), 5u, color);
	} else if (point_id >= (uint8_t)TEMP_POINT_USER1) {
		ui_draw_hline((uint16_t)(x + 2u), y, 5u, color);
		ui_draw_hline((uint16_t)(x + 2u), (uint16_t)(y + 8u), 5u, color);
		ui_draw_vline(x, (uint16_t)(y + 2u), 5u, color);
		ui_draw_vline((uint16_t)(x + 8u), (uint16_t)(y + 2u), 5u, color);
	} else {
		ui_draw_hline((uint16_t)(x + 1u), (uint16_t)(y + 4u), 7u, color);
		ui_draw_vline((uint16_t)(x + 4u), (uint16_t)(y + 1u), 7u, color);
	}
}

static void ui_draw_format_lut_temp(char *buf, uint32_t len, int16_t t)
{
	int32_t v;
	char sign = '+';

	if (len == 0u) {
		return;
	}
	if (t == INT16_MIN) {
		(void)snprintf(buf, len, "--");
		return;
	}
	v = t;
	if (v < 0) {
		sign = '-';
		v = -v;
	}
	(void)snprintf(buf, len, "%c%ld", sign, (long)(v / 100));
}

void ui_draw_lut_body(void)
{
	const uint16_t *pal;
	uint16_t i;
	uint8_t v;
	const ui_rect_t *lut = &g_layout.lut;

	pal = palette_get_lcd565();
	ui_draw_fill_rect(*lut, UI_COLOR_BG);

	if (g_draw_orient == 2u || g_draw_orient == 3u) {
		uint16_t bar_x = (uint16_t)(lut->x + 34);
		uint16_t bar_y = (uint16_t)(lut->y + 6);
		uint16_t bar_w = (uint16_t)(lut->w - 68);
		uint16_t bar_h = 12u;

		for (i = 0u; i < bar_w; i++) {
			v = (uint8_t)((uint32_t)i * 255u / (bar_w - 1u));
			ui_draw_fill_rect_lcd((uint16_t)(bar_x + i), bar_y, 1u, bar_h, pal[v]);
		}
	} else {
		uint16_t bar_x = (uint16_t)(lut->x + 6);
		uint16_t bar_y = (uint16_t)(lut->y + 34);
		uint16_t bar_w = 12u;
		uint16_t bar_h = 172u;

		for (i = 0u; i < bar_h; i++) {
			v = (uint8_t)(255u - ((uint32_t)i * 255u / (bar_h - 1u)));
			ui_draw_fill_rect_lcd(bar_x, (uint16_t)(bar_y + i), bar_w, 1u, pal[v]);
		}
		ui_draw_text_middle((uint16_t)(lut->x + 12), (uint16_t)(lut->y + 0), "max",
		                    UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 12), (uint16_t)(lut->y + 208), "min",
		                    UI_COLOR_TEXT, UI_COLOR_BG);
	}
}

void ui_draw_lut_values(void)
{
	const temp_points_t *points;
	char high[8] = "--";
	char low[8] = "--";
	const ui_rect_t *lut = &g_layout.lut;

	points = temp_get_points();
	if (points != NULL) {
		ui_draw_format_lut_temp(high, sizeof(high),
		                        points->point[TEMP_POINT_MAX].temp_c_x100);
		ui_draw_format_lut_temp(low, sizeof(low),
		                        points->point[TEMP_POINT_MIN].temp_c_x100);
	}
	if (g_draw_orient == 2u || g_draw_orient == 3u) {
		ui_draw_fill_rect_xy((uint16_t)lut->x, (uint16_t)lut->y, 34u, 24u, UI_COLOR_BG);
		ui_draw_fill_rect_xy((uint16_t)(lut->x + lut->w - 34), (uint16_t)lut->y,
		                     34u, 24u, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 17), (uint16_t)lut->y, low,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + lut->w - 17), (uint16_t)lut->y, high,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
	} else {
		ui_draw_fill_rect_xy((uint16_t)lut->x, (uint16_t)(lut->y + 16u),
		                     24u, 18u, UI_COLOR_BG);
		ui_draw_fill_rect_xy((uint16_t)lut->x, (uint16_t)(lut->y + 224u),
		                     24u, 16u, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 12), (uint16_t)(lut->y + 16), high,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 12), (uint16_t)(lut->y + 224), low,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
	}
}

static uint8_t ui_map_temp_point_to_image(const temp_point_t *point,
                                           uint16_t *image_x, uint16_t *image_y)
{
	if (point == NULL || image_x == NULL || image_y == NULL ||
	    point->x >= UI_TEMP_SOURCE_W || point->y >= UI_TEMP_SOURCE_H) {
		return 0u;
	}

	/*
	 * The thermal frame is streamed top-to-bottom into a base LCD window whose
	 * GRAM Y direction is reversed. Map every measured source point through the
	 * same vertical row order before drawing it with a separate small window.
	 */
	*image_x = (uint16_t)(UI_IMAGE_X + ((uint16_t)point->x * UI_TEMP_SCALE) +
	                      (UI_TEMP_SCALE / 2u));
	*image_y = (uint16_t)(UI_IMAGE_Y + UI_IMAGE_H - 1u -
	                      (((uint16_t)point->y * UI_TEMP_SCALE) +
	                       (UI_TEMP_SCALE / 2u)));
	return 1u;
}

static void ui_draw_image_cross(uint16_t x, uint16_t y, uint16_t color)
{
	uint16_t image_right = (uint16_t)(UI_IMAGE_X + UI_IMAGE_W - 1u);
	uint16_t image_bottom = (uint16_t)(UI_IMAGE_Y + UI_IMAGE_H - 1u);
	uint16_t x0;
	uint16_t x1;
	uint16_t y0;
	uint16_t y1;

	if (x < UI_IMAGE_X || x > image_right || y < UI_IMAGE_Y || y > image_bottom) {
		return;
	}

	x0 = (x > (uint16_t)(UI_IMAGE_X + 1u)) ? (uint16_t)(x - 2u) : UI_IMAGE_X;
	x1 = (x < (uint16_t)(image_right - 1u)) ? (uint16_t)(x + 2u) : image_right;
	y0 = (y > (uint16_t)(UI_IMAGE_Y + 1u)) ? (uint16_t)(y - 2u) : UI_IMAGE_Y;
	y1 = (y < (uint16_t)(image_bottom - 1u)) ? (uint16_t)(y + 2u) : image_bottom;

	ui_draw_base_fill_rect(x0, y, (uint16_t)(x1 - x0 + 1u), 1u, color);
	ui_draw_base_fill_rect(x, y0, 1u, (uint16_t)(y1 - y0 + 1u), color);
}

void UI_DrawImageOverlay(void)
{
	const temp_points_t *points;
	uint32_t i;

	points = temp_get_points();
	if (points == NULL) {
		return;
	}
	for (i = 0u; i < (uint32_t)TEMP_POINT_COUNT; i++) {
		const temp_point_t *p = &points->point[i];
		uint16_t sx, sy, color;

		if (!p->enabled) {
			continue;
		}
		if (!ui_map_temp_point_to_image(p, &sx, &sy)) {
			continue;
		}
		color = UI_COLOR_TEXT;
		if (i == (uint32_t)TEMP_POINT_MAX) {
			color = UI_COLOR_WARN;
		} else if (i == (uint32_t)TEMP_POINT_MIN) {
			color = UI_COLOR_OK;
		} else if (i >= (uint32_t)TEMP_POINT_USER1) {
			color = UI_COLOR_ERR;
		}
		ui_draw_image_cross(sx, sy, color);
	}
}

void UI_DrawThermalFrameRgb565(uint8_t gray[240][320])
{
	const uint16_t *pal;
	uint32_t y, x;

	if (gray == NULL) {
		return;
	}
	LCD_String_Write_End();
	pal = palette_get_lcd565();
	LCD_Begin_Logical_Window(UI_IMAGE_X, UI_IMAGE_Y, UI_IMAGE_W, UI_IMAGE_H);
	for (y = 0u; y < UI_IMAGE_H; y++) {
		for (x = 0u; x < UI_IMAGE_W; x++) {
			LCD_Write_DAT16(pal[gray[y][x]]);
		}
	}
	UI_DrawImageOverlay();
}
