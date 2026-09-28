#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "modbus_rtu_server.h"

static void expect_u16(const char *name, uint16_t actual, uint16_t expected)
{
    if (actual != expected)
    {
        fprintf(stderr, "%s: expected 0x%04X, got 0x%04X\n",
                name, expected, actual);
        exit(1);
    }
}

static void expect_bytes(const char *name,
                         const modbus_octet_t *actual,
                         const modbus_octet_t *expected,
                         uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        if (actual[index] != expected[index])
        {
            fprintf(stderr,
                    "%s byte %u: expected 0x%02X, got 0x%02X\n",
                    name, index, expected[index], actual[index]);
            exit(1);
        }
    }
}

int main(void)
{
    static const modbus_octet_t request[] = {
        0x01U, 0x03U, 0x00U, 0x04U, 0x00U, 0x02U
    };
    static const modbus_octet_t request_with_crc[] = {
        0x01U, 0x03U, 0x00U, 0x04U, 0x00U, 0x02U, 0x85U, 0xCAU
    };
    static const modbus_octet_t expected_response[] = {
        0x01U, 0x03U, 0x04U, 0x56U, 0x78U, 0x12U, 0x34U, 0x66U, 0xD5U
    };
    modbus_octet_t response[MODBUS_RTU_MAX_ADU_LENGTH];
    modbus_tcp_result_t result;
    uint16_t response_length;

    expect_u16("CRC16 for FC03 request",
               ModbusRtu_CalculateCrc(request, sizeof(request)),
               0xCA85U);

    ModbusTcp_ResetStorage();
    response_length = ModbusRtu_ProcessRequest(
        request_with_crc,
        sizeof(request_with_crc),
        1U,
        0x12345678UL,
        response,
        sizeof(response),
        &result);

    expect_u16("FC03 response length",
               response_length,
               sizeof(expected_response));
    expect_bytes("FC03 response",
                 response,
                 expected_response,
                 sizeof(expected_response));

    {
        modbus_octet_t bad_crc_request[sizeof(request_with_crc)];
        uint16_t index;

        for (index = 0U; index < sizeof(request_with_crc); index++)
        {
            bad_crc_request[index] = request_with_crc[index];
        }
        bad_crc_request[sizeof(bad_crc_request) - 1U] ^= 0x01U;

        response_length = ModbusRtu_ProcessRequest(
            bad_crc_request,
            sizeof(bad_crc_request),
            1U,
            0U,
            response,
            sizeof(response),
            &result);
        expect_u16("bad CRC is discarded", response_length, 0U);
    }

    {
        modbus_octet_t other_slave_request[sizeof(request_with_crc)];
        uint16_t crc;
        uint16_t index;

        for (index = 0U; index < sizeof(request_with_crc); index++)
        {
            other_slave_request[index] = request_with_crc[index];
        }
        other_slave_request[0] = 2U;
        crc = ModbusRtu_CalculateCrc(
            other_slave_request, (uint16_t)(sizeof(other_slave_request) - 2U));
        other_slave_request[sizeof(other_slave_request) - 2U] =
            (modbus_octet_t)crc;
        other_slave_request[sizeof(other_slave_request) - 1U] =
            (modbus_octet_t)(crc >> 8U);

        response_length = ModbusRtu_ProcessRequest(
            other_slave_request,
            sizeof(other_slave_request),
            1U,
            0U,
            response,
            sizeof(response),
            &result);
        expect_u16("other slave is ignored", response_length, 0U);
    }

    puts("modbus_rtu_server tests passed");
    return 0;
}
