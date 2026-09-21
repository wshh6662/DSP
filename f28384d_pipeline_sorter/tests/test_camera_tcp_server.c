//#############################################################################
// FILE:   test_camera_tcp_server.c
// TITLE:  相机 TCP 结果接收主机侧测试
//#############################################################################

#include "camera_tcp_server.h"
#include "socket.h"
#include "w5500.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t stub_socket_status = SOCK_CLOSED;
static uint8_t stub_socket_interrupt = 0U;
static uint8_t stub_received_data[16];
static uint16_t stub_received_length = 0U;
static uint16_t stub_received_offset = 0U;
static uint16_t stub_socket_calls = 0U;
static uint16_t stub_listen_calls = 0U;
static uint16_t stub_disconnect_calls = 0U;
static uint16_t stub_close_calls = 0U;

static void queue_received_text(const char *text)
{
    stub_received_length = (uint16_t)strlen(text);
    assert(stub_received_length <= (uint16_t)sizeof(stub_received_data));
    memcpy(stub_received_data, text, stub_received_length);
    stub_received_offset = 0U;
}

uint8_t getSn_SR(uint8_t socket_number)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    return stub_socket_status;
}

uint8_t getSn_IR(uint8_t socket_number)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    return stub_socket_interrupt;
}

uint16_t getSn_RX_RSR(uint8_t socket_number)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    return (uint16_t)(stub_received_length - stub_received_offset);
}

void setSn_IR(uint8_t socket_number, uint8_t value)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    assert(value == Sn_IR_CON);
    stub_socket_interrupt = 0U;
}

void setSn_KPALVTR(uint8_t socket_number, uint8_t value)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    assert(value == 2U);
}

int8_t socket(uint8_t socket_number, uint8_t protocol,
              uint16_t port, uint8_t flag)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    assert(protocol == Sn_MR_TCP);
    assert(port == CAMERA_TCP_PORT);
    assert(flag == SF_IO_NONBLOCK);
    stub_socket_calls++;
    return (int8_t)CAMERA_TCP_SOCKET;
}

int8_t listen(uint8_t socket_number)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    stub_listen_calls++;
    return SOCK_OK;
}

int8_t disconnect(uint8_t socket_number)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    stub_disconnect_calls++;
    return SOCK_OK;
}

int8_t close(uint8_t socket_number)
{
    assert(socket_number == CAMERA_TCP_SOCKET);
    stub_close_calls++;
    return SOCK_OK;
}

int32_t recv(uint8_t socket_number, uint8_t *buffer, uint16_t length)
{
    uint16_t available;
    uint16_t receive_length;

    assert(socket_number == CAMERA_TCP_SOCKET);
    assert(buffer != 0);

    available = getSn_RX_RSR(socket_number);
    receive_length = (length < available) ? length : available;
    memcpy(buffer, &stub_received_data[stub_received_offset], receive_length);
    stub_received_offset = (uint16_t)(stub_received_offset + receive_length);
    return (int32_t)receive_length;
}

int main(void)
{
    camera_tcp_server_init();
    assert(g_camera_result == CAMERA_RESULT_NONE);
    assert(g_camera_result_count == 0U);

    camera_tcp_server_poll();
    assert(stub_socket_calls == 1U);

    stub_socket_status = SOCK_INIT;
    camera_tcp_server_poll();
    assert(stub_listen_calls == 1U);

    stub_socket_status = SOCK_ESTABLISHED;
    stub_socket_interrupt = Sn_IR_CON;
    queue_received_text("1\r\n");
    camera_tcp_server_poll();
    assert(g_camera_result == CAMERA_RESULT_GOOD);
    assert(g_camera_result_count == 1U);

    queue_received_text("2\r\n");
    camera_tcp_server_poll();
    assert(g_camera_result == CAMERA_RESULT_BAD);
    assert(g_camera_result_count == 2U);

    queue_received_text("X\r\n");
    camera_tcp_server_poll();
    assert(g_camera_result == CAMERA_RESULT_BAD);
    assert(g_camera_invalid_result_count == 1U);

    stub_socket_status = SOCK_CLOSE_WAIT;
    camera_tcp_server_poll();
    assert(stub_disconnect_calls == 1U);

    return 0;
}
