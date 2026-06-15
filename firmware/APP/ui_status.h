#ifndef _UI_STATUS_H_
#define _UI_STATUS_H_

#include <stdint.h>

void UI_StatusDrawStatic(void);
void UI_StatusDrawSystem(void);
void UI_StatusDrawDividers(void);
void UI_StatusCapture(void);
uint8_t UI_StatusHasChanged(void);

#endif
