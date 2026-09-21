//#############################################################################
// FILE:   camera_tcp_server.c
// TITLE:  相机检测结果 W5500 TCP Server
//#############################################################################
//
// Socket 0 / 2000 继续给电脑发送转速、角度和光电状态；
// 本模块只使用 Socket 1 / 2001 接收相机主动发来的 ASCII 结果：
//   '1' = 良品
//   '2' = 不良品
// 当前阶段每收到一个有效字符就更新一次最近结果。相机品牌、触发命令、
// 瓶子编号关联和完整帧协议确定后，再在本模块中扩展。
//#############################################################################

#ifdef CAMERA_TCP_HOST_TEST
#include "camera_tcp_server.h"
#include <socket.h>
#include <w5500.h>
#else
#include "driverlib.h"
#include "camera_tcp_server.h"
#include "socket.h"
#include "w5500.h"
#endif

#define CAMERA_TCP_KEEPALIVE_5S_UNITS    2U
#define CAMERA_TCP_RECEIVE_BUFFER_SIZE   16U

// ---- CCS Watch 变量 ----
volatile uint8_t  g_camera_socket_status = SOCK_CLOSED; // Socket 1 当前状态，23表示已连接。
volatile uint16_t g_camera_result = CAMERA_RESULT_NONE; // 最近一次有效相机结果：0=无、1=良品、2=不良品。
volatile uint32_t g_camera_result_count = 0U;           // 收到有效相机结果的累计次数。
volatile uint32_t g_camera_invalid_result_count = 0U;   // 收到非空白且不是1/2的字符次数。
volatile int32_t  g_camera_last_receive_result = 0;     // 最近一次 recv() 的返回值。

static uint16_t camera_tcp_is_whitespace(uint8_t value)
{
    return ((value == (uint8_t)'\r') ||
            (value == (uint8_t)'\n') ||
            (value == (uint8_t)' ') ||
            (value == (uint8_t)'\t')) ? 1U : 0U;
}

static void camera_tcp_handle_received_byte(uint8_t value)
{
    if (value == (uint8_t)'1')
    {
        g_camera_result = CAMERA_RESULT_GOOD;
        g_camera_result_count++;
    }
    else if (value == (uint8_t)'2')
    {
        g_camera_result = CAMERA_RESULT_BAD;
        g_camera_result_count++;
    }
    else if (camera_tcp_is_whitespace(value) == 0U)
    {
        g_camera_invalid_result_count++;
    }
}

void camera_tcp_server_init(void)
{
    g_camera_socket_status = SOCK_CLOSED;
    g_camera_result = CAMERA_RESULT_NONE;
    g_camera_result_count = 0U;
    g_camera_invalid_result_count = 0U;
    g_camera_last_receive_result = 0;
}

void camera_tcp_server_poll(void)
{
    uint8_t receive_buffer[CAMERA_TCP_RECEIVE_BUFFER_SIZE];
    uint8_t status;
    uint16_t available;
    uint16_t receive_length;
    uint16_t index;

    status = getSn_SR(CAMERA_TCP_SOCKET);
    g_camera_socket_status = status;

    if (status == SOCK_CLOSED)
    {
        (void)socket(CAMERA_TCP_SOCKET, Sn_MR_TCP,
                     CAMERA_TCP_PORT, SF_IO_NONBLOCK);
        return;
    }

    if (status == SOCK_INIT)
    {
        if (listen(CAMERA_TCP_SOCKET) < SOCK_OK)
        {
            (void)close(CAMERA_TCP_SOCKET);
        }
        return;
    }

    if (status == SOCK_LISTEN)
    {
        return;
    }

    if (status == SOCK_ESTABLISHED)
    {
        if ((getSn_IR(CAMERA_TCP_SOCKET) & Sn_IR_CON) != 0U)
        {
            setSn_IR(CAMERA_TCP_SOCKET, Sn_IR_CON);
            setSn_KPALVTR(CAMERA_TCP_SOCKET,
                          CAMERA_TCP_KEEPALIVE_5S_UNITS);
        }

        available = getSn_RX_RSR(CAMERA_TCP_SOCKET);
        while (available != 0U)
        {
            receive_length = (available > CAMERA_TCP_RECEIVE_BUFFER_SIZE) ?
                             CAMERA_TCP_RECEIVE_BUFFER_SIZE : available;
            g_camera_last_receive_result = recv(CAMERA_TCP_SOCKET,
                                                receive_buffer,
                                                receive_length);
            if (g_camera_last_receive_result <= 0)
            {
                return;
            }

            for (index = 0U;
                 index < (uint16_t)g_camera_last_receive_result;
                 index++)
            {
                camera_tcp_handle_received_byte(receive_buffer[index]);
            }

            available = getSn_RX_RSR(CAMERA_TCP_SOCKET);
        }
        return;
    }

    if (status == SOCK_CLOSE_WAIT)
    {
        (void)disconnect(CAMERA_TCP_SOCKET);
        return;
    }

    (void)close(CAMERA_TCP_SOCKET);
}
