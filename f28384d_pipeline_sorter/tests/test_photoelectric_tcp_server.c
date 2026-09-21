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

// 一行报文的长度：编码器转速为两位整数时 34 字节，角度回到 0.00 时 32 字节。
#define FRAME_MEASURED_LENGTH   34U
#define FRAME_ZERO_LENGTH       32U

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
static uint8_t stub_sent_data[256];
static uint8_t stub_received_data[16];
static uint16_t stub_received_data_length = 0U;
static uint16_t stub_received_data_offset = 0U;
static uint16_t stub_recv_calls = 0U;
static wiz_NetInfo stub_network;

static void queue_received_text(const char *text)
{
    stub_received_data_length = (uint16_t)strlen(text);
    assert(stub_received_data_length <= (uint16_t)sizeof(stub_received_data));
    memcpy(stub_received_data, text, stub_received_data_length);
    stub_received_data_offset = 0U;
}

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

uint16_t getSn_RX_RSR(uint8_t socket_number)
{
    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    return (uint16_t)(stub_received_data_length - stub_received_data_offset);
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

int32_t recv(uint8_t socket_number, uint8_t *buffer, uint16_t length)
{
    uint16_t available;
    uint16_t receive_length;

    assert(socket_number == PHOTOELECTRIC_TCP_SOCKET);
    assert(buffer != 0);
    stub_recv_calls++;

    available = getSn_RX_RSR(socket_number);
    receive_length = (length < available) ? length : available;
    memcpy(buffer, &stub_received_data[stub_received_data_offset],
           receive_length);
    stub_received_data_offset = (uint16_t)(stub_received_data_offset +
                                           receive_length);
    return (int32_t)receive_length;
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
    photoelectric_octet_t probe[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];

    // W5500 发送缓冲区要覆盖编码器转速取 INT32_MIN 的最坏情况 41 个 octet。
    assert(PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY >= 41U);

    // 容量不足时必须整体拒绝，正好够时必须完整成帧。
    assert(photoelectric_tcp_build_payload(1U, 2929, 35991, probe, 33U) == 0U);
    assert(photoelectric_tcp_build_payload(1U, 2929, 35991, probe,
                                           FRAME_MEASURED_LENGTH)
           == FRAME_MEASURED_LENGTH);

    assert(photoelectric_tcp_server_init() == 0);
    assert(stub_network.ip[0] == 192U);
    assert(stub_network.ip[1] == 168U);
    assert(stub_network.ip[2] == 1U);
    // DSP 地址仍然是 192.168.1.20。
    assert(stub_network.ip[3] == 20U);

    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    assert(stub_socket_calls == 1U);

    stub_socket_status = SOCK_INIT;
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    assert(stub_listen_calls == 1U);

    // 编码器 29.29 rpm、检测到物品、当前 359.91 度。
    stub_socket_status = SOCK_ESTABLISHED;
    stub_socket_interrupt = Sn_IR_CON;
    for (index = 0U; index < 99U; index++)
    {
        assert(photoelectric_tcp_server_poll(1U, 2929, 35991) == 0);
    }
    assert(stub_send_calls == 0U);

    assert(photoelectric_tcp_server_poll(1U, 2929, 35991) == 0);
    assert(stub_send_calls == 1U);
    assert_frame(0U, FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,obj=1,angle=359.91\r\n");
    assert(g_photoelectric_tcp_send_count == 1U);

    // TCP 命令可能分成多个数据包；先收到 r 不复位，随后收到 e 才触发一次。
    queue_received_text("r");
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    queue_received_text("e");
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 1);
    assert(stub_recv_calls == 2U);

    // 复位后的角度为 0.00，且重新等待完整的一秒发送周期。
    for (index = 0U; index < 100U; index++)
    {
        assert(photoelectric_tcp_server_poll(0U, 2929, 0) == 0);
    }
    assert(stub_send_calls == 2U);
    assert_frame(FRAME_MEASURED_LENGTH, FRAME_ZERO_LENGTH,
                 "enc_rpm=29.29,obj=0,angle=0.00\r\n");
    assert(g_photoelectric_tcp_send_count == 2U);

    // 分段发送：先重新建立连接，再把 send() 限成每次 1 个字节。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = 1;
    for (index = 0U; index < 100U; index++)
    {
        assert(photoelectric_tcp_server_poll(1U, 2929, 27000) == 0);
    }
    assert(stub_send_calls == 3U);
    assert(g_photoelectric_tcp_send_count == 2U);

    // 旧报文没发完之前，新状态不能覆盖发送缓冲区：这一段必须还是刚才那一帧。
    // 一共 34 个字节，已经发出 1 个，剩下 33 次轮询发完。
    for (index = 0U; index < 32U; index++)
    {
        assert(photoelectric_tcp_server_poll(0U, 1, 90) == 0);
    }
    assert(g_photoelectric_tcp_send_count == 2U);
    assert(photoelectric_tcp_server_poll(0U, 1, 9000) == 0);
    assert(g_photoelectric_tcp_send_count == 3U);
    assert(stub_send_calls == 36U);
    assert_frame((uint16_t)(FRAME_MEASURED_LENGTH + FRAME_ZERO_LENGTH),
                 FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,obj=1,angle=270.00\r\n");

    // SOCK_BUSY：发送缓冲区满，既不能丢报文也不能断开连接。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = SOCK_BUSY;
    for (index = 0U; index < 100U; index++)
    {
        assert(photoelectric_tcp_server_poll(1U, 2929, 35991) == 0);
    }
    assert(stub_send_calls == 37U);
    assert(g_photoelectric_last_send_result == SOCK_BUSY);
    assert(g_photoelectric_tcp_send_count == 3U);
    assert(stub_close_calls == 0U);
    assert(stub_disconnect_calls == 0U);

    // 缓冲区恢复后先把这一帧发完，不会丢帧。
    stub_send_result = FRAME_MEASURED_LENGTH;
    assert(photoelectric_tcp_server_poll(1U, 2929, 35991) == 0);
    assert(stub_send_calls == 38U);
    assert(g_photoelectric_tcp_send_count == 4U);
    assert_frame((uint16_t)(FRAME_MEASURED_LENGTH + FRAME_ZERO_LENGTH +
                            FRAME_MEASURED_LENGTH),
                 FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,obj=1,angle=359.91\r\n");

    // 真正的发送错误才断开，交给下一轮重连。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = -1;
    for (index = 0U; index < 100U; index++)
    {
        assert(photoelectric_tcp_server_poll(1U, 2929, 35991) == 0);
    }
    assert(g_photoelectric_last_send_result == -1);
    assert(stub_close_calls == 1U);

    stub_socket_status = SOCK_CLOSE_WAIT;
    assert(photoelectric_tcp_server_poll(0U, 0, 0) == 0);
    assert(stub_disconnect_calls == 1U);
    assert(stub_close_calls == 1U);

    return 0;
}
