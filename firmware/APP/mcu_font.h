#ifndef _MCU_FONT_H_
#define _MCU_FONT_H_

#include <stdint.h>
#include <stddef.h>

#define LVGL_VERSION_MAJOR 6
#define LVGL_VERSION_MINOR 0
#define LV_ATTRIBUTE_LARGE_CONST
#define lv_font_get_glyph_dsc_fmt_txt NULL
#define lv_font_get_bitmap_fmt_txt NULL
#define LV_FONT_SUBPX_NONE 0

typedef uint8_t bool;
typedef int16_t lv_coord_t;
typedef uint32_t lv_uintptr_t;

typedef struct {
	uint32_t bitmap_index : 20;
	uint32_t adv_w : 12;
	uint8_t box_w;
	uint8_t box_h;
	int8_t ofs_x;
	int8_t ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

enum {
	LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
	LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
	LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
	LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
};

typedef uint8_t lv_font_fmt_txt_cmap_type_t;

typedef struct {
	uint32_t range_start;
	uint16_t range_length;
	uint16_t glyph_id_start;
	const uint16_t *unicode_list;
	const void *glyph_id_ofs_list;
	uint16_t list_length;
	lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
	const uint8_t *glyph_bitmap;
	const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
	const lv_font_fmt_txt_cmap_t *cmaps;
	const void *kern_dsc;
	uint16_t kern_scale;
	uint16_t cmap_num : 10;
	uint16_t bpp : 4;
	uint16_t kern_classes : 1;
	uint16_t bitmap_format : 2;
	uint32_t last_letter;
	uint32_t last_glyph_id;
} lv_font_fmt_txt_dsc_t;

typedef struct {
	uint16_t adv_w;
	uint16_t box_w;
	uint16_t box_h;
	int16_t ofs_x;
	int16_t ofs_y;
	uint8_t bpp;
} lv_font_glyph_dsc_t;

typedef struct _lv_font_struct {
	bool (*get_glyph_dsc)(const struct _lv_font_struct *, lv_font_glyph_dsc_t *,
	                      uint32_t letter, uint32_t letter_next);
	const uint8_t *(*get_glyph_bitmap)(const struct _lv_font_struct *, uint32_t);
	lv_coord_t line_height;
	lv_coord_t base_line;
	uint8_t subpx : 2;
	void *dsc;
} lv_font_t;

extern lv_font_t fdxb_font_16;

void Show_MCU_Set_Color(uint16_t fg, uint16_t bg);
void Show_MCU_Set_Orientation(uint8_t orient);
uint32_t Show_Str_Get_Width(const char *txt, const lv_font_t *font);
void Show_MCU_Str(uint16_t x, uint16_t y, const char *txt, const lv_font_t *font);
void Show_MCU_Str_Middle(uint16_t x, uint16_t y, const char *txt, const lv_font_t *font);

#endif
