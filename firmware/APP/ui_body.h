#ifndef _UI_BODY_H_
#define _UI_BODY_H_

#include <stdint.h>
#include "temp_measure.h"

#define UI_BODY_UPDATE_NONE   0u
#define UI_BODY_UPDATE_VALUES (1u << 0)
#define UI_BODY_UPDATE_FULL   (1u << 1)

void UI_BodyInit(void);
void UI_BodyPublishTemperature(const temp_points_t *points);
uint8_t UI_BodyConsumeTemperature(uint32_t now_ms);
void UI_BodyDrawFull(void);
void UI_BodyDrawTemperatureValues(void);
void UI_BodyDrawEmissivity(void);
void UI_BodySetStorageText(const char *text, uint16_t color);
void UI_BodyDrawStorage(void);
const temp_points_t *UI_BodyGetDisplayedTemperature(void);

#endif
