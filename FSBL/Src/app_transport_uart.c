#include "app_transport_uart.h"
#include "app_crypto.h"
#include "app_memory.h"
#include "app_resources.h"
#include "snapshot_protocol.h"
#include "main.h"
#include "stm32n6570_discovery.h"
#include "FreeRTOS.h"

static uint32_t UartFrameId = 1U;
static uint8_t UartTxBuffer[APP_UART_TX_CHUNK_SIZE];

AppStatus_t AppUartTransport_SendSnapshot(uint32_t FrameBufferAddress,
                                          uint32_t Width,
                                          uint32_t Height,
                                          uint32_t Decimation)
{
  uint8_t header[SNAPSHOT_PROTOCOL_HEADER_SIZE];
  AppCrypto_Aes128CtrContext_t crypto_context;
  SnapshotProtocol_FrameInfo_t info;
  uint8_t *frame = (uint8_t *)FrameBufferAddress;
  uint32_t payload_size;
  uint32_t remaining;
  AppStatus_t status;

  payload_size = AppMemory_Rgb565PayloadSize(Width, Height);
  status = AppMemory_ValidateFrameBuffer(FrameBufferAddress, payload_size);
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  status = AppResources_Lock(APP_RESOURCE_UART, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    return status;
  }

  status = AppResources_Lock(APP_RESOURCE_FRAMEBUFFER, pdMS_TO_TICKS(1000U));
  if (status != APP_STATUS_OK)
  {
    AppResources_Unlock(APP_RESOURCE_UART);
    return status;
  }

  status = AppMemory_InvalidateDCache(FrameBufferAddress, payload_size);
  if (status == APP_STATUS_OK)
  {
    info.Width = (uint16_t)Width;
    info.Height = (uint16_t)Height;
    info.BytesPerPixel = 2U;
    info.Decimation = (uint16_t)Decimation;
    info.PixelFormat = SNAPSHOT_PROTOCOL_PIXEL_FORMAT_RGB565;
    info.Flags = SNAPSHOT_PROTOCOL_FLAG_CRC32 | SNAPSHOT_PROTOCOL_FLAG_AES128_CTR;
    info.PayloadSize = payload_size;
    info.FrameId = UartFrameId++;
    info.PayloadCrc32 = SnapshotProtocol_Crc32(frame, payload_size);

    status = SnapshotProtocol_BuildHeader(header, sizeof(header), &info);
  }

  AppResources_Unlock(APP_RESOURCE_FRAMEBUFFER);
  if (status != APP_STATUS_OK)
  {
    AppResources_Unlock(APP_RESOURCE_UART);
    return status;
  }

  status = AppCrypto_Aes128CtrStartFrame(&crypto_context, info.FrameId);
  if (status != APP_STATUS_OK)
  {
    AppResources_Unlock(APP_RESOURCE_UART);
    return status;
  }

  if (HAL_UART_Transmit(&hcom_uart[COM1], header, sizeof(header), HAL_MAX_DELAY) != HAL_OK)
  {
    AppResources_Unlock(APP_RESOURCE_UART);
    return APP_STATUS_ERROR;
  }

  remaining = payload_size;
  while (remaining > 0U)
  {
    uint16_t tx_size = (remaining > APP_UART_TX_CHUNK_SIZE) ?
                       (uint16_t)APP_UART_TX_CHUNK_SIZE :
                       (uint16_t)remaining;

    status = AppCrypto_Aes128CtrCrypt(&crypto_context, frame, UartTxBuffer, tx_size);
    if (status != APP_STATUS_OK)
    {
      AppResources_Unlock(APP_RESOURCE_UART);
      return status;
    }

    if (HAL_UART_Transmit(&hcom_uart[COM1], UartTxBuffer, tx_size, HAL_MAX_DELAY) != HAL_OK)
    {
      AppResources_Unlock(APP_RESOURCE_UART);
      return APP_STATUS_ERROR;
    }

    frame += tx_size;
    remaining -= tx_size;
  }

  AppResources_Unlock(APP_RESOURCE_UART);
  return APP_STATUS_OK;
}
