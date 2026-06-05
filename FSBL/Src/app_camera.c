#include "app_camera.h"
#include "app_memory.h"
#include "app_resources.h"
#include "main.h"
#include "isp_api.h"
#include "imx335_E27_isp_param_conf.h"
#include "task.h"
#include <math.h>

static volatile uint32_t MainFrameCounter;
static DCMIPP_HandleTypeDef DcmippHandle;
static IMX335_Object_t IMX335Obj;
static ISP_HandleTypeDef CameraIsp;
static int32_t IspGain;
static int32_t IspExposure;

#define DIV_FACTOR(SRC, DST) (((uint32_t)((1024 * DST) / SRC)) > 1023 ? 1023 : ((uint32_t)((1024 * DST) / SRC)))
#define DOWNSCALE_RATIO(SRC, DST) (((uint32_t)(((float_t)(SRC) / (float_t)(DST)) * 8192) < 8192) ? 8192 : \
                                   ((((uint32_t)(((float_t)(SRC) / (float_t)(DST)) * 8192)) > 65535) ? 65535 : \
                                   ((uint32_t)(((float_t)(SRC) / (float_t)(DST)) * 8192))))

static ISP_StatusTypeDef GetSensorInfoHelper(uint32_t Instance, ISP_SensorInfoTypeDef *SensorInfo);
static ISP_StatusTypeDef SetSensorGainHelper(uint32_t Instance, int32_t Gain);
static ISP_StatusTypeDef GetSensorGainHelper(uint32_t Instance, int32_t *Gain);
static ISP_StatusTypeDef SetSensorExposureHelper(uint32_t Instance, int32_t Exposure);
static ISP_StatusTypeDef GetSensorExposureHelper(uint32_t Instance, int32_t *Exposure);

DCMIPP_HandleTypeDef *AppCamera_GetDcmippHandle(void)
{
  return &DcmippHandle;
}

AppStatus_t AppCamera_InitDcmipp(void)
{
  DCMIPP_PipeConfTypeDef pPipeConf = {0};
  DCMIPP_CSI_PIPE_ConfTypeDef pCSIPipeConf = {0};
  DCMIPP_CSI_ConfTypeDef csiconf = {0};
  DCMIPP_DownsizeTypeDef DonwsizeConf = {0};

  DcmippHandle.Instance = DCMIPP;
  if (HAL_DCMIPP_Init(&DcmippHandle) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  csiconf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csiconf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csiconf.PHYBitrate = DCMIPP_CSI_PHY_BT_1600;
  if (HAL_DCMIPP_CSI_SetConfig(&DcmippHandle, &csiconf) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  if (HAL_DCMIPP_CSI_SetVCConfig(&DcmippHandle, DCMIPP_VIRTUAL_CHANNEL0,
                                 DCMIPP_CSI_DT_BPP10) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  pCSIPipeConf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  pCSIPipeConf.DataTypeIDA = DCMIPP_DT_RAW10;
  pCSIPipeConf.DataTypeIDB = DCMIPP_DT_RAW10;

  if (HAL_DCMIPP_CSI_PIPE_SetConfig(&DcmippHandle, DCMIPP_PIPE1, &pCSIPipeConf) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  pPipeConf.FrameRate = DCMIPP_FRAME_RATE_ALL;
  pPipeConf.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
  pPipeConf.PixelPipePitch = 1600;

  if (HAL_DCMIPP_PIPE_SetConfig(&DcmippHandle, DCMIPP_PIPE1, &pPipeConf) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  DonwsizeConf.HSize = 800;
  DonwsizeConf.VSize = 480;
  DonwsizeConf.HRatio = DOWNSCALE_RATIO(IMX335_WIDTH, DonwsizeConf.HSize);
  DonwsizeConf.VRatio = DOWNSCALE_RATIO(IMX335_HEIGHT, DonwsizeConf.VSize);
  DonwsizeConf.HDivFactor = DIV_FACTOR(IMX335_WIDTH, DonwsizeConf.HSize);
  DonwsizeConf.VDivFactor = DIV_FACTOR(IMX335_HEIGHT, DonwsizeConf.VSize);

  if (HAL_DCMIPP_PIPE_SetDownsizeConfig(&DcmippHandle, DCMIPP_PIPE1, &DonwsizeConf) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  if (HAL_DCMIPP_PIPE_EnableDownsize(&DcmippHandle, DCMIPP_PIPE1) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  return APP_STATUS_OK;
}

AppStatus_t AppCamera_Probe(uint32_t Resolution, uint32_t PixelFormat)
{
  IMX335_IO_t io_ctx;
  uint32_t id;

  io_ctx.Address = CAMERA_IMX335_ADDRESS;
  io_ctx.Init = BSP_I2C1_Init;
  io_ctx.DeInit = BSP_I2C1_DeInit;
  io_ctx.ReadReg = BSP_I2C1_ReadReg16;
  io_ctx.WriteReg = BSP_I2C1_WriteReg16;
  io_ctx.GetTick = BSP_GetTick;

  if (IMX335_RegisterBusIO(&IMX335Obj, &io_ctx) != IMX335_OK)
  {
    return APP_STATUS_ERROR;
  }

  if (IMX335_ReadID(&IMX335Obj, &id) != IMX335_OK)
  {
    return APP_STATUS_ERROR;
  }

  if (id != (uint32_t)IMX335_CHIP_ID)
  {
    return APP_STATUS_SECURITY_ERROR;
  }

  if (IMX335_Init(&IMX335Obj, Resolution, PixelFormat) != IMX335_OK)
  {
    return APP_STATUS_ERROR;
  }

  if (IMX335_SetFrequency(&IMX335Obj, IMX335_INCK_24MHZ) != IMX335_OK)
  {
    return APP_STATUS_ERROR;
  }

  return APP_STATUS_OK;
}

AppStatus_t AppCamera_InitSensor(void)
{
  return AppCamera_Probe(IMX335_R2592_1944, IMX335_RAW_RGGB10);
}

AppStatus_t AppCamera_StartIspPreview(DCMIPP_HandleTypeDef *Dcmipp)
{
  ISP_AppliHelpersTypeDef helpers = {0};
  AppStatus_t status;

  if (Dcmipp == NULL)
  {
    return APP_STATUS_INVALID_ARG;
  }

  status = AppResources_Lock(APP_RESOURCE_CAMERA, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  helpers.GetSensorInfo = GetSensorInfoHelper;
  helpers.SetSensorGain = SetSensorGainHelper;
  helpers.GetSensorGain = GetSensorGainHelper;
  helpers.SetSensorExposure = SetSensorExposureHelper;
  helpers.GetSensorExposure = GetSensorExposureHelper;

  if (ISP_Init(&CameraIsp, Dcmipp, 0, &helpers, ISP_IQParamCacheInit[0]) != ISP_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  MainFrameCounter = 0U;
  if (HAL_DCMIPP_CSI_PIPE_Start(Dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0,
                                BUFFER_ADDRESS, DCMIPP_MODE_CONTINUOUS) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  if (ISP_Start(&CameraIsp) != ISP_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  AppResources_Unlock(APP_RESOURCE_CAMERA);
  return APP_STATUS_OK;
}

AppStatus_t AppCamera_RunIspWarmup(uint32_t FrameCount)
{
  if (FrameCount == 0U)
  {
    return APP_STATUS_INVALID_ARG;
  }

  while (MainFrameCounter < FrameCount)
  {
    BSP_LED_Toggle(LED_GREEN);
    if (ISP_BackgroundProcess(&CameraIsp) != ISP_OK)
    {
      BSP_LED_Toggle(LED_RED);
    }
    vTaskDelay(pdMS_TO_TICKS(1U));
  }

  MainFrameCounter = 0U;
  return APP_STATUS_OK;
}

AppStatus_t AppCamera_StopPreview(DCMIPP_HandleTypeDef *Dcmipp)
{
  AppStatus_t status;

  if (Dcmipp == NULL)
  {
    return APP_STATUS_INVALID_ARG;
  }

  status = AppResources_Lock(APP_RESOURCE_CAMERA, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  if (HAL_DCMIPP_CSI_PIPE_Stop(Dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  AppResources_Unlock(APP_RESOURCE_CAMERA);
  return APP_STATUS_OK;
}

AppStatus_t AppCamera_ConfigureSnapshotDecimation(DCMIPP_HandleTypeDef *Dcmipp,
                                                  LTDC_HandleTypeDef *Ltdc)
{
  DCMIPP_DecimationConfTypeDef decimation_config = {0};
  AppStatus_t status;

  if ((Dcmipp == NULL) || (Ltdc == NULL))
  {
    return APP_STATUS_INVALID_ARG;
  }

  status = AppResources_Lock(APP_RESOURCE_CAMERA, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  decimation_config.HRatio = DCMIPP_HDEC_1_OUT_2;
  decimation_config.VRatio = DCMIPP_VDEC_1_OUT_2;

  if (HAL_DCMIPP_PIPE_SetDecimationConfig(Dcmipp, DCMIPP_PIPE1, &decimation_config) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  if (HAL_DCMIPP_PIPE_EnableDecimation(Dcmipp, DCMIPP_PIPE1) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  if (HAL_DCMIPP_PIPE_SetPitch(Dcmipp, DCMIPP_PIPE1, SNAPSHOT_WIDTH * 2U) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  if (HAL_LTDC_SetWindowSize(Ltdc, SNAPSHOT_WIDTH, SNAPSHOT_HEIGHT, LTDC_LAYER_1) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  AppResources_Unlock(APP_RESOURCE_CAMERA);
  return APP_STATUS_OK;
}

AppStatus_t AppCamera_CaptureSnapshot(DCMIPP_HandleTypeDef *Dcmipp,
                                      uint32_t FrameBufferAddress,
                                      TickType_t Timeout)
{
  TickType_t start_tick;
  AppStatus_t status;

  if (Dcmipp == NULL)
  {
    return APP_STATUS_INVALID_ARG;
  }

  status = AppMemory_ValidateFrameBuffer(FrameBufferAddress, SNAPSHOT_FRAME_BUFFER_SIZE);
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  status = AppResources_Lock(APP_RESOURCE_CAMERA, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  status = AppResources_Lock(APP_RESOURCE_FRAMEBUFFER, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return status;
  }

  MainFrameCounter = 0U;
  if (HAL_DCMIPP_CSI_PIPE_Start(Dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0,
                                FrameBufferAddress, DCMIPP_MODE_SNAPSHOT) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_FRAMEBUFFER);
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  start_tick = xTaskGetTickCount();
  while (MainFrameCounter == 0U)
  {
    if ((xTaskGetTickCount() - start_tick) > Timeout)
    {
      (void)HAL_DCMIPP_CSI_PIPE_Stop(Dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0);
      AppResources_Unlock(APP_RESOURCE_FRAMEBUFFER);
      AppResources_Unlock(APP_RESOURCE_CAMERA);
      return APP_STATUS_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(1U));
  }

  MainFrameCounter = 0U;
  if (HAL_DCMIPP_CSI_PIPE_Stop(Dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_FRAMEBUFFER);
    AppResources_Unlock(APP_RESOURCE_CAMERA);
    return APP_STATUS_ERROR;
  }

  AppResources_Unlock(APP_RESOURCE_FRAMEBUFFER);
  AppResources_Unlock(APP_RESOURCE_CAMERA);
  return APP_STATUS_OK;
}

void AppCamera_OnPipeFrameEvent(DCMIPP_HandleTypeDef *Dcmipp, uint32_t Pipe)
{
  UNUSED(Dcmipp);
  UNUSED(Pipe);
  MainFrameCounter++;
}

void AppCamera_OnPipeVsyncEvent(DCMIPP_HandleTypeDef *Dcmipp, uint32_t Pipe)
{
  UNUSED(Dcmipp);

  switch (Pipe)
  {
    case DCMIPP_PIPE0:
      ISP_IncDumpFrameId(&CameraIsp);
      break;
    case DCMIPP_PIPE1:
      ISP_IncMainFrameId(&CameraIsp);
      ISP_GatherStatistics(&CameraIsp);
      break;
    case DCMIPP_PIPE2:
      ISP_IncAncillaryFrameId(&CameraIsp);
      break;
    default:
      break;
  }
}

void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  AppCamera_OnPipeFrameEvent(hdcmipp, Pipe);
}

void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  AppCamera_OnPipeVsyncEvent(hdcmipp, Pipe);
}

static ISP_StatusTypeDef GetSensorInfoHelper(uint32_t Instance, ISP_SensorInfoTypeDef *SensorInfo)
{
  UNUSED(Instance);
  return (ISP_StatusTypeDef)IMX335_GetSensorInfo(&IMX335Obj, (IMX335_SensorInfo_t *)SensorInfo);
}

static ISP_StatusTypeDef SetSensorGainHelper(uint32_t Instance, int32_t Gain)
{
  UNUSED(Instance);
  IspGain = Gain;
  return (ISP_StatusTypeDef)IMX335_SetGain(&IMX335Obj, Gain);
}

static ISP_StatusTypeDef GetSensorGainHelper(uint32_t Instance, int32_t *Gain)
{
  UNUSED(Instance);
  *Gain = IspGain;
  return ISP_OK;
}

static ISP_StatusTypeDef SetSensorExposureHelper(uint32_t Instance, int32_t Exposure)
{
  UNUSED(Instance);
  IspExposure = Exposure;
  return (ISP_StatusTypeDef)IMX335_SetExposure(&IMX335Obj, Exposure);
}

static ISP_StatusTypeDef GetSensorExposureHelper(uint32_t Instance, int32_t *Exposure)
{
  UNUSED(Instance);
  *Exposure = IspExposure;
  return ISP_OK;
}
