#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"

// Estructura para pasar las colas entre tareas
typedef struct {
    QueueHandle_t queue_tcp_to_control;
    QueueHandle_t queue_control_to_tcp;
    QueueHandle_t queue_ia_to_tcp;
} task_params_t;

// Estructura para los mensajes entre tareas
typedef struct
{
    uint8_t command;        // Tipo de comando (ver defines abajo)
    char data[64];         // Datos del comando (texto del comando)
    uint32_t value;        // Para client ID u otros valores numéricos
} message_t;

// Estructura para entradas de comandos (usada en control.c)
typedef struct {
    const char* cmd;       // Comando como string
    int output_num;        // Número de salida (0 = todas, 1-4 = específica)
    bool state;           // Estado deseado (true = ON, false = OFF)
    bool is_status_cmd;   // Si es comando de estado
} command_entry_t;

// Defines para tipos de comandos entre tareas
#define CMD_IA_TO_TCP 1         // Comando de IA a TCP
#define CMD_TCP_TO_CONTROL 2    // Comando de TCP a Control
#define CMD_CONTROL_TO_TCP 3    // Respuesta de Control a TCP

#endif /* TYPES_H */