#ifndef _LCD_H_
#define _LCD_H_
#include "M480.h"
void LCD_Init(void);

void LCD_Clear(uint16_t color);
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos);
uint16_t LCD_ReadPoint(uint16_t x,uint16_t y);

void LCD_Set_Window(uint16_t x,uint16_t y,uint16_t width,uint16_t height);
/* Fixed 432x240 landscape window used by the heatmap path. */
void LCD_Begin_Logical_Window(uint16_t x,uint16_t y,uint16_t width,uint16_t height);
/* Top-left UI coordinates for fills, lines and individual pixels. */
void LCD_Begin_UI_Window(uint8_t orient,uint16_t x,uint16_t y,uint16_t width,uint16_t height);
/* Top-left UI coordinates for one continuously written glyph bitmap. */
void LCD_Begin_Glyph_Window(uint8_t orient,uint16_t x,uint16_t y,
                            uint16_t width,uint16_t height);
/* Restore the fixed landscape Entry Mode before heatmap output. */
void LCD_String_Write_End(void);
void LCD_WriteRAM_Prepare(void);
void LCD_WriteReg(uint8_t reg,uint16_t dat);

void LCD_Lepton_Test(uint8_t* dat_buff);
void LCD_Lepton_Test2(uint16_t* dat_buff);

#define LCD_BK_DUTY_CYCLE(dat) BPWM0->CMPDAT[4] = dat;


#define LCD_Read() (*(__IO uint16_t*)0x60000002)
#define LCD_Write_DAT16(dat) (*(__IO uint16_t*)0x60000002)=dat//注意 大小端是反的
#define LCD_Write_DAT8(dat) (*(__IO uint8_t*)0x60000002)=dat

//������ɫ
#define WHITE            0xFFFF
#define BLACK            0x0000
#define BLUE             0x001F
#define BRED             0XF81F
#define GRED             0XFFE0
#define GBLUE            0X07FF
#define RED              0xF800
#define MAGENTA          0xF81F
#define GREEN            0x07E0
#define CYAN             0x7FFF
#define YELLOW           0xFFE0
#define BROWN            0XBC40 //��ɫ
#define BRRED            0XFC07 //�غ�ɫ
#define GRAY             0X8430 //��ɫ
//GUI��ɫ
#define DARKBLUE         0X01CF //����ɫ
#define LIGHTBLUE        0X7D7C //ǳ��ɫ  
#define GRAYBLUE         0X5458 //����ɫ
//������ɫΪPANEL����ɫ

#define LIGHTGREEN       0X841F //ǳ��ɫ 
#define LGRAY            0XC618 //ǳ��ɫ(PANNEL),���屳��ɫ
#define LGRAYBLUE        0XA651 //ǳ����ɫ(�м����ɫ)
#define LBBLUE           0X2B12 //ǳ����ɫ(ѡ����Ŀ�ķ�ɫ)
#endif
