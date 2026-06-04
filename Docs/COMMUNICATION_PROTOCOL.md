# Snapshot UART Communication Protocol

The board sends one decimated RGB565 snapshot to the host over `COM1`.

## Frame Layout

All integer fields are little-endian.

| Offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 4 | magic | ASCII `SNAP` |
| 4 | 1 | version | Protocol version, currently `2` |
| 5 | 1 | header_size | Header size, currently `32` |
| 6 | 1 | pixel_format | `1` means RGB565 |
| 7 | 1 | flags | bit0 means payload CRC32 is present |
| 8 | 2 | width | Snapshot width after decimation |
| 10 | 2 | height | Snapshot height after decimation |
| 12 | 2 | bytes_per_pixel | RGB565 uses `2` |
| 14 | 2 | decimation | `2` means 1 output pixel for each 2 input pixels |
| 16 | 4 | payload_size | `width * height * bytes_per_pixel` |
| 20 | 4 | frame_id | Monotonic firmware frame counter |
| 24 | 4 | payload_crc32 | CRC32 of the raw RGB565 payload |
| 28 | 4 | header_crc32 | CRC32 of header bytes `[0:28]` |
| 32 | payload_size | payload | Raw RGB565 pixels |

## Host Flow

1. Open the serial port with the same baudrate as firmware, default `115200`.
2. Scan byte-by-byte until `SNAP` is seen.
3. Read the remaining 28 bytes of the v2 header.
4. Validate magic, version, header CRC, dimensions and payload size.
5. Read `payload_size` bytes.
6. Validate payload CRC32.
7. Convert RGB565 to RGB888 and save PNG or PPM.

Use:

```bash
source .venv/bin/activate
python Tools/receive_snapshot.py /dev/ttyACM0 -b 115200 -o snapshot.png
```

## Security And Robustness

This protocol is not encrypted and does not authenticate the device. It does
provide integrity checks, strict size checks and sync recovery. The firmware also
checks that the transmitted buffer remains inside the configured framebuffer
region before invalidating cache or sending bytes.
