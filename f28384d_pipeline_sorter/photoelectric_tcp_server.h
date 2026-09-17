#ifndef PHOTOELECTRIC_TCP_SERVER_H
#define PHOTOELECTRIC_TCP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTOELECTRIC_TCP_SOCKET             0U
#define PHOTOELECTRIC_TCP_PORT               2000U
#define PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS  5U

// CCS Watch 诊断变量：socket状态、网络初始化和累计发送帧数。
extern volatile uint8_t g_photoelectric_socket_status;
extern volatile int32_t g_photoelectric_last_send_result;
extern volatile uint32_t g_photoelectric_tcp_poll_count;
extern volatile uint32_t g_photoelectric_tcp_send_count;
extern volatile int32_t g_photoelectric_network_init_result;
extern volatile uint16_t g_photoelectric_w5500_version;

// 初始化 W5500，配置 DSP 地址为 192.168.1.20。
int photoelectric_tcp_server_init(void);

// 维护非阻塞 TCP Server，每 5 次 10 ms 轮询发送一行
// "speed=xx.xx,photo=n\r\n"；转速与光电状态合并到同一行，避免两个模块抢占 socket 0。
// speed_rpm_x100 由调用方在主循环里先取好快照，本模块不直接读取正在变化的 ISR 变量。
void photoelectric_tcp_server_poll(uint16_t detected, int32_t speed_rpm_x100);

#ifdef __cplusplus
}
#endif

#endif
