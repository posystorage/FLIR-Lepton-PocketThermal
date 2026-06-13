#include "ui_status.h"
#include "ui_draw.h"
#include "power_manager.h"
#include "sdcard.h"
#include "usb_composite.h"

static uint16_t sd_color(void)
{
	if (!SDCard_IsInserted()) {
		return UI_COLOR_DIM;
	}
	if (SDCard_IsOwnedByMSC()) {
		return UI_COLOR_WARN;
	}
	return SDCard_IsMounted() ? UI_COLOR_OK : UI_COLOR_ERR;
}

static uint16_t usb_color(void)
{
	usb_ui_state_t state = USB_Composite_GetUIState();

	if (state == USB_UI_DETACHED) {
		return UI_COLOR_DIM;
	}
	return (state == USB_UI_MSC_OWNING_SD) ? UI_COLOR_WARN : UI_COLOR_TEXT;
}

static uint8_t slot_index(uint8_t index, uint8_t count)
{
	ui_orientation_t orientation = UI_LayoutGetOrientation();

	if (orientation == UI_ORIENTATION_180 ||
	    orientation == UI_ORIENTATION_270) {
		return (uint8_t)(count - 1u - index);
	}
	return index;
}

static void draw_keybar(void)
{
	const ui_layout_t *layout = UI_LayoutGet();
	const ui_rect_t *bar = &layout->keybar;
	uint8_t e_slot = slot_index(0u, 4u);
	uint8_t t_slot = slot_index(1u, 4u);
	uint8_t l_slot = slot_index(2u, 4u);
	uint8_t cam_slot = slot_index(3u, 4u);
	uint16_t step;

	ui_draw_fill_rect(*bar, UI_COLOR_PANEL);
	if (layout->bars_vertical) {
		step = (uint16_t)(bar->h / 4);
		ui_draw_text((uint16_t)(bar->x + 2),
		             (uint16_t)(bar->y + step * e_slot + 3),
		             "E", UI_COLOR_TEXT, UI_COLOR_PANEL);
		ui_draw_text((uint16_t)(bar->x + 2),
		             (uint16_t)(bar->y + step * t_slot + 3),
		             "T", UI_COLOR_TEXT, UI_COLOR_PANEL);
		ui_draw_text((uint16_t)(bar->x + 2),
		             (uint16_t)(bar->y + step * l_slot + 3),
		             "L", UI_COLOR_TEXT, UI_COLOR_PANEL);
		ui_draw_bitmap_rot((uint16_t)(bar->x + 6),
		                   (uint16_t)(bar->y + step * cam_slot + 7),
		                   &ui_icon_camera, UI_COLOR_TEXT, UI_COLOR_PANEL);
	} else {
		step = (uint16_t)(bar->w / 4);
		ui_draw_text((uint16_t)(bar->x + step * e_slot + 4),
		             (uint16_t)(bar->y + 2),
		             "E", UI_COLOR_TEXT, UI_COLOR_PANEL);
		ui_draw_text((uint16_t)(bar->x + step * t_slot + 4),
		             (uint16_t)(bar->y + 2),
		             "T", UI_COLOR_TEXT, UI_COLOR_PANEL);
		ui_draw_text((uint16_t)(bar->x + step * l_slot + 4),
		             (uint16_t)(bar->y + 2),
		             "L", UI_COLOR_TEXT, UI_COLOR_PANEL);
		ui_draw_bitmap_rot((uint16_t)(bar->x + step * cam_slot + 4),
		                   (uint16_t)(bar->y + 2),
		                   &ui_icon_camera, UI_COLOR_TEXT, UI_COLOR_PANEL);
	}
}

static void draw_system_status(void)
{
	const ui_layout_t *layout = UI_LayoutGet();
	const ui_rect_t *bar = &layout->status;
	uint8_t sd_slot = slot_index(0u, 3u);
	uint8_t usb_slot = slot_index(1u, 3u);
	uint8_t bat_slot = slot_index(2u, 3u);

	ui_draw_fill_rect(*bar, UI_COLOR_BG);
	if (layout->bars_vertical) {
		ui_draw_sd_status((uint16_t)(bar->x + 2),
		                  (uint16_t)(bar->y + sd_slot * 30u + 4),
		                  sd_color(), UI_COLOR_BG);
		ui_draw_usb_status((uint16_t)(bar->x + 2),
		                   (uint16_t)(bar->y + usb_slot * 30u + 4),
		                   usb_color(), UI_COLOR_BG);
		ui_draw_battery_status((uint16_t)bar->x,
		                       (uint16_t)(bar->y + bat_slot * 30u + 6),
		                       Power_GetBatteryPercent(), Power_IsCharging(),
		                       UI_COLOR_TEXT, UI_COLOR_BG);
	} else {
		ui_draw_sd_status((uint16_t)(bar->x + sd_slot * 32u + 2),
		                  (uint16_t)(bar->y + 2),
		                  sd_color(), UI_COLOR_BG);
		ui_draw_usb_status((uint16_t)(bar->x + usb_slot * 32u + 2),
		                   (uint16_t)(bar->y + 2),
		                   usb_color(), UI_COLOR_BG);
		ui_draw_battery_status((uint16_t)(bar->x + bat_slot * 32u),
		                       (uint16_t)(bar->y + 2),
		                       Power_GetBatteryPercent(), Power_IsCharging(),
		                       UI_COLOR_TEXT, UI_COLOR_BG);
	}
}

static void draw_dividers(void)
{
	const ui_layout_t *layout = UI_LayoutGet();
	ui_orientation_t orientation = UI_LayoutGetOrientation();

	if (orientation == UI_ORIENTATION_0) {
		ui_draw_hline((uint16_t)layout->panel.x,
		              (uint16_t)(layout->keybar.y + layout->keybar.h - 1),
		              (uint16_t)layout->panel.w, UI_COLOR_DIM);
		ui_draw_hline((uint16_t)layout->panel.x,
		              (uint16_t)(layout->status.y + layout->status.h - 1),
		              (uint16_t)layout->panel.w, UI_COLOR_DIM);
	} else if (orientation == UI_ORIENTATION_180) {
		ui_draw_hline((uint16_t)layout->panel.x,
		              UI_LANDSCAPE_180_STORAGE_DIVIDER_Y,
		              (uint16_t)layout->panel.w, UI_COLOR_DIM);
		ui_draw_hline((uint16_t)layout->panel.x,
		              UI_LANDSCAPE_180_STATUS_DIVIDER_Y,
		              (uint16_t)layout->panel.w, UI_COLOR_DIM);
	} else {
		uint16_t body_status;
		uint16_t status_key;

		if (orientation == UI_ORIENTATION_90) {
			body_status = (uint16_t)layout->status.x;
			status_key = (uint16_t)layout->keybar.x;
		} else {
			status_key = (uint16_t)layout->status.x;
			body_status = (uint16_t)layout->body.x;
		}
		ui_draw_vline(body_status, (uint16_t)layout->panel.y,
		              (uint16_t)layout->panel.h, UI_COLOR_DIM);
		ui_draw_vline(status_key, (uint16_t)layout->panel.y,
		              (uint16_t)layout->panel.h, UI_COLOR_DIM);
	}
}

void UI_StatusDrawStatic(void)
{
	draw_keybar();
	draw_system_status();
	draw_dividers();
}
