#ifndef SNAPSHOT_PROTOCOL_H
#define SNAPSHOT_PROTOCOL_H

#include "app_status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SNAPSHOT_PROTOCOL_MAGIC             "SNAP"
#define SNAPSHOT_PROTOCOL_MAGIC_SIZE        4U
#define SNAPSHOT_PROTOCOL_VERSION           2U
#define SNAPSHOT_PROTOCOL_HEADER_SIZE       32U
#define SNAPSHOT_PROTOCOL_PIXEL_FORMAT_RGB565 1U
#define SNAPSHOT_PROTOCOL_FLAG_AES128_CTR   0x02U

typedef struct
{
  uint16_t Width;
  uint16_t Height;
  uint16_t BytesPerPixel;
  uint16_t Decimation;
  uint8_t PixelFormat;
  uint8_t Flags;
  uint32_t PayloadSize;
  uint32_t FrameId;
} SnapshotProtocol_FrameInfo_t;

AppStatus_t SnapshotProtocol_BuildHeader(uint8_t *Header,
                                         uint32_t HeaderCapacity,
                                         const SnapshotProtocol_FrameInfo_t *Info);

#ifdef __cplusplus
}
#endif

#endif /* SNAPSHOT_PROTOCOL_H */
