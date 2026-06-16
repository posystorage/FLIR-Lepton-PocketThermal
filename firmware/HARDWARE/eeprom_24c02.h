#ifndef _EEPROM_24C02_H_
#define _EEPROM_24C02_H_

#include <stdint.h>

#define EEPROM_24C02_SIZE       256u
#define EEPROM_24C02_PAGE_SIZE  8u

uint8_t EEPROM24C02_Read(uint8_t addr, uint8_t *data, uint16_t len);
uint8_t EEPROM24C02_WritePage(uint8_t addr, const uint8_t *data, uint8_t len);
uint8_t EEPROM24C02_IsReady(void);

#endif
