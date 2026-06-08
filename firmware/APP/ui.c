#include "ui.h"
#include "ui_draw.h"
#include "ui_menu.h"
#include "ui_logo.h"
#include "storage.h"
#include "temp_measure.h"
#include "color_palette.h"
#include "power_manager.h"
#include "sdcard.h"
#include "usb_composite.h"
#include "usb_uvc.h"
#include "sys_tick.h"
#include "agc.h"
#include <stdio.h>
#include <limits.h>

typedef enum {
	UI_STATE_BOOT = 0,
	UI_STATE_HOME,
	UI_STATE_MENU_EMISS,
	UI_STATE_MENU_TEMP,
	UI_STATE_MENU_LUT,
	UI_STATE_TEMP_EDIT_POINT,
	UI_STATE_STORAGE_BUSY,
	UI_STATE_TOAST,
} ui_state_t;

typedef enum {
	UI_KEY_POWER,
	UI_KEY_EMISS_UP_LEFT,
	UI_KEY_TEMP_DOWN_RIGHT,
	UI_KEY_LUT_CANCEL,
	UI_KEY_OK_SHUTTER,
	UI_KEY_UNKNOWN,
} ui_key_t;

typedef enum {
	UI_EDIT_ONOFF = 0,
	UI_EDIT_X,
	UI_EDIT_Y,
} ui_edit_step_t;

static ui_state_t g_state = UI_STATE_BOOT;
static ui_state_t g_return_state = UI_STATE_HOME;
static uint32_t g_dirty = UI_DIRTY_ALL;
static uint8_t g_orient = UI_ORIENT_LANDSCAPE_0;
static uint8_t g_menu_index = 0;
static uint8_t g_menu_prev_index = 0xFFu;
static uint8_t g_menu_redraw_all = 0;
static uint8_t g_menu_draw_index = 0;
static temp_point_id_t g_edit_point = TEMP_POINT_USER1;
static ui_edit_step_t g_edit_step = UI_EDIT_ONOFF;
static uint32_t g_last_poll_ms = 0;
static uint32_t g_toast_until_ms = 0;
static char g_toast[34];
static uint16_t g_toast_color = UI_COLOR_TEXT;
static uint8_t g_last_battery_pct = 0xFFu;
static uint8_t g_last_charging = 0xFFu;
static uint8_t g_last_sd_inserted = 0xFFu;
static uint8_t g_last_sd_mounted = 0xFFu;
static uint8_t g_last_sd_msc_owned = 0xFFu;
static usb_ui_state_t g_last_usb_state = (usb_ui_state_t)0xFFu;
static uint8_t g_full_clear_pending = 0u;

#define UI_DIRTY_HOME_PARAM (UI_DIRTY_TEMP | UI_DIRTY_EMISS | UI_DIRTY_TOAST)

#define UI_PORTRAIT_BAR_W          20u
#define UI_PORTRAIT_CONTENT_W     200u
#define UI_PORTRAIT_COLUMN_W      100u
#define UI_PORTRAIT_ROW_H          16u
#define UI_LANDSCAPE_KEYBAR_H      20u
#define UI_LANDSCAPE_STATUS_H      20u
#define UI_LANDSCAPE_BODY_H       200u
#define UI_LANDSCAPE_TEMP_H       160u
#define UI_LANDSCAPE_EMISS_H        40u

typedef struct {
	ui_rect_t keybar;
	ui_rect_t status;
	ui_rect_t body;
	ui_rect_t temp;
	ui_rect_t emiss;
	ui_rect_t menu;
	uint8_t keybar_vertical;
	uint8_t status_vertical;
	uint8_t content_portrait;
} ui_panel_regions_t;

#define UI_TXT_INSERT_SD  "请插卡"
#define UI_TXT_SAVING     "保存中"
#define UI_TXT_READING_SD "读卡中"
#define UI_TXT_WRITE_OK   "写入成功"
#define UI_TXT_WRITE_FAIL "写入失败"
#define UI_TXT_CANCEL     "取消"
#define UI_TXT_SD_ERR     "SD错误"
#define UI_TXT_ERROR      "错误"

const char Ascii_str []= " !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~";//do not delet

static ui_key_t ui_map_key(uint8_t key_id)
{
	switch (key_id) {
	case KEY1_ID:
		return UI_KEY_POWER;
	case KEY5_ID:
		return UI_KEY_EMISS_UP_LEFT;
	case KEY4_ID:
		return UI_KEY_TEMP_DOWN_RIGHT;
	case KEY3_ID:
		return UI_KEY_LUT_CANCEL;
	case KEY2_ID:
		return UI_KEY_OK_SHUTTER;
	default:
		return UI_KEY_UNKNOWN;
	}
}

static void ui_set_toast(const char *text, uint16_t color, uint32_t ms)
{
	uint32_t i;

	for (i = 0u; i < sizeof(g_toast) - 1u && text[i] != '\0'; i++) {
		g_toast[i] = text[i];
	}
	g_toast[i] = '\0';
	g_toast_color = color;
	g_toast_until_ms = GetTick() + ms;
	g_dirty |= UI_DIRTY_TOAST | UI_DIRTY_EMISS;
}

void UI_RequestRedraw(uint32_t dirty_flags)
{
	g_dirty |= dirty_flags;
}

static void ui_format_temp_value(char *buf, uint32_t len, int16_t t)
{
	int32_t v;
	char sign = '+';

	if (len == 0u) {
		return;
	}
	if (t == INT16_MIN) {
		(void)snprintf(buf, len, "--.-℃");
		return;
	}
	v = t;
	if (v < 0) {
		sign = '-';
		v = -v;
	}
	(void)snprintf(buf, len, "%c%ld.%01ld℃", sign,
	               (long)(v / 100), (long)((v / 10) % 10));
}

static uint8_t ui_is_portrait(void)
{
	return (g_orient == UI_ORIENT_PORTRAIT_90 || g_orient == UI_ORIENT_PORTRAIT_270) ? 1u : 0u;
}

static uint8_t ui_is_menu_state(void)
{
	return (g_state == UI_STATE_MENU_EMISS ||
	        g_state == UI_STATE_MENU_TEMP ||
	        g_state == UI_STATE_MENU_LUT ||
	        g_state == UI_STATE_TEMP_EDIT_POINT) ? 1u : 0u;
}

static void ui_format_emiss(char *buf, uint32_t len, const char *prefix)
{
	uint16_t emiss;

	if (len == 0u) {
		return;
	}
	emiss = temp_get_emissivity();
	if (emiss >= 100u) {
		(void)snprintf(buf, len, "%s1.00", prefix);
	} else {
		(void)snprintf(buf, len, "%s0.%02u", prefix, (unsigned)emiss);
	}
}

static uint16_t ui_point_color(temp_point_id_t id)
{
	if (id == TEMP_POINT_MAX) {
		return UI_COLOR_WARN;
	}
	if (id == TEMP_POINT_MIN) {
		return UI_COLOR_OK;
	}
	if (id == TEMP_POINT_USER1 || id == TEMP_POINT_USER2) {
		return UI_COLOR_ERR;
	}
	return UI_COLOR_TEXT;
}

static const char *ui_point_label(temp_point_id_t id)
{
	if (id == TEMP_POINT_MAX) {
		return "最高温";
	}
	if (id == TEMP_POINT_MIN) {
		return "最低温";
	}
	if (id == TEMP_POINT_USER1) {
		return "用户1";
	}
	if (id == TEMP_POINT_USER2) {
		return "用户2";
	}
	return "中心点";
}

static const char *ui_point_short_label(temp_point_id_t id)
{
	if (id == TEMP_POINT_MAX) {
		return "最高";
	}
	if (id == TEMP_POINT_MIN) {
		return "最低";
	}
	if (id == TEMP_POINT_USER1) {
		return "用户1";
	}
	if (id == TEMP_POINT_USER2) {
		return "用户2";
	}
	return "中心";
}

static uint16_t ui_sd_color(void)
{
	if (!SDCard_IsInserted()) {
		return UI_COLOR_DIM;
	}
	if (SDCard_IsOwnedByMSC()) {
		return UI_COLOR_WARN;
	}
	if (SDCard_IsMounted()) {
		return UI_COLOR_OK;
	}
	return UI_COLOR_ERR;
}

static uint16_t ui_usb_color(usb_ui_state_t usb)
{
	if (usb == USB_UI_DETACHED) {
		return UI_COLOR_DIM;
	}
	if (usb == USB_UI_MSC_OWNING_SD) {
		return UI_COLOR_WARN;
	}
	return UI_COLOR_TEXT;
}

static void ui_capture_status_snapshot(void)
{
	g_last_battery_pct = Power_GetBatteryPercent();
	g_last_charging = Power_IsCharging();
	g_last_sd_inserted = SDCard_IsInserted();
	g_last_sd_mounted = SDCard_IsMounted();
	g_last_sd_msc_owned = SDCard_IsOwnedByMSC();
	g_last_usb_state = USB_Composite_GetUIState();
}

static const ui_rect_t *ui_panel_rect(void)
{
	return &ui_draw_get_layout()->panel;
}

static void ui_set_region(ui_rect_t *r, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	r->x = (int16_t)x;
	r->y = (int16_t)y;
	r->w = (int16_t)w;
	r->h = (int16_t)h;
}

static void ui_panel_regions(ui_panel_regions_t *r)
{
	const ui_rect_t *p = ui_panel_rect();
	uint16_t px = (uint16_t)p->x;
	uint16_t py = (uint16_t)p->y;
	uint16_t pw = (uint16_t)p->w;
	uint16_t ph = (uint16_t)p->h;

	r->keybar_vertical = 0u;
	r->status_vertical = 0u;
	r->content_portrait = ui_is_portrait();

	if (g_orient == UI_ORIENT_LANDSCAPE_180) {
		ui_set_region(&r->temp, px, py, pw, UI_LANDSCAPE_TEMP_H);
		ui_set_region(&r->emiss, px, (uint16_t)(py + UI_LANDSCAPE_TEMP_H), pw, UI_LANDSCAPE_EMISS_H);
		ui_set_region(&r->keybar, px, (uint16_t)(py + UI_LANDSCAPE_BODY_H), pw, UI_LANDSCAPE_KEYBAR_H);
		ui_set_region(&r->status, px, (uint16_t)(py + 220u), pw, UI_LANDSCAPE_STATUS_H);
		ui_set_region(&r->body, px, py, pw, UI_LANDSCAPE_BODY_H);
		ui_set_region(&r->menu, px, py, pw, UI_LANDSCAPE_BODY_H);
	} else if (g_orient == UI_ORIENT_PORTRAIT_90) {
		ui_set_region(&r->temp, px, py, UI_PORTRAIT_CONTENT_W, ph);
		ui_set_region(&r->emiss, (uint16_t)(px + UI_PORTRAIT_COLUMN_W),
		              (uint16_t)(py + UI_PORTRAIT_ROW_H * 2u),
		              UI_PORTRAIT_COLUMN_W, UI_PORTRAIT_ROW_H);
		ui_set_region(&r->status, (uint16_t)(px + UI_PORTRAIT_CONTENT_W), py,
		              UI_PORTRAIT_BAR_W, ph);
		ui_set_region(&r->keybar, (uint16_t)(px + UI_PORTRAIT_CONTENT_W + UI_PORTRAIT_BAR_W),
		              py, UI_PORTRAIT_BAR_W, ph);
		ui_set_region(&r->body, px, py, UI_PORTRAIT_CONTENT_W, ph);
		ui_set_region(&r->menu, px, py, UI_PORTRAIT_CONTENT_W, ph);
		r->keybar_vertical = 1u;
		r->status_vertical = 1u;
	} else if (g_orient == UI_ORIENT_PORTRAIT_270) {
		ui_set_region(&r->keybar, px, py, UI_PORTRAIT_BAR_W, ph);
		ui_set_region(&r->status, (uint16_t)(px + UI_PORTRAIT_BAR_W), py,
		              UI_PORTRAIT_BAR_W, ph);
		ui_set_region(&r->temp, (uint16_t)(px + UI_PORTRAIT_BAR_W * 2u), py,
		              UI_PORTRAIT_CONTENT_W, ph);
		ui_set_region(&r->emiss, (uint16_t)(px + UI_PORTRAIT_BAR_W * 2u + UI_PORTRAIT_COLUMN_W),
		              (uint16_t)(py + UI_PORTRAIT_ROW_H * 2u),
		              UI_PORTRAIT_COLUMN_W, UI_PORTRAIT_ROW_H);
		ui_set_region(&r->body, (uint16_t)(px + UI_PORTRAIT_BAR_W * 2u), py,
		              UI_PORTRAIT_CONTENT_W, ph);
		ui_set_region(&r->menu, (uint16_t)(px + UI_PORTRAIT_BAR_W * 2u), py,
		              UI_PORTRAIT_CONTENT_W, ph);
		r->keybar_vertical = 1u;
		r->status_vertical = 1u;
	} else {
		ui_set_region(&r->keybar, px, py, pw, UI_LANDSCAPE_KEYBAR_H);
		ui_set_region(&r->status, px, (uint16_t)(py + UI_LANDSCAPE_KEYBAR_H), pw, UI_LANDSCAPE_STATUS_H);
		ui_set_region(&r->body, px, (uint16_t)(py + 40u), pw, UI_LANDSCAPE_BODY_H);
		ui_set_region(&r->temp, px, (uint16_t)(py + 40u), pw, UI_LANDSCAPE_TEMP_H);
		ui_set_region(&r->emiss, px, (uint16_t)(py + 200u), pw, UI_LANDSCAPE_EMISS_H);
		ui_set_region(&r->menu, px, (uint16_t)(py + 40u), pw, UI_LANDSCAPE_BODY_H);
	}
}

static void ui_draw_panel_dividers(void)
{
	ui_panel_regions_t rg;

	ui_panel_regions(&rg);
	if (g_orient == UI_ORIENT_LANDSCAPE_0) {
		ui_draw_hline((uint16_t)rg.status.x, (uint16_t)rg.status.y,
		              (uint16_t)rg.status.w, UI_COLOR_DIM);
		ui_draw_hline((uint16_t)rg.status.x,
		              (uint16_t)(rg.status.y + rg.status.h - 1u),
		              (uint16_t)rg.status.w, UI_COLOR_DIM);
	} else if (g_orient == UI_ORIENT_LANDSCAPE_180) {
		ui_draw_hline((uint16_t)rg.keybar.x, (uint16_t)rg.keybar.y,
		              (uint16_t)rg.keybar.w, UI_COLOR_DIM);
		ui_draw_hline((uint16_t)rg.keybar.x,
		              (uint16_t)(rg.keybar.y + rg.keybar.h - 1u),
		              (uint16_t)rg.keybar.w, UI_COLOR_DIM);
		ui_draw_hline((uint16_t)rg.status.x, (uint16_t)rg.status.y,
		              (uint16_t)rg.status.w, UI_COLOR_DIM);
	} else if (g_orient == UI_ORIENT_PORTRAIT_90) {
		ui_draw_vline((uint16_t)rg.status.x, (uint16_t)rg.status.y,
		              (uint16_t)rg.status.h, UI_COLOR_DIM);
		ui_draw_vline((uint16_t)(rg.status.x + rg.status.w - 1u),
		              (uint16_t)rg.status.y,
		              (uint16_t)rg.status.h, UI_COLOR_DIM);
	} else {
		ui_draw_vline((uint16_t)rg.status.x, (uint16_t)rg.status.y,
		              (uint16_t)rg.status.h, UI_COLOR_DIM);
		ui_draw_vline((uint16_t)(rg.status.x + rg.status.w - 1u),
		              (uint16_t)rg.status.y,
		              (uint16_t)rg.status.h, UI_COLOR_DIM);
	}
}

static void ui_clear_full_screen(void)
{
	const ui_layout_t *layout = ui_draw_get_layout();
	ui_draw_fill_rect_xy(0u, 0u, layout->screen_w, layout->screen_h, UI_COLOR_BG);
}

static void ui_draw_keybar(void)
{
	ui_panel_regions_t rg;
	uint16_t x0;
	uint16_t y;
	uint16_t step;
	uint16_t h;

	ui_panel_regions(&rg);
	x0 = (uint16_t)rg.keybar.x;
	y = (uint16_t)rg.keybar.y;
	h = (uint16_t)rg.keybar.h;
	step = rg.keybar_vertical ? (uint16_t)(h / 4u) : (uint16_t)(rg.keybar.w / 4);

	ui_draw_fill_rect_xy(x0, y, (uint16_t)rg.keybar.w, h, UI_COLOR_PANEL);
	if (g_state == UI_STATE_HOME) {
		if (rg.keybar_vertical) {
			ui_draw_text((uint16_t)(x0 + 2u), (uint16_t)(y + 3u), "E", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_text((uint16_t)(x0 + 2u), (uint16_t)(y + step + 3u), "T", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_text((uint16_t)(x0 + 2u), (uint16_t)(y + step * 2u + 3u), "L", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_bitmap_rot((uint16_t)(x0 + 6u), (uint16_t)(y + step * 3u + 7u),
			                   &ui_icon_camera, UI_COLOR_TEXT, UI_COLOR_PANEL);
		} else {
			ui_draw_text((uint16_t)(x0 + 4u), (uint16_t)(y + 2u), "E", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_text((uint16_t)(x0 + step + 4u), (uint16_t)(y + 2u), "T", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_text((uint16_t)(x0 + step * 2u + 4u), (uint16_t)(y + 2u), "L", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_bitmap_rot((uint16_t)(x0 + step * 3u + 4u), (uint16_t)(y + 2u),
			                   &ui_icon_camera, UI_COLOR_TEXT, UI_COLOR_PANEL);
		}
	} else if (g_state == UI_STATE_STORAGE_BUSY) {
		if (rg.keybar_vertical) {
			ui_draw_bitmap_rot((uint16_t)(x0 + 6u), (uint16_t)(y + step * 2u + 7u),
			                   &ui_icon_cancel, UI_COLOR_WARN, UI_COLOR_PANEL);
		} else {
			ui_draw_bitmap_rot((uint16_t)(x0 + step * 2u + 4u), (uint16_t)(y + 2u),
			                   &ui_icon_cancel, UI_COLOR_WARN, UI_COLOR_PANEL);
		}
	} else {
		if (rg.keybar_vertical) {
			ui_draw_text((uint16_t)(x0 + 2u), (uint16_t)(y + 3u), "<", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_text((uint16_t)(x0 + 2u), (uint16_t)(y + step + 3u), ">", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_bitmap_rot((uint16_t)(x0 + 6u), (uint16_t)(y + step * 2u + 7u),
			                   &ui_icon_cancel, UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_bitmap_rot((uint16_t)(x0 + 6u), (uint16_t)(y + step * 3u + 7u),
			                   &ui_icon_check, UI_COLOR_TEXT, UI_COLOR_PANEL);
		} else {
			ui_draw_text((uint16_t)(x0 + 4u), (uint16_t)(y + 2u), "<", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_text((uint16_t)(x0 + step + 4u), (uint16_t)(y + 2u), ">", UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_bitmap_rot((uint16_t)(x0 + step * 2u + 4u), (uint16_t)(y + 2u),
			                   &ui_icon_cancel, UI_COLOR_TEXT, UI_COLOR_PANEL);
			ui_draw_bitmap_rot((uint16_t)(x0 + step * 3u + 4u), (uint16_t)(y + 2u),
			                   &ui_icon_check, UI_COLOR_TEXT, UI_COLOR_PANEL);
		}
	}
	if (g_orient == UI_ORIENT_LANDSCAPE_180) {
		ui_draw_panel_dividers();
	}
}

static void ui_draw_sys_status(void)
{
	usb_ui_state_t usb;
	ui_panel_regions_t rg;
	uint16_t x = 0u;
	uint16_t y = 0u;

	ui_panel_regions(&rg);
	x = (uint16_t)rg.status.x;
	y = (uint16_t)rg.status.y;
	ui_draw_fill_rect_xy(x, y, (uint16_t)rg.status.w, (uint16_t)rg.status.h, UI_COLOR_BG);
	if (rg.status_vertical) {
		ui_draw_sd_status((uint16_t)(x + 2u), (uint16_t)(y + 4u),
		                  ui_sd_color(), UI_COLOR_BG);
		usb = USB_Composite_GetUIState();
		ui_draw_usb_status((uint16_t)(x + 2u), (uint16_t)(y + 34u),
		                   ui_usb_color(usb), UI_COLOR_BG);
		ui_draw_battery_status(x, (uint16_t)(y + 66u),
		                       Power_GetBatteryPercent(), Power_IsCharging(),
		                       UI_COLOR_TEXT, UI_COLOR_BG);
		ui_draw_panel_dividers();
		return;
	}
	ui_draw_sd_status((uint16_t)(x + 2u), (uint16_t)(y + 2u),
	                  ui_sd_color(), UI_COLOR_BG);
	usb = USB_Composite_GetUIState();
	ui_draw_usb_status((uint16_t)(x + 34u), (uint16_t)(y + 2u),
	                   ui_usb_color(usb),
	                   UI_COLOR_BG);
	ui_draw_battery_status((uint16_t)(x + 64u), (uint16_t)(y + 2u),
	                       Power_GetBatteryPercent(), Power_IsCharging(),
	                       UI_COLOR_TEXT, UI_COLOR_BG);
	ui_draw_panel_dividers();
}

static void ui_draw_portrait_temp_item(uint16_t x, uint16_t y,
                                       temp_point_id_t id,
                                       const temp_points_t *points)
{
	char temp[18];
	char line[32];

	ui_draw_fill_rect_xy(x, y, UI_PORTRAIT_COLUMN_W, UI_PORTRAIT_ROW_H, UI_COLOR_BG);
	if (points == NULL || !points->point[id].enabled) {
		return;
	}
	ui_format_temp_value(temp, sizeof(temp), points->point[id].temp_c_x100);
	(void)snprintf(line, sizeof(line), "%s%s", ui_point_short_label(id), temp);
	ui_draw_text((uint16_t)(x + 2u), y, line, ui_point_color(id), UI_COLOR_BG);
}

static void ui_draw_temp_status(void)
{
	const temp_points_t *points;
	char buf[18];
	ui_panel_regions_t rg;
	uint16_t y;
	uint8_t i;
	static const temp_point_id_t ids[5] = {
		TEMP_POINT_MAX,
		TEMP_POINT_MIN,
		TEMP_POINT_CENTER,
		TEMP_POINT_USER1,
		TEMP_POINT_USER2,
	};

	ui_panel_regions(&rg);
	y = (uint16_t)rg.temp.y;
	if (rg.content_portrait) {
		uint16_t left = (uint16_t)rg.temp.x;
		uint16_t right = (uint16_t)(rg.temp.x + UI_PORTRAIT_COLUMN_W);
		uint16_t top = (uint16_t)rg.temp.y;

		points = temp_get_points();
		ui_draw_portrait_temp_item(left, top, TEMP_POINT_MAX, points);
		ui_draw_portrait_temp_item(left, (uint16_t)(top + UI_PORTRAIT_ROW_H),
		                           TEMP_POINT_MIN, points);
		ui_draw_portrait_temp_item(left, (uint16_t)(top + UI_PORTRAIT_ROW_H * 2u),
		                           TEMP_POINT_CENTER, points);
		ui_draw_portrait_temp_item(right, top, TEMP_POINT_USER1, points);
		ui_draw_portrait_temp_item(right, (uint16_t)(top + UI_PORTRAIT_ROW_H),
		                           TEMP_POINT_USER2, points);
		return;
	}
	points = temp_get_points();
	ui_draw_fill_rect_xy((uint16_t)rg.temp.x, (uint16_t)rg.temp.y,
	                     (uint16_t)rg.temp.w, (uint16_t)rg.temp.h, UI_COLOR_BG);
	if (points != 0) {
		for (i = 0u; i < 5u; i++) {
			temp_point_id_t id = ids[i];
			uint16_t color = ui_point_color(id);

			if (!points->point[id].enabled) {
				continue;
			}
			ui_draw_text((uint16_t)(rg.temp.x + 2u), y, ui_point_label(id), UI_COLOR_TEXT, UI_COLOR_BG);
			ui_draw_temp_mark((uint16_t)(rg.temp.x + 2u), (uint16_t)(y + 18u), (uint8_t)id, color);
			ui_format_temp_value(buf, sizeof(buf), points->point[id].temp_c_x100);
			ui_draw_text((uint16_t)(rg.temp.x + 14u), (uint16_t)(y + 16u), buf, color, UI_COLOR_BG);
			y = (uint16_t)(y + 32u);
		}
	}
}

static void ui_draw_emiss_status(void)
{
	char buf[18];
	uint16_t fg = UI_COLOR_TEXT;
	ui_panel_regions_t rg;
	uint16_t x;
	uint16_t y;

	ui_panel_regions(&rg);
	x = (uint16_t)rg.emiss.x;
	y = (uint16_t)rg.emiss.y;
	if (g_state == UI_STATE_STORAGE_BUSY) {
		ui_draw_fill_rect_xy(x, y, (uint16_t)rg.emiss.w, (uint16_t)rg.emiss.h, UI_COLOR_BG);
		if (g_toast[0] != '\0' && (int32_t)(GetTick() - g_toast_until_ms) < 0) {
			ui_draw_text((uint16_t)(x + 2u), (uint16_t)(y + 2u), g_toast, g_toast_color, UI_COLOR_BG);
		} else {
			ui_draw_text((uint16_t)(x + 2u), (uint16_t)(y + 2u), UI_TXT_SAVING, UI_COLOR_WARN, UI_COLOR_BG);
		}
		if (!ui_is_portrait()) {
			ui_draw_text((uint16_t)(x + 2u), (uint16_t)(y + 18u), UI_TXT_CANCEL, UI_COLOR_TEXT, UI_COLOR_BG);
		}
		return;
	}
	if (rg.content_portrait) {
		ui_draw_fill_rect_xy(x, y, (uint16_t)rg.emiss.w,
		                     (uint16_t)rg.emiss.h, UI_COLOR_BG);
		if (g_toast[0] != '\0' && (int32_t)(GetTick() - g_toast_until_ms) < 0) {
			ui_draw_text((uint16_t)(x + 2u), y, g_toast, g_toast_color, UI_COLOR_BG);
		} else {
			ui_format_emiss(buf, sizeof(buf), "E=");
			ui_draw_text((uint16_t)(x + 2u), y, buf, UI_COLOR_TEXT, UI_COLOR_BG);
		}
		return;
	}
	ui_draw_fill_rect_xy(x, y, (uint16_t)rg.emiss.w, (uint16_t)rg.emiss.h, UI_COLOR_BG);
	ui_format_emiss(buf, sizeof(buf), "E=");
	ui_draw_text((uint16_t)(x + 2u), (uint16_t)(y + 1u), buf, UI_COLOR_TEXT, UI_COLOR_BG);
	if (g_toast[0] != '\0' && (int32_t)(GetTick() - g_toast_until_ms) < 0) {
		fg = g_toast_color;
		ui_draw_text((uint16_t)(x + 2u), (uint16_t)(y + 17u), g_toast, fg, UI_COLOR_BG);
	}
}

static void ui_draw_home_static(void)
{
	const ui_rect_t *p = ui_panel_rect();

	ui_draw_fill_rect(*p, UI_COLOR_BG);
	ui_draw_keybar();
	ui_draw_sys_status();
	ui_draw_temp_status();
	ui_draw_emiss_status();
}

static uint8_t ui_menu_count_current(void)
{
	if (g_state == UI_STATE_MENU_EMISS) {
		return UI_MenuEmissCount();
	}
	if (g_state == UI_STATE_MENU_LUT) {
		return (uint8_t)PALETTE_ID_COUNT;
	}
	if (g_state == UI_STATE_MENU_TEMP) {
		return 5u;
	}
	if (g_state == UI_STATE_TEMP_EDIT_POINT) {
		return 3u;
	}
	return 0u;
}

static uint8_t ui_menu_current_item_index(void)
{
	if (g_state == UI_STATE_TEMP_EDIT_POINT) {
		return (uint8_t)g_edit_step;
	}
	return g_menu_index;
}

static void ui_menu_request_all(void)
{
	g_menu_prev_index = 0xFFu;
	g_menu_redraw_all = 1u;
	g_menu_draw_index = 0u;
	g_dirty |= UI_DIRTY_MENU;
}

static temp_point_id_t ui_temp_id_by_menu(uint8_t idx)
{
	static const temp_point_id_t ids[5] = {
		TEMP_POINT_CENTER,
		TEMP_POINT_MAX,
		TEMP_POINT_MIN,
		TEMP_POINT_USER1,
		TEMP_POINT_USER2,
	};
	if (idx >= 5u) {
		idx = 0u;
	}
	return ids[idx];
}

static const char *ui_temp_name_by_menu(uint8_t idx)
{
	static const char *names[5] = {"CENTER", "MAX", "MIN", "P1", "P2"};
	if (idx >= 5u) {
		idx = 0u;
	}
	return names[idx];
}

static void ui_clear_menu_body(void)
{
	ui_panel_regions_t rg;

	ui_panel_regions(&rg);
	ui_draw_fill_rect_xy((uint16_t)rg.body.x, (uint16_t)rg.body.y,
	                     (uint16_t)rg.body.w, (uint16_t)rg.body.h, UI_COLOR_BG);
}

static void ui_draw_menu_item(uint8_t i)
{
	char buf[18];
	uint16_t y;
	uint16_t val;
	palette_id_t cur;
	temp_point_id_t id;
	uint8_t x = 0u, yy = 0u;
	ui_panel_regions_t rg;
	uint16_t item_x;
	uint16_t item_y;
	uint16_t item_w;
	uint16_t item_col_w;
	uint8_t page_start;
	uint8_t local_index;

	ui_panel_regions(&rg);
	item_col_w = rg.content_portrait ? (uint16_t)((uint16_t)rg.body.w / 2u) : (uint16_t)rg.body.w;
	page_start = (uint8_t)((ui_menu_current_item_index() / 6u) * 6u);
	local_index = (uint8_t)(i - page_start);

	if (g_state == UI_STATE_MENU_EMISS) {
		val = UI_MenuEmissAt(i);
		if (val >= 100u) {
			(void)snprintf(buf, sizeof(buf), "%c 1.00%c",
			               (i == g_menu_index) ? '>' : ' ',
			               (val == temp_get_emissivity()) ? '*' : ' ');
		} else {
			(void)snprintf(buf, sizeof(buf), "%c 0.%02u%c",
			               (i == g_menu_index) ? '>' : ' ',
			               (unsigned)val,
			               (val == temp_get_emissivity()) ? '*' : ' ');
		}
		if (ui_is_portrait()) {
			if (i < page_start || local_index >= 6u) {
				return;
			}
			item_x = (uint16_t)(rg.body.x + 2u + ((uint16_t)(local_index % 2u) * item_col_w));
			item_y = (uint16_t)(rg.body.y + ((uint16_t)(local_index / 2u) * 16u));
			item_w = (uint16_t)(item_col_w - 4u);
			ui_draw_fill_rect_xy(item_x, item_y, item_w, 16u, UI_COLOR_BG);
			ui_draw_text(item_x, item_y, buf,
			             (i == g_menu_index) ? UI_COLOR_WARN : UI_COLOR_TEXT,
			             UI_COLOR_BG);
		} else {
			y = (uint16_t)(rg.body.y + 2u + ((uint16_t)i * 18u));
			ui_draw_fill_rect_xy((uint16_t)rg.body.x, y, (uint16_t)rg.body.w, 18u, UI_COLOR_BG);
			ui_draw_text((uint16_t)(rg.body.x + 2u), y, buf,
			             (i == g_menu_index) ? UI_COLOR_WARN : UI_COLOR_TEXT,
			             UI_COLOR_BG);
		}
	} else if (g_state == UI_STATE_MENU_LUT) {
		cur = palette_get_current_id();
		(void)snprintf(buf, sizeof(buf), "%c%.9s%c",
		               (i == g_menu_index) ? '>' : ' ',
		               palette_get_name((palette_id_t)i),
		               ((palette_id_t)i == cur) ? '*' : ' ');
		if (ui_is_portrait()) {
			if (i < page_start || local_index >= 6u) {
				return;
			}
			item_x = (uint16_t)(rg.body.x + 2u + ((uint16_t)(local_index % 2u) * item_col_w));
			item_y = (uint16_t)(rg.body.y + ((uint16_t)(local_index / 2u) * 16u));
			item_w = (uint16_t)(item_col_w - 4u);
			ui_draw_fill_rect_xy(item_x, item_y, item_w, 16u, UI_COLOR_BG);
			ui_draw_text(item_x, item_y, buf,
			             (i == g_menu_index) ? UI_COLOR_WARN : UI_COLOR_TEXT, UI_COLOR_BG);
		} else {
			y = (uint16_t)(rg.body.y + 2u + ((uint16_t)i * 18u));
			ui_draw_fill_rect_xy((uint16_t)rg.body.x, y, (uint16_t)rg.body.w, 18u, UI_COLOR_BG);
			ui_draw_text((uint16_t)(rg.body.x + 2u), y, buf,
			             (i == g_menu_index) ? UI_COLOR_WARN : UI_COLOR_TEXT, UI_COLOR_BG);
		}
	} else if (g_state == UI_STATE_MENU_TEMP) {
		id = ui_temp_id_by_menu(i);
		(void)snprintf(buf, sizeof(buf), "%c%.6s %s",
		               (i == g_menu_index) ? '>' : ' ',
		               ui_temp_name_by_menu(i),
		               temp_get_point_enabled(id) ? "ON" : "OFF");
		if (ui_is_portrait()) {
			if (i < page_start || local_index >= 6u) {
				return;
			}
			item_x = (uint16_t)(rg.body.x + 2u + ((uint16_t)(local_index % 2u) * item_col_w));
			item_y = (uint16_t)(rg.body.y + ((uint16_t)(local_index / 2u) * 16u));
			item_w = (uint16_t)(item_col_w - 4u);
			ui_draw_fill_rect_xy(item_x, item_y, item_w, 16u, UI_COLOR_BG);
			ui_draw_text(item_x, item_y, buf,
			             (i == g_menu_index) ? UI_COLOR_WARN : UI_COLOR_TEXT, UI_COLOR_BG);
		} else {
			y = (uint16_t)(rg.body.y + 4u + ((uint16_t)i * 18u));
			ui_draw_fill_rect_xy((uint16_t)rg.body.x, y, (uint16_t)rg.body.w, 18u, UI_COLOR_BG);
			ui_draw_text((uint16_t)(rg.body.x + 2u), y, buf,
			             (i == g_menu_index) ? UI_COLOR_WARN : UI_COLOR_TEXT, UI_COLOR_BG);
		}
	} else if (g_state == UI_STATE_TEMP_EDIT_POINT) {
		temp_get_user_point(g_edit_point, &x, &yy);
		if (i == 0u) {
			(void)snprintf(buf, sizeof(buf), "ON %s", temp_get_point_enabled(g_edit_point) ? "YES" : "NO");
		} else if (i == 1u) {
			(void)snprintf(buf, sizeof(buf), "X %u", (unsigned)x);
		} else {
			(void)snprintf(buf, sizeof(buf), "Y %u", (unsigned)yy);
		}
		if (ui_is_portrait()) {
			if (i < page_start || local_index >= 6u) {
				return;
			}
			item_x = (uint16_t)(rg.body.x + 2u + ((uint16_t)(local_index % 2u) * item_col_w));
			item_y = (uint16_t)(rg.body.y + ((uint16_t)(local_index / 2u) * 16u));
			item_w = (uint16_t)(item_col_w - 4u);
			ui_draw_fill_rect_xy(item_x, item_y, item_w, 16u, UI_COLOR_BG);
			ui_draw_text(item_x, item_y, buf,
			             (i == (uint8_t)g_edit_step) ? UI_COLOR_WARN : UI_COLOR_TEXT,
			             UI_COLOR_BG);
		} else {
			y = (uint16_t)(rg.body.y + 8u + ((uint16_t)i * 18u));
			ui_draw_fill_rect_xy((uint16_t)rg.body.x, y, (uint16_t)rg.body.w, 18u, UI_COLOR_BG);
			ui_draw_text((uint16_t)(rg.body.x + 2u), y, buf,
			             (i == (uint8_t)g_edit_step) ? UI_COLOR_WARN : UI_COLOR_TEXT,
			             UI_COLOR_BG);
		}
	}
}

static uint8_t ui_draw_menu(void)
{
	uint8_t drawn = 0u;
	uint8_t count;

	count = ui_menu_count_current();
	if (count == 0u) {
		return 1u;
	}
	if (g_menu_redraw_all) {
		if (g_menu_draw_index == 0u) {
			ui_clear_menu_body();
		}
		while (g_menu_draw_index < count && drawn < 2u) {
			ui_draw_menu_item(g_menu_draw_index);
			g_menu_draw_index++;
			drawn++;
		}
		if (g_menu_draw_index >= count) {
			g_menu_redraw_all = 0u;
			return 1u;
		}
		return 0u;
	}
	if (g_menu_prev_index != 0xFFu && g_menu_prev_index < count) {
		ui_draw_menu_item(g_menu_prev_index);
	}
	if (ui_menu_current_item_index() < count) {
		ui_draw_menu_item(ui_menu_current_item_index());
	}
	g_menu_prev_index = 0xFFu;
	return 1u;
}

void UI_Init(void)
{
	g_state = UI_STATE_HOME;
	g_return_state = UI_STATE_HOME;
	g_orient = UI_ORIENT_LANDSCAPE_0;
	ui_draw_set_orientation(g_orient);
	g_dirty = UI_DIRTY_ALL;
	g_toast[0] = '\0';
	g_toast_until_ms = 0u;
	g_last_poll_ms = GetTick();
	temp_set_emissivity(95u);
	temp_set_ambient_c_x100(2500);
	ui_capture_status_snapshot();
	ui_clear_full_screen();
	ui_draw_lut_body();
	ui_draw_lut_values();
	ui_draw_home_static();
	g_dirty = 0u;
	g_full_clear_pending = 0u;
}

void UI_OnOrientationChanged(uint8_t orient)
{
	if (orient == g_orient) {
		return;
	}
	g_orient = orient;
	ui_draw_set_orientation(g_orient);
	g_full_clear_pending = 1u;
	g_dirty = UI_DIRTY_ALL;
}

static void ui_poll_periodic(void)
{
	uint32_t now = GetTick();

	if ((int32_t)(now - g_last_poll_ms) >= 500) {
		uint8_t battery_pct;
		uint8_t charging;
		uint8_t sd_inserted;
		uint8_t sd_mounted;
		uint8_t sd_msc_owned;
		usb_ui_state_t usb_state;

		g_last_poll_ms = now;
		battery_pct = Power_GetBatteryPercent();
		charging = Power_IsCharging();
		sd_inserted = SDCard_IsInserted();
		sd_mounted = SDCard_IsMounted();
		sd_msc_owned = SDCard_IsOwnedByMSC();
		usb_state = USB_Composite_GetUIState();
		if (battery_pct != g_last_battery_pct ||
		    charging != g_last_charging ||
		    sd_inserted != g_last_sd_inserted ||
		    sd_mounted != g_last_sd_mounted ||
		    sd_msc_owned != g_last_sd_msc_owned ||
		    usb_state != g_last_usb_state) {
			g_dirty |= UI_DIRTY_SYS_STATUS;
			g_last_battery_pct = battery_pct;
			g_last_charging = charging;
			g_last_sd_inserted = sd_inserted;
			g_last_sd_mounted = sd_mounted;
			g_last_sd_msc_owned = sd_msc_owned;
			g_last_usb_state = usb_state;
		}
	}
	if (g_toast[0] != '\0' && (int32_t)(now - g_toast_until_ms) >= 0) {
		g_toast[0] = '\0';
		if (!ui_is_menu_state()) {
			g_dirty |= UI_DIRTY_EMISS;
		}
	}
	if (g_state == UI_STATE_STORAGE_BUSY && !Storage_IsBusy()) {
		storage_result_t res = Storage_GetLastResult();
		g_state = UI_STATE_HOME;
		if (res == STORAGE_RESULT_OK) {
			ui_set_toast(UI_TXT_WRITE_OK, UI_COLOR_OK, 2500u);
		} else if (res == STORAGE_RESULT_CANCELLED) {
			ui_set_toast(UI_TXT_CANCEL, UI_COLOR_WARN, 2000u);
		} else {
			ui_set_toast(UI_TXT_WRITE_FAIL, UI_COLOR_ERR, 2500u);
		}
		g_dirty |= UI_DIRTY_KEYBAR | UI_DIRTY_SYS_STATUS |
		           UI_DIRTY_TEMP | UI_DIRTY_EMISS | UI_DIRTY_LUT_VALUES;
	}
}

void UI_Service(void)
{
	ui_poll_periodic();

	if (g_dirty != 0u) {
		if (g_full_clear_pending) {
			ui_clear_full_screen();
			g_full_clear_pending = 0u;
		}
		if (g_dirty == UI_DIRTY_ALL) {
			g_dirty = UI_DIRTY_LUT_BODY | UI_DIRTY_LUT_VALUES |
			          UI_DIRTY_KEYBAR | UI_DIRTY_SYS_STATUS;
			if (g_state == UI_STATE_HOME || g_state == UI_STATE_STORAGE_BUSY) {
				g_dirty |= UI_DIRTY_TEMP | UI_DIRTY_EMISS;
			} else {
				ui_menu_request_all();
			}
		}
		if (ui_is_menu_state()) {
			g_dirty &= ~UI_DIRTY_HOME_PARAM;
		}
		if (g_dirty & UI_DIRTY_LUT_BODY) {
			ui_draw_lut_body();
			g_dirty &= ~UI_DIRTY_LUT_BODY;
			return;
		}
		if (g_dirty & UI_DIRTY_LUT_VALUES) {
			ui_draw_lut_values();
			g_dirty &= ~UI_DIRTY_LUT_VALUES;
			return;
		}
		if (g_dirty & UI_DIRTY_KEYBAR) {
			ui_draw_keybar();
			g_dirty &= ~UI_DIRTY_KEYBAR;
			return;
		}
		if (g_dirty & UI_DIRTY_SYS_STATUS) {
			ui_draw_sys_status();
			g_dirty &= ~UI_DIRTY_SYS_STATUS;
			return;
		}
		if (g_dirty & UI_DIRTY_TEMP) {
			ui_draw_temp_status();
			g_dirty &= ~UI_DIRTY_TEMP;
			return;
		}
		if (g_dirty & UI_DIRTY_EMISS) {
			ui_draw_emiss_status();
			g_dirty &= ~UI_DIRTY_EMISS;
			return;
		}
		if (g_dirty & UI_DIRTY_MENU) {
			if (ui_draw_menu()) {
				g_dirty &= ~UI_DIRTY_MENU;
			}
			return;
		}
		if (g_dirty & UI_DIRTY_TOAST) {
			ui_draw_emiss_status();
			g_dirty &= ~UI_DIRTY_TOAST;
			return;
		}
	}
}

static void ui_enter_home(void)
{
	g_state = UI_STATE_HOME;
	g_menu_prev_index = 0xFFu;
	g_menu_redraw_all = 0u;
	g_dirty |= UI_DIRTY_KEYBAR | UI_DIRTY_SYS_STATUS |
	           UI_DIRTY_TEMP | UI_DIRTY_EMISS | UI_DIRTY_LUT_VALUES;
}

static void ui_menu_move(uint8_t count, uint8_t down)
{
	uint8_t old_index;

	if (count == 0u) {
		return;
	}
	old_index = g_menu_index;
	if (down) {
		g_menu_index = (uint8_t)((g_menu_index + 1u) % count);
	} else {
		g_menu_index = (g_menu_index == 0u) ? (uint8_t)(count - 1u) : (uint8_t)(g_menu_index - 1u);
	}
	if (ui_is_portrait() && (old_index / 6u) != (g_menu_index / 6u)) {
		ui_menu_request_all();
		return;
	}
	g_menu_prev_index = old_index;
	g_dirty |= UI_DIRTY_MENU;
}

static void ui_start_storage(void)
{
	storage_result_t res;

	res = Storage_BeginCapture();
	if (res == STORAGE_RESULT_BUSY) {
		g_state = UI_STATE_STORAGE_BUSY;
		ui_set_toast(UI_TXT_SAVING, UI_COLOR_WARN, 1000u);
	} else if (res == STORAGE_RESULT_NO_CARD) {
		ui_set_toast(UI_TXT_INSERT_SD, UI_COLOR_ERR, 2000u);
	} else if (res == STORAGE_RESULT_MSC_BUSY) {
		ui_set_toast(UI_TXT_READING_SD, UI_COLOR_WARN, 2000u);
	} else if (res == STORAGE_RESULT_NO_FRAME) {
		ui_set_toast(UI_TXT_ERROR, UI_COLOR_ERR, 2000u);
	} else {
		ui_set_toast(UI_TXT_SD_ERR, UI_COLOR_ERR, 2000u);
	}
	g_dirty |= UI_DIRTY_KEYBAR | UI_DIRTY_SYS_STATUS | UI_DIRTY_EMISS;
}

static void ui_handle_home(ui_key_t key, key_event_type_t ev)
{
	if (ev != KEY_EVENT_PRESS) {
		return;
	}
	if (key == UI_KEY_EMISS_UP_LEFT) {
		g_state = UI_STATE_MENU_EMISS;
		g_menu_index = UI_MenuFindEmiss(temp_get_emissivity());
		g_dirty |= UI_DIRTY_KEYBAR;
		ui_menu_request_all();
	} else if (key == UI_KEY_TEMP_DOWN_RIGHT) {
		g_state = UI_STATE_MENU_TEMP;
		g_menu_index = 0u;
		g_dirty |= UI_DIRTY_KEYBAR;
		ui_menu_request_all();
	} else if (key == UI_KEY_LUT_CANCEL) {
		g_state = UI_STATE_MENU_LUT;
		g_menu_index = (uint8_t)palette_get_current_id();
		g_dirty |= UI_DIRTY_KEYBAR;
		ui_menu_request_all();
	} else if (key == UI_KEY_OK_SHUTTER) {
		ui_start_storage();
	}
}

static void ui_handle_emiss(ui_key_t key, key_event_type_t ev)
{
	if (ev != KEY_EVENT_PRESS) {
		return;
	}
	if (key == UI_KEY_LUT_CANCEL) {
		ui_enter_home();
	} else if (key == UI_KEY_EMISS_UP_LEFT) {
		ui_menu_move(UI_MenuEmissCount(), 0u);
	} else if (key == UI_KEY_TEMP_DOWN_RIGHT) {
		ui_menu_move(UI_MenuEmissCount(), 1u);
	} else if (key == UI_KEY_OK_SHUTTER) {
		temp_set_emissivity(UI_MenuEmissAt(g_menu_index));
		ui_set_toast("EMISS OK", UI_COLOR_OK, 1200u);
		ui_enter_home();
	}
}

static void ui_handle_lut(ui_key_t key, key_event_type_t ev)
{
	if (ev != KEY_EVENT_PRESS) {
		return;
	}
	if (key == UI_KEY_LUT_CANCEL) {
		ui_enter_home();
	} else if (key == UI_KEY_EMISS_UP_LEFT) {
		ui_menu_move((uint8_t)PALETTE_ID_COUNT, 0u);
	} else if (key == UI_KEY_TEMP_DOWN_RIGHT) {
		ui_menu_move((uint8_t)PALETTE_ID_COUNT, 1u);
	} else if (key == UI_KEY_OK_SHUTTER) {
		UVC_AbortFrame();
		agc_init();
		palette_init_by_id((palette_id_t)g_menu_index);
		ui_set_toast(palette_get_name((palette_id_t)g_menu_index), UI_COLOR_OK, 1600u);
		g_dirty |= UI_DIRTY_LUT_BODY | UI_DIRTY_LUT_VALUES;
		ui_enter_home();
	}
}

static void ui_handle_temp_menu(ui_key_t key, key_event_type_t ev)
{
	temp_point_id_t id;

	if (ev != KEY_EVENT_PRESS) {
		return;
	}
	if (key == UI_KEY_LUT_CANCEL) {
		ui_enter_home();
	} else if (key == UI_KEY_EMISS_UP_LEFT) {
		ui_menu_move(5u, 0u);
	} else if (key == UI_KEY_TEMP_DOWN_RIGHT) {
		ui_menu_move(5u, 1u);
	} else if (key == UI_KEY_OK_SHUTTER) {
		id = ui_temp_id_by_menu(g_menu_index);
		if (id == TEMP_POINT_USER1 || id == TEMP_POINT_USER2) {
			g_edit_point = id;
			g_edit_step = UI_EDIT_ONOFF;
			g_state = UI_STATE_TEMP_EDIT_POINT;
			ui_menu_request_all();
		} else {
			temp_set_point_enabled(id, temp_get_point_enabled(id) ? 0u : 1u);
			g_menu_prev_index = g_menu_index;
			g_dirty |= UI_DIRTY_MENU;
		}
		g_dirty |= UI_DIRTY_TEMP;
	}
}

static uint8_t ui_wrap_coord(uint8_t value, uint8_t max, int8_t delta)
{
	int16_t next = (int16_t)value + (int16_t)delta;

	while (next < 0) {
		next = (int16_t)(next + max + 1);
	}
	while (next > (int16_t)max) {
		next = (int16_t)(next - max - 1);
	}
	return (uint8_t)next;
}

static void ui_handle_temp_edit(ui_key_t key, key_event_type_t ev)
{
	uint8_t x, y, step;
	int8_t delta = 0;

	temp_get_user_point(g_edit_point, &x, &y);
	if (key == UI_KEY_LUT_CANCEL && ev == KEY_EVENT_PRESS) {
		g_state = UI_STATE_MENU_TEMP;
		ui_menu_request_all();
		return;
	}
	if (key == UI_KEY_EMISS_UP_LEFT && (ev == KEY_EVENT_PRESS || ev == KEY_EVENT_LONG)) {
		step = (ev == KEY_EVENT_LONG) ? 5u : 1u;
		delta = -(int8_t)step;
	} else if (key == UI_KEY_TEMP_DOWN_RIGHT && (ev == KEY_EVENT_PRESS || ev == KEY_EVENT_LONG)) {
		step = (ev == KEY_EVENT_LONG) ? 5u : 1u;
		delta = (int8_t)step;
	}
	if (delta != 0) {
		if (g_edit_step == UI_EDIT_ONOFF) {
			temp_set_point_enabled(g_edit_point, temp_get_point_enabled(g_edit_point) ? 0u : 1u);
		} else if (g_edit_step == UI_EDIT_X) {
			x = ui_wrap_coord(x, 79u, delta);
			temp_set_user_point(g_edit_point, x, y);
		} else if (g_edit_step == UI_EDIT_Y) {
			y = ui_wrap_coord(y, 59u, delta);
			temp_set_user_point(g_edit_point, x, y);
		}
		g_menu_prev_index = (uint8_t)g_edit_step;
		g_dirty |= UI_DIRTY_MENU | UI_DIRTY_TEMP;
		return;
	}
	if (key == UI_KEY_OK_SHUTTER && ev == KEY_EVENT_PRESS) {
		ui_edit_step_t old_step = g_edit_step;
		if (g_edit_step == UI_EDIT_ONOFF) {
			if (!temp_get_point_enabled(g_edit_point)) {
				g_state = UI_STATE_MENU_TEMP;
				ui_menu_request_all();
			} else {
				g_edit_step = UI_EDIT_X;
				g_menu_prev_index = (uint8_t)old_step;
				g_dirty |= UI_DIRTY_MENU;
			}
		} else if (g_edit_step == UI_EDIT_X) {
			g_edit_step = UI_EDIT_Y;
			g_menu_prev_index = (uint8_t)old_step;
			g_dirty |= UI_DIRTY_MENU;
		} else {
			g_state = UI_STATE_MENU_TEMP;
			ui_menu_request_all();
		}
		g_dirty |= UI_DIRTY_TEMP;
	}
}

void UI_OnKeyEvent(const key_event_t *ev)
{
	ui_key_t key;

	if (ev == 0) {
		return;
	}
	key = ui_map_key(ev->key_id);
	if (key == UI_KEY_UNKNOWN) {
		return;
	}
	if (g_state == UI_STATE_STORAGE_BUSY || Storage_IsBusy()) {
		if (key == UI_KEY_LUT_CANCEL && ev->event == KEY_EVENT_PRESS) {
			Storage_Cancel();
			ui_set_toast(UI_TXT_CANCEL, UI_COLOR_WARN, 1000u);
		}
		return;
	}
	if (g_state == UI_STATE_HOME) {
		ui_handle_home(key, ev->event);
	} else if (g_state == UI_STATE_MENU_EMISS) {
		ui_handle_emiss(key, ev->event);
	} else if (g_state == UI_STATE_MENU_LUT) {
		ui_handle_lut(key, ev->event);
	} else if (g_state == UI_STATE_MENU_TEMP) {
		ui_handle_temp_menu(key, ev->event);
	} else if (g_state == UI_STATE_TEMP_EDIT_POINT) {
		ui_handle_temp_edit(key, ev->event);
	} else if (g_state == UI_STATE_TOAST) {
		g_state = g_return_state;
	}
}
