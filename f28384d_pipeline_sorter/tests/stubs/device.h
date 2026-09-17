#ifndef TEST_STUB_DEVICE_H
#define TEST_STUB_DEVICE_H

#include <stdint.h>

void test_device_delay_us(uint32_t delay_us);

#define DEVICE_DELAY_US(delay_us)    test_device_delay_us(delay_us)

#endif
