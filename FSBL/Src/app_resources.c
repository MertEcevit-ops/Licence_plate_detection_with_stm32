#include "app_resources.h"
#include "semphr.h"

static SemaphoreHandle_t ResourceMutexes[APP_RESOURCE_COUNT];

AppStatus_t AppResources_Init(void)
{
  uint32_t index;

  for (index = 0U; index < (uint32_t)APP_RESOURCE_COUNT; index++)
  {
    ResourceMutexes[index] = xSemaphoreCreateMutex();
    if (ResourceMutexes[index] == NULL)
    {
      return APP_STATUS_ERROR;
    }
  }

  return APP_STATUS_OK;
}

AppStatus_t AppResources_Lock(AppResourceId_t Resource, TickType_t Timeout)
{
  if ((Resource >= APP_RESOURCE_COUNT) || (ResourceMutexes[Resource] == NULL))
  {
    return APP_STATUS_INVALID_ARG;
  }

  if (xSemaphoreTake(ResourceMutexes[Resource], Timeout) != pdTRUE)
  {
    return APP_STATUS_BUSY;
  }

  return APP_STATUS_OK;
}

void AppResources_Unlock(AppResourceId_t Resource)
{
  if ((Resource < APP_RESOURCE_COUNT) && (ResourceMutexes[Resource] != NULL))
  {
    (void)xSemaphoreGive(ResourceMutexes[Resource]);
  }
}
