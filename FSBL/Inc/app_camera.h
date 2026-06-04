#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "app_status.h"
#include "FreeRTOS.h"
#include "stm32n6xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

AppStatus_t AppCamera_Probe(uint32_t Resolution, uint32_t PixelFormat);
AppStatus_t AppCamera_StartIspPreview(DCMIPP_HandleTypeDef *Dcmipp);
AppStatus_t AppCamera_RunIspWarmup(uint32_t FrameCount);
AppStatus_t AppCamera_StopPreview(DCMIPP_HandleTypeDef *Dcmipp);
AppStatus_t AppCamera_ConfigureSnapshotDecimation(DCMIPP_HandleTypeDef *Dcmipp,
                                                  LTDC_HandleTypeDef *Ltdc);
AppStatus_t AppCamera_CaptureSnapshot(DCMIPP_HandleTypeDef *Dcmipp,
                                      uint32_t FrameBufferAddress,
                                      TickType_t Timeout);
void AppCamera_OnPipeFrameEvent(DCMIPP_HandleTypeDef *Dcmipp, uint32_t Pipe);
void AppCamera_OnPipeVsyncEvent(DCMIPP_HandleTypeDef *Dcmipp, uint32_t Pipe);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */
