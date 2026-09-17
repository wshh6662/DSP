#ifndef TEST_STUB_WIZCHIP_CONF_H
#define TEST_STUB_WIZCHIP_CONF_H

#include <stdint.h>

#define CW_INIT_WIZCHIP    0U
#define CN_SET_NETINFO     1U

typedef struct
{
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
} wiz_NetInfo;

void reg_wizchip_cs_cbfunc(void (*select_callback)(void),
                           void (*deselect_callback)(void));
void reg_wizchip_spi_cbfunc(uint8_t (*read_callback)(void),
                            void (*write_callback)(uint8_t));
void reg_wizchip_spiburst_cbfunc(void (*read_callback)(uint8_t *, uint16_t),
                                 void (*write_callback)(uint8_t *, uint16_t));
int8_t ctlwizchip(uint8_t control, void *argument);
int8_t ctlnetwork(uint8_t control, void *argument);

#endif
