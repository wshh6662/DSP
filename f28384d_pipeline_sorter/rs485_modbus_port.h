#ifndef RS485_MODBUS_PORT_H
#define RS485_MODBUS_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 显示器 TESTMACRO.exob 的 COM2 参数：115200, 8 data bits, no parity, 1 stop bit。
#define RS485_MODBUS_BAUD_RATE          115200U
#define RS485_MODBUS_SLAVE_ADDRESS      1U

// 新版板卡 RS485：SCI2/SCIB 使用 GPIO54(TX)、GPIO55(RX)，GPIO133 控制收发方向。
#define RS485_MODBUS_DIRECTION_PIN      133U
#define RS485_MODBUS_TX_ENABLE_LEVEL    1U
#define RS485_MODBUS_RX_ENABLE_LEVEL    0U

// 主循环约 1 ms 调用一次；连续两次未收到新字节即认为一帧已经结束。
#define RS485_MODBUS_FRAME_GAP_POLLS    2U

// 地址表中的一次性命令线圈（Modbus 协议地址从 0 开始）。
#define RS485_MODBUS_COUNT_RESET_COIL_ADDRESS  82U
#define RS485_MODBUS_QUEUE_RESET_COIL_ADDRESS  86U
#define RS485_MODBUS_ALARM_CLEAR_COIL_ADDRESS  87U

// 初始化 SCIB 和板载 RS485 收发器，初始化完成后默认处于接收状态。
void rs485_modbus_port_init(void);

// 从 SCI FIFO 收集请求；帧结束后处理 Modbus RTU 并通过同一接口回复。
void rs485_modbus_port_poll(uint32_t detection_count);

// 读取并消费 HMI 计数清零请求；每个 0x-83 写 ON 请求只返回一次 1。
uint16_t rs485_modbus_port_take_count_reset_request(void);

#ifdef __cplusplus
}
#endif

#endif
