#ifndef ENCODER_TCP_PAYLOAD_H
#define ENCODER_TCP_PAYLOAD_H

#include <stdint.h>

#if defined(__TMS320C2000__)
typedef uint16_t encoder_octet_t;
#else
typedef uint8_t encoder_octet_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ENCODER_TCP_MAX_PAYLOAD_LENGTH  32U

// 作用：把编码器计数转换为电脑容易查看的 ASCII 行，例如 encoder=1234\r\n。
// 用法：传入输出缓冲区及容量；成功返回有效字节数，容量不足返回 0。
uint16_t EncoderTcp_BuildPayload(uint32_t position,
                                 encoder_octet_t *payload,
                                 uint16_t payload_capacity);

// 作用：生成带原始 A/B/Z 电平的诊断报文，例如 encoder=0,A=0,B=0,Z=0\r\n。
// 用法：input_a/input_b/input_z 非零时输出 1，零时输出 0。
uint16_t EncoderTcp_BuildDiagnosticPayload(uint32_t position,
                                           uint16_t input_a,
                                           uint16_t input_b,
                                           uint16_t input_z,
                                           encoder_octet_t *payload,
                                           uint16_t payload_capacity);

// 作用：判断 10 ms 主循环是否到了发送周期。
// 用法：elapsed_ticks 达到 period_ticks 时返回 1；周期为 0 时始终不发送。
uint16_t EncoderTcp_IsSendDue(uint16_t elapsed_ticks,
                              uint16_t period_ticks);

#ifdef __cplusplus
}
#endif

#endif
