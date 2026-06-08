#ifndef _POWER_MANAGER_H
#define _POWER_MANAGER_H
#include "M480.h"

/* ── 电池ADC参数 ── */
#define BATT_STABILIZE_MS       50     /* PF10开启后稳定等待时间 */
#define BATT_MEASURE_INTERVAL_MS 500   /* 测量间隔 */
#define BATT_VREF_MV            3300    /* ADC参考电压(mV) */
#define BATT_CALIBRATION        1.0f    /* 电压校准系数 */
#define BATT_DIVIDER_RATIO      2.0f    /* 分压比 (电池→ADC) */
#define BATTERY_LOW_MV          3100    /* 低电压关机阈值(mV) */

typedef enum {
    POWER_ON,
    POWER_AUTO_OFF_WARN,
    POWER_SHUTDOWN,
    POWER_OFF,
} power_state_t;

void Power_Init(void);
void Power_Service(void);
void Power_ResetIdleTimer(void);
void Power_Shutdown(void);

power_state_t Power_GetState(void);
uint8_t Power_GetBatteryPercent(void);
uint8_t Power_IsCharging(void);

/* 由 main.c 按键事件处理中调用 */
void Power_OnKeyEvent(uint8_t key_id, uint8_t event);

/* ── 电池ADC接口 ── */
void Battery_ADC_Init(void);
void Battery_ADC_Service(void);
uint16_t Battery_GetVoltage_mV(void);

#endif
