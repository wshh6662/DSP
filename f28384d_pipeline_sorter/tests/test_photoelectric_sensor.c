#include "photoelectric_sensor.h"
#include "photoelectric_tcp_payload.h"
#include "driverlib.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t stub_input_level = 1U;
static uint32_t configured_direction_pin = 0U;
static uint32_t configured_direction = 0xFFFFU;
static uint32_t configured_pad_pin = 0U;
static uint32_t configured_pad_type = 0xFFFFU;
static uint32_t configured_qualification_pin = 0U;
static uint32_t configured_qualification = 0xFFFFU;

uint32_t GPIO_readPin(uint32_t pin)
{
    assert(pin == PHOTOELECTRIC_SENSOR_GPIO);
    return stub_input_level;
}

void GPIO_setDirectionMode(uint32_t pin, uint32_t direction)
{
    configured_direction_pin = pin;
    configured_direction = direction;
}

void GPIO_setPadConfig(uint32_t pin, uint32_t pin_type)
{
    configured_pad_pin = pin;
    configured_pad_type = pin_type;
}

void GPIO_setQualificationMode(uint32_t pin, uint32_t qualification)
{
    configured_qualification_pin = pin;
    configured_qualification = qualification;
}

// 校验整行报文与期望完全一致，并把实际长度也检查一遍。
static void assert_payload(uint16_t detected,
                           int32_t speed_rpm_x100,
                           const char *expected)
{
    photoelectric_octet_t payload[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];
    uint16_t index;
    uint16_t length = photoelectric_tcp_build_payload(
        detected,
        speed_rpm_x100,
        payload,
        PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY);

    assert(length == (uint16_t)strlen(expected));
    for (index = 0U; index < length; index++)
    {
        assert(payload[index] == (photoelectric_octet_t)expected[index]);
    }
}

int main(void)
{
    photoelectric_octet_t too_small[2];

    stub_input_level = 1U;
    photoelectric_sensor_init();
    assert(configured_direction_pin == PHOTOELECTRIC_SENSOR_GPIO);
    assert(configured_direction == GPIO_DIR_MODE_IN);
    assert(configured_pad_pin == PHOTOELECTRIC_SENSOR_GPIO);
    assert(configured_pad_type == GPIO_PIN_TYPE_PULLUP);
    assert(configured_qualification_pin == PHOTOELECTRIC_SENSOR_GPIO);
    assert(configured_qualification == GPIO_QUAL_SYNC);

    // 常开 NPN 通过光耦后低电平有效：无遮挡为 0。
    stub_input_level = 1U;
    photoelectric_sensor_update();
    assert(g_photoelectric_raw_level == 1U);
    assert(g_photoelectric_detected == 0U);
    assert(g_photoelectric_falling_edge == 0U);
    assert(g_photoelectric_rising_edge == 0U);

    // 高到低是物品进入沿：只触发一次，并把检测状态置 1。
    stub_input_level = 0U;
    photoelectric_sensor_update();
    assert(g_photoelectric_raw_level == 0U);
    assert(g_photoelectric_detected == 1U);
    assert(g_photoelectric_falling_edge == 1U);
    assert(g_photoelectric_rising_edge == 0U);
    assert(g_photoelectric_enter_count == 1U);

    // 持续遮挡不能重复产生进入沿，状态保持 1。
    photoelectric_sensor_update();
    assert(g_photoelectric_detected == 1U);
    assert(g_photoelectric_falling_edge == 0U);
    assert(g_photoelectric_rising_edge == 0U);
    assert(g_photoelectric_enter_count == 1U);

    // 低到高是物品离开沿：只触发一次，并把检测状态归 0。
    stub_input_level = 1U;
    photoelectric_sensor_update();
    assert(g_photoelectric_detected == 0U);
    assert(g_photoelectric_falling_edge == 0U);
    assert(g_photoelectric_rising_edge == 1U);
    assert(g_photoelectric_leave_count == 1U);

    // 持续无遮挡不能重复产生离开沿。
    photoelectric_sensor_update();
    assert(g_photoelectric_detected == 0U);
    assert(g_photoelectric_rising_edge == 0U);
    assert(g_photoelectric_leave_count == 1U);

    // TCP 客户端按行接收 "speed=xx.xx,photo=n"。
    assert_payload(0U, 0, "speed=0.00,photo=0\r\n");
    assert_payload(1U, 2929, "speed=29.29,photo=1\r\n");
    assert_payload(0U, -1250, "speed=-12.50,photo=0\r\n");

    // 补齐定点格式的边界：小数补零、负零附近、位数增加与最坏长度。
    assert_payload(1U, 100, "speed=1.00,photo=1\r\n");
    assert_payload(0U, 5, "speed=0.05,photo=0\r\n");
    assert_payload(1U, -1, "speed=-0.01,photo=1\r\n");
    assert_payload(1U, 123456, "speed=1234.56,photo=1\r\n");
    assert_payload(1U, -2929, "speed=-29.29,photo=1\r\n");
    assert_payload(0U,
                   (-2147483647 - 1),
                   "speed=-21474836.48,photo=0\r\n");

    // 容量不足或空指针时必须整体拒绝。
    assert(photoelectric_tcp_build_payload(1U, 2929, too_small, 2U) == 0U);
    assert(photoelectric_tcp_build_payload(1U, 0, 0, 48U) == 0U);

    // 主循环周期为 10 ms，五次轮询形成 50 ms 发送周期。
    assert(photoelectric_tcp_is_send_due(4U, 5U) == 0U);
    assert(photoelectric_tcp_is_send_due(5U, 5U) == 1U);
    assert(photoelectric_tcp_is_send_due(6U, 5U) == 1U);
    assert(photoelectric_tcp_is_send_due(0U, 0U) == 0U);

    puts("Photoelectric sensor tests passed.");
    return 0;
}
