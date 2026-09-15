#include "encoder_tcp_payload.h"

// 作用：生成固定格式的编码器 TCP 文本，不依赖 printf，减少嵌入式程序开销。
// 用法：发送时只发送函数返回的有效长度，不发送缓冲区剩余内容。
uint16_t EncoderTcp_BuildPayload(uint32_t position,
                                 encoder_octet_t *payload,
                                 uint16_t payload_capacity)
{
    static const encoder_octet_t prefix[] = {
        'e', 'n', 'c', 'o', 'd', 'e', 'r', '='
    };
    encoder_octet_t reversed_digits[10];
    uint16_t digit_count = 0U;
    uint16_t payload_length;
    uint16_t index;

    // 至少生成一个数字，因此位置为 0 时也会输出字符 '0'。
    do
    {
        reversed_digits[digit_count] =
            (encoder_octet_t)('0' + (position % 10U));
        digit_count++;
        position /= 10U;
    }
    while (position != 0U);

    payload_length = (uint16_t)(sizeof(prefix) / sizeof(prefix[0]));
    payload_length = (uint16_t)(payload_length + digit_count + 2U);
    if ((payload == 0) || (payload_capacity < payload_length))
    {
        return 0U;
    }

    // 先复制 "encoder="，再把临时保存的倒序数字反向写入输出缓冲区。
    for (index = 0U; index < (uint16_t)(sizeof(prefix) / sizeof(prefix[0]));
         index++)
    {
        payload[index] = prefix[index];
    }
    for (index = 0U; index < digit_count; index++)
    {
        payload[8U + index] = reversed_digits[digit_count - 1U - index];
    }

    // 每条数据以 CRLF 结束，方便网络调试助手按行显示。
    payload[8U + digit_count] = '\r';
    payload[9U + digit_count] = '\n';
    return payload_length;
}

// 作用：在编码器位置后附加 A/B/Z 原始输入电平，帮助区分 GPIO 与 eQEP 故障。
// 用法：成功返回有效长度；缓冲区不足或为空时返回 0。
uint16_t EncoderTcp_BuildDiagnosticPayload(uint32_t position,
                                           uint16_t input_a,
                                           uint16_t input_b,
                                           uint16_t input_z,
                                           encoder_octet_t *payload,
                                           uint16_t payload_capacity)
{
    uint16_t payload_length;

    if ((payload == 0) || (payload_capacity < 23U))
    {
        return 0U;
    }

    payload_length = EncoderTcp_BuildPayload(position, payload,
                                              payload_capacity);
    if ((payload_length == 0U) ||
        ((uint16_t)(payload_length + 12U) > payload_capacity))
    {
        return 0U;
    }

    // 覆盖基础报文末尾的 CRLF，再写入固定顺序的诊断字段。
    payload_length = (uint16_t)(payload_length - 2U);
    payload[payload_length++] = ',';
    payload[payload_length++] = 'A';
    payload[payload_length++] = '=';
    payload[payload_length++] = (input_a != 0U) ? '1' : '0';
    payload[payload_length++] = ',';
    payload[payload_length++] = 'B';
    payload[payload_length++] = '=';
    payload[payload_length++] = (input_b != 0U) ? '1' : '0';
    payload[payload_length++] = ',';
    payload[payload_length++] = 'Z';
    payload[payload_length++] = '=';
    payload[payload_length++] = (input_z != 0U) ? '1' : '0';
    payload[payload_length++] = '\r';
    payload[payload_length++] = '\n';

    return payload_length;
}

uint16_t EncoderTcp_IsSendDue(uint16_t elapsed_ticks,
                              uint16_t period_ticks)
{
    if ((period_ticks != 0U) && (elapsed_ticks >= period_ticks))
    {
        return 1U;
    }

    return 0U;
}
