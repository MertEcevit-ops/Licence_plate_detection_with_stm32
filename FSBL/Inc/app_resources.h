#ifndef APP_RESOURCES_H
#define APP_RESOURCES_H

#include "app_status.h"
#include "FreeRTOS.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  APP_RESOURCE_CAMERA = 0,
  APP_RESOURCE_UART,
  APP_RESOURCE_FRAMEBUFFER,
  APP_RESOURCE_COUNT
} AppResourceId_t;

AppStatus_t AppResources_Init(void);
AppStatus_t AppResources_Lock(AppResourceId_t Resource, TickType_t Timeout);
void AppResources_Unlock(AppResourceId_t Resource);

#ifdef __cplusplus
}
#endif

#endif /* APP_RESOURCES_H */
