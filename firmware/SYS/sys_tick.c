#include "sys_tick.h"
#include "timer.h"   /* BSP StdDriver: TIMER_Open / TIMER_EnableInt / ... */
#include "key.h"
#include "mpu6050.h"

volatile uint32_t g_sys_tick_ms = 0;

void Timer0_Init(void)
{
    /* 使能 TIMER0 时钟 */
    CLK->APBCLK0 |= CLK_APBCLK0_TMR0CKEN_Msk;

    /* 时钟源选择 PCLK0 (96MHz) */
    CLK->CLKSEL1 = (CLK->CLKSEL1 & ~CLK_CLKSEL1_TMR0SEL_Msk);

    /* 周期模式, 1000Hz (1ms) */
    TIMER_Open(TIMER0, TIMER_PERIODIC_MODE, 1000);

    /* 使能中断 */
    TIMER_EnableInt(TIMER0);
    NVIC_EnableIRQ(TMR0_IRQn);

    /* 启动 */
    TIMER_Start(TIMER0);
	
	
    //GPIO_SetMode(PA, BIT12, GPIO_MODE_OUTPUT);//debug
}

uint32_t GetTick(void)
{
    uint32_t tick;
    do {
        tick = g_sys_tick_ms;
    } while (tick != g_sys_tick_ms); // 防撕裂读
    return tick;
}

void TMR0_IRQHandler(void)
{
    TIMER_ClearIntFlag(TIMER0);
    g_sys_tick_ms++;

    /* 每 10ms 扫描一次按键 + 标记MPU6050就绪 */
    if ((g_sys_tick_ms % 10) == 0) {
        Key_Scan();
        g_mpu_tick_flag = 1;
    }
}
