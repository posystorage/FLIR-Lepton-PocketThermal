#ifndef _CONFIG_STORE_H_
#define _CONFIG_STORE_H_

#include <stdint.h>

typedef enum {
	CONFIG_STORE_DEFAULTS = 0,
	CONFIG_STORE_LOADED,
	CONFIG_STORE_ERROR,
} config_store_status_t;

void ConfigStore_Init(void);
void ConfigStore_Apply(void);
void ConfigStore_Service(void);
void ConfigStore_RequestSave(void);
uint8_t ConfigStore_IsBusy(void);
config_store_status_t ConfigStore_GetStatus(void);

#endif
