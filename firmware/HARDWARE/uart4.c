#include "uart4.h"
#include "uart.h"
#include "stdio.h"

void Uart4_Init(void)
{
	/* UART4 TX pin on PA.12 */
	SYS->GPA_MFPH &= 0xfff0ffff;
  SYS->GPA_MFPH |= SYS_GPA_MFPH_PA12MFP_UART4_TXD;
	CLK->APBCLK0 |= CLK_APBCLK0_UART4CKEN_Msk;
 /* Select UART clock source is PLL */
	CLK->CLKSEL3 = (CLK->CLKSEL3 & ~CLK_CLKSEL3_UART4SEL_Msk) | (0x1 << CLK_CLKSEL3_UART4SEL_Pos);
	UART_Open(UART4, 921600);
}


void Uart4_TX(uint8_t* dat,uint32_t num)
{
	UART_Write(UART4, dat,num);
}

/* printf 重定向到 UART4 (MicroLIB 只需要 fputc) */
int fputc(int ch, FILE *f)
{
    while ((UART4->FIFOSTS & UART_FIFOSTS_TXEMPTY_Msk) == 0) {}
    UART4->DAT = (uint8_t)ch;
    return ch;
}
