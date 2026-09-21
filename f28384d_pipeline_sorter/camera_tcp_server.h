//#############################################################################
// FILE:   camera_tcp_server.h
// TITLE:  相机检测结果 TCP 接收接口
//#############################################################################

#ifndef CAMERA_TCP_SERVER_H
#define CAMERA_TCP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Socket 0 的 2000 端口已用于电脑遥测；相机独占 Socket 1 的 2001 端口。
#define CAMERA_TCP_SOCKET             1U
#define CAMERA_TCP_PORT               2001U

// 相机检测结果约定：0 = 未收到结果，1 = 良品，2 = 不良品。
#define CAMERA_RESULT_NONE            0U
#define CAMERA_RESULT_GOOD            1U
#define CAMERA_RESULT_BAD             2U

// CCS Watch 变量，全部在 camera_tcp_server.c 顶部定义。
extern volatile uint8_t  g_camera_socket_status;          // Socket 1 当前状态，23表示已连接。
extern volatile uint16_t g_camera_result;                 // 最近一次有效相机结果：0=无、1=良品、2=不良品。
extern volatile uint32_t g_camera_result_count;           // 收到有效相机结果的累计次数。
extern volatile uint32_t g_camera_invalid_result_count;   // 收到非空白且不是1/2的字符次数。
extern volatile int32_t  g_camera_last_receive_result;    // 最近一次 recv() 的返回值。

// 清零相机结果诊断变量；不初始化 W5500，W5500 仍由 Socket 0 模块统一初始化。
void camera_tcp_server_init(void);

// 维护 Socket 1 的非阻塞 TCP Server，并接收 ASCII '1'（良品）或 '2'（不良品）。
// 可在主循环中持续调用；CR、LF、空格和制表符会被忽略。
void camera_tcp_server_poll(void);

#ifdef __cplusplus
}
#endif

#endif
