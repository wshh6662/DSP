//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D 编码器 3 与光电检测 TCP 测试
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"
#include "encoder_watch.h"
#include "photoelectric_sensor.h"
#include "photoelectric_tcp_server.h"

#define NETWORK_RETRY_TICKS    100U

void main(void)
{
    int network_ready;
    uint16_t network_retry_ticks = 0U;

    // 初始化 CPU、GPIO 和中断模块。
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // SysConfig 在这里初始化并启用编码器 1、2、3。
    Board_init();
    C2000Ware_libraries_init();

    // IN0 对应 GPIO26。NPN 常开传感器检测到物体时输入为低电平。
    photoelectric_sensor_init();

    // W5500 使用 192.168.1.20:2000；初始化失败时主循环每约 1 秒重试。
    network_ready = photoelectric_tcp_server_init();

    while (1)
    {
        // 刷新编码器 3 和光电输入的 CCS Watch 变量。
        encoder_watch_update_encoder3();
        photoelectric_sensor_update();

        if (network_ready == 0)
        {
            // 每 50 ms 向已连接客户端发送“1\r\n”或“0\r\n”。
            photoelectric_tcp_server_poll(g_photoelectric_detected);
        }
        else
        {
            network_retry_ticks++;
            if (network_retry_ticks >= NETWORK_RETRY_TICKS)
            {
                network_retry_ticks = 0U;
                network_ready = photoelectric_tcp_server_init();
            }
        }

        // 10 ms 周期兼顾 CCS Watch 刷新和 TCP 非阻塞轮询。
        DEVICE_DELAY_US(10000U);
    }
}
