//#############################################################################
// FILE:   camera_result.c
// TITLE:  TCP 字节流中的相机判定结果解析
//#############################################################################

#include "camera_result.h"

volatile uint16_t g_camera_inspection_result = CAMERA_INSPECTION_NONE;
volatile uint32_t g_camera_inspection_result_count = 0U;
volatile uint32_t g_camera_inspection_invalid_count = 0U;

static uint16_t camera_result_state = 0U;
static uint16_t camera_result_candidate = CAMERA_INSPECTION_NONE;

void camera_result_init(void)
{
    g_camera_inspection_result = CAMERA_INSPECTION_NONE;
    g_camera_inspection_result_count = 0U;
    g_camera_inspection_invalid_count = 0U;
    camera_result_state = 0U;
    camera_result_candidate = CAMERA_INSPECTION_NONE;
}

void camera_result_reset_partial_frame(void)
{
    camera_result_state = 0U;
    camera_result_candidate = CAMERA_INSPECTION_NONE;
}

void camera_result_feed_byte(camera_result_octet_t value)
{
    static const camera_result_octet_t prefix[4] = {'t', 'c', 'm', 'c'};

    if (camera_result_state < 4U)
    {
        if (value == prefix[camera_result_state])
        {
            camera_result_state++;
        }
        else
        {
            camera_result_state = (value == (camera_result_octet_t)'t') ? 1U : 0U;
        }
        return;
    }

    if (camera_result_state == 4U)
    {
        if (value == (camera_result_octet_t)'1')
        {
            camera_result_candidate = CAMERA_INSPECTION_GOOD;
            camera_result_state = 5U;
        }
        else if (value == (camera_result_octet_t)'2')
        {
            camera_result_candidate = CAMERA_INSPECTION_BAD;
            camera_result_state = 5U;
        }
        else
        {
            g_camera_inspection_invalid_count++;
            camera_result_reset_partial_frame();
        }
        return;
    }

    if (value == (camera_result_octet_t)',')
    {
        g_camera_inspection_result = camera_result_candidate;
        g_camera_inspection_result_count++;
    }
    camera_result_reset_partial_frame();
}
