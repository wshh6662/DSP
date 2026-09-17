#include "photoelectric_tcp_server.h"

#include "board.h"
#include "driverlib.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "w5500.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t stub_socket_status = SOCK_CLOSED;
static uint8_t stub_socket_interrupt = 0U;
static uint8_t stub_version = 0x04U;
static int32_t stub_send_result = 3;
static uint16_t stub_socket_calls = 0U;
static uint16_t stub_listen_calls = 0U;
static uint16_t stub_close_calls = 0U;
static uint16_t stub_disconnect_calls = 0U;
static uint16_t stub_send_calls = 0U;
static uint16_t stub_send_bytes = 0U;
static uint8_t stub_sent_data[32];
static wiz_NetInfo stub_network;

void test_device_delay_us(uint32_t delay_us)
{
    assert((delay_us == 100000U) || (delay_us == 500000U));
}

void GPIO_writePin(uint32_t pin, uint32_t value)
{
    assert((pin == W5500_CS) || (pin == W5500_RST));
    assert(value <= 1U);
}

void SPI_writeDataBlockingNonFIFO(uint32_t base, uint16_t data)
{
    assert(base == mySPI0_BASE);
    (void)data;
}

uint16_t SPI_readDataBlockingNonFIFO(uint32_t base)
{
    assert(base == mySPI0_BASE);
    return 0x5AU;
}

void reg_wizchip_cs_cbfunc(void (*select_callback)(void),
                           void (*deselect_callback)(void))
{
    assert(select_callback != 0);
    assert(deselect_callback != 0);
}

void reg_wizchip_spi_cbfunc(uint8_t (*read_callback)(void),
                            void (*write_callback)(uint8_t))
{
    assert(read_callback != 0);
    assert(write_callback != 0);
}

void reg_wizchip_spiburst_cbfunc(void (*read_callback)(uint8_t *, uint16_t),
                                 void (*write_callback)(uint8_t *, uint16_t))
{
    assert(read_callback != 0);
    assert(write_callback != 0);
}

int8_t ctlwizchip(uint8_t control, void *argument)
{
    assert(control == CW_INIT_WIZCHIP);
    assert(argument != 0);
    return 0;
}

int8_t ctlnetwork(uint8_t control, void *argument)
{
    assert(control == CN_SET_NETINFO);
    assert(argument != 0);
    memcpy(&stub_network, argument, sizeof(stub_network));
    return 0;
}

uint8_t getVERSIONR(void)
{
    return stub_version;
}

uint8_t getSn_SR(uint8_t socket_number)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    return stub_socket_status;
}

uint8_t getSn_IR(uint8_t socket_number)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    return stub_socket_interrupt;
}

void setSn_IR(uint8_t socket_number, uint8_t value)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    assert(value == Sn_IR_CON);
    stub_socket_interrupt = 0U;
}

void setSn_KPALVTR(uint8_t socket_number, uint8_t value)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    assert(value == 2U);
}

int8_t socket(uint8_t socket_number, uint8_t protocol,
              uint16_t port, uint8_t flags)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    assert(protocol == Sn_MR_TCP);
    assert(port == PHOTOELECTRIC_TCP_PORT);
    assert(flags == SF_IO_NONBLOCK);
    stub_socket_calls++;
    return SOCK_OK;
}

int8_t listen(uint8_t socket_number)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    stub_listen_calls++;
    return SOCK_OK;
}

int8_t disconnect(uint8_t socket_number)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    stub_disconnect_calls++;
    return SOCK_OK;
}

int8_t close(uint8_t socket_number)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    stub_close_calls++;
    return SOCK_OK;
}

int32_t send(uint8_t socket_number, uint8_t *buffer, uint16_t length)
{
    uint16_t index;
    uint16_t bytes_to_send;

    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    assert(buffer != 0);
    assert(length != 0U);
    stub_send_calls++;

    if (stub_send_result <= 0)
    {
        return stub_send_result;
    }

    bytes_to_send = (uint16_t)stub_send_result;
    if (bytes_to_send > length)
    {
        bytes_to_send = length;
    }
    for (index = 0U; index < bytes_to_send; index++)
    {
        stub_sent_data[stub_send_bytes++] = buffer[index];
    }
    return (int32_t)bytes_to_send;
}

int main(void)
{
    uint16_t index;

    assert(photoelectric_tcp_server_init() == 0);
    assert(stub_network.ip[0] == 192U);
    assert(stub_network.ip[1] == 168U);
    assert(stub_network.ip[2] == 1U);
    assert(stub_network.ip[3] == 20U);

    stub_socket_status = SOCK_CLOSED;
    photoelectric_tcp_server_poll(0U);
    assert(stub_socket_calls == 1U);

    stub_socket_status = SOCK_INIT;
    photoelectric_tcp_server_poll(0U);
    assert(stub_listen_calls == 1U);

    stub_socket_status = SOCK_ESTABLISHED;
    stub_socket_interrupt = Sn_IR_CON;
    for (index = 0U; index < 4U; index++)
    {
        photoelectric_tcp_server_poll(1U);
    }
    assert(stub_send_calls == 0U);

    photoelectric_tcp_server_poll(1U);
    assert(stub_send_calls == 1U);
    assert(stub_send_bytes == 3U);
    assert(stub_sent_data[0] == '1');
    assert(stub_sent_data[1] == '\r');
    assert(stub_sent_data[2] == '\n');
    assert(g_photoelectric_tcp_send_count == 1U);

    for (index = 0U; index < 5U; index++)
    {
        photoelectric_tcp_server_poll(0U);
    }
    assert(stub_send_calls == 2U);
    assert(stub_send_bytes == 6U);
    assert(stub_sent_data[3] == '0');
    assert(stub_sent_data[4] == '\r');
    assert(stub_sent_data[5] == '\n');
    assert(g_photoelectric_tcp_send_count == 2U);

    // 分段发送时必须先完成旧报文，不能用新状态覆盖发送缓冲区。
    stub_socket_status = SOCK_CLOSED;
    photoelectric_tcp_server_poll(0U);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = 1;
    for (index = 0U; index < 5U; index++)
    {
        photoelectric_tcp_server_poll(1U);
    }
    assert(g_photoelectric_tcp_send_count == 2U);
    photoelectric_tcp_server_poll(0U);
    photoelectric_tcp_server_poll(0U);
    assert(g_photoelectric_tcp_send_count == 3U);

    stub_socket_status = SOCK_CLOSE_WAIT;
    photoelectric_tcp_server_poll(0U);
    assert(stub_disconnect_calls == 1U);
    assert(stub_close_calls == 0U);

    return 0;
}
