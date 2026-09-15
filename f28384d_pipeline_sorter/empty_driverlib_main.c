//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D 编码器 3 CCS Watch 最小测试
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "encoder_watch.h"

void main(void)
{
    // 初始化 CPU、GPIO 和中断模块。
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // SysConfig 在这里初始化并启用编码器 1、2、3。
    Board_init();

    while (1)
    {
        // 当前只刷新编码器 3；在 CCS Watch 中观察 g_encoder3_* 变量。
        encoder_watch_update_encoder3();

        // 降低调试器刷新压力，不影响 eQEP 硬件计数。
        DEVICE_DELAY_US(1000U);
    }
}
