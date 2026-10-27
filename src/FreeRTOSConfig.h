#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// FreeRTOS SMP on both RP2040 cores. The pico-sdk locks (mutex, queue, sem) and sleeps block the
// calling task through the port (configSUPPORT_PICO_SYNC_INTEROP / _TIME_INTEROP, on by default
// in rp2040_config.h), instead of a wfe loop per core as in arduino-pico. The values follow the
// FreeRTOSConfig.h of arduino-pico 5.5.1 that the firmware ran on before.

#define configNUMBER_OF_CORES                   2
#define configTICK_CORE                         0
#define configUSE_CORE_AFFINITY                 1
#define configRUN_MULTIPLE_PRIORITIES           1
#define configUSE_TASK_PREEMPTION_DISABLE       1
#define configUSE_PASSIVE_IDLE_HOOK             0

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configCPU_CLOCK_HZ                      120000000
#define configTICK_RATE_HZ                      1000
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                256
#define configMAX_TASK_NAME_LEN                 10
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TICKLESS_IDLE                 0

#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configSTACK_DEPTH_TYPE                  uint32_t

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   4
#define configQUEUE_REGISTRY_SIZE               8

// The timer task runs the deferred wakeups of the pico-sdk locks released from an irq: above
// APP and USB, which never block (an assert in main.c)
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                32
#define configTIMER_TASK_STACK_DEPTH            1024

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_TRACE_FACILITY                1
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_CO_ROUTINES                   0

// newlib's struct _reent per task: the toolchain's newlib asks __getreent() for it (dynamic
// reent), which src/platform/newlib.c answers with the block of the task running on the core
#define configUSE_NEWLIB_REENTRANT              1
#ifndef __ASSEMBLER__
struct _reent;
extern struct _reent *kg_task_reent[2];
#endif
#define configSET_TLS_BLOCK(xTLSBlock)          (kg_task_reent[portGET_CORE_ID()] = &(xTLSBlock))

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xQueueGetMutexHolder            1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_xTaskAbortDelay                 1

// asserts end in panic(): src/diag/hang_diag keeps the message for the next boot's log
#ifndef __ASSEMBLER__
#include "pico/platform/panic.h"
#endif
#define configASSERT(x) do { if (!(x)) panic("FreeRTOS assert %s:%d", __FILE__, __LINE__); } while (0)

#endif
