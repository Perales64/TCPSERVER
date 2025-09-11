#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <string.h>
#include <stdio.h>
#include "control.h"
#include "config.h"
#include "types.h"

void control(void *arg)
{
    // Recibir parámetros de las colas
    task_params_t *params = (task_params_t *)arg;
    
    typedef struct
    {
        cyhal_gpio_t pin;
        bool state;
    } gpio_output_t;

    static gpio_output_t outputs[NUM_OUTPUTS];
    // Pin definitions - más fácil de modificar
    static const cyhal_gpio_t OUTPUT_PINS[NUM_OUTPUTS] = {OUT1, OUT2, OUT3, OUT4};
    
    // Inicializar los pines GPIO
    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        outputs[i].pin = OUTPUT_PINS[i];
        outputs[i].state = false;

        cy_rslt_t result = cyhal_gpio_init(outputs[i].pin, CYHAL_GPIO_DIR_OUTPUT,
                                           CYHAL_GPIO_DRIVE_STRONG, false);

        if (result != CY_RSLT_SUCCESS)
        {
            printf("Error inicializando salida %d: 0x%lX\n", i + 1, result);
            return;
        }
    }

    printf("Control: Inicializado correctamente con %d salidas\n", NUM_OUTPUTS);

    message_t received_msg;
    message_t response_msg;
    
    for (;;)
    {
        // Esperar mensaje del TCP server
        if (xQueueReceive(params->queue_tcp_to_control, &received_msg, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            printf("Control: Comando recibido: %s\n", received_msg.data);
            
            bool command_found = false;
            bool command_executed = false;
            char response_buffer[64] = {0};
            
            // Procesar comandos individuales
            if (strncmp(received_msg.data, "1_ON", 4) == 0) {
                outputs[0].state = true;
                cyhal_gpio_write(outputs[0].pin, true);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 1: ON");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "1_OFF", 5) == 0) {
                outputs[0].state = false;
                cyhal_gpio_write(outputs[0].pin, false);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 1: OFF");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "2_ON", 4) == 0) {
                outputs[1].state = true;
                cyhal_gpio_write(outputs[1].pin, true);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 2: ON");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "2_OFF", 5) == 0) {
                outputs[1].state = false;
                cyhal_gpio_write(outputs[1].pin, false);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 2: OFF");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "3_ON", 4) == 0) {
                outputs[2].state = true;
                cyhal_gpio_write(outputs[2].pin, true);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 3: ON");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "3_OFF", 5) == 0) {
                outputs[2].state = false;
                cyhal_gpio_write(outputs[2].pin, false);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 3: OFF");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "4_ON", 4) == 0) {
                outputs[3].state = true;
                cyhal_gpio_write(outputs[3].pin, true);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 4: ON");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "4_OFF", 5) == 0) {
                outputs[3].state = false;
                cyhal_gpio_write(outputs[3].pin, false);
                snprintf(response_buffer, sizeof(response_buffer), "SALIDA 4: OFF");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "ALL_ON", 6) == 0) {
                for (int i = 0; i < NUM_OUTPUTS; i++) {
                    outputs[i].state = true;
                    cyhal_gpio_write(outputs[i].pin, true);
                }
                snprintf(response_buffer, sizeof(response_buffer), "TODAS LAS SALIDAS: ON");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "ALL_OFF", 7) == 0) {
                for (int i = 0; i < NUM_OUTPUTS; i++) {
                    outputs[i].state = false;
                    cyhal_gpio_write(outputs[i].pin, false);
                }
                snprintf(response_buffer, sizeof(response_buffer), "TODAS LAS SALIDAS: OFF");
                command_found = command_executed = true;
            }
            else if (strncmp(received_msg.data, "STATUS", 6) == 0) {
                snprintf(response_buffer, sizeof(response_buffer), 
                        "STATUS - S1:%s S2:%s S3:%s S4:%s",
                        outputs[0].state ? "ON" : "OFF",
                        outputs[1].state ? "ON" : "OFF", 
                        outputs[2].state ? "ON" : "OFF",
                        outputs[3].state ? "ON" : "OFF");
                command_found = command_executed = true;
            }
            
            // Preparar respuesta
            response_msg.command = CMD_CONTROL_TO_TCP;
            response_msg.value = received_msg.value; // Mantener el client ID
            
            if (command_found && command_executed) {
                strcpy(response_msg.data, response_buffer);
                printf("Control: Comando ejecutado - %s\n", response_buffer);
            } else {
                strcpy(response_msg.data, "COMANDO NO RECONOCIDO");
                printf("Control: Comando no reconocido: %s\n", received_msg.data);
            }
            
            // Enviar respuesta al TCP server
            if (xQueueSend(params->queue_control_to_tcp, &response_msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
                printf("Control: Error enviando respuesta al TCP server\n");
            }
        }
        
        // Pequeño delay para no saturar la CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}