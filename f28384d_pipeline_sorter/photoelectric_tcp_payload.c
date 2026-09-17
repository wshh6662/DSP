//#############################################################################
// FILE:   photoelectric_tcp_payload.c
// TITLE:  转盘转速 + 光电检测 TCP 文本生成
//#############################################################################
//
// 发送格式（每约 50 ms 一行）：
//   speed=29.29,photo=1\r\n
//   speed=-12.50,photo=0\r\n
//   speed=0.00,photo=0\r\n
//
// 转速由 0.01 rpm 的定点整数手工转换成 ASCII，不用 sprintf 的浮点格式化：
//   2929  -> "29.29"
//   -1250 -> "-12.50"
//   0     -> "0.00"
//#############################################################################

#include "photoelectric_tcp_payload.h"

// 逐字节写入：容量不足时返回 0，否则返回新的写入位置。
static uint16_t payload_append_char(photoelectric_octet_t *payload,
                                    uint16_t payload_capacity,
                                    uint16_t length,
                                    char value)
{
    if (length >= payload_capacity)
    {
        return 0U;
    }

    payload[length] = (photoelectric_octet_t)value;
    return (uint16_t)(length + 1U);
}

static uint16_t payload_append_text(photoelectric_octet_t *payload,
                                    uint16_t payload_capacity,
                                    uint16_t length,
                                    const char *text)
{
    while (*text != '\0')
    {
        length = payload_append_char(payload, payload_capacity, length, *text);
        if (length == 0U)
        {
            return 0U;
        }

        text++;
    }

    return length;
}

// 把 0.01 rpm 定点数写成 "整数.两位小数"，带前导负号。
static uint16_t payload_append_rpm_x100(photoelectric_octet_t *payload,
                                        uint16_t payload_capacity,
                                        uint16_t length,
                                        int32_t speed_rpm_x100)
{
    uint32_t magnitude;
    uint32_t whole;
    uint32_t fraction;
    char digits[12];
    uint16_t digit_count = 0U;
    uint16_t index;

    if (speed_rpm_x100 < 0)
    {
        // 先写负号，再用 -(value + 1) + 1 取绝对值，避免 INT32_MIN 直接取负溢出。
        length = payload_append_char(payload, payload_capacity, length, '-');
        if (length == 0U)
        {
            return 0U;
        }

        magnitude = (uint32_t)(-(speed_rpm_x100 + 1)) + 1U;
    }
    else
    {
        magnitude = (uint32_t)speed_rpm_x100;
    }

    whole = magnitude / 100U;
    fraction = magnitude % 100U;

    // 整数部分按低位到高位压栈，再反序输出；0 也要输出一位 '0'。
    do
    {
        digits[digit_count] = (char)('0' + (char)(whole % 10U));
        digit_count++;
        whole /= 10U;
    } while (whole != 0U);

    for (index = digit_count; index > 0U; index--)
    {
        length = payload_append_char(payload,
                                     payload_capacity,
                                     length,
                                     digits[index - 1U]);
        if (length == 0U)
        {
            return 0U;
        }
    }

    length = payload_append_char(payload, payload_capacity, length, '.');
    if (length == 0U)
    {
        return 0U;
    }

    // 小数部分固定两位、不足补零。
    length = payload_append_char(payload,
                                 payload_capacity,
                                 length,
                                 (char)('0' + (char)(fraction / 10U)));
    if (length == 0U)
    {
        return 0U;
    }

    return payload_append_char(payload,
                               payload_capacity,
                               length,
                               (char)('0' + (char)(fraction % 10U)));
}

uint16_t photoelectric_tcp_build_payload(
    uint16_t detected,
    int32_t speed_rpm_x100,
    photoelectric_octet_t *payload,
    uint16_t payload_capacity)
{
    uint16_t length;

    if (payload == 0)
    {
        return 0U;
    }

    length = payload_append_text(payload, payload_capacity, 0U, "speed=");
    if (length == 0U)
    {
        return 0U;
    }

    length = payload_append_rpm_x100(payload,
                                     payload_capacity,
                                     length,
                                     speed_rpm_x100);
    if (length == 0U)
    {
        return 0U;
    }

    length = payload_append_text(payload, payload_capacity, length, ",photo=");
    if (length == 0U)
    {
        return 0U;
    }

    length = payload_append_char(payload,
                                 payload_capacity,
                                 length,
                                 (detected != 0U) ? '1' : '0');
    if (length == 0U)
    {
        return 0U;
    }

    return payload_append_text(payload, payload_capacity, length, "\r\n");
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
