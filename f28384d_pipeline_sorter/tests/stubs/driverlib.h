#ifndef TEST_STUB_DRIVERLIB_H
#define TEST_STUB_DRIVERLIB_H

#include <stdbool.h>
#include <stdint.h>

#define EQEP_INT_INDEX_EVNT_LATCH    0x0400U

#define GPIO_DIR_MODE_IN             0U
#define GPIO_PIN_TYPE_PULLUP         1U
#define GPIO_QUAL_SYNC               0U

// CPU Timer0（F2838x：CPUTIMER0_BASE = 0x00000C00，PIE 1.7 = INT_TIMER0）
#define CPUTIMER0_BASE               0x00000C00U
#define INT_TIMER0                   0x00260107U
#define INTERRUPT_ACK_GROUP1         0x1U

#define CPUTIMER_EMULATIONMODE_STOPAFTERNEXTDECREMENT    0x0000U
#define CPUTIMER_EMULATIONMODE_STOPATZERO                0x0400U
#define CPUTIMER_EMULATIONMODE_RUNFREE                   0x0800U

uint32_t EQEP_getPosition(uint32_t base);
void EQEP_setPosition(uint32_t base, uint32_t position);
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

void CPUTimer_stopTimer(uint32_t base);
void CPUTimer_setPreScaler(uint32_t base, uint16_t prescaler);
void CPUTimer_setPeriod(uint32_t base, uint32_t period_count);
void CPUTimer_setEmulationMode(uint32_t base, uint32_t emulation_mode);
void CPUTimer_reloadTimerCounter(uint32_t base);
void CPUTimer_clearOverflowFlag(uint32_t base);
void CPUTimer_enableInterrupt(uint32_t base);
void CPUTimer_startTimer(uint32_t base);

void Interrupt_register(uint32_t interrupt_number, void (*handler)(void));
void Interrupt_enable(uint32_t interrupt_number);
void Interrupt_clearACKGroup(uint16_t ack_group);
bool Interrupt_disableGlobal(void);
bool Interrupt_enableGlobal(void);

#endif
