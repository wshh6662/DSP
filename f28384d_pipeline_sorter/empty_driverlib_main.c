//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D 转盘转速（编码器 3，M 法）+ 光电检测 TCP 测试
//#############################################################################
//
// 转盘转速由 turntable_speed.c 里的 CPU Timer0 10 ms 中断独立测量，
// 主循环只负责取速度快照、刷新 CCS Watch 变量和维护非阻塞 TCP Server。
// 因此 W5500 收发造成的轮询抖动不会影响 10 ms 采样周期。
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"
#include "encoder_watch.h"
#include "photoelectric_sensor.h"
#include "photoelectric_tcp_server.h"
#include "turntable_speed.h"

#define NETWORK_RETRY_TICKS    100U

void main(void)
{
    int network_ready;
    uint16_t network_retry_ticks = 0U;
    int32_t speed_snapshot;

    // 初始化 CPU、GPIO 和中断模块。
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // SysConfig 在这里初始化并启用编码器 1、2、3。
    Board_init();
    C2000Ware_libraries_init();

    // IN0 对应 GPIO26。NPN 常开传感器检测到物体时输入为低电平。
    photoelectric_sensor_init();

    // 启动 10 ms 的 CPU Timer0，用编码器 3 做 M 法测速。
    turntable_speed_init();

    EINT;
    ERTM;

    // W5500 使用 192.168.1.20:2000；初始化失败时主循环每约 1 秒重试。
    network_ready = photoelectric_tcp_server_init();

    while (1)
    {
        // 刷新编码器 3 和光电输入的 CCS Watch 变量。
        encoder_watch_update_encoder3();
        photoelectric_sensor_update();

        // 每轮只取一次转速快照，传给 TCP 模块，避免它重复读取正在被 ISR 更新的变量。
        speed_snapshot = turntable_speed_get_rpm_x100();

        if (network_ready == 0)
        {
            // 每 50 ms 向已连接客户端发送一行 "speed=xx.xx,photo=n\r\n"。
            photoelectric_tcp_server_poll(g_photoelectric_detected,
                                          speed_snapshot);
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

        // 10 ms 周期只用来降低轮询压力，测速精度由 Timer0 中断保证。
        DEVICE_DELAY_US(10000U);
    }
}
