#ifndef APP_MEMORY_H
#define APP_MEMORY_H

#include "app_status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MEMORY_CACHE_LINE_SIZE 32U

uint32_t AppMemory_Rgb565PayloadSize(uint32_t Width, uint32_t Height);
AppStatus_t AppMemory_ValidateFrameBuffer(uint32_t Address, uint32_t Size);
AppStatus_t AppMemory_InvalidateDCache(uint32_t Address, uint32_t Size);

#ifdef __cplusplus
}
#endif

#endif /* APP_MEMORY_H */
