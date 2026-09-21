//#############################################################################
// FILE:   test_photoelectric_speed.c
// TITLE:  光电单瓶遮挡时间测速主机侧测试（gcc 直接编译，不需要 C2000 工具链）
//#############################################################################
//
// 一次测量 = 一对「下降沿 + 后续上升沿」：
//   下降沿记下 enter_tick_ms，上升沿用 (当前 tick - enter_tick_ms) 得到遮挡时间。
//   1 ms 时基由 Timer0 提供，主循环不参与计时。
// 标称几何：半径 120 mm、瓶子有效遮挡宽度 35 mm、遮挡 174 ms。
//#############################################################################

#include "photoelectric_speed.h"
#include "driverlib.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t stub_disable_global_count = 0U;
static uint32_t stub_enable_global_count = 0U;

// 传感器累计计数由测试直接摆布，等价于 photoelectric_sensor_update() 的结果。
static uint32_t stub_enter_count = 0U;
static uint32_t stub_leave_count = 0U;

bool Interrupt_disableGlobal(void)
{
    stub_disable_global_count++;
    return false;
}

bool Interrupt_enableGlobal(void)
{
    stub_enable_global_count++;
    return false;
}

// 一次 1 ms 时基推进。
static void speed_tick(void)
{
    photoelectric_speed_update(stub_enter_count, stub_leave_count);
}

static void run_ticks(uint32_t count)
{
    uint32_t index;

    for (index = 0U; index < count; index++)
    {
        speed_tick();
    }
}

// 把计数和模块状态一起归零，开始一段独立场景。
static void start_scenario(void)
{
    stub_enter_count = 0U;
    stub_leave_count = 0U;
    photoelectric_speed_init();
}

int main(void)
{
    // ---- 几何与阈值常量 ----
    assert(TURNTABLE_RADIUS_MM == 120L);
    assert(PHOTOELECTRIC_BOTTLE_EFFECTIVE_WIDTH_MM == 35L);
    assert(PHOTOELECTRIC_CIRCUMFERENCE_MM_X10000 == 7539840L);
    assert(PHOTOELECTRIC_MIN_BLOCK_TIME_MS == 20U);
    assert(PHOTOELECTRIC_BLOCK_TIMEOUT_MS == 1000U);
    assert(PHOTOELECTRIC_TIMEBASE_TICK_MS == 1U);

    // ---- 初始化后全部归零 ----
    start_scenario();
    assert(g_photoelectric_speed_tick_ms == 0U);
    assert(g_photoelectric_enter_tick_ms == 0U);
    assert(g_photoelectric_block_time_ms == 0U);
    assert(g_photoelectric_speed_rpm_x100 == 0);
    assert(g_photoelectric_speed_rpm == 0.0f);
    assert(g_photoelectric_linear_speed_cm_s == 0.0f);
    assert(g_photoelectric_speed_valid == 0U);
    assert(g_photoelectric_measurement_in_progress == 0U);

    // ---- 测试 7：孤立上升沿（没有对应的下降沿）不算速度 ----
    // 先让时基走满 500 ms，这样即使实现漏了"必须先有下降沿"的保护，
    // 算出来的遮挡时间也不会被 20 ms 毛刺门限挡住，测试才能真正区分。
    run_ticks(500U);
    assert(g_photoelectric_speed_tick_ms == 500U);

    stub_leave_count = 1U;
    speed_tick();
    assert(g_photoelectric_speed_tick_ms == 501U);
    assert(g_photoelectric_measurement_in_progress == 0U);
    assert(g_photoelectric_block_time_ms == 0U);
    assert(g_photoelectric_speed_rpm_x100 == 0);
    assert(g_photoelectric_speed_valid == 0U);

    run_ticks(50U);
    assert(g_photoelectric_measurement_in_progress == 0U);
    assert(g_photoelectric_speed_rpm_x100 == 0);
    assert(g_photoelectric_speed_valid == 0U);

    // ---- 测试 4 + 测试 5：下降沿记起始，174 ms 后上升沿完成一次测速 ----
    start_scenario();
    run_ticks(99U);
    assert(g_photoelectric_speed_tick_ms == 99U);

    stub_enter_count = 1U;
    speed_tick();
    assert(g_photoelectric_speed_tick_ms == 100U);
    assert(g_photoelectric_enter_tick_ms == 100U);
    assert(g_photoelectric_measurement_in_progress == 1U);
    assert(g_photoelectric_speed_valid == 0U);

    // 遮挡期间没有任何新边沿，测量还在进行中。
    run_ticks(173U);
    assert(g_photoelectric_speed_tick_ms == 273U);
    assert(g_photoelectric_measurement_in_progress == 1U);
    assert(g_photoelectric_speed_valid == 0U);
    assert(g_photoelectric_block_time_ms == 0U);

    stub_leave_count = 1U;
    speed_tick();
    assert(g_photoelectric_speed_tick_ms == 274U);
    // 遮挡时间 = 274 - 100 = 174 ms。
    assert(g_photoelectric_block_time_ms == 174U);
    assert(g_photoelectric_measurement_in_progress == 0U);
    assert(g_photoelectric_speed_valid == 1U);
    // 35 mm / 174 ms 对应约 16.00 rpm 与 20.11 cm/s。
    assert(g_photoelectric_speed_rpm_x100 == 1600);
    assert(g_photoelectric_speed_rpm > 15.99f);
    assert(g_photoelectric_speed_rpm < 16.01f);
    assert(g_photoelectric_linear_speed_cm_s > 20.10f);
    assert(g_photoelectric_linear_speed_cm_s < 20.12f);

    // ---- 测试 8：小于 20 ms 的遮挡作为毛刺丢弃，不覆盖上一次有效结果 ----
    stub_enter_count = 2U;
    speed_tick();
    assert(g_photoelectric_enter_tick_ms == 275U);
    assert(g_photoelectric_measurement_in_progress == 1U);

    run_ticks(4U);
    stub_leave_count = 2U;
    speed_tick();
    assert(g_photoelectric_speed_tick_ms == 280U);
    // 遮挡时间 5 ms < 20 ms：整次测量被丢弃。
    assert(g_photoelectric_block_time_ms == 174U);
    assert(g_photoelectric_speed_rpm_x100 == 1600);
    assert(g_photoelectric_speed_valid == 1U);
    assert(g_photoelectric_measurement_in_progress == 0U);

    // 边界：正好 20 ms 的遮挡是有效的，不能被滤掉。
    stub_enter_count = 3U;
    speed_tick();
    assert(g_photoelectric_enter_tick_ms == 281U);
    run_ticks(19U);
    stub_leave_count = 3U;
    speed_tick();
    assert(g_photoelectric_block_time_ms == 20U);
    assert(g_photoelectric_speed_rpm_x100 == 13926);

    // ---- 测试 6：下降沿之后一直没有上升沿，超过 1000 ms 判定超时 ----
    stub_enter_count = 4U;
    speed_tick();
    assert(g_photoelectric_enter_tick_ms == 302U);
    assert(g_photoelectric_measurement_in_progress == 1U);

    run_ticks(1000U);
    assert(g_photoelectric_speed_tick_ms == 1302U);
    // 距起始正好 1000 ms，还没有越过门限。
    assert(g_photoelectric_measurement_in_progress == 1U);
    assert(g_photoelectric_speed_valid == 1U);

    speed_tick();
    assert(g_photoelectric_speed_tick_ms == 1303U);
    // 1001 ms：超时，本次测量无效。
    assert(g_photoelectric_measurement_in_progress == 0U);
    assert(g_photoelectric_speed_valid == 0U);
    // 超时只是把本次测量判无效，上一次有效遮挡时间与速度保持不动。
    assert(g_photoelectric_block_time_ms == 20U);
    assert(g_photoelectric_speed_rpm_x100 == 13926);

    // 超时之后再来一次完整遮挡，应当重新正常工作。
    stub_enter_count = 5U;
    speed_tick();
    assert(g_photoelectric_enter_tick_ms == 1304U);
    assert(g_photoelectric_measurement_in_progress == 1U);
    run_ticks(173U);
    stub_leave_count = 4U;
    speed_tick();
    assert(g_photoelectric_speed_valid == 1U);
    assert(g_photoelectric_block_time_ms == 174U);
    assert(g_photoelectric_speed_rpm_x100 == 1600);

    // ---- 测试 9：re 复位后开始时间、速度、有效标志清零 ----
    assert(photoelectric_speed_get_rpm_x100() == 1600);
    photoelectric_speed_reset();
    assert(g_photoelectric_speed_tick_ms == 0U);
    assert(g_photoelectric_enter_tick_ms == 0U);
    assert(g_photoelectric_block_time_ms == 0U);
    assert(g_photoelectric_speed_rpm_x100 == 0);
    assert(g_photoelectric_speed_rpm == 0.0f);
    assert(g_photoelectric_linear_speed_cm_s == 0.0f);
    assert(g_photoelectric_speed_valid == 0U);
    assert(g_photoelectric_measurement_in_progress == 0U);
    assert(photoelectric_speed_get_rpm_x100() == 0);

    // 复位不能把旧的 enter/leave 累计值当成一次新边沿。
    run_ticks(10U);
    assert(g_photoelectric_measurement_in_progress == 0U);
    assert(g_photoelectric_speed_rpm_x100 == 0);
    assert(g_photoelectric_speed_valid == 0U);

    // 复位后必须还能正常测到下一个瓶子。
    // 如果 reset() 连"上次看到"的边沿计数也一起清了，那么复位后的第一次 update
    // 会拿旧计数假开始一次测量、再被 0 ms 毛刺结束，紧接着真实的下降沿就被漏掉了。
    stub_enter_count = 0U;
    stub_leave_count = 0U;
    photoelectric_speed_init();
    // 把基准同步到 5 / 5，模拟已经跑过一阵子的现场状态。
    stub_enter_count = 5U;
    stub_leave_count = 5U;
    speed_tick();
    photoelectric_speed_reset();

    stub_enter_count = 6U;
    speed_tick();
    assert(g_photoelectric_speed_tick_ms == 1U);
    assert(g_photoelectric_measurement_in_progress == 1U);
    assert(g_photoelectric_enter_tick_ms == 1U);

    run_ticks(173U);
    stub_leave_count = 6U;
    speed_tick();
    assert(g_photoelectric_speed_valid == 1U);
    assert(g_photoelectric_block_time_ms == 174U);
    assert(g_photoelectric_speed_rpm_x100 == 1600);

    // ---- 换算公式 ----
    // optical_rpm_x100 = (6000000 × 宽度 × 10000) / (周长_x10000 × 遮挡时间)
    assert(photoelectric_speed_rpm_x100_from_block_time(0U) == 0);
    assert(photoelectric_speed_rpm_x100_from_block_time(174U) == 1600);
    assert(photoelectric_speed_rpm_x100_from_block_time(348U) == 800);
    assert(photoelectric_speed_rpm_x100_from_block_time(20U) == 13926);
    assert(photoelectric_speed_rpm_x100_from_block_time(1000U) == 278);

    // linear_speed_cm_s_x100 = (10000 × 宽度) / 遮挡时间
    assert(photoelectric_speed_linear_cm_s_x100_from_block_time(0U) == 0);
    assert(photoelectric_speed_linear_cm_s_x100_from_block_time(174U) == 2011);
    assert(photoelectric_speed_linear_cm_s_x100_from_block_time(20U) == 17500);
    assert(photoelectric_speed_linear_cm_s_x100_from_block_time(1000U) == 350);

    // ---- 临界区读取：复制前后各开关一次全局中断 ----
    {
        uint32_t before = stub_disable_global_count;
        int32_t expected = g_photoelectric_speed_rpm_x100;

        assert(photoelectric_speed_get_rpm_x100() == expected);
        assert(stub_disable_global_count == (before + 1U));
        assert(stub_enable_global_count == stub_disable_global_count);
    }

    puts("Photoelectric speed tests passed.");
    return 0;
}
