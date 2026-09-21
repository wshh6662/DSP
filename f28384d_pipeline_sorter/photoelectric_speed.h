#ifndef PHOTOELECTRIC_SPEED_H
#define PHOTOELECTRIC_SPEED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 时基：Timer0 每 1 ms 触发一次，所以一个 tick 就是 1 ms。
// 必须与 turntable_speed.h 的 TURNTABLE_TIMER_TICK_FREQUENCY_HZ（1000 Hz）一致，
// turntable_speed.c 里有编译期检查，两边改动不同步会直接编译报错。
#define PHOTOELECTRIC_TIMEBASE_TICK_MS             1U

// 转盘几何与瓶子的有效遮挡宽度：单瓶遮挡时间由这两个量和线速度决定。
#define TURNTABLE_RADIUS_MM                        120L
#define PHOTOELECTRIC_BOTTLE_EFFECTIVE_WIDTH_MM    35L

// 圆周长，单位 mm × 10000：2 × π(3.1416) × 半径。
// 半径 120 mm -> 7539840，即 753.9840 mm。
#define PHOTOELECTRIC_CIRCUMFERENCE_MM_X10000 \
    (2L * 31416L * TURNTABLE_RADIUS_MM)

// 一次遮挡短于 20 ms 视为毛刺，整次测量丢弃，不覆盖上一次有效结果。
#define PHOTOELECTRIC_MIN_BLOCK_TIME_MS            20U
// 下降沿之后超过 1000 ms 仍没有上升沿，判定本次测量超时并取消。
#define PHOTOELECTRIC_BLOCK_TIMEOUT_MS             1000U

// CCS Watch 变量，全部在 photoelectric_speed.c 顶部定义。
extern volatile uint32_t g_photoelectric_speed_tick_ms;            // 1 ms 时基计数
extern volatile uint32_t g_photoelectric_enter_tick_ms;            // 本次遮挡下降沿的时刻
extern volatile uint32_t g_photoelectric_block_time_ms;            // 最近一次有效遮挡时间
extern volatile int32_t  g_photoelectric_speed_rpm_x100;           // 光电转速，单位 0.01 rpm
extern volatile float    g_photoelectric_speed_rpm;                // 便于 CCS Watch 观察的浮点 rpm
extern volatile float    g_photoelectric_linear_speed_cm_s;        // 转盘线速度，单位 cm/s
extern volatile uint16_t g_photoelectric_speed_valid;              // 1 = 最近一次测量有效
extern volatile uint16_t g_photoelectric_measurement_in_progress;  // 1 = 正在测一次遮挡

// 清零全部测速状态。必须在 Timer0 中断启动之前调用。
void photoelectric_speed_init(void);

// 1 ms 时基推进：由 turntable_speed.c 的 Timer0 中断每个周期调用一次。
// enter_count / leave_count 取自 photoelectric_sensor 的累计边沿计数，
// 只有计数发生变化才算发生了一次边沿，一次「下降沿 + 后续上升沿」才算一次测量。
void photoelectric_speed_update(uint32_t enter_count, uint32_t leave_count);

// 供 TCP "re" 命令调用：清掉开始时刻、速度和有效标志。
// 刻意不动 enter/leave 的"上次看到"计数，否则复位后的第一次 update
// 会把旧的累计值误当成一次新边沿。
void photoelectric_speed_reset(void);

// 读取光电转速快照，单位 0.01 rpm。
// 内部在极短临界区内复制 32 位数值，避免读到半新半旧的值。
int32_t photoelectric_speed_get_rpm_x100(void);

// 遮挡时间 -> 0.01 rpm 定点转速：
//   (6000000 × 宽度 × 10000) / (周长_x10000 × 遮挡时间)
int32_t photoelectric_speed_rpm_x100_from_block_time(uint32_t block_time_ms);

// 遮挡时间 -> 0.01 cm/s 定点线速度：(10000 × 宽度) / 遮挡时间
int32_t photoelectric_speed_linear_cm_s_x100_from_block_time(
    uint32_t block_time_ms);

#ifdef __cplusplus
}
#endif

#endif
