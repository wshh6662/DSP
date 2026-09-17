//#############################################################################
// FILE:   test_turntable_speed.c
// TITLE:  转盘 M 法测速主机侧测试（gcc 直接编译，不需要 C2000 工具链）
//#############################################################################

#include "turntable_speed.h"

#include "board.h"
#include "device.h"
#include "driverlib.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// 编码器 3 位置桩：每次中断前由测试改写。
static uint32_t stub_encoder3_position = 0U;

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
#define STUB_TIMER_LOG_CAPACITY   48U
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

// 模拟一次 Timer0 中断：先摆好编码器位置，再调用注册过的 ISR。
static void run_timer_interrupt(uint32_t position)
{
    assert(stub_timer0_handler != 0);
    stub_encoder3_position = position;
    stub_timer0_handler();
}

static void run_timer_interrupts(const uint32_t *positions, uint32_t count)
{
    uint32_t index;

    for (index = 0U; index < count; index++)
    {
        run_timer_interrupt(positions[index]);
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
    // 每个 50 ms 测速窗口固定 5 次 10 ms 采样。
    static const uint32_t window_idle[5] = {100U, 100U, 100U, 100U, 100U};
    static const uint32_t window_forward[5] = {120U, 140U, 160U, 180U, 200U};
    static const uint32_t window_reverse[5] = {180U, 160U, 140U, 120U, 100U};
    static const uint32_t window_fast[5] = {880U, 1660U, 2440U, 3220U, 4000U};
    static const uint32_t window_forward_rollover[5] = {4090U, 4095U, 4U, 9U, 14U};
    static const uint32_t window_reverse_rollover[5] = {9U, 4U, 4095U, 4090U, 4085U};

    // ---- CPU Timer0：预分频 0，周期 10 ms，自由运行仿真模式 ----
    stub_encoder3_position = 100U;
    turntable_speed_init();

    assert(stub_timer_period == ((DEVICE_SYSCLK_FREQ / 100U) - 1U));
    assert(stub_timer_period == 1999999U);
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

    // 初始化后用当前位置建立基准，第一次采样不会产生虚假位移。
    assert(g_turntable_encoder_position == 100U);
    assert(g_turntable_previous_position == 100U);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_rpm_x100 == 0);
    assert(g_turntable_speed_update_count == 0U);
    assert(g_turntable_timer_interrupt_count == 0U);

    // ---- 测试 1：编码器不动，M 为 0，转速为 0.00 ----
    run_timer_interrupts(window_idle, 5U);
    assert(g_turntable_timer_interrupt_count == 5U);
    assert(g_turntable_sample_delta == 0);
    assert_window(0, 0, 0.0f, 1U);

    // 每次中断都要清标志并应答 PIE 第 1 组。
    assert(stub_timer_overflow_clear_count == 6U);
    assert(stub_ack_group1_count == 5U);

    // ---- 测试 2：正转普通计数 previous = 100，current = 120，delta = +20 ----
    assert(turntable_speed_wrap_delta(120U, 100U) == 20);

    // ---- 测试 3：反转普通计数 previous = 120，current = 100，delta = -20 ----
    assert(turntable_speed_wrap_delta(100U, 120U) == -20);

    // ---- 测试 4：正转跨零 previous = 4090，current = 5，delta = +11 ----
    assert(turntable_speed_wrap_delta(5U, 4090U) == 11);

    // ---- 测试 5：反转跨零 previous = 5，current = 4090，delta = -11 ----
    assert(turntable_speed_wrap_delta(4090U, 5U) == -11);

    // 边界：正好相差半个计数周期时不改判方向。
    assert(turntable_speed_wrap_delta(100U, 100U) == 0);
    assert(turntable_speed_wrap_delta(2048U, 0U) == 2048);
    assert(turntable_speed_wrap_delta(0U, 2048U) == -2048);
    assert(turntable_speed_wrap_delta(4095U, 0U) == -1);
    assert(turntable_speed_wrap_delta(0U, 4095U) == 1);

    // ---- 测试 7：0.01 rpm 定点换算，rpm_x100 = M × 1875 / 64 ----
    assert(turntable_speed_rpm_x100_from_count(0) == 0);
    assert(turntable_speed_rpm_x100_from_count(100) == 2929);
    assert(turntable_speed_rpm_x100_from_count(-100) == -2929);
    assert(turntable_speed_rpm_x100_from_count(1) == 29);
    assert(turntable_speed_rpm_x100_from_count(-1) == -29);
    assert(turntable_speed_rpm_x100_from_count(34) == 996);
    assert(turntable_speed_rpm_x100_from_count(50) == 1464);

    // ---- 测试 6：5 次 10 ms 采样累加成 M，不能把单次 delta 当成 50 ms 的 M ----
    // 上一个窗口停在 100，这里开始新的 50 ms 窗口。
    run_timer_interrupt(window_forward[0]);
    assert(g_turntable_sample_delta == 20);
    assert(g_turntable_accumulated_count == 20);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_update_count == 1U);

    run_timer_interrupt(window_forward[1]);
    assert(g_turntable_accumulated_count == 40);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_update_count == 1U);

    run_timer_interrupt(window_forward[2]);
    assert(g_turntable_accumulated_count == 60);

    run_timer_interrupt(window_forward[3]);
    assert(g_turntable_accumulated_count == 80);
    assert(g_turntable_speed_count_m == 0);
    assert(g_turntable_speed_update_count == 1U);

    run_timer_interrupt(window_forward[4]);
    assert(g_turntable_sample_delta == 20);
    assert(g_turntable_accumulated_count == 0);
    // M = +100（= 5 × 20）对应 29.29 rpm。
    assert_window(100, 2929, 29.29f, 2U);

    // ---- 反转整窗口：M = -100，转速应为负 ----
    run_timer_interrupts(window_reverse, 5U);
    assert_window(-100, -2929, -29.29f, 3U);

    // ---- 高速正转整窗口：M = +3900 ----
    run_timer_interrupts(window_fast, 5U);
    assert(g_turntable_sample_delta == 780);
    assert_window(3900, 114257, 1142.57f, 4U);

    // ---- 正转跨零整窗口：4095 之后回到 0，delta 仍为小幅正值 ----
    run_timer_interrupts(window_forward_rollover, 5U);
    assert(g_turntable_sample_delta == 5);
    assert_window(110, 3222, 32.22f, 5U);

    // ---- 反转跨零整窗口：0 之前退到 4095，delta 仍为小幅负值 ----
    run_timer_interrupts(window_reverse_rollover, 5U);
    assert(g_turntable_sample_delta == -5);
    assert_window(-25, -732, -7.32f, 6U);

    assert(g_turntable_timer_interrupt_count == 30U);
    assert(stub_timer_overflow_clear_count == 31U);
    assert(stub_ack_group1_count == 30U);

    // ISR 只允许清溢出标志：配置阶段的 9 次操作之后，日志里应当全是 'C'。
    assert(stub_timer_log_length == (9U + 30U));
    assert(strncmp(stub_timer_log, "TPRELCGIS", 9U) == 0);
    {
        uint32_t log_index;
        for (log_index = 9U; log_index < stub_timer_log_length; log_index++)
        {
            assert(stub_timer_log[log_index] == 'C');
        }
    }

    // ---- 临界区读取：复制快照前后各开关一次全局中断 ----
    assert(stub_disable_global_count == 0U);
    assert(stub_enable_global_count == 0U);
    assert(turntable_speed_get_rpm_x100() == -732);
    assert(stub_disable_global_count == 1U);
    assert(stub_enable_global_count == 1U);
    assert(turntable_speed_get_rpm_x100() == g_turntable_speed_rpm_x100);
    assert(stub_disable_global_count == 2U);
    assert(stub_enable_global_count == 2U);

    puts("Turntable speed tests passed.");
    return 0;
}
