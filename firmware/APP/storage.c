#include "storage.h"
#include "image_proc.h"
#include "color_palette.h"
#include "temp_measure.h"
#include "sdcard.h"
#include "sys_tick.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define STORAGE_BMP_W          344u
#define STORAGE_BMP_H          240u
#define STORAGE_BMP_LINE_BYTES (STORAGE_BMP_W * 3u)
#define STORAGE_BMP_ROWS_STEP  4u
#define STORAGE_NAME_STEP      8u
#define STORAGE_DIR            "0:/FLIR"

typedef enum {
	STORAGE_IDLE = 0,
	STORAGE_PREPARE_DIR,
	STORAGE_FIND_NAME,
	STORAGE_OPEN_BMP,
	STORAGE_WRITE_BMP_HEADER,
	STORAGE_WRITE_BMP_ROWS,
	STORAGE_CLOSE_BMP,
	STORAGE_OPEN_RAW,
	STORAGE_WRITE_RAW,
	STORAGE_CLOSE_RAW,
	STORAGE_OPEN_TXT,
	STORAGE_WRITE_TXT,
	STORAGE_CLOSE_TXT,
	STORAGE_DONE,
	STORAGE_ERROR,
} storage_state_t;

static storage_state_t g_state = STORAGE_IDLE;
static storage_result_t g_last_result = STORAGE_RESULT_NONE;
static FIL g_file;
static uint8_t g_file_open = 0;
static uint8_t g_cancel_req = 0;
static uint32_t g_seq = 0;
static uint32_t g_name_tries = 0;
static uint32_t g_bmp_row = 0;
static uint8_t g_bmp_line[STORAGE_BMP_LINE_BYTES];
static char g_bmp_name[48];
static char g_raw_name[48];
static char g_txt_name[48];
static const uint16_t (*g_raw)[80];
static uint8_t (*g_gray)[320];

static void storage_put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)(v >> 8);
}

static void storage_put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void storage_close_file(void)
{
	if (g_file_open) {
		(void)f_close(&g_file);
		g_file_open = 0u;
	}
}

static void storage_set_error(storage_result_t result)
{
	storage_close_file();
	g_last_result = result;
	g_state = STORAGE_IDLE;
}

static void storage_format_names(uint32_t seq)
{
	(void)snprintf(g_bmp_name, sizeof(g_bmp_name), STORAGE_DIR "/IMG_%06lu.bmp", (unsigned long)seq);
	(void)snprintf(g_raw_name, sizeof(g_raw_name), STORAGE_DIR "/RAW_%06lu.bin", (unsigned long)seq);
	(void)snprintf(g_txt_name, sizeof(g_txt_name), STORAGE_DIR "/DAT_%06lu.txt", (unsigned long)seq);
}

static uint8_t storage_path_is_free(const char *path)
{
	FILINFO fno;
	FRESULT res;

	res = f_stat(path, &fno);
	return (res == FR_NO_FILE) ? 1u : 0u;
}

static uint8_t storage_make_unique_names_step(void)
{
	uint8_t step;

	for (step = 0u; step < STORAGE_NAME_STEP && g_name_tries < 999999u; step++) {
		if (g_seq >= 999999u) {
			g_seq = 0u;
		}
		g_seq++;
		g_name_tries++;
		storage_format_names(g_seq);
		if (storage_path_is_free(g_bmp_name) &&
		    storage_path_is_free(g_raw_name) &&
		    storage_path_is_free(g_txt_name)) {
			return 1u;
		}
	}
	return 0u;
}

static FRESULT storage_open_new(FIL *fil, const char *path)
{
	return f_open(fil, path, FA_CREATE_NEW | FA_WRITE);
}

static void storage_make_bmp_header(uint8_t header[54])
{
	uint32_t data_size = STORAGE_BMP_LINE_BYTES * STORAGE_BMP_H;
	uint32_t file_size = 54u + data_size;

	memset(header, 0, 54u);
	header[0] = 'B';
	header[1] = 'M';
	storage_put_le32(&header[2], file_size);
	storage_put_le32(&header[10], 54u);
	storage_put_le32(&header[14], 40u);
	storage_put_le32(&header[18], STORAGE_BMP_W);
	storage_put_le32(&header[22], STORAGE_BMP_H);
	storage_put_le16(&header[26], 1u);
	storage_put_le16(&header[28], 24u);
	storage_put_le32(&header[34], data_size);
}

static void storage_make_bmp_line(uint32_t logical_y)
{
	const rgb888_t *pal = palette_get();
	uint32_t x;
	uint8_t gray;

	for (x = 0u; x < STORAGE_BMP_W; x++) {
		rgb888_t rgb;
		uint32_t out = x * 3u;

		if (x < 24u) {
			gray = (uint8_t)(255u - (logical_y * 255u / (STORAGE_BMP_H - 1u)));
		} else {
			gray = g_gray[logical_y][x - 24u];
		}
		rgb = pal[gray];
		g_bmp_line[out + 0u] = rgb.b;
		g_bmp_line[out + 1u] = rgb.g;
		g_bmp_line[out + 2u] = rgb.r;
	}
}

static void storage_append_temp(char *buf, uint32_t size, uint32_t *pos,
                                const char *name, int16_t value)
{
	int32_t v;
	char sign = '\0';
	int n;

	if (*pos >= size) {
		return;
	}
	if (value == INT16_MIN) {
		n = snprintf(&buf[*pos], size - *pos, "%s=invalid\r\n", name);
		if (n > 0) *pos += (uint32_t)n;
		return;
	}
	v = value;
	if (v < 0) {
		sign = '-';
		v = -v;
	}
	if (sign != '\0') {
		n = snprintf(&buf[*pos], size - *pos, "%s=-%ld.%02ldC\r\n",
		             name, (long)(v / 100), (long)(v % 100));
	} else {
		n = snprintf(&buf[*pos], size - *pos, "%s=%ld.%02ldC\r\n",
		             name, (long)(v / 100), (long)(v % 100));
	}
	if (n > 0) {
		*pos += (uint32_t)n;
		if (*pos > size) {
			*pos = size;
		}
	}
}

static void storage_append_emiss(char *buf, uint32_t size, uint32_t *pos)
{
	uint16_t emiss;
	int n;

	if (*pos >= size) {
		return;
	}
	emiss = temp_get_emissivity();
	if (emiss >= 100u) {
		n = snprintf(&buf[*pos], size - *pos, "emiss=1.00\r\n");
	} else {
		n = snprintf(&buf[*pos], size - *pos, "emiss=0.%02u\r\n", (unsigned)emiss);
	}
	if (n > 0) {
		*pos += (uint32_t)n;
		if (*pos > size) {
			*pos = size;
		}
	}
}

void Storage_Init(void)
{
	g_state = STORAGE_IDLE;
	g_last_result = STORAGE_RESULT_NONE;
	g_file_open = 0u;
	g_cancel_req = 0u;
	g_seq = 0u;
	g_name_tries = 0u;
	g_bmp_row = 0u;
	g_raw = 0;
	g_gray = 0;
}

storage_result_t Storage_BeginCapture(void)
{
	if (g_state != STORAGE_IDLE) {
		return STORAGE_RESULT_BUSY;
	}
	if (!SDCard_IsInserted()) {
		g_last_result = STORAGE_RESULT_NO_CARD;
		return STORAGE_RESULT_NO_CARD;
	}
	if (SDCard_IsOwnedByMSC()) {
		g_last_result = STORAGE_RESULT_MSC_BUSY;
		return STORAGE_RESULT_MSC_BUSY;
	}
	if (!SDCard_IsMounted()) {
		g_last_result = STORAGE_RESULT_ERROR;
		return STORAGE_RESULT_ERROR;
	}
	if (!image_proc_has_frame()) {
		g_last_result = STORAGE_RESULT_NO_FRAME;
		return STORAGE_RESULT_NO_FRAME;
	}
	g_raw = image_proc_get_last_raw14();
	g_gray = image_proc_get_last_gray320();
	if (g_raw == 0 || g_gray == 0) {
		g_last_result = STORAGE_RESULT_NO_FRAME;
		return STORAGE_RESULT_NO_FRAME;
	}
	g_bmp_row = 0u;
	g_name_tries = 0u;
	g_cancel_req = 0u;
	g_last_result = STORAGE_RESULT_BUSY;
	g_state = STORAGE_PREPARE_DIR;
	return STORAGE_RESULT_BUSY;
}

void Storage_Cancel(void)
{
	if (g_state != STORAGE_IDLE) {
		g_cancel_req = 1u;
	}
}

uint8_t Storage_IsBusy(void)
{
	return (g_state != STORAGE_IDLE) ? 1u : 0u;
}

storage_result_t Storage_GetLastResult(void)
{
	return g_last_result;
}

void Storage_Service(void)
{
	UINT bw;
	FRESULT res;

	if (g_state == STORAGE_IDLE) {
		return;
	}
	if (g_cancel_req) {
		storage_set_error(STORAGE_RESULT_CANCELLED);
		return;
	}
	switch (g_state) {
	case STORAGE_PREPARE_DIR:
		res = f_mkdir(STORAGE_DIR);
		if (res != FR_OK && res != FR_EXIST) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_state = STORAGE_FIND_NAME;
		break;

	case STORAGE_FIND_NAME:
		if (storage_make_unique_names_step()) {
			g_state = STORAGE_OPEN_BMP;
		} else if (g_name_tries >= 999999u) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		break;

	case STORAGE_OPEN_BMP:
		res = storage_open_new(&g_file, g_bmp_name);
		if (res != FR_OK) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_file_open = 1u;
		g_state = STORAGE_WRITE_BMP_HEADER;
		break;

	case STORAGE_WRITE_BMP_HEADER: {
		uint8_t header[54];
		storage_make_bmp_header(header);
		res = f_write(&g_file, header, sizeof(header), &bw);
		if (res != FR_OK || bw != sizeof(header)) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_state = STORAGE_WRITE_BMP_ROWS;
		break;
	}

	case STORAGE_WRITE_BMP_ROWS: {
		uint8_t rows = 0u;
		while (g_bmp_row < STORAGE_BMP_H && rows < STORAGE_BMP_ROWS_STEP) {
			uint32_t logical_y = (STORAGE_BMP_H - 1u) - g_bmp_row;
			storage_make_bmp_line(logical_y);
			res = f_write(&g_file, g_bmp_line, STORAGE_BMP_LINE_BYTES, &bw);
			if (res != FR_OK || bw != STORAGE_BMP_LINE_BYTES) {
				storage_set_error(STORAGE_RESULT_ERROR);
				return;
			}
			g_bmp_row++;
			rows++;
		}
		if (g_bmp_row >= STORAGE_BMP_H) {
			g_state = STORAGE_CLOSE_BMP;
		}
		break;
	}

	case STORAGE_CLOSE_BMP:
		storage_close_file();
		g_state = STORAGE_OPEN_RAW;
		break;

	case STORAGE_OPEN_RAW:
		res = storage_open_new(&g_file, g_raw_name);
		if (res != FR_OK) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_file_open = 1u;
		g_state = STORAGE_WRITE_RAW;
		break;

	case STORAGE_WRITE_RAW:
		res = f_write(&g_file, g_raw, 60u * 80u * sizeof(uint16_t), &bw);
		if (res != FR_OK || bw != (60u * 80u * sizeof(uint16_t))) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_state = STORAGE_CLOSE_RAW;
		break;

	case STORAGE_CLOSE_RAW:
		storage_close_file();
		g_state = STORAGE_OPEN_TXT;
		break;

	case STORAGE_OPEN_TXT:
		res = storage_open_new(&g_file, g_txt_name);
		if (res != FR_OK) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_file_open = 1u;
		g_state = STORAGE_WRITE_TXT;
		break;

	case STORAGE_WRITE_TXT: {
		char txt[640];
		uint32_t pos = 0u;
		const temp_points_t *points = temp_get_points();
		const image_proc_stats_t *stats = image_proc_get_stats();
		int n;

		n = snprintf(txt, sizeof(txt),
		             "seq=%lu\r\ntick=%lu\r\nlut=%s\r\n",
		             (unsigned long)g_seq,
		             (unsigned long)GetTick(),
		             palette_get_name(palette_get_current_id()));
		if (n > 0) pos = (uint32_t)n;
		storage_append_emiss(txt, sizeof(txt), &pos);
		if (pos < sizeof(txt)) {
			n = snprintf(&txt[pos], sizeof(txt) - pos, "frame=%lu\r\n",
			             stats ? (unsigned long)stats->frame_count : 0ul);
			if (n > 0) {
				pos += (uint32_t)n;
				if (pos > sizeof(txt)) {
					pos = sizeof(txt);
				}
			}
		}
		if (points != 0) {
			storage_append_temp(txt, sizeof(txt), &pos, "center", points->point[TEMP_POINT_CENTER].temp_c_x100);
			storage_append_temp(txt, sizeof(txt), &pos, "max", points->point[TEMP_POINT_MAX].temp_c_x100);
			storage_append_temp(txt, sizeof(txt), &pos, "min", points->point[TEMP_POINT_MIN].temp_c_x100);
			storage_append_temp(txt, sizeof(txt), &pos, "p1", points->point[TEMP_POINT_USER1].temp_c_x100);
			storage_append_temp(txt, sizeof(txt), &pos, "p2", points->point[TEMP_POINT_USER2].temp_c_x100);
		}
		res = f_write(&g_file, txt, pos, &bw);
		if (res != FR_OK || bw != pos) {
			storage_set_error(STORAGE_RESULT_ERROR);
			return;
		}
		g_state = STORAGE_CLOSE_TXT;
		break;
	}

	case STORAGE_CLOSE_TXT:
		storage_close_file();
		g_state = STORAGE_DONE;
		break;

	case STORAGE_DONE:
		g_last_result = STORAGE_RESULT_OK;
		g_state = STORAGE_IDLE;
		break;

	case STORAGE_ERROR:
	default:
		storage_set_error(STORAGE_RESULT_ERROR);
		break;
	}
}
