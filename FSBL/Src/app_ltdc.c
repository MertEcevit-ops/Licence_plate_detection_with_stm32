#include "app_ltdc.h"
#include "main.h"

static LTDC_HandleTypeDef LtdcHandle;

LTDC_HandleTypeDef *AppLtdc_GetHandle(void)
{
  return &LtdcHandle;
}

AppStatus_t AppLtdc_Init(uint32_t Width, uint32_t Height)
{
  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  LtdcHandle.Instance = LTDC;
  LtdcHandle.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  LtdcHandle.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  LtdcHandle.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  LtdcHandle.Init.PCPolarity = LTDC_PCPOLARITY_IPC;

  LtdcHandle.Init.HorizontalSync = RK050HR18_HSYNC - 1;
  LtdcHandle.Init.AccumulatedHBP = RK050HR18_HSYNC + RK050HR18_HBP - 1;
  LtdcHandle.Init.AccumulatedActiveW = RK050HR18_HSYNC + Width + RK050HR18_HBP - 1;
  LtdcHandle.Init.TotalWidth = RK050HR18_HSYNC + Width + RK050HR18_HBP + RK050HR18_HFP - 1;
  LtdcHandle.Init.VerticalSync = RK050HR18_VSYNC - 1;
  LtdcHandle.Init.AccumulatedVBP = RK050HR18_VSYNC + RK050HR18_VBP - 1;
  LtdcHandle.Init.AccumulatedActiveH = RK050HR18_VSYNC + Height + RK050HR18_VBP - 1;
  LtdcHandle.Init.TotalHeigh = RK050HR18_VSYNC + Height + RK050HR18_VBP + RK050HR18_VFP - 1;

  LtdcHandle.Init.Backcolor.Blue = 0x0;
  LtdcHandle.Init.Backcolor.Green = 0xFF;
  LtdcHandle.Init.Backcolor.Red = 0x0;

  if (HAL_LTDC_Init(&LtdcHandle) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = Width;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = Height;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.FBStartAdress = BUFFER_ADDRESS;
  pLayerCfg.Alpha = LTDC_LxCACR_CONSTA;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  pLayerCfg.ImageWidth = Width;
  pLayerCfg.ImageHeight = Height;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;

  if (HAL_LTDC_ConfigLayer(&LtdcHandle, &pLayerCfg, LTDC_LAYER_1) != HAL_OK)
  {
    return APP_STATUS_ERROR;
  }

  return APP_STATUS_OK;
}
