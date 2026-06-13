#ifndef _UI_LAYOUT_H_
#define _UI_LAYOUT_H_

#include <stdint.h>
#include "ui_orientation.h"

#define UI_SCREEN_W                    432u
#define UI_SCREEN_H                    240u
#define UI_PORTRAIT_W                  240u
#define UI_PORTRAIT_H                  432u

#define UI_LUT_W                        24u
#define UI_IMAGE_W                     320u
#define UI_IMAGE_H                     240u
#define UI_PANEL_LANDSCAPE_W            88u
#define UI_PANEL_LANDSCAPE_H           240u
#define UI_PANEL_PORTRAIT_W            240u
#define UI_PANEL_PORTRAIT_H             88u

#define UI_LANDSCAPE_BAR_H              20u
#define UI_LANDSCAPE_TEMP_H            170u
#define UI_LANDSCAPE_EMISS_H            13u
#define UI_LANDSCAPE_STORAGE_H          16u
#define UI_LANDSCAPE_BODY_H            200u
#define UI_TEMP_LINE_SPACING             17u
#define UI_EMISS_LINE_SPACING            12u
#define UI_STORAGE_LINE_SPACING          17u

#define UI_LANDSCAPE_0_KEY_Y              0u
#define UI_LANDSCAPE_0_STATUS_Y          20u
#define UI_LANDSCAPE_0_TEMP_Y            40u
#define UI_LANDSCAPE_0_EMISS_Y          210u
#define UI_LANDSCAPE_0_EMISS_DIVIDER_Y  223u
#define UI_LANDSCAPE_0_STORAGE_Y        224u

#define UI_LANDSCAPE_180_TEMP_Y           0u
#define UI_LANDSCAPE_180_EMISS_Y        170u
#define UI_LANDSCAPE_180_EMISS_DIVIDER_Y 183u
#define UI_LANDSCAPE_180_STORAGE_Y      184u
#define UI_LANDSCAPE_180_STORAGE_DIVIDER_Y 200u
#define UI_LANDSCAPE_180_STATUS_Y       200u
#define UI_LANDSCAPE_180_STATUS_DIVIDER_Y 220u
#define UI_LANDSCAPE_180_KEY_Y          220u

#define UI_PORTRAIT_BAR_W                20u
#define UI_PORTRAIT_BODY_W              200u
#define UI_BODY_TEMP_COLUMN_W           132u
#define UI_BODY_COLUMN_DIVIDER_W           1u
#define UI_BODY_SIDE_COLUMN_W            67u
#define UI_MARKER_W                       9u
#define UI_MARKER_TEXT_GAP                3u

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
	ui_rect_t keybar;
	ui_rect_t status;
	ui_rect_t body;
	ui_rect_t temperature;
	ui_rect_t side;
	ui_rect_t emissivity;
	ui_rect_t storage;
	uint16_t screen_w;
	uint16_t screen_h;
	uint8_t bars_vertical;
} ui_layout_t;

void UI_LayoutSetOrientation(ui_orientation_t orientation);
ui_orientation_t UI_LayoutGetOrientation(void);
const ui_layout_t *UI_LayoutGet(void);

#endif
