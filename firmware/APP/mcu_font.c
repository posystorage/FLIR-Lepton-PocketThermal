#include "mcu_font.h"
#include "lcd.h"

uint16_t Font_Color = 0xFFFFu;
uint16_t Backdrop_Color = 0x0000u;
static uint8_t g_font_orient = 1u;

#define MCU_FONT_MAX_CHARS          48u

//static lv_font_fmt_txt_dsc_t flir_font_16_empty_dsc = {
//	0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0
//};

//__weak lv_font_t flir_font_16 = {
//	lv_font_get_glyph_dsc_fmt_txt,
//	lv_font_get_bitmap_fmt_txt,
//	16,
//	2,
//	0,
//	&flir_font_16_empty_dsc
//};

void Show_MCU_Set_Color(uint16_t fg, uint16_t bg)
{
	Font_Color = fg;
	Backdrop_Color = bg;
}

void Show_MCU_Set_Orientation(uint8_t orient)
{
	g_font_orient = orient;
}

static uint16_t mcu_font_color_to_lcd(uint16_t rgb565)
{
	return (uint16_t)(__REV(rgb565) >> 16);
}

static uint32_t lv_txt_utf8_next(const char *txt, uint32_t *i)
{
	uint32_t result = 0;
	uint32_t i_tmp = 0;

	if (i == NULL) {
		i = &i_tmp;
	}

	if ((txt[*i] & 0x80) == 0) {
		result = txt[*i];
		(*i)++;
	} else if ((txt[*i] & 0xE0) == 0xC0) {
		result = (uint32_t)(txt[*i] & 0x1F) << 6;
		(*i)++;
		if ((txt[*i] & 0xC0) != 0x80) return 0;
		result += (txt[*i] & 0x3F);
		(*i)++;
	} else if ((txt[*i] & 0xF0) == 0xE0) {
		result = (uint32_t)(txt[*i] & 0x0F) << 12;
		(*i)++;
		if ((txt[*i] & 0xC0) != 0x80) return 0;
		result += (uint32_t)(txt[*i] & 0x3F) << 6;
		(*i)++;
		if ((txt[*i] & 0xC0) != 0x80) return 0;
		result += (txt[*i] & 0x3F);
		(*i)++;
	} else if ((txt[*i] & 0xF8) == 0xF0) {
		result = (uint32_t)(txt[*i] & 0x07) << 18;
		(*i)++;
		if ((txt[*i] & 0xC0) != 0x80) return 0;
		result += (uint32_t)(txt[*i] & 0x3F) << 12;
		(*i)++;
		if ((txt[*i] & 0xC0) != 0x80) return 0;
		result += (uint32_t)(txt[*i] & 0x3F) << 6;
		(*i)++;
		if ((txt[*i] & 0xC0) != 0x80) return 0;
		result += txt[*i] & 0x3F;
		(*i)++;
	} else {
		(*i)++;
	}
	return result;
}

static void *_lv_utils_bsearch(const void *key, const void *base, uint32_t n, uint32_t size,
                               int32_t (*cmp)(const void *pRef, const void *pElement))
{
	const char *middle;
	int32_t c;

	for (middle = (const char *)base; n != 0u;) {
		middle += (n / 2u) * size;
		c = (*cmp)(key, middle);
		if (c > 0) {
			n = (n / 2u) - ((n & 1u) == 0u);
			base = (middle += size);
		} else if (c < 0) {
			n /= 2u;
			middle = (const char *)base;
		} else {
			return (void *)middle;
		}
	}
	return NULL;
}

static int32_t unicode_list_compare(const void *ref, const void *element)
{
	return ((int32_t)(*(const uint16_t *)ref)) - ((int32_t)(*(const uint16_t *)element));
}

static uint32_t get_glyph_dsc_id(const lv_font_t *font, uint32_t letter)
{
	lv_font_fmt_txt_dsc_t *fdsc;
	uint32_t glyph_id = 0;
	uint32_t rcp;
	uint8_t *p;
	lv_uintptr_t ofs;

	if (letter == '\0' || font == NULL || font->dsc == NULL) {
		return 0;
	}
	fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;
	if (letter == fdsc->last_letter) {
		return fdsc->last_glyph_id;
	}
	if (fdsc->cmap_num == 1u && fdsc->cmaps != NULL) {
		rcp = letter - fdsc->cmaps[0].range_start;
		if (rcp <= fdsc->cmaps[0].range_length &&
		    fdsc->cmaps[0].type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) {
			p = (uint8_t *)_lv_utils_bsearch(&rcp, fdsc->cmaps[0].unicode_list,
			                                 fdsc->cmaps[0].list_length,
			                                 sizeof(fdsc->cmaps[0].unicode_list[0]),
			                                 unicode_list_compare);
			if (p != NULL) {
				ofs = (lv_uintptr_t)(p - (uint8_t *)fdsc->cmaps[0].unicode_list);
				ofs >>= 1;
				glyph_id = fdsc->cmaps[0].glyph_id_start + ofs;
			}
		}
	}
	fdsc->last_letter = letter;
	fdsc->last_glyph_id = glyph_id;
	return glyph_id;
}

static uint8_t mcu_font_next_pixel(const uint8_t *bitmap, uint32_t bit_index, uint8_t bpp)
{
	uint32_t packed_bit;
	uint8_t shift;

	if (bpp == 0u || bitmap == NULL) {
		return 0;
	}
	packed_bit = bit_index * (uint32_t)bpp;
	shift = (uint8_t)(8u - bpp - (packed_bit & 7u));
	return (uint8_t)((bitmap[packed_bit >> 3] >> shift) & ((1u << bpp) - 1u));
}

uint16_t Show_MCU_Font_Fast(uint16_t x, uint16_t y, uint32_t unicode, const lv_font_t *font)
{
	lv_font_fmt_txt_dsc_t *fdsc;
	const lv_font_fmt_txt_glyph_dsc_t *gdsc;
	const uint8_t *bitmap;
	uint32_t gid;
	uint32_t row;
	uint32_t col;
	uint32_t bit_index;
	uint16_t fg;
	uint16_t bg;
	uint16_t adv;
	int16_t glyph_x;
	int16_t glyph_y;

	if (unicode == '\t') {
		unicode = ' ';
	}
	if (font == NULL || font->dsc == NULL) {
		return 0;
	}
	fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;
	gid = get_glyph_dsc_id(font, unicode);
	if (gid == 0u || fdsc->glyph_dsc == NULL || fdsc->glyph_bitmap == NULL) {
		return 0;
	}
	gdsc = &fdsc->glyph_dsc[gid];
	adv = (uint16_t)((gdsc->adv_w + 8u) >> 4);
	if (gdsc->box_w == 0u || gdsc->box_h == 0u) {
		return adv;
	}
	bitmap = &fdsc->glyph_bitmap[gdsc->bitmap_index];
	fg = mcu_font_color_to_lcd(Font_Color);
	bg = mcu_font_color_to_lcd(Backdrop_Color);

	glyph_x = gdsc->ofs_x;
	glyph_y = (int16_t)font->line_height - (int16_t)font->base_line -
	          (int16_t)gdsc->box_h - (int16_t)gdsc->ofs_y;
	LCD_Begin_Glyph_Window(g_font_orient,
	                       (uint16_t)((int32_t)x + glyph_x),
	                       (uint16_t)((int32_t)y + glyph_y),
	                       gdsc->box_w, gdsc->box_h);
	for (row = 0u; row < gdsc->box_h; row++) {
		for (col = 0u; col < gdsc->box_w; col++) {
			bit_index = (row * gdsc->box_w) + col;
			LCD_Write_DAT16(mcu_font_next_pixel(bitmap, bit_index, fdsc->bpp) ? fg : bg);
		}
	}
	return adv;
}

uint32_t Show_Str_Get_Width(const char *txt, const lv_font_t *font)
{
	uint32_t i = 0;
	uint32_t x_add = 0;
	uint32_t unicode;
	uint32_t gid;
	lv_font_fmt_txt_dsc_t *fdsc;

	if (txt == NULL || font == NULL || font->dsc == NULL) {
		return 0;
	}
	fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;
	do {
		unicode = lv_txt_utf8_next(txt, &i);
		gid = get_glyph_dsc_id(font, unicode);
		if (gid != 0u && fdsc->glyph_dsc != NULL) {
			x_add += (fdsc->glyph_dsc[gid].adv_w + 8u) >> 4;
		}
	} while (unicode != 0u);
	return x_add;
}

void Show_MCU_Str(uint16_t x, uint16_t y, const char *txt, const lv_font_t *font)
{
	uint32_t i = 0;
	uint32_t dx;
	uint32_t unicode;
	uint32_t unicodes[MCU_FONT_MAX_CHARS];
	uint32_t count = 0;
	uint32_t idx;
	uint16_t cx = x;

	if (txt == NULL || font == NULL || font->dsc == NULL) {
		return;
	}
	do {
		unicode = lv_txt_utf8_next(txt, &i);
		if (unicode != 0u && count < MCU_FONT_MAX_CHARS) {
			unicodes[count] = unicode;
			count++;
		}
	} while (unicode != 0u && count < MCU_FONT_MAX_CHARS);
	if (count == 0u) {
		return;
	}

	for (idx = 0u; idx < count; idx++) {
		dx = Show_MCU_Font_Fast(cx, y, unicodes[idx], font);
		cx = (uint16_t)(cx + dx);
	}
	LCD_String_Write_End();
}

void Show_MCU_Str_Middle(uint16_t x, uint16_t y, const char *txt, const lv_font_t *font)
{
	uint32_t dx;

	if (txt == NULL || font == NULL) {
		return;
	}
	dx = Show_Str_Get_Width(txt, font) / 2u;
	if (dx > x) {
		x = 0u;
	} else {
		x = (uint16_t)(x - dx);
	}
	Show_MCU_Str(x, y, txt, font);
}
