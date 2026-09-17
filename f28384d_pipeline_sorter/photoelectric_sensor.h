#ifndef PHOTOELECTRIC_SENSOR_H
#define PHOTOELECTRIC_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 板卡 IN0 经隔离输入电路连接到 CPU1 GPIO26。
#define PHOTOELECTRIC_SENSOR_GPIO    26U

// CCS Watch 诊断变量：电平、检测状态、最近边沿状态和累计次数。
extern volatile uint16_t g_photoelectric_raw_level;
extern volatile uint16_t g_photoelectric_detected;
extern volatile uint16_t g_photoelectric_falling_edge; // 最近一次边沿为下降沿时保持 1
extern volatile uint16_t g_photoelectric_rising_edge;  // 最近一次边沿为上升沿时保持 1
extern volatile uint32_t g_photoelectric_enter_count;
extern volatile uint32_t g_photoelectric_leave_count;

// 配置 IN0 为带上拉、同步采样的普通 GPIO 输入。
void photoelectric_sensor_init(void);

// 高到低表示物品进入并置 1；低到高表示物品离开并归 0。
void photoelectric_sensor_update(void);

#ifdef __cplusplus
}
#endif

#endif
