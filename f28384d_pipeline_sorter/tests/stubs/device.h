#ifndef TEST_STUB_DEVICE_H
#define TEST_STUB_DEVICE_H

#include <stdint.h>

// 与 CPU1_FLASH/syscfg/clocktree.h 生成的数值保持一致：
// XTAL 20 MHz × PLL 40 ÷ 2 ÷ 2 = 200 MHz。
#define DEVICE_SYSCLK_FREQ          200000000U
#define DEVICE_LSPCLK_FREQ          (DEVICE_SYSCLK_FREQ / 4)

void test_device_delay_us(uint32_t delay_us);

#define DEVICE_DELAY_US(delay_us)    test_device_delay_us(delay_us)

#endif
