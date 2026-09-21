#ifndef TURNTABLE_SPEED_H
#define TURNTABLE_SPEED_H

#include <stdint.h>

#if defined(__TMS320C2000__)
#define TURNTABLE_SPEED_ISR     __interrupt
#else
#define TURNTABLE_SPEED_ISR
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 编码器 3：1000 P/R，A/B 两相四倍频后每转 4000 个计数。
// SysConfig 把 QPOSCNT 配成 EQEP_POSITION_RESET_MAX_POS，计到 3999 后回到 0。
#define TURNTABLE_SPEED_COUNTS_PER_REVOLUTION   4000L
#define TURNTABLE_SPEED_COUNTS_ROLLOVER         4000L
#define TURNTABLE_SPEED_COUNTS_ROLLOVER_HALF    2000L

// CPU Timer0 时基：1 ms 一个 tick（1000 Hz）。
// 1 ms 时基确保光电传感器的上升沿和下降沿能及时采样。
#define TURNTABLE_TIMER_TICK_FREQUENCY_HZ       1000U

// 编码器仍然按原来的 10 ms 采样：每 10 个 1 ms tick 才读一次 eQEP3，
// 连续 5 次采样仍然组成 50 ms 的 M 法测速窗口，精度与窗口长度都没有变化。
#define TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS 10U
#define TURNTABLE_ENCODER_SAMPLE_FREQUENCY_HZ   100U
#define TURNTABLE_SPEED_SAMPLES_PER_WINDOW      5U

// 三个常量必须自洽：时基频率 = 编码器采样频率 × 分频比。
#if (TURNTABLE_TIMER_TICK_FREQUENCY_HZ != \
     (TURNTABLE_ENCODER_SAMPLE_FREQUENCY_HZ * \
      TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS))
#error "TURNTABLE_TIMER_TICK_FREQUENCY_HZ must equal TURNTABLE_ENCODER_SAMPLE_FREQUENCY_HZ * TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS"
#endif

// CCS Watch 变量，全部在 turntable_speed.c 顶部定义。
extern volatile uint32_t g_turntable_encoder_position;       // 当前读取的编码器 3 位置，范围 0～3999
extern volatile uint32_t g_turntable_previous_position;      // 上一次 10 ms 采样的位置，范围 0～3999
extern volatile int32_t  g_turntable_sample_delta;           // 最近一次 10 ms 采样的带方向计数差
extern volatile int32_t  g_turntable_accumulated_count;      // 当前 50 ms 窗口内已累计的计数增量
extern volatile int32_t  g_turntable_speed_count_m;          // 最近一个 50 ms 测速窗口内的 M 值
extern volatile int32_t  g_turntable_speed_rpm_x100;         // 带方向转速，单位 0.01 rpm
extern volatile float    g_turntable_speed_rpm;              // 便于 CCS Watch 观察的浮点 rpm 值
extern volatile uint32_t g_turntable_speed_update_count;     // 已完成的 50 ms 测速计算次数
extern volatile uint32_t g_turntable_timer_interrupt_count;  // Timer0 中断累计次数，1 ms 加一

// 建立编码器 3 的位置基准，并启动周期 1 ms 的 CPU Timer0（编码器仍每 10 ms 采一次）。
// 必须在 Interrupt_initModule()、Interrupt_initVectorTable() 和 Board_init() 之后调用。
void turntable_speed_init(void);

// 读取带方向的转盘转速快照，单位 0.01 rpm；正数为正转、负数为反转。
// 内部在极短临界区内复制 ISR 正在更新的数值，避免 32 位数据被读成半新半旧。
int32_t turntable_speed_get_rpm_x100(void);

// 把 eQEP3 位置、测速基准和软件角度同步清零，供 TCP "re" 命令调用。
void turntable_speed_reset_encoder3(void);

// Timer0 中断服务函数：每 1 ms 刷新光电边沿，
// 每 10 个 tick 才采一次编码器，更新 M 法转速和当前角度。
TURNTABLE_SPEED_ISR void turntable_speed_timer_isr(void);

// 把本次位置与上次位置之差换算成带方向的计数增量，并处理 0～3999 的回绕。
int32_t turntable_speed_wrap_delta(uint32_t current_position,
                                   uint32_t previous_position);

// 把 50 ms 窗口内的 M 值换算成 0.01 rpm 定点转速：M × 30。
int32_t turntable_speed_rpm_x100_from_count(int32_t count_m);

#ifdef __cplusplus
}
#endif

#endif
