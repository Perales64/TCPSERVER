// types.h - Verificar que existan estas definiciones
#ifndef TYPES_H_
#define TYPES_H_

#include "cyhal.h"
#include <stdbool.h>

// Comandos para comunicación entre tareas
typedef enum {
    CMD_TCP_TO_CONTROL = 1,
    CMD_CONTROL_TO_TCP = 2,
    CMD_IA_TO_TCP = 3
} command_type_t;

// Estructura de mensaje para colas
typedef struct {
    command_type_t command;
    uint32_t value;  // ID de cliente o otros datos
    char data[64];   // Datos del mensaje
} message_t;

// Parámetros para las tareas (punteros a colas)
typedef struct {
    QueueHandle_t queue_tcp_to_control;
    QueueHandle_t queue_control_to_tcp;
    QueueHandle_t queue_ia_to_tcp;
} task_params_t;

#endif /* TYPES_H_ */