//#############################################################################
// FILE:   camera_trigger.h
// TITLE:  光电入瓶后正向行进115度触发相机
//#############################################################################

#ifndef CAMERA_TRIGGER_H
#define CAMERA_TRIGGER_H

#include <stdint.h>

// PCB3: K0 = GPIO0，OCOM0 接 +24 V；GPIO0 低电平时光耦导通，K0 输出 +24 V。
#ifndef CAMERA_TRIGGER_OUTPUT_ENABLED
#define CAMERA_TRIGGER_OUTPUT_ENABLED 1U
#endif

#if CAMERA_TRIGGER_OUTPUT_ENABLED
#ifndef CAMERA_TRIGGER_OUTPUT_GPIO
#define CAMERA_TRIGGER_OUTPUT_GPIO 0U
#endif
#ifndef CAMERA_TRIGGER_OUTPUT_PIN_CONFIG
#define CAMERA_TRIGGER_OUTPUT_PIN_CONFIG GPIO_0_GPIO0
#endif
#endif

#define CAMERA_TRIGGER_COUNTS_PER_REVOLUTION 8000U
// eQEP3 已交换A/B，正常生产计数递增；倒退仍抵消前进量。
#ifndef CAMERA_TRIGGER_FORWARD_DIRECTION
#define CAMERA_TRIGGER_FORWARD_DIRECTION    1L
#endif
#if (CAMERA_TRIGGER_FORWARD_DIRECTION != 1L) && (CAMERA_TRIGGER_FORWARD_DIRECTION != -1L)
#error CAMERA_TRIGGER_FORWARD_DIRECTION must be 1 or -1
#endif
// 从光电进入沿锁存位置，前进115度：8000 * 115 / 360，四舍五入为2556。
#define CAMERA_TRIGGER_DISTANCE_COUNTS      2556L
#define CAMERA_TRIGGER_PULSE_TICKS           5U
#define CAMERA_TRIGGER_MAX_PENDING           8U

extern volatile uint32_t g_camera_trigger_count;          // 已到达拍照位置的瓶子数量
extern volatile uint32_t g_camera_trigger_overflow_count; // 队列满时丢弃的入瓶事件
extern volatile uint16_t g_camera_trigger_output_active;  // 逻辑触发脉冲当前状态

void camera_trigger_init(void);
void camera_trigger_on_bottle(uint32_t encoder_position);
void camera_trigger_update_position(uint32_t encoder_position);
void camera_trigger_tick_1ms(void);
void camera_trigger_reset_pending(void);

#endif
