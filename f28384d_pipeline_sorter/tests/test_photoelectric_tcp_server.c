#include "photoelectric_tcp_server.h"
#include "photoelectric_tcp_payload.h"
#include "camera_result.h"

#include "board.h"
#include "driverlib.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "w5500.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

// 遥测仅包含编码器转速和角度。
#define FRAME_MEASURED_LENGTH   36U
#define FRAME_ZERO_LENGTH       34U
#define FRAME_BOTH_ZERO_LENGTH  33U

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
static uint8_t stub_sent_data[50000];
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
    assert(((uint8_t (*)[8])argument)[0][0] == 16U);
    assert(((uint8_t (*)[8])argument)[1][0] == 16U);
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
    // 本电脑遥测使用 2000，且只能是非阻塞模式。
    assert(port == 2000U);
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
        assert(stub_sent_data_length < sizeof(stub_sent_data));
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
    uint16_t sent_length_before;
    photoelectric_octet_t probe[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];

    // W5500 发送缓冲区必须容纳完整遥测帧。
    assert(PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY >= FRAME_MEASURED_LENGTH);

    // 容量不足时必须整体拒绝，正好够时必须完整成帧。
    assert(photoelectric_tcp_build_payload(2929, 35991, 0U,
                                             probe, (FRAME_MEASURED_LENGTH - 1U)) == 0U);
    assert(photoelectric_tcp_build_payload(2929, 35991, 0U, probe,
                                             FRAME_MEASURED_LENGTH)
           == FRAME_MEASURED_LENGTH);

    assert(photoelectric_tcp_server_init() == 0);
    assert(stub_network.ip[0] == 192U);
    assert(stub_network.ip[1] == 168U);
    assert(stub_network.ip[2] == 1U);
    // 当前 DSP 静态地址是 192.168.1.60。
    assert(stub_network.ip[3] == 60U);

    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(stub_socket_calls == 1U);

    stub_socket_status = SOCK_INIT;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(stub_listen_calls == 1U);

    // 编码器 29.29 rpm、当前 359.91 度。
    stub_socket_status = SOCK_ESTABLISHED;
    stub_socket_interrupt = Sn_IR_CON;
    for (index = 0U;
         index < (PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS - 1U);
         index++)
    {
        assert(photoelectric_tcp_server_poll(2929, 35991, 0U) == 0);
    }
    assert(stub_send_calls == 0U);

    assert(photoelectric_tcp_server_poll(2929, 35991, 0U) == 0);
    assert(stub_send_calls == 1U);
    assert_frame(0U, 36U,
                 "enc_rpm=29.29,angle=359.91,photo=0\r\n");
    assert(g_photoelectric_tcp_send_count == 1U);

    // TCP 命令可能分成多个数据包；先收到 r 不复位，随后收到 e 才触发一次。
    queue_received_text("r");
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    queue_received_text("e");
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 1);
    assert(stub_recv_calls == 2U);

    // 同一 2000 端口解析电脑2的完整相机结果；TCP 分片不影响判定。
    queue_received_text("tcm");
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(g_camera_inspection_result_count == 0U);
    queue_received_text("c1,");
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(g_camera_inspection_result == CAMERA_INSPECTION_GOOD);
    queue_received_text("tcmc2,");
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(g_camera_inspection_result == CAMERA_INSPECTION_BAD);
    assert(g_camera_inspection_result_count == 2U);

    // 复位后的角度为 0.00，且重新等待完整的一秒发送周期。
    for (index = 0U;
         index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS;
         index++)
    {
        assert(photoelectric_tcp_server_poll(2929, 0, 0U) == 0);
    }
    assert(stub_send_calls == 2U);
    assert_frame(FRAME_MEASURED_LENGTH, FRAME_ZERO_LENGTH,
                 "enc_rpm=29.29,angle=0.00,photo=0\r\n");
    assert(g_photoelectric_tcp_send_count == 2U);

    // 分段发送：先重新建立连接，再把 send() 限成每次 1 个字节。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = 1;
    for (index = 0U;
         index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS;
         index++)
    {
        assert(photoelectric_tcp_server_poll(2929, 27000, 0U) == 0);
    }
    assert(stub_send_calls == 3U);
    assert(g_photoelectric_tcp_send_count == 2U);

    // 旧报文没发完之前，新状态不能覆盖发送缓冲区：这一段必须还是刚才那一帧。
    // 已发出 1 个字节，最后一个字节发送前不能发布整帧完成。
    for (index = 0U; index < (FRAME_MEASURED_LENGTH - 2U); index++)
    {
        assert(photoelectric_tcp_server_poll(1, 90, 0U) == 0);
    }
    assert(g_photoelectric_tcp_send_count == 2U);
    assert(photoelectric_tcp_server_poll(1, 9000, 0U) == 0);
    assert(g_photoelectric_tcp_send_count == 3U);
    assert(stub_send_calls == (2U + FRAME_MEASURED_LENGTH));
    assert_frame((uint16_t)(FRAME_MEASURED_LENGTH + FRAME_ZERO_LENGTH),
                 FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,angle=270.00,photo=0\r\n");

    // SOCK_BUSY：发送缓冲区满，既不能丢报文也不能断开连接。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = SOCK_BUSY;
    for (index = 0U;
         index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS;
         index++)
    {
        assert(photoelectric_tcp_server_poll(2929, 35991, 0U) == 0);
    }
    assert(stub_send_calls == (3U + FRAME_MEASURED_LENGTH));
    assert(g_photoelectric_last_send_result == SOCK_BUSY);
    assert(g_photoelectric_tcp_send_count == 3U);
    assert(stub_close_calls == 0U);
    assert(stub_disconnect_calls == 0U);

    // 缓冲区恢复后先把这一帧发完，不会丢帧。
    stub_send_result = FRAME_MEASURED_LENGTH;
    assert(photoelectric_tcp_server_poll(2929, 35991, 0U) == 0);
    assert(stub_send_calls == (4U + FRAME_MEASURED_LENGTH));
    assert(g_photoelectric_tcp_send_count == 4U);
    assert_frame((uint16_t)(FRAME_MEASURED_LENGTH + FRAME_ZERO_LENGTH +
                            FRAME_MEASURED_LENGTH),
                 FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,angle=359.91,photo=0\r\n");

    // 真正的发送错误才断开，交给下一轮重连。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = -1;
    for (index = 0U;
         index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS;
         index++)
    {
        assert(photoelectric_tcp_server_poll(2929, 35991, 0U) == 0);
    }
    assert(g_photoelectric_last_send_result == -1);
    assert(stub_close_calls == 1U);

    stub_socket_status = SOCK_CLOSE_WAIT;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(stub_disconnect_calls == 1U);
    assert(stub_close_calls == 1U);

    // 电脑2连接后不再发送早期联通测试用的 hi。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_socket_interrupt = Sn_IR_CON;
    stub_send_result = FRAME_MEASURED_LENGTH;
    sent_length_before = stub_sent_data_length;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert(stub_sent_data_length == sent_length_before);

    // 精确核对周期报文包含当前光电状态。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_send_result = PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY;
    sent_length_before = stub_sent_data_length;
    for (index = 0U; index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS; index++)
    {
        assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    }
    assert_frame(sent_length_before, 35U,
                 "enc_rpm=16.00,angle=90.00,photo=0\r\n");

    // 状态变化立即发送完整状态行，稳定状态不重复发送。
    stub_sent_data_length = 0U;
    assert(photoelectric_tcp_server_poll(1600, 9000, 1U) == 0);
    assert_frame(0U, 35U, "enc_rpm=16.00,angle=90.00,photo=1\r\n");
    sent_length_before = stub_send_calls;
    assert(photoelectric_tcp_server_poll(1600, 9000, 1U) == 0);
    assert(stub_send_calls == sent_length_before);
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    assert_frame(35U, 35U, "enc_rpm=16.00,angle=90.00,photo=0\r\n");

    // 遥测正在部分发送时，状态按顺序排队，不能插入遥测行内部。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_sent_data_length = 0U;
    stub_send_result = 1;
    for (index = 0U; index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS; index++)
    {
        assert(photoelectric_tcp_server_poll(2929, 35991, 0U) == 0);
    }
    assert(stub_sent_data_length == 1U);
    stub_send_result = SOCK_BUSY;
    assert(photoelectric_tcp_server_poll(2929, 35991, 1U) == 0);
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    assert(stub_sent_data_length == 1U);
    stub_send_result = PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY;
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    assert_frame(0U, FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,angle=359.91,photo=0\r\n");
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    assert_frame(FRAME_MEASURED_LENGTH, FRAME_MEASURED_LENGTH,
                 "enc_rpm=29.29,angle=359.91,photo=1\r\n");
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    assert_frame(FRAME_MEASURED_LENGTH * 2U, 35U,
                 "enc_rpm=16.00,angle=90.00,photo=0\r\n");

    // 状态行自身也支持 busy/部分发送；后一条事件不能覆盖前一条。
    stub_sent_data_length = 0U;
    stub_send_result = 1;
    assert(photoelectric_tcp_server_poll(0, 0, 1U) == 0);
    assert(stub_sent_data_length == 1U);
    stub_send_result = SOCK_BUSY;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_send_result = PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert_frame(0U, FRAME_BOTH_ZERO_LENGTH,
                 "enc_rpm=0.00,angle=0.00,photo=1\r\n");
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert_frame(FRAME_BOTH_ZERO_LENGTH, FRAME_BOTH_ZERO_LENGTH,
                 "enc_rpm=0.00,angle=0.00,photo=0\r\n");

    // 重连丢弃旧连接的未完成行/队列，首次采样仅建立新基准。
    stub_send_result = SOCK_BUSY;
    assert(photoelectric_tcp_server_poll(0, 0, 1U) == 0);
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 1U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_sent_data_length = 0U;
    stub_send_result = PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY;
    assert(photoelectric_tcp_server_poll(0, 0, 1U) == 0);
    assert(stub_sent_data_length == 0U);
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    assert_frame(0U, FRAME_BOTH_ZERO_LENGTH,
                 "enc_rpm=0.00,angle=0.00,photo=0\r\n");

    // 连续变化也不能饿死周期遥测，同时覆盖事件队列头的多次回绕。
    stub_socket_status = SOCK_CLOSED;
    assert(photoelectric_tcp_server_poll(0, 0, 0U) == 0);
    stub_socket_status = SOCK_ESTABLISHED;
    stub_sent_data_length = 0U;
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);
    for (index = 1U; index < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS; index++)
    {
        assert(photoelectric_tcp_server_poll(1600, 9000, index % 2U) == 0);
    }
    assert_frame((PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS - 2U) * 35U,
                 35U, "enc_rpm=16.00,angle=90.00,photo=1\r\n");
    assert(photoelectric_tcp_server_poll(1600, 9000, 1U) == 0);
    assert_frame((PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS - 2U) * 35U + 35U,
                 35U, "enc_rpm=16.00,angle=90.00,photo=1\r\n");
    assert(photoelectric_tcp_server_poll(1600, 9000, 0U) == 0);

    // 持续背压使事件队列满时断开，不能静默覆盖旧事件。
    stub_send_result = SOCK_BUSY;
    sent_length_before = stub_close_calls;
    for (index = 0U; index < 34U; index++)
    {
        assert(photoelectric_tcp_server_poll(0, 0, (index + 1U) % 2U) == 0);
    }
    assert(stub_close_calls > sent_length_before);

    return 0;
}
