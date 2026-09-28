//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D 编码器 + 光电检测 + TCP + RS485 Modbus RTU
//#############################################################################
//
// CPU Timer0 每 1 ms 中断一次：
//   每 1 ms ：刷新光电边沿；
//   每 10 ms：采一次编码器 3，做 M 法测速并更新单圈角度。
// 主循环负责取快照、维护 TCP 和轮询 RS485。
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"
#include "modbus_tcp_server.h"
#include "photoelectric_sensor.h"
#include "photoelectric_tcp_server.h"
#include "rs485_modbus_port.h"
#include "turntable_angle.h"
#include "turntable_speed.h"

#define NETWORK_RETRY_TICKS    1000U

void main(void)
{
    int network_ready;
    uint16_t network_retry_ticks = 0U;
    int32_t encoder_speed_snapshot;
    int32_t angle_snapshot_x100;

    // 初始化 CPU、GPIO 和中断模块。
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // SysConfig 在这里初始化并启用编码器 1、2、3。
    Board_init();
    C2000Ware_libraries_init();

    // 清空 HMI 使用的保持寄存器和线圈。该存储区位于专用 RAM 段，
    // 不能依赖 C 运行时自动清零。
    ModbusTcp_ResetStorage();

    // 上层板 RS485 接口：SCIB GPIO54/55，GPIO133 控制方向，115200 8N1。
    rs485_modbus_port_init();

    // IN0 对应 GPIO26。NPN 常开传感器检测到物体时输入为低电平。
    photoelectric_sensor_init();

    // 建立当前角度的零点。
    turntable_angle_init();

    // 启动 1 ms Timer0：编码器测速、当前角度和光电边沿检测共用此时基。
    turntable_speed_init();

    EINT;
    ERTM;

    // W5500 使用 192.168.1.20:2000；初始化失败时主循环每约 1 秒重试。
    network_ready = photoelectric_tcp_server_init();

    while (1)
    {
        // 每轮各取一次快照，避免 TCP 模块自己反复读取正在被中断更新的变量。
        encoder_speed_snapshot = turntable_speed_get_rpm_x100();
        angle_snapshot_x100 = turntable_angle_get_degrees_x100();

        // 显示器通过 Modbus RTU 读取寄存器。4x-5/4x-6 发布累计进入数量的
        // 低 16 位和高 16 位；其余地址继续使用原有通用寄存器映射。
        rs485_modbus_port_poll(g_photoelectric_enter_count);
        if (rs485_modbus_port_take_count_reset_request() != 0U)
        {
            // 0x-83“计数清零”在 Timer0 中断内执行，安全更新 32 位累计值。
            photoelectric_sensor_request_count_reset();
        }

        if (network_ready == 0)
        {
            // 每约 1 秒发送一行
            // "enc_rpm=...,obj=n,angle=..."；收到 ASCII "re" 时返回复位请求。
            if (photoelectric_tcp_server_poll(g_photoelectric_detected,
                                              encoder_speed_snapshot,
                                              angle_snapshot_x100) != 0)
            {
                // 同步清零 eQEP3、测速基准和当前角度，
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

        // 1 ms 轮询可及时清空 16 字节 SCI FIFO；测量精度仍由 Timer0 中断保证。
        DEVICE_DELAY_US(1000U);
    }
}
