#include "ui_status.h"
#include "ui_draw.h"
#include "power_manager.h"
#include "sdcard.h"
#include "usb_composite.h"

typedef struct {
	uint16_t sd_color;
	usb_ui_state_t usb_state;
	uint8_t battery_fill;
	uint8_t charging;
} ui_status_snapshot_t;

static ui_status_snapshot_t g_status_snapshot;
static uint8_t g_status_snapshot_valid;

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

static uint8_t battery_fill_bucket(void)
{
	uint8_t pct = Power_GetBatteryPercent();

	if (pct > 100u) {
		pct = 100u;
	}
	return (uint8_t)((uint16_t)pct * 14u / 100u);
}

static void capture_status(ui_status_snapshot_t *snapshot)
{
	snapshot->sd_color = sd_color();
	snapshot->usb_state = USB_Composite_GetUIState();
	snapshot->battery_fill = battery_fill_bucket();
	snapshot->charging = Power_IsCharging();
}

static uint8_t snapshots_equal(const ui_status_snapshot_t *a,
                               const ui_status_snapshot_t *b)
{
	return (a->sd_color == b->sd_color &&
	        a->usb_state == b->usb_state &&
	        a->battery_fill == b->battery_fill &&
	        a->charging == b->charging) ? 1u : 0u;
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

static uint16_t slot_center(int16_t base, uint16_t length, uint8_t slot, uint8_t count)
{
	uint16_t start;
	uint16_t end;

	start = (uint16_t)(((uint32_t)slot * length) / count);
	end = (uint16_t)(((uint32_t)(slot + 1u) * length) / count);
	return (uint16_t)(base + (int16_t)((start + end) / 2u));
}

static uint16_t slot_bitmap_origin(int16_t base, uint16_t length,
                                   uint8_t slot, uint8_t count,
                                   uint8_t bitmap_size)
{
	uint16_t center;
	uint16_t half;

	center = slot_center(base, length, slot, count);
	half = (uint16_t)(bitmap_size / 2u);
	return (center > half) ? (uint16_t)(center - half) : 0u;
}

static void draw_keybar_icon(const ui_rect_t *bar, uint8_t slot,
                             const ui_bitmap_t *icon)
{
	uint16_t x;
	uint16_t y;

	x = slot_bitmap_origin(bar->x, (uint16_t)bar->w, slot, 5u, icon->w);
	y = slot_bitmap_origin(bar->y, (uint16_t)bar->h, slot, 5u, icon->h);
	if (bar->w > bar->h) {
		y = slot_bitmap_origin(bar->y, (uint16_t)bar->h, 0u, 1u, icon->h);
	} else {
		x = slot_bitmap_origin(bar->x, (uint16_t)bar->w, 0u, 1u, icon->w);
	}
	ui_draw_bitmap_rot(x, y, icon, UI_COLOR_TEXT, UI_COLOR_PANEL);
}

static void draw_keybar_text(const ui_rect_t *bar, uint8_t slot, const char *text)
{
	uint16_t x;
	uint16_t y;

	x = slot_center(bar->x, (uint16_t)bar->w, slot, 5u);
	y = slot_center(bar->y, (uint16_t)bar->h, slot, 5u);
	if (bar->w > bar->h) {
		y = slot_center(bar->y, (uint16_t)bar->h, 0u, 1u);
	} else {
		x = slot_center(bar->x, (uint16_t)bar->w, 0u, 1u);
	}
	ui_draw_text_middle(x, (uint16_t)(y - 8u), text,
	                    UI_COLOR_TEXT, UI_COLOR_PANEL);
}

static void draw_keybar(void)
{
	const ui_layout_t *layout = UI_LayoutGet();
	const ui_rect_t *bar = &layout->keybar;
	uint8_t power_slot = slot_index(0u, 5u);
	uint8_t e_slot = slot_index(1u, 5u);
	uint8_t temp_slot = slot_index(2u, 5u);
	uint8_t l_slot = slot_index(3u, 5u);
	uint8_t cam_slot = slot_index(4u, 5u);

	ui_draw_fill_rect(*bar, UI_COLOR_PANEL);
	draw_keybar_icon(bar, power_slot, &ui_icon_power);
	draw_keybar_text(bar, e_slot, "E");
	draw_keybar_icon(bar, temp_slot, &ui_icon_temp_point);
	draw_keybar_text(bar, l_slot, "L");
	draw_keybar_icon(bar, cam_slot, &ui_icon_camera);
}

void UI_StatusDrawSystem(void)
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
	UI_StatusCapture();
	UI_StatusDrawDividers();
}

void UI_StatusDrawDividers(void)
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
	UI_StatusDrawSystem();
}

void UI_StatusCapture(void)
{
	capture_status(&g_status_snapshot);
	g_status_snapshot_valid = 1u;
}

uint8_t UI_StatusHasChanged(void)
{
	ui_status_snapshot_t current;

	capture_status(&current);
	if (!g_status_snapshot_valid ||
	    !snapshots_equal(&current, &g_status_snapshot)) {
		g_status_snapshot = current;
		g_status_snapshot_valid = 1u;
		return 1u;
	}
	return 0u;
}
