#include "ui_thermal.h"
#include "ui_draw.h"
#include "ui_marker.h"
#include "color_palette.h"
#include "temp_measure.h"
#include "lcd.h"
#include <limits.h>
#include <stdio.h>

#define UI_TEMP_SOURCE_W 80u
#define UI_TEMP_SOURCE_H 60u
#define UI_TEMP_SCALE     4u

static void format_lut_temp(char *buf, uint32_t len, int16_t temp)
{
	int32_t value;
	char sign = '+';

	if (temp == INT16_MIN) {
		(void)snprintf(buf, len, "--");
		return;
	}
	value = temp;
	if (value < 0) {
		sign = '-';
		value = -value;
	}
	if (sign == '+' && value >= 10000) {
		(void)snprintf(buf, len, "%ld", (long)(value / 100));
	} else {
		(void)snprintf(buf, len, "%c%ld", sign, (long)(value / 100));
	}
}

void UI_ThermalDrawLutBody(void)
{
	const ui_layout_t *layout = UI_LayoutGet();
	const ui_rect_t *lut = &layout->lut;
	const uint16_t *palette = palette_get_lcd565();
	uint16_t i;
	uint16_t pixel;
	uint8_t value;

	ui_draw_fill_rect(*lut, UI_COLOR_BG);
	if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
		uint16_t bar_x = (uint16_t)(lut->x + 34);
		uint16_t bar_y = (uint16_t)(lut->y + 6);
		uint16_t bar_w = (uint16_t)(lut->w - 68);

		for (i = 0u; i < bar_w; i++) {
			value = (uint8_t)((uint32_t)i * 255u / (bar_w - 1u));
			LCD_Begin_UI_Window(
				UI_OrientationToLcdCode(UI_LayoutGetOrientation()),
				(uint16_t)(bar_x + i), bar_y, 1u, 12u);
			for (pixel = 0u; pixel < 12u; pixel++) {
				LCD_Write_DAT16(palette[value]);
			}
		}
	} else {
		uint16_t bar_x = (uint16_t)(lut->x + 6);
		uint16_t bar_y = (uint16_t)(lut->y + 34);

		for (i = 0u; i < 172u; i++) {
			value = (uint8_t)(255u - ((uint32_t)i * 255u / 171u));
			LCD_Begin_UI_Window(
				UI_OrientationToLcdCode(UI_LayoutGetOrientation()),
				bar_x, (uint16_t)(bar_y + i), 12u, 1u);
			for (pixel = 0u; pixel < 12u; pixel++) {
				LCD_Write_DAT16(palette[value]);
			}
		}
		ui_draw_text_middle((uint16_t)(lut->x + 12), (uint16_t)lut->y,
		                    "HI", UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 12),
		                    (uint16_t)(lut->y + 208), "LO",
		                    UI_COLOR_TEXT, UI_COLOR_BG);
	}
}

void UI_ThermalDrawLutValues(void)
{
	const temp_points_t *points = temp_get_points();
	const ui_rect_t *lut = &UI_LayoutGet()->lut;
	char high[8] = "--";
	char low[8] = "--";

	if (points != 0) {
		format_lut_temp(high, sizeof(high),
		                points->point[TEMP_POINT_MAX].temp_c_x100);
		format_lut_temp(low, sizeof(low),
		                points->point[TEMP_POINT_MIN].temp_c_x100);
	}
	if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
		ui_draw_fill_rect_xy((uint16_t)lut->x, (uint16_t)(lut->y + 3),
		                     34u, 21u, UI_COLOR_BG);
		ui_draw_fill_rect_xy((uint16_t)(lut->x + lut->w - 34),
		                     (uint16_t)(lut->y + 3), 34u, 21u, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 17),
		                    (uint16_t)(lut->y + 3), low,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + lut->w - 17),
		                    (uint16_t)(lut->y + 3), high,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
	} else {
		ui_draw_fill_rect_xy((uint16_t)lut->x, (uint16_t)(lut->y + 16),
		                     24u, 18u, UI_COLOR_BG);
		ui_draw_fill_rect_xy((uint16_t)lut->x, (uint16_t)(lut->y + 224),
		                     24u, 16u, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 12),
		                    (uint16_t)(lut->y + 16), high,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_text_middle((uint16_t)(lut->x + 12),
		                    (uint16_t)(lut->y + 224), low,
		                    UI_COLOR_TEXT, UI_COLOR_BG);
	}
}

static uint8_t map_point(const temp_point_t *point, uint16_t *x, uint16_t *y)
{
	if (point == 0 || point->x >= UI_TEMP_SOURCE_W ||
	    point->y >= UI_TEMP_SOURCE_H) {
		return 0u;
	}
	*x = (uint16_t)(UI_LUT_W + ((uint16_t)point->x * UI_TEMP_SCALE) + 2u);
	*y = (uint16_t)(UI_IMAGE_H - 1u -
	                (((uint16_t)point->y * UI_TEMP_SCALE) + 2u));
	if (*x < UI_LUT_W + 4u) {
		*x = UI_LUT_W + 4u;
	} else if (*x > UI_LUT_W + UI_IMAGE_W - 5u) {
		*x = UI_LUT_W + UI_IMAGE_W - 5u;
	}
	if (*y < 4u) {
		*y = 4u;
	} else if (*y > UI_IMAGE_H - 5u) {
		*y = UI_IMAGE_H - 5u;
	}
	return 1u;
}

void UI_ThermalDrawOverlay(void)
{
	const temp_points_t *points = temp_get_points();
	uint32_t i;

	if (points == 0) {
		return;
	}
	for (i = 0u; i < (uint32_t)TEMP_POINT_COUNT; i++) {
		uint16_t x;
		uint16_t y;
		const temp_point_t *point = &points->point[i];

		if (point->enabled && map_point(point, &x, &y)) {
			UI_MarkerDrawThermal(x, y, (temp_point_id_t)i);
		}
	}
}

void UI_ThermalDrawFrame(uint8_t gray[240][320])
{
	const uint16_t *palette;
	uint32_t y;
	uint32_t x;

	if (gray == 0) {
		return;
	}
	LCD_String_Write_End();
	palette = palette_get_lcd565();
	LCD_Begin_Logical_Window(UI_LUT_W, 0u, UI_IMAGE_W, UI_IMAGE_H);
	for (y = 0u; y < UI_IMAGE_H; y++) {
		for (x = 0u; x < UI_IMAGE_W; x++) {
			LCD_Write_DAT16(palette[gray[y][x]]);
		}
	}
	UI_ThermalDrawOverlay();
}
