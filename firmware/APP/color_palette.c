#include "color_palette.h"
#include "M480.h"

static rgb888_t g_palette[PALETTE_SIZE];
static uint16_t g_palette_lcd565[PALETTE_SIZE];
static yuv888_t g_palette_yuv[PALETTE_SIZE];
static palette_id_t g_palette_id = PALETTE_ID_FUSION;

static uint8_t clamp_u8_i32(int32_t v)
{
	if (v < 0) {
		return 0u;
	}
	if (v > 255) {
		return 255u;
	}
	return (uint8_t)v;
}

static uint16_t palette_rgb_to_lcd565(uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t rgb565;

	rgb565 = (uint16_t)((((uint16_t)r & 0xF8u) << 8) |
	                    (((uint16_t)g & 0xFCu) << 3) |
	                    ((uint16_t)b >> 3));

	return (uint16_t)(__REV(rgb565) >> 16);
}

static yuv888_t palette_rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b)
{
	yuv888_t yuv;
	int32_t y, u, v;

	y = ((77 * (int32_t)r) + (150 * (int32_t)g) + (29 * (int32_t)b)) >> 8;
	u = 128 + (((-43 * (int32_t)r) - (85 * (int32_t)g) + (128 * (int32_t)b)) >> 8);
	v = 128 + (((128 * (int32_t)r) - (107 * (int32_t)g) - (21 * (int32_t)b)) >> 8);

	yuv.y = clamp_u8_i32(y);
	yuv.u = clamp_u8_i32(u);
	yuv.v = clamp_u8_i32(v);
	return yuv;
}

static void palette_store(uint32_t idx, uint8_t r, uint8_t g, uint8_t b)
{
	g_palette[idx].r = r;
	g_palette[idx].g = g;
	g_palette[idx].b = b;
	g_palette_lcd565[idx] = palette_rgb_to_lcd565(r, g, b);
	g_palette_yuv[idx] = palette_rgb_to_yuv(r, g, b);
}

/* Common linear interpolation over indexed color stops.
 * stops[][4] = {index, R, G, B}, n = number of stops. */
static void palette_interp(const uint8_t stops[][4], uint32_t n)
{
	uint32_t i, s, range, frac;
	uint8_t idx0, idx1;
	uint8_t r, g, b;
	int32_t r0, g0, b0;
	int32_t dr, dg, db;

	for (i = 0; i < PALETTE_SIZE; i++) {
		for (s = 0; s < n - 1u; s++) {
			if (i <= stops[s + 1u][0]) {
				break;
			}
		}
		if (s >= n - 1u) {
			s = n - 2u;
		}
		idx0 = stops[s][0];
		idx1 = stops[s + 1u][0];
		range = idx1 - idx0;
		frac = i - idx0;
		if (range == 0u) {
			range = 1u;
		}
		r0 = stops[s][1];
		g0 = stops[s][2];
		b0 = stops[s][3];
		dr = (int32_t)stops[s + 1u][1] - r0;
		dg = (int32_t)stops[s + 1u][2] - g0;
		db = (int32_t)stops[s + 1u][3] - b0;
		r = clamp_u8_i32(r0 + dr * (int32_t)frac / (int32_t)range);
		g = clamp_u8_i32(g0 + dg * (int32_t)frac / (int32_t)range);
		b = clamp_u8_i32(b0 + db * (int32_t)frac / (int32_t)range);
		palette_store(i, r, g, b);
	}
}

static const uint8_t ironbow_stops[][4] = {
	{  0,   0,   0,   0},
	{ 24,   8,   0,  35},
	{ 48,  38,   0,  88},
	{ 76,  94,   0, 120},
	{108, 150,  12,  78},
	{140, 205,  35,  26},
	{172, 242,  92,   0},
	{204, 255, 170,   0},
	{232, 255, 232,  80},
	{255, 255, 255, 255},
};
#define N_STOPS ((uint32_t)(sizeof(ironbow_stops) / sizeof(ironbow_stops[0])))

void palette_init_ironbow(void)
{
	palette_interp(ironbow_stops, N_STOPS);
	g_palette_id = PALETTE_ID_IRONBOW;
}

static const uint8_t icefire_stops[][4] = {
	{  0,   0,   0, 255},
	{  1,   0,   0,   0},
	{250, 250, 250, 250},
	{255, 255,   0,   0},
};
#define N_ICEFIRE_STOPS ((uint32_t)(sizeof(icefire_stops) / sizeof(icefire_stops[0])))

void palette_init_icefire(void)
{
	palette_interp(icefire_stops, N_ICEFIRE_STOPS);
	g_palette_id = PALETTE_ID_ICEFIRE;
}

const rgb888_t *palette_get(void)
{
	return g_palette;
}

const uint16_t *palette_get_lcd565(void)
{
	return g_palette_lcd565;
}

const yuv888_t *palette_get_yuv(void)
{
	return g_palette_yuv;
}

/* Wheel6 reference order: green, magenta/violet, light blue, red,
 * blue-violet, gray-yellow, yellow. */
static const uint8_t wheel6_stops[][4] = {
	{  0,   8,  48,  24},
	{ 24,  32, 130,  55},
	{ 46, 118, 145, 112},
	{ 68, 150,  88, 170},
	{ 92, 118, 100, 196},
	{116, 120, 210, 230},
	{138,  92, 198, 200},
	{160, 170, 135, 115},
	{182, 220,  38,  68},
	{204, 152,  32, 142},
	{226,  56,  54, 170},
	{240, 148, 155, 120},
	{255, 255, 244,  54},
};

void palette_init_wheel6(void)
{
	palette_interp(wheel6_stops, sizeof(wheel6_stops) / sizeof(wheel6_stops[0]));
	g_palette_id = PALETTE_ID_WHEEL6;
}

static const uint8_t fusion_stops[][4] = {
	{  0,   0,   0,   0},
	{ 64,   0,   0, 255},
	{128, 128,   0, 128},
	{192, 255,   0,   0},
	{224, 255, 255,   0},
	{255, 255, 255, 255},
};

void palette_init_fusion(void)
{
	palette_interp(fusion_stops, sizeof(fusion_stops) / sizeof(fusion_stops[0]));
	g_palette_id = PALETTE_ID_FUSION;
}

/* Turbo-like smooth rainbow for scalar heat maps. */
static const uint8_t rainbow_stops[][4] = {
	{  0,  48,  18,  59},
	{ 24,  65,  68, 178},
	{ 48,  70, 120, 246},
	{ 72,  44, 170, 225},
	{ 96,  32, 202, 172},
	{120,  70, 224, 116},
	{144, 150, 238,  62},
	{168, 223, 220,  45},
	{192, 250, 172,  40},
	{216, 242, 104,  34},
	{240, 202,  46,  28},
	{255, 122,   4,   3},
};

void palette_init_rainbow(void)
{
	palette_interp(rainbow_stops, sizeof(rainbow_stops) / sizeof(rainbow_stops[0]));
	g_palette_id = PALETTE_ID_RAINBOW;
}

static const uint8_t glowbow_stops[][4] = {
	{  0,   0,   0,   0},
	{ 64, 128,   0,   0},
	{128, 255,   0,   0},
	{192, 255, 128,   0},
	{224, 255, 255,   0},
	{255, 255, 255, 255},
};

void palette_init_glowbow(void)
{
	palette_interp(glowbow_stops, sizeof(glowbow_stops) / sizeof(glowbow_stops[0]));
	g_palette_id = PALETTE_ID_GLOWBOW;
}

static const uint8_t sepia_stops[][4] = {
	{  0,   0,   0,   0},
	{ 64,  64,  42,  21},
	{150, 168, 136,  92},
	{220, 230, 210, 170},
	{255, 255, 248, 230},
};

void palette_init_sepia(void)
{
	palette_interp(sepia_stops, sizeof(sepia_stops) / sizeof(sepia_stops[0]));
	g_palette_id = PALETTE_ID_SEPIA;
}

static const uint8_t color_stops[][4] = {
	{  0,   0,   0,   0},
	{ 32,  80,   0, 128},
	{ 64,   0,   0, 255},
	{ 96,   0, 255, 255},
	{128,   0, 255,   0},
	{160, 255, 255,   0},
	{192, 255, 128,   0},
	{224, 255,   0,   0},
	{255, 255, 255, 255},
};

void palette_init_color(void)
{
	palette_interp(color_stops, sizeof(color_stops) / sizeof(color_stops[0]));
	g_palette_id = PALETTE_ID_COLOR;
}

static const uint8_t rain_stops[][4] = {
	{  0,   0,   7,  60},
	{ 40,   0,  42, 135},
	{ 82,   0, 105, 190},
	{122,  24, 165, 210},
	{160,  76, 205, 185},
	{196, 175, 220, 120},
	{228, 244, 170,  54},
	{255, 224,  48,  32},
};

void palette_init_rain(void)
{
	palette_interp(rain_stops, sizeof(rain_stops) / sizeof(rain_stops[0]));
	g_palette_id = PALETTE_ID_RAIN;
}

void palette_init_by_id(palette_id_t id)
{
	switch (id) {
	case PALETTE_ID_IRONBOW:
		palette_init_ironbow();
		break;
	case PALETTE_ID_ICEFIRE:
		palette_init_icefire();
		break;
	case PALETTE_ID_WHEEL6:
		palette_init_wheel6();
		break;
	case PALETTE_ID_FUSION:
		palette_init_fusion();
		break;
	case PALETTE_ID_RAINBOW:
		palette_init_rainbow();
		break;
	case PALETTE_ID_GLOWBOW:
		palette_init_glowbow();
		break;
	case PALETTE_ID_SEPIA:
		palette_init_sepia();
		break;
	case PALETTE_ID_COLOR:
		palette_init_color();
		break;
	case PALETTE_ID_RAIN:
		palette_init_rain();
		break;
	default:
		palette_init_fusion();
		break;
	}
}

const char *palette_get_name(palette_id_t id)
{
	static const char *names[PALETTE_ID_COUNT] = {
		"Ironbow",
		"IceFire",
		"Wheel6",
		"Fusion",
		"Rainbow",
		"Glowbow",
		"Sepia",
		"Color",
		"Rain",
	};

	if ((uint32_t)id >= (uint32_t)PALETTE_ID_COUNT) {
		return "Fusion";
	}
	return names[id];
}

palette_id_t palette_get_current_id(void)
{
	return g_palette_id;
}
