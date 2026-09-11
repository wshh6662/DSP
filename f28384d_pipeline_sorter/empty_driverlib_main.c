//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D W5500 Modbus TCP HMI test
//
// DSP/W5500: 192.168.1.20:502 (Modbus TCP Server, 只使用 W5500 socket 0)
// HMI:       Modbus TCP Client，EasyBuilder 设备 DSP_W5500，站号 1
// 支持 FC01/03/05/06/0F/10，映射到通用 2048 保持寄存器 + 2048 线圈；
// 检测数量按 32 位发布在 4x-5 / 4x-6（低字在前）。
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "modbus_tcp_server.h"

#define W5500_SOCKET                       0U
#define MODBUS_TCP_SERVER_PORT             502U
#define MODBUS_DETECTION_COUNT_VALUE       1234U
#define MAIN_LOOP_PERIOD_US                10000U
#define NETWORK_RETRY_TICKS                100U
#define TCP_KEEPALIVE_5S_UNITS             2U

// 这些变量可加入 CCS Watch，用于确认 HMI 是否连接、请求和响应是否成功。
volatile uint32_t g_detection_count = MODBUS_DETECTION_COUNT_VALUE;
volatile uint8_t g_modbus_socket_status = SOCK_CLOSED;
volatile int32_t g_modbus_last_receive_result = 0;
volatile int32_t g_modbus_last_send_result = 0;
volatile uint32_t g_modbus_request_count = 0U;
volatile uint16_t g_modbus_last_frame_length = 0U;
volatile uint16_t g_modbus_last_mbap_length = 0U;
volatile uint16_t g_modbus_last_request_address = 0U;
volatile uint16_t g_modbus_last_request_quantity = 0U;
volatile uint16_t g_modbus_last_response_length = 0U;
volatile int32_t g_w5500_network_init_result = 0;
volatile uint16_t g_w5500_version = 0U;
volatile uint16_t g_w5500_phy_link = 0U;
volatile int32_t g_network_ready_state = 0;
volatile uint32_t g_modbus_poll_count = 0U;
volatile uint16_t g_modbus_rx_available = 0U;
volatile uint16_t g_modbus_rx_read_pointer = 0U;
volatile uint16_t g_modbus_rx_write_pointer = 0U;
volatile uint16_t g_modbus_socket_interrupt = 0U;
// 功能码与写操作诊断：在收到完整帧后由 ModbusTcp_ProcessRequest 回填。
// 收到 FC16 时 g_modbus_last_function 应为 0x10、write_quantity 应为 1。
volatile uint8_t g_modbus_last_function = 0U;
volatile uint8_t g_modbus_last_exception_code = 0U;
volatile uint16_t g_modbus_last_write_address = 0U;
volatile uint16_t g_modbus_last_write_quantity = 0U;

// WIZnet 库不知道本板 CS 接在哪个 GPIO，因此注册这两个函数给它调用。
// 每次读写 W5500 寄存器前都必须先选中芯片。
// 作用：拉低 W5500 片选信号，开始一次 SPI 事务。
// 用法：由 WIZnet 驱动在每次寄存器/数据访问前自动调用。
static void W5500_Select(void)
{
    GPIO_writePin(W5500_CS, 0U);
}

// 一次 W5500 寄存器访问完成后取消选中，避免后面的 SPI 数据误写进 W5500。
// 作用：拉高 W5500 片选信号，结束一次 SPI 事务。
// 用法：由 WIZnet 驱动在每次寄存器/数据访问后自动调用。
static void W5500_Deselect(void)
{
    GPIO_writePin(W5500_CS, 1U);
}

// SPI 每发出 1 个字节就会同时收到 1 个字节。
// mySPI0_BASE 是 SysConfig 生成的 SPIA 名称，不能再手写 SPI 参数。
// 作用：通过 SPIA 全双工收发一个 W5500 字节。
// 用法：读写单字节回调的底层函数；返回 SPI 同时收到的字节。
static uint8_t W5500_SpiTransferByte(uint8_t data)
{
    SPI_writeDataBlockingNonFIFO(mySPI0_BASE, (uint16_t)data << 8U);
    return (uint8_t)SPI_readDataBlockingNonFIFO(mySPI0_BASE);
}

// 读 SPI 时也必须提供时钟，所以发 0xFF 只是为了产生 8 个时钟脉冲。
// 作用：从 W5500 读取一个字节。
// 用法：作为 WIZnet 单字节读取回调，发送 0xFF 仅用于产生 SPI 时钟。
static uint8_t W5500_SpiReadByte(void)
{
    return W5500_SpiTransferByte(0xFFU);
}

// 写操作不关心 SPI 同时收到的字节，因此直接丢弃返回值。
// 作用：向 W5500 写入一个字节。
// 用法：作为 WIZnet 单字节写入回调，忽略 SPI 同时收到的数据。
static void W5500_SpiWriteByte(uint8_t data)
{
    (void)W5500_SpiTransferByte(data);
}

// WIZnet 库读寄存器块或收网络数据时会调用这个连续读函数。
// 作用：连续读取一段 W5500 SPI 数据。
// 用法：由 WIZnet 驱动读取寄存器块或 socket 接收数据时调用。
static void W5500_SpiReadBurst(uint8_t *buffer, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        buffer[index] = W5500_SpiReadByte();
    }
}

// WIZnet 库写寄存器块或把 TCP 数据放进发送缓冲区时会调用这里。
// 作用：连续写入一段 W5500 SPI 数据。
// 用法：由 WIZnet 驱动写寄存器块或填充 socket 发送缓冲区时调用。
static void W5500_SpiWriteBurst(uint8_t *buffer, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        W5500_SpiWriteByte(buffer[index]);
    }
}

// 复位 W5500，设置网卡 IP 地址，并检查芯片有没有正常回应。
// SPIA、CS、RST 的引脚方向和 SPI 参数已经由 Board_init() 的 SysConfig 代码完成。
// 作用：复位并初始化 W5500，注册 SPI 回调，设置 DSP IP 为 192.168.1.20。
// 用法：main() 启动时调用；返回 0 表示成功，非 0 表示版本或初始化失败。
static int W5500_NetworkInit(void)
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

    // W5500 上电后先保持复位低电平 100 ms，再释放并等待芯片内部启动。
    GPIO_writePin(W5500_RST, 0U);
    DEVICE_DELAY_US(100000U);
    GPIO_writePin(W5500_RST, 1U);
    DEVICE_DELAY_US(500000U);

    // 把本板的 GPIO/SPI 操作方法交给通用 WIZnet 协议库。
    // 后续 socket()、connect()、send() 才能真正操作到 W5500。
    reg_wizchip_cs_cbfunc(W5500_Select, W5500_Deselect);
    reg_wizchip_spi_cbfunc(W5500_SpiReadByte, W5500_SpiWriteByte);
    reg_wizchip_spiburst_cbfunc(W5500_SpiReadBurst, W5500_SpiWriteBurst);

    // W5500 的 32 KB RAM 分给 8 个 socket；本测试只使用 socket 0，
    // 所以 TX/RX 各给它 16 KB，其余 socket 不分配。
    if (ctlwizchip(CW_INIT_WIZCHIP, socket_buffer) != 0)
    {
        return -1;
    }

    // 版本寄存器固定应为 0x04；不是它通常表示 SPI、CS 或复位有问题。
    g_w5500_version = getVERSIONR();
    if (g_w5500_version != 0x04U)
    {
        g_w5500_network_init_result = -2;
        return -2;
    }

    // 将 MAC、板子 IP、子网掩码和网关写入 W5500。
    g_w5500_network_init_result = ctlnetwork(CN_SET_NETINFO, &network);
    return g_w5500_network_init_result;
}

// 作用：从接收缓冲区头部删除一帧已经完成响应的 Modbus TCP 请求。
// 用法：响应完整发送后调用；remaining_length 会更新为剩余粘包数据长度。
static void ModbusTcp_RemoveRequest(modbus_octet_t *buffer,
                                    uint16_t *remaining_length,
                                    uint16_t removed_length)
{
    uint16_t index;

    if (removed_length >= *remaining_length)
    {
        *remaining_length = 0U;
        return;
    }

    for (index = removed_length; index < *remaining_length; index++)
    {
        buffer[index - removed_length] = buffer[index];
    }
    *remaining_length = (uint16_t)(*remaining_length - removed_length);
}

// 作用：维护 W5500 的 Modbus TCP Server，把 HMI 的功能码请求交给协议层处理。
// 用法：main() 每 10 ms 调用；HMI 连接 192.168.1.20:502，通用寄存器映射见 modbus_tcp_server.h。
static void ModbusTcpServerPoll(void)
{
    static modbus_octet_t request_buffer[MODBUS_TCP_MAX_ADU_LENGTH];
    static modbus_octet_t response_buffer[MODBUS_TCP_MAX_ADU_LENGTH];
    static uint16_t request_length = 0U;
    static uint16_t response_length = 0U;
    static uint16_t response_sent = 0U;
    static uint16_t pending_frame_length = 0U;
    modbus_tcp_result_t process_result;
    uint8_t phy_link = PHY_LINK_OFF;
    uint8_t status = getSn_SR(W5500_SOCKET);
    uint16_t available_length;
    uint16_t read_length;
    uint16_t mbap_length;
    uint16_t frame_length;

    // 每进入一次轮询就递增，确认 main() 确实持续执行网络服务。
    g_modbus_poll_count++;

    // 保存 socket 状态，便于在 CCS Watch 中确认是否已进入 SOCK_ESTABLISHED。
    g_modbus_socket_status = status;

    // 读取 PHY 链路状态，仅用于 CCS 监视。不能在这里直接 close socket：
    // 某些 W5500 驱动未实现可靠的 PHY 查询，可能在 TCP 已连接时误报断链。
    if (ctlwizchip(CW_GET_PHYLINK, &phy_link) != 0)
    {
        g_w5500_phy_link = 0U;
    }
    else
    {
        g_w5500_phy_link = phy_link;
    }

    if (status == SOCK_CLOSED)
    {
        // 创建非阻塞 TCP socket；DSP作为服务器使用标准 Modbus TCP 端口 502。
        request_length = 0U;
        response_length = 0U;
        response_sent = 0U;
        pending_frame_length = 0U;
        g_modbus_last_frame_length = 0U;
        g_modbus_last_mbap_length = 0U;
        g_modbus_last_request_address = 0U;
        g_modbus_last_request_quantity = 0U;
        g_modbus_last_response_length = 0U;
        (void)socket(W5500_SOCKET, Sn_MR_TCP,
                     MODBUS_TCP_SERVER_PORT, SF_IO_NONBLOCK);
        return;
    }

    if (status == SOCK_INIT)
    {
        // socket创建后进入监听状态，等待HMI主动建立TCP连接。
        if (listen(W5500_SOCKET) < SOCK_OK)
        {
            (void)close(W5500_SOCKET);
        }
        return;
    }

    if (status == SOCK_LISTEN)
    {
        // W5500硬件负责接受连接，监听期间主程序无需阻塞等待。
        return;
    }

    if (status == SOCK_ESTABLISHED)
    {
        // 保存 W5500 原始 RX 状态，用于区分网络未收包和软件未读取。
        g_modbus_rx_available = getSn_RX_RSR(W5500_SOCKET);
        g_modbus_rx_read_pointer = getSn_RX_RD(W5500_SOCKET);
        g_modbus_rx_write_pointer = getSn_RX_WR(W5500_SOCKET);
        g_modbus_socket_interrupt = getSn_IR(W5500_SOCKET);

        // 新连接建立后清除连接事件，并启用W5500硬件TCP keepalive。
        if ((getSn_IR(W5500_SOCKET) & Sn_IR_CON) != 0U)
        {
            setSn_IR(W5500_SOCKET, Sn_IR_CON);
            setSn_KPALVTR(W5500_SOCKET, TCP_KEEPALIVE_5S_UNITS);
        }

        // 如果上次响应尚未全部提交，先继续发送，避免覆盖响应缓冲区。
        if (response_length != 0U)
        {
            g_modbus_last_send_result = send(
                W5500_SOCKET,
                (uint8_t *)&response_buffer[response_sent],
                (uint16_t)(response_length - response_sent));

            if (g_modbus_last_send_result > 0)
            {
                response_sent = (uint16_t)(response_sent +
                                           g_modbus_last_send_result);
                if (response_sent >= response_length)
                {
                    ModbusTcp_RemoveRequest(request_buffer,
                                            &request_length,
                                            pending_frame_length);
                    response_length = 0U;
                    response_sent = 0U;
                    pending_frame_length = 0U;
                    g_modbus_request_count++;
                }
            }
            else if (g_modbus_last_send_result != SOCK_BUSY)
            {
                (void)close(W5500_SOCKET);
            }
            return;
        }

        // 按剩余缓冲区容量接收TCP字节流，可兼容分片到达和连续粘包。
        available_length = g_modbus_rx_available;
        if (available_length != 0U)
        {
            if (request_length >= MODBUS_TCP_MAX_ADU_LENGTH)
            {
                (void)close(W5500_SOCKET);
                return;
            }

            read_length = (uint16_t)(MODBUS_TCP_MAX_ADU_LENGTH -
                                     request_length);
            if (read_length > available_length)
            {
                read_length = available_length;
            }

            g_modbus_last_receive_result = recv(
                W5500_SOCKET,
                (uint8_t *)&request_buffer[request_length],
                read_length);
            if (g_modbus_last_receive_result > 0)
            {
                request_length = (uint16_t)(request_length +
                                            g_modbus_last_receive_result);
            }
            else if (g_modbus_last_receive_result != SOCK_BUSY)
            {
                (void)close(W5500_SOCKET);
                return;
            }
        }

        // MBAP头至少6字节；Length字段决定当前完整请求帧的总长度。
        if (request_length >= 6U)
        {
            mbap_length = (uint16_t)(((uint16_t)request_buffer[4] << 8U) |
                                     (uint16_t)request_buffer[5]);
            g_modbus_last_mbap_length = mbap_length;
            if ((mbap_length < 2U) ||
                (mbap_length >
                 (MODBUS_TCP_MAX_ADU_LENGTH - 6U)))
            {
                (void)close(W5500_SOCKET);
                return;
            }

            frame_length = (uint16_t)(6U + mbap_length);
            g_modbus_last_frame_length = frame_length;
            if (request_length >= frame_length)
            {
                if (frame_length >= 12U)
                {
                    g_modbus_last_request_address =
                        (uint16_t)(((uint16_t)request_buffer[8] << 8U) |
                                   (uint16_t)request_buffer[9]);
                    g_modbus_last_request_quantity =
                        (uint16_t)(((uint16_t)request_buffer[10] << 8U) |
                                   (uint16_t)request_buffer[11]);
                }

                // 交给协议层：按功能码分别校验长度，合法但不支持的请求回 Modbus 异常，
                // 不会因为收到 FC16 或未知功能码而关闭连接。
                response_length = ModbusTcp_ProcessRequest(
                    request_buffer,
                    frame_length,
                    g_detection_count,
                    response_buffer,
                    MODBUS_TCP_MAX_ADU_LENGTH,
                    &process_result);

                g_modbus_last_function = process_result.function;
                g_modbus_last_exception_code = process_result.exception_code;
                g_modbus_last_write_address = process_result.write_address;
                g_modbus_last_write_quantity = process_result.write_quantity;
                g_modbus_last_response_length = response_length;

                if (response_length == 0U)
                {
                    // 只有 MBAP 本身无法解析（协议标识非 0、长度字段超出已收字节数）
                    // 才会走到这里。丢弃该帧并继续，不能让无法处理的请求一直占着
                    // 缓冲区把它堆满，否则会误关连接。
                    ModbusTcp_RemoveRequest(request_buffer,
                                            &request_length,
                                            frame_length);
                    return;
                }

                response_sent = 0U;
                pending_frame_length = frame_length;
            }
        }
        return;
    }

    if (status == SOCK_CLOSE_WAIT)
    {
        // HMI关闭连接后完成TCP断开，下一轮重新创建并监听socket。
        (void)disconnect(W5500_SOCKET);
        return;
    }

    // 其他异常或关闭过渡状态统一关闭，避免旧请求污染下一次连接。
    (void)close(W5500_SOCKET);
}

// 程序从这里开始：初始化CPU和W5500，然后持续处理HMI的Modbus请求。
// 作用：完成CPU1、SysConfig和W5500初始化，并提供4x-5的32位固定检测数量。
// 用法：C28xx_CPU1 复位后由启动代码自动进入；不由其他函数主动调用。
void main(void)
{
    // network_ready 为 0 表示 W5500 初始化成功；非 0 时按周期重试初始化。
    int network_ready;
    uint16_t network_retry_ticks = 0U;

    // 以下是 C2000 基础初始化；Board_init() 才是 SysConfig 生成的硬件配置入口。
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();
    Board_init();
    C2000Ware_libraries_init();

    // modbus_data 段由链接脚本放进 RAMGS0。SysConfig/board.c 没有显式配置 GS RAM
    // 的归属，这里明确把 GS0 划给 CPU1，避免以后给 CPU2/CM 分配共享内存时被改走。
    // 只动 GS0，不碰 RAMGS1~15，也不影响 W5500 和编码器使用的区域。
    MemCfg_setGSRAMMasterSel(MEMCFG_SECT_GS0, MEMCFG_GSRAMMASTER_CPU1);

    // modbus_data 不在 .bss 的自动清零范围内，启动时显式清一次，
    // 保证首次上电读到的是 0 而不是随机值。
    ModbusTcp_ResetStorage();

    // 先确认W5500能通过SPI读到版本号，再启动502端口；失败也不会卡死。
    network_ready = W5500_NetworkInit();
    g_network_ready_state = network_ready;

    while (1)
    {
        if (network_ready == 0)
        {
            // W5500正常时持续响应HMI对4x-5两个寄存器的读取请求。
            ModbusTcpServerPoll();
        }
        else
        {
            // 初始化失败后每累计 1 秒复位并重试 W5500。
            network_retry_ticks++;
            if (network_retry_ticks >= NETWORK_RETRY_TICKS)
            {
                // 每100个主循环周期重新初始化一次W5500，约为1秒。
                network_retry_ticks = 0U;
                network_ready = W5500_NetworkInit();
                g_network_ready_state = network_ready;
            }
        }

        // 10 ms轮询可及时处理HMI请求，同时避免主循环无延时占满CPU。
        DEVICE_DELAY_US(MAIN_LOOP_PERIOD_US);
    }
}
