//#############################################################################
// FILE:   encoder_watch.c
// TITLE:  F28384D 三路编码器 CCS Watch 数据采集
//#############################################################################

#include "driverlib.h"
#include "board.h"
#include "encoder_watch.h"

volatile uint32_t g_encoder1_position = 0U;
volatile int16_t g_encoder1_direction = 0;
volatile uint16_t g_encoder1_pin_a_level = 0U;
volatile uint16_t g_encoder1_pin_b_level = 0U;
volatile uint16_t g_encoder1_pin_z_level = 0U;
volatile uint32_t g_encoder1_index_event_count = 0U;
volatile uint32_t g_encoder1_index_latched_position = 0U;

volatile uint32_t g_encoder2_position = 0U;
volatile int16_t g_encoder2_direction = 0;
volatile uint16_t g_encoder2_pin_a_level = 0U;
volatile uint16_t g_encoder2_pin_b_level = 0U;
volatile uint16_t g_encoder2_pin_z_level = 0U;
volatile uint32_t g_encoder2_index_event_count = 0U;
volatile uint32_t g_encoder2_index_latched_position = 0U;

volatile uint32_t g_encoder3_position = 0U;
volatile int16_t g_encoder3_direction = 0;
volatile uint16_t g_encoder3_pin_a_level = 0U;
volatile uint16_t g_encoder3_pin_b_level = 0U;
volatile uint16_t g_encoder3_pin_z_level = 0U;
volatile uint32_t g_encoder3_index_event_count = 0U;
volatile uint32_t g_encoder3_index_latched_position = 0U;

// 刷新编码器 1 的位置、方向、A/B/Z 电平和 Z 相事件信息。
void encoder_watch_update_encoder1(void)
{
    g_encoder1_position = EQEP_getPosition(myEQEP1_BASE);
    g_encoder1_direction = EQEP_getDirection(myEQEP1_BASE);
    g_encoder1_pin_a_level = (uint16_t)GPIO_readPin(myEQEP1_EQEPA_GPIO);
    g_encoder1_pin_b_level = (uint16_t)GPIO_readPin(myEQEP1_EQEPB_GPIO);
    g_encoder1_pin_z_level = (uint16_t)GPIO_readPin(myEQEP1_EQEPINDEX_GPIO);

    if ((EQEP_getInterruptStatus(myEQEP1_BASE) &
         EQEP_INT_INDEX_EVNT_LATCH) != 0U)
    {
        g_encoder1_index_latched_position =
            EQEP_getIndexPositionLatch(myEQEP1_BASE);
        g_encoder1_index_event_count++;
        EQEP_clearInterruptStatus(myEQEP1_BASE, EQEP_INT_INDEX_EVNT_LATCH);
    }
}

// 刷新编码器 2 的位置、方向、A/B/Z 电平和 Z 相事件信息。
void encoder_watch_update_encoder2(void)
{
    g_encoder2_position = EQEP_getPosition(myEQEP2_BASE);
    g_encoder2_direction = EQEP_getDirection(myEQEP2_BASE);
    g_encoder2_pin_a_level = (uint16_t)GPIO_readPin(myEQEP2_EQEPA_GPIO);
    g_encoder2_pin_b_level = (uint16_t)GPIO_readPin(myEQEP2_EQEPB_GPIO);
    g_encoder2_pin_z_level = (uint16_t)GPIO_readPin(myEQEP2_EQEPINDEX_GPIO);

    if ((EQEP_getInterruptStatus(myEQEP2_BASE) &
         EQEP_INT_INDEX_EVNT_LATCH) != 0U)
    {
        g_encoder2_index_latched_position =
            EQEP_getIndexPositionLatch(myEQEP2_BASE);
        g_encoder2_index_event_count++;
        EQEP_clearInterruptStatus(myEQEP2_BASE, EQEP_INT_INDEX_EVNT_LATCH);
    }
}

// 刷新编码器 3 的位置、方向、A/B/Z 电平和 Z 相事件信息。
void encoder_watch_update_encoder3(void)
{
    g_encoder3_position = EQEP_getPosition(myEQEP3_BASE);
    g_encoder3_direction = EQEP_getDirection(myEQEP3_BASE);
    g_encoder3_pin_a_level = (uint16_t)GPIO_readPin(myEQEP3_EQEPA_GPIO);
    g_encoder3_pin_b_level = (uint16_t)GPIO_readPin(myEQEP3_EQEPB_GPIO);
    g_encoder3_pin_z_level = (uint16_t)GPIO_readPin(myEQEP3_EQEPINDEX_GPIO);

    if ((EQEP_getInterruptStatus(myEQEP3_BASE) &
         EQEP_INT_INDEX_EVNT_LATCH) != 0U)
    {
        g_encoder3_index_latched_position =
            EQEP_getIndexPositionLatch(myEQEP3_BASE);
        g_encoder3_index_event_count++;
        EQEP_clearInterruptStatus(myEQEP3_BASE, EQEP_INT_INDEX_EVNT_LATCH);
    }
}
