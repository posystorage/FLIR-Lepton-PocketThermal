#ifndef _COLOR_PALETTE_H_
#define _COLOR_PALETTE_H_

#include <stdint.h>

#define PALETTE_SIZE  256u

#define LCD_OUTPUT_RGB888              0u
#define LCD_OUTPUT_RGB565              1u

#ifndef LCD_OUTPUT_COLOR_MODE
#define LCD_OUTPUT_COLOR_MODE          LCD_OUTPUT_RGB565
#endif

#ifndef PALETTE_ENABLE_RGB888
#define PALETTE_ENABLE_RGB888          (LCD_OUTPUT_COLOR_MODE == LCD_OUTPUT_RGB888)
#endif

typedef struct {
	uint8_t r, g, b;
} rgb888_t;

typedef struct {
	uint8_t y, u, v;
} yuv888_t;

typedef enum {
	PALETTE_ID_IRONBOW = 0,
	PALETTE_ID_ICEFIRE,
	PALETTE_ID_WHEEL6,
	PALETTE_ID_FUSION,
	PALETTE_ID_RAINBOW,
	PALETTE_ID_GLOWBOW,
	PALETTE_ID_SEPIA,
	PALETTE_ID_COLOR,
	PALETTE_ID_RAIN,
	PALETTE_ID_COUNT,
} palette_id_t;

void          palette_init_ironbow(void);
void          palette_init_icefire(void);
void          palette_init_wheel6(void);
void          palette_init_fusion(void);
void          palette_init_rainbow(void);
void          palette_init_glowbow(void);
void          palette_init_sepia(void);
void          palette_init_color(void);
void          palette_init_rain(void);
const rgb888_t *palette_get(void);
const uint16_t *palette_get_lcd565(void);
const yuv888_t *palette_get_yuv(void);
void          palette_init_by_id(palette_id_t id);
const char   *palette_get_name(palette_id_t id);
palette_id_t palette_get_current_id(void);

#endif
