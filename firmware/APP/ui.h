#ifndef _UI_H_
#define _UI_H_

#include <stdint.h>
#include "key.h"
#include "temp_measure.h"
#include "ui_orientation.h"

void UI_Init(void);
void UI_Service(void);
void UI_OnKeyEvent(const key_event_t *event);
void UI_OnOrientationChanged(ui_orientation_t orientation);
void UI_OnTemperatureFrame(const temp_points_t *points);
void UI_DrawThermalFrameRgb565(uint8_t gray[240][320]);
void UI_DrawImageOverlay(void);

#endif
