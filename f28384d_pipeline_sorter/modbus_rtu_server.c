#include "modbus_rtu_server.h"

#define MODBUS_RTU_ADDRESS_LENGTH 1U
#define MODBUS_RTU_CRC_LENGTH     2U
#define MODBUS_TCP_MBAP_LENGTH    6U

static void ModbusRtu_ClearResult(modbus_tcp_result_t *result)
{
    if (result != 0)
    {
        result->function = 0U;
        result->exception_code = 0U;
        result->write_address = 0U;
        result->write_quantity = 0U;
    }
}

uint16_t ModbusRtu_CalculateCrc(const modbus_octet_t *data,
                                uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint16_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)data[index];

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

uint16_t ModbusRtu_ProcessRequest(const modbus_octet_t *request,
                                  uint16_t request_length,
                                  uint16_t slave_address,
                                  uint32_t detection_count,
                                  modbus_octet_t *response,
                                  uint16_t response_capacity,
                                  modbus_tcp_result_t *result)
{
    modbus_octet_t tcp_request[MODBUS_TCP_MAX_ADU_LENGTH];
    modbus_octet_t tcp_response[MODBUS_TCP_MAX_ADU_LENGTH];
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint16_t pdu_length;
    uint16_t tcp_request_length;
    uint16_t tcp_response_length;
    uint16_t rtu_response_length;
    uint16_t response_crc;
    uint16_t index;
    uint16_t request_address;

    ModbusRtu_ClearResult(result);

    if ((request == 0) || (response == 0) ||
        (request_length < 4U) ||
        (request_length > MODBUS_RTU_MAX_ADU_LENGTH) ||
        (slave_address == 0U) || (slave_address > 247U))
    {
        return 0U;
    }

    request_address = (uint16_t)request[0];
    if ((request_address != 0U) && (request_address != slave_address))
    {
        return 0U;
    }

    received_crc = (uint16_t)request[request_length - 2U] |
                   (uint16_t)((uint16_t)request[request_length - 1U] << 8U);
    calculated_crc = ModbusRtu_CalculateCrc(
        request, (uint16_t)(request_length - MODBUS_RTU_CRC_LENGTH));
    if (received_crc != calculated_crc)
    {
        return 0U;
    }

    pdu_length = (uint16_t)(request_length -
                            MODBUS_RTU_ADDRESS_LENGTH -
                            MODBUS_RTU_CRC_LENGTH);
    tcp_request_length = (uint16_t)(MODBUS_TCP_MBAP_LENGTH +
                                    MODBUS_RTU_ADDRESS_LENGTH +
                                    pdu_length);

    tcp_request[0] = 0U;
    tcp_request[1] = 0U;
    tcp_request[2] = 0U;
    tcp_request[3] = 0U;
    tcp_request[4] = (modbus_octet_t)((pdu_length + 1U) >> 8U);
    tcp_request[5] = (modbus_octet_t)(pdu_length + 1U);
    tcp_request[6] = (modbus_octet_t)request_address;

    for (index = 0U; index < pdu_length; index++)
    {
        tcp_request[7U + index] = request[1U + index];
    }

    tcp_response_length = ModbusTcp_ProcessRequest(
        tcp_request,
        tcp_request_length,
        detection_count,
        tcp_response,
        MODBUS_TCP_MAX_ADU_LENGTH,
        result);
    if (tcp_response_length <= MODBUS_TCP_MBAP_LENGTH)
    {
        return 0U;
    }

    // 地址 0 是广播。写操作已经交给协议层执行，但从站不能发送响应。
    if (request_address == 0U)
    {
        return 0U;
    }

    rtu_response_length = (uint16_t)(tcp_response_length -
                                     MODBUS_TCP_MBAP_LENGTH +
                                     MODBUS_RTU_CRC_LENGTH);
    if ((rtu_response_length > response_capacity) ||
        (rtu_response_length > MODBUS_RTU_MAX_ADU_LENGTH))
    {
        return 0U;
    }

    for (index = MODBUS_TCP_MBAP_LENGTH;
         index < tcp_response_length;
         index++)
    {
        response[index - MODBUS_TCP_MBAP_LENGTH] = tcp_response[index];
    }

    response_crc = ModbusRtu_CalculateCrc(
        response, (uint16_t)(rtu_response_length - MODBUS_RTU_CRC_LENGTH));
    response[rtu_response_length - 2U] = (modbus_octet_t)response_crc;
    response[rtu_response_length - 1U] = (modbus_octet_t)(response_crc >> 8U);

    return rtu_response_length;
}
