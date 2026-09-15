#include "encoder_tcp_payload.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void AssertPayload(uint32_t position, const char *expected,
                          uint16_t expected_length)
{
    encoder_octet_t payload[ENCODER_TCP_MAX_PAYLOAD_LENGTH];
    uint16_t index;
    uint16_t length = EncoderTcp_BuildPayload(position, payload,
                                               ENCODER_TCP_MAX_PAYLOAD_LENGTH);

    assert(length == expected_length);
    for (index = 0U; index < length; index++)
    {
        assert(payload[index] == (encoder_octet_t)expected[index]);
    }
}

static void AssertDiagnosticPayload(uint32_t position,
                                    uint16_t input_a,
                                    uint16_t input_b,
                                    uint16_t input_z,
                                    const char *expected,
                                    uint16_t expected_length)
{
    encoder_octet_t payload[ENCODER_TCP_MAX_PAYLOAD_LENGTH];
    uint16_t index;
    uint16_t length = EncoderTcp_BuildDiagnosticPayload(
        position, input_a, input_b, input_z, payload,
        ENCODER_TCP_MAX_PAYLOAD_LENGTH);

    assert(length == expected_length);
    for (index = 0U; index < length; index++)
    {
        assert(payload[index] == (encoder_octet_t)expected[index]);
    }
}

int main(void)
{
    encoder_octet_t too_small[4];

    // These literals independently define the wire format expected by the PC.
    AssertPayload(0U, "encoder=0\r\n", 11U);
    AssertPayload(1234U, "encoder=1234\r\n", 14U);
    AssertPayload(2047U, "encoder=2047\r\n", 14U);
    AssertPayload(4294967295UL, "encoder=4294967295\r\n", 20U);
    assert(EncoderTcp_BuildPayload(1234U, too_small, 4U) == 0U);

    // This catches missing or misordered raw A/B/Z diagnostic fields.
    AssertDiagnosticPayload(0U, 0U, 0U, 0U,
                            "encoder=0,A=0,B=0,Z=0\r\n", 23U);
    AssertDiagnosticPayload(1234U, 1U, 0U, 1U,
                            "encoder=1234,A=1,B=0,Z=1\r\n", 26U);
    AssertDiagnosticPayload(4294967295UL, 1U, 1U, 1U,
                            "encoder=4294967295,A=1,B=1,Z=1\r\n", 32U);

    // The TCP stream is called every 10 ms and must publish one sample
    // exactly every five calls, i.e. every 50 ms.
    assert(EncoderTcp_IsSendDue(4U, 5U) == 0U);
    assert(EncoderTcp_IsSendDue(5U, 5U) == 1U);
    assert(EncoderTcp_IsSendDue(6U, 5U) == 1U);
    assert(EncoderTcp_IsSendDue(0U, 0U) == 0U);

    puts("Encoder TCP payload tests passed.");
    return 0;
}
