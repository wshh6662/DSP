//#############################################################################
// FILE:   turntable_angle.c
// TITLE:  F28384D 转盘单圈实时角度
//#############################################################################

#include "driverlib.h"
#include "turntable_angle.h"

volatile int32_t g_turntable_angle_accumulated_counts = 0; // 复位零点后的单圈位置计数，范围 0～3999

void turntable_angle_init(void)
{
    g_turntable_angle_accumulated_counts = 0;
}

void turntable_angle_update_delta(int32_t count_delta)
{
    g_turntable_angle_accumulated_counts += count_delta;

    while (g_turntable_angle_accumulated_counts >=
           TURNTABLE_ANGLE_COUNTS_PER_REVOLUTION)
    {
        g_turntable_angle_accumulated_counts -=
            TURNTABLE_ANGLE_COUNTS_PER_REVOLUTION;
    }

    while (g_turntable_angle_accumulated_counts < 0)
    {
        g_turntable_angle_accumulated_counts +=
            TURNTABLE_ANGLE_COUNTS_PER_REVOLUTION;
    }
}

int32_t turntable_angle_get_degrees_x100(void)
{
    int32_t position_counts;
    bool interrupts_were_disabled;

    interrupts_were_disabled = Interrupt_disableGlobal();
    position_counts = g_turntable_angle_accumulated_counts;
    if (interrupts_were_disabled == false)
    {
        (void)Interrupt_enableGlobal();
    }

    return (position_counts * 36000L) /
           TURNTABLE_ANGLE_COUNTS_PER_REVOLUTION;
}
