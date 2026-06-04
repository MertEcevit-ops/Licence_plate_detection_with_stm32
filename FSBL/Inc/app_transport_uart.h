#ifndef APP_TRANSPORT_UART_H
#define APP_TRANSPORT_UART_H

#include "app_status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_UART_TX_CHUNK_SIZE 4096U

AppStatus_t AppUartTransport_SendSnapshot(uint32_t FrameBufferAddress,
                                          uint32_t Width,
                                          uint32_t Height,
                                          uint32_t Decimation);

#ifdef __cplusplus
}
#endif

#endif /* APP_TRANSPORT_UART_H */
