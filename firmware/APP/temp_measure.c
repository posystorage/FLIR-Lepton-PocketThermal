#include "temp_measure.h"
#include "debug.h"
#include <math.h>
#include <limits.h>

static const lep_rbfo_t *g_p;
static float g_cal_gain  = 1.0f;
static float g_cal_offset = 0.0f;
static uint16_t g_emiss_x100 = 95u;
static int16_t g_ambient_c_x100 = 2500;
static temp_points_t g_points = {
	{
		{1u, 40u, 30u, INT16_MIN},
		{1u, 0u, 0u, INT16_MIN},
		{1u, 0u, 0u, INT16_MIN},
		{0u, 30u, 30u, INT16_MIN},
		{0u, 50u, 30u, INT16_MIN},
	}
};

void temp_init(const lep_rbfo_t *rbfo, uint8_t valid)
{
	g_p = rbfo;
	(void)valid;
	g_cal_gain  = 1.0f;
	g_cal_offset = 0.0f;
	g_emiss_x100 = 95u;
	g_ambient_c_x100 = 2500;
//	IMG_DEBUG("temp_init: RBFO=%s R=%u B=%u F=%u O=%d",
//		valid ? "RAD" : "fallback",
//		(unsigned)rbfo->R, (unsigned)rbfo->B,
//		(unsigned)rbfo->F, (int)rbfo->O);
}

void temp_set_calibration(float gain, float offset)
{
	g_cal_gain  = gain;
	g_cal_offset = offset;
	IMG_DEBUG("cal: gain=%.4f offset=%.2f", gain, offset);
}

void temp_set_emissivity(uint16_t emiss_x100)
{
	if (emiss_x100 < 1u) {
		emiss_x100 = 1u;
	}
	if (emiss_x100 > 100u) {
		emiss_x100 = 100u;
	}
	g_emiss_x100 = emiss_x100;
}

void temp_set_ambient_c_x100(int16_t ambient_c_x100)
{
	g_ambient_c_x100 = ambient_c_x100;
}

uint16_t temp_get_emissivity(void)
{
	return g_emiss_x100;
}

static int16_t clamp_c_x100(float temp_c)
{
	if (temp_c < -40.0f) temp_c = -40.0f;
	if (temp_c > 300.0f) temp_c = 300.0f;
	return (int16_t)(temp_c * 100.0f);
}

static float rbfo_signal_from_c(float temp_c)
{
	float r, b, f, o, temp_k, exp_arg, denom;

	if (!g_p) {
		return 0.0f;
	}
	r = (float)g_p->R;
	b = (float)g_p->B * 0.001f;
	f = (float)g_p->F * 0.001f;
	o = (float)g_p->O * 0.001f;
	temp_k = temp_c + 273.15f;
	if (temp_k <= 0.0f) {
		return o;
	}
	exp_arg = expf(b / temp_k);
	denom = exp_arg - f;
	if (denom <= 0.0f) {
		return o;
	}
	return o + (r / denom);
}

static int16_t signal_to_c_x100(float signal)
{
	float r, b, f, o;
	float denom, log_arg, temp_c;

	if (!g_p) return INT16_MIN;
	r = (float)g_p->R;
	b = (float)g_p->B * 0.001f;
	f = (float)g_p->F * 0.001f;
	o = (float)g_p->O * 0.001f;
	denom = signal - o;

	if (denom <= 0.0f) return INT16_MIN;
	log_arg = r / denom + f;
	if (log_arg <= 1.0f) return INT16_MIN;

	temp_c = b / logf(log_arg) - 273.15f;
	temp_c = g_cal_gain * temp_c + g_cal_offset;
	return clamp_c_x100(temp_c);
}

static int16_t raw14_to_c_x100(uint16_t pixel)
{
	float s, amb_s, emiss, corrected_s;

	s = (float)(pixel & 0x3FFFu);
	emiss = (float)g_emiss_x100 * 0.01f;
	if (emiss < 0.01f) {
		emiss = 0.01f;
	}
	if (emiss < 0.999f) {
		amb_s = rbfo_signal_from_c((float)g_ambient_c_x100 * 0.01f);
		corrected_s = (s - ((1.0f - emiss) * amb_s)) / emiss;
	} else {
		corrected_s = s;
	}
	return signal_to_c_x100(corrected_s);
}

void convert_frame(const uint16_t raw14[60][80], int16_t temp_c_x100[60][80])
{
	uint32_t y, x;
	for (y = 0; y < 60; y++)
		for (x = 0; x < 80; x++)
			temp_c_x100[y][x] = raw14_to_c_x100(raw14[y][x]);
}

void temp_measure_points_full(const uint16_t raw14[60][80],
                              temp_points_t *points)
{
	uint32_t y, x;
	int32_t center_sum = 0;
	uint32_t center_count = 0;
	uint16_t raw_max = 0;
	uint16_t raw_min = 0x3FFFu;
	uint8_t max_x = 0u, max_y = 0u;
	uint8_t min_x = 0u, min_y = 0u;
	uint16_t raw;
	int16_t t;
	temp_points_t *dst;

	dst = (points != 0) ? points : &g_points;
	for (y = 0; y < 60; y++) {
		for (x = 0; x < 80; x++) {
			raw = raw14[y][x] & 0x3FFFu;

			if (raw > raw_max) {
				raw_max = raw;
				max_x = (uint8_t)x;
				max_y = (uint8_t)y;
			}
			if (raw < raw_min) {
				raw_min = raw;
				min_x = (uint8_t)x;
				min_y = (uint8_t)y;
			}

			if (y >= 29 && y < 31 && x >= 39 && x < 41) {
				t = raw14_to_c_x100(raw);
				if (t == INT16_MIN)
					continue;
				center_sum += t;
				center_count++;
			}
		}
	}

	dst->point[TEMP_POINT_CENTER].x = 40u;
	dst->point[TEMP_POINT_CENTER].y = 30u;
	dst->point[TEMP_POINT_CENTER].temp_c_x100 =
		center_count ? (int16_t)(center_sum / (int32_t)center_count) : INT16_MIN;
	dst->point[TEMP_POINT_MAX].x = max_x;
	dst->point[TEMP_POINT_MAX].y = max_y;
	dst->point[TEMP_POINT_MAX].temp_c_x100 = raw14_to_c_x100(raw_max);
	dst->point[TEMP_POINT_MIN].x = min_x;
	dst->point[TEMP_POINT_MIN].y = min_y;
	dst->point[TEMP_POINT_MIN].temp_c_x100 = raw14_to_c_x100(raw_min);
	if (dst->point[TEMP_POINT_USER1].enabled) {
		dst->point[TEMP_POINT_USER1].temp_c_x100 =
			raw14_to_c_x100(raw14[dst->point[TEMP_POINT_USER1].y][dst->point[TEMP_POINT_USER1].x]);
	} else {
		dst->point[TEMP_POINT_USER1].temp_c_x100 = INT16_MIN;
	}
	if (dst->point[TEMP_POINT_USER2].enabled) {
		dst->point[TEMP_POINT_USER2].temp_c_x100 =
			raw14_to_c_x100(raw14[dst->point[TEMP_POINT_USER2].y][dst->point[TEMP_POINT_USER2].x]);
	} else {
		dst->point[TEMP_POINT_USER2].temp_c_x100 = INT16_MIN;
	}
}

void temp_measure_points(const uint16_t raw14[60][80],
                         int16_t *center_c_x100,
                         int16_t *max_c_x100,
                         int16_t *min_c_x100)
{
	temp_measure_points_full(raw14, &g_points);
	if (center_c_x100 != 0)
		*center_c_x100 = g_points.point[TEMP_POINT_CENTER].temp_c_x100;
	if (max_c_x100 != 0)
		*max_c_x100 = g_points.point[TEMP_POINT_MAX].temp_c_x100;
	if (min_c_x100 != 0)
		*min_c_x100 = g_points.point[TEMP_POINT_MIN].temp_c_x100;
}

const temp_points_t *temp_get_points(void)
{
	return &g_points;
}

void temp_set_point_enabled(temp_point_id_t id, uint8_t enabled)
{
	if ((uint32_t)id >= (uint32_t)TEMP_POINT_COUNT) {
		return;
	}
	g_points.point[id].enabled = enabled ? 1u : 0u;
}

uint8_t temp_get_point_enabled(temp_point_id_t id)
{
	if ((uint32_t)id >= (uint32_t)TEMP_POINT_COUNT) {
		return 0u;
	}
	return g_points.point[id].enabled;
}

void temp_set_user_point(temp_point_id_t id, uint8_t x, uint8_t y)
{
	if (id != TEMP_POINT_USER1 && id != TEMP_POINT_USER2) {
		return;
	}
	if (x >= 80u) {
		x = 79u;
	}
	if (y >= 60u) {
		y = 59u;
	}
	g_points.point[id].x = x;
	g_points.point[id].y = y;
}

void temp_get_user_point(temp_point_id_t id, uint8_t *x, uint8_t *y)
{
	if (id != TEMP_POINT_USER1 && id != TEMP_POINT_USER2) {
		return;
	}
	if (x != 0) {
		*x = g_points.point[id].x;
	}
	if (y != 0) {
		*y = g_points.point[id].y;
	}
}

int16_t temp_get_center_roi(const int16_t temp_c_x100[60][80])
{
	uint32_t y, x;
	int32_t sum = 0;
	for (y = 29; y < 31; y++)
		for (x = 39; x < 41; x++)
			sum += temp_c_x100[y][x];
	return (int16_t)(sum / 4);
}

int16_t temp_get_max(const int16_t temp_c_x100[60][80])
{
	int16_t max_val = INT16_MIN;
	uint32_t y, x;
	for (y = 0; y < 60; y++)
		for (x = 0; x < 80; x++)
			if (temp_c_x100[y][x] > max_val && temp_c_x100[y][x] != INT16_MIN)
				max_val = temp_c_x100[y][x];
	return max_val;
}

int16_t temp_get_min(const int16_t temp_c_x100[60][80])
{
	int16_t min_val = INT16_MAX;
	uint32_t y, x;
	for (y = 0; y < 60; y++)
		for (x = 0; x < 80; x++)
			if (temp_c_x100[y][x] < min_val && temp_c_x100[y][x] != INT16_MIN)
				min_val = temp_c_x100[y][x];
	return min_val;
}
