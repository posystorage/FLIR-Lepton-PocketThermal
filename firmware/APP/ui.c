#include "ui.h"
#include "ui_draw.h"
#include "ui_status.h"
#include "ui_body.h"
#include "ui_menu.h"
#include "ui_marker.h"
#include "ui_thermal.h"
#include "storage.h"
#include "lepton_app.h"
#include "temp_measure.h"
#include "sys_tick.h"
#include "mpu6050.h"
#include "debug.h"

#define UI_DIRTY_LUT_BODY       (1u << 0)
#define UI_DIRTY_LUT_VALUES     (1u << 1)
#define UI_DIRTY_STATIC_BARS    (1u << 2)
#define UI_DIRTY_BODY_FULL      (1u << 3)
#define UI_DIRTY_TEMP_VALUES    (1u << 4)
#define UI_DIRTY_EMISSIVITY     (1u << 5)
#define UI_DIRTY_STORAGE        (1u << 6)
#define UI_DIRTY_MENU           (1u << 7)
#define UI_DIRTY_ALL            0xFFFFFFFFu

#define UI_TEXT_INSERT_SD       "请插卡"
#define UI_TEXT_SAVING          "保存中"
#define UI_TEXT_READING_SD      "读卡中"
#define UI_TEXT_WRITE_OK        "写入成功"
#define UI_TEXT_WRITE_FAIL      "写入失败"
#define UI_TEXT_CANCEL          "取消"
#define UI_TEXT_SD_ERROR        "SD错误"
#define UI_TEXT_ERROR           "错误"
#define UI_TEXT_FFC             "FFC..."

static ui_orientation_t g_orientation = UI_ORIENTATION_0;
static mpu6050_orientation_t g_last_mpu_orientation =
	MPU6050_ORIENTATION_UNKNOWN;
static uint32_t g_dirty;
static uint8_t g_full_clear_pending;
static uint8_t g_storage_was_busy;
static uint8_t g_ffc_text_visible;
static uint8_t g_power_key_pressed;
static uint8_t g_power_key_long_seen;
static uint32_t g_storage_text_until;

static ui_key_t map_key(uint8_t key_id)
{
	switch (key_id) {
	case KEY1_ID:
		return UI_KEY_POWER;
	case KEY5_ID:
		return UI_KEY_PREVIOUS;
	case KEY4_ID:
		return UI_KEY_NEXT;
	case KEY3_ID:
		return UI_KEY_CANCEL;
	case KEY2_ID:
		return UI_KEY_CONFIRM;
	default:
		return UI_KEY_UNKNOWN;
	}
}

static void clear_screen(void)
{
	const ui_layout_t *layout = UI_LayoutGet();
	ui_draw_fill_rect_xy(0u, 0u, layout->screen_w, layout->screen_h,
	                     UI_COLOR_BG);
}

void UI_OnOrientationChanged(ui_orientation_t orientation)
{
	if (orientation == g_orientation) {
		return;
	}
	g_orientation = orientation;
	ui_draw_set_orientation(orientation);
	g_full_clear_pending = 1u;
	g_dirty = UI_DIRTY_ALL;
}

static ui_orientation_t map_mpu_orientation(mpu6050_orientation_t orientation)
{
	switch (orientation) {
	case MPU6050_ORIENTATION_0:
		return UI_ORIENTATION_180;
	case MPU6050_ORIENTATION_180:
		return UI_ORIENTATION_0;
	case MPU6050_ORIENTATION_90:
		return UI_ORIENTATION_90;
	case MPU6050_ORIENTATION_270:
	default:
		return UI_ORIENTATION_270;
	}
}

static void orientation_service(void)
{
	mpu6050_orientation_t current;

	if (!g_mpu_tick_flag) {
		return;
	}
	g_mpu_tick_flag = 0u;
	MPU6050_Service();
	current = MPU6050_GetOrientation();
	if (current == g_last_mpu_orientation ||
	    current == MPU6050_ORIENTATION_UNKNOWN ||
	    current == MPU6050_ORIENTATION_FLAT) {
		return;
	}
	g_last_mpu_orientation = current;
	HW_DEBUG("rotate: %d", current);
	UI_OnOrientationChanged(map_mpu_orientation(current));
}

static void set_storage_text(const char *text, uint16_t color, uint32_t ms)
{
	g_ffc_text_visible = 0u;
	UI_BodySetStorageText(text, color);
	g_storage_text_until = (ms == 0u) ? 0u : GetTick() + ms;
	g_dirty |= UI_DIRTY_STORAGE;
}

static void start_storage(void)
{
	storage_result_t result = Storage_BeginCapture();

	if (result == STORAGE_RESULT_BUSY) {
		g_storage_was_busy = 1u;
		set_storage_text(UI_TEXT_SAVING, UI_COLOR_WARN, 0u);
	} else if (result == STORAGE_RESULT_NO_CARD) {
		set_storage_text(UI_TEXT_INSERT_SD, UI_COLOR_ERR, 2000u);
	} else if (result == STORAGE_RESULT_MSC_BUSY) {
		set_storage_text(UI_TEXT_READING_SD, UI_COLOR_WARN, 2000u);
	} else if (result == STORAGE_RESULT_NO_FRAME) {
		set_storage_text(UI_TEXT_ERROR, UI_COLOR_ERR, 2000u);
	} else {
		set_storage_text(UI_TEXT_SD_ERROR, UI_COLOR_ERR, 2000u);
	}
}

void UI_Init(void)
{
	g_orientation = UI_ORIENTATION_0;
	g_last_mpu_orientation = MPU6050_ORIENTATION_UNKNOWN;
	ui_draw_set_orientation(g_orientation);
	UI_MarkerInit();
	UI_BodyInit();
	UI_MenuInit();
	temp_set_emissivity(95u);
	temp_set_ambient_c_x100(2500);
	g_storage_was_busy = 0u;
	g_ffc_text_visible = 0u;
	g_power_key_pressed = 0u;
	g_power_key_long_seen = 0u;
	g_storage_text_until = 0u;
	g_full_clear_pending = 0u;
	clear_screen();
	UI_ThermalDrawLutBody();
	UI_ThermalDrawLutValues();
	UI_StatusDrawStatic();
	UI_BodyDrawFull();
	g_dirty = 0u;
}

void UI_OnTemperatureFrame(const temp_points_t *points)
{
	UI_BodyPublishTemperature(points);
}

void UI_DrawThermalFrameRgb565(uint8_t gray[240][320])
{
	UI_ThermalDrawFrame(gray);
}

void UI_DrawImageOverlay(void)
{
	UI_ThermalDrawOverlay();
}

static void service_temperature(void)
{
	uint8_t update = UI_BodyConsumeTemperature(GetTick());

	if (update & UI_BODY_UPDATE_FULL) {
		g_dirty |= UI_DIRTY_BODY_FULL | UI_DIRTY_LUT_VALUES;
	} else if (update & UI_BODY_UPDATE_VALUES) {
		g_dirty |= UI_DIRTY_TEMP_VALUES | UI_DIRTY_LUT_VALUES;
	}
}

static void service_storage(void)
{
	uint32_t now = GetTick();
	uint8_t ffc_busy;

	if (g_storage_was_busy && !Storage_IsBusy()) {
		storage_result_t result = Storage_GetLastResult();
		g_storage_was_busy = 0u;
		if (result == STORAGE_RESULT_OK) {
			set_storage_text(UI_TEXT_WRITE_OK, UI_COLOR_OK, 2500u);
		} else if (result == STORAGE_RESULT_CANCELLED) {
			set_storage_text(UI_TEXT_CANCEL, UI_COLOR_WARN, 2000u);
		} else {
			set_storage_text(UI_TEXT_WRITE_FAIL, UI_COLOR_ERR, 2500u);
		}
	}
	if (g_storage_text_until != 0u &&
	    (int32_t)(now - g_storage_text_until) >= 0) {
		g_storage_text_until = 0u;
		UI_BodySetStorageText("", UI_COLOR_TEXT);
		g_dirty |= UI_DIRTY_STORAGE;
	}

	ffc_busy = Lepton_App_IsFFCInProgress();
	if (!g_storage_was_busy && g_storage_text_until == 0u) {
		if (ffc_busy && !g_ffc_text_visible) {
			g_ffc_text_visible = 1u;
			UI_BodySetStorageText(UI_TEXT_FFC, UI_COLOR_WARN);
			g_dirty |= UI_DIRTY_STORAGE;
		} else if (!ffc_busy && g_ffc_text_visible) {
			g_ffc_text_visible = 0u;
			UI_BodySetStorageText("", UI_COLOR_TEXT);
			g_dirty |= UI_DIRTY_STORAGE;
		}
	}
}

static void expand_full_redraw(void)
{
	if (g_dirty != UI_DIRTY_ALL) {
		return;
	}
	g_dirty = UI_DIRTY_LUT_BODY | UI_DIRTY_LUT_VALUES |
	          UI_DIRTY_STATIC_BARS;
	if (UI_MenuIsActive()) {
		g_dirty |= UI_DIRTY_MENU;
	} else {
		g_dirty |= UI_DIRTY_BODY_FULL;
	}
}

void UI_Service(void)
{
	orientation_service();
	service_temperature();
	service_storage();
	expand_full_redraw();

	if (g_full_clear_pending) {
		clear_screen();
		g_full_clear_pending = 0u;
	}
	if (g_dirty & UI_DIRTY_LUT_BODY) {
		UI_ThermalDrawLutBody();
		g_dirty &= ~UI_DIRTY_LUT_BODY;
		return;
	}
	if (g_dirty & UI_DIRTY_LUT_VALUES) {
		UI_ThermalDrawLutValues();
		g_dirty &= ~UI_DIRTY_LUT_VALUES;
		return;
	}
	if (g_dirty & UI_DIRTY_STATIC_BARS) {
		UI_StatusDrawStatic();
		g_dirty &= ~UI_DIRTY_STATIC_BARS;
		return;
	}
	if (g_dirty & UI_DIRTY_MENU) {
		UI_MenuDraw();
		g_dirty &= ~UI_DIRTY_MENU;
		return;
	}
	if (UI_MenuIsActive()) {
		g_dirty &= ~(UI_DIRTY_BODY_FULL | UI_DIRTY_TEMP_VALUES |
		             UI_DIRTY_EMISSIVITY | UI_DIRTY_STORAGE);
		return;
	}
	if (g_dirty & UI_DIRTY_BODY_FULL) {
		UI_BodyDrawFull();
		g_dirty &= ~(UI_DIRTY_BODY_FULL | UI_DIRTY_TEMP_VALUES |
		             UI_DIRTY_EMISSIVITY | UI_DIRTY_STORAGE);
		return;
	}
	if (g_dirty & UI_DIRTY_TEMP_VALUES) {
		UI_BodyDrawTemperatureValues();
		g_dirty &= ~UI_DIRTY_TEMP_VALUES;
		return;
	}
	if (g_dirty & UI_DIRTY_EMISSIVITY) {
		UI_BodyDrawEmissivity();
		g_dirty &= ~UI_DIRTY_EMISSIVITY;
		return;
	}
	if (g_dirty & UI_DIRTY_STORAGE) {
		UI_BodyDrawStorage();
		g_dirty &= ~UI_DIRTY_STORAGE;
	}
}

void UI_OnKeyEvent(const key_event_t *event)
{
	ui_key_t key;

	if (event == 0) {
		return;
	}
	key = map_key(event->key_id);
	if (key == UI_KEY_UNKNOWN) {
		return;
	}
	if (key == UI_KEY_POWER) {
		if (event->event == KEY_EVENT_PRESS) {
			g_power_key_pressed = 1u;
			g_power_key_long_seen = 0u;
		} else if (event->event == KEY_EVENT_LONG) {
			g_power_key_long_seen = 1u;
		} else if (event->event == KEY_EVENT_RELEASE) {
			if (g_power_key_pressed && !g_power_key_long_seen &&
			    !Storage_IsBusy()) {
				Lepton_App_RequestManualFFC();
			}
			g_power_key_pressed = 0u;
			g_power_key_long_seen = 0u;
		}
		return;
	}
	if (Storage_IsBusy()) {
		if (key == UI_KEY_CANCEL && event->event == KEY_EVENT_PRESS) {
			Storage_Cancel();
			set_storage_text(UI_TEXT_CANCEL, UI_COLOR_WARN, 1000u);
		}
		return;
	}
	if (UI_MenuIsActive()) {
		uint8_t result = UI_MenuHandleKey(key, event->event);
		g_dirty |= UI_DIRTY_MENU;
		if (result & UI_MENU_RESULT_LUT) {
			g_dirty |= UI_DIRTY_LUT_BODY | UI_DIRTY_LUT_VALUES;
		}
		if (result & UI_MENU_RESULT_EMISS) {
			g_dirty |= UI_DIRTY_EMISSIVITY;
		}
		if (result & (UI_MENU_RESULT_TEMP | UI_MENU_RESULT_COLOR)) {
			g_dirty |= UI_DIRTY_BODY_FULL;
		}
		if (result & UI_MENU_RESULT_CLOSED) {
			g_dirty &= ~UI_DIRTY_MENU;
			g_dirty |= UI_DIRTY_BODY_FULL;
		}
		return;
	}
	if (event->event != KEY_EVENT_PRESS) {
		return;
	}
	if (key == UI_KEY_PREVIOUS) {
		UI_MenuOpen(UI_MENU_EMISSIVITY);
		g_dirty |= UI_DIRTY_MENU;
	} else if (key == UI_KEY_NEXT) {
		UI_MenuOpen(UI_MENU_TEMPERATURE);
		g_dirty |= UI_DIRTY_MENU;
	} else if (key == UI_KEY_CANCEL) {
		UI_MenuOpen(UI_MENU_LUT);
		g_dirty |= UI_DIRTY_MENU;
	} else if (key == UI_KEY_CONFIRM) {
		start_storage();
	}
}
