#ifndef TURNTABLE_ANGLE_H
#define TURNTABLE_ANGLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 编码器每圈 4096 个计数，角度以 0.01 度为单位返回。
#define TURNTABLE_ANGLE_COUNTS_PER_REVOLUTION   4096L

// CCS Watch 变量。
extern volatile int32_t g_turntable_angle_accumulated_counts; // 复位零点后的单圈位置计数，范围 0～4095

// 把当前软件角度清零。
void turntable_angle_init(void);

// 输入一次编码器带方向计数差，并在 0～4095 范围内循环。
void turntable_angle_update_delta(int32_t count_delta);

// 读取当前单圈角度，单位 0.01 度；范围 0～35991。
int32_t turntable_angle_get_degrees_x100(void);

#ifdef __cplusplus
}
#endif

#endif
