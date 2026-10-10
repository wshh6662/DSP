//#############################################################################
// FILE:   photoelectric_sensor.c
// TITLE:  IN0 光电传感器检测
//#############################################################################

#include "driverlib.h"
#include "photoelectric_sensor.h"

volatile uint16_t g_photoelectric_raw_level = 1U;      // GPIO26 当前原始电平
volatile uint16_t g_photoelectric_detected = 0U;       // 物品状态：下降沿置 1，上升沿置 0
volatile uint16_t g_photoelectric_falling_edge = 0U;   // 最近一次边沿为下降沿时保持 1
volatile uint16_t g_photoelectric_rising_edge = 0U;    // 最近一次边沿为上升沿时保持 1
volatile uint32_t g_photoelectric_enter_count = 0U;    // 下降沿累计次数
volatile uint32_t g_photoelectric_leave_count = 0U;    // 上升沿累计次数
volatile uint32_t g_photoelectric_count_reset_count = 0U; // 已执行的计数清零次数
volatile uint32_t g_photoelectric_blocked_ms = 0U;        // 上一瓶完整遮挡持续时间
volatile int32_t g_photoelectric_speed_rpm_x100 = 0;      // 单瓶宽度估算转速，0.01 rpm

// v=35 mm / 遮挡秒数，rpm=v*60/(2*pi*120 mm)；278521/t_ms 为 rpm*100。
#define PHOTOELECTRIC_RPM_X100_MS_NUMERATOR  278521U
#define PHOTOELECTRIC_SPEED_STALE_MS         2000U

static uint16_t photoelectric_previous_raw_level = 1U; // 上一次采样的 GPIO26 电平
static uint16_t photoelectric_measurement_started = 0U;
static uint32_t photoelectric_blocked_ticks = 0U;
static uint16_t photoelectric_speed_age_ms = 0U;
// 主循环只置请求标志，由更新函数在 Timer0 中断内清零累计计数。
static volatile uint16_t photoelectric_count_reset_requested = 0U;

// 配置低电平有效的光电输入，并读取初始电平建立边沿检测基准。
void photoelectric_sensor_init(void)
{
    GPIO_setDirectionMode(PHOTOELECTRIC_SENSOR_GPIO, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(PHOTOELECTRIC_SENSOR_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(PHOTOELECTRIC_SENSOR_GPIO, GPIO_QUAL_SYNC);

    // 用启动时的真实电平建立基准，避免上电产生一次虚假边沿。
    g_photoelectric_raw_level =
        (uint16_t)GPIO_readPin(PHOTOELECTRIC_SENSOR_GPIO);
    photoelectric_previous_raw_level = g_photoelectric_raw_level;
    g_photoelectric_detected =
        (g_photoelectric_raw_level == 0U) ? 1U : 0U;
    g_photoelectric_falling_edge = 0U;
    g_photoelectric_rising_edge = 0U;
    g_photoelectric_enter_count = 0U;
    g_photoelectric_leave_count = 0U;
    g_photoelectric_count_reset_count = 0U;
    g_photoelectric_blocked_ms = 0U;
    g_photoelectric_speed_rpm_x100 = 0;
    photoelectric_measurement_started = 0U;
    photoelectric_blocked_ticks = 0U;
    photoelectric_speed_age_ms = 0U;
    photoelectric_count_reset_requested = 0U;
}

// 延迟到下一次光电更新时执行，避免主循环与中断同时修改 32 位计数。
void photoelectric_sensor_request_count_reset(void)
{
    photoelectric_count_reset_requested = 1U;
}

// Timer0 每 1 ms 调用：先消费清零请求，再比较本次与上次输入电平。
uint16_t photoelectric_sensor_update(void)
{
    if (photoelectric_count_reset_requested != 0U)
    {
        g_photoelectric_enter_count = 0U;
        g_photoelectric_leave_count = 0U;
        photoelectric_count_reset_requested = 0U;
        g_photoelectric_count_reset_count++;
    }

    g_photoelectric_raw_level =
        (uint16_t)GPIO_readPin(PHOTOELECTRIC_SENSOR_GPIO);

    // 只计完整的进入沿到离开沿；启动时已经遮挡的半个瓶子不参与测速。
    if ((photoelectric_measurement_started != 0U) &&
        (photoelectric_previous_raw_level == 0U) &&
        (photoelectric_blocked_ticks < UINT32_MAX))
    {
        photoelectric_blocked_ticks++;
    }

    if ((photoelectric_previous_raw_level != 0U) &&
        (g_photoelectric_raw_level != 0U) &&
        (g_photoelectric_speed_rpm_x100 != 0))
    {
        if (photoelectric_speed_age_ms < PHOTOELECTRIC_SPEED_STALE_MS)
        {
            photoelectric_speed_age_ms++;
        }
        if (photoelectric_speed_age_ms >= PHOTOELECTRIC_SPEED_STALE_MS)
        {
            g_photoelectric_speed_rpm_x100 = 0;
        }
    }

    if ((photoelectric_previous_raw_level != 0U) &&
        (g_photoelectric_raw_level == 0U))
    {
        // NPN 常开输出导通：物品进入，GPIO26 出现下降沿。
        g_photoelectric_falling_edge = 1U;
        g_photoelectric_rising_edge = 0U;
        g_photoelectric_detected = 1U;
        g_photoelectric_enter_count++;
        photoelectric_measurement_started = 1U;
        photoelectric_blocked_ticks = 0U;
        photoelectric_previous_raw_level = g_photoelectric_raw_level;
        return 1U;
    }
    else if ((photoelectric_previous_raw_level == 0U) &&
             (g_photoelectric_raw_level != 0U))
    {
        // NPN 常开输出断开：物品离开，GPIO26 出现上升沿。
        g_photoelectric_falling_edge = 0U;
        g_photoelectric_rising_edge = 1U;
        g_photoelectric_detected = 0U;
        g_photoelectric_leave_count++;
        if ((photoelectric_measurement_started != 0U) &&
            (photoelectric_blocked_ticks != 0U))
        {
            g_photoelectric_blocked_ms = photoelectric_blocked_ticks;
            g_photoelectric_speed_rpm_x100 = (int32_t)(
                (PHOTOELECTRIC_RPM_X100_MS_NUMERATOR +
                 photoelectric_blocked_ticks / 2U) /
                photoelectric_blocked_ticks);
            photoelectric_speed_age_ms = 0U;
        }
        photoelectric_measurement_started = 0U;
    }

    photoelectric_previous_raw_level = g_photoelectric_raw_level;
    return 0U;
}

void photoelectric_sensor_get_snapshot(uint16_t *detected,
                                       int32_t *speed_rpm_x100,
                                       uint32_t *bottle_count)
{
    bool interrupts_were_disabled = Interrupt_disableGlobal();

    *detected = g_photoelectric_detected;
    *speed_rpm_x100 = g_photoelectric_speed_rpm_x100;
    *bottle_count = g_photoelectric_enter_count;

    if (interrupts_were_disabled == false)
    {
        (void)Interrupt_enableGlobal();
    }
}
