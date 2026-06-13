#ifndef _UI_THERMAL_H_
#define _UI_THERMAL_H_

#include <stdint.h>

void UI_ThermalDrawLutBody(void);
void UI_ThermalDrawLutValues(void);
void UI_ThermalDrawFrame(uint8_t gray[240][320]);
void UI_ThermalDrawOverlay(void);

#endif
