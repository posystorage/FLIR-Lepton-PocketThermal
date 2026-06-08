#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdint.h>

typedef enum {
    STORAGE_RESULT_NONE = 0,
    STORAGE_RESULT_BUSY,
    STORAGE_RESULT_OK,
    STORAGE_RESULT_ERROR,
    STORAGE_RESULT_NO_CARD,
    STORAGE_RESULT_MSC_BUSY,
    STORAGE_RESULT_NO_FRAME,
    STORAGE_RESULT_CANCELLED,
} storage_result_t;

void Storage_Init(void);
storage_result_t Storage_BeginCapture(void);
void Storage_Service(void);
void Storage_Cancel(void);
uint8_t Storage_IsBusy(void);
storage_result_t Storage_GetLastResult(void);

#endif
