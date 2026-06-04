#include "app_memory.h"
#include "main.h"

uint32_t AppMemory_Rgb565PayloadSize(uint32_t Width, uint32_t Height)
{
  uint64_t size = (uint64_t)Width * (uint64_t)Height * 2ULL;

  if ((Width == 0U) || (Height == 0U) || (size > UINT32_MAX))
  {
    return 0U;
  }

  return (uint32_t)size;
}

AppStatus_t AppMemory_ValidateFrameBuffer(uint32_t Address, uint32_t Size)
{
  uint64_t start = (uint64_t)Address;
  uint64_t end = start + (uint64_t)Size;
  uint64_t fb_start = (uint64_t)BUFFER_ADDRESS;
  uint64_t fb_end = fb_start + (uint64_t)FRAME_BUFFER_SIZE;

  if ((Size == 0U) || (end < start))
  {
    return APP_STATUS_INVALID_ARG;
  }

  if ((start < fb_start) || (end > fb_end))
  {
    return APP_STATUS_SECURITY_ERROR;
  }

  return APP_STATUS_OK;
}

AppStatus_t AppMemory_InvalidateDCache(uint32_t Address, uint32_t Size)
{
  uint32_t aligned_addr;
  uint32_t aligned_end;
  AppStatus_t status;

  status = AppMemory_ValidateFrameBuffer(Address, Size);
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  aligned_addr = Address & ~(APP_MEMORY_CACHE_LINE_SIZE - 1U);
  aligned_end = (Address + Size + APP_MEMORY_CACHE_LINE_SIZE - 1U) &
                ~(APP_MEMORY_CACHE_LINE_SIZE - 1U);

  SCB_InvalidateDCache_by_Addr((uint32_t *)aligned_addr,
                               (int32_t)(aligned_end - aligned_addr));
  return APP_STATUS_OK;
}
