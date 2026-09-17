//#############################################################################
// FILE:   photoelectric_sensor.c
// TITLE:  IN0 光电传感器检测
//#############################################################################

#include "driverlib.h"
#include "photoelectric_sensor.h"

volatile uint16_t g_photoelectric_raw_level = 1U;
volatile uint16_t g_photoelectric_detected = 0U;
volatile uint16_t g_photoelectric_falling_edge = 0U;
volatile uint16_t g_photoelectric_rising_edge = 0U;
volatile uint32_t g_photoelectric_enter_count = 0U;
volatile uint32_t g_photoelectric_leave_count = 0U;

static uint16_t photoelectric_previous_raw_level = 1U;

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
}

void photoelectric_sensor_update(void)
{
    g_photoelectric_raw_level =
        (uint16_t)GPIO_readPin(PHOTOELECTRIC_SENSOR_GPIO);

    // 边沿标志只保持一个主循环周期，便于在 CCS Watch 中诊断。
    g_photoelectric_falling_edge = 0U;
    g_photoelectric_rising_edge = 0U;

    if ((photoelectric_previous_raw_level != 0U) &&
        (g_photoelectric_raw_level == 0U))
    {
        // NPN 常开输出导通：物品进入，GPIO26 出现下降沿。
        g_photoelectric_falling_edge = 1U;
        g_photoelectric_detected = 1U;
        g_photoelectric_enter_count++;
    }
    else if ((photoelectric_previous_raw_level == 0U) &&
             (g_photoelectric_raw_level != 0U))
    {
        // NPN 常开输出断开：物品离开，GPIO26 出现上升沿。
        g_photoelectric_rising_edge = 1U;
        g_photoelectric_detected = 0U;
        g_photoelectric_leave_count++;
    }

    photoelectric_previous_raw_level = g_photoelectric_raw_level;
}
