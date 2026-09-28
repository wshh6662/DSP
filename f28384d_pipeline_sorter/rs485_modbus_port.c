#include "rs485_modbus_port.h"

#include "device.h"
#include "driverlib.h"
#include "modbus_rtu_server.h"

static modbus_octet_t rs485_request[MODBUS_RTU_MAX_ADU_LENGTH];
static modbus_octet_t rs485_response[MODBUS_RTU_MAX_ADU_LENGTH];
static uint16_t rs485_request_length = 0U;
static uint16_t rs485_idle_polls = 0U;
static uint16_t rs485_count_reset_requested = 0U;

static uint16_t rs485_request_sets_coil(const modbus_tcp_result_t *result,
                                       uint16_t target_address)
{
    uint16_t bit_index;

    if ((result->exception_code != 0U) ||
        (result->write_quantity == 0U))
    {
        return 0U;
    }

    if ((result->function == MODBUS_FC_WRITE_SINGLE_COIL) &&
        (result->write_address == target_address))
    {
        return ((rs485_request[4] == 0xFFU) &&
                (rs485_request[5] == 0x00U)) ? 1U : 0U;
    }

    if ((result->function == MODBUS_FC_WRITE_MULTIPLE_COILS) &&
        (result->write_address <= target_address) &&
        ((uint32_t)result->write_address + (uint32_t)result->write_quantity >
         (uint32_t)target_address))
    {
        bit_index = (uint16_t)(target_address - result->write_address);
        return (uint16_t)((rs485_request[7U + (bit_index >> 3U)] >>
                           (bit_index & 7U)) & 0x01U);
    }

    return 0U;
}

static void rs485_set_receive_mode(void)
{
    GPIO_writePin(RS485_MODBUS_DIRECTION_PIN,
                  RS485_MODBUS_RX_ENABLE_LEVEL);
}

static void rs485_set_transmit_mode(void)
{
    GPIO_writePin(RS485_MODBUS_DIRECTION_PIN,
                  RS485_MODBUS_TX_ENABLE_LEVEL);
}

static void rs485_send_response(uint16_t response_length)
{
    uint16_t index;

    rs485_set_transmit_mode();

    for (index = 0U; index < response_length; index++)
    {
        SCI_writeCharBlockingFIFO(SCIB_BASE,
                                  (uint16_t)rs485_response[index]);
    }

    // FIFO 为空并不代表最后一个停止位已经发出。C2000Ware 在启用 FIFO 时，
    // SCI_isTransmitterBusy() 只检查 TX FIFO 计数，因此还要等待一个完整的
    // 115200 8N1 字符时间（约 87 us），避免切回接收时截断帧尾 CRC。
    while (SCI_isTransmitterBusy(SCIB_BASE))
    {
    }
    DEVICE_DELAY_US(100U);

    rs485_set_receive_mode();
}

static void rs485_process_request(uint32_t detection_count)
{
    modbus_tcp_result_t result;
    uint16_t response_length;

    response_length = ModbusRtu_ProcessRequest(
        rs485_request,
        rs485_request_length,
        RS485_MODBUS_SLAVE_ADDRESS,
        detection_count,
        rs485_response,
        MODBUS_RTU_MAX_ADU_LENGTH,
        &result);

    if (rs485_request_sets_coil(&result,
                               RS485_MODBUS_COUNT_RESET_COIL_ADDRESS) != 0U)
    {
        rs485_count_reset_requested = 1U;
        ModbusTcp_ClearCoil(RS485_MODBUS_COUNT_RESET_COIL_ADDRESS);
    }
    if (rs485_request_sets_coil(&result,
                               RS485_MODBUS_QUEUE_RESET_COIL_ADDRESS) != 0U)
    {
        ModbusTcp_ClearCoil(RS485_MODBUS_QUEUE_RESET_COIL_ADDRESS);
    }
    if (rs485_request_sets_coil(&result,
                               RS485_MODBUS_ALARM_CLEAR_COIL_ADDRESS) != 0U)
    {
        ModbusTcp_ClearCoil(RS485_MODBUS_ALARM_CLEAR_COIL_ADDRESS);
    }

    rs485_request_length = 0U;

    if (response_length != 0U)
    {
        rs485_send_response(response_length);
    }
}

void rs485_modbus_port_init(void)
{
    // 将新版板卡标注的 SCI2 引脚连接到 F28384D 的 SCIB 外设。
    GPIO_setPinConfig(GPIO_54_SCIB_TX);
    GPIO_setPinConfig(GPIO_55_SCIB_RX);
    GPIO_setPinConfig(GPIO_133_GPIO133);

    GPIO_setPadConfig(54U, GPIO_PIN_TYPE_STD);
    GPIO_setPadConfig(55U, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(55U, GPIO_QUAL_ASYNC);

    // GPIO133 控制 RS485 收发器方向。先写接收电平，再设为输出，减少上电毛刺。
    rs485_set_receive_mode();
    GPIO_setPadConfig(RS485_MODBUS_DIRECTION_PIN, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(RS485_MODBUS_DIRECTION_PIN, GPIO_DIR_MODE_OUT);

    // 显示器 COM2 配置为 115200、8N1；两端参数不一致时无法通过 CRC 校验。
    SCI_setConfig(SCIB_BASE,
                  DEVICE_LSPCLK_FREQ,
                  RS485_MODBUS_BAUD_RATE,
                  SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE |
                      SCI_CONFIG_PAR_NONE);
    SCI_enableFIFO(SCIB_BASE);
    SCI_resetChannels(SCIB_BASE);
    SCI_resetRxFIFO(SCIB_BASE);
    SCI_resetTxFIFO(SCIB_BASE);
    SCI_enableModule(SCIB_BASE);

    rs485_request_length = 0U;
    rs485_idle_polls = 0U;
    rs485_count_reset_requested = 0U;
}

uint16_t rs485_modbus_port_take_count_reset_request(void)
{
    uint16_t requested = rs485_count_reset_requested;

    rs485_count_reset_requested = 0U;
    return requested;
}

void rs485_modbus_port_poll(uint32_t detection_count)
{
    uint16_t received_any = 0U;
    uint16_t rx_status;

    rx_status = SCI_getRxStatus(SCIB_BASE);

    // BREAK、framing 等接收错误会锁存在 SCI 中；不复位模块时，后续读取会
    // 持续得到 0x00，并被误判为短 Modbus 帧。软件复位只清状态，不改波特率配置。
    if ((rx_status & SCI_RXSTATUS_ERROR) != 0U)
    {
        SCI_performSoftwareReset(SCIB_BASE);
        SCI_resetRxFIFO(SCIB_BASE);
        rs485_request_length = 0U;
        rs485_idle_polls = 0U;
        return;
    }

    if (SCI_getOverflowStatus(SCIB_BASE))
    {
        SCI_clearOverflowStatus(SCIB_BASE);
        SCI_resetRxFIFO(SCIB_BASE);
        rs485_request_length = 0U;
        rs485_idle_polls = 0U;
        return;
    }

    while (SCI_getRxFIFOStatus(SCIB_BASE) != SCI_FIFO_RX0)
    {
        received_any = 1U;

        if (rs485_request_length < MODBUS_RTU_MAX_ADU_LENGTH)
        {
            rs485_request[rs485_request_length] =
                (modbus_octet_t)SCI_readCharNonBlocking(SCIB_BASE);
            rs485_request_length++;
        }
        else
        {
            (void)SCI_readCharNonBlocking(SCIB_BASE);
            rs485_request_length = 0U;
        }
    }

    if (received_any != 0U)
    {
        rs485_idle_polls = 0U;
    }
    else if (rs485_request_length != 0U)
    {
        rs485_idle_polls++;
        if (rs485_idle_polls >= RS485_MODBUS_FRAME_GAP_POLLS)
        {
            rs485_idle_polls = 0U;
            rs485_process_request(detection_count);
        }
    }
}
