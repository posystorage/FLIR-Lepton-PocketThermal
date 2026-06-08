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

	/* 预先转成 LCD_Write_DAT16() 需要的字节序，避免刷屏内层循环再 __REV。 */
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
	uint8_t  idx0, idx1;
	uint8_t  r, g, b;

	for (i = 0; i < PALETTE_SIZE; i++) {
		for (s = 0; s < n - 1; s++) {
			if (i <= stops[s + 1][0])
				break;
		}
		if (s >= n - 1) s = n - 2;
		idx0  = stops[s][0];
		idx1  = stops[s + 1][0];
		range = idx1 - idx0;
		frac  = i - idx0;
		if (range == 0) range = 1;
		r = (uint8_t)(stops[s][1] +
			(stops[s + 1][1] - stops[s][1]) * frac / range);
		g = (uint8_t)(stops[s][2] +
			(stops[s + 1][2] - stops[s][2]) * frac / range);
		b = (uint8_t)(stops[s][3] +
			(stops[s + 1][3] - stops[s][3]) * frac / range);
		palette_store(i, r, g, b);
	}
}

/* Ironbow color stops: {index, R, G, B} */
static const uint8_t ironbow_stops[][4] = {
	{   0,   0,   0,   0},
	{  20,   0,   0,  50},
	{  50,  40,   0, 120},
	{  80, 120,   0, 160},
	{ 120, 180,  10,  60},
	{ 160, 240,  60,   0},
	{ 200, 255, 180,   0},
	{ 240, 255, 255, 100},
	{ 255, 255, 255, 255},
};
#define N_STOPS ((uint32_t)(sizeof(ironbow_stops) / sizeof(ironbow_stops[0])))

void palette_init_ironbow(void)
{
	palette_interp(ironbow_stops, N_STOPS);
	g_palette_id = PALETTE_ID_IRONBOW;
}


/* Ice-Fire color stops: blue at low, grayscale in middle, red at high */
static const uint8_t icefire_stops[][4] = {
//	{  0,   0,   0, 255},
//	{ 64,   0,   0,  64},
//	{ 80,  80,  80,  80},
//	{176, 176, 176, 176},
//	{192,  64,   0,   0},
//	{255, 255,   0,   0},
	{  0,   0,   0, 255},
	{ 1,   0,   0,  0  },
	{250,  250, 250,250},
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

/* ---- Wheel6: green→purple→light blue→red→dark blue→yellow ---- */
static const uint8_t wheel6_stops[][4] = {
	{  0,   0, 255,   0},  /* green      */
	{ 42, 128,   0, 128},  /* purple     */
	{ 85, 128, 200, 255},  /* light blue */
	{127, 255,   0,   0},  /* red        */
	{170,   0,   0, 128},  /* dark blue  */
	{212, 255, 255,   0},  /* yellow     */
	{255,   0, 255,   0},  /* green      */
};
void palette_init_wheel6(void)
{
	palette_interp(wheel6_stops, sizeof(wheel6_stops)/sizeof(wheel6_stops[0]));
	g_palette_id = PALETTE_ID_WHEEL6;
}

/* ---- Fusion: FLIR-style thermal fusion ---- */
static const uint8_t fusion_stops[][4] = {
	{  0,   0,   0,   0},  /* black     */
	{ 64,   0,   0, 255},  /* blue      */
	{128, 128,   0, 128},  /* purple    */
	{192, 255,   0,   0},  /* red       */
	{224, 255, 255,   0},  /* yellow    */
	{255, 255, 255, 255},  /* white     */
};
void palette_init_fusion(void)
{
	palette_interp(fusion_stops, sizeof(fusion_stops)/sizeof(fusion_stops[0]));
	g_palette_id = PALETTE_ID_FUSION;
}

/* ---- Rainbow: classic spectral rainbow ---- */
static const uint8_t rainbow_stops[][4] = {
	{  0,   0,   0,   0},  /* black  */
	{ 36, 128,   0, 128},  /* purple */
	{ 73,   0,   0, 255},  /* blue   */
	{109,   0, 255, 255},  /* cyan   */
	{146,   0, 255,   0},  /* green  */
	{182, 255,   0,   0},  /* red    */
	{218, 255, 255,   0},  /* yellow */
	{255, 255, 255, 255},  /* white  */
};
void palette_init_rainbow(void)
{
	palette_interp(rainbow_stops, sizeof(rainbow_stops)/sizeof(rainbow_stops[0]));
	g_palette_id = PALETTE_ID_RAINBOW;
}

/* ---- Glowbow: warm glow palette ---- */
static const uint8_t glowbow_stops[][4] = {
	{  0,   0,   0,   0},  /* black     */
	{ 64, 128,   0,   0},  /* dark red  */
	{128, 255,   0,   0},  /* red       */
	{192, 255, 128,   0},  /* orange    */
	{224, 255, 255,   0},  /* yellow    */
	{255, 255, 255, 255},  /* white     */
};
void palette_init_glowbow(void)
{
	palette_interp(glowbow_stops, sizeof(glowbow_stops)/sizeof(glowbow_stops[0]));
	g_palette_id = PALETTE_ID_GLOWBOW;
}

/* ---- Sepia: monochrome sepia tone ---- */
static const uint8_t sepia_stops[][4] = {
	{  0,   0,   0,   0},  /* black     */
	{ 64,  64,  42,  21},  /* dark brown*/
	{150, 168, 136,  92},  /* tan       */
	{220, 230, 210, 170},  /* cream     */
	{255, 255, 248, 230},  /* off-white */
};
void palette_init_sepia(void)
{
	palette_interp(sepia_stops, sizeof(sepia_stops)/sizeof(sepia_stops[0]));
	g_palette_id = PALETTE_ID_SEPIA;
}

/* ---- Color: black→purple→blue→cyan→green→yellow→orange→red→white ---- */
static const uint8_t color_stops[][4] = {
	{  0,   0,   0,   0},  /* black       */
	{ 32,  80,   0, 128},  /* dark purple */
	{ 64,   0,   0, 255},  /* blue        */
	{ 96,   0, 255, 255},  /* cyan        */
	{128,   0, 255,   0},  /* green       */
	{160, 255, 255,   0},  /* yellow      */
	{192, 255, 128,   0},  /* orange      */
	{224, 255,   0,   0},  /* red         */
	{255, 255, 255, 255},  /* white       */
};
void palette_init_color(void)
{
	palette_interp(color_stops, sizeof(color_stops)/sizeof(color_stops[0]));
	g_palette_id = PALETTE_ID_COLOR;
}

/* ---- Rain: cool rain-like gradient ---- */
static const uint8_t rain_stops[][4] = {
	{  0,   0,   0, 128},  /* dark blue */
	{ 48,   0,  64, 200},  /* blue      */
	{ 96,   0, 160, 255},  /* sky blue  */
	{128,  64, 224, 192},  /* teal      */
	{176, 128, 255,  64},  /* lime      */
	{216, 255, 224,   0},  /* yellow    */
	{255, 255,   0,   0},  /* red       */
};
void palette_init_rain(void)
{
	palette_interp(rain_stops, sizeof(rain_stops)/sizeof(rain_stops[0]));
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
