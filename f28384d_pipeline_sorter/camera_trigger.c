//#############################################################################
// FILE:   camera_trigger.c
// TITLE:  按每个瓶子的入瓶位置独立计算实测相机触发点
//#############################################################################

#include "camera_trigger.h"

#if CAMERA_TRIGGER_OUTPUT_ENABLED && !defined(CAMERA_TRIGGER_HOST_TEST)
#include "driverlib.h"
#endif

typedef struct
{
    uint32_t previous_position;
    int32_t travel_count;
    uint16_t active;
} camera_trigger_bottle_t;

volatile uint32_t g_camera_trigger_count = 0U;
volatile uint32_t g_camera_trigger_overflow_count = 0U;
volatile uint16_t g_camera_trigger_output_active = 0U;

static camera_trigger_bottle_t camera_trigger_bottles[CAMERA_TRIGGER_MAX_PENDING];
static uint16_t camera_trigger_pulse_ticks = 0U;
static uint16_t camera_trigger_gap_ticks = 0U;
static uint16_t camera_trigger_queued_pulses = 0U;

static void camera_trigger_write_output(uint16_t active)
{
    g_camera_trigger_output_active = active;
#if CAMERA_TRIGGER_OUTPUT_ENABLED && !defined(CAMERA_TRIGGER_HOST_TEST)
    // K0 光耦输入低电平有效；GPIO0 拉低时 K0 向相机输入端输出 +24 V。
    GPIO_writePin(CAMERA_TRIGGER_OUTPUT_GPIO, (active != 0U) ? 0U : 1U);
#endif
}

static void camera_trigger_start_pulse(void)
{
    camera_trigger_write_output(1U);
    camera_trigger_pulse_ticks = CAMERA_TRIGGER_PULSE_TICKS;
}

static int32_t camera_trigger_wrap_delta(uint32_t current,
                                         uint32_t previous)
{
    int32_t delta = (int32_t)current - (int32_t)previous;

    if (delta > (int32_t)(CAMERA_TRIGGER_COUNTS_PER_REVOLUTION / 2U))
    {
        delta -= (int32_t)CAMERA_TRIGGER_COUNTS_PER_REVOLUTION;
    }
    else if (delta < -(int32_t)(CAMERA_TRIGGER_COUNTS_PER_REVOLUTION / 2U))
    {
        delta += (int32_t)CAMERA_TRIGGER_COUNTS_PER_REVOLUTION;
    }
    return delta;
}

void camera_trigger_init(void)
{
    uint16_t index;

#if CAMERA_TRIGGER_OUTPUT_ENABLED && !defined(CAMERA_TRIGGER_HOST_TEST)
    GPIO_setPinConfig(CAMERA_TRIGGER_OUTPUT_PIN_CONFIG);
    GPIO_writePin(CAMERA_TRIGGER_OUTPUT_GPIO, 1U);
    GPIO_setPadConfig(CAMERA_TRIGGER_OUTPUT_GPIO, GPIO_PIN_TYPE_OD);
    GPIO_setDirectionMode(CAMERA_TRIGGER_OUTPUT_GPIO, GPIO_DIR_MODE_OUT);
#endif

    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING; index++)
    {
        camera_trigger_bottles[index].previous_position = 0U;
        camera_trigger_bottles[index].travel_count = 0L;
        camera_trigger_bottles[index].active = 0U;
    }
    g_camera_trigger_count = 0U;
    g_camera_trigger_overflow_count = 0U;
    camera_trigger_pulse_ticks = 0U;
    camera_trigger_gap_ticks = 0U;
    camera_trigger_queued_pulses = 0U;
    camera_trigger_write_output(0U);
}

void camera_trigger_on_bottle(uint32_t encoder_position)
{
    uint16_t index;

    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING; index++)
    {
        if (camera_trigger_bottles[index].active == 0U)
        {
            camera_trigger_bottles[index].previous_position = encoder_position;
            camera_trigger_bottles[index].travel_count = 0L;
            camera_trigger_bottles[index].active = 1U;
            return;
        }
    }
    g_camera_trigger_overflow_count++;
}

void camera_trigger_update_position(uint32_t encoder_position)
{
    uint16_t index;
    camera_trigger_bottle_t *bottle;

    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING; index++)
    {
        bottle = &camera_trigger_bottles[index];
        if (bottle->active == 0U)
        {
            continue;
        }

        // 映射为生产前进量；倒退抵消前进，不能取绝对值累计。
        bottle->travel_count += CAMERA_TRIGGER_FORWARD_DIRECTION *
            camera_trigger_wrap_delta(encoder_position, bottle->previous_position);
        bottle->previous_position = encoder_position;

        if (bottle->travel_count >= CAMERA_TRIGGER_DISTANCE_COUNTS)
        {
            bottle->active = 0U;
            g_camera_trigger_count++;
            if ((camera_trigger_pulse_ticks == 0U) &&
                (camera_trigger_gap_ticks == 0U))
            {
                camera_trigger_start_pulse();
            }
            else
            {
                camera_trigger_queued_pulses++;
            }
        }
    }
}

void camera_trigger_tick_1ms(void)
{
    if (camera_trigger_pulse_ticks != 0U)
    {
        camera_trigger_pulse_ticks--;
        if (camera_trigger_pulse_ticks == 0U)
        {
            camera_trigger_write_output(0U);
            camera_trigger_gap_ticks = 1U;
        }
    }
    else if (camera_trigger_gap_ticks != 0U)
    {
        camera_trigger_gap_ticks--;
    }
    else if (camera_trigger_queued_pulses != 0U)
    {
        camera_trigger_queued_pulses--;
        camera_trigger_start_pulse();
    }
}

void camera_trigger_reset_pending(void)
{
    uint16_t index;

    for (index = 0U; index < CAMERA_TRIGGER_MAX_PENDING; index++)
    {
        camera_trigger_bottles[index].active = 0U;
    }
    camera_trigger_pulse_ticks = 0U;
    camera_trigger_gap_ticks = 0U;
    camera_trigger_queued_pulses = 0U;
    camera_trigger_write_output(0U);
}
