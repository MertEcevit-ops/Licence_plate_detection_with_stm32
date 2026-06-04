#include "snapshot_protocol.h"
#include <stddef.h>

static void StoreLe16(uint8_t *Buffer, uint16_t Value)
{
  Buffer[0] = (uint8_t)(Value & 0xFFU);
  Buffer[1] = (uint8_t)((Value >> 8) & 0xFFU);
}

static void StoreLe32(uint8_t *Buffer, uint32_t Value)
{
  Buffer[0] = (uint8_t)(Value & 0xFFU);
  Buffer[1] = (uint8_t)((Value >> 8) & 0xFFU);
  Buffer[2] = (uint8_t)((Value >> 16) & 0xFFU);
  Buffer[3] = (uint8_t)((Value >> 24) & 0xFFU);
}

uint32_t SnapshotProtocol_Crc32(const uint8_t *Data, uint32_t Size)
{
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t i;
  uint8_t bit;

  if ((Data == NULL) && (Size != 0U))
  {
    return 0U;
  }

  for (i = 0U; i < Size; i++)
  {
    crc ^= (uint32_t)Data[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1U) ^ 0xEDB88320U;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return ~crc;
}

AppStatus_t SnapshotProtocol_BuildHeader(uint8_t *Header,
                                         uint32_t HeaderCapacity,
                                         const SnapshotProtocol_FrameInfo_t *Info)
{
  uint64_t expected_payload;
  uint32_t header_crc;

  if ((Header == NULL) || (Info == NULL) ||
      (HeaderCapacity < SNAPSHOT_PROTOCOL_HEADER_SIZE))
  {
    return APP_STATUS_INVALID_ARG;
  }

  expected_payload = (uint64_t)Info->Width * (uint64_t)Info->Height *
                     (uint64_t)Info->BytesPerPixel;
  if ((Info->Width == 0U) || (Info->Height == 0U) ||
      (Info->BytesPerPixel != 2U) ||
      (Info->PixelFormat != SNAPSHOT_PROTOCOL_PIXEL_FORMAT_RGB565) ||
      (expected_payload != (uint64_t)Info->PayloadSize))
  {
    return APP_STATUS_PROTOCOL_ERROR;
  }

  Header[0] = (uint8_t)'S';
  Header[1] = (uint8_t)'N';
  Header[2] = (uint8_t)'A';
  Header[3] = (uint8_t)'P';
  Header[4] = SNAPSHOT_PROTOCOL_VERSION;
  Header[5] = SNAPSHOT_PROTOCOL_HEADER_SIZE;
  Header[6] = Info->PixelFormat;
  Header[7] = Info->Flags;
  StoreLe16(&Header[8], Info->Width);
  StoreLe16(&Header[10], Info->Height);
  StoreLe16(&Header[12], Info->BytesPerPixel);
  StoreLe16(&Header[14], Info->Decimation);
  StoreLe32(&Header[16], Info->PayloadSize);
  StoreLe32(&Header[20], Info->FrameId);
  StoreLe32(&Header[24], Info->PayloadCrc32);
  StoreLe32(&Header[28], 0U);

  header_crc = SnapshotProtocol_Crc32(Header, SNAPSHOT_PROTOCOL_HEADER_SIZE - 4U);
  StoreLe32(&Header[28], header_crc);

  return APP_STATUS_OK;
}
