#ifndef _LP3921_H_
#define _LP3921_H_
#include "M480.h"
#include "iic.h"

#define LP3921_IIC_ADDR 0xFC
#define LP3921_IIC_PORT I2C2


uint8_t LP3921_Get_Charge_Sate(void);

void LP3921_PWRUP_Init(void);

void LP3921_ENABLE_PER(void);
void LP3921_DISABLE_PER(void);
void LP3921_ENABLE_CAM(void);
void LP3921_DISABLE_CAM(void);

void LP3921_SYS_PWR_Init_ON(void);
void LP3921_SYS_PWR_OFF(void);
#endif
