#include "eeprom_24c02.h"
#include "iic.h"

#define EEPROM_I2C          I2C1
#define EEPROM_ADDR_WRITE   0xA0u
#define EEPROM_OWNER        2u

uint8_t EEPROM24C02_IsReady(void)
{
	uint8_t ok;

	if (!IIC1_TryLock(EEPROM_OWNER)) {
		return 0u;
	}
	ok = IIC_Test_Per(EEPROM_I2C, EEPROM_ADDR_WRITE);
	IIC1_Unlock(EEPROM_OWNER);
	return ok;
}

uint8_t EEPROM24C02_Read(uint8_t addr, uint8_t *data, uint16_t len)
{
	uint8_t ok;

	if (data == 0 || len == 0u) {
		return 0u;
	}
	if ((uint16_t)addr + len > EEPROM_24C02_SIZE) {
		return 0u;
	}
	if (!IIC1_TryLock(EEPROM_OWNER)) {
		return 0u;
	}
	ok = IIC_Read_Multi(EEPROM_I2C, EEPROM_ADDR_WRITE, addr, data, len);
	IIC1_Unlock(EEPROM_OWNER);
	return ok;
}

uint8_t EEPROM24C02_WritePage(uint8_t addr, const uint8_t *data, uint8_t len)
{
	uint8_t i;
	uint8_t ok = 1u;

	if (data == 0 || len == 0u || len > EEPROM_24C02_PAGE_SIZE) {
		return 0u;
	}
	if ((uint16_t)addr + len > EEPROM_24C02_SIZE) {
		return 0u;
	}
	if (((uint16_t)addr & (EEPROM_24C02_PAGE_SIZE - 1u)) + len >
	    EEPROM_24C02_PAGE_SIZE) {
		return 0u;
	}
	if (!IIC1_TryLock(EEPROM_OWNER)) {
		return 0u;
	}

	I2C_SET_CONTROL_REG(EEPROM_I2C, I2C_CTL_STA);
	if(IIC_Wait_State(EEPROM_I2C,0x08)==0) ok = 0u;
	if (ok) {
		EEPROM_I2C->DAT = EEPROM_ADDR_WRITE;
		I2C_SET_CONTROL_REG(EEPROM_I2C, I2C_CTL_SI);
		if(IIC_Wait_State(EEPROM_I2C,0x18)==0) ok = 0u;
	}
	if (ok) {
		EEPROM_I2C->DAT = addr;
		I2C_SET_CONTROL_REG(EEPROM_I2C, I2C_CTL_SI);
		if(IIC_Wait_State(EEPROM_I2C,0x28)==0) ok = 0u;
	}
	for (i = 0u; ok && i < len; i++) {
		EEPROM_I2C->DAT = data[i];
		I2C_SET_CONTROL_REG(EEPROM_I2C, I2C_CTL_SI);
		if(IIC_Wait_State(EEPROM_I2C,0x28)==0) ok = 0u;
	}
	if (ok) {
		I2C_SET_CONTROL_REG(EEPROM_I2C, I2C_CTL_STO_SI);
	}
	IIC1_Unlock(EEPROM_OWNER);
	return ok;
}
