#include "config_store.h"
#include "eeprom_24c02.h"
#include "temp_measure.h"
#include "color_palette.h"
#include "ui_marker.h"
#include "power_manager.h"
#include "sys_tick.h"
#include <string.h>

#define CONFIG_MAGIC0          'F'
#define CONFIG_MAGIC1          'L'
#define CONFIG_MAGIC2          'I'
#define CONFIG_MAGIC3          'C'
#define CONFIG_FORMAT_VER      1u
#define CONFIG_HEADER_LEN      16u
#define CONFIG_SLOT_SIZE       128u
#define CONFIG_SLOT_A_ADDR     0u
#define CONFIG_SLOT_B_ADDR     128u
#define CONFIG_PAYLOAD_MAX     (CONFIG_SLOT_SIZE - CONFIG_HEADER_LEN)
#define CONFIG_DEBOUNCE_MS     500u
#define CONFIG_ACK_POLL_MS     2u
#define CONFIG_ACK_TIMEOUT_MS  20u

#define TLV_EMISSIVITY         0x01u
#define TLV_PALETTE            0x02u
#define TLV_AUTO_OFF_MIN       0x03u
#define TLV_POINT_ENABLE       0x10u
#define TLV_USER1_POS          0x11u
#define TLV_USER2_POS          0x12u
#define TLV_MARKER_COLOR       0x13u
#define TLV_END                0xFEu

typedef struct {
	uint8_t emiss_x100;
	uint8_t palette_id;
	uint16_t auto_off_minutes;
	uint8_t point_enabled_mask;
	uint8_t user_x[2];
	uint8_t user_y[2];
	uint8_t marker_color[TEMP_POINT_COUNT];
} app_config_t;

typedef enum {
	CONFIG_WRITE_IDLE = 0,
	CONFIG_WRITE_DEBOUNCE,
	CONFIG_WRITE_PAGE,
	CONFIG_WRITE_ACK,
	CONFIG_WRITE_VERIFY,
} config_write_state_t;

static app_config_t g_cfg;
static config_store_status_t g_status;
static config_write_state_t g_write_state;
static uint8_t g_slot[CONFIG_SLOT_SIZE];
static uint8_t g_next_slot_addr;
static uint8_t g_write_offset;
static uint8_t g_write_len;
static uint32_t g_seq;
static uint32_t g_due_tick;
static uint32_t g_ack_start_tick;
static uint8_t g_resave_pending;
static uint8_t g_io_retry;

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
	uint16_t crc = 0xFFFFu;
	uint16_t i;
	uint8_t bit;

	for (i = 0u; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (bit = 0u; bit < 8u; bit++) {
			crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) :
			      (uint16_t)(crc << 1);
		}
	}
	return crc;
}

static void set_defaults(app_config_t *cfg)
{
	cfg->emiss_x100 = 95u;
	cfg->palette_id = (uint8_t)PALETTE_ID_FUSION;
	cfg->auto_off_minutes = 5u;
	cfg->point_enabled_mask = (uint8_t)((1u << TEMP_POINT_CENTER) |
	                                    (1u << TEMP_POINT_MAX) |
	                                    (1u << TEMP_POINT_MIN));
	cfg->user_x[0] = 30u;
	cfg->user_y[0] = 30u;
	cfg->user_x[1] = 50u;
	cfg->user_y[1] = 30u;
	cfg->marker_color[TEMP_POINT_CENTER] = 0u;
	cfg->marker_color[TEMP_POINT_MAX] = 5u;
	cfg->marker_color[TEMP_POINT_MIN] = 3u;
	cfg->marker_color[TEMP_POINT_USER1] = 2u;
	cfg->marker_color[TEMP_POINT_USER2] = 4u;
}

static uint8_t valid_auto_off(uint16_t minutes)
{
	return (minutes == 0u || minutes == 3u || minutes == 5u ||
	        minutes == 10u || minutes == 20u || minutes == 30u ||
	        minutes == 60u) ? 1u : 0u;
}

static uint8_t normalize_config(app_config_t *cfg)
{
	uint8_t i;

	if (cfg->emiss_x100 < 1u || cfg->emiss_x100 > 100u) return 0u;
	if (cfg->palette_id >= (uint8_t)PALETTE_ID_COUNT) return 0u;
	if (!valid_auto_off(cfg->auto_off_minutes)) return 0u;
	if (cfg->user_x[0] >= 80u || cfg->user_x[1] >= 80u ||
	    cfg->user_y[0] >= 60u || cfg->user_y[1] >= 60u) return 0u;
	for (i = 0u; i < (uint8_t)TEMP_POINT_COUNT; i++) {
		if (cfg->marker_color[i] >= UI_MARKER_COLOR_COUNT) return 0u;
	}
	cfg->point_enabled_mask &= (uint8_t)((1u << TEMP_POINT_COUNT) - 1u);
	return 1u;
}

static void capture_runtime(app_config_t *cfg)
{
	uint8_t x;
	uint8_t y;
	uint8_t i;

	cfg->emiss_x100 = (uint8_t)temp_get_emissivity();
	cfg->palette_id = (uint8_t)palette_get_current_id();
	cfg->auto_off_minutes = Power_GetAutoOffMinutes();
	cfg->point_enabled_mask = 0u;
	for (i = 0u; i < (uint8_t)TEMP_POINT_COUNT; i++) {
		if (temp_get_point_enabled((temp_point_id_t)i)) {
			cfg->point_enabled_mask |= (uint8_t)(1u << i);
		}
		cfg->marker_color[i] =
			UI_MarkerGetColorIndex((temp_point_id_t)i);
	}
	temp_get_user_point(TEMP_POINT_USER1, &x, &y);
	cfg->user_x[0] = x;
	cfg->user_y[0] = y;
	temp_get_user_point(TEMP_POINT_USER2, &x, &y);
	cfg->user_x[1] = x;
	cfg->user_y[1] = y;
}

void ConfigStore_Apply(void)
{
	uint8_t i;

	if (!normalize_config(&g_cfg)) {
		set_defaults(&g_cfg);
		g_status = CONFIG_STORE_DEFAULTS;
	}
	temp_set_emissivity(g_cfg.emiss_x100);
	palette_init_by_id((palette_id_t)g_cfg.palette_id);
	Power_SetAutoOffMinutes(g_cfg.auto_off_minutes);
	for (i = 0u; i < (uint8_t)TEMP_POINT_COUNT; i++) {
		temp_set_point_enabled((temp_point_id_t)i,
		                       (g_cfg.point_enabled_mask & (1u << i)) ? 1u : 0u);
		UI_MarkerSetColorIndex((temp_point_id_t)i, g_cfg.marker_color[i]);
	}
	temp_set_user_point(TEMP_POINT_USER1, g_cfg.user_x[0], g_cfg.user_y[0]);
	temp_set_user_point(TEMP_POINT_USER2, g_cfg.user_x[1], g_cfg.user_y[1]);
}

static uint8_t append_tlv(uint8_t *payload, uint8_t *pos, uint8_t tag,
                          const uint8_t *value, uint8_t len)
{
	if ((uint16_t)*pos + 2u + len > CONFIG_PAYLOAD_MAX) return 0u;
	payload[(*pos)++] = tag;
	payload[(*pos)++] = len;
	memcpy(&payload[*pos], value, len);
	*pos = (uint8_t)(*pos + len);
	return 1u;
}

static uint8_t build_slot(uint8_t base_addr)
{
	uint8_t payload_pos = 0u;
	uint8_t tmp[8];
	uint16_t payload_crc;
	uint16_t header_crc;

	memset(g_slot, 0xFF, sizeof(g_slot));
	capture_runtime(&g_cfg);
	if (!normalize_config(&g_cfg)) {
		set_defaults(&g_cfg);
	}

	tmp[0] = g_cfg.emiss_x100;
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_EMISSIVITY, tmp, 1u)) return 0u;
	tmp[0] = g_cfg.palette_id;
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_PALETTE, tmp, 1u)) return 0u;
	wr16(tmp, g_cfg.auto_off_minutes);
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_AUTO_OFF_MIN, tmp, 2u)) return 0u;
	tmp[0] = g_cfg.point_enabled_mask;
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_POINT_ENABLE, tmp, 1u)) return 0u;
	tmp[0] = g_cfg.user_x[0]; tmp[1] = g_cfg.user_y[0];
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_USER1_POS, tmp, 2u)) return 0u;
	tmp[0] = g_cfg.user_x[1]; tmp[1] = g_cfg.user_y[1];
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_USER2_POS, tmp, 2u)) return 0u;
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_MARKER_COLOR, g_cfg.marker_color,
	                (uint8_t)TEMP_POINT_COUNT)) return 0u;
	tmp[0] = 0u;
	if (!append_tlv(&g_slot[CONFIG_HEADER_LEN], &payload_pos,
	                TLV_END, tmp, 0u)) return 0u;

	g_slot[0] = CONFIG_MAGIC0;
	g_slot[1] = CONFIG_MAGIC1;
	g_slot[2] = CONFIG_MAGIC2;
	g_slot[3] = CONFIG_MAGIC3;
	g_slot[4] = CONFIG_FORMAT_VER;
	g_slot[5] = CONFIG_HEADER_LEN;
	wr32(&g_slot[6], g_seq + 1u);
	wr16(&g_slot[10], payload_pos);
	payload_crc = crc16_ccitt(&g_slot[CONFIG_HEADER_LEN], payload_pos);
	wr16(&g_slot[12], payload_crc);
	wr16(&g_slot[14], 0u);
	header_crc = crc16_ccitt(g_slot, CONFIG_HEADER_LEN);
	wr16(&g_slot[14], header_crc);
	g_write_len = (uint8_t)(CONFIG_HEADER_LEN + payload_pos);
	g_next_slot_addr = base_addr;
	return 1u;
}

static uint8_t parse_payload(const uint8_t *payload, uint16_t len,
                             app_config_t *cfg)
{
	uint16_t pos = 0u;
	uint8_t seen_end = 0u;

	set_defaults(cfg);
	while (pos < len) {
		uint8_t tag;
		uint8_t tlv_len;
		const uint8_t *val;

		if (pos + 2u > len) return 0u;
		tag = payload[pos++];
		tlv_len = payload[pos++];
		if (pos + tlv_len > len) return 0u;
		val = &payload[pos];
		if (tag == TLV_EMISSIVITY && tlv_len == 1u) {
			cfg->emiss_x100 = val[0];
		} else if (tag == TLV_PALETTE && tlv_len == 1u) {
			cfg->palette_id = val[0];
		} else if (tag == TLV_AUTO_OFF_MIN && tlv_len == 2u) {
			cfg->auto_off_minutes = rd16(val);
		} else if (tag == TLV_POINT_ENABLE && tlv_len == 1u) {
			cfg->point_enabled_mask = val[0];
		} else if (tag == TLV_USER1_POS && tlv_len == 2u) {
			cfg->user_x[0] = val[0];
			cfg->user_y[0] = val[1];
		} else if (tag == TLV_USER2_POS && tlv_len == 2u) {
			cfg->user_x[1] = val[0];
			cfg->user_y[1] = val[1];
		} else if (tag == TLV_MARKER_COLOR &&
		           tlv_len == (uint8_t)TEMP_POINT_COUNT) {
			memcpy(cfg->marker_color, val, TEMP_POINT_COUNT);
		} else if (tag == TLV_END && tlv_len == 0u) {
			seen_end = 1u;
			break;
		}
		pos = (uint16_t)(pos + tlv_len);
	}
	return (seen_end && normalize_config(cfg)) ? 1u : 0u;
}

static uint8_t read_slot(uint8_t addr, app_config_t *cfg, uint32_t *seq)
{
	uint8_t buf[CONFIG_SLOT_SIZE];
	uint16_t payload_len;
	uint16_t got_header_crc;
	uint16_t got_payload_crc;

	if (!EEPROM24C02_Read(addr, buf, CONFIG_SLOT_SIZE)) return 0u;
	if (buf[0] != CONFIG_MAGIC0 || buf[1] != CONFIG_MAGIC1 ||
	    buf[2] != CONFIG_MAGIC2 || buf[3] != CONFIG_MAGIC3) return 0u;
	if (buf[4] != CONFIG_FORMAT_VER || buf[5] != CONFIG_HEADER_LEN) return 0u;
	payload_len = rd16(&buf[10]);
	if (payload_len == 0u || payload_len > CONFIG_PAYLOAD_MAX) return 0u;
	got_payload_crc = rd16(&buf[12]);
	got_header_crc = rd16(&buf[14]);
	wr16(&buf[14], 0u);
	if (crc16_ccitt(buf, CONFIG_HEADER_LEN) != got_header_crc) return 0u;
	if (crc16_ccitt(&buf[CONFIG_HEADER_LEN], payload_len) !=
	    got_payload_crc) return 0u;
	if (!parse_payload(&buf[CONFIG_HEADER_LEN], payload_len, cfg)) return 0u;
	*seq = rd32(&buf[6]);
	return 1u;
}

void ConfigStore_Init(void)
{
	app_config_t cfg_a;
	app_config_t cfg_b;
	uint32_t seq_a = 0u;
	uint32_t seq_b = 0u;
	uint8_t ok_a;
	uint8_t ok_b;

	set_defaults(&g_cfg);
	g_status = CONFIG_STORE_DEFAULTS;
	g_write_state = CONFIG_WRITE_IDLE;
	g_write_offset = 0u;
	g_write_len = 0u;
	g_next_slot_addr = CONFIG_SLOT_A_ADDR;
	g_seq = 0u;
	g_resave_pending = 0u;
	g_io_retry = 0u;

	ok_a = read_slot(CONFIG_SLOT_A_ADDR, &cfg_a, &seq_a);
	ok_b = read_slot(CONFIG_SLOT_B_ADDR, &cfg_b, &seq_b);
	if (ok_a && (!ok_b || (int32_t)(seq_a - seq_b) > 0)) {
		g_cfg = cfg_a;
		g_seq = seq_a;
		g_next_slot_addr = CONFIG_SLOT_B_ADDR;
		g_status = CONFIG_STORE_LOADED;
	} else if (ok_b) {
		g_cfg = cfg_b;
		g_seq = seq_b;
		g_next_slot_addr = CONFIG_SLOT_A_ADDR;
		g_status = CONFIG_STORE_LOADED;
	} else {
		ConfigStore_RequestSave();
	}
}

void ConfigStore_RequestSave(void)
{
	g_due_tick = GetTick() + CONFIG_DEBOUNCE_MS;
	if (g_write_state == CONFIG_WRITE_IDLE) {
		g_write_state = CONFIG_WRITE_DEBOUNCE;
	} else if (g_write_state != CONFIG_WRITE_DEBOUNCE) {
		g_resave_pending = 1u;
	}
}

uint8_t ConfigStore_IsBusy(void)
{
	return (g_write_state != CONFIG_WRITE_IDLE) ? 1u : 0u;
}

config_store_status_t ConfigStore_GetStatus(void)
{
	return g_status;
}

void ConfigStore_Service(void)
{
	uint32_t now = GetTick();

	if (g_write_state == CONFIG_WRITE_IDLE) return;
	if (g_write_state == CONFIG_WRITE_DEBOUNCE) {
		if ((int32_t)(now - g_due_tick) < 0) return;
		if (!build_slot(g_next_slot_addr)) {
			g_status = CONFIG_STORE_ERROR;
			g_write_state = CONFIG_WRITE_IDLE;
			return;
		}
		g_write_offset = 0u;
		g_io_retry = 0u;
		g_write_state = CONFIG_WRITE_PAGE;
	}
	if (g_write_state == CONFIG_WRITE_PAGE) {
		uint8_t page_len = (uint8_t)(EEPROM_24C02_PAGE_SIZE -
			((g_next_slot_addr + g_write_offset) &
			 (EEPROM_24C02_PAGE_SIZE - 1u)));
		if (g_io_retry != 0u && (int32_t)(now - g_due_tick) < 0) {
			return;
		}
		if ((uint16_t)g_write_offset + page_len > g_write_len) {
			page_len = (uint8_t)(g_write_len - g_write_offset);
		}
		if (page_len == 0u) {
			g_write_state = CONFIG_WRITE_VERIFY;
			return;
		}
		if (!EEPROM24C02_WritePage((uint8_t)(g_next_slot_addr + g_write_offset),
		                           &g_slot[g_write_offset], page_len)) {
			if (++g_io_retry >= 5u) {
				g_status = CONFIG_STORE_ERROR;
				g_write_state = CONFIG_WRITE_IDLE;
				g_resave_pending = 0u;
			} else {
				g_due_tick = now + CONFIG_ACK_POLL_MS;
			}
			return;
		}
		g_io_retry = 0u;
		g_write_offset = (uint8_t)(g_write_offset + page_len);
		g_ack_start_tick = now;
		g_due_tick = now + CONFIG_ACK_POLL_MS;
		g_write_state = CONFIG_WRITE_ACK;
		return;
	}
	if (g_write_state == CONFIG_WRITE_ACK) {
		if ((int32_t)(now - g_due_tick) < 0) return;
		if (EEPROM24C02_IsReady()) {
			g_write_state = CONFIG_WRITE_PAGE;
			return;
		}
		if ((int32_t)(now - g_ack_start_tick) >=
		    (int32_t)CONFIG_ACK_TIMEOUT_MS) {
			g_status = CONFIG_STORE_ERROR;
			g_write_state = CONFIG_WRITE_IDLE;
		}
		return;
	}
	if (g_write_state == CONFIG_WRITE_VERIFY) {
		app_config_t verify_cfg;
		uint32_t verify_seq;
		if (read_slot(g_next_slot_addr, &verify_cfg, &verify_seq)) {
			g_cfg = verify_cfg;
			g_seq = verify_seq;
			g_next_slot_addr = (g_next_slot_addr == CONFIG_SLOT_A_ADDR) ?
			                   CONFIG_SLOT_B_ADDR : CONFIG_SLOT_A_ADDR;
			g_status = CONFIG_STORE_LOADED;
		} else {
			g_status = CONFIG_STORE_ERROR;
		}
		if (g_resave_pending) {
			g_resave_pending = 0u;
			g_due_tick = GetTick() + CONFIG_DEBOUNCE_MS;
			g_write_state = CONFIG_WRITE_DEBOUNCE;
		} else {
			g_write_state = CONFIG_WRITE_IDLE;
		}
	}
}
