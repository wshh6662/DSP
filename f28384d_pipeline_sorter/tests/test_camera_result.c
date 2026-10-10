#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "camera_result.h"

static void feed(const char *text)
{
    while (*text != '\0')
    {
        camera_result_feed_byte((uint8_t)*text);
        text++;
    }
}

int main(void)
{
    camera_result_init();
    feed("tcm");
    assert(g_camera_inspection_result == CAMERA_INSPECTION_NONE);
    feed("c1,");
    assert(g_camera_inspection_result == CAMERA_INSPECTION_GOOD);
    assert(g_camera_inspection_result_count == 1U);

    feed("noise,tcmc2,");
    assert(g_camera_inspection_result == CAMERA_INSPECTION_BAD);
    assert(g_camera_inspection_result_count == 2U);

    feed("tcmc1");
    assert(g_camera_inspection_result_count == 2U);
    camera_result_reset_partial_frame();
    feed(",");
    assert(g_camera_inspection_result_count == 2U);

    feed("tcmc3,tcmc1,");
    assert(g_camera_inspection_result == CAMERA_INSPECTION_GOOD);
    assert(g_camera_inspection_result_count == 3U);
    assert(g_camera_inspection_invalid_count == 1U);

    puts("camera result tests passed");
    return 0;
}
