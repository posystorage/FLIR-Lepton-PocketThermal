#include "ui_body.h"
#include "ui_draw.h"
#include "ui_marker.h"
#include "sys_tick.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define UI_TEMP_REFRESH_MS 250u

static temp_points_t g_latest;
static temp_points_t g_displayed;
static uint8_t g_latest_valid;
static uint8_t g_display_valid;
static uint8_t g_latest_updated;
static uint32_t g_last_refresh_ms;
static char g_storage_text[24];
static uint16_t g_storage_color;

static const temp_point_id_t g_point_order[TEMP_POINT_COUNT] = {
	TEMP_POINT_MAX,
	TEMP_POINT_MIN,
	TEMP_POINT_CENTER,
	TEMP_POINT_USER1,
	TEMP_POINT_USER2,
};

static const char *point_label(temp_point_id_t id)
{
	switch (id) {
	case TEMP_POINT_MAX:
		return "最高温";
	case TEMP_POINT_MIN:
		return "最低温";
	case TEMP_POINT_USER1:
		return "用户1";
	case TEMP_POINT_USER2:
		return "用户2";
	case TEMP_POINT_CENTER:
	default:
		return "中心点";
	}
}

static void format_temp(char *buf, uint32_t len, int16_t temp)
{
	int32_t value;
	char sign = '+';

	if (temp == INT16_MIN) {
		(void)snprintf(buf, len, "--.-℃");
		return;
	}
	value = temp;
	if (value < 0) {
		sign = '-';
		value = -value;
	}
	(void)snprintf(buf, len, "%c%ld.%01ld℃", sign,
	               (long)(value / 100), (long)((value / 10) % 10));
}

void UI_BodyInit(void)
{
	memset(&g_latest, 0, sizeof(g_latest));
	memset(&g_displayed, 0, sizeof(g_displayed));
	g_latest_valid = 0u;
	g_display_valid = 0u;
	g_latest_updated = 0u;
	g_last_refresh_ms = GetTick();
	g_storage_text[0] = '\0';
	g_storage_color = UI_COLOR_TEXT;
}

void UI_BodyPublishTemperature(const temp_points_t *points)
{
	if (points == 0) {
		return;
	}
	memcpy(&g_latest, points, sizeof(g_latest));
	g_latest_valid = 1u;
	g_latest_updated = 1u;
}

uint8_t UI_BodyConsumeTemperature(uint32_t now_ms)
{
	uint8_t old_enabled = 0u;
	uint8_t new_enabled = 0u;
	uint32_t i;

	if ((uint32_t)(now_ms - g_last_refresh_ms) < UI_TEMP_REFRESH_MS) {
		return UI_BODY_UPDATE_NONE;
	}
	g_last_refresh_ms = now_ms;
	if (!g_latest_valid || !g_latest_updated) {
		return UI_BODY_UPDATE_NONE;
	}
	for (i = 0u; i < (uint32_t)TEMP_POINT_COUNT; i++) {
		if (g_displayed.point[i].enabled) {
			old_enabled |= (uint8_t)(1u << i);
		}
		if (g_latest.point[i].enabled) {
			new_enabled |= (uint8_t)(1u << i);
		}
	}
	memcpy(&g_displayed, &g_latest, sizeof(g_displayed));
	g_display_valid = 1u;
	g_latest_updated = 0u;
	return (old_enabled == new_enabled) ?
	       UI_BODY_UPDATE_VALUES : UI_BODY_UPDATE_FULL;
}

const temp_points_t *UI_BodyGetDisplayedTemperature(void)
{
	return g_display_valid ? &g_displayed : 0;
}

static uint8_t enabled_row(temp_point_id_t id)
{
	uint8_t row = 0u;
	uint32_t i;

	for (i = 0u; i < (uint32_t)TEMP_POINT_COUNT; i++) {
		temp_point_id_t current = g_point_order[i];
		if (!g_displayed.point[current].enabled) {
			continue;
		}
		if (current == id) {
			return row;
		}
		row++;
	}
	return 0xFFu;
}

static void draw_landscape_value(temp_point_id_t id)
{
	const ui_rect_t *region = &UI_LayoutGet()->temperature;
	char value[18];
	uint8_t row = enabled_row(id);
	uint16_t y;
	uint16_t width;
	uint16_t x;

	if (row == 0xFFu) {
		return;
	}
	y = (uint16_t)(region->y +
	    ((uint16_t)row * UI_TEMP_LINE_SPACING * 2u) +
	    UI_TEMP_LINE_SPACING);
	ui_draw_fill_rect_xy((uint16_t)region->x, y,
	                     (uint16_t)region->w, UI_TEMP_LINE_SPACING,
	                     UI_COLOR_BG);
	format_temp(value, sizeof(value), g_displayed.point[id].temp_c_x100);
	width = (uint16_t)(UI_MARKER_W + UI_MARKER_TEXT_GAP +
	                   ui_draw_text_width(value));
	x = (uint16_t)(region->x + ((uint16_t)region->w - width) / 2u);
	UI_MarkerDrawPanel(x, (uint16_t)(y + 3u), id);
	ui_draw_text((uint16_t)(x + UI_MARKER_W + UI_MARKER_TEXT_GAP), y,
	             value, UI_MarkerGetColor(id), UI_COLOR_BG);
}

static void draw_portrait_item(temp_point_id_t id)
{
	const ui_rect_t *region = &UI_LayoutGet()->temperature;
	char value[18];
	char label[18];
	uint8_t row = enabled_row(id);
	uint16_t y;
	uint16_t value_x;

	if (row == 0xFFu) {
		return;
	}
	y = (uint16_t)(region->y + ((uint16_t)row * UI_TEMP_LINE_SPACING));
	(void)snprintf(label, sizeof(label), "%s:", point_label(id));
	format_temp(value, sizeof(value), g_displayed.point[id].temp_c_x100);
	UI_MarkerDrawPanel((uint16_t)(region->x + 1), (uint16_t)(y + 3u), id);
	ui_draw_text((uint16_t)(region->x + UI_MARKER_W + UI_MARKER_TEXT_GAP),
	             y, label, UI_COLOR_TEXT, UI_COLOR_BG);
	value_x = (uint16_t)(region->x + UI_MARKER_W + UI_MARKER_TEXT_GAP +
	                     ui_draw_text_width(label));
	ui_draw_fill_rect_xy(value_x, y,
	                     (uint16_t)(region->x + region->w - value_x),
	                     UI_TEMP_LINE_SPACING, UI_COLOR_BG);
	ui_draw_text(value_x, y, value, UI_MarkerGetColor(id), UI_COLOR_BG);
}

void UI_BodyDrawTemperatureValues(void)
{
	uint32_t i;

	if (!g_display_valid) {
		return;
	}
	for (i = 0u; i < (uint32_t)TEMP_POINT_COUNT; i++) {
		temp_point_id_t id = g_point_order[i];
		if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
			draw_portrait_item(id);
		} else {
			draw_landscape_value(id);
		}
	}
}

static void draw_temperature_full(void)
{
	const ui_rect_t *region = &UI_LayoutGet()->temperature;
	uint32_t i;

	ui_draw_fill_rect(*region, UI_COLOR_BG);
	if (!g_display_valid) {
		return;
	}
	for (i = 0u; i < (uint32_t)TEMP_POINT_COUNT; i++) {
		temp_point_id_t id = g_point_order[i];
		uint8_t row = enabled_row(id);

		if (row == 0xFFu) {
			continue;
		}
		if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
			draw_portrait_item(id);
		} else {
			uint16_t y = (uint16_t)(region->y +
			             ((uint16_t)row * UI_TEMP_LINE_SPACING * 2u));
			ui_draw_text_middle((uint16_t)(region->x + region->w / 2),
			                    y, point_label(id),
			                    UI_COLOR_TEXT, UI_COLOR_BG);
			draw_landscape_value(id);
		}
	}
}

void UI_BodyDrawEmissivity(void)
{
	const ui_rect_t *region = &UI_LayoutGet()->emissivity;
	char text[16];
	uint16_t emiss = temp_get_emissivity();
	uint16_t y = (uint16_t)region->y;

	ui_draw_fill_rect(*region, UI_COLOR_BG);
	if (emiss >= 100u) {
		(void)snprintf(text, sizeof(text), "E=1.00");
	} else {
		(void)snprintf(text, sizeof(text), "E=0.%02u", (unsigned)emiss);
	}
	if (!UI_OrientationIsPortrait(UI_LayoutGetOrientation()) && y > 0u) {
		y--;
	}
	ui_draw_text_middle((uint16_t)(region->x + region->w / 2),
	                    y, text, UI_COLOR_TEXT, UI_COLOR_BG);
}

void UI_BodySetStorageText(const char *text, uint16_t color)
{
	uint32_t i = 0u;

	if (text != 0) {
		while (i < sizeof(g_storage_text) - 1u && text[i] != '\0') {
			g_storage_text[i] = text[i];
			i++;
		}
	}
	g_storage_text[i] = '\0';
	g_storage_color = color;
}

void UI_BodyDrawStorage(void)
{
	const ui_rect_t *region = &UI_LayoutGet()->storage;

	ui_draw_fill_rect(*region, UI_COLOR_BG);
	if (g_storage_text[0] != '\0') {
		ui_draw_text_middle((uint16_t)(region->x + region->w / 2),
		                    (uint16_t)region->y, g_storage_text,
		                    g_storage_color, UI_COLOR_BG);
	}
}

void UI_BodyDrawFull(void)
{
	const ui_layout_t *layout = UI_LayoutGet();

	ui_draw_fill_rect(UI_LayoutGet()->body, UI_COLOR_BG);
	draw_temperature_full();
	UI_BodyDrawEmissivity();
	UI_BodyDrawStorage();
	if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
		ui_draw_vline((uint16_t)(layout->temperature.x +
		                         layout->temperature.w),
		              (uint16_t)layout->panel.y,
		              (uint16_t)layout->panel.h, UI_COLOR_DIM);
	} else if (UI_LayoutGetOrientation() == UI_ORIENTATION_180) {
		ui_draw_hline((uint16_t)layout->panel.x,
		              UI_LANDSCAPE_180_EMISS_DIVIDER_Y,
		              (uint16_t)layout->panel.w, UI_COLOR_DIM);
	} else {
		ui_draw_hline((uint16_t)layout->panel.x,
		              UI_LANDSCAPE_0_EMISS_DIVIDER_Y,
		              (uint16_t)layout->panel.w, UI_COLOR_DIM);
	}
}
