#include "app_tasks.h"
#include "app_camera.h"
#include "app_ltdc.h"
#include "app_transport_uart.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#define ISP_WARMUP_FRAME_COUNT 60U
#define SNAPSHOT_CAPTURE_TIMEOUT_MS 3000U
#define USER_BUTTON_DEBOUNCE_MS 30U
#define USER_BUTTON_POLL_MS 10U
#define CAMERA_TASK_STACK_SIZE 2048U

static void CameraTask(void *argument);
static void WaitForUserButtonPress(void);

AppStatus_t AppTasks_Create(void)
{
  if (xTaskCreate(CameraTask, "Camera", CAMERA_TASK_STACK_SIZE, NULL,
                  tskIDLE_PRIORITY + 3U, NULL) != pdPASS)
  {
    return APP_STATUS_ERROR;
  }

  return APP_STATUS_OK;
}

void AppTasks_StartScheduler(void)
{
  vTaskStartScheduler();
}

static void CameraTask(void *argument)
{
  DCMIPP_HandleTypeDef *dcmipp;
  LTDC_HandleTypeDef *ltdc;

  UNUSED(argument);

  dcmipp = AppCamera_GetDcmippHandle();
  ltdc = AppLtdc_GetHandle();

  if (AppCamera_StartIspPreview(dcmipp) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (AppCamera_RunIspWarmup(ISP_WARMUP_FRAME_COUNT) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (AppCamera_StopPreview(dcmipp) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (AppLtdc_Init(FRAME_WIDTH, FRAME_HEIGHT) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (AppCamera_ConfigureSnapshotDecimation(dcmipp, ltdc) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  for (;;)
  {
    WaitForUserButtonPress();

    if (AppCamera_CaptureSnapshot(dcmipp, BUFFER_ADDRESS,
                                  pdMS_TO_TICKS(SNAPSHOT_CAPTURE_TIMEOUT_MS)) != APP_STATUS_OK)
    {
      Error_Handler();
    }

    if (AppUartTransport_SendSnapshot(BUFFER_ADDRESS, SNAPSHOT_WIDTH,
                                      SNAPSHOT_HEIGHT, SNAPSHOT_DECIMATION_FACTOR) != APP_STATUS_OK)
    {
      Error_Handler();
    }

    BSP_LED_Toggle(LED_GREEN);
  }
}

static void WaitForUserButtonPress(void)
{
  while (BSP_PB_GetState(BUTTON_USER1) != BUTTON_PRESSED)
  {
    vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_POLL_MS));
  }

  vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_DEBOUNCE_MS));

  while (BSP_PB_GetState(BUTTON_USER1) == BUTTON_PRESSED)
  {
    vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_POLL_MS));
  }

  vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_DEBOUNCE_MS));
}
