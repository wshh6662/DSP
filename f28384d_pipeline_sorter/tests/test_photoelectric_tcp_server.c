#include "photoelectric_tcp_server.h"
#include "photoelectric_tcp_payload.h"

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
// 默认一次把整帧发完；分段发送的场景在后面单独改小。
static int32_t stub_send_result = PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY;
static uint16_t stub_socket_calls = 0U;
static uint16_t stub_listen_calls = 0U;
static uint16_t stub_close_calls = 0U;
static uint16_t stub_disconnect_calls = 0U;
static uint16_t stub_send_calls = 0U;
static uint16_t stub_sent_data_length = 0U;
static uint8_t stub_sent_data[96];
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
    // 端口必须是 2000，且只能是非阻塞模式。
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
        stub_sent_data[stub_sent_data_length] = buffer[index];
        stub_sent_data_length++;
    }
    return (int32_t)bytes_to_send;
}

// 校验从 offset 开始、长度 length 的一行报文与期望完全一致。
static void assert_frame(uint16_t offset, uint16_t length, const char *expected)
{
    uint16_t index;

    assert(stub_sent_data_length == (uint16_t)(offset + length));
    for (index = 0U; index < length; index++)
    {
        assert(stub_sent_data[offset + index] == (uint8_t)expected[index]);
    }
}

int main(void)
{
    uint16_t index;
    photoelectric_octet_t longest[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];

    // W5500 发送缓冲区至少要能放下整行报文。
    assert(PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY >= 48U);

    // 最坏情况：INT32_MIN 写成 "-21474836.48"，整行也只有 28 个 octet。
    assert(photoelectric_tcp_build_payload(1U,
                                           (-2147483647 - 1),
                                           longest,
                                           PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY)
           == 28U);

    // 容量不足时必须整体拒绝，不能发出半截报文。
    // 10 正好卡在 rpm 最后一位小数上，20 卡在结尾 CR 上，必须都能挡住。
    assert(photoelectric_tcp_build_payload(1U, 2929, longest, 10U) == 0U);
    assert(photoelectric_tcp_build_payload(1U, 2929, longest, 20U) == 0U);
    assert(photoelectric_tcp_build_payload(1U, 2929, longest, 21U) == 21U);

    // 容量之外一个字节都不能写。
    {
        uint8_t guarded[16];

        memset(guarded, 0xAA, sizeof(guarded));
        assert(photoelectric_tcp_build_payload(1U, 2929, guarded, 10U) == 0U);
        for (index = 10U; index < (uint16_t)sizeof(guarded); index++)
        {
            assert(guarded[index] == 0xAAU);
        }
    }

    assert(photoelectric_tcp_server_init() == 0);
    assert(stub_network.ip[0] == 192U);
    assert(stub_network.ip[1] == 168U);
    assert(stub_network.ip[2] == 1U);
    // DSP 地址仍然是 192.168.1.20。
    assert(stub_network.ip[3] == 20U);

    stub_socket_status = SOCK_CLOSED;
    photoelectric_tcp_server_poll(0U, 0);
    assert(stub_socket_calls == 1U);

    stub_socket_status = SOCK_INIT;
    photoelectric_tcp_server_poll(0U, 0);
    assert(stub_listen_calls == 1U);

    // 正转 29.29 rpm 且检测到物品：报文为 speed=29.29,photo=1。
    stub_socket_status = SOCK_ESTABLISHED;
    stub_socket_interrupt = Sn_IR_CON;
    for (index = 0U; index < 4U; index++)
    {
        photoelectric_tcp_server_poll(1U, 2929);
    }
    assert(stub_send_calls == 0U);

    photoelectric_tcp_server_poll(1U, 2929);
    assert(stub_send_calls == 1U);
    assert_frame(0U, 21U, "speed=29.29,photo=1\r\n");
    assert(g_photoelectric_tcp_send_count == 1U);

    // 反转 12.50 rpm 且无物品：报文为 speed=-12.50,photo=0。
    for (index = 0U; index < 5U; index++)
    {
        photoelectric_tcp_server_poll(0U, -1250);
    }
    assert(stub_send_calls == 2U);
    assert_frame(21U, 22U, "speed=-12.50,photo=0\r\n");
    assert(g_photoelectric_tcp_send_count == 2U);

    // 分段发送：先重新建立连接，再把 send() 限成每次 1 个字节。
    stub_socket_status = SOCK_CLOSED;
    photoelectric_tcp_server_poll(0U, 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = 1;
    for (index = 0U; index < 5U; index++)
    {
        photoelectric_tcp_server_poll(1U, 2929);
    }
    assert(stub_send_calls == 3U);
    assert(g_photoelectric_tcp_send_count == 2U);

    // 旧报文没发完之前，新状态不能覆盖发送缓冲区：这一段必须还是 speed=29.29,photo=1。
    // 一共 21 个字节，已经发出 1 个，剩下 20 次轮询发完。
    for (index = 0U; index < 19U; index++)
    {
        photoelectric_tcp_server_poll(0U, -1250);
    }
    assert(g_photoelectric_tcp_send_count == 2U);
    photoelectric_tcp_server_poll(0U, -1250);
    assert(g_photoelectric_tcp_send_count == 3U);
    assert_frame(43U, 21U, "speed=29.29,photo=1\r\n");

    // SOCK_BUSY：发送缓冲区满，既不能丢报文也不能断开连接。
    stub_socket_status = SOCK_CLOSED;
    photoelectric_tcp_server_poll(0U, 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = SOCK_BUSY;
    for (index = 0U; index < 5U; index++)
    {
        photoelectric_tcp_server_poll(1U, 2929);
    }
    assert(stub_send_calls == 24U);
    assert(g_photoelectric_last_send_result == SOCK_BUSY);
    assert(g_photoelectric_tcp_send_count == 3U);
    assert(stub_close_calls == 0U);
    assert(stub_disconnect_calls == 0U);

    // 缓冲区恢复后先把这一帧发完，不会丢帧。
    stub_send_result = 21;
    photoelectric_tcp_server_poll(1U, 2929);
    assert(g_photoelectric_tcp_send_count == 4U);
    assert_frame(64U, 21U, "speed=29.29,photo=1\r\n");

    // 真正的发送错误才断开，交给下一轮重连。
    stub_socket_status = SOCK_CLOSED;
    photoelectric_tcp_server_poll(0U, 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = -1;
    for (index = 0U; index < 5U; index++)
    {
        photoelectric_tcp_server_poll(1U, 2929);
    }
    assert(g_photoelectric_last_send_result == -1);
    assert(stub_close_calls == 1U);

    stub_socket_status = SOCK_CLOSE_WAIT;
    photoelectric_tcp_server_poll(0U, 0);
    assert(stub_disconnect_calls == 1U);
    assert(stub_close_calls == 1U);

    return 0;
}
