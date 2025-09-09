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
* Simplified Global Variables
*******************************************************************************/
static cy_socket_t server_socket;
static cy_socket_t client_socket;
static cy_socket_sockaddr_t server_addr;
static bool client_connected = false;

/*******************************************************************************
* Simple Error Handler - Centralized
*******************************************************************************/
static void handle_error(const char* msg, cy_rslt_t result)
{
    printf("ERROR: %s (0x%08lX)\n", msg, result);
    CY_ASSERT(0);
}

/*******************************************************************************
* Connect to WiFi - Simplified Version
*******************************************************************************/
static cy_rslt_t connect_wifi(void)
{
    cy_wcm_config_t config = { .interface = CY_WCM_INTERFACE_TYPE_STA };
    cy_wcm_connect_params_t params = {0};
    cy_wcm_ip_address_t ip;
    cy_rslt_t result;
    
    // Initialize WiFi
    result = cy_wcm_init(&config);
    if (result != CY_RSLT_SUCCESS) return result;
    
    // Setup credentials
    strcpy((char*)params.ap_credentials.SSID, WIFI_SSID);
    strcpy((char*)params.ap_credentials.password, WIFI_PASSWORD);
    params.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;
    
    // Simple retry loop
    for (int i = 0; i < MAX_RETRIES; i++) {
        printf("Connecting to WiFi... attempt %d\n", i + 1);
        result = cy_wcm_connect_ap(&params, &ip);
        
        if (result == CY_RSLT_SUCCESS) {
            printf("WiFi IP: %lu.%lu.%lu.%lu\n", 
                   (ip.ip.v4 >> 0) & 0xFF,
                   (ip.ip.v4 >> 8) & 0xFF,
                   (ip.ip.v4 >> 16) & 0xFF,
                   (ip.ip.v4 >> 24) & 0xFF);
            
            // Setup server address
            server_addr.ip_address.ip.v4 = ip.ip.v4;
            server_addr.ip_address.version = CY_SOCKET_IP_VER_V4;
            server_addr.port = TCP_PORT;
            return CY_RSLT_SUCCESS;
        }
        
        cy_rtos_delay_milliseconds(1000);
    }
    
    return result;
}

/*******************************************************************************
* Create Server Socket - Simplified Version
*******************************************************************************/
static cy_rslt_t create_server_socket(void)
{
    cy_rslt_t result;
    
    // Create socket
    result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET, 
                             CY_SOCKET_TYPE_STREAM,
                             CY_SOCKET_IPPROTO_TCP, 
                             &server_socket);
    if (result != CY_RSLT_SUCCESS) return result;
    
    // Bind to port
    result = cy_socket_bind(server_socket, &server_addr, sizeof(server_addr));
    if (result != CY_RSLT_SUCCESS) return result;
    
    // Listen for connections
    result = cy_socket_listen(server_socket, 3);
    if (result != CY_RSLT_SUCCESS) return result;
    
    printf("TCP server listening on port %d\n", TCP_PORT);
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Handle Client Communication - Simplified Version
*******************************************************************************/
static void handle_client(void)
{
    char buffer[BUFFER_SIZE];
    uint32_t bytes_received, bytes_sent;
    cy_rslt_t result;
    
    // Receive data
    result = cy_socket_recv(client_socket, buffer, sizeof(buffer) - 1, 
                           CY_SOCKET_FLAGS_NONE, &bytes_received);
    
    if (result == CY_RSLT_SUCCESS && bytes_received > 0) {
        buffer[bytes_received] = '\0';  // Null terminate
        printf("Received: %s\n", buffer);
        
        // Simple echo - send back same message
        result = cy_socket_send(client_socket, buffer, bytes_received,
                               CY_SOCKET_FLAGS_NONE, &bytes_sent);
        
        if (result == CY_RSLT_SUCCESS) {
            printf("Echo sent\n");
        } else {
            printf("Error sending echo\n");
        }
    }
    else if (result != CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT) {
        // Client disconnected or error
        printf("Client disconnected\n");
        cy_socket_disconnect(client_socket, 0);
        cy_socket_delete(client_socket);
        client_connected = false;
    }
}

/*******************************************************************************
* Accept Connection - Simplified Version
*******************************************************************************/
static void accept_connection(void)
{
    cy_socket_sockaddr_t peer_addr;
    uint32_t peer_len = sizeof(peer_addr);
    cy_rslt_t result;
    
    result = cy_socket_accept(server_socket, &peer_addr, &peer_len, &client_socket);
    
    if (result == CY_RSLT_SUCCESS) {
        printf("Client connected\n");
        client_connected = true;
        
        // Simple timeout configuration for receive
        uint32_t timeout = 500;  // 500ms
        cy_socket_setsockopt(client_socket, CY_SOCKET_SOL_SOCKET,
                            CY_SOCKET_SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
}

/*******************************************************************************
* TCP Server Task - Simplified Main Loop
*******************************************************************************/
void tarea_TCPserver(void *arg)
{
    cy_rslt_t result;
    
    printf("== TCP Server ==\n");
    
    // Step 1: Connect WiFi
    result = connect_wifi();
    if (result != CY_RSLT_SUCCESS) {
        handle_error("WiFi connection failed", result);
    }
    
    // Step 2: Initialize sockets
    result = cy_socket_init();
    if (result != CY_RSLT_SUCCESS) {
        handle_error("Socket initialization failed", result);
    }
    
    // Step 3: Create server
    result = create_server_socket();
    if (result != CY_RSLT_SUCCESS) {
        handle_error("Server creation failed", result);
    }
    
    // Simplified main loop - only two states
    while (true) {
        if (!client_connected) {
            // Wait for new connection
            accept_connection();
        } else {
            // Handle current client
            handle_client();
        }
        
        // Small delay to prevent CPU saturation
        cy_rtos_delay_milliseconds(10);
    }
}

/* [] END OF FILE */