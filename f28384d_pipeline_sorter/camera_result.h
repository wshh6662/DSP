//#############################################################################
// FILE:   camera_result.h
// TITLE:  电脑2相机结果报文 tcmc1,/tcmc2, 解析
//#############################################################################

#ifndef CAMERA_RESULT_H
#define CAMERA_RESULT_H

#include <stdint.h>

#if defined(__TMS320C2000__)
typedef uint16_t camera_result_octet_t;
#else
typedef uint8_t camera_result_octet_t;
#endif

#define CAMERA_INSPECTION_NONE 0U
#define CAMERA_INSPECTION_GOOD 1U
#define CAMERA_INSPECTION_BAD  2U

extern volatile uint16_t g_camera_inspection_result;       // 最近一次完整结果
extern volatile uint32_t g_camera_inspection_result_count; // 完整结果累计数
extern volatile uint32_t g_camera_inspection_invalid_count; // 非 1/2 的结果数

void camera_result_init(void);
void camera_result_feed_byte(camera_result_octet_t value);
void camera_result_reset_partial_frame(void);

#endif
