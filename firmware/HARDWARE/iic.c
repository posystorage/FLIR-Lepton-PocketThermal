#include "iic.h"
#include "lepton.h"
#include "debug.h"

static volatile uint8_t g_iic1_owner = 0u;

uint8_t IIC1_TryLock(uint8_t owner)
{
	if (owner == 0u) {
		return 0u;
	}
	if (g_iic1_owner != 0u && g_iic1_owner != owner) {
		return 0u;
	}
	g_iic1_owner = owner;
	return 1u;
}

void IIC1_Unlock(uint8_t owner)
{
	if (g_iic1_owner == owner) {
		g_iic1_owner = 0u;
	}
}

uint8_t IIC1_IsLocked(void)
{
	return (g_iic1_owner != 0u) ? 1u : 0u;
}

void IIC_DELAY(void)//100k
{
	uint32_t i;
	for(i=0;i<186;i++)
	__NOP();
}
//0 timeout
uint8_t IIC_Wait_State(I2C_T *i2c,uint8_t State)
{
	while(i2c->STATUS0!=State)
	{
    if (I2C_GET_TIMEOUT_FLAG(i2c))
    {
        I2C_ClearTimeoutFlag(i2c);
				I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
				//if(i2c == I2C1)HW_DEBUG("1");
				return 0;
    }
		if(i2c->STATUS0==0x20||i2c->STATUS0==0x00||i2c->STATUS0==0x30||i2c->STATUS0==0x48)//Master Transmit Address NACK  	Bus error   Master Transmit Data NACK
		{
				I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
				//HW_DEBUG("2");
				//if(i2c == I2C1)HW_DEBUG("%X",i2c->STATUS0);
				return 0;
		}
	}
	return 1;
}


void IIC0_Deinit(void)
{
	PA4=1;
	PA5=1;
	PA->MODE = (PA->MODE & ~((0x3ul << (4*2))|(0x3ul << (5*2)))) | ((2 << (4*2))|(2 << (5*2)));
	SYS->GPA_MFPL &= ~(SYS_GPA_MFPL_PA4MFP_Msk | SYS_GPA_MFPL_PA5MFP_Msk);
	I2C0->CTL0 &=~ I2C_CTL0_I2CEN_Msk;
	CLK->APBCLK0 &=~ CLK_APBCLK0_I2C0CKEN_Msk;
}


void IIC0_Init(void)//PA4/5
{
		uint32_t i;
		PA4=1;
		PA5=1;
		PA->MODE = (PA->MODE & ~((0x3ul << (4*2))|(0x3ul << (5*2)))) | ((2 << (4*2))|(1 << (5*2)));
		for(i=0;i<10;i++)
		{
			PA5=0;
			IIC_DELAY();
			PA5=1;
			IIC_DELAY();
		}
    /* Enable I2C0 peripheral clock */
		CLK->APBCLK0|=CLK_APBCLK0_I2C0CKEN_Msk;
    /* Set I2C0 multi-function pins */
    SYS->GPA_MFPL = (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA4MFP_Msk | SYS_GPA_MFPL_PA5MFP_Msk)) |
                    (SYS_GPA_MFPL_PA4MFP_I2C0_SDA | SYS_GPA_MFPL_PA5MFP_I2C0_SCL);

		/* I2C clock pin enable schmitt trigger */
    PA->SMTEN |= GPIO_SMTEN_SMTEN5_Msk;

		/* Compute proper divider for I2C clock */
		I2C0->CLKDIV = 59;//400khz@pclk0=96mhz
	  /* Enable I2C */
    I2C0->CTL0 |= I2C_CTL0_I2CEN_Msk;
		//I2C0->TOCTL=0x06;

		I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STA);
		if(IIC_Wait_State(I2C0,0x08)==0)
		I2C0->DAT=0xfe;
		I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
		if(IIC_Wait_State(I2C0,0x18)==0)
		I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STO_SI);

}
void IIC1_Init(void)//PG2/3 I2C1-mpu6050 24c02
{
	uint32_t i;
	/* Bus recovery: GPIO bit-bang 10 SCL pulses to release stuck slaves */
	PG3=1;
	PG2=1;
	PG->MODE = (PG->MODE & ~((0x3ul << (2*2))|(0x3ul << (3*2)))) | ((2 << (2*2))|(1 << (3*2)));
	for(i=0;i<10;i++)
	{
		PG2=0;
		IIC_DELAY();
		PG2=1;
		IIC_DELAY();
	}
    /* Enable I2C1 peripheral clock */
	CLK->APBCLK0|=CLK_APBCLK0_I2C1CKEN_Msk;
    /* Set I2C1 multi-function pins */
    SYS->GPG_MFPL = (SYS->GPG_MFPL & ~(SYS_GPG_MFPL_PG2MFP_Msk | SYS_GPG_MFPL_PG3MFP_Msk)) |
                    (SYS_GPG_MFPL_PG3MFP_I2C1_SDA | SYS_GPG_MFPL_PG2MFP_I2C1_SCL);

		/* Compute proper divider for I2C clock */
    I2C1->CLKDIV = 59;//400khz@pclk1=96mhz
	  /* Enable I2C */
    I2C1->CTL0 |= I2C_CTL0_I2CEN_Msk;
		//I2C1->TOCTL=0x06;
	

		I2C_SET_CONTROL_REG(I2C1, I2C_CTL_STA);
		if(IIC_Wait_State(I2C1,0x08)==0)
		I2C1->DAT=0xfe;
		I2C_SET_CONTROL_REG(I2C1, I2C_CTL_SI);
		if(IIC_Wait_State(I2C1,0x18)==0)
		I2C_SET_CONTROL_REG(I2C1, I2C_CTL_STO_SI);
	
}

void IIC2_Init(void)//PB12/13 system-lp3921 rtc
{
		uint32_t i;
		PB12=1;
		PB13=1;
		PB->MODE = (PB->MODE & ~((0x3ul << (12*2))|(0x3ul << (13*2)))) | ((2 << (12*2))|(1 << (13*2)));
		for(i=0;i<10;i++)
		{
			PB13=0;
			IIC_DELAY();
			PB13=1;
			IIC_DELAY();
		}

    /* Enable I2C2 peripheral clock */
		CLK->APBCLK0|=CLK_APBCLK0_I2C2CKEN_Msk;
    /* Set I2C2 multi-function pins */
    SYS->GPB_MFPH = (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB12MFP_Msk | SYS_GPB_MFPH_PB13MFP_Msk)) |
                    (SYS_GPB_MFPH_PB12MFP_I2C2_SDA | SYS_GPB_MFPH_PB13MFP_I2C2_SCL);

		/* I2C clock pin enable schmitt trigger */
    PB->SMTEN |= GPIO_SMTEN_SMTEN13_Msk;

		/* Compute proper divider for I2C clock */
	  I2C2->CLKDIV = 59;
		/* Enable I2C */
    I2C2->CTL0 |= I2C_CTL0_I2CEN_Msk;
		I2C2->TOCTL=0x04;

		I2C_SET_CONTROL_REG(I2C2, I2C_CTL_STA);
		if(IIC_Wait_State(I2C2,0x08)==0)
		I2C2->DAT=0xfe;
		I2C_SET_CONTROL_REG(I2C2, I2C_CTL_SI);
		if(IIC_Wait_State(I2C2,0x18)==0)
		I2C_SET_CONTROL_REG(I2C2, I2C_CTL_STO_SI);
}

//0 fail 1 success
uint8_t IIC_Test_Per(I2C_T *i2c,uint8_t Slave_Addr)
{
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STA);
	if(IIC_Wait_State(i2c,0x08)==0)return 0;
	i2c->DAT=Slave_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x18)==0)return 0;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
	return 1;
}

//0 fail 1 success
uint8_t IIC_Write_Reg(I2C_T *i2c,uint8_t Slave_Addr,uint8_t Reg_Addr,uint8_t Data)
{
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STA);
	if(IIC_Wait_State(i2c,0x08)==0)return 0;
	i2c->DAT=Slave_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x18)==0)return 0;
	i2c->DAT=Reg_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x28)==0)return 0;
	i2c->DAT=Data;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x28)==0)return 0;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
	return 1;
}

//0 fail 1 success
uint8_t IIC_Read_Reg(I2C_T *i2c,uint8_t Slave_Addr,uint8_t Reg_Addr,uint8_t* Data)
{
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STA);
	if(IIC_Wait_State(i2c,0x08)==0)return 0;
	i2c->DAT=Slave_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x18)==0)return 0;
	i2c->DAT=Reg_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x28)==0)return 0;

	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STA_SI);//repeat start
	if(IIC_Wait_State(i2c,0x10)==0)return 0;
	i2c->DAT=Slave_Addr|0x01;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x40)==0)return 0;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x58)==0)return 0;
	*Data=i2c->DAT;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STO_SI);
	return 1;
}

//0 fail 1 success
//Burst read multiple bytes starting from Reg_Addr
uint8_t IIC_Read_Multi(I2C_T *i2c,uint8_t Slave_Addr,uint8_t Reg_Addr,uint8_t *Data,uint16_t len)
{
	uint16_t i;
	if(len == 0) return 0;

	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STA);
	if(IIC_Wait_State(i2c,0x08)==0) return 0; 
	i2c->DAT=Slave_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x18)==0) return 0; 
	i2c->DAT=Reg_Addr;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x28)==0) return 0; 

	I2C_SET_CONTROL_REG(i2c, I2C_CTL_STA_SI);//repeat start
	if(IIC_Wait_State(i2c,0x10)==0) return 0; 
	i2c->DAT=Slave_Addr|0x01;
	I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);
	if(IIC_Wait_State(i2c,0x40)==0) return 0; 

	for(i=0;i<len;i++)
	{
		if(i < len-1)
		{
			I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI_AA);//ACK for more bytes
			if(IIC_Wait_State(i2c,0x50)==0) return 0; 
		}
		else
		{
			I2C_SET_CONTROL_REG(i2c, I2C_CTL_SI);//NACK last byte
			if(IIC_Wait_State(i2c,0x58)==0) return 0; 
		}
		Data[i] = i2c->DAT;
	}
	i2c->CTL0 |= (I2C_CTL0_SI_Msk | I2C_CTL0_STO_Msk);//STOP
	while(i2c->CTL0 & I2C_CTL0_STO_Msk){} //wait STOP complete
	return 1;
}


//0 fail 1 success
//16bit register
uint8_t IIC0_Lepton_Write_Reg(uint16_t Reg_Addr,uint16_t Data)
{
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STA);
	if(IIC_Wait_State(I2C0,0x08)==0)return 0;
	I2C0->DAT=Lepton_IIC_ADDR;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x18)==0)return 0;
	I2C0->DAT=Reg_Addr>>8;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	I2C0->DAT=Reg_Addr&0xff;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	I2C0->DAT=Data>>8;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	I2C0->DAT=Data&0xff;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STO_SI);
	return 1;
}

//0 fail 1 success
uint8_t IIC0_Lepton_Read_Reg(uint16_t Reg_Addr,uint16_t* Data)
{
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STA);
	if(IIC_Wait_State(I2C0,0x08)==0)return 0;
	I2C0->DAT=Lepton_IIC_ADDR;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x18)==0)return 0;
	I2C0->DAT=Reg_Addr>>8;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	I2C0->DAT=Reg_Addr&0xff;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;

	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STA_SI);//repeat start
	if(IIC_Wait_State(I2C0,0x10)==0)return 0;
	I2C0->DAT=Lepton_IIC_ADDR|0x01;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x40)==0)
		return 0;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI_AA);
	if(IIC_Wait_State(I2C0,0x50)==0)return 0;
	*(((uint8_t*)Data)+1)=(uint8_t)I2C0->DAT;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x58)==0)return 0;
	*((uint8_t*)Data)=(uint8_t)I2C0->DAT;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STO_SI);
	return 1;
}

//0 fail 1 success
//write 16bit data
uint8_t IIC0_Lepton_Write_DAT(uint16_t* DAT_BUFF,uint16_t DAT_Nums)
{
	uint32_t i;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STA);
	if(IIC_Wait_State(I2C0,0x08)==0)return 0;
	I2C0->DAT=Lepton_IIC_ADDR;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x18)==0)return 0;
	I2C0->DAT=0x00;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	I2C0->DAT=0x08;
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
	if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	for(i=0;i<DAT_Nums;i++)
	{
		I2C0->DAT=DAT_BUFF[i]>>8;
		I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
		if(IIC_Wait_State(I2C0,0x28)==0)return 0;
		I2C0->DAT=DAT_BUFF[i]&0xff;
		I2C_SET_CONTROL_REG(I2C0, I2C_CTL_SI);
		if(IIC_Wait_State(I2C0,0x28)==0)return 0;
	}
	I2C_SET_CONTROL_REG(I2C0, I2C_CTL_STO_SI);
	return 1;
}

//0 fail 1 success
//read multiple 16bit data words from CCI data registers
uint8_t IIC0_Lepton_Read_DAT(uint16_t* DAT_BUFF,uint16_t DAT_Nums)
{
	uint32_t i;

	if (DAT_BUFF == 0 || DAT_Nums == 0) return 0;

	for (i = 0; i < DAT_Nums; i++)
	{
		if (IIC0_Lepton_Read_Reg((uint16_t)(0x0008 + i * 2u), &DAT_BUFF[i]) == 0)
			return 0;
	}
	return 1;
}
