#include "lp3921.h"

uint8_t lp3921_reg0;//寄存器0的缓存


//PB4 系统总电源使能
//PB3 LDO4 使能
//PE8 DCDC-1V2 使能





void LP3921_PWRUP_Init(void)
{
	PB3=0;//拉低PB3
	PB->MODE&=~0xC0;
	PB->MODE|=0x40;//设置PB3为推挽模式
	PE8=0;//拉低PE8
	PE->MODE&=~0x30000;
	PE->MODE|=0x10000;//设置PE8为推挽模式
	
	IIC2_Init();
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x00,0x00);//关闭各个电源 现在只剩MCU电源
	lp3921_reg0=0;
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x01,0x0F);//LDO1 外设3.3v
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x02,0x0F);//LDO2 MCU 3.3V
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x04,0x07);//LDO4 摄像头模拟电源 2V8
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x07,0x07);//LDO7 摄像头IO电源 2V8
	
	//IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x10,0x09);//按设置模式充电
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x11,0x03);//200mA 充电电流
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x12,0x12);//4.2V 电池 0.1C结束充电  V TERM - 150 mV
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x19,0x00);//关闭音频运放
}


/* ── 读 LP3921 充电状态 ── */
//0-not in charge
//1-charging
//2-full/End Of Charge
uint8_t LP3921_Get_Charge_Sate(void)
{
    uint8_t val = 0;
    IIC_Read_Reg(LP3921_IIC_PORT, LP3921_IIC_ADDR, 0x13, &val);//LP3921_CHGSTATUS1
		if(val&0x40)
		{
			if(val&0x20)
			{
				return 2;
			}
			else
			{
				return 1;
			}
		}
		else return 0;
    
}


//使能外设电源
void LP3921_ENABLE_PER(void)
{
	lp3921_reg0|=0x01;//使能LDO1
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x01,0x0F);//LDO1 外设3.3v
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x00,lp3921_reg0);
}
//关闭外设电源
void LP3921_DISABLE_PER(void)
{
	lp3921_reg0&=~0x01;//失能LDO1
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x00,lp3921_reg0);
}


//使能摄像头电源
void LP3921_ENABLE_CAM(void)
{
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x04,0x07);//LDO4 摄像头模拟电源 2V8
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x07,0x07);//LDO7 摄像头IO电源 2V8
	PE8=1;//内核
	lp3921_reg0|=0x08;//使能LDO7 IO
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x00,lp3921_reg0);
	PB3=1;//LDO4 模拟
	
}
//关闭摄像头电源
void LP3921_DISABLE_CAM(void)
{
	lp3921_reg0&=~0x08;//失能LDO7 IO
	IIC_Write_Reg(LP3921_IIC_PORT,LP3921_IIC_ADDR,0x00,lp3921_reg0);
	PB3=0; //模拟
	PE8=0; //内核
}

void LP3921_SYS_PWR_Init_ON(void)
{
	//目前先简易暴力供电  实际应该还需要检查是否符合供电条件
	PB->MODE&=~0x300;
	PB->MODE|=0x100;//设置PB4为推挽模式
	PB4=1;
}
void LP3921_SYS_PWR_OFF(void)
{
	PB4=0;
}

