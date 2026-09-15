#ifndef TEST_STUB_DRIVERLIB_H
#define TEST_STUB_DRIVERLIB_H

#include <stdint.h>

#define EQEP_INT_INDEX_EVNT_LATCH    0x0400U

uint32_t EQEP_getPosition(uint32_t base);
int16_t EQEP_getDirection(uint32_t base);
uint16_t EQEP_getInterruptStatus(uint32_t base);
uint32_t EQEP_getIndexPositionLatch(uint32_t base);
void EQEP_clearInterruptStatus(uint32_t base, uint16_t interrupt_status);
uint32_t GPIO_readPin(uint32_t pin);

#endif
