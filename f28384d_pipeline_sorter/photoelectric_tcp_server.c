//#############################################################################
// FILE:   photoelectric_tcp_server.c
// TITLE:  光电检测 W5500 TCP Server 和转台遥测
//#############################################################################

#ifdef PHOTOELECTRIC_HOST_TEST
#include <driverlib.h>
#include <device.h>
#include <board.h>
#include <wizchip_conf.h>
#include <w5500.h>
#include <socket.h>
#else
#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#endif
#include "photoelectric_tcp_payload.h"
#include "photoelectric_tcp_server.h"
#include "camera_result.h"

#define TCP_KEEPALIVE_5S_UNITS    2U // W5500 保活间隔：2 个 5 秒单位。
#define TCP_COMMAND_BUFFER_SIZE   8U // 单次读取量，较长数据流分批处理。
#define TCP_STATE_QUEUE_CAPACITY  32U // 保存已观察到的光电变化，网络背压时不覆盖。

typedef struct
{
    int32_t encoder_rpm_x100;
    int32_t angle_degrees_x100;
    uint16_t detected;
} photoelectric_state_snapshot_t;

volatile uint8_t g_photoelectric_socket_status = SOCK_CLOSED; // Socket 0 当前状态，23表示已连接。
volatile int32_t g_photoelectric_last_send_result = 0;        // 最近一次 send() 的返回值。
volatile uint32_t g_photoelectric_tcp_poll_count = 0U;        // TCP 服务轮询累计次数。
volatile uint32_t g_photoelectric_tcp_send_count = 0U;        // 完整发送状态帧的累计次数。
volatile int32_t g_photoelectric_network_init_result = 0;     // W5500 初始化结果：0=成功，非0=失败。
volatile uint16_t g_photoelectric_w5500_version = 0U;         // W5500 版本寄存器，正常值为4。
volatile int32_t g_photoelectric_last_receive_result = 0;     // 最近一次 recv() 的返回值。
volatile uint32_t g_photoelectric_reset_command_count = 0U;   // 已识别的 re 复位命令次数。

static uint16_t photoelectric_command_saw_r = 0U; // 跨 recv() 保存 re 命令的前半个字符。
// 按 TCP 字节流识别大小写均可的 "re"，支持命令被拆成两个数据包。
static uint16_t photoelectric_tcp_receive_reset_command(void)
{
    uint8_t command_buffer[TCP_COMMAND_BUFFER_SIZE];
    uint16_t available;
    uint16_t index;
    uint16_t reset_requested = 0U;
    uint16_t receive_length;

    available = getSn_RX_RSR(PHOTOELECTRIC_TCP_SOCKET);
    while (available != 0U)
    {
        receive_length = (available > TCP_COMMAND_BUFFER_SIZE) ?
                         TCP_COMMAND_BUFFER_SIZE : available;
        g_photoelectric_last_receive_result = recv(
            PHOTOELECTRIC_TCP_SOCKET, command_buffer, receive_length);
        if (g_photoelectric_last_receive_result <= 0)
        {
            break;
        }

        for (index = 0U;
             index < (uint16_t)g_photoelectric_last_receive_result;
             index++)
        {
            uint8_t value = command_buffer[index];

            // 电脑2的 tcmc1,/tcmc2, 与遥测共用 Socket 0/2000。
            camera_result_feed_byte(value);

            if ((photoelectric_command_saw_r != 0U) &&
                ((value == (uint8_t)'e') || (value == (uint8_t)'E')))
            {
                reset_requested = 1U;
                g_photoelectric_reset_command_count++;
                photoelectric_command_saw_r = 0U;
            }
            else if ((value == (uint8_t)'r') || (value == (uint8_t)'R'))
            {
                photoelectric_command_saw_r = 1U;
            }
            else
            {
                photoelectric_command_saw_r = 0U;
            }
        }

        available = getSn_RX_RSR(PHOTOELECTRIC_TCP_SOCKET);
    }

    return reset_requested;
}

// W5500 SPI 片选低有效，由 WIZnet 驱动在访问芯片时调用。
static void photoelectric_w5500_select(void)
{
    GPIO_writePin(W5500_CS, 0U);
}

// 一次芯片访问结束后释放片选。
static void photoelectric_w5500_deselect(void)
{
    GPIO_writePin(W5500_CS, 1U);
}

// C2000 的 8 位 SPI 数据需放在发送寄存器高 8 位；同时读取返回字节。
static uint8_t photoelectric_w5500_spi_transfer(uint8_t data)
{
    SPI_writeDataBlockingNonFIFO(mySPI0_BASE, (uint16_t)data << 8U);
    return (uint8_t)SPI_readDataBlockingNonFIFO(mySPI0_BASE);
}

// 发送填充字节产生 SPI 时钟，取回一个字节。
static uint8_t photoelectric_w5500_spi_read(void)
{
    return photoelectric_w5500_spi_transfer(0xFFU);
}

// SPI 写回调只需要发送数据，接收值在此丢弃。
static void photoelectric_w5500_spi_write(uint8_t data)
{
    (void)photoelectric_w5500_spi_transfer(data);
}

// 将 WIZnet 的连续读请求转换为逐字节 SPI 传输。
static void photoelectric_w5500_spi_read_burst(uint8_t *buffer,
                                                uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        buffer[index] = photoelectric_w5500_spi_read();
    }
}

// 将 WIZnet 的连续写请求转换为逐字节 SPI 传输。
static void photoelectric_w5500_spi_write_burst(uint8_t *buffer,
                                                 uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        photoelectric_w5500_spi_write(buffer[index]);
    }
}

// 硬件复位、注册 SPI 回调、分配 Socket 缓冲区并写入静态网络参数。
int photoelectric_tcp_server_init(void)
{
    // 两行分别配置 TX/RX 容量，单位 KB；当前各 16 KB 全部分给 Socket 0。
    uint8_t socket_buffer[2][8] = {
        {16U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
        {16U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
    wiz_NetInfo network = {
        .mac = {0x00U, 0x08U, 0xDCU, 0x11U, 0x22U, 0x33U},
        .ip = {192U, 168U, 1U, 60U},
        .sn = {255U, 255U, 255U, 0U},
        .gw = {192U, 168U, 1U, 1U}
    };

    // 拉低复位后等待芯片启动，再访问寄存器。
    GPIO_writePin(W5500_RST, 0U);
    DEVICE_DELAY_US(100000U);
    GPIO_writePin(W5500_RST, 1U);
    DEVICE_DELAY_US(500000U);

    // 库通过这些回调操作实际的 GPIO 和 SPI 外设。
    reg_wizchip_cs_cbfunc(photoelectric_w5500_select,
                          photoelectric_w5500_deselect);
    reg_wizchip_spi_cbfunc(photoelectric_w5500_spi_read,
                           photoelectric_w5500_spi_write);
    reg_wizchip_spiburst_cbfunc(photoelectric_w5500_spi_read_burst,
                                photoelectric_w5500_spi_write_burst);

    if (ctlwizchip(CW_INIT_WIZCHIP, socket_buffer) != 0)
    {
        g_photoelectric_network_init_result = -1;
        return -1;
    }

    // 版本寄存器不是 0x04 时，SPI 通信或芯片状态不符合 W5500 预期。
    g_photoelectric_w5500_version = getVERSIONR();
    if (g_photoelectric_w5500_version != 0x04U)
    {
        g_photoelectric_network_init_result = -2;
        return -2;
    }

    g_photoelectric_network_init_result =
        ctlnetwork(CN_SET_NETINFO, &network);
    return g_photoelectric_network_init_result;
}

// 每轮推进连接、命令接收和状态帧发送；返回 1 交给主循环复位编码器。
int32_t photoelectric_tcp_server_poll(int32_t encoder_rpm_x100,
                                      int32_t angle_degrees_x100,
                                      uint16_t detected)
{
    // 缓存同一帧及发送偏移，部分发送或 SOCK_BUSY 时下一轮继续。
    static photoelectric_octet_t payload[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];
    static uint16_t payload_length = 0U;
    static uint16_t payload_sent = 0U;
    static uint16_t elapsed_ticks = 0U;
    static photoelectric_state_snapshot_t state_queue[TCP_STATE_QUEUE_CAPACITY];
    static uint16_t state_head = 0U;
    static uint16_t state_count = 0U;
    static uint16_t state_valid = 0U;
    static uint16_t last_detected = 0U;
    uint8_t status = getSn_SR(PHOTOELECTRIC_TCP_SOCKET);

    g_photoelectric_tcp_poll_count++;
    g_photoelectric_socket_status = status;

    // 初次启动或连接关闭后，清理旧会话并重新打开非阻塞 Socket。
    if (status == SOCK_CLOSED)
    {
        payload_length = 0U;
        payload_sent = 0U;
        elapsed_ticks = 0U;
        state_head = 0U;
        state_count = 0U;
        state_valid = 0U;
        photoelectric_command_saw_r = 0U;
        camera_result_reset_partial_frame();
        (void)socket(PHOTOELECTRIC_TCP_SOCKET, Sn_MR_TCP,
                     PHOTOELECTRIC_TCP_PORT, SF_IO_NONBLOCK);
        return 0;
    }

    // Socket 打开成功后进入监听，等待上位机主动连接。
    if (status == SOCK_INIT)
    {
        if (listen(PHOTOELECTRIC_TCP_SOCKET) < SOCK_OK)
        {
            (void)close(PHOTOELECTRIC_TCP_SOCKET);
        }
        return 0;
    }

    if (status == SOCK_LISTEN)
    {
        return 0;
    }

    if (status == SOCK_ESTABLISHED)
    {
        detected = (detected != 0U) ? 1U : 0U;
        if (state_valid == 0U)
        {
            // 新连接从当前采样建立基准，不发送/重放断线期间的变化。
            last_detected = detected;
            state_valid = 1U;
        }
        else if (detected != last_detected)
        {
            if (state_count >= TCP_STATE_QUEUE_CAPACITY)
            {
                // 不静默漏掉中间边沿；断开后让新会话重新建立基准。
                (void)close(PHOTOELECTRIC_TCP_SOCKET);
                return 0;
            }
            photoelectric_state_snapshot_t *snapshot =
                &state_queue[(state_head + state_count) % TCP_STATE_QUEUE_CAPACITY];
            snapshot->encoder_rpm_x100 = encoder_rpm_x100;
            snapshot->angle_degrees_x100 = angle_degrees_x100;
            snapshot->detected = detected;
            state_count++;
            last_detected = detected;
        }

        if ((getSn_IR(PHOTOELECTRIC_TCP_SOCKET) & Sn_IR_CON) != 0U)
        {
            setSn_IR(PHOTOELECTRIC_TCP_SOCKET, Sn_IR_CON);
            setSn_KPALVTR(PHOTOELECTRIC_TCP_SOCKET,
                          TCP_KEEPALIVE_5S_UNITS);
        }

        if (photoelectric_tcp_receive_reset_command() != 0U)
        {
            // 复位后重新计算一秒发送周期，下一帧从零位状态开始。
            elapsed_ticks = 0U;
            return 1;
        }

        // 仅在上一帧发完且达到轮询间隔时，用当前快照生成新帧。
        if (payload_length == 0U)
        {
            if (elapsed_ticks < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS)
            {
                elapsed_ticks++;
            }
            if ((state_count != 0U) &&
                (elapsed_ticks < PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS))
            {
                // 状态行与遥测共用发送缓冲和偏移，不能在旧行中间插入字节。
                const photoelectric_state_snapshot_t *snapshot =
                    &state_queue[state_head];
                payload_length = photoelectric_tcp_build_payload(
                    snapshot->encoder_rpm_x100,
                    snapshot->angle_degrees_x100,
                    snapshot->detected,
                    payload,
                    PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY);
                payload_sent = 0U;
                state_head = (state_head + 1U) % TCP_STATE_QUEUE_CAPACITY;
                state_count--;
            }
            else
            {
                // 周期到期先发一行遥测，避免持续变化使遥测一直饥饿。
                if (photoelectric_tcp_is_send_due(
                    elapsed_ticks,
                    PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS) == 0U)
                {
                    return 0;
                }

                elapsed_ticks = 0U;
                payload_length = photoelectric_tcp_build_payload(
                    encoder_rpm_x100,
                    angle_degrees_x100,
                    detected,
                    payload,
                    PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY);
                payload_sent = 0U;
                if (payload_length == 0U)
                {
                    return 0;
                }
            }
        }

        // 非阻塞发送可能只接受部分字节；按返回值推进，不重复已发内容。
        g_photoelectric_last_send_result = send(
            PHOTOELECTRIC_TCP_SOCKET,
            (uint8_t *)&payload[payload_sent],
            (uint16_t)(payload_length - payload_sent));

        if (g_photoelectric_last_send_result > 0)
        {
            payload_sent = (uint16_t)(payload_sent +
                                      g_photoelectric_last_send_result);
            if (payload_sent >= payload_length)
            {
                payload_length = 0U;
                payload_sent = 0U;
                g_photoelectric_tcp_send_count++;
                return 0;
            }
        }
        else if (g_photoelectric_last_send_result != SOCK_BUSY)
        {
            (void)close(PHOTOELECTRIC_TCP_SOCKET);
        }
        return 0;
    }

    // 对端已关闭连接，完成本端断开；下一轮再回到监听流程。
    if (status == SOCK_CLOSE_WAIT)
    {
        (void)disconnect(PHOTOELECTRIC_TCP_SOCKET);
        return 0;
    }

    // 其他未处理的状态统一关闭，由 SOCK_CLOSED 分支重新建立服务。
    (void)close(PHOTOELECTRIC_TCP_SOCKET);
    return 0;
}
