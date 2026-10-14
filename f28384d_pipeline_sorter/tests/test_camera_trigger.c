#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "camera_trigger.h"

static void test_positive_115_degree_travel_triggers_once(void)
{
    camera_trigger_init();
    camera_trigger_on_bottle(300U, 0);
    camera_trigger_update_position(2855U);
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(2856U);
    assert(g_camera_trigger_count == 1U);
    camera_trigger_update_position(3000U);
    assert(g_camera_trigger_count == 1U);
}

static void test_two_bottles_keep_separate_start_positions(void)
{
    camera_trigger_init();
    camera_trigger_on_bottle(0U, 0);
    camera_trigger_update_position(500U);
    camera_trigger_on_bottle(500U, 0);
    camera_trigger_update_position(2555U);
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(2556U);
    assert(g_camera_trigger_count == 1U);
    camera_trigger_update_position(3056U);
    assert(g_camera_trigger_count == 2U);
}

static void test_rollover_and_reverse_do_not_trigger_early(void)
{
    camera_trigger_init();
    camera_trigger_on_bottle(7500U, 0);
    camera_trigger_update_position(100U);
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(7900U);
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(2056U);
    assert(g_camera_trigger_count == 1U);
}

static void test_reset_discards_pending_bottle(void)
{
    camera_trigger_init();
    camera_trigger_on_bottle(0U, 0);
    camera_trigger_update_position(2555U);
    camera_trigger_reset_pending();
    camera_trigger_update_position(2700U);
    assert(g_camera_trigger_count == 0U);
}

static void test_reverse_distance_is_not_absolute(void)
{
    camera_trigger_init();
    camera_trigger_on_bottle(3000U, 0);
    camera_trigger_update_position(444U); // 倒退2556计数不能触发。
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(3000U); // 回到入瓶点，净前进量仍为0。
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(5556U);
    assert(g_camera_trigger_count == 1U);
}

static void test_pending_slots_are_released_after_forward_travel(void)
{
    uint16_t index;
    camera_trigger_init();
    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING + 1U; index++)
    {
        camera_trigger_on_bottle(0U, 0);
    }
    assert(g_camera_trigger_overflow_count == 1U);
    camera_trigger_update_position(2556U);
    assert(g_camera_trigger_count == CAMERA_TRIGGER_MAX_PENDING);
    camera_trigger_on_bottle(2556U, 0); // 已触发槽位可以重新入瓶。
    assert(g_camera_trigger_overflow_count == 1U);
    camera_trigger_update_position(5112U);
    assert(g_camera_trigger_count == CAMERA_TRIGGER_MAX_PENDING + 1U);
}

static void test_base_count_is_latched_for_each_bottle(void)
{
    camera_trigger_init();
    g_camera_trigger_base_counts = 2556U;
    camera_trigger_on_bottle(0U, 0);
    g_camera_trigger_base_counts = 2600U;
    camera_trigger_on_bottle(500U, 0);

    camera_trigger_update_position(2556U);
    assert(g_camera_trigger_count == 1U);
    camera_trigger_update_position(3099U);
    assert(g_camera_trigger_count == 1U);
    camera_trigger_update_position(3100U);
    assert(g_camera_trigger_count == 2U);
}

static void test_delay_advances_trigger_by_encoder_counts(void)
{
    camera_trigger_init();
    g_camera_trigger_base_counts = 2556U;
    g_camera_trigger_delay_ms = 10U;
    camera_trigger_on_bottle(0U, 3000); // 30 rpm: 40 counts in 10 ms.
    assert(g_camera_trigger_actual_target_counts == 2516U);
    g_camera_trigger_delay_ms = 0U; // 已入瓶目标不随现场调参跳变。
    camera_trigger_update_position(2515U);
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(2516U);
    assert(g_camera_trigger_count == 1U);
}

static void test_zero_delay_and_reverse_speed_keep_base_position(void)
{
    camera_trigger_init();
    camera_trigger_on_bottle(0U, 3000);
    assert(g_camera_trigger_actual_target_counts == 2556U);
    camera_trigger_update_position(2555U);
    assert(g_camera_trigger_count == 0U);
    camera_trigger_update_position(2556U);
    assert(g_camera_trigger_count == 1U);

    camera_trigger_init();
    g_camera_trigger_delay_ms = 10U;
    camera_trigger_on_bottle(0U, -3000);
    assert(g_camera_trigger_actual_target_counts == 2556U);
}

static void test_excess_delay_clamps_at_entry_and_records_it(void)
{
    camera_trigger_init();
    g_camera_trigger_base_counts = 20U;
    g_camera_trigger_delay_ms = 10U;
    camera_trigger_on_bottle(0U, 3000);
    assert(g_camera_trigger_actual_target_counts == 0U);
    assert(g_camera_trigger_compensation_saturated_count == 1U);
    camera_trigger_update_position(0U);
    assert(g_camera_trigger_count == 1U);
}

int main(void)
{
    test_positive_115_degree_travel_triggers_once();
    test_two_bottles_keep_separate_start_positions();
    test_rollover_and_reverse_do_not_trigger_early();
    test_reset_discards_pending_bottle();
    test_reverse_distance_is_not_absolute();
    test_pending_slots_are_released_after_forward_travel();
    test_base_count_is_latched_for_each_bottle();
    test_delay_advances_trigger_by_encoder_counts();
    test_zero_delay_and_reverse_speed_keep_base_position();
    test_excess_delay_clamps_at_entry_and_records_it();
    puts("camera trigger tests passed");
    return 0;
}
