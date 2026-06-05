#ifndef TEST_STUB_STM32N6570_DISCOVERY_H
#define TEST_STUB_STM32N6570_DISCOVERY_H

#include <stdint.h>

typedef enum
{
  HAL_OK = 0,
  HAL_ERROR = 1
} HAL_StatusTypeDef;

typedef struct
{
  uint32_t Dummy;
} UART_HandleTypeDef;

typedef enum
{
  COM1 = 0,
  COMn
} COM_TypeDef;

#define HAL_MAX_DELAY 0xFFFFFFFFU

extern UART_HandleTypeDef hcom_uart[COMn];

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    const uint8_t *pData,
                                    uint16_t Size,
                                    uint32_t Timeout);

#endif /* TEST_STUB_STM32N6570_DISCOVERY_H */
