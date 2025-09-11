#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cy_secure_sockets.h"
#include "cy_wcm.h"
#include "cy_nw_helper.h"
#include <string.h>
#include "tcp_server.h"
#include "config.h"

/*******************************************************************************
 * Enhanced Structures
 *******************************************************************************/
typedef enum
{
    CLIENT_STATE_DISCONNECTED = 0,
    CLIENT_STATE_CONNECTED,
    CLIENT_STATE_ACTIVE,
    CLIENT_STATE_TIMEOUT,
    CLIENT_STATE_ERROR
} client_state_t;

typedef struct
{
    cy_socket_t socket;
    client_state_t state;
    TaskHandle_t task_handle;
    uint32_t client_id;
    uint32_t last_activity;
    cy_socket_sockaddr_t peer_addr;
} client_info_t;

typedef enum
{
    ERROR_RECOVERABLE = 0,
    ERROR_CRITICAL,
    ERROR_NETWORK,
    ERROR_RESOURCE
} error_type_t;

/*******************************************************************************
 * Enhanced Global Variables
 *******************************************************************************/
static cy_socket_t server_socket;
static cy_socket_sockaddr_t server_addr;
static client_info_t clients[MAX_CLIENTS];
static SemaphoreHandle_t clients_mutex;
static bool server_running = false;
static uint32_t next_client_id = 1;

/*******************************************************************************
 * Enhanced Error Handling System
 *******************************************************************************/
typedef struct
{
    uint32_t recoverable_errors;
    uint32_t critical_errors;
    uint32_t network_errors;
    uint32_t last_error_time;
    cy_rslt_t last_error_code;
} error_stats_t;

static error_stats_t error_stats = {0};

/*******************************************************************************
 * Function: classify_error
 * Classifies error type based on error code
 *******************************************************************************/
static error_type_t classify_error(cy_rslt_t error_code)
{
    // Network related errors - recoverable
    if (error_code == CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_CLOSED ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_HOST_NOT_FOUND)
    {
        return ERROR_NETWORK;
    }

    // Resource errors - may be recoverable
    if (error_code == CY_RSLT_MODULE_SECURE_SOCKETS_NOMEM ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_ADDRESS_IN_USE)
    {
        return ERROR_RESOURCE;
    }

    // System errors - critical
    if (error_code == CY_RSLT_MODULE_SECURE_SOCKETS_INVALID_SOCKET ||
        error_code == CY_RSLT_MODULE_SECURE_SOCKETS_PROTOCOL_NOT_SUPPORTED)
    {
        return ERROR_CRITICAL;
    }

    // Default to recoverable
    return ERROR_RECOVERABLE;
}

/*******************************************************************************
 * Function: handle_error_enhanced
 * Enhanced error handling with recovery strategies
 *******************************************************************************/
static bool handle_error_enhanced(const char *context, cy_rslt_t error_code, bool is_critical_path)
{
    error_type_t error_type = classify_error(error_code);
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Update error statistics
    error_stats.last_error_code = error_code;
    error_stats.last_error_time = current_time;

    switch (error_type)
    {
    case ERROR_RECOVERABLE:
        error_stats.recoverable_errors++;
        printf("RECOVERABLE ERROR in %s: 0x%08lX (Count: %lu)\n",
               context, error_code, error_stats.recoverable_errors);

        // Simple backoff strategy
        vTaskDelay(pdMS_TO_TICKS(100 * error_stats.recoverable_errors));
        return true; // Continue operation

    case ERROR_NETWORK:
        printf("\x1b[31m");
        error_stats.network_errors++;
        printf("NETWORK ERROR in %s: 0x%08lX (Count: %lu)\n",
               context, error_code, error_stats.network_errors);

        // For critical path, attempt immediate recovery
        if (is_critical_path && error_stats.network_errors < 5)
        {
            printf("Attempting network recovery...\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            return true;
        }
        return false; // Need restart of network component

    case ERROR_RESOURCE:
        printf("RESOURCE ERROR in %s: 0x%08lX\n", context, error_code);

        // Wait for resources to become available
        if (error_stats.recoverable_errors < 10)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            return true;
        }
        return false;

    case ERROR_CRITICAL:
    default:
        error_stats.critical_errors++;
        printf("CRITICAL ERROR in %s: 0x%08lX (Count: %lu)\n",
               context, error_code, error_stats.critical_errors);

        // Only terminate if we're in a critical path and have too many errors
        if (is_critical_path && error_stats.critical_errors > 3)
        {
            printf("SYSTEM HALT: Too many critical errors\n");
            CY_ASSERT(0);
        }
        return false;
    }
}

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
    return -1; // No free slots
}

static void limpiar_cliente(int client_index)
{
    if (client_index < 0 || client_index >= MAX_CLIENTS)
        return;

    client_info_t *client = &clients[client_index];

    printf("Cleaning up client %lu (slot %d)\n", client->client_id, client_index);

    // Close socket if valid
    if (client->socket != CY_SOCKET_INVALID_HANDLE)
    {
        cy_socket_disconnect(client->socket, 0);
        cy_socket_delete(client->socket);
        client->socket = CY_SOCKET_INVALID_HANDLE;
    }

    // Delete task if running
    if (client->task_handle != NULL)
    {
        vTaskDelete(client->task_handle);
        client->task_handle = NULL;
    }

    // Reset client info
    memset(client, 0, sizeof(client_info_t));
    client->state = CLIENT_STATE_DISCONNECTED;
    client->socket = CY_SOCKET_INVALID_HANDLE;
}

/*******************************************************************************
 * Function: client_task
 * Individual task for handling each client
 *******************************************************************************/
static void client_task(void *param)
{
    int client_index = (int)(intptr_t)param;
    client_info_t *client = &clients[client_index];
    char buffer[BUFFER_SIZE];
    uint32_t bytes_received, bytes_sent;
    cy_rslt_t result;

    printf("[%lu] CLIENT %lu CONNECTED (slot %d)\n", client->client_id, client_index);

    // Set socket timeout
    uint32_t timeout = 400; // 400ms seconds
    cy_socket_setsockopt(client->socket, CY_SOCKET_SOL_SOCKET,
                         CY_SOCKET_SO_RCVTIMEO, &timeout, sizeof(timeout));

    client->state = CLIENT_STATE_ACTIVE;
    client->last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Client communication loop
    while (client->state == CLIENT_STATE_ACTIVE)
    {
        // Check for timeout
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if ((current_time - client->last_activity) > CLIENT_TIMEOUT_MS)
        {
            printf("Client %lu timeout\n", client->client_id);
            client->state = CLIENT_STATE_TIMEOUT;
            break;
        }

        // Attempt to receive data
        memset(buffer, 0, sizeof(buffer));
        result = cy_socket_recv(client->socket, buffer, sizeof(buffer) - 1,
                                CY_SOCKET_FLAGS_NONE, &bytes_received);

        if (result == CY_RSLT_SUCCESS && bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            client->last_activity = current_time;
            printf("\x1b[38;5;214m");
            printf("Client %lu sent: %s\n", client->client_id, buffer);
        }
        else if (result == CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT)
        {
            // Timeout is normal, continue loop
            continue;
        }
        else
        {
            // Other error occurred
            if (!handle_error_enhanced("Client receive", result, false))
            {
                printf("Client %lu disconnected due to error\n", client->client_id);
                client->state = CLIENT_STATE_ERROR;
                break;
            }
        }

        // Small delay to prevent CPU saturation
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    printf("Client task ending for client %lu\n", client->client_id);

    // Mark for cleanup (will be cleaned up by main server task)
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        client->state = CLIENT_STATE_DISCONNECTED;
        xSemaphoreGive(clients_mutex);
    }

    // Task will terminate itself
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Function: accept_new_client
 * Accepts a new client connection and creates handler task
 *******************************************************************************/
static void accept_new_client(void)
{
    cy_socket_sockaddr_t peer_addr;
    uint32_t peer_len = sizeof(peer_addr);
    cy_socket_t new_socket;
    cy_rslt_t result;
    BaseType_t task_result;

    // Try to accept a new connection (non-blocking)
    result = cy_socket_accept(server_socket, &peer_addr, &peer_len, &new_socket);

    if (result == CY_RSLT_SUCCESS)
    {
        // Find free client slot
        int client_index = get_free_client_slot();

        if (client_index >= 0)
        {
            // Initialize client info
            if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                clients[client_index].socket = new_socket;
                clients[client_index].state = CLIENT_STATE_CONNECTED;
                clients[client_index].client_id = next_client_id++;
                clients[client_index].peer_addr = peer_addr;
                clients[client_index].last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;

                // Create client handler task
                char task_name[20];
                snprintf(task_name, sizeof(task_name), "Client_%lu", clients[client_index].client_id);

                task_result = xTaskCreate(
                    client_task,
                    task_name,
                    CLIENT_TASK_STACK_SIZE,
                    (void *)(intptr_t)client_index,
                    CLIENT_TASK_PRIORITY,
                    &clients[client_index].task_handle);

                if (task_result == pdPASS)
                {
                    printf("\x1b[35m");
                    printf("New client %lu connected from %lu.%lu.%lu.%lu (slot %d)\n",
                           clients[client_index].client_id,
                           (peer_addr.ip_address.ip.v4 >> 0) & 0xFF,
                           (peer_addr.ip_address.ip.v4 >> 8) & 0xFF,
                           (peer_addr.ip_address.ip.v4 >> 16) & 0xFF,
                           (peer_addr.ip_address.ip.v4 >> 24) & 0xFF,
                           client_index);
                }
                else
                {
                    printf("Failed to create client task\n");
                    cleanup_client(client_index);
                }

                xSemaphoreGive(clients_mutex);
            }
        }
        else
        {
            // No free slots, reject connection
            printf("Server full, rejecting new connection\n");
            const char *reject_msg = "Server full, try again later\n";
            uint32_t bytes_sent;
            cy_socket_send(new_socket, reject_msg, strlen(reject_msg),
                           CY_SOCKET_FLAGS_NONE, &bytes_sent);
            cy_socket_disconnect(new_socket, 0);
            cy_socket_delete(new_socket);
        }
    }
    else if (result != CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT)
    {
        // Real error occurred
        handle_error_enhanced("Accept connection", result, false);
    }
}

/*******************************************************************************
 * Function: cleanup_disconnected_clients
 * Cleans up clients that have disconnected
 *******************************************************************************/
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

/*******************************************************************************
 * Function: print_server_status
 * Prints current server status
 *******************************************************************************/
static void print_server_status(void)
{
    static uint32_t last_status_time = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Print status every 30 seconds
    if ((current_time - last_status_time) > 30000)
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
        printf("\n=== SERVER STATUS [%lu] ===\n", xTaskGetTickCount());
        printf("Active clients: %d/%d\n", connected_clients, MAX_CLIENTS);
        printf("Total clients handled: %lu\n", next_client_id - 1);
        printf("Error stats - Recov: %lu, Net: %lu, Crit: %lu\n",
               error_stats.recoverable_errors, error_stats.network_errors,
               error_stats.critical_errors);
        printf("Uptime: %lu seconds\n", current_time / 1000);
        printf("===============================\n\n");

        last_status_time = current_time;
    }
}

/*******************************************************************************
 * WiFi and Server Setup Functions (from original code)
 *******************************************************************************/
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
        printf("Connecting to WiFi... attempt %d\n", retry_count + 1);
        result = cy_wcm_connect_ap(&params, &ip);

        if (result == CY_RSLT_SUCCESS)
        {
            printf("WiFi connected! IP: %lu.%lu.%lu.%lu\n",
                   (ip.ip.v4 >> 0) & 0xFF, (ip.ip.v4 >> 8) & 0xFF,
                   (ip.ip.v4 >> 16) & 0xFF, (ip.ip.v4 >> 24) & 0xFF);

            server_addr.ip_address.ip.v4 = ip.ip.v4;
            server_addr.ip_address.version = CY_SOCKET_IP_VER_V4;
            server_addr.port = TCP_PORT;
            return CY_RSLT_SUCCESS;
        }

        if (!handle_error_enhanced("WiFi connection", result, true))
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

    // Set socket to non-blocking for accept
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

    printf("Enhanced TCP server listening on port %d (max %d clients)\n",
           TCP_PORT, MAX_CLIENTS);
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Function: tarea_TCPserver
 * Enhanced TCP Server Task - Main Loop
 *******************************************************************************/
void tarea_TCPserver(void *arg)
{
    cy_rslt_t result;

    // Initialize client array
    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = CY_SOCKET_INVALID_HANDLE;
        clients[i].state = CLIENT_STATE_DISCONNECTED;
    }

    // Create mutex for client array protection
    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL)
    {
        printf("Failed to create clients mutex\n");
        vTaskDelete(NULL);
        return;
    }

    // Connect to WiFi with retry logic
    do
    {
        result = connect_wifi();
        if (result != CY_RSLT_SUCCESS)
        {
            printf("WiFi connection failed, retrying in %d seconds...\n",
                   SERVER_RECOVERY_DELAY_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(SERVER_RECOVERY_DELAY_MS));
        }
    } while (result != CY_RSLT_SUCCESS);

    // Initialize sockets
    result = cy_socket_init();
    if (result != CY_RSLT_SUCCESS)
    {
        handle_error_enhanced("Socket initialization", result, true);
        vTaskDelete(NULL);
        return;
    }

    // Create and configure server socket
    do
    {
        result = create_server_socket();
        if (result != CY_RSLT_SUCCESS)
        {
            if (!handle_error_enhanced("Server creation", result, true))
            {
                vTaskDelay(pdMS_TO_TICKS(SERVER_RECOVERY_DELAY_MS));
            }
        }
    } while (result != CY_RSLT_SUCCESS);

    server_running = true;

    // Main server loop
    while (server_running)
    {
        aceptar_nuevo_cliente();

        // Clean up disconnected clients
        cleanup_disconnected_clients();

        // Print periodic status
        print_server_status();

        // Small delay to prevent CPU saturation
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Cleanup on exit
    printf("Server shutting down...\n");

    // Clean up all clients
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            cleanup_client(i);
        }
        xSemaphoreGive(clients_mutex);
    }

    // Clean up server socket
    if (server_socket != CY_SOCKET_INVALID_HANDLE)
    {
        cy_socket_disconnect(server_socket, 0);
        cy_socket_delete(server_socket);
    }

    vSemaphoreDelete(clients_mutex);
    vTaskDelete(NULL);
}