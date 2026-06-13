#include "ui_layout.h"

static ui_orientation_t g_orientation = UI_ORIENTATION_0;
static ui_layout_t g_layout;

static void set_rect(ui_rect_t *r, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	r->x = (int16_t)x;
	r->y = (int16_t)y;
	r->w = (int16_t)w;
	r->h = (int16_t)h;
}

void UI_LayoutSetOrientation(ui_orientation_t orientation)
{
	uint16_t px;
	uint16_t py;

	g_orientation = orientation;
	set_rect(&g_layout.image, UI_LUT_W, 0u, UI_IMAGE_W, UI_IMAGE_H);
	g_layout.bars_vertical = UI_OrientationIsPortrait(orientation);

	if (orientation == UI_ORIENTATION_180) {
		set_rect(&g_layout.lut, UI_SCREEN_W - UI_LUT_W, 0u,
		         UI_LUT_W, UI_SCREEN_H);
		set_rect(&g_layout.panel, 0u, 0u,
		         UI_PANEL_LANDSCAPE_W, UI_PANEL_LANDSCAPE_H);
		g_layout.screen_w = UI_SCREEN_W;
		g_layout.screen_h = UI_SCREEN_H;
	} else if (orientation == UI_ORIENTATION_90) {
		set_rect(&g_layout.lut, 0u, 0u, UI_PORTRAIT_W, UI_LUT_W);
		set_rect(&g_layout.panel, 0u, UI_IMAGE_W + UI_LUT_W,
		         UI_PANEL_PORTRAIT_W, UI_PANEL_PORTRAIT_H);
		g_layout.screen_w = UI_PORTRAIT_W;
		g_layout.screen_h = UI_PORTRAIT_H;
	} else if (orientation == UI_ORIENTATION_270) {
		set_rect(&g_layout.lut, 0u, UI_IMAGE_W + UI_PANEL_PORTRAIT_H,
		         UI_PORTRAIT_W, UI_LUT_W);
		set_rect(&g_layout.panel, 0u, 0u,
		         UI_PANEL_PORTRAIT_W, UI_PANEL_PORTRAIT_H);
		g_layout.screen_w = UI_PORTRAIT_W;
		g_layout.screen_h = UI_PORTRAIT_H;
	} else {
		set_rect(&g_layout.lut, 0u, 0u, UI_LUT_W, UI_SCREEN_H);
		set_rect(&g_layout.panel, UI_LUT_W + UI_IMAGE_W, 0u,
		         UI_PANEL_LANDSCAPE_W, UI_PANEL_LANDSCAPE_H);
		g_layout.screen_w = UI_SCREEN_W;
		g_layout.screen_h = UI_SCREEN_H;
	}

	px = (uint16_t)g_layout.panel.x;
	py = (uint16_t)g_layout.panel.y;
	if (orientation == UI_ORIENTATION_180) {
		set_rect(&g_layout.temperature, px, py + UI_LANDSCAPE_180_TEMP_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_TEMP_H);
		set_rect(&g_layout.emissivity, px, py + UI_LANDSCAPE_180_EMISS_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_EMISS_H);
		set_rect(&g_layout.storage, px, py + UI_LANDSCAPE_180_STORAGE_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_STORAGE_H);
		set_rect(&g_layout.status, px, py + UI_LANDSCAPE_180_STATUS_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_BAR_H);
		set_rect(&g_layout.keybar, px, py + UI_LANDSCAPE_180_KEY_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_BAR_H);
		set_rect(&g_layout.body, px, py,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_BODY_H);
		set_rect(&g_layout.side, 0u, 0u, 0u, 0u);
	} else if (orientation == UI_ORIENTATION_0) {
		set_rect(&g_layout.keybar, px, py + UI_LANDSCAPE_0_KEY_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_BAR_H);
		set_rect(&g_layout.status, px, py + UI_LANDSCAPE_0_STATUS_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_BAR_H);
		set_rect(&g_layout.temperature, px, py + UI_LANDSCAPE_0_TEMP_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_TEMP_H);
		set_rect(&g_layout.emissivity, px, py + UI_LANDSCAPE_0_EMISS_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_EMISS_H);
		set_rect(&g_layout.storage, px, py + UI_LANDSCAPE_0_STORAGE_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_STORAGE_H);
		set_rect(&g_layout.body, px, py + UI_LANDSCAPE_0_TEMP_Y,
		         UI_PANEL_LANDSCAPE_W, UI_LANDSCAPE_BODY_H);
		set_rect(&g_layout.side, 0u, 0u, 0u, 0u);
	} else if (orientation == UI_ORIENTATION_90) {
		set_rect(&g_layout.body, px, py, UI_PORTRAIT_BODY_W,
		         UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.status, px + UI_PORTRAIT_BODY_W, py,
		         UI_PORTRAIT_BAR_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.keybar, px + UI_PORTRAIT_BODY_W + UI_PORTRAIT_BAR_W, py,
		         UI_PORTRAIT_BAR_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.temperature, px, py, UI_BODY_TEMP_COLUMN_W,
		         UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.side,
		         px + UI_BODY_TEMP_COLUMN_W + UI_BODY_COLUMN_DIVIDER_W, py,
		         UI_BODY_SIDE_COLUMN_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.emissivity,
		         px + UI_BODY_TEMP_COLUMN_W + UI_BODY_COLUMN_DIVIDER_W, py,
		         UI_BODY_SIDE_COLUMN_W, UI_TEMP_LINE_SPACING);
		set_rect(&g_layout.storage,
		         px + UI_BODY_TEMP_COLUMN_W + UI_BODY_COLUMN_DIVIDER_W,
		         py + UI_TEMP_LINE_SPACING,
		         UI_BODY_SIDE_COLUMN_W, UI_STORAGE_LINE_SPACING);
	} else {
		set_rect(&g_layout.keybar, px, py,
		         UI_PORTRAIT_BAR_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.status, px + UI_PORTRAIT_BAR_W, py,
		         UI_PORTRAIT_BAR_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.body, px + UI_PORTRAIT_BAR_W * 2u, py,
		         UI_PORTRAIT_BODY_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.temperature, px + UI_PORTRAIT_BAR_W * 2u, py,
		         UI_BODY_TEMP_COLUMN_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.side,
		         px + UI_PORTRAIT_BAR_W * 2u + UI_BODY_TEMP_COLUMN_W +
		         UI_BODY_COLUMN_DIVIDER_W, py,
		         UI_BODY_SIDE_COLUMN_W, UI_PANEL_PORTRAIT_H);
		set_rect(&g_layout.emissivity,
		         px + UI_PORTRAIT_BAR_W * 2u + UI_BODY_TEMP_COLUMN_W +
		         UI_BODY_COLUMN_DIVIDER_W, py,
		         UI_BODY_SIDE_COLUMN_W, UI_TEMP_LINE_SPACING);
		set_rect(&g_layout.storage,
		         px + UI_PORTRAIT_BAR_W * 2u + UI_BODY_TEMP_COLUMN_W +
		         UI_BODY_COLUMN_DIVIDER_W,
		         py + UI_TEMP_LINE_SPACING,
		         UI_BODY_SIDE_COLUMN_W, UI_STORAGE_LINE_SPACING);
	}
}

ui_orientation_t UI_LayoutGetOrientation(void)
{
	return g_orientation;
}

const ui_layout_t *UI_LayoutGet(void)
{
	return &g_layout;
}
