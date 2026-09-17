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

#define PHOTOELECTRIC_TCP_PAYLOAD_LENGTH    3U

// 生成一行 ASCII 状态：无遮挡为 "0\r\n"，检测到物体为 "1\r\n"。
uint16_t photoelectric_tcp_build_payload(
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
