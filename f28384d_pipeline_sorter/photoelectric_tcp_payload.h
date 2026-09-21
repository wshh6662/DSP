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

// 单行报文：
//   "enc_rpm=" + 编码器转速 + ",opt_rpm=" + 光电转速 + ",obj=" + 0/1
//   + ",angle=" + 当前角度 + CRLF
// 两段转速都是带符号的 0.01 rpm 定点数，每段最多 "-21474836.48"（12 个 octet），
// 角度固定 0.00~359.91（最多 6 个 octet），所以最坏情况整行 62 个 octet；这里留到 80。
#define PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY   80U

// 生成一行 ASCII 状态："enc_rpm=29.29,opt_rpm=16.00,obj=1,angle=359.91\r\n"
//   encoder_rpm_x100  ：编码器 3 的 M 法转速，单位 0.01 rpm
//   optical_rpm_x100  ：光电单瓶遮挡时间测速转速，单位 0.01 rpm；
//                       还没有测到完整遮挡（或测量已超时）时为 0
//   detected          ：光电传感器当前状态，1 = 检测到物品，0 = 无物品
//   angle_degrees_x100：编码器当前单圈角度，单位 0.01 度，范围 0～35991
// 两段转速和角度都由定点整数手工换算成 ASCII，不依赖 sprintf 的浮点格式化。
// 容量不足时返回 0，此时缓冲区内容无意义，调用方应当丢弃。
uint16_t photoelectric_tcp_build_payload(
    uint16_t detected,
    int32_t encoder_rpm_x100,
    int32_t optical_rpm_x100,
    int32_t angle_degrees_x100,
    photoelectric_octet_t *payload,
    uint16_t payload_capacity);

// 判断轮询次数是否达到发送周期；周期为 0 时不发送。
uint16_t photoelectric_tcp_is_send_due(uint16_t elapsed_ticks,
                                       uint16_t period_ticks);

#ifdef __cplusplus
}
#endif

#endif
