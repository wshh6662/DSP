#ifndef MODBUS_TCP_SERVER_H
#define MODBUS_TCP_SERVER_H

#include <stdint.h>

// W5500 recv/send 按 8 位字节访问；C2000 编译器不提供 uint8_t 时使用 unsigned char。
typedef unsigned char modbus_octet_t;

#ifdef __cplusplus
extern "C" {
#endif

// Modbus TCP 最大 ADU：6 字节 MBAP + 253 字节 PDU + 1 字节单元标识。
// HMI 一次读写 120 个 word 时帧长约 252 字节，原值 64 会直接截断。
#define MODBUS_TCP_MAX_ADU_LENGTH       260U

// 提供给 HMI 的通用寄存器映射，全部使用静态数组，不使用 malloc。
// EasyBuilder 的 4x-1 对应协议地址 0，0x-1 对应协议线圈地址 0。
#define MODBUS_HOLDING_REGISTER_COUNT   2048U
#define MODBUS_COIL_COUNT               2048U

// 检测数量按 32 位发布：EasyBuilder 4x-5 读低字（协议地址 4），4x-6 读高字（地址 5）。
#define MODBUS_DETECTION_LOW_ADDRESS    4U
#define MODBUS_DETECTION_HIGH_ADDRESS   5U

// 本服务器支持的功能码。
#define MODBUS_FC_READ_COILS                0x01U
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03U
#define MODBUS_FC_WRITE_SINGLE_COIL         0x05U
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06U
#define MODBUS_FC_WRITE_MULTIPLE_COILS      0x0FU
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10U

// Modbus 异常码。
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION   0x01U
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS    0x02U
#define MODBUS_EXCEPTION_ILLEGAL_VALUE      0x03U

// 处理一帧后回填给应用层的诊断信息，应用层再转存到 CCS Watch 可见的变量。
typedef struct {
    modbus_octet_t function;       // 请求中的功能码，未识别时也照原值记录
    modbus_octet_t exception_code; // 0 表示正常响应
    uint16_t write_address;    // 仅 FC05/06/0F/10 有效
    uint16_t write_quantity;   // 仅 FC05/06/0F/10 有效
} modbus_tcp_result_t;

// 把 HMI 可见的寄存器映射（保持寄存器 + 线圈）清零。
// modbus_data 段被链接到 RAMGS0，不在 C 运行时自动清零的范围内，
// 必须在 main() 里、使用寄存器映射之前调用一次，否则首次上电会读到随机值。
void ModbusTcp_ResetStorage(void);

// 处理一帧完整的 Modbus TCP 请求并生成响应。
//
// request            指向完整请求帧的首字节
// request_length     请求帧字节数，必须 >= 6 + MBAP 中声明的长度
// detection_count    应用层当前检测数量，读 4x-5 / 4x-6 时动态取值
// response           响应缓冲区
// response_capacity  响应缓冲区容量，建议传 MODBUS_TCP_MAX_ADU_LENGTH
// result             可传 0；非 0 时回填诊断信息
//
// 返回响应字节数。返回 0 表示该帧必须整体丢弃（MBAP 无法解析，例如协议标识非 0、
// 长度字段超出已收字节数）。格式合法但功能码不支持、地址或取值非法的请求一律返回
// 非 0 的 Modbus 异常响应，不会静默丢弃，也不会导致关闭 TCP 连接。
uint16_t ModbusTcp_ProcessRequest(const modbus_octet_t *request,
                                  uint16_t request_length,
                                  uint32_t detection_count,
                                  modbus_octet_t *response,
                                  uint16_t response_capacity,
                                  modbus_tcp_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
