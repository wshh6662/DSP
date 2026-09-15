#if ENABLE_MODBUS_TCP_HMI
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


#endif

// 旧 HMI Modbus 轮询代码保留在这里，当前编码器 TCP 模式不参与编译。
// 若日后要切回 Modbus TCP Server，改为 1 并在 main() 中调用 ModbusTcpServerPoll()。
#if ENABLE_MODBUS_TCP_HMI
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
#endif
