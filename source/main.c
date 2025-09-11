#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include "tcp_server.h"
#include "ia.h"
#include "control.h"
#include "types.h"  // Importante: incluir types.h

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

    printf("Sistema iniciando...\n");

    // Step 4: Create queues and mutex
    QueueHandle_t Buzon_ia_to_tcp;      // IA Task -> TCP Server
    QueueHandle_t Buzon_tcp_to_control; // TCP Server -> Control
    QueueHandle_t Buzon_control_to_tcp; // Control -> TCP Server
    SemaphoreHandle_t mutex_datos_compartidos;
    
    Buzon_ia_to_tcp = xQueueCreate(10, sizeof(message_t));
    Buzon_tcp_to_control = xQueueCreate(10, sizeof(message_t));
    Buzon_control_to_tcp = xQueueCreate(10, sizeof(message_t));
    mutex_datos_compartidos = xSemaphoreCreateMutex();
    
    // Verificar que las colas se crearon correctamente
    if (!Buzon_ia_to_tcp || !Buzon_tcp_to_control || !Buzon_control_to_tcp || !mutex_datos_compartidos) {
        printf("ERROR: No se pudieron crear las colas de comunicación\n");
        CY_ASSERT(0);
    }
    
    printf("Colas de comunicación creadas exitosamente\n");

    // Crear estructura de parámetros para las tareas
    static task_params_t task_params = {0};  // Static para que persista
    task_params.queue_tcp_to_control = Buzon_tcp_to_control;
    task_params.queue_control_to_tcp = Buzon_control_to_tcp;
    task_params.queue_ia_to_tcp = Buzon_ia_to_tcp;

    // Step 5: Create tasks with parameters
    BaseType_t task_result = xTaskCreate(
        tarea_TCPserver,     // Task function
        "TCP_Server",        // Task name
        (1024 * 5),         // 5KB Stack size
        &task_params,       // Parameters - IMPORTANTE: pasar los parámetros
        (2),                // Priority
        NULL                // Task handle (not needed)
    );
    
    BaseType_t task_result2 = xTaskCreate(
        tarea_ia,           // Task function
        "IA_Task",          // Task name
        (1024 * 50),        // 50KB Stack size
        &task_params,       // Parameters - IMPORTANTE: pasar los parámetros
        (3),                // Priority
        NULL                // Task handle (not needed)
    );
    
    BaseType_t task_result3 = xTaskCreate(
        control,            // Task function
        "Controlpin",       // Task name
        (1024 * 2),         // 2KB Stack size
        &task_params,       // Parameters - IMPORTANTE: pasar los parámetros
        (1),                // Priority
        NULL                // Task handle (not needed)
    );

    printf("\x1b[0m");
    
    // Check task creation
    if (task_result != pdPASS || task_result2 != pdPASS || task_result3 != pdPASS)
    {
        printf("Error: No se pudieron crear todas las tareas\n");
        printf("TCP Server: %s\n", (task_result == pdPASS) ? "OK" : "ERROR");
        printf("IA Task: %s\n", (task_result2 == pdPASS) ? "OK" : "ERROR"); 
        printf("Control Task: %s\n", (task_result3 == pdPASS) ? "OK" : "ERROR");
        CY_ASSERT(0);
    }
    else
    {
        printf("Todas las tareas creadas exitosamente\n");
        printf("Iniciando scheduler...\n");
    }

    // Step 6: Start scheduler
    vTaskStartScheduler();

    // Should never reach here
    printf("ERROR: Scheduler returned\n");
    CY_ASSERT(0);

    return 0;
}

/* [] END OF FILE */