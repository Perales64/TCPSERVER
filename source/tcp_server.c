#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cy_secure_sockets.h"
#include "cy_wcm.h"
#include "cy_nw_helper.h"
#include <string.h>
#include <queue.h>
#include "tcp_server.h"
#include "config.h"
#include "types.h"

// TIPOS Y ENUMERACIONES
typedef enum
{
    CLIENT_STATE_DISCONNECTED = 0,
    CLIENT_STATE_CONNECTED,
    CLIENT_STATE_ACTIVE,
    CLIENT_STATE_TIMEOUT,
    CLIENT_STATE_ERROR
} client_state_t;

typedef enum
{
    ERROR_RECOVERABLE = 0,
    ERROR_CRITICAL,
    ERROR_NETWORK,
    ERROR_RESOURCE
} error_type_t;

typedef struct
{
    cy_socket_t socket;
    client_state_t state;
    TaskHandle_t task_handle;
    uint32_t client_id;
    uint32_t last_activity;
    cy_socket_sockaddr_t peer_addr;
    task_params_t *params; // Agregar parÃ¡metros de colas
} client_info_t;

typedef struct
{
    uint32_t recoverable_errors;
    uint32_t critical_errors;
    uint32_t network_errors;
    uint32_t last_error_time;
    cy_rslt_t last_error_code;
} error_stats_t;

typedef struct
{
    message_t messages[8]; // Buffer circular de 8 mensajes
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint8_t count;
} response_buffer_t;

// VARIABLES GLOBALES

static cy_socket_t server_socket;
static cy_socket_sockaddr_t server_addr;
static client_info_t clients[MAX_CLIENTS];
static SemaphoreHandle_t clients_mutex;
static bool server_running = false;
static uint32_t next_client_id = 1;
static error_stats_t error_stats = {0};
static task_params_t *global_params; // ParÃ¡metros globales
static response_buffer_t response_buffers[MAX_CLIENTS];

// FUNCIONES DE MANEJO DE ERRORES (mantener las mismas)

static error_type_t classify_error(cy_rslt_t error_code)
{
    if (error_code == CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_CLOSED ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_HOST_NOT_FOUND)
    {
        return ERROR_NETWORK;
    }

    if (error_code == CY_RSLT_MODULE_SECURE_SOCKETS_NOMEM ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_ADDRESS_IN_USE)
    {
        return ERROR_RESOURCE;
    }

    if (error_code == CY_RSLT_MODULE_SECURE_SOCKETS_INVALID_SOCKET ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_PROTOCOL_NOT_SUPPORTED)
    {
        return ERROR_CRITICAL;
    }

    return ERROR_RECOVERABLE;
}

static bool handle_error_enhanced(const char *context, cy_rslt_t error_code, bool is_critical_path)
{
    error_type_t error_type = classify_error(error_code);
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    error_stats.last_error_code = error_code;
    error_stats.last_error_time = current_time;

    switch (error_type)
    {
    case ERROR_RECOVERABLE:
        error_stats.recoverable_errors++;
        printf("ERROR RECUPERABLE en %s: 0x%08lX (Contador: %lu)\n",
               context, error_code, error_stats.recoverable_errors);
        vTaskDelay(pdMS_TO_TICKS(100 * error_stats.recoverable_errors));
        return true;

    case ERROR_NETWORK:
        printf("\x1b[31m");
        error_stats.network_errors++;
        printf("ERROR DE RED en %s: 0x%08lX (Contador: %lu)\n",
               context, error_code, error_stats.network_errors);

        if (is_critical_path && error_stats.network_errors < 5)
        {
            printf("Intentando recuperaciÃ³n de red...\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            return true;
        }
        return false;

    case ERROR_RESOURCE:
        printf("ERROR DE RECURSOS en %s: 0x%08lX\n", context, error_code);

        if (error_stats.recoverable_errors < 10)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            return true;
        }
        return false;

    case ERROR_CRITICAL:
    default:
        error_stats.critical_errors++;
        printf("ERROR CRÃTICO en %s: 0x%08lX (Contador: %lu)\n",
               context, error_code, error_stats.critical_errors);

        if (is_critical_path && error_stats.critical_errors > 3)
        {
            printf("DETENCIÃ“N DEL SISTEMA: Demasiados errores crÃ­ticos\n");
            CY_ASSERT(0);
        }
        return false;
    }
}

// FUNCIONES DE GESTIÃ“N DE CLIENTES
static int obtener_ranura_cliente_libre(void)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].state == CLIENT_STATE_DISCONNECTED)
            {
                xSemaphoreGive(clients_mutex);
                return i;
            }
        }
        xSemaphoreGive(clients_mutex);
    }
    return -1;
}

static void cleanup_client(int client_index)
{
    if (client_index < 0 || client_index >= MAX_CLIENTS)
        return;

    client_info_t *client = &clients[client_index];

    printf("Limpiando cliente %lu (ranura %d)\n", client->client_id, client_index);

    if (client->socket != CY_SOCKET_INVALID_HANDLE)
    {
        cy_socket_disconnect(client->socket, 0);
        cy_socket_delete(client->socket);
        client->socket = CY_SOCKET_INVALID_HANDLE;
    }

    if (client->task_handle != NULL)
    {
        vTaskDelete(client->task_handle);
        client->task_handle = NULL;
    }

    memset(client, 0, sizeof(client_info_t));
    client->state = CLIENT_STATE_DISCONNECTED;
    client->socket = CY_SOCKET_INVALID_HANDLE;
}

static void cleanup_disconnected_clients(void)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].state == CLIENT_STATE_DISCONNECTED &&
                clients[i].socket != CY_SOCKET_INVALID_HANDLE)
            {
                cleanup_client(i);
            }
        }
        xSemaphoreGive(clients_mutex);
    }
}

// TAREA DE CLIENTE ACTUALIZADA
static bool process_control_responses(client_info_t *client, int client_index)
{
    message_t response_msg;
    response_buffer_t *rb = &response_buffers[client_index];
    bool has_responses = false;

    // Leer mÃºltiples respuestas de una vez (batch processing)
    while (xQueueReceive(client->params->queue_control_to_tcp, &response_msg, 0) == pdTRUE)
    {
        // Solo procesar si es para este cliente o broadcast
        if (response_msg.value == client->client_id || response_msg.value == 0)
        {
            // Almacenar en buffer circular
            if (rb->count < 8)
            { // Buffer no lleno
                rb->messages[rb->head] = response_msg;
                rb->head = (rb->head + 1) % 8;
                rb->count++;
                has_responses = true;
            }
            else
            {
                printf("TCP: Buffer de respuestas lleno para cliente %lu\n", client->client_id);
            }
        }
    }

    return has_responses;
}
static void send_buffered_responses(client_info_t *client, int client_index)
{
    response_buffer_t *rb = &response_buffers[client_index];
    char response_buffer[BUFFER_SIZE];
    uint32_t bytes_sent;
    cy_rslt_t result;

    while (rb->count > 0)
    {
        message_t *msg = &rb->messages[rb->tail];

        // Formatear respuesta optimizada
        int len = snprintf(response_buffer, sizeof(response_buffer),
                           "%s\n> ", msg->data);

        result = cy_socket_send(client->socket, response_buffer, len,
                                CY_SOCKET_FLAGS_NONE, &bytes_sent);

        if (result == CY_RSLT_SUCCESS)
        {
            // Remover del buffer circular
            rb->tail = (rb->tail + 1) % 8;
            rb->count--;
        }
        else
        {
            printf("TCP: Error enviando a cliente %lu: 0x%08lX\n",
                   client->client_id, result);
            break; // Detener envÃ­o si hay error
        }
    }
}
static void process_client_command(client_info_t *client, char *buffer, size_t bytes_received)
{
    // Limpiar buffer de manera optimizada
    buffer[bytes_received] = '\0';

    // Remover caracteres de control de manera mÃ¡s eficiente
    char *cmd_end = buffer + bytes_received - 1;
    while (cmd_end >= buffer && (*cmd_end == '\n' || *cmd_end == '\r' || *cmd_end == ' '))
    {
        *cmd_end-- = '\0';
    }

    // Skip leading whitespace
    char *cmd_start = buffer;
    while (*cmd_start == ' ' || *cmd_start == '\t')
        cmd_start++;

    if (strlen(cmd_start) == 0)
        return; // Comando vacÃ­o

    // Logging optimizado con color
    printf("\x1b[38;5;214m[%lu] CMD: %s\x1b[0m\n", client->client_id, cmd_start);

    // Preparar mensaje de control optimizado
    message_t control_msg = {
        .command = CMD_TCP_TO_CONTROL,
        .value = client->client_id,
        .data = {0}};

    // Copiar comando de manera segura
    size_t cmd_len = strlen(cmd_start);
    if (cmd_len >= sizeof(control_msg.data))
    {
        cmd_len = sizeof(control_msg.data) - 1;
    }
    memcpy(control_msg.data, cmd_start, cmd_len);
    control_msg.data[cmd_len] = '\0';

    // EnvÃ­o no bloqueante al control
    BaseType_t send_result = xQueueSend(client->params->queue_tcp_to_control,
                                        &control_msg, pdMS_TO_TICKS(50));

    if (send_result != pdTRUE)
    {
        printf("TCP: ADVERTENCIA - Cola control llena, cliente %lu\n", client->client_id);

        // Enviar mensaje de error inmediato al cliente
        const char *error_msg = "SERVIDOR OCUPADO - Intente nuevamente\n> ";
        uint32_t bytes_sent;
        cy_socket_send(client->socket, error_msg, strlen(error_msg),
                       CY_SOCKET_FLAGS_NONE, &bytes_sent);
    }
}

static void client_task(void *param)
{

    int client_index = (int)(intptr_t)param;
    client_info_t *client = &clients[client_index];
    char rx_buffer[BUFFER_SIZE];
    uint32_t bytes_received;
    cy_rslt_t result;

    printf("[%lu] CLIENTE CONECTADO (slot %d)\n", client->client_id, client_index);

    // Configurar timeouts optimizados
    uint32_t rx_timeout = 200; // Timeout mÃ¡s corto para mejor responsividad
    cy_socket_setsockopt(client->socket, CY_SOCKET_SOL_SOCKET,
                         CY_SOCKET_SO_RCVTIMEO, &rx_timeout, sizeof(rx_timeout));

    client->state = CLIENT_STATE_ACTIVE;
    client->last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Inicializar buffer circular para este cliente
    memset(&response_buffers[client_index], 0, sizeof(response_buffer_t));

    // Mensaje de bienvenida optimizado
    const char *welcome = 
                        "\x1b[34m" // Yellow color
                        "\x1b[1m\n"
                        "      ___           ___                       ___           ___           ___     \n"
                        "     /\\  \\         /\\__\\          ___        /\\__\\         /\\  \\         /\\  \\    \n"
                        "    /::\\  \\       /::|  |        /\\  \\      /:/  /        /::\\  \\        \\:\\  \\   \n"
                        "   /:/\\:\\  \\     /:|:|  |        \\:\\  \\    /:/  /        /:/\\:\\  \\        \\:\\  \\  \n"
                        "  /::\\~\\:\\  \\   /:/|:|  |__      /::\\__\\  /:/  /  ___   /::\\~\\:\\  \\       /::\\  \\ \n"
                        " /:/\\:\\ \\:\\__\\ /:/ |:| /\\__\\  __/:/\\/__/ /:/__/  /\\__\\ /:/\\:\\ \\:\\__\\     /:/\\:\\__\\\n"
                        " \\/__\\:\\/:/  / \\/__|:|/:/  / /\\/:/  /    \\:\\  \\ /:/  / \\:\\~\\:\\ \\/__/    /:/  \\/__/\n"
                        "      \\::/  /      |:/:/  /  \\::/__/      \\:\\  /:/  /   \\:\\ \\:\\__\\     /:/  /     \n"
                        "      /:/  /       |::/  /    \\:\\__\\       \\:\\/:/  /     \\:\\ \\/__/     \\/__/      \n"
                        "     /:/  /        /:/  /      \\/__/        \\::/  /       \\:\\__\\                  \n"
                        "     \\/__/         \\/__/                     \\/__/         \\/__/                  \n"
                        "\x1b[0m"
                        "=== CONTROL SERVER v2.0 ===\n"
                          "Comandos: 1_ON/OFF, 2_ON/OFF, 3_ON/OFF, 4_ON/OFF\n"
                          "         ALL_ON, ALL_OFF, STATUS\n"
                          "Listo para comandos...\n> ";
    uint32_t bytes_sent;
    cy_socket_send(client->socket, welcome, strlen(welcome), CY_SOCKET_FLAGS_NONE, &bytes_sent);

    // Variables de optimizaciÃ³n
    uint32_t commands_processed = 0;
    uint32_t last_activity_check = 0;

    while (client->state == CLIENT_STATE_ACTIVE)
    {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // VerificaciÃ³n de timeout optimizada (cada 5 ciclos)
        if (++last_activity_check % 5 == 0)
        {
            if ((current_time - client->last_activity) > CLIENT_TIMEOUT_MS)
            {
                printf("Cliente %lu - timeout\n", client->client_id);
                client->state = CLIENT_STATE_TIMEOUT;
                break;
            }
        }

        // Procesar respuestas del control (prioridad alta)
        if (process_control_responses(client, client_index))
        {
            send_buffered_responses(client, client_index);
        }

        // Recibir comandos del cliente
        result = cy_socket_recv(client->socket, rx_buffer, sizeof(rx_buffer) - 1,
                                CY_SOCKET_FLAGS_NONE, &bytes_received);

        if (result == CY_RSLT_SUCCESS && bytes_received > 0)
        {
            client->last_activity = current_time;
            commands_processed++;

            process_client_command(client, rx_buffer, bytes_received);
        }
        else if (result == CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT)
        {
            // Timeout normal - continuar
            continue;
        }
        else
        {
            // Error de comunicaciÃ³n
            if (!handle_error_enhanced("Client recv", result, false))
            {
                printf("Cliente %lu desconectado por error\n", client->client_id);
                client->state = CLIENT_STATE_ERROR;
                break;
            }
        }

        // Yield inteligente - menor delay si hay actividad
        vTaskDelay(pdMS_TO_TICKS(response_buffers[client_index].count > 0 ? 5 : 15));
    }

    printf("Cliente %lu finalizo - Comandos procesados: %lu\n",
           client->client_id, commands_processed);

    // Limpiar buffer circular
    memset(&response_buffers[client_index], 0, sizeof(response_buffer_t));

    // Cambiar estado para limpieza
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        client->state = CLIENT_STATE_DISCONNECTED;
        xSemaphoreGive(clients_mutex);
    }

    vTaskDelete(NULL);
}

// FUNCIONES DE CONEXIÃ“N (mantener las mismas pero actualizar accept_new_client)

static void accept_new_client(void)
{
    cy_socket_sockaddr_t peer_addr;
    uint32_t peer_len = sizeof(peer_addr);
    cy_socket_t new_socket;
    cy_rslt_t result;
    BaseType_t task_result;

    result = cy_socket_accept(server_socket, &peer_addr, &peer_len, &new_socket);

    if (result == CY_RSLT_SUCCESS)
    {
        int client_index = obtener_ranura_cliente_libre();

        if (client_index >= 0)
        {
            if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                clients[client_index].socket = new_socket;
                clients[client_index].state = CLIENT_STATE_CONNECTED;
                clients[client_index].client_id = next_client_id++;
                clients[client_index].peer_addr = peer_addr;
                clients[client_index].last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;
                clients[client_index].params = global_params; // Asignar parÃ¡metros

                char task_name[20];
                snprintf(task_name, sizeof(task_name), "Cliente_%lu", clients[client_index].client_id);

                task_result = xTaskCreate(
                    client_task,
                    task_name,
                    CLIENT_TASK_STACK_SIZE,
                    (void *)(intptr_t)client_index,
                    CLIENT_TASK_PRIORITY,
                    &clients[client_index].task_handle);

                if (task_result == pdPASS)
                {
                    printf("\x1b[1m");  
                    printf("\x1b[3m");
                    printf("Nuevo cliente %lu conectado desde %lu.%lu.%lu.%lu (ranura %d)\n",
                           clients[client_index].client_id,
                           (peer_addr.ip_address.ip.v4 >> 0) & 0xFF,
                           (peer_addr.ip_address.ip.v4 >> 8) & 0xFF,
                           (peer_addr.ip_address.ip.v4 >> 16) & 0xFF,
                           (peer_addr.ip_address.ip.v4 >> 24) & 0xFF,
                           client_index);
                    printf("\x1b[0m");  
                }
                else
                {
                    printf("Error al crear la tarea del cliente\n");
                    cleanup_client(client_index);
                }

                xSemaphoreGive(clients_mutex);
            }
        }
        else
        {
            printf("Servidor lleno, rechazando nueva conexiÃ³n\n");
            const char *reject_msg = "Servidor lleno, intente mÃ¡s tarde\n";
            uint32_t bytes_sent;
            cy_socket_send(new_socket, reject_msg, strlen(reject_msg),
                           CY_SOCKET_FLAGS_NONE, &bytes_sent);
            cy_socket_disconnect(new_socket, 0);
            cy_socket_delete(new_socket);
        }
    }
    else if (result != CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT)
    {
        handle_error_enhanced("Aceptar conexiÃ³n", result, false);
    }
}

static cy_rslt_t connect_wifi(void)
{
    cy_wcm_config_t config = {.interface = CY_WCM_INTERFACE_TYPE_STA};
    cy_wcm_connect_params_t params = {0};
    cy_wcm_ip_address_t ip;
    cy_rslt_t result;
    int retry_count = 0;

    result = cy_wcm_init(&config);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    strcpy((char *)params.ap_credentials.SSID, WIFI_SSID);
    strcpy((char *)params.ap_credentials.password, WIFI_PASSWORD);
    params.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;

    while (retry_count < MAX_RETRIES)
    {
        printf("Conectando a WiFi... intento %d\n", retry_count + 1);
        result = cy_wcm_connect_ap(&params, &ip);

        if (result == CY_RSLT_SUCCESS)
        {
            // printf("\x1b[43m");
            printf("Conectado a WiFi\n");
            printf("\x1b[33m");
            printf("IP: %lu.%lu.%lu.%lu\n",
                   (ip.ip.v4 >> 0) & 0xFF, (ip.ip.v4 >> 8) & 0xFF,
                   (ip.ip.v4 >> 16) & 0xFF, (ip.ip.v4 >> 24) & 0xFF);

            server_addr.ip_address.ip.v4 = ip.ip.v4;
            server_addr.ip_address.version = CY_SOCKET_IP_VER_V4;
            server_addr.port = TCP_PORT;
            return CY_RSLT_SUCCESS;
        }

        if (!handle_error_enhanced("ConexiÃ³n WiFi", result, true))
        {
            break;
        }

        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    return result;
}

static cy_rslt_t create_server_socket(void)
{
    cy_rslt_t result;

    result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET,
                              CY_SOCKET_TYPE_STREAM,
                              CY_SOCKET_IPPROTO_TCP,
                              &server_socket);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    uint32_t nonblocking = 1;
    cy_socket_setsockopt(server_socket, CY_SOCKET_SOL_SOCKET,
                         CY_SOCKET_SO_NONBLOCK, &nonblocking, sizeof(nonblocking));

    result = cy_socket_bind(server_socket, &server_addr, sizeof(server_addr));
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    result = cy_socket_listen(server_socket, MAX_CLIENTS);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    printf("Puerto: %d (Maximo %d clientes)\n",
           TCP_PORT, MAX_CLIENTS);
    return CY_RSLT_SUCCESS;
}

// FUNCIONES DE MONITOREO Y ESTADO (mantener la misma)

static void print_server_status(void)
{
    static uint32_t last_status_time = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if ((current_time - last_status_time) > 10000)
    {
        int connected_clients = 0;

        if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].state == CLIENT_STATE_ACTIVE)
                {
                    connected_clients++;
                }
            }
            xSemaphoreGive(clients_mutex);
        }

        printf("\x1b[33m");
        printf("\n=== ESTADO DEL SERVIDOR [%lu] ===\n", xTaskGetTickCount());
        printf("Clientes activos: %d/%d\n", connected_clients, MAX_CLIENTS);
        printf("Total de clientes atendidos: %lu\n", next_client_id - 1);
        printf("Estadisticas de errores - Recup: %lu, Red: %lu, CrÃ­t: %lu\n",
               error_stats.recoverable_errors, error_stats.network_errors,
               error_stats.critical_errors);
        printf("Tiempo de funcionamiento: %lu segundos\n", current_time / 1000);
        printf("==================================\n\n");

        last_status_time = current_time;
    }
}

// FUNCIÃ“N PRINCIPAL DEL SERVIDOR
void tarea_TCPserver(void *arg)
{
    cy_rslt_t result;

    // Guardar parÃ¡metros globalmente
    global_params = (task_params_t *)arg;

    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = CY_SOCKET_INVALID_HANDLE;
        clients[i].state = CLIENT_STATE_DISCONNECTED;
    }

    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL)
    {
        printf("Error al crear el mutex de clientes\n");
        vTaskDelete(NULL);
        return;
    }

    do
    {
        result = connect_wifi();
        if (result != CY_RSLT_SUCCESS)
        {
            printf("ConexiÃ³n WiFi fallÃ³, reintentando en %d segundos...\n",
                   SERVER_RECOVERY_DELAY_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(SERVER_RECOVERY_DELAY_MS));
        }
    } while (result != CY_RSLT_SUCCESS);

    result = cy_socket_init();
    if (result != CY_RSLT_SUCCESS)
    {
        handle_error_enhanced("InicializaciÃ³n de sockets", result, true);
        vTaskDelete(NULL);
        return;
    }

    do
    {
        result = create_server_socket();
        if (result != CY_RSLT_SUCCESS)
        {
            if (!handle_error_enhanced("CreaciÃ³n del servidor", result, true))
            {
                vTaskDelay(pdMS_TO_TICKS(SERVER_RECOVERY_DELAY_MS));
            }
        }
    } while (result != CY_RSLT_SUCCESS);

    server_running = true;

    while (server_running)
    {
        accept_new_client();
        cleanup_disconnected_clients();
        print_server_status();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("Cerrando servidor...\n");

    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            cleanup_client(i);
        }
        xSemaphoreGive(clients_mutex);
    }

    if (server_socket != CY_SOCKET_INVALID_HANDLE)
    {
        cy_socket_disconnect(server_socket, 0);
        cy_socket_delete(server_socket);
    }

    vSemaphoreDelete(clients_mutex);
    vTaskDelete(NULL);
}