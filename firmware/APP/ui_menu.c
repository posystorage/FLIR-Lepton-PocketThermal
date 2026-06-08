#include "ui_menu.h"

static const uint16_t g_emiss_values[] = {
	100u, 98u, 95u, 93u, 90u, 85u, 80u, 75u, 70u, 60u, 50u
};

uint8_t UI_MenuEmissCount(void)
{
	return (uint8_t)(sizeof(g_emiss_values) / sizeof(g_emiss_values[0]));
}

uint16_t UI_MenuEmissAt(uint8_t idx)
{
	if (idx >= UI_MenuEmissCount()) {
		idx = UI_MenuFindEmiss(95u);
	}
	return g_emiss_values[idx];
}

uint8_t UI_MenuFindEmiss(uint16_t emiss_x100)
{
	uint8_t i;

	for (i = 0u; i < UI_MenuEmissCount(); i++) {
		if (g_emiss_values[i] == emiss_x100) {
			return i;
		}
	}
	return 2u;
}
