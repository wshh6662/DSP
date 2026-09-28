#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driverlib.h"
#include "device.h"
#include "modbus_rtu_server.h"
#include "rs485_modbus_port.h"

static uint32_t configured_pins[3];
static uint16_t configured_pin_count;
static uint32_t configured_sci_base;
static uint32_t configured_lspclk;
static uint32_t configured_baud;
static uint32_t configured_format;
static uint32_t direction_pin;
static uint32_t direction_mode;
static uint32_t last_written_pin;
static uint32_t last_written_value;
static uint16_t fake_rx_data[16];
static uint16_t fake_rx_length;
static uint16_t fake_rx_index;
static uint16_t fake_tx_data[MODBUS_RTU_MAX_ADU_LENGTH];
static uint16_t fake_tx_length;
static uint16_t fake_rx_status;
static uint16_t software_reset_count;
static uint32_t last_delay_us;

static void expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected)
    {
        fprintf(stderr, "%s: expected %lu, got %lu\n",
                name, (unsigned long)expected, (unsigned long)actual);
        exit(1);
    }
}

void GPIO_setPinConfig(uint32_t pin_config)
{
    if (configured_pin_count < 3U)
    {
        configured_pins[configured_pin_count++] = pin_config;
    }
}

void GPIO_setDirectionMode(uint32_t pin, uint32_t direction)
{
    direction_pin = pin;
    direction_mode = direction;
}

void GPIO_setPadConfig(uint32_t pin, uint32_t pin_type)
{
    (void)pin;
    (void)pin_type;
}

void GPIO_setQualificationMode(uint32_t pin, uint32_t qualification)
{
    (void)pin;
    (void)qualification;
}

void GPIO_writePin(uint32_t pin, uint32_t value)
{
    last_written_pin = pin;
    last_written_value = value;
}

uint32_t GPIO_readPin(uint32_t pin)
{
    (void)pin;
    return last_written_value;
}

void SCI_setConfig(uint32_t base, uint32_t lspclk_hz, uint32_t baud,
                   uint32_t config)
{
    configured_sci_base = base;
    configured_lspclk = lspclk_hz;
    configured_baud = baud;
    configured_format = config;
}

void SCI_enableFIFO(uint32_t base) { (void)base; }
void SCI_resetChannels(uint32_t base) { (void)base; }
void SCI_resetRxFIFO(uint32_t base) { (void)base; }
void SCI_resetTxFIFO(uint32_t base) { (void)base; }
void SCI_enableModule(uint32_t base) { (void)base; }
uint16_t SCI_getRxFIFOStatus(uint32_t base)
{
    (void)base;
    return (fake_rx_index < fake_rx_length) ? 1U : SCI_FIFO_RX0;
}
uint16_t SCI_readCharNonBlocking(uint32_t base)
{
    (void)base;
    return fake_rx_data[fake_rx_index++];
}
void SCI_writeCharBlockingFIFO(uint32_t base, uint16_t data)
{
    (void)base;
    fake_tx_data[fake_tx_length++] = data;
}
bool SCI_isTransmitterBusy(uint32_t base) { (void)base; return false; }
uint16_t SCI_getRxStatus(uint32_t base) { (void)base; return fake_rx_status; }
bool SCI_getOverflowStatus(uint32_t base) { (void)base; return false; }
void SCI_clearOverflowStatus(uint32_t base) { (void)base; }
void SCI_performSoftwareReset(uint32_t base)
{
    (void)base;
    software_reset_count++;
    fake_rx_status = 0U;
}
void test_device_delay_us(uint32_t delay_us) { last_delay_us = delay_us; }

int main(void)
{
    static const uint16_t request[] = {
        0x01U, 0x03U, 0x00U, 0x04U, 0x00U, 0x02U, 0x85U, 0xCAU
    };
    static const uint16_t expected_response[] = {
        0x01U, 0x03U, 0x04U, 0x56U, 0x78U, 0x12U, 0x34U, 0x66U, 0xD5U
    };
    uint16_t index;
    uint16_t crc;
    modbus_octet_t reset_request[8];
    modbus_octet_t coil_response[MODBUS_TCP_MAX_ADU_LENGTH];
    modbus_octet_t read_reset_coil[] = {
        0U, 1U, 0U, 0U, 0U, 6U, 1U, 1U, 0U, 82U, 0U, 1U
    };

    rs485_modbus_port_init();

    expect_u32("pin count", configured_pin_count, 3U);
    expect_u32("TX pin mux", configured_pins[0], GPIO_54_SCIB_TX);
    expect_u32("RX pin mux", configured_pins[1], GPIO_55_SCIB_RX);
    expect_u32("direction pin mux", configured_pins[2], GPIO_133_GPIO133);
    expect_u32("SCI base", configured_sci_base, SCIB_BASE);
    expect_u32("SCI clock", configured_lspclk, DEVICE_LSPCLK_FREQ);
    expect_u32("baud", configured_baud, 115200U);
    expect_u32("format", configured_format,
               SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE |
               SCI_CONFIG_PAR_NONE);
    expect_u32("direction GPIO", direction_pin, 133U);
    expect_u32("direction mode", direction_mode, GPIO_DIR_MODE_OUT);
    expect_u32("receive mode GPIO", last_written_pin, 133U);
    expect_u32("receive mode level", last_written_value, 0U);

    fake_rx_status = SCI_RXSTATUS_ERROR |
                     SCI_RXSTATUS_BREAK |
                     SCI_RXSTATUS_FRAMING;
    rs485_modbus_port_poll(0U);
    expect_u32("SCI software reset", software_reset_count, 1U);
    rs485_modbus_port_poll(0U);
    expect_u32("SCI reset occurs once", software_reset_count, 1U);

    ModbusTcp_ResetStorage();
    for (index = 0U; index < (uint16_t)(sizeof(request) / sizeof(request[0]));
         index++)
    {
        fake_rx_data[index] = request[index];
    }
    fake_rx_length = (uint16_t)(sizeof(request) / sizeof(request[0]));

    rs485_modbus_port_poll(0x12345678UL);
    rs485_modbus_port_poll(0x12345678UL);
    rs485_modbus_port_poll(0x12345678UL);

    expect_u32("RTU response length", fake_tx_length,
               sizeof(expected_response) / sizeof(expected_response[0]));
    for (index = 0U;
         index < (uint16_t)(sizeof(expected_response) /
                            sizeof(expected_response[0]));
         index++)
    {
        expect_u32("RTU response byte", fake_tx_data[index],
                   expected_response[index]);
    }
    if (last_delay_us < 87U)
    {
        fprintf(stderr,
                "RS485 transmit tail delay: expected at least 87 us, got %lu\n",
                (unsigned long)last_delay_us);
        return 1;
    }
    expect_u32("direction returns to receive", last_written_value, 0U);

    // EasyBuilder 0x-83 对应协议线圈地址 82；写 ON 必须生成一次计数清零请求。
    fake_rx_index = 0U;
    fake_rx_length = 8U;
    fake_tx_length = 0U;
    reset_request[0] = 0x01U;
    reset_request[1] = 0x05U;
    reset_request[2] = 0x00U;
    reset_request[3] = 0x52U;
    reset_request[4] = 0xFFU;
    reset_request[5] = 0x00U;
    crc = ModbusRtu_CalculateCrc(reset_request, 6U);
    reset_request[6] = (modbus_octet_t)(crc & 0xFFU);
    reset_request[7] = (modbus_octet_t)(crc >> 8U);
    for (index = 0U; index < 8U; index++)
    {
        fake_rx_data[index] = (uint16_t)reset_request[index];
    }

    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);

    expect_u32("count reset response length", fake_tx_length, 8U);
    expect_u32("count reset request", rs485_modbus_port_take_count_reset_request(), 1U);
    expect_u32("count reset request consumed", rs485_modbus_port_take_count_reset_request(), 0U);
    expect_u32("reset coil read response length",
               ModbusTcp_ProcessRequest(read_reset_coil,
                                        (uint16_t)sizeof(read_reset_coil),
                                        9U, coil_response,
                                        MODBUS_TCP_MAX_ADU_LENGTH, 0), 10U);
    expect_u32("reset command coil returns to OFF", coil_response[9], 0U);

    fake_rx_index = 0U;
    fake_tx_length = 0U;
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    expect_u32("second reset press responds", fake_tx_length, 8U);
    expect_u32("second reset press is accepted",
               rs485_modbus_port_take_count_reset_request(), 1U);

    // 报警清除是一次性命令，不应锁存，也不应触发计数清零。
    reset_request[3] = 87U;
    crc = ModbusRtu_CalculateCrc(reset_request, 6U);
    reset_request[6] = (modbus_octet_t)(crc & 0xFFU);
    reset_request[7] = (modbus_octet_t)(crc >> 8U);
    for (index = 0U; index < 8U; index++)
    {
        fake_rx_data[index] = (uint16_t)reset_request[index];
    }
    fake_rx_index = 0U;
    fake_tx_length = 0U;
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    expect_u32("alarm clear response length", fake_tx_length, 8U);
    expect_u32("alarm clear does not reset count",
               rs485_modbus_port_take_count_reset_request(), 0U);
    read_reset_coil[9] = 87U;
    expect_u32("alarm clear read response length",
               ModbusTcp_ProcessRequest(read_reset_coil,
                                        (uint16_t)sizeof(read_reset_coil),
                                        9U, coil_response,
                                        MODBUS_TCP_MAX_ADU_LENGTH, 0), 10U);
    expect_u32("alarm clear coil returns to OFF", coil_response[9], 0U);

    // 队列复位也是命令；检测开关则必须保持写入的 ON 状态。
    reset_request[3] = 86U;
    crc = ModbusRtu_CalculateCrc(reset_request, 6U);
    reset_request[6] = (modbus_octet_t)(crc & 0xFFU);
    reset_request[7] = (modbus_octet_t)(crc >> 8U);
    for (index = 0U; index < 8U; index++)
    {
        fake_rx_data[index] = (uint16_t)reset_request[index];
    }
    fake_rx_index = 0U;
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    read_reset_coil[9] = 86U;
    (void)ModbusTcp_ProcessRequest(read_reset_coil,
                                   (uint16_t)sizeof(read_reset_coil),
                                   9U, coil_response,
                                   MODBUS_TCP_MAX_ADU_LENGTH, 0);
    expect_u32("queue reset coil returns to OFF", coil_response[9], 0U);

    reset_request[3] = 80U;
    crc = ModbusRtu_CalculateCrc(reset_request, 6U);
    reset_request[6] = (modbus_octet_t)(crc & 0xFFU);
    reset_request[7] = (modbus_octet_t)(crc >> 8U);
    for (index = 0U; index < 8U; index++)
    {
        fake_rx_data[index] = (uint16_t)reset_request[index];
    }
    fake_rx_index = 0U;
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    read_reset_coil[9] = 80U;
    (void)ModbusTcp_ProcessRequest(read_reset_coil,
                                   (uint16_t)sizeof(read_reset_coil),
                                   9U, coil_response,
                                   MODBUS_TCP_MAX_ADU_LENGTH, 0);
    expect_u32("detection switch remains ON", coil_response[9], 1U);

    // 错误 CRC 由 RTU 层丢弃，端口既不应答也不执行清零。
    fake_rx_index = 0U;
    fake_tx_length = 0U;
    fake_rx_data[7] ^= 0x01U;
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    rs485_modbus_port_poll(9U);
    expect_u32("bad CRC has no response", fake_tx_length, 0U);
    expect_u32("bad CRC cannot reset count",
               rs485_modbus_port_take_count_reset_request(), 0U);

    puts("rs485_modbus_port tests passed");
    return 0;
}
