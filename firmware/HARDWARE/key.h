#ifndef _KEY_H
#define _KEY_H
#include "M480.h"

#define KEY1_ID 0
#define KEY2_ID 1
#define KEY3_ID 2
#define KEY4_ID 3
#define KEY5_ID 4
#define KEY_COUNT 5

typedef enum {
    KEY_EVENT_NONE,
    KEY_EVENT_PRESS,
    KEY_EVENT_LONG,
    KEY_EVENT_REPEAT,
    KEY_EVENT_RELEASE,
} key_event_type_t;

typedef struct {
    uint8_t key_id;
    key_event_type_t event;
} key_event_t;

void Key_Init(void);
void Key_Scan(void);
uint8_t Key_GetEvent(key_event_t *ev);

#endif
