#include "app_platform.h"
#include "main.h"

AppStatus_t AppPlatform_Init(void)
{
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_RED);

  if (BSP_PB_Init(BUTTON_USER1, BUTTON_MODE_GPIO) != BSP_ERROR_NONE)
  {
    return APP_STATUS_ERROR;
  }

#if USE_COM_LOG
  COM_InitTypeDef COM_Init;

  COM_Init.BaudRate = 115200;
  COM_Init.WordLength = COM_WORDLENGTH_8B;
  COM_Init.StopBits = COM_STOPBITS_1;
  COM_Init.Parity = COM_PARITY_NONE;
  COM_Init.HwFlowCtl = COM_HWCONTROL_NONE;

  BSP_COM_Init(COM1, &COM_Init);

  if (BSP_COM_SelectLogPort(COM1) != BSP_ERROR_NONE)
  {
    return APP_STATUS_ERROR;
  }
#endif

  return APP_STATUS_OK;
}
