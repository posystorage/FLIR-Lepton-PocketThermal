#ifndef _UI_MENU_H_
#define _UI_MENU_H_

#include <stdint.h>
#include "key.h"
#include "ui_input.h"

#define UI_MENU_RESULT_NONE       0u
#define UI_MENU_RESULT_CLOSED     (1u << 0)
#define UI_MENU_RESULT_EMISS      (1u << 1)
#define UI_MENU_RESULT_LUT        (1u << 2)
#define UI_MENU_RESULT_TEMP       (1u << 3)
#define UI_MENU_RESULT_COLOR      (1u << 4)
#define UI_MENU_RESULT_POWER      (1u << 5)

typedef enum {
	UI_MENU_NONE = 0,
	UI_MENU_EMISSIVITY,
	UI_MENU_TEMPERATURE,
	UI_MENU_LUT,
	UI_MENU_AUTO_OFF,
	UI_MENU_POINT_EDIT,
} ui_menu_page_t;

void UI_MenuInit(void);
void UI_MenuOpen(ui_menu_page_t page);
uint8_t UI_MenuIsActive(void);
uint8_t UI_MenuHandleKey(ui_key_t key, key_event_type_t event);
void UI_MenuDraw(void);

uint8_t UI_MenuEmissCount(void);
uint16_t UI_MenuEmissAt(uint8_t index);
uint8_t UI_MenuFindEmiss(uint16_t emiss_x100);
uint16_t UI_MenuAutoOffAt(uint8_t index);
uint8_t UI_MenuFindAutoOff(uint16_t minutes);

#endif
