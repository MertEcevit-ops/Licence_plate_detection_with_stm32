# Firmware Architecture Notes

The application logic is intentionally still the same as the original snapshot
flow:

1. Initialize DCMIPP, IMX335, LCD and FreeRTOS.
2. Start ISP preview long enough for statistics and exposure to settle.
3. Stop preview.
4. Configure DCMIPP snapshot decimation by two.
5. Capture one RGB565 frame into the existing framebuffer.
6. Encrypt the RGB565 payload with AES-128-CTR and send the frame to the host over UART.

The implementation is now split into modules:

| Module | Responsibility |
| --- | --- |
| `app_camera` | IMX335 probe, ISP helpers, preview warmup, snapshot capture and DCMIPP callbacks |
| `app_resources` | FreeRTOS mutexes for camera, UART and framebuffer ownership |
| `app_memory` | RGB565 size calculation, framebuffer bounds checks and D-cache invalidation |
| `snapshot_protocol` | v2 UART header generation |
| `app_transport_uart` | UART chunking, AES-CTR payload encryption and serialized transmission |
| `Tools/snapshot_protocol.py` | Host-side protocol parser and validation |
| `Tools/receive_snapshot.py` | Host CLI that receives, decrypts and saves the image |
| `alpr_inference.py` | Live host pipeline that receives AES SNAP frames, decrypts them, and runs YOLO/EasyOCR |

Memory usage is kept bounded: the firmware does not allocate an image copy, only
a 32-byte protocol header on the stack. The payload is streamed directly from the
framebuffer in `APP_UART_TX_CHUNK_SIZE` chunks.

Run host-side tests with:

```bash
source .venv/bin/activate
python -m unittest Tools/test_snapshot_protocol.py
make -C Tests/c_driver_unit test
make -C Tests/c_driver_unit report
```

Run live host inference with:

```bash
source .venv/bin/activate
python alpr_inference.py --port /dev/ttyACM0 -b 115200 --pt yolov8_plaka.pt --conf 0.25
```
