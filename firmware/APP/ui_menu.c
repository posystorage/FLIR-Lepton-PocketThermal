#include "ui_menu.h"
#include "ui_draw.h"
#include "ui_marker.h"
#include "temp_measure.h"
#include "color_palette.h"
#include "usb_uvc.h"
#include "agc.h"
#include "power_manager.h"
#include <stdio.h>

typedef enum {
	UI_POINT_EDIT_ENABLE = 0,
	UI_POINT_EDIT_COLOR,
	UI_POINT_EDIT_X,
	UI_POINT_EDIT_Y,
} ui_point_edit_step_t;

#define UI_POINT_MOVE_FAST_STEP 3
#define UI_MENU_PORTRAIT_ROWS  5u
#define UI_MENU_PORTRAIT_COLS  2u

static const uint16_t g_emiss_values[] = {
	100u, 98u, 95u, 93u, 90u, 85u, 80u, 75u, 70u, 60u, 50u
};

static const uint16_t g_auto_off_values[] = {
	0u, 3u, 5u, 10u, 20u, 30u, 60u
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

static uint8_t auto_off_count(void)
{
	return (uint8_t)(sizeof(g_auto_off_values) /
	                 sizeof(g_auto_off_values[0]));
}

uint16_t UI_MenuAutoOffAt(uint8_t index)
{
	if (index >= auto_off_count()) {
		index = UI_MenuFindAutoOff(5u);
	}
	return g_auto_off_values[index];
}

uint8_t UI_MenuFindAutoOff(uint16_t minutes)
{
	uint8_t i;

	for (i = 0u; i < auto_off_count(); i++) {
		if (g_auto_off_values[i] == minutes) {
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
	} else if (page == UI_MENU_AUTO_OFF) {
		g_index = UI_MenuFindAutoOff(Power_GetAutoOffMinutes());
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
	if (g_page == UI_MENU_AUTO_OFF) {
		return auto_off_count();
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
	int8_t step = fast ? UI_POINT_MOVE_FAST_STEP : 1;
	int8_t move = (int8_t)(delta * step);

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
			x = wrap_coordinate(x, 79u, move);
		} else {
			y = wrap_coordinate(y, 59u, move);
		}
		temp_set_user_point(g_edit_point, x, y);
	}
}

static int16_t user_x_to_screen(uint8_t x)
{
	return (int16_t)(((int16_t)x - 40) * 4);
}

static int16_t user_y_to_screen(uint8_t y)
{
	return (int16_t)((30 - (int16_t)y) * 4);
}

uint8_t UI_MenuHandleKey(ui_key_t key, key_event_type_t event)
{
	uint8_t result = UI_MENU_RESULT_NONE;

	if (key == UI_KEY_CANCEL &&
	    (event == KEY_EVENT_PRESS || event == KEY_EVENT_LONG)) {
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
		    (event == KEY_EVENT_PRESS || event == KEY_EVENT_LONG ||
		     event == KEY_EVENT_REPEAT)) {
			if (event == KEY_EVENT_REPEAT &&
			    g_edit_step != UI_POINT_EDIT_X &&
			    g_edit_step != UI_POINT_EDIT_Y) {
				return result;
			}
			adjust_point((key == UI_KEY_NEXT) ? 1 : -1,
			             (event == KEY_EVENT_LONG ||
			              event == KEY_EVENT_REPEAT) ? 1u : 0u);
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
		} else if (g_page == UI_MENU_AUTO_OFF) {
			Power_SetAutoOffMinutes(UI_MenuAutoOffAt(g_index));
			g_page = UI_MENU_NONE;
			result |= UI_MENU_RESULT_CLOSED | UI_MENU_RESULT_POWER;
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

static const char *temp_menu_name(temp_point_id_t id)
{
	if (id == TEMP_POINT_CENTER) {
		return "CTR";
	}
	return temp_name(id);
}

static void draw_item(uint8_t item, const char *text, uint16_t color)
{
	const ui_rect_t *body = &UI_LayoutGet()->body;
	uint16_t x;
	uint16_t y;
	uint16_t width;

	if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
		uint16_t column_w = (uint16_t)(body->w / 2);
		uint8_t page_capacity =
			(uint8_t)(UI_MENU_PORTRAIT_ROWS * UI_MENU_PORTRAIT_COLS);
		uint8_t page_start = (uint8_t)((g_index / page_capacity) *
		                               page_capacity);
		uint8_t local;

		if (item < page_start ||
		    item >= (uint8_t)(page_start + page_capacity)) {
			return;
		}
		local = (uint8_t)(item - page_start);
		x = (uint16_t)(body->x +
		               (local / UI_MENU_PORTRAIT_ROWS) * column_w);
		y = (uint16_t)(body->y +
		               (local % UI_MENU_PORTRAIT_ROWS) *
		               UI_TEMP_LINE_SPACING);
		width = column_w;
	} else {
		x = (uint16_t)body->x;
		y = (uint16_t)(body->y + item * 18u);
		width = (uint16_t)body->w;
	}
	ui_draw_fill_rect_xy(x, y, width, UI_TEMP_LINE_SPACING, UI_COLOR_BG);
	ui_draw_text((uint16_t)(x + 2u), y, text, color, UI_COLOR_BG);
}

static void point_edit_item_rect(uint8_t item, uint16_t *x, uint16_t *y,
                                 uint16_t *width, uint16_t *height)
{
	const ui_rect_t *body = &UI_LayoutGet()->body;
	uint16_t row;

	if (UI_OrientationIsPortrait(UI_LayoutGetOrientation())) {
		*x = (uint16_t)body->x;
		*width = (uint16_t)body->w;
	} else {
		*x = (uint16_t)body->x;
		*width = (uint16_t)body->w;
	}
	row = item;
	if (item > UI_POINT_EDIT_COLOR) {
		row++;
	}
	*y = (uint16_t)(body->y + row * UI_TEMP_LINE_SPACING);
	*height = (item == UI_POINT_EDIT_COLOR) ?
	          (uint16_t)(UI_TEMP_LINE_SPACING * 2u) :
	          (uint16_t)UI_TEMP_LINE_SPACING;
}

static void draw_point_edit_item(uint8_t item)
{
	char text[28];
	uint16_t color = (item == g_index) ? UI_COLOR_WARN : UI_COLOR_TEXT;
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;

	point_edit_item_rect(item, &x, &y, &width, &height);
	ui_draw_fill_rect_xy(x, y, width, height, UI_COLOR_BG);
	if (item == UI_POINT_EDIT_ENABLE) {
		(void)snprintf(text, sizeof(text), "%c%s",
		               (item == g_index) ? '>' : ' ',
		               temp_get_point_enabled(g_edit_point) ? "ON" : "OFF");
		ui_draw_text((uint16_t)(x + 2u), y, text, color, UI_COLOR_BG);
	} else if (item == UI_POINT_EDIT_COLOR) {
		uint8_t color_index = UI_MarkerGetColorIndex(g_edit_point);
		(void)snprintf(text, sizeof(text), "%cCOLOR",
		               (item == g_index) ? '>' : ' ');
		ui_draw_text((uint16_t)(x + 2u), y, text, color, UI_COLOR_BG);
		(void)snprintf(text, sizeof(text), " %s",
		               UI_MarkerColorName(color_index));
		ui_draw_text((uint16_t)(x + 2u),
		             (uint16_t)(y + UI_TEMP_LINE_SPACING), text,
		             UI_MarkerColorValue(color_index), UI_COLOR_BG);
	} else {
		uint8_t raw_x = 0u;
		uint8_t raw_y = 0u;
		int16_t screen_value;

		temp_get_user_point(g_edit_point, &raw_x, &raw_y);
		screen_value = (item == UI_POINT_EDIT_X) ?
		               user_x_to_screen(raw_x) : user_y_to_screen(raw_y);
		(void)snprintf(text, sizeof(text), "%c%c %d",
		               (item == g_index) ? '>' : ' ',
		               (item == UI_POINT_EDIT_X) ? 'X' : 'Y',
		               (int)screen_value);
		ui_draw_text((uint16_t)(x + 2u), y, text, color, UI_COLOR_BG);
	}
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
		} else if (g_page == UI_MENU_AUTO_OFF) {
			uint16_t value = UI_MenuAutoOffAt(i);
			if (value == 0u) {
				(void)snprintf(text, sizeof(text), "%c NEVER%c",
				               (i == g_index) ? '>' : ' ',
				               (value == Power_GetAutoOffMinutes()) ? '*' : ' ');
			} else {
				(void)snprintf(text, sizeof(text), "%c %umin%c",
				               (i == g_index) ? '>' : ' ',
				               (unsigned)value,
				               (value == Power_GetAutoOffMinutes()) ? '*' : ' ');
			}
		} else if (g_page == UI_MENU_LUT) {
			(void)snprintf(text, sizeof(text), "%c%.10s%c",
			               (i == g_index) ? '>' : ' ',
			               palette_get_name((palette_id_t)i),
			               ((palette_id_t)i == palette_get_current_id()) ? '*' : ' ');
		} else if (g_page == UI_MENU_TEMPERATURE) {
			temp_point_id_t id = g_temp_ids[i];
			(void)snprintf(text, sizeof(text), "%c%s %s",
			               (i == g_index) ? '>' : ' ', temp_menu_name(id),
			               temp_get_point_enabled(id) ? "ON" : "OFF");
		} else {
			draw_point_edit_item(i);
			continue;
		}
		draw_item(i, text, color);
	}
}
