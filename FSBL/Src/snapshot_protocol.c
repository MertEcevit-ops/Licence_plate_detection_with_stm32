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

AppStatus_t SnapshotProtocol_BuildHeader(uint8_t *Header,
                                         uint32_t HeaderCapacity,
                                         const SnapshotProtocol_FrameInfo_t *Info)
{
  uint64_t expected_payload;

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
  StoreLe32(&Header[24], 0U);
  StoreLe32(&Header[28], 0U);

  return APP_STATUS_OK;
}
