#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include "tcp_server.h"
#include "ia.h"
#include "control.h"

#define CMD_IA_TO_TCP 1
#define CMD_TCP_TO_CONTROL 2
#define CMD_CONTROL_TO_TCP 3

int main(void)
{

    cy_rslt_t result;

    // Step 1: Initialize board
    result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    // Estructura dato
    typedef struct
    {
        uint8_t command;
        char data[64];
        uint32_t value;
    } message_t;

    // Step 2: Enable interrupts
    __enable_irq();

    // Step 3: Initialize debug UART
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                        CY_RETARGET_IO_BAUDRATE);

    // Step 4: Create task
    QueueHandle_t Buzon_ia_to_tcp;      // IA Task -> TCP Server
    QueueHandle_t Buzon_tcp_to_control; // TCP Server -> Control
    QueueHandle_t Buzon_control_to_tcp; // Control -> TCP Server
    SemaphoreHandle_t mutex_datos_compartidos;
    Buzon_ia_to_tcp = xQueueCreate(10, sizeof(message_t));
    Buzon_tcp_to_control = xQueueCreate(10, sizeof(message_t));
    Buzon_control_to_tcp = xQueueCreate(10, sizeof(message_t));
    mutex_datos_compartidos = xSemaphoreCreateMutex();

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
    BaseType_t task_result3 = xTaskCreate(
        control,      // Task function
        "Controlpin", // Task name
        (1024 * 2),   // 2KB Stack size
        NULL,         // Parameters
        (3),          // Priority
        NULL          // Task handle (not needed)
    );

    // Check task creation
    if (task_result != pdPASS || task_result2 != pdPASS || task_result3 != pdPASS)
    {
        printf("Error: No se pudieron crear todas las tareas\n");
    }
    else
    {
        printf("Todas las tareas creadas exitosamente\n");
    }

    // Step 5: Start scheduler
    vTaskStartScheduler();

    // Should never reach here
    printf("ERROR: Scheduler returned\n");
    CY_ASSERT(0);

    return 0;
}

/* [] END OF FILE */