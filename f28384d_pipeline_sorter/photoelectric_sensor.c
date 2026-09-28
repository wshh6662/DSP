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

static uint16_t photoelectric_previous_raw_level = 1U; // 上一次采样的 GPIO26 电平
static volatile uint16_t photoelectric_count_reset_requested = 0U;

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
    photoelectric_count_reset_requested = 0U;
}

void photoelectric_sensor_request_count_reset(void)
{
    photoelectric_count_reset_requested = 1U;
}

void photoelectric_sensor_update(void)
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

    if ((photoelectric_previous_raw_level != 0U) &&
        (g_photoelectric_raw_level == 0U))
    {
        // NPN 常开输出导通：物品进入，GPIO26 出现下降沿。
        g_photoelectric_falling_edge = 1U;
        g_photoelectric_rising_edge = 0U;
        g_photoelectric_detected = 1U;
        g_photoelectric_enter_count++;
    }
    else if ((photoelectric_previous_raw_level == 0U) &&
             (g_photoelectric_raw_level != 0U))
    {
        // NPN 常开输出断开：物品离开，GPIO26 出现上升沿。
        g_photoelectric_falling_edge = 0U;
        g_photoelectric_rising_edge = 1U;
        g_photoelectric_detected = 0U;
        g_photoelectric_leave_count++;
    }

    photoelectric_previous_raw_level = g_photoelectric_raw_level;
}
