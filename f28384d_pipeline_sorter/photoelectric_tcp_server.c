//#############################################################################
// FILE:   photoelectric_tcp_server.c
// TITLE:  光电检测 W5500 TCP Server
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

#define TCP_KEEPALIVE_5S_UNITS    2U
#define TCP_COMMAND_BUFFER_SIZE   8U

volatile uint8_t g_photoelectric_socket_status = SOCK_CLOSED; // Socket 0 当前状态，23表示已连接。
volatile int32_t g_photoelectric_last_send_result = 0;        // 最近一次 send() 的返回值。
volatile uint32_t g_photoelectric_tcp_poll_count = 0U;        // TCP 服务轮询累计次数。
volatile uint32_t g_photoelectric_tcp_send_count = 0U;        // 完整发送状态帧的累计次数。
volatile int32_t g_photoelectric_network_init_result = 0;     // W5500 初始化结果：0=成功，非0=失败。
volatile uint16_t g_photoelectric_w5500_version = 0U;         // W5500 版本寄存器，正常值为4。
volatile int32_t g_photoelectric_last_receive_result = 0;     // 最近一次 recv() 的返回值。
volatile uint32_t g_photoelectric_reset_command_count = 0U;   // 已识别的 re 复位命令次数。

static uint16_t photoelectric_command_saw_r = 0U;

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

static void photoelectric_w5500_select(void)
{
    GPIO_writePin(W5500_CS, 0U);
}

static void photoelectric_w5500_deselect(void)
{
    GPIO_writePin(W5500_CS, 1U);
}

static uint8_t photoelectric_w5500_spi_transfer(uint8_t data)
{
    SPI_writeDataBlockingNonFIFO(mySPI0_BASE, (uint16_t)data << 8U);
    return (uint8_t)SPI_readDataBlockingNonFIFO(mySPI0_BASE);
}

static uint8_t photoelectric_w5500_spi_read(void)
{
    return photoelectric_w5500_spi_transfer(0xFFU);
}

static void photoelectric_w5500_spi_write(uint8_t data)
{
    (void)photoelectric_w5500_spi_transfer(data);
}

static void photoelectric_w5500_spi_read_burst(uint8_t *buffer,
                                                uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        buffer[index] = photoelectric_w5500_spi_read();
    }
}

static void photoelectric_w5500_spi_write_burst(uint8_t *buffer,
                                                 uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        photoelectric_w5500_spi_write(buffer[index]);
    }
}

int photoelectric_tcp_server_init(void)
{
    uint8_t socket_buffer[2][8] = {
        {16U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
        {16U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
    wiz_NetInfo network = {
        .mac = {0x00U, 0x08U, 0xDCU, 0x11U, 0x22U, 0x33U},
        .ip = {192U, 168U, 1U, 20U},
        .sn = {255U, 255U, 255U, 0U},
        .gw = {192U, 168U, 1U, 1U}
    };

    GPIO_writePin(W5500_RST, 0U);
    DEVICE_DELAY_US(100000U);
    GPIO_writePin(W5500_RST, 1U);
    DEVICE_DELAY_US(500000U);

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

int32_t photoelectric_tcp_server_poll(uint16_t detected,
                                      int32_t encoder_rpm_x100,
                                      int32_t angle_degrees_x100)
{
    static photoelectric_octet_t payload[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];
    static uint16_t payload_length = 0U;
    static uint16_t payload_sent = 0U;
    static uint16_t elapsed_ticks = 0U;
    uint8_t status = getSn_SR(PHOTOELECTRIC_TCP_SOCKET);

    g_photoelectric_tcp_poll_count++;
    g_photoelectric_socket_status = status;

    if (status == SOCK_CLOSED)
    {
        payload_length = 0U;
        payload_sent = 0U;
        elapsed_ticks = 0U;
        photoelectric_command_saw_r = 0U;
        (void)socket(PHOTOELECTRIC_TCP_SOCKET, Sn_MR_TCP,
                     PHOTOELECTRIC_TCP_PORT, SF_IO_NONBLOCK);
        return 0;
    }

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

        if (payload_length == 0U)
        {
            elapsed_ticks++;
            if (photoelectric_tcp_is_send_due(
                    elapsed_ticks,
                    PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS) == 0U)
            {
                return 0;
            }

            elapsed_ticks = 0U;
            payload_length = photoelectric_tcp_build_payload(
                detected,
                encoder_rpm_x100,
                angle_degrees_x100,
                payload,
                PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY);
            payload_sent = 0U;
        }

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

    if (status == SOCK_CLOSE_WAIT)
    {
        (void)disconnect(PHOTOELECTRIC_TCP_SOCKET);
        return 0;
    }

    (void)close(PHOTOELECTRIC_TCP_SOCKET);
    return 0;
}
