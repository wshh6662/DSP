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
                           int32_t encoder_rpm_x100,
                           int32_t angle_degrees_x100,
                           const char *expected)
{
    photoelectric_octet_t payload[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];
    uint16_t index;
    uint16_t length = photoelectric_tcp_build_payload(detected,
                                                      encoder_rpm_x100,
                                                      angle_degrees_x100,
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
    const char *typical = "enc_rpm=29.29,obj=1,angle=359.91\r\n";
    photoelectric_octet_t probe[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];

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

    // 持续遮挡不能重复产生进入沿；下降沿显示保持 1，便于 CCS Watch 观察。
    photoelectric_sensor_update();
    assert(g_photoelectric_detected == 1U);
    assert(g_photoelectric_falling_edge == 1U);
    assert(g_photoelectric_rising_edge == 0U);
    assert(g_photoelectric_enter_count == 1U);

    // 低到高是物品离开沿：只触发一次，并把检测状态归 0。
    stub_input_level = 1U;
    photoelectric_sensor_update();
    assert(g_photoelectric_detected == 0U);
    assert(g_photoelectric_falling_edge == 0U);
    assert(g_photoelectric_rising_edge == 1U);
    assert(g_photoelectric_leave_count == 1U);

    // 持续无遮挡不能重复产生离开沿；上升沿显示保持 1，直到下一次下降沿。
    photoelectric_sensor_update();
    assert(g_photoelectric_detected == 0U);
    assert(g_photoelectric_falling_edge == 0U);
    assert(g_photoelectric_rising_edge == 1U);
    assert(g_photoelectric_leave_count == 1U);

    // HMI 的计数清零请求由 1 ms 更新函数执行，避免主循环与中断同时改 32 位计数。
    photoelectric_sensor_request_count_reset();
    photoelectric_sensor_update();
    assert(g_photoelectric_enter_count == 0U);
    assert(g_photoelectric_leave_count == 0U);
    assert(g_photoelectric_count_reset_count == 1U);

    // ---- 测试 6：TCP 报文格式 ----
    // 只发送编码器转速、物品状态和当前角度；光电测速字段已经移除。
    assert_payload(1U, 2929, 35991, typical);
    assert(strlen(typical) == 34U);

    assert_payload(1U, 2929, 0,
                   "enc_rpm=29.29,obj=1,angle=0.00\r\n");

    // 两个都停着：全 0 且补两位小数。
    assert_payload(0U, 0, 0,
                   "enc_rpm=0.00,obj=0,angle=0.00\r\n");

    // 反转时当前位置仍使用 0～359.91 度的单圈角度。
    assert_payload(1U, -100, 35991,
                   "enc_rpm=-1.00,obj=1,angle=359.91\r\n");

    // 三位整数、负数小数补零、小数前面补零。
    assert_payload(0U, 123456, 9000,
                   "enc_rpm=1234.56,obj=0,angle=90.00\r\n");
    assert_payload(1U, 5, 0,
                   "enc_rpm=0.05,obj=1,angle=0.00\r\n");

    // 极值：编码器转速取 INT32_MIN，整行达到最坏长度 41。
    assert_payload(1U, (-2147483647 - 1), 35991,
                   "enc_rpm=-21474836.48,obj=1,angle=359.91\r\n");

    // 缓冲区至少覆盖最坏情况：
    // (8+12) + (5+1) + (7+6) + 2 = 41。
    assert(PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY >= 41U);

    // 容量不足时必须整体拒绝，不能发出半截报文。
    assert(photoelectric_tcp_build_payload(1U, 2929, 35991, probe, 33U) == 0U);
    assert(photoelectric_tcp_build_payload(1U, 2929, 35991, probe, 34U) == 34U);

    // 容量之外一个字节都不能写。
    {
        uint8_t guarded[96];
        uint16_t index;

        memset(guarded, 0xAA, sizeof(guarded));
        assert(photoelectric_tcp_build_payload(1U, 2929, 90, guarded, 10U) == 0U);
        for (index = 10U; index < (uint16_t)sizeof(guarded); index++)
        {
            assert(guarded[index] == 0xAAU);
        }
    }

    // 空指针必须被拒绝。
    assert(photoelectric_tcp_build_payload(1U, 0, 0, 0, 80U) == 0U);

    // 主循环周期为 10 ms，100 次轮询形成 1 秒发送周期。
    assert(photoelectric_tcp_is_send_due(99U, 100U) == 0U);
    assert(photoelectric_tcp_is_send_due(100U, 100U) == 1U);
    assert(photoelectric_tcp_is_send_due(101U, 100U) == 1U);
    assert(photoelectric_tcp_is_send_due(0U, 0U) == 0U);

    puts("Photoelectric sensor tests passed.");
    return 0;
}
