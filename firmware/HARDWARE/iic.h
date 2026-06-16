#ifndef _IIC_H_
#define _IIC_H_
#include "M480.h"
void IIC0_Deinit(void);
void IIC0_Init(void);
void IIC1_Init(void);
void IIC2_Init(void);

uint8_t IIC1_TryLock(uint8_t owner);
void IIC1_Unlock(uint8_t owner);
uint8_t IIC1_IsLocked(void);
uint8_t IIC_Wait_State(I2C_T *i2c,uint8_t State);


uint8_t IIC_Write_Reg(I2C_T *i2c,uint8_t Slave_Addr,uint8_t Reg_Addr,uint8_t Data);
uint8_t IIC_Read_Reg(I2C_T *i2c,uint8_t Slave_Addr,uint8_t Reg_Addr,uint8_t* Data);

uint8_t IIC_Test_Per(I2C_T *i2c,uint8_t Slave_Addr);

uint8_t IIC_Read_Multi(I2C_T *i2c,uint8_t Slave_Addr,uint8_t Reg_Addr,uint8_t *Data,uint16_t len);

uint8_t IIC0_Lepton_Write_Reg(uint16_t Reg_Addr,uint16_t Data);
uint8_t IIC0_Lepton_Read_Reg(uint16_t Reg_Addr,uint16_t* Data);
uint8_t IIC0_Lepton_Write_DAT(uint16_t* DAT_BUFF,uint16_t DAT_Nums);
uint8_t IIC0_Lepton_Read_DAT(uint16_t* DAT_BUFF,uint16_t DAT_Nums);

#endif
