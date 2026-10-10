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
extern volatile uint32_t g_photoelectric_count_reset_count;
extern volatile uint32_t g_photoelectric_blocked_ms;        // 上一瓶遮挡光束的时间
extern volatile int32_t g_photoelectric_speed_rpm_x100;     // 按单瓶遮挡时间估算的转台速度

// 配置 IN0 为带上拉、同步采样的普通 GPIO 输入。
void photoelectric_sensor_init(void);

// 高到低表示物品进入并置 1；低到高表示物品离开并归 0。
// 仅在本次采样出现进入沿时返回 1，供相机触发模块记录该瓶位置。
uint16_t photoelectric_sensor_update(void);

// 请求在下一次 1 ms 采样中断内清零累计次数，避免主循环与中断竞争。
void photoelectric_sensor_request_count_reset(void);

// 在同一短临界区取得 Timer0 更新的状态、光电速度和入瓶累计数。
void photoelectric_sensor_get_snapshot(uint16_t *detected,
                                       int32_t *speed_rpm_x100,
                                       uint32_t *bottle_count);

#ifdef __cplusplus
}
#endif

#endif
