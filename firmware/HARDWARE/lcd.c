#include "lcd.h"
#include "delay.h"
//LCD_RST PA9
//LCD_BK PC13/BPWM0_CH4
//LCD_CS PD12/EBI_nCS0
//LCD_RS PH7/EBI_ADR0

//#define USE_HW_LCD_RW

#ifndef USE_HW_LCD_RW
#define LCD_RS PH7
#endif
#define LCD_RST PA9



#ifdef USE_HW_LCD_RW
#define LCD_Write_CMD8(dat) (*(__IO uint8_t*)0x60000000)=dat;
#else
#define LCD_Write_CMD8(dat) LCD_RS=0;(*(__IO uint8_t*)0x60000000)=dat;LCD_RS=1;
#endif

//#define LCD_BK_DUTY_CYCLE(dat) BPWM0->CMPDAT[4] = dat-1;

// Heatmap and LCD memory use the fixed landscape 432x240 coordinate space.
#define LCD_SCREEN_W        432u
#define LCD_SCREEN_H        240u
/* Entry Mode bit6 is AM: landscape uses AM=1, portrait uses AM=0. */
#define LCD_ENTRY_DEFAULT   0x1060u
#define LCD_ENTRY_ROT_180   0x1050u
#define LCD_ENTRY_ROT_90    0x1000u
#define LCD_ENTRY_ROT_270   0x1030u

#define LCD_ORIENT_LANDSCAPE_180  0u
#define LCD_ORIENT_LANDSCAPE_0    1u
#define LCD_ORIENT_PORTRAIT_90    2u
#define LCD_ORIENT_PORTRAIT_270   3u

void LCD_EBI_Init(void)
{
	LCD_RST=0;//拉低PA9 复位LCD
	PA->MODE&=~0xC0000;
	PA->MODE|=0x40000;//设置PA9为推挽模式

#ifndef USE_HW_LCD_RW
	PH->MODE&=~0xC000;
	PH->MODE|=0x4000;//设置PH7为推挽模式
#endif

	/* Enable EBI BPWM peripheral clock */
	CLK->AHBCLK |= CLK_AHBCLK_EBICKEN_Msk;
	CLK->APBCLK1 |= CLK_APBCLK1_BPWM0CKEN_Msk;
	CLK->CLKSEL2 |= CLK_CLKSEL2_BPWM0SEL_Msk; //PCLK0

	/* EBI AD0~5 pins on PC.0~5 */
	SYS->GPC_MFPL &= 0xff000000;
	SYS->GPC_MFPL |= SYS_GPC_MFPL_PC0MFP_EBI_AD0 | SYS_GPC_MFPL_PC1MFP_EBI_AD1 |
									 SYS_GPC_MFPL_PC2MFP_EBI_AD2 | SYS_GPC_MFPL_PC3MFP_EBI_AD3 |
									 SYS_GPC_MFPL_PC4MFP_EBI_AD4 | SYS_GPC_MFPL_PC5MFP_EBI_AD5;
	/* EBI AD6, AD7 pins on PD.8, PD.9 */
	/* EBI CS0 pin on PD.12 */
	SYS->GPD_MFPH &= 0xfff0ff00;
	SYS->GPD_MFPH |= SYS_GPD_MFPH_PD8MFP_EBI_AD6 | SYS_GPD_MFPH_PD9MFP_EBI_AD7 | SYS_GPD_MFPH_PD12MFP_EBI_nCS0;

	/* EBI RD and WR pins on PA.11 and PA.10 */
	SYS->GPA_MFPH &= 0xffff00ff;
  SYS->GPA_MFPH |= SYS_GPA_MFPH_PA10MFP_EBI_nWR | SYS_GPA_MFPH_PA11MFP_EBI_nRD;

#ifdef USE_HW_LCD_RW
	/* EBI ADR0 LCD_RS pins on PH.7 */
	SYS->GPH_MFPL &= 0x0fffffff;
  SYS->GPH_MFPL |= SYS_GPH_MFPL_PH7MFP_EBI_ADR0;
#endif

	/* BPWM0-ch4 pin on PC.13 */
	SYS->GPC_MFPH &= 0xff0fffff;
	SYS->GPC_MFPH |= SYS_GPC_MFPH_PC13MFP_BPWM0_CH4;

	EBI->CTL0=(EBI_MCLKDIV_1 << EBI_CTL_MCLKDIV_Pos)|EBI_CTL_ADSEPEN_Msk|EBI_CTL_EN_Msk|EBI_CTL_CACCESS_Msk;//1分频 CS低有效 8位 数据地址分离 使能  连续访问模式
	EBI->TCTL0 = 0x03000038U;

	//96分频
	BPWM0->CLKPSC = 96-1;
	//10khz pwm
	BPWM0->PERIOD = 100-1;
	//0%占空比
	BPWM0->CMPDAT[4]=0;
	//下降计数
	BPWM0->CTL1 = 1;
	//设置极性
	BPWM0->WGCTL0 &= ~((BPWM_WGCTL0_PRDPCTLn_Msk | BPWM_WGCTL0_ZPCTLn_Msk) << (4 * 2U));
	BPWM0->WGCTL0 |= (BPWM_OUTPUT_LOW << ((4 * (2U)) + (uint32_t)BPWM_WGCTL0_PRDPCTLn_Pos));
	BPWM0->WGCTL1 &= ~((BPWM_WGCTL1_CMPDCTLn_Msk | BPWM_WGCTL1_CMPUCTLn_Msk) << (4 * 2U));
	BPWM0->WGCTL1 |= (BPWM_OUTPUT_HIGH << (4 * (2U) + (uint32_t)BPWM_WGCTL1_CMPDCTLn_Pos));

	SYS_UnlockReg();
	BPWM0->CTL0 |= BPWM_CTL0_DBGTRIOFF_Msk;
	SYS_LockReg();

  //使能输出
  BPWM0->POEN |= BPWM_CH_4_MASK;
	BPWM0->CNTEN = BPWM_CNTEN_CNTEN0_Msk;

	delay_ms(1);
	LCD_RST=1;
	delay_ms(8);
}

uint16_t LCD_ReadReg(uint8_t reg)
{
	uint32_t read_dat;
	LCD_Write_CMD8(reg);
	read_dat = LCD_Read();
	read_dat=__REV(read_dat);
	return read_dat>>16;
}
void LCD_WriteReg(uint8_t reg,uint16_t dat)
{
	uint32_t dat32;
	LCD_Write_CMD8(reg);
	dat32=__REV(dat);
	dat32>>=16;
	LCD_Write_DAT16(dat32);
}
__inline void LCD_WriteRAM_Prepare(void)
{
	LCD_Write_CMD8(0x22);
}

void LCD_CMD_Init(void)
{
	LCD_Write_CMD8(0x24);//8bit

	LCD_WriteReg(0x11, 0x0000);
	LCD_WriteReg(0x12, 0x0000);
	LCD_WriteReg(0x13, 0x0000);
	LCD_WriteReg(0x14, 0x0000);
	delay_ms(1);

	LCD_WriteReg(0x11, 0x0010);
	LCD_WriteReg(0x12, 0x3222);
	LCD_WriteReg(0x13, 0x204E);
	LCD_WriteReg(0x14, 0x0220);//VCOM-VCOML 电压调节 改变显示色彩
	LCD_WriteReg(0x10, 0x0700);
	delay_ms(1);

	LCD_WriteReg(0x11, 0x0112);
	delay_ms(1);
	LCD_WriteReg(0x11, 0x0312);
	delay_ms(1);
	LCD_WriteReg(0x11, 0x0712);
	delay_ms(1);
	LCD_WriteReg(0x11, 0x0F1B);
	delay_ms(1);
	LCD_WriteReg(0x11, 0x0F3B);
	delay_ms(3);

	/* Display Contron Register Setup */
	LCD_WriteReg(0x01, 0x0136);
	LCD_WriteReg(0x02, 0x0000);
//LCD_WriteReg(0x03, 0x9000);
	//LCD_WriteReg(0x03, 0x1000);	//Set BGR = 1
	LCD_WriteReg(0x03, 0x1060);

	LCD_WriteReg(0x07, 0x0104);
	LCD_WriteReg(0x08, 0x00E2);
	LCD_WriteReg(0x0B, 0x1100);
	LCD_WriteReg(0x0C, 0x0000);
	LCD_WriteReg(0x0F, 0x0001);	// OSC. freq.
	delay_ms(4);

	LCD_WriteReg(0x15, 0x0031);
	LCD_WriteReg(0x46, 0x00EF);
	LCD_WriteReg(0x47, 0x0000);
	LCD_WriteReg(0x48, 0x01AF);
	LCD_WriteReg(0x49, 0x0000);

	// Gamma (R)
	LCD_WriteReg(0x50, 0x0000);
	LCD_WriteReg(0x51, 0x030c);
	LCD_WriteReg(0x52, 0x0801);
	LCD_WriteReg(0x53, 0x0109);
	LCD_WriteReg(0x54, 0x0b01);
	LCD_WriteReg(0x55, 0x0200);
	LCD_WriteReg(0x56, 0x020d);
	LCD_WriteReg(0x57, 0x0e00);
	LCD_WriteReg(0x58, 0x0002);
	LCD_WriteReg(0x59, 0x010b);

	// Gamma (G)
	LCD_WriteReg(0x60, 0x0B00);
	LCD_WriteReg(0x61, 0x000D);
	LCD_WriteReg(0x62, 0x0000);
	LCD_WriteReg(0x63, 0x0002);
	LCD_WriteReg(0x64, 0x0604);
	LCD_WriteReg(0x65, 0x0000);
	LCD_WriteReg(0x66, 0x000C);
	LCD_WriteReg(0x67, 0x060F);
	LCD_WriteReg(0x68, 0x0F0F);
	LCD_WriteReg(0x69, 0x0A06);

	// Gamma (B)
	LCD_WriteReg(0x70, 0x0B00);
	LCD_WriteReg(0x71, 0x000D);
	LCD_WriteReg(0x72, 0x0000);
	LCD_WriteReg(0x73, 0x0002);
	LCD_WriteReg(0x74, 0x0604);
	LCD_WriteReg(0x75, 0x0000);
	LCD_WriteReg(0x76, 0x000C);
	LCD_WriteReg(0x77, 0x060F);
	LCD_WriteReg(0x78, 0x0F0F);
	LCD_WriteReg(0x79, 0x0A06);

//LCD_WriteReg(0x80, 0x0101);		//MTP control

	// Display Sequence
	LCD_WriteReg(0x07, 0x0112);
	delay_ms(4);
	LCD_WriteReg(0x07, 0x1113);

	LCD_WriteReg(0x13, 0x2055);

	// Power Control 1(R10h)
	// SAP: Fast    DSTB1F: Off    DSTB: Off    STB: Off
	LCD_WriteReg(0x10, 0x0700);

	// Blank Period Control(R08h)
	// FP: 2    BP: 2
	LCD_WriteReg(0x08, 0x0022);

	// Frame Cycle Control(R0Bh)
	// NO: 2 INCLK    SDT: 2 INCLK    DIV: fosc/1    RTN: 17 INCLK
	LCD_WriteReg(0x0B, 0x2201);
}


void LCD_Init(void)
{
	LCD_EBI_Init();
	LCD_CMD_Init();

	LCD_Clear(0xffff);

	LCD_BK_DUTY_CYCLE(90);
	//LCD_ReadPoint(0,0);
}

static uint16_t LCD_To_Bus_Color(uint16_t color)
{
	return (uint16_t)(__REV(color) >> 16);
}

static void LCD_Write_Window_Regs(uint16_t hs,uint16_t he,uint16_t vs,uint16_t ve)
{
	LCD_Write_CMD8(0X47);
	LCD_Write_DAT16(__REV(hs)>>16);
	LCD_Write_CMD8(0X47-1);
	LCD_Write_DAT16(__REV(he)>>16);
	LCD_Write_CMD8(0X49);
	LCD_Write_DAT16(__REV(vs)>>16);
	LCD_Write_CMD8(0X49-1);
	LCD_Write_DAT16(__REV(ve)>>16);
}

void LCD_Set_Window(uint16_t x,uint16_t y,uint16_t width,uint16_t height)
{
	uint16_t hs;
	uint16_t he;
	uint16_t vs;
	uint16_t ve;

	if (width == 0u || height == 0u) {
		return;
	}
	hs = x;
	he = (uint16_t)(x + width - 1u);
	vs = (uint16_t)(LCD_SCREEN_H - y - height);
	ve = (uint16_t)(LCD_SCREEN_H - 1u - y);
	LCD_Write_Window_Regs(hs, he, vs, ve);
}

static void LCD_Begin_Base_Window(uint16_t x,uint16_t y,uint16_t width,uint16_t height)
{
	LCD_Set_Window(x, y, width, height);
	LCD_WriteRAM_Prepare();
}

void LCD_Begin_Logical_Window(uint16_t x,uint16_t y,uint16_t width,uint16_t height)
{
	LCD_WriteReg(0x03, LCD_ENTRY_DEFAULT);
	LCD_Begin_Base_Window(x, y, width, height);
}

void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
{
	LCD_WriteReg(0x03, LCD_ENTRY_DEFAULT);
	LCD_Set_Window(Xpos, Ypos, 1u, 1u);
}

uint16_t LCD_ReadPoint(uint16_t x,uint16_t y)
{
	uint32_t read_dat;
	LCD_SetCursor(x,y);
	LCD_WriteRAM_Prepare();
	read_dat = LCD_Read();
	delay_us(1);
	read_dat = LCD_Read();
	read_dat=__REV(read_dat);
	return read_dat>>16;
}

void LCD_Clear(uint16_t color)
{
	uint32_t index = 0;
	uint16_t color_bus = LCD_To_Bus_Color(color);

	LCD_Begin_Logical_Window(0u, 0u, LCD_SCREEN_W, LCD_SCREEN_H);
	for(index=0; index<(uint32_t)LCD_SCREEN_W * (uint32_t)LCD_SCREEN_H; index++)
	{
		LCD_Write_DAT16(color_bus);
	}
}

/*
 * Draw a solid UI rectangle from top-left coordinates in the current screen
 * orientation. The rectangle is mapped into the fixed 432x240 LCD memory
 * space. Solid colors do not depend on pixel traversal order.
 *
 * Portrait 90:  logical (x,y,w,h) -> base (y,x,h,w)
 * Portrait 270: logical rectangle is mirrored and its axes are exchanged.
 */
void LCD_Begin_UI_Window(uint8_t orient,uint16_t x,uint16_t y,
                         uint16_t width,uint16_t height)
{
	uint16_t hs;
	uint16_t he;
	uint16_t vs;
	uint16_t ve;
	uint16_t entry_mode;

	if (width == 0u || height == 0u) {
		return;
	}

	if (orient == LCD_ORIENT_PORTRAIT_90) {
		hs = y;
		he = (uint16_t)(y + height - 1u);
		vs = (uint16_t)(LCD_SCREEN_H - x - width);
		ve = (uint16_t)(LCD_SCREEN_H - 1u - x);
	} else if (orient == LCD_ORIENT_PORTRAIT_270) {
		hs = (uint16_t)(LCD_SCREEN_W - y - height);
		he = (uint16_t)(LCD_SCREEN_W - 1u - y);
		vs = x;
		ve = (uint16_t)(x + width - 1u);
	} else {
		hs = x;
		he = (uint16_t)(x + width - 1u);
		vs = y;
		ve = (uint16_t)(y + height - 1u);
	}

	entry_mode = (orient == LCD_ORIENT_LANDSCAPE_180) ?
	             LCD_ENTRY_ROT_180 : LCD_ENTRY_DEFAULT;
	LCD_Write_Window_Regs(hs, he, vs, ve);
	LCD_WriteReg(0x03, entry_mode);
	LCD_WriteRAM_Prepare();
}

/*
 * Draw one glyph bitmap from top-left coordinates in the current orientation.
 * Glyph window coordinates always use x/y directly. Entry Mode rotates the
 * continuous bitmap stream; swapping x/y here would move (200,0) to (0,200).
 */
void LCD_Begin_Glyph_Window(uint8_t orient,uint16_t x,uint16_t y,
                            uint16_t width,uint16_t height)
{
	uint16_t hs;
	uint16_t he;
	uint16_t vs;
	uint16_t ve;
	uint16_t entry_mode;

	if (width == 0u || height == 0u) {
		return;
	}

	hs = x;
	he = (uint16_t)(x + width - 1u);
	vs = y;
	ve = (uint16_t)(y + height - 1u);

	if (orient == LCD_ORIENT_PORTRAIT_90) {
		entry_mode = LCD_ENTRY_ROT_90;
	} else if (orient == LCD_ORIENT_PORTRAIT_270) {
		entry_mode = LCD_ENTRY_ROT_270;
	} else {
		entry_mode = (orient == LCD_ORIENT_LANDSCAPE_180) ?
		             LCD_ENTRY_ROT_180 : LCD_ENTRY_DEFAULT;
	}

	LCD_Write_Window_Regs(hs, he, vs, ve);
	LCD_WriteReg(0x03, entry_mode);
	LCD_WriteRAM_Prepare();
}

void LCD_String_Write_End(void)
{
	/* Heatmap writes always expect the fixed landscape GRAM direction. */
	LCD_WriteReg(0x03, LCD_ENTRY_DEFAULT);
}

void LCD_Lepton_Test(uint8_t* dat_buff)
{
#if	 0
	uint32_t i;
	//uint32_t dat;
	LCD_Set_Window(0,0,60,80);
	LCD_WriteReg(0x03,0x5060);//设置为3次传输
	LCD_WriteRAM_Prepare();
	for(i=0;i<60*80*3;i++)
	{
		LCD_Write_DAT8(dat_buff[i]);
	}
	LCD_WriteReg(0x03,LCD_ENTRY_DEFAULT);//Entry Mode Set.
#else
	uint32_t i,j,i1,j1;
	LCD_Set_Window(0,0,320,240);
	LCD_WriteReg(0x03,0x5060);//设置为3次传输
	LCD_WriteRAM_Prepare();
	for(i=0;i<240;i++)
	{
		for(j=0;j<320;j++)
		{
			i1=i/4;
			j1=j/4;
				LCD_Write_DAT8(dat_buff[i1*240+j1*3]);
				LCD_Write_DAT8(dat_buff[i1*240+j1*3+1]);
				LCD_Write_DAT8(dat_buff[i1*240+j1*3+2]);
		}
	}
	LCD_WriteReg(0x03,LCD_ENTRY_DEFAULT);//Entry Mode Set.
#endif
}

void LCD_Lepton_Test2(uint16_t* dat_buff)
{
#if	 0
	uint32_t i;
	LCD_Set_Window(0,0,60,80);
	LCD_WriteReg(0x03,0x5060);//设置为3次传输
	LCD_WriteRAM_Prepare();
	for(i=0;i<60*80;i++)
	{
		LCD_Write_DAT8(dat_buff[i]>>6);
		LCD_Write_DAT8(dat_buff[i]>>6);
		LCD_Write_DAT8(dat_buff[i]>>6);
	}
	LCD_WriteReg(0x03,LCD_ENTRY_DEFAULT);//Entry Mode Set.
#else
	uint32_t i,j,i1,j1;
	LCD_Set_Window(0,0,320,240);
	LCD_WriteReg(0x03,0x5060);//设置为3次传输
	LCD_WriteRAM_Prepare();
	for(i=0;i<240;i++)
	{
		for(j=0;j<320;j++)
		{
			i1=i/4;
			j1=j/4;
			LCD_Write_DAT8(dat_buff[i1*80+j1]>>1);
			LCD_Write_DAT8(dat_buff[i1*80+j1]>>1);
			LCD_Write_DAT8(dat_buff[i1*80+j1]>>1);
				//LCD_Write_DAT8(dat_buff[i1*240+j1*3]);
				//LCD_Write_DAT8(dat_buff[i1*240+j1*3+1]);
				//LCD_Write_DAT8(dat_buff[i1*240+j1*3+2]);
		}
	}
	LCD_WriteReg(0x03,LCD_ENTRY_DEFAULT);//Entry Mode Set.
#endif
}
