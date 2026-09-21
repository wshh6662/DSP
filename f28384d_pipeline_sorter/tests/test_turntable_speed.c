//#############################################################################
// FILE:   test_turntable_speed.c
// TITLE:  转盘 M 法测速主机侧测试（gcc 直接编译，不需要 C2000 工具链）
//#############################################################################
//
// Timer0 时基从 10 ms 提到 1 ms 之后：
//   每 1 个 tick（1 ms）刷新一次光电；
//   每 10 个 tick（10 ms）才采一次编码器；
//   5 次编码器采样仍然组成 50 ms 的 M 法窗口，窗口数值与之前完全一致。
//#############################################################################

#include "turntable_speed.h"

#include "board.h"
#include "device.h"
#include "driverlib.h"
#include "photoelectric_sensor.h"
#include "photoelectric_speed.h"
#include "turntable_angle.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// 编码器 3 位置桩：每次编码器采样前由测试改写。
static uint32_t stub_encoder3_position = 0U;
static uint32_t stub_encoder3_set_position_count = 0U;

// Timer0 配置桩：记录参数、调用顺序和运行状态。
static uint32_t stub_timer_period = 0U;
static uint32_t stub_timer_prescaler = 0xFFFFFFFFU;
static uint32_t stub_timer_emulation_mode = 0xFFFFFFFFU;
static uint32_t stub_timer_running = 0U;
static uint32_t stub_timer_interrupt_enabled = 0U;
static uint32_t stub_timer_overflow_clear_count = 0U;
static uint32_t stub_timer_reload_count = 0U;

// Timer0 寄存器操作顺序日志，用于确认先写周期再重载计数器，
// 以及确认中断服务函数只清溢出标志、不重新配置定时器。
// 取值含义：T=stopTimer P=setPreScaler R=setPeriod E=setEmulationMode
//           L=reloadTimerCounter C=clearOverflowFlag G=Interrupt_register
//           I=CPUTimer_enableInterrupt
#define STUB_TIMER_LOG_CAPACITY   512U
static char stub_timer_log[STUB_TIMER_LOG_CAPACITY + 1U];
static uint32_t stub_timer_log_length = 0U;

// 中断控制器桩。
static uint32_t stub_interrupt_registered_number = 0xFFFFFFFFU;
static void (*stub_timer0_handler)(void) = 0;
static uint32_t stub_interrupt_enabled_number = 0xFFFFFFFFU;
static uint32_t stub_ack_group1_count = 0U;
static uint32_t stub_disable_global_count = 0U;
static uint32_t stub_enable_global_count = 0U;

static void log_timer_operation(char operation)
{
    assert(stub_timer_log_length < STUB_TIMER_LOG_CAPACITY);
    stub_timer_log[stub_timer_log_length] = operation;
    stub_timer_log_length++;
    stub_timer_log[stub_timer_log_length] = '\0';
}

static void assert_timer_log(const char *expected)
{
    assert(strcmp(stub_timer_log, expected) == 0);
}

uint32_t EQEP_getPosition(uint32_t base)
{
    assert(base == myEQEP3_BASE);
    return stub_encoder3_position;
}

void EQEP_setPosition(uint32_t base, uint32_t position)
{
    assert(base == myEQEP3_BASE);
    stub_encoder3_position = position;
    stub_encoder3_set_position_count++;
}

void CPUTimer_stopTimer(uint32_t base)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('T');
    stub_timer_running = 0U;
}

void CPUTimer_setPreScaler(uint32_t base, uint16_t prescaler)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('P');
    stub_timer_prescaler = prescaler;
}

void CPUTimer_setPeriod(uint32_t base, uint32_t period_count)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('R');
    stub_timer_period = period_count;
}

void CPUTimer_setEmulationMode(uint32_t base, uint32_t emulation_mode)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('E');
    stub_timer_emulation_mode = emulation_mode;
}

void CPUTimer_reloadTimerCounter(uint32_t base)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('L');
    stub_timer_reload_count++;
}

void CPUTimer_clearOverflowFlag(uint32_t base)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('C');
    stub_timer_overflow_clear_count++;
}

void CPUTimer_enableInterrupt(uint32_t base)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('I');
    stub_timer_interrupt_enabled = 1U;
}

void CPUTimer_startTimer(uint32_t base)
{
    assert(base == CPUTIMER0_BASE);
    log_timer_operation('S');
    stub_timer_running = 1U;
}

void Interrupt_register(uint32_t interrupt_number, void (*handler)(void))
{
    log_timer_operation('G');
    stub_interrupt_registered_number = interrupt_number;
    stub_timer0_handler = handler;
}

void Interrupt_enable(uint32_t interrupt_number)
{
    stub_interrupt_enabled_number = interrupt_number;
}

void Interrupt_clearACKGroup(uint16_t ack_group)
{
    assert(ack_group == INTERRUPT_ACK_GROUP1);
    stub_ack_group1_count++;
}

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

// 光电边沿累计计数：真实的 photoelectric_sensor.c 不在本测试里，
// 这里直接提供 ISR 读取的那两个量，用来验证 ISR 把当前值透传给了光电测速模块。
volatile uint32_t g_photoelectric_enter_count = 0U;
volatile uint32_t g_photoelectric_leave_count = 0U;

// 光电和当前角度模块不在本测试范围内，用记录桩验证 ISR 调用关系。
static uint32_t stub_photo_update_count = 0U;
static uint32_t stub_photo_speed_update_count = 0U;
static uint32_t stub_photo_speed_reset_count = 0U;
static uint32_t stub_last_enter_count = 0U;
static uint32_t stub_last_leave_count = 0U;
static uint32_t stub_angle_update_count = 0U;
static uint32_t stub_angle_init_count = 0U;
static int32_t stub_last_angle_delta = 0;

void photoelectric_sensor_update(void)
{
    stub_photo_update_count++;
}

void photoelectric_speed_update(uint32_t enter_count, uint32_t leave_count)
{
    stub_photo_speed_update_count++;
    stub_last_enter_count = enter_count;
    stub_last_leave_count = leave_count;
}

void photoelectric_speed_reset(void)
{
    stub_photo_speed_reset_count++;
}

void turntable_angle_update_delta(int32_t count_delta)
{
    stub_angle_update_count++;
    stub_last_angle_delta = count_delta;
}

void turntable_angle_init(void)
{
    stub_angle_init_count++;
}

// 一次 1 ms Timer0 中断。
static void run_timer_tick(void)
{
    assert(stub_timer0_handler != 0);
    stub_timer0_handler();
}

// 推进一次编码器采样：摆好位置后跑满 10 个 1 ms tick，第 10 个 tick 才采样。
static void run_encoder_sample(uint32_t position)
{
    uint16_t index;

    stub_encoder3_position = position;
    for (index = 0U; index < TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS; index++)
    {
        run_timer_tick();
    }
}

static void run_encoder_samples(const uint32_t *positions, uint32_t count)
{
    uint32_t index;

    for (index = 0U; index < count; index++)
    {
        run_encoder_sample(positions[index]);
    }
}

// 断言一个 50 ms 窗口结束后的 M、定点转速和浮点转速。
static void assert_window(int32_t expected_m,
                          int32_t expected_rpm_x100,
                          float expected_rpm,
                          uint32_t expected_update_count)
{
    assert(g_turntable_speed_count_m == expected_m);
    assert(g_turntable_speed_rpm_x100 == expected_rpm_x100);
    assert(g_turntable_speed_rpm > (expected_rpm - 0.01f));
    assert(g_turntable_speed_rpm < (expected_rpm + 0.01f));
    assert(g_turntable_speed_update_count == expected_update_count);
    assert(g_turntable_accumulated_count == 0);
}

int main(void)
{
    // 每个 50 ms 测速窗口固定 5 次 10 ms 编码器采样。
    static const uint32_t window_idle[5] = {100U, 100U, 100U, 100U, 100U};
    static const uint32_t window_forward[5] = {120U, 140U, 160U, 180U, 200U};
    static const uint32_t window_reverse[5] = {180U, 160U, 140U, 120U, 100U};
    static const uint32_t window_fast[5] = {880U, 1660U, 2440U, 3220U, 0U};
    static const uint32_t window_forward_rollover[5] = {3990U, 3995U, 0U, 5U, 10U};
    static const uint32_t window_reverse_rollover[5] = {5U, 0U, 3995U, 3990U, 3985U};

    uint16_t index;

    // ---- CPU Timer0：预分频 0，周期 1 ms，自由运行仿真模式 ----
    stub_encoder3_position = 100U;
    turntable_speed_init();

    assert(stub_timer_period == ((DEVICE_SYSCLK_FREQ / 1000U) - 1U));
    assert(stub_timer_period == 199999U);
    assert(stub_timer_prescaler == 0U);
    assert(stub_timer_emulation_mode == CPUTIMER_EMULATIONMODE_RUNFREE);
    assert(stub_timer_reload_count == 1U);
    assert(stub_timer_running == 1U);
    assert(stub_timer_interrupt_enabled == 1U);
    assert(stub_interrupt_registered_number == INT_TIMER0);
    assert(stub_timer0_handler != 0);
    assert(stub_interrupt_enabled_number == INT_TIMER0);
    assert(stub_timer_overflow_clear_count == 1U);

    // 必须先把周期写进 PRD，再重载计数器，否则第一个周期长度是错的。
    assert_timer_log("TPRELCGIS");

    // 时基常量必须自洽：1000 Hz = 100 Hz × 10 分频。
    assert(TURNTABLE_TIMER_TICK_FREQUENCY_HZ == 1000U);
    assert(TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS == 10U);
    assert(TURNTABLE_ENCODER_SAMPLE_FREQUENCY_HZ == 100U);
    assert((TURNTABLE_ENCODER_SAMPLE_FREQUENCY_HZ *
            TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS) ==
           TURNTABLE_TIMER_TICK_FREQUENCY_HZ);
    assert(TURNTABLE_SPEED_COUNTS_PER_REVOLUTION == 4000L);
    assert(TURNTABLE_SPEED_COUNTS_ROLLOVER == 4000L);
    assert(TURNTABLE_SPEED_COUNTS_ROLLOVER_HALF == 2000L);

    // 初始化后用当前位置建立基准，第一次采样不会产生虚假位移。
    assert(g_turntable_encoder_position == 100U);
    assert(g_turntable_previous_position == 100U);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_rpm_x100 == 0);
    assert(g_turntable_speed_update_count == 0U);
    assert(g_turntable_timer_interrupt_count == 0U);

    // ---- 测试 1 + 测试 2：光电每 1 ms 刷新，编码器每 10 个 tick 才采一次 ----
    // 这 10 个 tick 里位置一直摆在 130：如果实现提前读了编码器，
    // g_turntable_encoder_position 就会提前变成 130。
    stub_encoder3_position = 130U;
    g_photoelectric_enter_count = 7U;
    g_photoelectric_leave_count = 6U;

    for (index = 0U; index < (TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS - 1U);
         index++)
    {
        run_timer_tick();
    }
    assert(g_turntable_timer_interrupt_count == 9U);
    assert(g_turntable_encoder_position == 100U);   // 前 9 个 tick 没有采编码器
    assert(stub_angle_update_count == 0U);
    assert(stub_photo_update_count == 9U);          // 光电每 1 ms 都刷
    assert(stub_photo_speed_update_count == 9U);

    // 第 10 个 tick 才采样。位置放回 100，让这次采样不污染后面的 M 法窗口。
    stub_encoder3_position = 100U;
    run_timer_tick();
    assert(g_turntable_timer_interrupt_count == 10U);
    assert(g_turntable_encoder_position == 100U);
    assert(g_turntable_sample_delta == 0);
    assert(stub_angle_update_count == 1U);          // 第 10 个 tick 才采
    assert(stub_photo_update_count == 10U);
    assert(stub_photo_speed_update_count == 10U);

    // ISR 每次都要把当前的 enter/leave 计数交给光电测速模块。
    assert(stub_last_enter_count == 7U);
    assert(stub_last_leave_count == 6U);

    // 上面的分频验证已经消耗了 1 次窗口采样，重新初始化回到干净的窗口状态。
    stub_timer_log_length = 0U;
    stub_timer_log[0] = '\0';
    stub_timer_overflow_clear_count = 0U;
    stub_ack_group1_count = 0U;
    stub_photo_update_count = 0U;
    stub_photo_speed_update_count = 0U;
    stub_angle_update_count = 0U;
    stub_encoder3_position = 100U;
    turntable_speed_init();
    assert_timer_log("TPRELCGIS");
    assert(g_turntable_timer_interrupt_count == 0U);

    // ---- 测试 3：50 ms 窗口的原有结果不变 ----
    // 编码器不动：M 为 0，转速为 0.00。5 次采样 = 50 个 1 ms tick。
    run_encoder_samples(window_idle, 5U);
    assert(g_turntable_timer_interrupt_count == 50U);
    assert(g_turntable_sample_delta == 0);
    assert_window(0, 0, 0.0f, 1U);

    // ---- 正转普通计数 previous = 100，current = 120，delta = +20 ----
    assert(turntable_speed_wrap_delta(120U, 100U) == 20);

    // ---- 反转普通计数 previous = 120，current = 100，delta = -20 ----
    assert(turntable_speed_wrap_delta(100U, 120U) == -20);

    // ---- 正转跨零 previous = 3990，current = 5，delta = +15 ----
    assert(turntable_speed_wrap_delta(5U, 3990U) == 15);

    // ---- 反转跨零 previous = 5，current = 3990，delta = -15 ----
    assert(turntable_speed_wrap_delta(3990U, 5U) == -15);

    // 边界：正好相差半个计数周期时不改判方向。
    assert(turntable_speed_wrap_delta(100U, 100U) == 0);
    assert(turntable_speed_wrap_delta(2000U, 0U) == 2000);
    assert(turntable_speed_wrap_delta(0U, 2000U) == -2000);
    assert(turntable_speed_wrap_delta(3999U, 0U) == -1);
    assert(turntable_speed_wrap_delta(0U, 3999U) == 1);

    // ---- 0.01 rpm 定点换算：rpm_x100 = M × 30 ----
    assert(turntable_speed_rpm_x100_from_count(0) == 0);
    assert(turntable_speed_rpm_x100_from_count(100) == 3000);
    assert(turntable_speed_rpm_x100_from_count(-100) == -3000);
    assert(turntable_speed_rpm_x100_from_count(1) == 30);
    assert(turntable_speed_rpm_x100_from_count(-1) == -30);
    assert(turntable_speed_rpm_x100_from_count(34) == 1020);
    assert(turntable_speed_rpm_x100_from_count(50) == 1500);

    // ---- 5 次 10 ms 采样累加成 M，不能把单次 delta 当成 50 ms 的 M ----
    // 上一个窗口停在 100，这里开始新的 50 ms 窗口。
    run_encoder_sample(window_forward[0]);
    assert(g_turntable_sample_delta == 20);
    assert(g_turntable_accumulated_count == 20);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_update_count == 1U);

    run_encoder_sample(window_forward[1]);
    assert(g_turntable_accumulated_count == 40);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_update_count == 1U);

    run_encoder_sample(window_forward[2]);
    assert(g_turntable_accumulated_count == 60);

    run_encoder_sample(window_forward[3]);
    assert(g_turntable_accumulated_count == 80);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_update_count == 1U);

    run_encoder_sample(window_forward[4]);
    assert(g_turntable_sample_delta == 20);
    assert(g_turntable_accumulated_count == 0);
    // M = +100（= 5 × 20）对应 30.00 rpm。
    assert_window(100, 3000, 30.00f, 2U);

    // ---- 反转整窗口：M = -100，转速应为负 ----
    run_encoder_samples(window_reverse, 5U);
    assert_window(-100, -3000, -30.00f, 3U);

    // ---- 高速正转整窗口：M = +3900 ----
    run_encoder_samples(window_fast, 5U);
    assert(g_turntable_sample_delta == 780);
    assert_window(3900, 117000, 1170.00f, 4U);

    // 为跨零窗口建立3990计数的位置基准。
    g_turntable_encoder_position = 3900U;
    g_turntable_previous_position = 3900U;
    stub_encoder3_position = 3900U;

    // ---- 正转跨零整窗口：3999 之后回到 0，delta 仍为小幅正值 ----
    run_encoder_samples(window_forward_rollover, 5U);
    assert(g_turntable_sample_delta == 5);
    assert_window(110, 3300, 33.00f, 5U);

    // ---- 反转跨零整窗口：0 之前退到 3999，delta 仍为小幅负值 ----
    run_encoder_samples(window_reverse_rollover, 5U);
    assert(g_turntable_sample_delta == -5);
    assert_window(-25, -750, -7.50f, 6U);

    // 6 个窗口 × 5 次采样 × 10 个 tick = 300 次 1 ms 中断。
    assert(g_turntable_timer_interrupt_count == 300U);
    assert(stub_timer_overflow_clear_count == 301U);
    assert(stub_ack_group1_count == 300U);

    // 光电每 1 ms 刷新一次，编码器 30 次采样各更新一次角度。
    assert(stub_photo_update_count == 300U);
    assert(stub_photo_speed_update_count == 300U);
    assert(stub_angle_update_count == 30U);
    assert(stub_last_angle_delta == -5);

    // ISR 只允许清溢出标志：配置阶段的 9 次操作之后，日志里应当全是 'C'。
    assert(stub_timer_log_length == (9U + 300U));
    assert(strncmp(stub_timer_log, "TPRELCGIS", 9U) == 0);
    {
        uint32_t log_index;
        for (log_index = 9U; log_index < stub_timer_log_length; log_index++)
        {
            assert(stub_timer_log[log_index] == 'C');
        }
    }

    // ---- 临界区读取：复制快照前后各开关一次全局中断 ----
    assert(turntable_speed_get_rpm_x100() == -750);
    assert(stub_disable_global_count != 0U);
    assert(stub_disable_global_count == stub_enable_global_count);
    assert(turntable_speed_get_rpm_x100() == g_turntable_speed_rpm_x100);

    // ---- TCP 收到 re 后，硬件位置、测速基准、软件角度和光电测速一起清零 ----
    stub_photo_speed_reset_count = 0U;
    turntable_speed_reset_encoder3();
    assert(stub_encoder3_position == 0U);
    assert(stub_encoder3_set_position_count == 1U);
    assert(g_turntable_encoder_position == 0U);
    assert(g_turntable_previous_position == 0U);
    assert(g_turntable_sample_delta == 0);
    assert(g_turntable_accumulated_count == 0);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_rpm_x100 == 0);
    assert(g_turntable_speed_rpm == 0.0f);
    assert(stub_angle_init_count == 1U);
    assert(stub_photo_speed_reset_count == 1U);

    puts("Turntable speed tests passed.");
    return 0;
}
