#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#ifndef CMSIS_device_header
#define CMSIS_device_header "stm32n6xx.h"
#endif

#define USE_FREERTOS                         1

#define configENABLE_TRUSTZONE               0
#define configRUN_FREERTOS_SECURE_ONLY       1
#define configENABLE_FPU                     1
#define configENABLE_MPU                     0
#define configENABLE_MVE                     0

#define configUSE_PREEMPTION                 1
#define configUSE_IDLE_HOOK                  0
#define configUSE_TICK_HOOK                  0
#define configCPU_CLOCK_HZ                   ( SystemCoreClock )
#define configTICK_RATE_HZ                   ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                 7
#define configMINIMAL_STACK_SIZE             ( ( uint16_t ) 128 )
#define configTOTAL_HEAP_SIZE                ( ( size_t ) ( 32 * 1024 ) )
#define configMAX_TASK_NAME_LEN              16
#define configUSE_TRACE_FACILITY             1
#define configUSE_16_BIT_TICKS               0
#define configIDLE_SHOULD_YIELD              1
#define configUSE_MUTEXES                    1
#define configQUEUE_REGISTRY_SIZE            8
#define configCHECK_FOR_STACK_OVERFLOW       0
#define configUSE_RECURSIVE_MUTEXES          1
#define configUSE_MALLOC_FAILED_HOOK         0
#define configUSE_COUNTING_SEMAPHORES        1
#define configUSE_TASK_NOTIFICATIONS         1
#define configUSE_TICKLESS_IDLE              0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

#define configSUPPORT_STATIC_ALLOCATION      0
#define configSUPPORT_DYNAMIC_ALLOCATION     1

#define configUSE_CO_ROUTINES                0
#define configMAX_CO_ROUTINE_PRIORITIES      2

#define configUSE_TIMERS                     0
#define configTIMER_TASK_PRIORITY            2
#define configTIMER_QUEUE_LENGTH             10
#define configTIMER_TASK_STACK_DEPTH         ( configMINIMAL_STACK_SIZE * 2 )

#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskDelayUntil              1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_eTaskGetState                1

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                      __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                      4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY                 ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY            ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* Keep the STM32 HAL SysTick handler and call the FreeRTOS tick from it. */
#define SysTick_Handler xPortSysTickHandler

#endif /* FREERTOS_CONFIG_H */
