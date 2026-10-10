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
static bool interrupts_disabled = false;

bool Interrupt_disableGlobal(void)
{
    bool previous = interrupts_disabled;
    interrupts_disabled = true;
    return previous;
}

bool Interrupt_enableGlobal(void)
{
    bool previous = interrupts_disabled;
    interrupts_disabled = false;
    return previous;
}

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
static void assert_payload(int32_t encoder_rpm_x100,
                           int32_t angle_degrees_x100,
                           uint16_t detected,
                           const char *expected)
{
    photoelectric_octet_t payload[PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY];
    uint16_t index;
    uint16_t length = photoelectric_tcp_build_payload(encoder_rpm_x100,
                                                      angle_degrees_x100,
                                                      detected,
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
    const char *typical = "enc_rpm=29.29,angle=359.91,photo=1\r\n";
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
    assert(photoelectric_sensor_update() == 0U);
    assert(g_photoelectric_raw_level == 1U);
    assert(g_photoelectric_detected == 0U);
    assert(g_photoelectric_falling_edge == 0U);
    assert(g_photoelectric_rising_edge == 0U);

    // 高到低是物品进入沿：只触发一次，并把检测状态置 1。
    stub_input_level = 0U;
    assert(photoelectric_sensor_update() == 1U);
    assert(g_photoelectric_raw_level == 0U);
    assert(g_photoelectric_detected == 1U);
    assert(g_photoelectric_falling_edge == 1U);
    assert(g_photoelectric_rising_edge == 0U);
    assert(g_photoelectric_enter_count == 1U);

    // 持续遮挡不能重复产生进入沿；下降沿显示保持 1，便于 CCS Watch 观察。
    assert(photoelectric_sensor_update() == 0U);
    assert(g_photoelectric_detected == 1U);
    assert(g_photoelectric_falling_edge == 1U);
    assert(g_photoelectric_rising_edge == 0U);
    assert(g_photoelectric_enter_count == 1U);

    // 低到高是物品离开沿：只触发一次，并把检测状态归 0。
    stub_input_level = 1U;
    assert(photoelectric_sensor_update() == 0U);
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

    // 单瓶遮挡 175 ms，直径 35 mm、转台半径 120 mm，对应约 15.92 rpm。
    // 初始已遮挡不应产生不完整的测速结果。
    stub_input_level = 0U;
    photoelectric_sensor_init();
    stub_input_level = 1U;
    photoelectric_sensor_update();
    assert(g_photoelectric_speed_rpm_x100 == 0);

    stub_input_level = 0U;
    assert(photoelectric_sensor_update() == 1U);
    for (uint16_t tick = 0U; tick < 174U; tick++)
    {
        photoelectric_sensor_update();
    }
    stub_input_level = 1U;
    photoelectric_sensor_update();
    assert(g_photoelectric_blocked_ms == 175U);
    assert(g_photoelectric_speed_rpm_x100 == 1592);
    assert(g_photoelectric_enter_count == 1U);

    // 主循环在同一个临界区取状态、速度和 32 位累计数。
    {
        uint16_t detected = 1U;
        int32_t speed = 0;
        uint32_t bottles = 0U;
        photoelectric_sensor_get_snapshot(&detected, &speed, &bottles);
        assert(detected == 0U);
        assert(speed == 1592);
        assert(bottles == 1U);
        assert(interrupts_disabled == false);
    }

    // 无新瓶经过时，旧测速值超时归零，不能冒充实时速度。
    for (uint16_t tick = 0U; tick < 2000U; tick++)
    {
        photoelectric_sensor_update();
    }
    assert(g_photoelectric_speed_rpm_x100 == 0);

    // ---- 测试 6：TCP 报文格式 ----
    // 每行同时发送编码器转速、当前角度和光电状态。
    assert_payload(2929, 35991, 1U, typical);
    assert(strlen(typical) == 36U);

    assert_payload(2929, 0, 0U,
                   "enc_rpm=29.29,angle=0.00,photo=0\r\n");

    // 两个都停着：全 0 且补两位小数。
    assert_payload(0, 0, 0U,
                   "enc_rpm=0.00,angle=0.00,photo=0\r\n");

    // 反转时当前位置仍使用 0～359.91 度的单圈角度。
    assert_payload(-100, 35991, 1U,
                   "enc_rpm=-1.00,angle=359.91,photo=1\r\n");

    // 三位整数、负数小数补零、小数前面补零。
    assert_payload(123456, 9000, 0U,
                   "enc_rpm=1234.56,angle=90.00,photo=0\r\n");
    assert_payload(5, 0, 1U,
                   "enc_rpm=0.05,angle=0.00,photo=1\r\n");

    // 极值：编码器转速取 INT32_MIN，配合实际单圈角度，整行长度为 35。
    assert_payload((-2147483647 - 1), 35991, 0U,
                   "enc_rpm=-21474836.48,angle=359.91,photo=0\r\n");

    // 两个定点参数都取 INT32_MIN 时仍应正确成帧，覆盖通用接口的最长输入。
    assert_payload((-2147483647 - 1), (-2147483647 - 1), 1U,
                   "enc_rpm=-21474836.48,angle=-21474836.48,photo=1\r\n");

    // 缓冲区至少覆盖最坏情况：
    // (8+12) + (7+12) + 8 + 2 = 49。
    assert(PHOTOELECTRIC_TCP_PAYLOAD_CAPACITY >= 49U);

    // 容量不足时必须整体拒绝，不能发出半截报文。
    assert(photoelectric_tcp_build_payload(2929, 35991, 1U, probe, 35U) == 0U);
    assert(photoelectric_tcp_build_payload(2929, 35991, 1U, probe, 36U) == 36U);

    // 容量之外一个字节都不能写。
    {
        uint8_t guarded[96];
        uint16_t index;

        memset(guarded, 0xAA, sizeof(guarded));
        assert(photoelectric_tcp_build_payload(2929, 90, 0U, guarded, 10U) == 0U);
        for (index = 10U; index < (uint16_t)sizeof(guarded); index++)
        {
            assert(guarded[index] == 0xAAU);
        }
    }

    // 空指针必须被拒绝。
    assert(photoelectric_tcp_build_payload(0, 0, 0U, 0, 80U) == 0U);

    // 主循环周期为 10 ms，100 次轮询形成 1 秒发送周期。
    assert(photoelectric_tcp_is_send_due(99U, 100U) == 0U);
    assert(photoelectric_tcp_is_send_due(100U, 100U) == 1U);
    assert(photoelectric_tcp_is_send_due(101U, 100U) == 1U);
    assert(photoelectric_tcp_is_send_due(0U, 0U) == 0U);

    puts("Photoelectric sensor tests passed.");
    return 0;
}
