#ifndef _UI_ICONS_H_
#define _UI_ICONS_H_

#include <stdint.h>

typedef struct {
    uint8_t w;
    uint8_t h;
    const uint16_t *rows;
} ui_bitmap_t;

extern const ui_bitmap_t ui_icon_sd;
extern const ui_bitmap_t ui_icon_usb;
extern const ui_bitmap_t ui_icon_power;
extern const ui_bitmap_t ui_icon_temp_point;
extern const ui_bitmap_t ui_icon_camera;
extern const ui_bitmap_t ui_icon_cancel;
extern const ui_bitmap_t ui_icon_check;

#endif
