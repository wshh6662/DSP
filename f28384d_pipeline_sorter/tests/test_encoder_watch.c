#include <assert.h>
#include <stdint.h>

#include "driverlib.h"
#include "board.h"
#include "encoder_watch.h"

static uint32_t stub_position[3] = {111U, 222U, 333U};
static int16_t stub_direction[3] = {1, -1, 1};
static uint16_t stub_interrupt_status[3] = {
    EQEP_INT_INDEX_EVNT_LATCH,
    0U,
    EQEP_INT_INDEX_EVNT_LATCH
};
static uint32_t stub_latched_position[3] = {101U, 202U, 303U};
static uint16_t stub_clear_count[3] = {0U, 0U, 0U};

static uint16_t encoder_index_from_base(uint32_t base)
{
    if (base == myEQEP1_BASE)
    {
        return 0U;
    }
    if (base == myEQEP2_BASE)
    {
        return 1U;
    }
    assert(base == myEQEP3_BASE);
    return 2U;
}

uint32_t EQEP_getPosition(uint32_t base)
{
    return stub_position[encoder_index_from_base(base)];
}

int16_t EQEP_getDirection(uint32_t base)
{
    return stub_direction[encoder_index_from_base(base)];
}

uint16_t EQEP_getInterruptStatus(uint32_t base)
{
    return stub_interrupt_status[encoder_index_from_base(base)];
}

uint32_t EQEP_getIndexPositionLatch(uint32_t base)
{
    return stub_latched_position[encoder_index_from_base(base)];
}

void EQEP_clearInterruptStatus(uint32_t base, uint16_t interrupt_status)
{
    uint16_t index = encoder_index_from_base(base);

    assert(interrupt_status == EQEP_INT_INDEX_EVNT_LATCH);
    stub_clear_count[index]++;
    stub_interrupt_status[index] = 0U;
}

uint32_t GPIO_readPin(uint32_t pin)
{
    switch (pin)
    {
        case myEQEP1_EQEPA_GPIO:     return 1U;
        case myEQEP1_EQEPB_GPIO:     return 0U;
        case myEQEP1_EQEPINDEX_GPIO: return 1U;
        case myEQEP2_EQEPA_GPIO:     return 0U;
        case myEQEP2_EQEPB_GPIO:     return 1U;
        case myEQEP2_EQEPINDEX_GPIO: return 0U;
        case myEQEP3_EQEPA_GPIO:     return 1U;
        case myEQEP3_EQEPB_GPIO:     return 1U;
        case myEQEP3_EQEPINDEX_GPIO: return 0U;
        default:                     assert(0); return 0U;
    }
}

int main(void)
{
    encoder_watch_update_encoder1();
    assert(g_encoder1_position == 111U);
    assert(g_encoder1_direction == 1);
    assert(g_encoder1_pin_a_level == 1U);
    assert(g_encoder1_pin_b_level == 0U);
    assert(g_encoder1_pin_z_level == 1U);
    assert(g_encoder1_index_event_count == 1U);
    assert(g_encoder1_index_latched_position == 101U);
    assert(stub_clear_count[0] == 1U);

    encoder_watch_update_encoder2();
    assert(g_encoder2_position == 222U);
    assert(g_encoder2_direction == -1);
    assert(g_encoder2_pin_a_level == 0U);
    assert(g_encoder2_pin_b_level == 1U);
    assert(g_encoder2_pin_z_level == 0U);
    assert(g_encoder2_index_event_count == 0U);
    assert(stub_clear_count[1] == 0U);

    encoder_watch_update_encoder3();
    assert(g_encoder3_position == 333U);
    assert(g_encoder3_direction == 1);
    assert(g_encoder3_pin_a_level == 1U);
    assert(g_encoder3_pin_b_level == 1U);
    assert(g_encoder3_pin_z_level == 0U);
    assert(g_encoder3_index_event_count == 1U);
    assert(g_encoder3_index_latched_position == 303U);
    assert(stub_clear_count[2] == 1U);

    encoder_watch_update_encoder3();
    assert(g_encoder3_index_event_count == 1U);
    assert(stub_clear_count[2] == 1U);

    return 0;
}
