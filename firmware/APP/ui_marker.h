#ifndef _UI_MARKER_H_
#define _UI_MARKER_H_

#include <stdint.h>
#include "temp_measure.h"

#define UI_MARKER_COLOR_COUNT 10u

void UI_MarkerInit(void);
uint16_t UI_MarkerGetColor(temp_point_id_t id);
uint8_t UI_MarkerGetColorIndex(temp_point_id_t id);
void UI_MarkerSetColorIndex(temp_point_id_t id, uint8_t color_index);
const char *UI_MarkerColorName(uint8_t color_index);
uint16_t UI_MarkerColorValue(uint8_t color_index);
void UI_MarkerDrawPanel(uint16_t x, uint16_t y, temp_point_id_t id);
void UI_MarkerDrawThermal(uint16_t center_x, uint16_t center_y,
                          temp_point_id_t id);

#endif
