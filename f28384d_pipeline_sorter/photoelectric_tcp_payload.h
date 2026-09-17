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

// 单行报文固定为 "speed=" + 可选负号 + 整数部分 + '.' + 两位小数 + ",photo=" + 0/1 + CRLF。
// 最坏情况（含 INT32_MIN 的 10 位整数）也只有 28 个 octet，这里给 W5500 发送缓冲区留 48 个。
#define PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY   48U

// 生成一行 ASCII 状态："speed=29.29,photo=1\r\n"。
// speed 为带方向的转盘转速，单位 rpm：正数正转、负数反转，由 0.01 rpm 定点数手工换算，
// 不依赖 sprintf 的浮点格式化；photo 为光电传感器状态，1 = 检测到物品，0 = 无物品。
// 容量不足时返回 0，此时缓冲区内容无意义，调用方应当丢弃。
uint16_t photoelectric_tcp_build_payload(
    uint16_t detected,
    int32_t speed_rpm_x100,
    photoelectric_octet_t *payload,
    uint16_t payload_capacity);

// 判断轮询次数是否达到发送周期；周期为 0 时不发送。
uint16_t photoelectric_tcp_is_send_due(uint16_t elapsed_ticks,
                                       uint16_t period_ticks);

#ifdef __cplusplus
}
#endif

#endif
