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

// Estructura optimizada para comandos
typedef struct
{
    const char *cmd;
    uint8_t cmd_len;
    uint8_t output_mask; // Bitmask para múltiples salidas
    bool state;
    bool is_status;
    bool is_multi_output;
} command_lookup_t;

// Tabla de lookup optimizada - ordenada por frecuencia de uso
static const command_lookup_t command_table[] = {
    {"STATUS", 6, 0x0F, false, true, false}, // Comando más común
    {"ALL_ON", 6, 0x0F, true, false, true},  // Segundo más común
    {"ALL_OFF", 7, 0x0F, false, false, true},
    {"1_ON", 4, 0x01, true, false, false},
    {"1_OFF", 5, 0x01, false, false, false},
    {"2_ON", 4, 0x02, true, false, false},
    {"2_OFF", 5, 0x02, false, false, false},
    {"3_ON", 4, 0x04, true, false, false},
    {"3_OFF", 5, 0x04, false, false, false},
    {"4_ON", 4, 0x08, true, false, false},
    {"4_OFF", 5, 0x08, false, false, false}};

#define COMMAND_TABLE_SIZE (sizeof(command_table) / sizeof(command_lookup_t))

typedef struct
{
    cyhal_gpio_t pin;
    bool state;
    uint32_t last_change_time; // Para debounce/logging
} gpio_output_t;

// Variables estáticas optimizadas
static gpio_output_t outputs[NUM_OUTPUTS];
static const cyhal_gpio_t OUTPUT_PINS[NUM_OUTPUTS] = {OUT1, OUT2, OUT3, OUT4};
static task_params_t *control_params;
static uint32_t command_stats[COMMAND_TABLE_SIZE] = {0}; // Estadísticas de uso

// Función de búsqueda binaria optimizada
static const command_lookup_t *find_command_fast(const char *cmd, size_t len)
{
    // Búsqueda lineal optimizada (mejor para tabla pequeña)
    for (int i = 0; i < COMMAND_TABLE_SIZE; i++)
    {
        if (len == command_table[i].cmd_len &&
            memcmp(cmd, command_table[i].cmd, len) == 0)
        {
            command_stats[i]++; // Actualizar estadísticas
            return &command_table[i];
        }
    }
    return NULL;
}

// Función optimizada para aplicar comandos usando bitmask
static void apply_command_bitmask(uint8_t output_mask, bool state)
{
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        if (output_mask & (1 << i))
        {
            if (outputs[i].state != state)
            { // Solo cambiar si es diferente
                outputs[i].state = state;
                outputs[i].last_change_time = current_time;
                cyhal_gpio_write(outputs[i].pin, state);
            }
        }
    }
}

// Función optimizada para generar respuesta de estado
static void generate_status_response(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "S1:%s S2:%s S3:%s S4:%s",
             outputs[0].state ? "ON" : "OFF",
             outputs[1].state ? "ON" : "OFF",
             outputs[2].state ? "ON" : "OFF",
             outputs[3].state ? "ON" : "OFF");
}

// Función optimizada para generar respuesta de comando
static void generate_command_response(const command_lookup_t *cmd_info, char *buffer, size_t buffer_size)
{
    if (cmd_info->is_multi_output)
    {
        snprintf(buffer, buffer_size, "TODAS LAS SALIDAS: %s",
                 cmd_info->state ? "ON" : "OFF");
    }
    else
    {
        // Determinar qué salida fue afectada
        int output_num = 0;
        uint8_t mask = cmd_info->output_mask;
        while (mask >>= 1)
            output_num++; // Encuentra el bit activo
        output_num++;     // Ajustar a base 1

        snprintf(buffer, buffer_size, "SALIDA %d: %s",
                 output_num, cmd_info->state ? "ON" : "OFF");
    }
}

// Función para inicializar GPIO de manera optimizada
static cy_rslt_t initialize_gpio_outputs(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        outputs[i].pin = OUTPUT_PINS[i];
        outputs[i].state = false;
        outputs[i].last_change_time = 0;

        cy_rslt_t gpio_result = cyhal_gpio_init(outputs[i].pin, CYHAL_GPIO_DIR_OUTPUT,
                                                CYHAL_GPIO_DRIVE_STRONG, false);

        if (gpio_result != CY_RSLT_SUCCESS)
        {
            printf("ERROR: GPIO %d init failed: 0x%lX\n", i + 1, gpio_result);
            result = gpio_result; // Guardar el primer error pero continuar
        }
    }

    return result;
}

// Función principal optimizada
void control(void *arg)
{
    control_params = (task_params_t *)arg;

    // Verificar parámetros
    if (!control_params || !control_params->queue_tcp_to_control ||
        !control_params->queue_control_to_tcp)
    {
        printf("ERROR: Parámetros de control inválidos\n");
        return;
    }

    // Inicializar GPIO
    if (initialize_gpio_outputs() != CY_RSLT_SUCCESS)
    {
        printf("ERROR: Inicialización GPIO fallida\n");
        return;
    }


    message_t received_msg;
    message_t response_msg;
    char response_buffer[64];

    // Variables de optimización
    TickType_t queue_timeout = pdMS_TO_TICKS(50); // Timeout más corto
    uint32_t processed_commands = 0;

    for (;;)
    {
        // Recepción optimizada con timeout más corto
        if (xQueueReceive(control_params->queue_tcp_to_control, &received_msg, queue_timeout) == pdTRUE)
        {

            processed_commands++;

            // Preparar mensaje de respuesta (optimizado)
            response_msg.command = CMD_CONTROL_TO_TCP;
            response_msg.value = received_msg.value; // Mantener client ID

            // Limpiar caracteres de control (optimizado)
            char *cmd_start = received_msg.data;
            while (*cmd_start == ' ' || *cmd_start == '\t')
                cmd_start++; // Skip whitespace

            size_t cmd_len = strlen(cmd_start);
            if (cmd_len > 0 && (cmd_start[cmd_len - 1] == '\n' || cmd_start[cmd_len - 1] == '\r'))
            {
                cmd_len--; // Remove trailing newline
                cmd_start[cmd_len] = '\0';
            }

            // Búsqueda optimizada del comando
            const command_lookup_t *cmd_info = find_command_fast(cmd_start, cmd_len);

            if (cmd_info)
            {
                if (cmd_info->is_status)
                {
                    // Comando de estado
                    generate_status_response(response_buffer, sizeof(response_buffer));
                }
                else
                {
                    // Comando de control
                    apply_command_bitmask(cmd_info->output_mask, cmd_info->state);
                    generate_command_response(cmd_info, response_buffer, sizeof(response_buffer));
                }

                strcpy(response_msg.data, response_buffer);
                printf("Control[%lu]: %s -> %s\n", processed_commands, cmd_start, response_buffer);
                printf("Comandos procesados: %lu\n", processed_commands);
                printf("Comandos mas usados:\n");
                for (int i = 0; i < 5 && i < COMMAND_TABLE_SIZE; i++)
                {
                    if (command_stats[i] > 0)
                    {
                        printf("  %s: %lu veces\n\n", command_table[i].cmd, command_stats[i]);
                    }
                }
                printf("Estado actual: S1=%s S2=%s S3=%s S4=%s\n",
                       outputs[0].state ? "ON" : "OFF",
                       outputs[1].state ? "ON" : "OFF",
                       outputs[2].state ? "ON" : "OFF",
                       outputs[3].state ? "ON" : "OFF");
                printf("===============================\n\n");
            }
            else
            {
                // Comando no reconocido
                printf("\x1b[0m"); 
                printf("\x1b[30m");  
                strcpy(response_msg.data, "COMANDO NO RECONOCIDO");
                printf("Control: Comando invalido: '%s'\n", cmd_start);
            }

            // Envío optimizado de respuesta
            BaseType_t send_result = xQueueSend(control_params->queue_control_to_tcp,
                                                &response_msg, pdMS_TO_TICKS(100));

            if (send_result != pdTRUE)
            {
                printf("Control: ERROR - Cola TCP llena\n");
            }
        }


        // Yield más inteligente - solo si no hay mensajes pendientes
        if (uxQueueMessagesWaiting(control_params->queue_tcp_to_control) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(5)); // Delay más corto cuando no hay trabajo
        }
    }
}
