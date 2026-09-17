//#############################################################################
// FILE:   turntable_speed.c
// TITLE:  F28384D 转盘转速 M 法测量（CPU Timer0 10 ms 采样）
//#############################################################################
//
// M 法测速原理：
//   1. CPU Timer0 每 10 ms 触发一次中断，读取 EQEP_getPosition(myEQEP3_BASE)；
//   2. 本次位置减上次位置得到带方向的计数差 delta，并处理 0～4095 回绕；
//   3. 连续 5 次 10 ms 的 delta 累加成一个 50 ms 测速窗口，累加值就是 M；
//   4. rpm = 60 × M / (4096 × 0.05) = M × 0.29296875。
//
// 主用百分之一 rpm 的定点整数表示转速，中断里只做整数乘除，浮点值仅用于 CCS Watch：
//   speed_rpm_x100 = M × 1875 / 64
//   例：M = 100   -> 2929   ->  29.29 rpm
//       M = -100  -> -2929  -> -29.29 rpm（负号表示反转）
//       M = 0     -> 0      ->   0.00 rpm
//
// 测速窗口长度只由 Timer0 决定，和主循环里的 TCP 轮询、DEVICE_DELAY_US 无关，
// 所以 W5500 收发造成的周期抖动不会污染测速结果。
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "turntable_speed.h"

// ---- CCS Watch 变量 ----
volatile uint32_t g_turntable_encoder_position = 0U;       // 当前读取的编码器 3 位置，范围 0～4095
volatile uint32_t g_turntable_previous_position = 0U;      // 上一次 10 ms 采样的位置
volatile int32_t  g_turntable_sample_delta = 0;            // 最近一次 10 ms 内的带方向计数差
volatile int32_t  g_turntable_accumulated_count = 0;       // 当前 50 ms 窗口内已累计的计数增量
volatile int32_t  g_turntable_speed_count_m = 0;           // 最近一个 50 ms 测速窗口内的 M 值
volatile int32_t  g_turntable_speed_rpm_x100 = 0;          // 带方向转速，单位 0.01 rpm
volatile float    g_turntable_speed_rpm = 0.0f;            // 便于 CCS Watch 观察的浮点 rpm 值
volatile uint32_t g_turntable_speed_update_count = 0U;     // 已完成的 50 ms 测速计算次数
volatile uint32_t g_turntable_timer_interrupt_count = 0U;  // Timer0 中断累计次数

// 当前 50 ms 窗口内已经完成的 10 ms 采样次数。
static uint16_t turntable_window_sample_count = 0U;

// 10 ms 定时周期对应的 CPU Timer0 计数值。
// DEVICE_SYSCLK_FREQ = 200 MHz，预分频为 0，所以 10 ms 需要 2000000 个 SYSCLK；
// 计数器从周期值递减到 0 再重载，所以 PRD 写 2000000 - 1。
#define TURNTABLE_SPEED_TIMER_PERIOD_COUNT \
    ((DEVICE_SYSCLK_FREQ / TURNTABLE_SPEED_TIMER_FREQUENCY_HZ) - 1U)

// 把本次位置与上次位置之差换算成带方向的计数增量。
// QPOSCNT 在 0～4095 之间循环：正转跨零得到很大的负数，反转跨零得到很大的正数，
// 超过半个计数周期就认为发生了回绕，加减一个 4096 换回真正的位移。
int32_t turntable_speed_wrap_delta(uint32_t current_position,
                                   uint32_t previous_position)
{
    int32_t delta = (int32_t)current_position - (int32_t)previous_position;

    if (delta > TURNTABLE_SPEED_COUNTS_ROLLOVER_HALF)
    {
        delta -= TURNTABLE_SPEED_COUNTS_ROLLOVER;
    }
    else if (delta < -TURNTABLE_SPEED_COUNTS_ROLLOVER_HALF)
    {
        delta += TURNTABLE_SPEED_COUNTS_ROLLOVER;
    }

    return delta;
}

// rpm = 60 × M / (4096 × 0.05) = M × 0.29296875
// rpm × 100 = M × 29.296875 = M × 1875 / 64
int32_t turntable_speed_rpm_x100_from_count(int32_t count_m)
{
    return (int32_t)((count_m * 1875) / 64);
}

// 推进一次 10 ms 采样：算 delta、累加 M，满 5 次就发布一次转速。
static void turntable_speed_sample_position(uint32_t current_position)
{
    int32_t delta;

    g_turntable_encoder_position = current_position;

    delta = turntable_speed_wrap_delta(current_position,
                                       g_turntable_previous_position);
    g_turntable_previous_position = current_position;
    g_turntable_sample_delta = delta;

    g_turntable_accumulated_count += delta;
    turntable_window_sample_count++;

    if (turntable_window_sample_count >= TURNTABLE_SPEED_SAMPLES_PER_WINDOW)
    {
        // 50 ms 窗口结束：锁定 M，换算转速，然后清零累计值开始下一个窗口。
        g_turntable_speed_count_m = g_turntable_accumulated_count;
        g_turntable_speed_rpm_x100 =
            turntable_speed_rpm_x100_from_count(g_turntable_speed_count_m);
        g_turntable_speed_rpm =
            (float)g_turntable_speed_rpm_x100 * 0.01f;
        g_turntable_speed_update_count++;

        g_turntable_accumulated_count = 0;
        turntable_window_sample_count = 0U;
    }
}

// Timer0 中断服务函数：只读取编码器并做 M 法累加。
// 这里不做 TCP 发送、不做 ASCII 转换、不做复杂格式化，保证 10 ms 周期稳定。
TURNTABLE_SPEED_ISR void turntable_speed_timer_isr(void)
{
    g_turntable_timer_interrupt_count++;

    turntable_speed_sample_position(EQEP_getPosition(myEQEP3_BASE));

    // Timer0 属于 PIE 第 1 组（INT1.7）。
    CPUTimer_clearOverflowFlag(CPUTIMER0_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

void turntable_speed_init(void)
{
    // 用当前位置建立基准，上电后的第一次采样不会被当成一大段位移。
    g_turntable_encoder_position = EQEP_getPosition(myEQEP3_BASE);
    g_turntable_previous_position = g_turntable_encoder_position;
    g_turntable_sample_delta = 0;
    g_turntable_accumulated_count = 0;
    g_turntable_speed_count_m = 0;
    g_turntable_speed_rpm_x100 = 0;
    g_turntable_speed_rpm = 0.0f;
    g_turntable_speed_update_count = 0U;
    g_turntable_timer_interrupt_count = 0U;
    turntable_window_sample_count = 0U;

    // CPU Timer0：预分频 0，周期 10 ms（100 Hz），仿真时自由运行。
    // 这里用 Driverlib 直接配置，不动 SysConfig 里的 eQEP3 配置。
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    CPUTimer_setPeriod(CPUTIMER0_BASE, TURNTABLE_SPEED_TIMER_PERIOD_COUNT);
    CPUTimer_setEmulationMode(CPUTIMER0_BASE,
                              CPUTIMER_EMULATIONMODE_RUNFREE);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_clearOverflowFlag(CPUTIMER0_BASE);

    Interrupt_register(INT_TIMER0, turntable_speed_timer_isr);

    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    Interrupt_enable(INT_TIMER0);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

int32_t turntable_speed_get_rpm_x100(void)
{
    int32_t snapshot;
    bool interrupts_were_enabled;

    // 极短临界区：复制完 32 位转速立刻恢复中断，避免 ISR 更新时读到半新半旧的值。
    interrupts_were_enabled = Interrupt_disableGlobal();
    snapshot = g_turntable_speed_rpm_x100;
    if (interrupts_were_enabled == false)
    {
        (void)Interrupt_enableGlobal();
    }

    return snapshot;
}
