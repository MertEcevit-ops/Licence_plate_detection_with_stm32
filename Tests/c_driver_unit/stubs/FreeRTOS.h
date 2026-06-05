#ifndef TEST_STUB_FREERTOS_H
#define TEST_STUB_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
#define pdTRUE 1
#define pdFALSE 0

#endif /* TEST_STUB_FREERTOS_H */
