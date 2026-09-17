#ifndef TEST_STUB_SOCKET_H
#define TEST_STUB_SOCKET_H

#include <stdint.h>

#define SOCK_CLOSED         0x00U
#define SOCK_INIT           0x13U
#define SOCK_LISTEN         0x14U
#define SOCK_ESTABLISHED    0x17U
#define SOCK_CLOSE_WAIT     0x1CU

#define Sn_MR_TCP           0x01U
#define SF_IO_NONBLOCK      0x01U
#define SOCK_OK             1
#define SOCK_BUSY           0

int8_t socket(uint8_t socket_number, uint8_t protocol,
              uint16_t port, uint8_t flags);
int8_t listen(uint8_t socket_number);
int8_t disconnect(uint8_t socket_number);
int8_t close(uint8_t socket_number);
int32_t send(uint8_t socket_number, uint8_t *buffer, uint16_t length);

#endif
