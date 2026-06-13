#include "ui_menu.h"
#include "ui_draw.h"
#include "ui_marker.h"
#include "temp_measure.h"
#include "color_palette.h"
#include "usb_uvc.h"
#include "agc.h"
#include <stdio.h>

typedef enum {
	UI_POINT_EDIT_ENABLE = 0,
	UI_POINT_EDIT_COLOR,
	UI_POINT_EDIT_X,
	UI_POINT_EDIT_Y,
} ui_point_edit_step_t;

static const uint16_t g_emiss_values[] = {
	100u, 98u, 95u, 93u, 90u, 85u, 80u, 75u, 70u, 60u, 50u
};

static const temp_point_id_t g_temp_ids[TEMP_POINT_COUNT] = {
	TEMP_POINT_CENTER,
	TEMP_POINT_MAX,
	TEMP_POINT_MIN,
	TEMP_POINT_USER1,
	TEMP_POINT_USER2,
};

static ui_menu_page_t g_page;
static uint8_t g_index;
static temp_point_id_t g_edit_point;
static ui_point_edit_step_t g_edit_step;

uint8_t UI_MenuEmissCount(void)
{
	return (uint8_t)(sizeof(g_emiss_values) / sizeof(g_emiss_values[0]));
}

uint16_t UI_MenuEmissAt(uint8_t index)
{
	if (index >= UI_MenuEmissCount()) {
		index = UI_MenuFindEmiss(95u);
	}
	return g_emiss_values[index];
}

uint8_t UI_MenuFindEmiss(uint16_t emiss_x100)
{
	uint8_t i;

	for (i = 0u; i < UI_MenuEmissCount(); i++) {
		if (g_emiss_values[i] == emiss_x100) {
			return i;
		}
	}
	return 2u;
}

void UI_MenuInit(void)
{
	g_page = UI_MENU_NONE;
	g_index = 0u;
	g_edit_point = TEMP_POINT_CENTER;
	g_edit_step = UI_POINT_EDIT_ENABLE;
}

void UI_MenuOpen(ui_menu_page_t page)
{
	g_page = page;
	if (page == UI_MENU_EMISSIVITY) {
		g_index = UI_MenuFindEmiss(temp_get_emissivity());
	} else if (page == UI_MENU_LUT) {
		g_index = (uint8_t)palette_get_current_id();
	} else {
		g_index = 0u;
	}
}

uint8_t UI_MenuIsActive(void)
{
	return (g_page != UI_MENU_NONE) ? 1u : 0u;
}

static uint8_t page_count(void)
{
	if (g_page == UI_MENU_EMISSIVITY) {
		return UI_MenuEmissCount();
	}
	if (g_page == UI_MENU_LUT) {
		return (uint8_t)PALETTE_ID_COUNT;
	}
	if (g_page == UI_MENU_TEMPERATURE) {
		return (uint8_t)TEMP_POINT_COUNT;
	}
	if (g_page == UI_MENU_POINT_EDIT) {
		return (g_edit_point == TEMP_POINT_USER1 ||
		        g_edit_point == TEMP_POINT_USER2) ? 4u : 2u;
	}
	return 0u;
}

static void move_index(uint8_t next)
{
	uint8_t count = page_count();

	if (count == 0u) {
		return;
	}
	if (next) {
		g_index = (uint8_t)((g_index + 1u) % count);
	} else {
		g_index = (g_index == 0u) ? (uint8_t)(count - 1u) :
		          (uint8_t)(g_index - 1u);
	}
}

static uint8_t wrap_coordinate(uint8_t value, uint8_t max, int8_t delta)
{
	int16_t next = (int16_t)value + delta;

	while (next < 0) {
		next = (int16_t)(next + max + 1);
	}
	while (next > max) {
		next = (int16_t)(next - max - 1);
	}
	return (uint8_t)next;
}

static void adjust_point(int8_t delta, uint8_t fast)
{
	uint8_t x;
	uint8_t y;
	uint8_t step = fast ? 5u : 1u;

	if (g_edit_step == UI_POINT_EDIT_ENABLE) {
		temp_set_point_enabled(g_edit_point,
		                       temp_get_point_enabled(g_edit_point) ? 0u : 1u);
	} else if (g_edit_step == UI_POINT_EDIT_COLOR) {
		uint8_t color = UI_MarkerGetColorIndex(g_edit_point);
		if (delta > 0) {
			color = (uint8_t)((color + 1u) % UI_MARKER_COLOR_COUNT);
		} else {
			color = (color == 0u) ? (UI_MARKER_COLOR_COUNT - 1u) :
			        (uint8_t)(color - 1u);
		}
		UI_MarkerSetColorIndex(g_edit_point, color);
	} else {
		temp_get_user_point(g_edit_point, &x, &y);
		if (g_edit_step == UI_POINT_EDIT_X) {
			x = wrap_coordinate(x, 79u, (int8_t)(delta * (int8_t)step));
		} else {
			y = wrap_coordinate(y, 59u, (int8_t)(delta * (int8_t)step));
		}
		temp_set_user_point(g_edit_point, x, y);
	}
}

uint8_t UI_MenuHandleKey(ui_key_t key, key_event_type_t event)
{
	uint8_t result = UI_MENU_RESULT_NONE;

	if (key == UI_KEY_CANCEL && event == KEY_EVENT_PRESS) {
		if (g_page == UI_MENU_POINT_EDIT) {
			g_page = UI_MENU_TEMPERATURE;
			g_index = (uint8_t)g_edit_point;
		} else {
			g_page = UI_MENU_NONE;
			result |= UI_MENU_RESULT_CLOSED;
		}
		return result;
	}
	if (g_page == UI_MENU_POINT_EDIT) {
		if ((key == UI_KEY_PREVIOUS || key == UI_KEY_NEXT) &&
		    (event == KEY_EVENT_PRESS || event == KEY_EVENT_LONG)) {
			adjust_point((key == UI_KEY_NEXT) ? 1 : -1,
			             (event == KEY_EVENT_LONG) ? 1u : 0u);
			result |= UI_MENU_RESULT_TEMP;
			if (g_edit_step == UI_POINT_EDIT_COLOR) {
				result |= UI_MENU_RESULT_COLOR;
			}
		} else if (key == UI_KEY_CONFIRM && event == KEY_EVENT_PRESS) {
			uint8_t count = page_count();
			g_edit_step = (ui_point_edit_step_t)
			              (((uint8_t)g_edit_step + 1u) % count);
			g_index = (uint8_t)g_edit_step;
		}
		return result;
	}
	if (event != KEY_EVENT_PRESS) {
		return result;
	}
	if (key == UI_KEY_PREVIOUS) {
		move_index(0u);
	} else if (key == UI_KEY_NEXT) {
		move_index(1u);
	} else if (key == UI_KEY_CONFIRM) {
		if (g_page == UI_MENU_EMISSIVITY) {
			temp_set_emissivity(UI_MenuEmissAt(g_index));
			g_page = UI_MENU_NONE;
			result |= UI_MENU_RESULT_CLOSED | UI_MENU_RESULT_EMISS;
		} else if (g_page == UI_MENU_LUT) {
			UVC_AbortFrame();
			agc_init();
			palette_init_by_id((palette_id_t)g_index);
			g_page = UI_MENU_NONE;
			result |= UI_MENU_RESULT_CLOSED | UI_MENU_RESULT_LUT;
		} else if (g_page == UI_MENU_TEMPERATURE) {
			g_edit_point = g_temp_ids[g_index];
			g_edit_step = UI_POINT_EDIT_ENABLE;
			g_index = 0u;
			g_page = UI_MENU_POINT_EDIT;
		}
	}
	return result;
}

static const char *temp_name(temp_point_id_t id)
{
	static const char *names[TEMP_POINT_COUNT] = {
		"CENTER", "MAX", "MIN", "USER1", "USER2"
	};
	return names[id];
}

static void draw_item(uint8_t item, const char *text, uint16_t color)
{
	const ui_rect_t *body = &UI_LayoutGet()->body;
	uint16_t x;
	uint16_t y;
	uint16_t width;

	if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
		uint16_t column_w = (uint16_t)(body->w / 2);
		uint8_t page_start = (uint8_t)((g_index / 6u) * 6u);
		uint8_t local;

		if (item < page_start || item >= (uint8_t)(page_start + 6u)) {
			return;
		}
		local = (uint8_t)(item - page_start);
		x = (uint16_t)(body->x + (local % 2u) * column_w);
		y = (uint16_t)(body->y + (local / 2u) * UI_TEMP_LINE_SPACING);
		width = column_w;
	} else {
		x = (uint16_t)body->x;
		y = (uint16_t)(body->y + item * 18u);
		width = (uint16_t)body->w;
	}
	ui_draw_fill_rect_xy(x, y, width, UI_TEMP_LINE_SPACING, UI_COLOR_BG);
	ui_draw_text((uint16_t)(x + 2u), y, text, color, UI_COLOR_BG);
}

void UI_MenuDraw(void)
{
	char text[28];
	uint8_t i;
	uint8_t count = page_count();
	const ui_rect_t *body = &UI_LayoutGet()->body;

	ui_draw_fill_rect(*body, UI_COLOR_BG);
	for (i = 0u; i < count; i++) {
		uint16_t color = (i == g_index) ? UI_COLOR_WARN : UI_COLOR_TEXT;

		if (g_page == UI_MENU_EMISSIVITY) {
			uint16_t value = UI_MenuEmissAt(i);
			if (value >= 100u) {
				(void)snprintf(text, sizeof(text), "%c 1.00%c",
				               (i == g_index) ? '>' : ' ',
				               (value == temp_get_emissivity()) ? '*' : ' ');
			} else {
				(void)snprintf(text, sizeof(text), "%c 0.%02u%c",
				               (i == g_index) ? '>' : ' ', (unsigned)value,
				               (value == temp_get_emissivity()) ? '*' : ' ');
			}
		} else if (g_page == UI_MENU_LUT) {
			(void)snprintf(text, sizeof(text), "%c%.10s%c",
			               (i == g_index) ? '>' : ' ',
			               palette_get_name((palette_id_t)i),
			               ((palette_id_t)i == palette_get_current_id()) ? '*' : ' ');
		} else if (g_page == UI_MENU_TEMPERATURE) {
			temp_point_id_t id = g_temp_ids[i];
			(void)snprintf(text, sizeof(text), "%c%s %s",
			               (i == g_index) ? '>' : ' ', temp_name(id),
			               temp_get_point_enabled(id) ? "ON" : "OFF");
		} else {
			uint8_t x = 0u;
			uint8_t y = 0u;
			temp_get_user_point(g_edit_point, &x, &y);
			if (i == UI_POINT_EDIT_ENABLE) {
				(void)snprintf(text, sizeof(text), "%c%s",
				               (i == g_index) ? '>' : ' ',
				               temp_get_point_enabled(g_edit_point) ? "ON" : "OFF");
			} else if (i == UI_POINT_EDIT_COLOR) {
				uint8_t color_index = UI_MarkerGetColorIndex(g_edit_point);
				(void)snprintf(text, sizeof(text), "%cCOLOR %s",
				               (i == g_index) ? '>' : ' ',
				               UI_MarkerColorName(color_index));
				color = UI_MarkerColorValue(color_index);
			} else if (i == UI_POINT_EDIT_X) {
				(void)snprintf(text, sizeof(text), "%cX %u",
				               (i == g_index) ? '>' : ' ', (unsigned)x);
			} else {
				(void)snprintf(text, sizeof(text), "%cY %u",
				               (i == g_index) ? '>' : ' ', (unsigned)y);
			}
		}
		draw_item(i, text, color);
	}
}
