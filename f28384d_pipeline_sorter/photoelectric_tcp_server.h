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

// 维护非阻塞 TCP Server，每 5 次 10 ms 轮询发送一次检测状态。
void photoelectric_tcp_server_poll(uint16_t detected);

#ifdef __cplusplus
}
#endif

#endif
