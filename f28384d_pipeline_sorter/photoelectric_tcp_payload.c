//#############################################################################
// FILE:   photoelectric_tcp_payload.c
// TITLE:  光电检测 TCP 文本生成
//#############################################################################

#include "photoelectric_tcp_payload.h"

uint16_t photoelectric_tcp_build_payload(
    uint16_t detected,
    photoelectric_octet_t *payload,
    uint16_t payload_capacity)
{
    if ((payload == 0) ||
        (payload_capacity < PHOTOELECTRIC_TCP_PAYLOAD_LENGTH))
    {
        return 0U;
    }

    payload[0] = (detected != 0U) ? '1' : '0';
    payload[1] = '\r';
    payload[2] = '\n';
    return PHOTOELECTRIC_TCP_PAYLOAD_LENGTH;
}

uint16_t photoelectric_tcp_is_send_due(uint16_t elapsed_ticks,
                                       uint16_t period_ticks)
{
    if ((period_ticks != 0U) && (elapsed_ticks >= period_ticks))
    {
        return 1U;
    }

    return 0U;
}
