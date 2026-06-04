# Firmware Architecture Notes

The application logic is intentionally still the same as the original snapshot
flow:

1. Initialize DCMIPP, IMX335, LCD and FreeRTOS.
2. Start ISP preview long enough for statistics and exposure to settle.
3. Stop preview.
4. Configure DCMIPP snapshot decimation by two.
5. Capture one RGB565 frame into the existing framebuffer.
6. Send the frame to the host over UART.

The implementation is now split into modules:

| Module | Responsibility |
| --- | --- |
| `app_camera` | IMX335 probe, ISP helpers, preview warmup, snapshot capture and DCMIPP callbacks |
| `app_resources` | FreeRTOS mutexes for camera, UART and framebuffer ownership |
| `app_memory` | RGB565 size calculation, framebuffer bounds checks and D-cache invalidation |
| `snapshot_protocol` | v2 UART header generation and CRC32 |
| `app_transport_uart` | UART chunking, payload CRC and serialized transmission |
| `Tools/snapshot_protocol.py` | Host-side protocol parser and validation |
| `Tools/receive_snapshot.py` | Host CLI that receives and saves the image |

Memory usage is kept bounded: the firmware does not allocate an image copy, only
a 32-byte protocol header on the stack. The payload is streamed directly from the
framebuffer in `APP_UART_TX_CHUNK_SIZE` chunks.

Run host-side tests with:

```bash
source .venv/bin/activate
python -m unittest Tools/test_snapshot_protocol.py
```
