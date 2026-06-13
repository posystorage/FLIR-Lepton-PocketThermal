#ifndef _LEPTON_APP_H_
#define _LEPTON_APP_H_

#include <stdint.h>

void Lepton_App_Init(void);
void Lepton_Frame_Service(void);
void Lepton_App_RequestManualFFC(void);
uint8_t Lepton_App_IsFFCInProgress(void);

#endif
