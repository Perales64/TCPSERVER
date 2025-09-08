#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include "tcp_server.h"

#define TASK_STACK_SIZE    (1024 * 5)  // 5KB stack
#define TASK_PRIORITY      (1)         // Low priority

int main(void)
{
    cy_rslt_t result;

    printf("=== TCP Server ===\n");

    // Step 1: Initialize board
    result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    // Step 2: Enable interrupts
    __enable_irq();

    // Step 3: Initialize debug UART
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                        CY_RETARGET_IO_BAUDRATE);

    // Step 4: Create single task
    BaseType_t task_result = xTaskCreate(
        tarea_TCPserver,        // Task function
        "TCP_Server",           // Task name
        TASK_STACK_SIZE,        // Stack size
        NULL,                   // Parameters
        TASK_PRIORITY,          // Priority
        NULL                    // Task handle (not needed)
    );

    // Check task creation
    if (task_result != pdPASS) {
        printf("ERROR: TCP no creado\n");
        CY_ASSERT(0);
    }

    printf("TCP Server task created successfully\n");

    // Step 5: Start scheduler
    vTaskStartScheduler();

    // Should never reach here
    printf("ERROR: Scheduler returned\n");
    CY_ASSERT(0);

    return 0;
}

/* [] END OF FILE */