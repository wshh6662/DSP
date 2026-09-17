#ifndef TEST_STUB_DRIVERLIB_H
#define TEST_STUB_DRIVERLIB_H

#include <stdint.h>

#define EQEP_INT_INDEX_EVNT_LATCH    0x0400U

#define GPIO_DIR_MODE_IN             0U
#define GPIO_PIN_TYPE_PULLUP         1U
#define GPIO_QUAL_SYNC               0U

uint32_t EQEP_getPosition(uint32_t base);
int16_t EQEP_getDirection(uint32_t base);
uint16_t EQEP_getInterruptStatus(uint32_t base);
uint32_t EQEP_getIndexPositionLatch(uint32_t base);
void EQEP_clearInterruptStatus(uint32_t base, uint16_t interrupt_status);
uint32_t GPIO_readPin(uint32_t pin);
void GPIO_setDirectionMode(uint32_t pin, uint32_t direction);
void GPIO_setPadConfig(uint32_t pin, uint32_t pin_type);
void GPIO_setQualificationMode(uint32_t pin, uint32_t qualification);
void GPIO_writePin(uint32_t pin, uint32_t value);
void SPI_writeDataBlockingNonFIFO(uint32_t base, uint16_t data);
uint16_t SPI_readDataBlockingNonFIFO(uint32_t base);

#endif
