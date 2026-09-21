#ifndef PHOTOELECTRIC_TCP_SERVER_H
#define PHOTOELECTRIC_TCP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTOELECTRIC_TCP_SOCKET             0U
#define PHOTOELECTRIC_TCP_PORT               2000U
#define PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS  100U

// CCS Watch 诊断变量：socket状态、网络初始化和累计发送帧数。
extern volatile uint8_t g_photoelectric_socket_status;
extern volatile int32_t g_photoelectric_last_send_result;
extern volatile uint32_t g_photoelectric_tcp_poll_count;
extern volatile uint32_t g_photoelectric_tcp_send_count;
extern volatile int32_t g_photoelectric_network_init_result;
extern volatile uint16_t g_photoelectric_w5500_version;
extern volatile int32_t g_photoelectric_last_receive_result;
extern volatile uint32_t g_photoelectric_reset_command_count;

// 初始化 W5500，配置 DSP 地址为 192.168.1.20。
int photoelectric_tcp_server_init(void);

// 维护非阻塞 TCP Server，每 100 次主循环轮询（约 1 秒）发送一行
// "enc_rpm=xx.xx,obj=n,angle=xxx.xx\r\n"，并接收 ASCII "re" 复位命令。
// 完整识别到一次 re 返回 1，其余情况返回 0。
// 三个测量值由调用方在主循环里先取好快照，本模块不直接读取正在被 Timer0 中断更新的变量。
int32_t photoelectric_tcp_server_poll(uint16_t detected,
                                      int32_t encoder_rpm_x100,
                                      int32_t angle_degrees_x100);

#ifdef __cplusplus
}
#endif

#endif
