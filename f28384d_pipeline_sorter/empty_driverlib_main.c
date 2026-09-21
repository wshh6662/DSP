//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D 编码器转速 + 光电转速 + 当前角度 + 光电检测 TCP 测试
//#############################################################################
//
// CPU Timer0 每 1 ms 中断一次：
//   每 1 ms ：刷新光电边沿，推进单瓶遮挡时间测速；
//   每 10 ms：采一次编码器 3，做 M 法测速并更新单圈角度。
// 主循环只负责取快照、刷新 CCS Watch 变量和维护非阻塞 TCP Server。
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"
#include "encoder_watch.h"
#include "photoelectric_sensor.h"
#include "photoelectric_speed.h"
#include "photoelectric_tcp_server.h"
#include "turntable_angle.h"
#include "turntable_speed.h"

#define NETWORK_RETRY_TICKS    100U

void main(void)
{
    int network_ready;
    uint16_t network_retry_ticks = 0U;
    int32_t encoder_speed_snapshot;
    int32_t optical_speed_snapshot;
    int32_t angle_snapshot_x100;
    uint16_t reset_requested;

    // 初始化 CPU、GPIO 和中断模块。
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // SysConfig 在这里初始化并启用编码器 1、2、3。
    Board_init();
    C2000Ware_libraries_init();

    // IN0 对应 GPIO26。NPN 常开传感器检测到物体时输入为低电平。
    photoelectric_sensor_init();

    // 清零光电测速状态；光电采样与遮挡计时都在 Timer0 中断里完成。
    photoelectric_speed_init();

    // 建立当前角度的零点。
    turntable_angle_init();

    // 启动 1 ms Timer0：编码器测速、当前角度、光电边沿与遮挡测速共用此时基。
    turntable_speed_init();

    EINT;
    ERTM;

    // W5500 使用 192.168.1.20:2000；初始化失败时主循环每约 1 秒重试。
    network_ready = photoelectric_tcp_server_init();

    while (1)
    {
        // 刷新编码器 3 的 CCS Watch 变量。
        encoder_watch_update_encoder3();

        // 每轮各取一次快照，避免 TCP 模块自己反复读取正在被中断更新的变量。
        encoder_speed_snapshot = turntable_speed_get_rpm_x100();
        optical_speed_snapshot = photoelectric_speed_get_rpm_x100();
        angle_snapshot_x100 = turntable_angle_get_degrees_x100();

        if (network_ready == 0)
        {
            // 每约 1 秒发送一行
            // "enc_rpm=...,opt_rpm=...,obj=n,angle=..."；收到 ASCII "re" 时返回复位请求。
            reset_requested = (uint16_t)photoelectric_tcp_server_poll(
                g_photoelectric_detected,
                encoder_speed_snapshot,
                optical_speed_snapshot,
                angle_snapshot_x100);

            if (reset_requested != 0U)
            {
                // 同步清零 eQEP3、测速基准、当前角度和光电测速状态，
                // 防止复位后速度或角度跳变。
                turntable_speed_reset_encoder3();
            }
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

        // 10 ms 周期只用来降低轮询压力，所有测量的精度都由 Timer0 中断保证。
        DEVICE_DELAY_US(10000U);
    }
}
