#pragma once

#include <stdint.h>

void AppPower_IdleHookNotify(void);
uint32_t AppPower_GetAndResetIdleHookCount(void);

