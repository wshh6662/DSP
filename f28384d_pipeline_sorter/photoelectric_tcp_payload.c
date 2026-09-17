//#############################################################################
// FILE:   photoelectric_tcp_payload.c
// TITLE:  编码器转速 + 光电检测 + 当前角度 TCP 文本生成
//#############################################################################
//
// 发送格式（每约 1 秒一行）：
//   enc_rpm=29.29,obj=1,angle=359.91\r\n
//   enc_rpm=-29.29,obj=0,angle=0.00\r\n
//
// 转速是 0.01 rpm 的定点整数，手工转换成 ASCII，不用 sprintf 浮点格式化：
//   2929  -> "29.29"
//   -1250 -> "-12.50"
//   0     -> "0.00"
//#############################################################################

#include "photoelectric_tcp_payload.h"

// 带容量保护的写入器：一旦越界就置 overflow 并停止写入，绝不写第 capacity 个字节。
typedef struct
{
    photoelectric_octet_t *buffer;
    uint16_t capacity;
    uint16_t length;
    uint16_t overflow;
} payload_writer_t;

static void payload_write_char(payload_writer_t *writer, char value)
{
    if (writer->overflow != 0U)
    {
        return;
    }

    if (writer->length >= writer->capacity)
    {
        writer->overflow = 1U;
        return;
    }

    writer->buffer[writer->length] = (photoelectric_octet_t)value;
    writer->length++;
}

static void payload_write_text(payload_writer_t *writer, const char *text)
{
    while (*text != '\0')
    {
        payload_write_char(writer, *text);
        text++;
    }
}

// 把 0.01 rpm 定点数写成 "整数.两位小数"，带前导负号。
static void payload_write_fixed_x100(payload_writer_t *writer,
                                     int32_t value_x100)
{
    uint32_t magnitude;
    uint32_t whole;
    uint32_t fraction;
    char digits[12];
    uint16_t digit_count = 0U;
    uint16_t index;

    if (value_x100 < 0)
    {
        // 先写负号，再用 -(value + 1) + 1 取绝对值，避免 INT32_MIN 直接取负溢出。
        payload_write_char(writer, '-');
        magnitude = (uint32_t)(-(value_x100 + 1)) + 1U;
    }
    else
    {
        magnitude = (uint32_t)value_x100;
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
        payload_write_char(writer, digits[index - 1U]);
    }

    payload_write_char(writer, '.');

    // 小数部分固定两位、不足补零。
    payload_write_char(writer, (char)('0' + (char)(fraction / 10U)));
    payload_write_char(writer, (char)('0' + (char)(fraction % 10U)));
}

uint16_t photoelectric_tcp_build_payload(
    uint16_t detected,
    int32_t encoder_rpm_x100,
    int32_t angle_degrees_x100,
    photoelectric_octet_t *payload,
    uint16_t payload_capacity)
{
    payload_writer_t writer;

    if (payload == 0)
    {
        return 0U;
    }

    writer.buffer = payload;
    writer.capacity = payload_capacity;
    writer.length = 0U;
    writer.overflow = 0U;

    payload_write_text(&writer, "enc_rpm=");
    payload_write_fixed_x100(&writer, encoder_rpm_x100);

    payload_write_text(&writer, ",obj=");
    payload_write_char(&writer, (detected != 0U) ? '1' : '0');

    payload_write_text(&writer, ",angle=");
    payload_write_fixed_x100(&writer, angle_degrees_x100);

    payload_write_text(&writer, "\r\n");

    return (writer.overflow != 0U) ? 0U : writer.length;
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
