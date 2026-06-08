#include "system_init.h"
#include "lp3921.h"

//POWER_EN PB4
void Power_Base_Init(void)//基础电源初始化 保障系统可正常运行
{
	LP3921_SYS_PWR_Init_ON();
}

void Power_Advanced_Init(void)//高级电源初始化 设置各项参数的具体值 在时钟初始化之后进行
{
	LP3921_PWRUP_Init();
}

void CLK_Init(void)
{
	uint32_t u32HIRCSTB=0UL;

	/* Set XT1_OUT(PF.2) and XT1_IN(PF.3) to input mode */
	PF->MODE &= ~(GPIO_MODE_MODE2_Msk | GPIO_MODE_MODE3_Msk);

	/* Unlock protected registers */
	SYS_UnlockReg();

	/* Enable HXT clock (external XTAL 8MHz) */
	CLK->PWRCTL |= CLK_PWRCTL_HXTEN_Msk;

	/* Switch HCLK clock source to HIRC clock for safe */
	CLK->PWRCTL |= CLK_PWRCTL_HIRCEN_Msk;
	CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
	CLK->CLKSEL0 |= CLK_CLKSEL0_HCLKSEL_Msk;
	CLK->CLKDIV0 &= (~CLK_CLKDIV0_HCLKDIV_Msk);	

	/* Wait for HXT clock ready */
	CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);
	
	if((CLK->STATUS & CLK_STATUS_HXTSTB_Msk) != CLK_STATUS_HXTSTB_Msk)//如果HXT没有就绪 使用内HIRC 12mhz
	{
		u32HIRCSTB=1;
		/* Enable and apply new PLL setting. 输出4分频 输入2分频 反馈64分频  12*2*64/2/4*/
		CLK->PLLCTL = CLK_PLLCTL_PLLSRC_HIRC | (0x03 << 14) | (0x01 << 9) | (64 - 2UL);		
	}
	else//HXT就绪  8mhz
	{
		/* Enable and apply new PLL setting. 输出2分频 输入1分频 反馈48分频  8*2*48/2/2=192 */
		//CLK->PLLCTL = CLK_PLLCTL_PLLSRC_HXT | (0x01 << 14) | (0x01 << 9) | (48 - 2UL);		
		/* Enable and apply new PLL setting. 输出4分频 输入1分频 反馈64分频  12*2*64/2/4=192 */
		CLK->PLLCTL = CLK_PLLCTL_PLLSRC_HXT | (0x03 << 14) | (0x01 << 9) | (64 - 2UL);		
	}
	/* Wait for PLL clock stable */
	CLK_WaitClockReady(CLK_STATUS_PLLSTB_Msk);		
	
	/* Select HCLK clock source to PLL,and update system core clock */
	/* Apply new Divider */
	CLK->CLKDIV0 = (CLK->CLKDIV0 & (~CLK_CLKDIV0_HCLKDIV_Msk)) | CLK_CLKDIV0_HCLK(1UL);
	/* Switch HCLK to new HCLK source */
	CLK->CLKSEL0 = (CLK->CLKSEL0 & (~CLK_CLKSEL0_HCLKSEL_Msk)) | CLK_CLKSEL0_HCLKSEL_PLL;		
	
	/* Set PCLK0/PCLK1 to HCLK/2 */
	CLK->PCLKDIV = (CLK_PCLKDIV_PCLK0DIV2 | CLK_PCLKDIV_PCLK1DIV2);

	/* Disable HIRC if HIRC is not useful */
	if(u32HIRCSTB == 0UL)
	{
			CLK->PWRCTL &= ~CLK_PWRCTL_HIRCEN_Msk;
	}		
	//设置systick时钟
	//CLK->CLKSEL0&=~0x38;//HXT
	//SysTick->CTRL&=~0x04;
	
	/* Lock protected registers */
	SYS_LockReg();
	
	//计算系统时钟
//	SystemCoreClock = (uint32_t)192000000;
//	CyclesPerUs = (SystemCoreClock + 500000UL) / 1000000UL;

}


void SYS_Init(void)
{
	Power_Base_Init();//使能保障性电源
	CLK_Init();//初始化时钟
	Power_Advanced_Init();//使能高级电源
}


