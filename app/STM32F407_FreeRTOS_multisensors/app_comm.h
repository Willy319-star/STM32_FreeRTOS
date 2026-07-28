#pragma once

#include "app_messages.h"

void AppComm_Start(void);
void AppComm_SubmitSensorMessage(const AppMessage *msg);
void AppComm_SetBinaryEnabled(uint8_t enabled);
uint8_t AppComm_IsBinaryEnabled(void);
uint8_t AppComm_ToggleBinaryEnabled(void);
