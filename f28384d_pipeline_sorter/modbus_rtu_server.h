#ifndef MODBUS_RTU_SERVER_H
#define MODBUS_RTU_SERVER_H

#include <stdint.h>

#include "modbus_tcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_RTU_MAX_ADU_LENGTH 256U

// 计算 Modbus RTU 使用的 CRC16。返回值低字节先发送、高字节后发送。
uint16_t ModbusRtu_CalculateCrc(const modbus_octet_t *data,
                                uint16_t length);

// 校验并处理一帧完整的 Modbus RTU 请求。
// 返回 0 表示地址不匹配、CRC 错误、帧不完整或广播请求无需响应。
uint16_t ModbusRtu_ProcessRequest(const modbus_octet_t *request,
                                  uint16_t request_length,
                                  uint16_t slave_address,
                                  uint32_t detection_count,
                                  modbus_octet_t *response,
                                  uint16_t response_capacity,
                                  modbus_tcp_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
