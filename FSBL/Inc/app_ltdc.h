#ifndef APP_LTDC_H
#define APP_LTDC_H

#include "app_status.h"
#include "stm32n6xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

AppStatus_t AppLtdc_Init(uint32_t Width, uint32_t Height);
LTDC_HandleTypeDef *AppLtdc_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LTDC_H */
