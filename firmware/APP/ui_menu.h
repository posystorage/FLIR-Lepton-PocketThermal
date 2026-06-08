#ifndef _UI_MENU_H_
#define _UI_MENU_H_

#include <stdint.h>

uint8_t UI_MenuEmissCount(void);
uint16_t UI_MenuEmissAt(uint8_t idx);
uint8_t UI_MenuFindEmiss(uint16_t emiss_x100);

#endif
