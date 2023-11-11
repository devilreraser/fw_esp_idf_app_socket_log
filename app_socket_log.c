/* *****************************************************************************
 * File:   app_socket_log.c
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "app_socket_log.h"

#include <sdkconfig.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "drv_socket.h"

#include "esp_event.h"
#include "esp_log.h"

#if CONFIG_DRV_CONSOLE_USE
#include "drv_console.h"
#endif

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "app_socket_log"


#define APP_SOCKET_LOG_SEND_BUFFER_SIZE  (2 * 1024)
#define APP_SOCKET_LOG_RECV_BUFFER_SIZE  256

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Prototype of functions definitions for Variables
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */


StreamBufferHandle_t log_stream_buffer_send = NULL;
StreamBufferHandle_t log_stream_buffer_recv = NULL;


drv_socket_t socket_log = 
{
    .cName = "log",
    .bServerType = false,
    .nSocketConnectionsCount = 0,
    .nSocketIndexPrimer = {-1},
    .nSocketIndexServer = -1,
    .cHostIP = DRV_SOCKET_DEFAULT_IP,
    .cURL = DRV_SOCKET_DEFAULT_URL,
    .u16Port = 3334,
    .bActiveTask = false,
    .bConnected = false,
    .bSendEnable = false,
    .bSendFillEnable = true,
    .bAutoSendEnable = false,
    .bIndentifyForced = true,
    .bIndentifyNeeded = false,
    .bResetSendStreamOnConnect = false,
    //.bPingUse = true,
    .bPingUse = false,
    .bLineEndingFixCRLFToCR = false,
    .bPermitBroadcast = false,
    .bConnectDeny = false,

    .bPreventOverflowReceivedData = false,
    .pTask = NULL,
    .adapter_interface = {DRV_SOCKET_IF_DEFAULT,  DRV_SOCKET_IF_BACKUP },
    .onConnect = NULL,
    .onReceive = NULL,
    .onSend = NULL,
    .onDisconnect = NULL,
    .onReceiveFrom = NULL,
    .onSendTo = NULL,
    .address_family = AF_INET,
    .protocol = IPPROTO_IP,
    .protocol_type = SOCK_STREAM,
    .pSendStreamBuffer = {&log_stream_buffer_send},
    .pRecvStreamBuffer = {&log_stream_buffer_recv},
};

 SemaphoreHandle_t flag_log_busy = NULL;

 int app_socket_log_send_counter = 0;

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */
int log_vprintf_stdout(const char *fmt, va_list args)
{
    size_t size_string;  
    size_string = vsnprintf(NULL, 0, fmt, args);

    xSemaphoreTake(flag_log_busy, portMAX_DELAY);

    #if CONFIG_DRV_CONSOLE_USE
    #if CONFIG_DRV_CONSOLE_CUSTOM
    #if CONFIG_DRV_CONSOLE_CUSTOM_LOG_DISABLE_FIX
    if (drv_console_get_log_disabled()) return size_string;
    bool needed_finish_line = drv_console_is_needed_finish_line();
    if (needed_finish_line) 
    {
        va_list dummy;
        vprintf("\r\n", dummy);
    }
    #endif
    #endif
    #endif

    vprintf(fmt, args);

    xSemaphoreGive(flag_log_busy);

    return size_string;
}

int log_vprintf(const char *fmt, va_list args)
{
    size_t size_string;  
    size_string = vsnprintf(NULL, 0, fmt, args);

    xSemaphoreTake(flag_log_busy, portMAX_DELAY);

    char *string;
    string = (char *)malloc(size_string+1);
    vsnprintf(string, size_string+1, fmt, args);
    size_string = strlen(string);

    #if CONFIG_DRV_CONSOLE_USE
    #if CONFIG_DRV_CONSOLE_CUSTOM
    #if CONFIG_DRV_CONSOLE_CUSTOM_LOG_DISABLE_FIX
    if (drv_console_get_log_disabled()) return size_string;
    bool needed_finish_line = drv_console_is_needed_finish_line();
    if (needed_finish_line) app_socket_log_send("\r\n", 2);
    #endif
    #endif
    #endif

    size_string = app_socket_log_send(string, size_string);

    free(string);



    #if CONFIG_APP_SOCKET_LOG_REDIRECT_KEEP_STDOUT

    #if CONFIG_DRV_CONSOLE_USE
    #if CONFIG_DRV_CONSOLE_CUSTOM
    #if CONFIG_DRV_CONSOLE_CUSTOM_LOG_DISABLE_FIX
    if (needed_finish_line) 
    {
        va_list dummy;
        vprintf("\r\n", dummy);
    }
    #endif
    #endif
    #endif

    vprintf(fmt, args);

    #endif  /* #if CONFIG_APP_SOCKET_LOG_REDIRECT_KEEP_STDOUT */


    xSemaphoreGive(flag_log_busy);

    return size_string;
}


void app_socket_log_redirect_start(void)
{
    esp_log_set_vprintf(log_vprintf);
}

void app_socket_log_redirect_stdio(void)
{
    if (flag_log_busy == NULL)
    {
        flag_log_busy = xSemaphoreCreateBinary();  
    }
    else
    {
        xSemaphoreTake(flag_log_busy, portMAX_DELAY);
    }
    xSemaphoreGive(flag_log_busy);
    esp_log_set_vprintf(log_vprintf_stdout);
}

void app_socket_log_redirect_stop(void)
{
    esp_log_set_vprintf(&vprintf);
}

void app_socket_log_init(void)
{
    if (flag_log_busy == NULL)
    {
        flag_log_busy = xSemaphoreCreateBinary();  
    }
    else
    {
        xSemaphoreTake(flag_log_busy, portMAX_DELAY);
    }
    xSemaphoreGive(flag_log_busy);
    
    drv_stream_init(socket_log.pSendStreamBuffer[0], APP_SOCKET_LOG_SEND_BUFFER_SIZE, "log_sock_send");
    drv_stream_init(socket_log.pRecvStreamBuffer[0], APP_SOCKET_LOG_RECV_BUFFER_SIZE, "log_sock_recv");
}

void app_socket_log_task(void)
{
    drv_socket_task(&socket_log, -1);
}

int app_socket_log_send(const char* pData, int size)
{
    if ((socket_log.bSendEnable) || (socket_log.bSendFillEnable))
    {
        //ESP_LOGI(TAG, "app_socket_log_send %d bytes", size);    esp log here not permitted
        //ESP_LOG_BUFFER_CHAR(TAG, pData, size);

        #if 0
        app_socket_log_send_counter++;
        if ((app_socket_log_send_counter % 1000) == 0)
        { 
            ESP_LOGI(TAG, "StreamBufferSend %d", app_socket_log_send_counter);
        } 
        #endif

        return drv_stream_push(socket_log.pSendStreamBuffer[0], (uint8_t*)pData, size);
    }
    else
    {
        return 0;
    }
}

int app_socket_log_recv(char* pData, int size)
{
    if (socket_log.bConnected)
    {
        //ESP_LOGI(TAG, "app_socket_log_recv %d bytes", size);
        //ESP_LOG_BUFFER_CHAR(TAG, pData, size);
        
        return drv_stream_pull(socket_log.pRecvStreamBuffer[0], (uint8_t*)pData, size);
    }
    else
    {
        return 0;
    }
}
