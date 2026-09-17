#include "turntable_angle.h"

#include "driverlib.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t stub_disable_global_count = 0U;
static uint32_t stub_enable_global_count = 0U;

bool Interrupt_disableGlobal(void)
{
    stub_disable_global_count++;
    return false;
}

bool Interrupt_enableGlobal(void)
{
    stub_enable_global_count++;
    return false;
}

int main(void)
{
    turntable_angle_init();

    assert(g_turntable_angle_accumulated_counts == 0);
    assert(turntable_angle_get_degrees_x100() == 0);

    // 1024 / 4096 圈正好是 90.00 度。
    turntable_angle_update_delta(1024);
    assert(g_turntable_angle_accumulated_counts == 1024);
    assert(turntable_angle_get_degrees_x100() == 9000);

    // 一圈最后一个计数对应 359.91 度，再前进一个计数回到 0.00 度。
    turntable_angle_update_delta(3071);
    assert(g_turntable_angle_accumulated_counts == 4095);
    assert(turntable_angle_get_degrees_x100() == 35991);
    turntable_angle_update_delta(1);
    assert(g_turntable_angle_accumulated_counts == 0);
    assert(turntable_angle_get_degrees_x100() == 0);

    // 从零位反转一个计数，应当回绕到本圈的 359.91 度。
    turntable_angle_update_delta(-1);
    assert(g_turntable_angle_accumulated_counts == 4095);
    assert(turntable_angle_get_degrees_x100() == 35991);

    // 收到复位命令后，软件角度计数和角度都回到零。
    turntable_angle_init();
    assert(g_turntable_angle_accumulated_counts == 0);
    assert(turntable_angle_get_degrees_x100() == 0);

    assert(stub_disable_global_count != 0U);
    assert(stub_disable_global_count == stub_enable_global_count);

    puts("Turntable angle tests passed.");
    return 0;
}
