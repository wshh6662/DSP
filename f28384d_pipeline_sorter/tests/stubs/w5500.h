#ifndef TEST_STUB_W5500_H
#define TEST_STUB_W5500_H

#include <stdint.h>

#define Sn_IR_CON    0x01U

uint8_t getVERSIONR(void);
uint8_t getSn_SR(uint8_t socket_number);
uint8_t getSn_IR(uint8_t socket_number);
void setSn_IR(uint8_t socket_number, uint8_t value);
void setSn_KPALVTR(uint8_t socket_number, uint8_t value);

#endif
