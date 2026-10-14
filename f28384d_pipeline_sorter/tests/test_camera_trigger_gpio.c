#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "driverlib.h"
#include "camera_trigger.h"

static uint32_t last_pin_config;
static uint32_t last_pad_pin;
static uint32_t last_pad_type;
static uint32_t last_direction_pin;
static uint32_t last_direction;
static uint32_t last_write_pin;
static uint32_t last_write_value;
static uint32_t write_count;
static uint32_t pulse_start_count;

void GPIO_setPinConfig(uint32_t pin_config)
{
    last_pin_config = pin_config;
}

void GPIO_setPadConfig(uint32_t pin, uint32_t pin_type)
{
    last_pad_pin = pin;
    last_pad_type = pin_type;
}

void GPIO_setDirectionMode(uint32_t pin, uint32_t direction)
{
    last_direction_pin = pin;
    last_direction = direction;
}

void GPIO_writePin(uint32_t pin, uint32_t value)
{
    last_write_pin = pin;
    last_write_value = value;
    write_count++;
    if (value == 0U) { pulse_start_count++; }
}

static void test_k0_starts_off_and_pulses_at_115_degrees(void)
{
    uint16_t tick;

    camera_trigger_init();
    assert(last_pin_config == GPIO_0_GPIO0);
    assert(last_pad_pin == 0U);
    assert(last_pad_type == GPIO_PIN_TYPE_OD);
    assert(last_direction_pin == 0U);
    assert(last_direction == GPIO_DIR_MODE_OUT);
    assert(last_write_pin == 0U);
    assert(last_write_value == 1U);

    camera_trigger_on_bottle(300U, 0);
    camera_trigger_update_position(2855U);
    assert(last_write_value == 1U);
    camera_trigger_update_position(2856U);
    assert(last_write_value == 0U);
    assert(g_camera_trigger_output_active == 1U);

    assert(g_camera_trigger_count == 1U);
    for (tick = 0U; tick < 4U; tick++)
    {
        camera_trigger_tick_1ms();
        assert(g_camera_trigger_output_active == 1U);
    }
    camera_trigger_tick_1ms();
    assert(last_write_value == 1U);
    assert(g_camera_trigger_output_active == 0U);
    assert(write_count >= 4U);
}

static void test_simultaneous_bottles_produce_separate_pulses(void)
{
    uint16_t index;
    uint32_t previous_pulses = pulse_start_count;
    camera_trigger_init();
    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING; index++)
    {
        camera_trigger_on_bottle(0U, 0);
    }
    camera_trigger_update_position(2556U);
    assert(g_camera_trigger_count == CAMERA_TRIGGER_MAX_PENDING);
    assert(g_camera_trigger_pulse_queue_count == CAMERA_TRIGGER_MAX_PENDING - 1U);
    assert(g_camera_trigger_pulse_queue_depth == CAMERA_TRIGGER_MAX_PENDING - 1U);
    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING * 7U; index++)
    {
        camera_trigger_tick_1ms();
    }
    assert(pulse_start_count - previous_pulses == CAMERA_TRIGGER_MAX_PENDING);
    assert(g_camera_trigger_output_active == 0U);
    assert(g_camera_trigger_pulse_queue_depth == 0U);
}

int main(void)
{
    test_k0_starts_off_and_pulses_at_115_degrees();
    test_simultaneous_bottles_produce_separate_pulses();
    puts("camera trigger K0 output tests passed");
    return 0;
}
