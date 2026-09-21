//#############################################################################
// FILE:   photoelectric_speed.c
// TITLE:  F28384D 光电单瓶遮挡时间测速
//#############################################################################
//
// 原理：
//   1. Timer0 每 1 ms 调用一次 photoelectric_speed_update()，得到 1 ms 分辨率的时基；
//   2. 瓶子进入光电区 -> GPIO 下降沿：记下 enter_tick_ms，开始一次测量；
//   3. 瓶子离开光电区 -> GPIO 上升沿：遮挡时间 = 当前 tick - enter_tick_ms；
//   4. 由「瓶子有效遮挡宽度 / 遮挡时间」得到线速度，再结合转盘周长得到转速。
//
// 一次测量必须是一对「下降沿 + 后续上升沿」：
//   - 孤立上升沿（前面没有配对的下降沿）不产生速度；
//   - 下降沿之后 1000 ms 内没有上升沿，本次测量超时取消；
//   - 短于 20 ms 的遮挡按毛刺丢弃，不覆盖上一次有效结果。
//
// 公式（用 int64 中间量，因为 6000000 × 35 × 10000 会超出 32 位）：
//   optical_rpm_x100 = (6000000 × width_mm × 10000) /
//                      (circumference_mm_x10000 × block_time_ms)
//   linear_cm_s_x100 = (10000 × width_mm) / block_time_ms
//
// 标称值：半径 120 mm、遮挡宽度 35 mm、遮挡 174 ms
//   -> circumference_mm_x10000 = 7539840（即 753.9840 mm）
//   -> optical_rpm_x100 = 1600（16.00 rpm）、linear_cm_s_x100 = 2011（20.11 cm/s）
//#############################################################################

#include "driverlib.h"
#include "photoelectric_speed.h"

// ---- CCS Watch 变量 ----
volatile uint32_t g_photoelectric_speed_tick_ms = 0U;            // 1 ms 时基计数
volatile uint32_t g_photoelectric_enter_tick_ms = 0U;            // 本次遮挡下降沿的时刻
volatile uint32_t g_photoelectric_block_time_ms = 0U;            // 最近一次有效遮挡时间
volatile int32_t  g_photoelectric_speed_rpm_x100 = 0;            // 光电转速，单位 0.01 rpm
volatile float    g_photoelectric_speed_rpm = 0.0f;              // 便于 CCS Watch 观察的浮点 rpm
volatile float    g_photoelectric_linear_speed_cm_s = 0.0f;      // 转盘线速度，单位 cm/s
volatile uint16_t g_photoelectric_speed_valid = 0U;              // 1 = 最近一次测量有效
volatile uint16_t g_photoelectric_measurement_in_progress = 0U;  // 1 = 正在测一次遮挡

// 上一次看到的累计边沿计数，用来判断本次 update 有没有出现新边沿。
static uint32_t photoelectric_last_enter_count = 0U;
static uint32_t photoelectric_last_leave_count = 0U;

int32_t photoelectric_speed_rpm_x100_from_block_time(uint32_t block_time_ms)
{
    int64_t numerator;
    int64_t denominator;

    if (block_time_ms == 0U)
    {
        return 0;
    }

    numerator = (int64_t)6000000 *
                (int64_t)PHOTOELECTRIC_BOTTLE_EFFECTIVE_WIDTH_MM *
                (int64_t)10000;
    denominator = (int64_t)PHOTOELECTRIC_CIRCUMFERENCE_MM_X10000 *
                  (int64_t)block_time_ms;

    return (int32_t)(numerator / denominator);
}

int32_t photoelectric_speed_linear_cm_s_x100_from_block_time(
    uint32_t block_time_ms)
{
    if (block_time_ms == 0U)
    {
        return 0;
    }

    return (int32_t)(((int64_t)10000 *
                      (int64_t)PHOTOELECTRIC_BOTTLE_EFFECTIVE_WIDTH_MM) /
                     (int64_t)block_time_ms);
}

void photoelectric_speed_reset(void)
{
    g_photoelectric_speed_tick_ms = 0U;
    g_photoelectric_enter_tick_ms = 0U;
    g_photoelectric_block_time_ms = 0U;
    g_photoelectric_speed_rpm_x100 = 0;
    g_photoelectric_speed_rpm = 0.0f;
    g_photoelectric_linear_speed_cm_s = 0.0f;
    g_photoelectric_speed_valid = 0U;
    g_photoelectric_measurement_in_progress = 0U;
}

void photoelectric_speed_init(void)
{
    // 上电时边沿计数本来就是 0，这里把"上次看到"的基准一并归零。
    photoelectric_last_enter_count = 0U;
    photoelectric_last_leave_count = 0U;

    photoelectric_speed_reset();
}

void photoelectric_speed_update(uint32_t enter_count, uint32_t leave_count)
{
    uint32_t block_time_ms;
    int32_t linear_speed_cm_s_x100;

    g_photoelectric_speed_tick_ms++;

    if (enter_count != photoelectric_last_enter_count)
    {
        // 下降沿：记下本次遮挡的开始时刻并开始测量。
        // 若上一次测量还没等到上升沿，就以这次的下降沿为准重新计时。
        photoelectric_last_enter_count = enter_count;
        g_photoelectric_enter_tick_ms = g_photoelectric_speed_tick_ms;
        g_photoelectric_measurement_in_progress = 1U;
    }

    if (leave_count != photoelectric_last_leave_count)
    {
        photoelectric_last_leave_count = leave_count;

        // 只有配过对的下降沿才构成一次测量；孤立上升沿直接忽略。
        if (g_photoelectric_measurement_in_progress != 0U)
        {
            block_time_ms = g_photoelectric_speed_tick_ms -
                            g_photoelectric_enter_tick_ms;
            g_photoelectric_measurement_in_progress = 0U;

            if (block_time_ms >= PHOTOELECTRIC_MIN_BLOCK_TIME_MS)
            {
                g_photoelectric_block_time_ms = block_time_ms;
                g_photoelectric_speed_rpm_x100 =
                    photoelectric_speed_rpm_x100_from_block_time(block_time_ms);
                g_photoelectric_speed_rpm =
                    (float)g_photoelectric_speed_rpm_x100 * 0.01f;

                linear_speed_cm_s_x100 =
                    photoelectric_speed_linear_cm_s_x100_from_block_time(
                        block_time_ms);
                g_photoelectric_linear_speed_cm_s =
                    (float)linear_speed_cm_s_x100 * 0.01f;

                g_photoelectric_speed_valid = 1U;
            }
            // 短于 20 ms 的遮挡按毛刺丢弃：上一次有效结果原样保留。
        }
    }

    // 下降沿之后一直没等到上升沿，超时取消本次测量。
    if ((g_photoelectric_measurement_in_progress != 0U) &&
        ((g_photoelectric_speed_tick_ms - g_photoelectric_enter_tick_ms) >
         PHOTOELECTRIC_BLOCK_TIMEOUT_MS))
    {
        g_photoelectric_measurement_in_progress = 0U;
        g_photoelectric_speed_valid = 0U;
    }
}

int32_t photoelectric_speed_get_rpm_x100(void)
{
    int32_t snapshot;
    bool interrupts_were_enabled;

    interrupts_were_enabled = Interrupt_disableGlobal();
    snapshot = g_photoelectric_speed_rpm_x100;
    if (interrupts_were_enabled == false)
    {
        (void)Interrupt_enableGlobal();
    }

    return snapshot;
}
