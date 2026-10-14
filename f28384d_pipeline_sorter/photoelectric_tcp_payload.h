#ifndef PHOTOELECTRIC_TCP_PAYLOAD_H
#define PHOTOELECTRIC_TCP_PAYLOAD_H

#include <stdint.h>

#if defined(__TMS320C2000__)
typedef uint16_t photoelectric_octet_t;
#else
typedef uint8_t photoelectric_octet_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 两个 int32_t 定点参数加光电状态的最长报文为 49 个 octet，预留容量 56。
#define PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY   56U

// 生成一行 ASCII 状态："enc_rpm=29.29,angle=359.91,photo=1\r\n"
//   encoder_rpm_x100  ：编码器 3 的 M 法转速，单位 0.01 rpm
//   angle_degrees_x100：8000 计数/圈换算的单圈角度，单位 0.01 度，范围 0～35995
//   detected          ：光电状态，1 表示检测到瓶子，0 表示离开
// 编码器转速和角度都由定点整数手工换算成 ASCII，不依赖 sprintf 的浮点格式化。
// 容量不足时返回 0，此时缓冲区内容无意义，调用方应当丢弃。
uint16_t photoelectric_tcp_build_payload(
    int32_t encoder_rpm_x100,
    int32_t angle_degrees_x100,
    uint16_t detected,
    photoelectric_octet_t *payload,
    uint16_t payload_capacity);

// 判断轮询次数是否达到发送周期；周期为 0 时不发送。
uint16_t photoelectric_tcp_is_send_due(uint16_t elapsed_ticks,
                                       uint16_t period_ticks);

#ifdef __cplusplus
}
#endif

#endif
