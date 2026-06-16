#include "power_manager.h"
#include "sys_tick.h"
#include "delay.h"
#include "lp3921.h"
#include "lepton.h"
#include "lcd.h"
#include "key.h"
#include "eadc.h"
#include "sys.h"
#include "clk.h"
#include "debug.h"

/* ── 参数配置 ── */
#define BATTERY_LOW_PERCENT      10u
#define BATTERY_CRIT_PERCENT      5u

/* ── ADC 测量状态 ── */
#define AUTO_OFF_DEFAULT_MIN     5u
#define AUTO_OFF_NEVER_MIN       0u
#define POWER_WARN_MS         10000u

typedef enum {
    ADC_IDLE,
    ADC_STABILIZE,
    ADC_WAIT,
} adc_state_t;

/* ── 内部状态 ── */
static power_state_t g_state = POWER_ON;
static uint32_t g_idle_tick = 0;
static uint8_t  g_battery_pct = 50;
static uint8_t  g_charging = 0;
static uint16_t g_auto_off_minutes = AUTO_OFF_DEFAULT_MIN;
static uint8_t  g_warn_from_auto_off = 0;
static uint32_t g_warn_start_tick = 0;

/* ── ADC 相关 ── */
static adc_state_t g_adc_state = ADC_IDLE;
static uint32_t   g_adc_tick = 0;
static uint32_t   g_next_measure_tick = 0;
static uint16_t   g_battery_mv = 3700;

/* ── 电池电压→电量百分比 查找表 (4.2V 锂电池放电曲线) ── */
typedef struct {
    uint16_t mv;
    uint8_t  pct;
} batt_lut_entry_t;

static const batt_lut_entry_t g_batt_lut[] = {
    {4200, 100},
    {4130,  90},
    {4060,  80},
    {3980,  70},
    {3920,  60},
    {3870,  50},
    {3820,  40},
    {3770,  30},
    {3720,  20},
    {3650,  10},
    {3500,   5},
    {3300,   0},
};
#define BATT_LUT_SIZE (sizeof(g_batt_lut) / sizeof(g_batt_lut[0]))

/* ── 电压→百分比 (线性插值查表) ── */
static uint8_t voltage_to_pct(uint16_t mv)
{
    uint8_t i;
    if (mv >= g_batt_lut[0].mv) return 100;
    if (mv <= g_batt_lut[BATT_LUT_SIZE - 1].mv) return 0;

    for (i = 0; i < BATT_LUT_SIZE - 1; i++) {
        if (mv <= g_batt_lut[i].mv && mv >= g_batt_lut[i + 1].mv) {
            const batt_lut_entry_t *hi = &g_batt_lut[i];
            const batt_lut_entry_t *lo = &g_batt_lut[i + 1];
            int32_t mv_range = (int32_t)hi->mv - (int32_t)lo->mv;
            int32_t pct_range = (int32_t)hi->pct - (int32_t)lo->pct;
            if (mv_range <= 0) return lo->pct;
            return (uint8_t)(lo->pct + pct_range * ((int32_t)mv - lo->mv) / mv_range);
        }
    }
    return 0;
}

/* ── 初始化 ── */
void Power_Init(void)
{
		Battery_ADC_Init();
    g_state = POWER_ON;
    g_idle_tick = GetTick();
    //g_shutdown_req = 0;
    g_next_measure_tick = GetTick() + BATT_MEASURE_INTERVAL_MS;
}

/* ── 复位空闲计时器 ── */
void Power_ResetIdleTimer(void)
{
    g_idle_tick = GetTick();
    if (g_state == POWER_AUTO_OFF_WARN) {
        g_state = POWER_ON;
        g_warn_from_auto_off = 0u;
    }
}

void Power_SetAutoOffMinutes(uint16_t minutes)
{
    if (minutes != 0u && minutes != 3u && minutes != 5u &&
        minutes != 10u && minutes != 20u && minutes != 30u &&
        minutes != 60u) {
        minutes = AUTO_OFF_DEFAULT_MIN;
    }
    g_auto_off_minutes = minutes;
    Power_ResetIdleTimer();
}

uint16_t Power_GetAutoOffMinutes(void)
{
    return g_auto_off_minutes;
}

/* ── 关机序列 ── */
void Power_Shutdown(void)
{
    if (g_state >= POWER_SHUTDOWN) return;
    g_state = POWER_SHUTDOWN;
}

/* ── 状态读取 ── */
power_state_t Power_GetState(void)       { return g_state; }
uint8_t Power_GetBatteryPercent(void)    { return g_battery_pct; }
uint8_t Power_IsCharging(void)           { return g_charging; }

/* ── 电池电压读取 (mV) ── */
uint16_t Battery_GetVoltage_mV(void)
{
    return g_battery_mv;
}

/* ── 按键事件接口 ── */
void Power_OnKeyEvent(uint8_t key_id, uint8_t event)
{
    if (event == KEY_EVENT_PRESS || event == KEY_EVENT_LONG) {
        Power_ResetIdleTimer();
    }
    if (key_id == KEY1_ID && event == KEY_EVENT_LONG) {
				PM_DEBUG("key Off");
        Power_Shutdown();
    }
}

/* ── 执行关机动作 ── */
static void do_shutdown(void)
{
    LP3921_DISABLE_CAM();
    Lepton_Deinit();
    IIC0_Deinit();
    LCD_BK_DUTY_CYCLE(0);
    delay_ms(50);
    LP3921_DISABLE_PER();
    delay_ms(10);
    g_state = POWER_OFF;
    LP3921_SYS_PWR_OFF();
    __WFI();
    while(1){}
}

/* ── 电池ADC初始化 ── */
void Battery_ADC_Init(void)
{
    /* PF10: 测量控制引脚 (推挽输出, 默认低→MOSFET关断) */
    SYS->GPF_MFPH = (SYS->GPF_MFPH & ~SYS_GPF_MFPH_PF10MFP_Msk) | SYS_GPF_MFPH_PF10MFP_GPIO;
    GPIO_SetMode(PF, BIT10, GPIO_MODE_OUTPUT);
    PF10 = 0;

    /* PF9: 充电指示灯 (推挽输出, 默认高→LED灭) */
    SYS->GPF_MFPH = (SYS->GPF_MFPH & ~SYS_GPF_MFPH_PF9MFP_Msk) | SYS_GPF_MFPH_PF9MFP_GPIO;
    GPIO_SetMode(PF, BIT9, GPIO_MODE_OUTPUT);
    PF9 = 1;

    /* PB1: ADC输入 (EADC0_CH1) */
    SYS->GPB_MFPL = (SYS->GPB_MFPL & ~SYS_GPB_MFPL_PB1MFP_Msk) | SYS_GPB_MFPL_PB1MFP_EADC0_CH1;
    GPIO_SetMode(PB, BIT1, GPIO_MODE_INPUT);
    GPIO_SetPullCtl(PB, BIT1, GPIO_PUSEL_DISABLE);

    /* 使能EADC时钟 */
    CLK->APBCLK0 |= CLK_APBCLK0_EADCCKEN_Msk;

    /* 开启EADC, 单端输入 */
    EADC_Open(EADC, EADC_CTL_DIFFEN_SINGLE_END);

    /* 配置采样模块0: 软件触发, 通道1(PB1) */
    EADC_ConfigSampleModule(EADC, 0, EADC_SOFTWARE_TRIGGER, EADC_SCTL_CHSEL(1));

    g_adc_state = ADC_IDLE;
}

/* ── ADC 状态机 (非阻塞, 由 Power_Service 调用) ── */
void Battery_ADC_Service(void)
{
    switch (g_adc_state) {
    case ADC_IDLE: {
        uint32_t now = GetTick();
        if ((int32_t)(now - g_next_measure_tick) < 0) {
            break;
        }
        g_next_measure_tick = now + BATT_MEASURE_INTERVAL_MS;
        PF10 = 1;
        g_adc_tick = now;
        g_adc_state = ADC_STABILIZE;
        break;
    }

    case ADC_STABILIZE:
        if (GetTick() - g_adc_tick >= BATT_STABILIZE_MS) {
            /* 稳定完成, 启动ADC转换 */
            EADC_START_CONV(EADC, 1UL << 0);
            g_adc_state = ADC_WAIT;
        }
        break;

    case ADC_WAIT:
        if (EADC_GET_DATA_VALID_FLAG(EADC, 1UL << 0)) {
            /* 转换完成, 读取结果 */
            uint32_t raw = EADC_GET_CONV_DATA(EADC, 0);
            float vbat;

            /* 读取后立即关闭测量 MOSFET */
            PF10 = 0;

            /* ADC值→电压: raw * VREF / 4096 * 分压比 * 校准系数 */
            vbat = (float)raw * (float)BATT_VREF_MV / 4096.0f;
            vbat = vbat * BATT_DIVIDER_RATIO * BATT_CALIBRATION;
            g_battery_mv = (uint16_t)vbat;

            /* 电量百分比 */
            g_battery_pct = voltage_to_pct(g_battery_mv);
						PM_DEBUG("battery: %d mV, pct: %d%%", g_battery_mv, g_battery_pct);
            g_adc_state = ADC_IDLE;
        }
        break;

    default:
        g_adc_state = ADC_IDLE;
        break;
    }
}

/* ── 电池综合更新 ── */
static void update_battery(void)
{
    /* 充电状态 */
    g_charging = LP3921_Get_Charge_Sate();

    /* 充电指示灯 PF9: 充电中拉低亮灯 */
    if (g_charging == 1) {
        PF9 = 0;
    } else {
        PF9 = 1;
    }

    /* ADC 测量状态机 */
    Battery_ADC_Service();
}

/* ── 主服务 ── */
void Power_Service(void)
{
    update_battery();

    switch (g_state)
    {
    case POWER_ON:
    {
        uint32_t elapsed = GetTick() - g_idle_tick;
        uint32_t auto_off_ms = (uint32_t)g_auto_off_minutes * 60000u;
//        if (g_battery_pct < BATTERY_CRIT_PERCENT) {
//            Power_Shutdown();
//            break;
//        }
				if(g_charging!=0)
				{
					Power_ResetIdleTimer();//如果在充电，则不会自动关闭
				}
        if (g_auto_off_minutes != AUTO_OFF_NEVER_MIN &&
            elapsed >= auto_off_ms - POWER_WARN_MS * 2u) {
            g_state = POWER_AUTO_OFF_WARN;
            g_warn_from_auto_off = 1u;
            g_warn_start_tick = GetTick();
						PM_DEBUG("Auto Off");
        }
        if (g_battery_mv < BATTERY_LOW_MV && g_battery_mv > 0) {
            g_state = POWER_AUTO_OFF_WARN;
            g_warn_from_auto_off = 0u;
            g_warn_start_tick = GetTick();
						PM_DEBUG("Low Power");
        }
        break;
    }

    case POWER_AUTO_OFF_WARN:
    {
        uint32_t elapsed = GetTick() - g_idle_tick;
        uint32_t auto_off_ms = (uint32_t)g_auto_off_minutes * 60000u;
        if (g_warn_from_auto_off && g_auto_off_minutes == AUTO_OFF_NEVER_MIN) {
            g_state = POWER_ON;
            g_warn_from_auto_off = 0u;
        } else if (g_warn_from_auto_off &&
                   elapsed >= auto_off_ms) {
            do_shutdown();
        } else if (!g_warn_from_auto_off &&
                   (int32_t)(GetTick() - g_warn_start_tick) >=
                   (int32_t)POWER_WARN_MS) {
            do_shutdown();
        }
        break;
    }

    case POWER_SHUTDOWN:
        do_shutdown();
        break;

    case POWER_OFF:
    default:
        break;
    }
}
