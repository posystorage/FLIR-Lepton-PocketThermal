#include "image_proc.h"
#include "agc.h"
#include "temp_measure.h"
#include "color_palette.h"
#include "image_upscale.h"
#include "usb_uvc.h"
#include "ui.h"
#include "lepton.h"
#include "sys_tick.h"
#include "debug.h"
#include <string.h>

static uint8_t  g_display8[60][80];
static uint16_t g_last_raw14[60][80];
static uint32_t g_pipeline_count = 0;
static int16_t  g_last_t_center;
static int16_t  g_last_t_max;
static int16_t  g_last_t_min;
static uint8_t  g_has_raw = 0;
static uint8_t  g_has_gray320 = 0;
static image_proc_stats_t g_stats;

#ifndef IMAGE_PIPELINE_UPSCALE_ROW_BUDGET
#define IMAGE_PIPELINE_UPSCALE_ROW_BUDGET  8u
#endif

#ifndef IMAGE_PIPELINE_UVC_WAIT_TIMEOUT_MS
#define IMAGE_PIPELINE_UVC_WAIT_TIMEOUT_MS  20u
#endif

#if (IMAGE_UPSCALE_MODE != IMAGE_UPSCALE_SR_2X2_LIMITED)
#error "image_pipeline_run now supports SR pipeline only; X4 direct output was removed from image_proc.c"
#endif

typedef enum {
	IMAGE_PIPELINE_STAGE_IDLE = 0,
	IMAGE_PIPELINE_STAGE_WAIT_UVC,
	IMAGE_PIPELINE_STAGE_UPSCALE,
	IMAGE_PIPELINE_STAGE_LCD
} image_pipeline_stage_t;

static image_pipeline_stage_t g_pipeline_stage = IMAGE_PIPELINE_STAGE_IDLE;
static uint32_t g_uvc_wait_start_ms;

static void image_pipeline_start_upscale(void)
{
	if (UVC_IsFrameBusy()) {
		g_uvc_wait_start_ms = GetTick();
		g_pipeline_stage = IMAGE_PIPELINE_STAGE_WAIT_UVC;
	} else {
		image_upscale_start(g_display8);
		g_pipeline_stage = IMAGE_PIPELINE_STAGE_UPSCALE;
	}
}

void image_proc_init(void)
{
	agc_init();
	palette_init_by_id(PALETTE_ID_FUSION);
	g_pipeline_count = 0;
	g_pipeline_stage = IMAGE_PIPELINE_STAGE_IDLE;
	g_has_raw = 0;
	g_has_gray320 = 0;
	memset(&g_stats, 0, sizeof(g_stats));
	IMG_DEBUG("pipeline init ok");
}

uint8_t image_pipeline_run(const uint16_t raw14[60][80])
{
	uint8_t (*sr)[IMAGE_UPSCALE_OUT_W];
	if (g_pipeline_stage == IMAGE_PIPELINE_STAGE_IDLE)
	{
		if (raw14 == 0)
		{
			return 1u;
		}

		memcpy(g_last_raw14, raw14, sizeof(g_last_raw14));
		g_has_raw = 1u;
		agc_build_map(raw14);
		agc_render(raw14, g_display8);
		temp_measure_points(raw14, &g_last_t_center, &g_last_t_max, &g_last_t_min);
		g_pipeline_count++;
		g_stats.frame_count = g_pipeline_count;
		g_stats.center_c_x100 = g_last_t_center;
		g_stats.max_c_x100 = g_last_t_max;
		g_stats.min_c_x100 = g_last_t_min;
		if ((g_pipeline_count & 0x1F) == 0)
		{
			IMG_DEBUG("frame %u: center=%d max=%d min=%d",
				(unsigned)g_pipeline_count, g_last_t_center, g_last_t_max, g_last_t_min);
		}
		image_pipeline_start_upscale();
		return 0u;
	}

	if (g_pipeline_stage == IMAGE_PIPELINE_STAGE_WAIT_UVC) {
		if (!UVC_IsFrameBusy()) {
			image_upscale_start(g_display8);
			g_pipeline_stage = IMAGE_PIPELINE_STAGE_UPSCALE;
			return 0u;
		}
		if ((int32_t)(GetTick() - g_uvc_wait_start_ms) >= (int32_t)IMAGE_PIPELINE_UVC_WAIT_TIMEOUT_MS) {
			UVC_AbortFrame();
			image_upscale_start(g_display8);
			g_pipeline_stage = IMAGE_PIPELINE_STAGE_UPSCALE;
		}
		return 0u;
	}

	if (g_pipeline_stage == IMAGE_PIPELINE_STAGE_UPSCALE) {
		if (image_upscale_step(IMAGE_PIPELINE_UPSCALE_ROW_BUDGET)) {
			g_pipeline_stage = IMAGE_PIPELINE_STAGE_LCD;
		}
		return 0u;
	}

	if (g_pipeline_stage != IMAGE_PIPELINE_STAGE_LCD) {
		return 0u;
	}
	sr = image_upscale_get_result();
	g_has_gray320 = 1u;
	UI_DrawThermalFrameRgb565(sr);
	UI_RequestRedraw(UI_DIRTY_TEMP | UI_DIRTY_LUT_VALUES);
	(void)UVC_BeginGrayFrame(sr);

	g_pipeline_stage = IMAGE_PIPELINE_STAGE_IDLE;


	return 1u;
}

uint8_t image_proc_has_frame(void)
{
	return (g_has_raw && g_has_gray320) ? 1u : 0u;
}

const uint16_t (*image_proc_get_last_raw14(void))[80]
{
	return g_has_raw ? (const uint16_t (*)[80])g_last_raw14 : 0;
}

uint8_t (*image_proc_get_last_gray320(void))[320]
{
	return g_has_gray320 ? image_upscale_get_result() : 0;
}

const image_proc_stats_t *image_proc_get_stats(void)
{
	return &g_stats;
}

const temp_points_t *image_proc_get_temp_points(void)
{
	return temp_get_points();
}
