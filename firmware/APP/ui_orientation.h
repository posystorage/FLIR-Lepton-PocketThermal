#ifndef _UI_ORIENTATION_H_
#define _UI_ORIENTATION_H_

#include <stdint.h>

typedef enum {
	UI_ORIENTATION_0 = 0,
	UI_ORIENTATION_90 = 90,
	UI_ORIENTATION_180 = 180,
	UI_ORIENTATION_270 = 270,
} ui_orientation_t;

uint8_t UI_OrientationToLcdCode(ui_orientation_t orientation);
uint8_t UI_OrientationIsPortrait(ui_orientation_t orientation);

#endif
