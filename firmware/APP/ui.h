#ifndef _UI_H_
#define _UI_H_

#include <stdint.h>
#include "key.h"

#define UI_ORIENT_LANDSCAPE_0    1u
#define UI_ORIENT_LANDSCAPE_180  0u
#define UI_ORIENT_PORTRAIT_90    2u
#define UI_ORIENT_PORTRAIT_270   3u

#define UI_DIRTY_KEYBAR       (1u << 0)
#define UI_DIRTY_SYS_STATUS   (1u << 2)
#define UI_DIRTY_TEMP         (1u << 3)
#define UI_DIRTY_EMISS        (1u << 4)
#define UI_DIRTY_LUT_BODY     (1u << 5)
#define UI_DIRTY_LUT_VALUES   (1u << 6)
#define UI_DIRTY_MENU         (1u << 7)
#define UI_DIRTY_TOAST        (1u << 8)
#define UI_DIRTY_ALL          0xFFFFFFFFu

void UI_Init(void);
void UI_Service(void);
void UI_OnKeyEvent(const key_event_t *ev);
void UI_OnOrientationChanged(uint8_t orient);
void UI_RequestRedraw(uint32_t dirty_flags);
void UI_DrawThermalFrameRgb565(uint8_t gray[240][320]);
void UI_DrawImageOverlay(void);

#endif
