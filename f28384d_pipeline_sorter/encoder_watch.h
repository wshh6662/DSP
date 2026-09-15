#ifndef ENCODER_WATCH_H
#define ENCODER_WATCH_H

#include <stdint.h>

// 编码器 1：在 CCS Watch 中添加这些变量即可观察实时状态。
extern volatile uint32_t g_encoder1_position;
extern volatile int16_t g_encoder1_direction;
extern volatile uint16_t g_encoder1_pin_a_level;
extern volatile uint16_t g_encoder1_pin_b_level;
extern volatile uint16_t g_encoder1_pin_z_level;
extern volatile uint32_t g_encoder1_index_event_count;
extern volatile uint32_t g_encoder1_index_latched_position;

// 编码器 2：在 CCS Watch 中添加这些变量即可观察实时状态。
extern volatile uint32_t g_encoder2_position;
extern volatile int16_t g_encoder2_direction;
extern volatile uint16_t g_encoder2_pin_a_level;
extern volatile uint16_t g_encoder2_pin_b_level;
extern volatile uint16_t g_encoder2_pin_z_level;
extern volatile uint32_t g_encoder2_index_event_count;
extern volatile uint32_t g_encoder2_index_latched_position;

// 编码器 3：当前 main() 使用这一组变量进行现场测试。
extern volatile uint32_t g_encoder3_position;
extern volatile int16_t g_encoder3_direction;
extern volatile uint16_t g_encoder3_pin_a_level;
extern volatile uint16_t g_encoder3_pin_b_level;
extern volatile uint16_t g_encoder3_pin_z_level;
extern volatile uint32_t g_encoder3_index_event_count;
extern volatile uint32_t g_encoder3_index_latched_position;

// 调用对应函数即可刷新该编码器的全部 CCS Watch 变量。
void encoder_watch_update_encoder1(void);
void encoder_watch_update_encoder2(void);
void encoder_watch_update_encoder3(void);

#endif
