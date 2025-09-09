#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include "tcp_server.h"
#include "ia.h"

int main(void)
{

    cy_rslt_t result;

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
        tarea_TCPserver, // Task function
        "TCP_Server",    // Task name
        (1024 * 5),      // 5KB Stack size
        NULL,            // Parameters
        (2),             // Priority
        NULL             // Task handle (not needed)
    );
    BaseType_t task_result2 = xTaskCreate(
        tarea_ia,    // Task function
        "IA_Task",   // Task name
        (1024 * 50), // 50KB Stack size
        NULL,        // Parameters
        (3),         // Priority
        NULL         // Task handle (not needed)
    );

    // Check task creation
    if (task_result != pdPASS)
    {
        printf("ERROR: TCP no creado\n");
        CY_ASSERT(0);
    }
    if (task_result2 != pdPASS)
    {
        printf("ERROR: TCP no creado\n");
        CY_ASSERT(0);
    }

    // Step 5: Start scheduler
    vTaskStartScheduler();

    // Should never reach here
    printf("ERROR: Scheduler returned\n");
    CY_ASSERT(0);

    return 0;
}

/* [] END OF FILE */