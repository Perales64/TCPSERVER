#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
typedef struct {
    QueueHandle_t queue_tcp_to_control;
    QueueHandle_t queue_control_to_tcp;
    QueueHandle_t queue_ia_to_tcp;
} task_params_t;

typedef struct
{
    uint8_t command;
    char data[64];
    uint32_t value;  // Para client ID u otros valores
} message_t;

typedef struct {
    const char* cmd;
    int output_num;
    bool state;
    bool is_status_cmd;
} command_entry_t;

#define CMD_IA_TO_TCP 1
#define CMD_TCP_TO_CONTROL 2
#define CMD_CONTROL_TO_TCP 3

#endif