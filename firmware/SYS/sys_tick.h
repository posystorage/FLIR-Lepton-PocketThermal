#ifndef __SYS_TICK_H
#define __SYS_TICK_H
#include "M480.h"

extern volatile uint32_t g_sys_tick_ms;

void Timer0_Init(void);
uint32_t GetTick(void);

#endif
