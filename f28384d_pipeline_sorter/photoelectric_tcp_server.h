#ifndef PHOTOELECTRIC_TCP_SERVER_H
#define PHOTOELECTRIC_TCP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 主循环约 1 ms 轮询一次，1000 次对应约 1 秒的遥测发送间隔。
#define PHOTOELECTRIC_TCP_SOCKET             0U
#define PHOTOELECTRIC_TCP_PORT               2000U
#define PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS  1000U

// CCS Watch 诊断变量：socket状态、网络初始化和累计发送帧数。
extern volatile uint8_t g_photoelectric_socket_status;
extern volatile int32_t g_photoelectric_last_send_result;
extern volatile uint32_t g_photoelectric_tcp_poll_count;
extern volatile uint32_t g_photoelectric_tcp_send_count;
extern volatile int32_t g_photoelectric_network_init_result;
extern volatile uint16_t g_photoelectric_w5500_version;
extern volatile int32_t g_photoelectric_last_receive_result;
extern volatile uint32_t g_photoelectric_reset_command_count;

// 初始化 W5500，配置 DSP 地址为 192.168.1.60。
int photoelectric_tcp_server_init(void);

// 维护非阻塞 TCP Server，每 1000 次主循环轮询（约 1 秒）发送一行
// "enc_rpm=...,angle=...,photo=...\r\n"，并接收 "re"。
// 完整识别到一次 re 返回 1，其余情况返回 0。
// 同一连接还接收电脑2发来的 tcmc1,/tcmc2, 相机检测结果。
// detected 为光电状态快照；连接期间的变化立即发送同格式的完整状态行。
// 重连不重放旧事件，首次采样仅建立基准；队列满时关闭连接。
// 所有测量值由调用方在主循环里先取快照，本模块不读取 Timer0 ISR 变量。
int32_t photoelectric_tcp_server_poll(int32_t encoder_rpm_x100,
                                      int32_t angle_degrees_x100,
                                      uint16_t detected);

#ifdef __cplusplus
}
#endif

#endif
