#include "ui_marker.h"
#include "ui_draw.h"

typedef struct {
	const char *name;
	uint16_t rgb565;
} ui_marker_color_t;

static const ui_marker_color_t g_colors[UI_MARKER_COLOR_COUNT] = {
	{"WHITE",  0xFFFFu},
	{"BLACK",  0x0000u},
	{"RED",    0xF800u},
	{"GREEN",  0x07E0u},
	{"BLUE",   0x001Fu},
	{"YELLOW", 0xFFE0u},
	{"LIME",   0xAFE5u},
	{"VIOLET", 0x801Fu},
	{"NAVY",   0x0010u},
	{"SCARLET",0xF980u},
};

static uint8_t g_point_color[TEMP_POINT_COUNT];

void UI_MarkerInit(void)
{
	g_point_color[TEMP_POINT_CENTER] = 0u;
	g_point_color[TEMP_POINT_MAX] = 5u;
	g_point_color[TEMP_POINT_MIN] = 3u;
	g_point_color[TEMP_POINT_USER1] = 2u;
	g_point_color[TEMP_POINT_USER2] = 4u;
}

uint16_t UI_MarkerGetColor(temp_point_id_t id)
{
	return UI_MarkerColorValue(UI_MarkerGetColorIndex(id));
}

uint8_t UI_MarkerGetColorIndex(temp_point_id_t id)
{
	if ((uint32_t)id >= (uint32_t)TEMP_POINT_COUNT) {
		return 0u;
	}
	return g_point_color[id];
}

void UI_MarkerSetColorIndex(temp_point_id_t id, uint8_t color_index)
{
	if ((uint32_t)id >= (uint32_t)TEMP_POINT_COUNT) {
		return;
	}
	g_point_color[id] = (uint8_t)(color_index % UI_MARKER_COLOR_COUNT);
}

const char *UI_MarkerColorName(uint8_t color_index)
{
	return g_colors[color_index % UI_MARKER_COLOR_COUNT].name;
}

uint16_t UI_MarkerColorValue(uint8_t color_index)
{
	return g_colors[color_index % UI_MARKER_COLOR_COUNT].rgb565;
}

static void draw_panel_cross(uint16_t x, uint16_t y, uint16_t color)
{
	ui_draw_hline((uint16_t)(x + 1u), (uint16_t)(y + 4u), 7u, color);
	ui_draw_vline((uint16_t)(x + 4u), (uint16_t)(y + 1u), 7u, color);
}

void UI_MarkerDrawPanel(uint16_t x, uint16_t y, temp_point_id_t id)
{
	uint16_t color = UI_MarkerGetColor(id);

	if (id == TEMP_POINT_MAX) {
		ui_draw_hline((uint16_t)(x + 1u), y, 7u, color);
		ui_draw_vline((uint16_t)(x + 4u), y, 8u, color);
	} else if (id == TEMP_POINT_MIN) {
		ui_draw_vline((uint16_t)(x + 4u), y, 8u, color);
		ui_draw_hline((uint16_t)(x + 1u), (uint16_t)(y + 7u), 7u, color);
	} else {
		draw_panel_cross(x, y, color);
	}
}

static void fixed_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
	ui_draw_fixed_fill_rect(x, y, w, 1u, color);
}

static void fixed_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
	ui_draw_fixed_fill_rect(x, y, 1u, h, color);
}

static void rotate_thermal_pixel(uint8_t x, uint8_t y, uint8_t *rx, uint8_t *ry)
{
	switch (UI_LayoutGetOrientation()) {
	case UI_ORIENTATION_0:
		*rx = (uint8_t)(8u - x);
		*ry = (uint8_t)(8u - y);
		break;
	case UI_ORIENTATION_90:
		*rx = y;
		*ry = (uint8_t)(8u - x);
		break;
	case UI_ORIENTATION_270:
		*rx = (uint8_t)(8u - y);
		*ry = x;
		break;
	case UI_ORIENTATION_180:
	default:
		*rx = x;
		*ry = y;
		break;
	}
}

static void draw_thermal_pixel(uint16_t x, uint16_t y,
                               uint8_t px, uint8_t py, uint16_t color)
{
	uint8_t rx;
	uint8_t ry;

	rotate_thermal_pixel(px, py, &rx, &ry);
	ui_draw_fixed_fill_rect((uint16_t)(x + rx), (uint16_t)(y + ry),
	                        1u, 1u, color);
}

static void draw_thermal_hline(uint16_t x, uint16_t y,
                               uint8_t px, uint8_t py, uint8_t w,
                               uint16_t color)
{
	uint8_t i;

	for (i = 0u; i < w; i++) {
		draw_thermal_pixel(x, y, (uint8_t)(px + i), py, color);
	}
}

static void draw_thermal_vline(uint16_t x, uint16_t y,
                               uint8_t px, uint8_t py, uint8_t h,
                               uint16_t color)
{
	uint8_t i;

	for (i = 0u; i < h; i++) {
		draw_thermal_pixel(x, y, px, (uint8_t)(py + i), color);
	}
}

void UI_MarkerDrawThermal(uint16_t center_x, uint16_t center_y,
                          temp_point_id_t id)
{
	uint16_t color = UI_MarkerGetColor(id);
	uint16_t x = (center_x >= 4u) ? (uint16_t)(center_x - 4u) : 0u;
	uint16_t y = (center_y >= 4u) ? (uint16_t)(center_y - 4u) : 0u;

	if (id == TEMP_POINT_MAX) {
		draw_thermal_hline(x, y, 1u, 0u, 7u, color);
		draw_thermal_vline(x, y, 4u, 0u, 8u, color);
	} else if (id == TEMP_POINT_MIN) {
		draw_thermal_vline(x, y, 4u, 0u, 8u, color);
		draw_thermal_hline(x, y, 1u, 7u, 7u, color);
	} else {
		fixed_hline((uint16_t)(x + 1u), (uint16_t)(y + 4u), 7u, color);
		fixed_vline((uint16_t)(x + 4u), (uint16_t)(y + 1u), 7u, color);
	}
}
