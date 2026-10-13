//#############################################################################
// FILE:   empty_driverlib_main.c
// TITLE:  F28384D 编码器 + 光电检测 + TCP + RS485 Modbus RTU
//#############################################################################
//
// CPU Timer0 每 1 ms 中断一次：
//   每 1 ms ：刷新光电边沿并读取编码器定位相机触发点；
//   每 10 ms：把编码器位置用于 M 法测速和单圈角度。
// 主循环负责取快照、维护 TCP 和轮询 RS485。
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"
#include "camera_trigger.h"
#include "camera_result.h"
#include "modbus_tcp_server.h"
#include "photoelectric_sensor.h"
#include "photoelectric_tcp_server.h"
#include "rs485_modbus_port.h"
#include "turntable_angle.h"
#include "turntable_speed.h"

#define NETWORK_RETRY_TICKS    1000U // 约 1 ms 一轮，1000 轮后重试网络初始化。

#define CAMERA_BUZZER_DURATION_TICKS 500U

volatile uint16_t g_camera_buzzer_active = 0U;

static void camera_result_output_init(void)
{
    // K1/K4隔离源型输出：GPIO低导通，高关闭。先锁存关闭值再使能输出。
    GPIO_setPinConfig(GPIO_1_GPIO1);
    GPIO_setPinConfig(GPIO_4_GPIO4);
    GPIO_writePin(1U, 1U);
    GPIO_writePin(4U, 1U);
    GPIO_setPadConfig(1U, GPIO_PIN_TYPE_OD);
    GPIO_setPadConfig(4U, GPIO_PIN_TYPE_OD);
    GPIO_setDirectionMode(1U, GPIO_DIR_MODE_OUT);
    GPIO_setDirectionMode(4U, GPIO_DIR_MODE_OUT);
}

static void camera_result_output_poll(void)
{
    static uint32_t applied_result_count = 0U;
    static uint32_t applied_bad_count = 0U;
    static uint32_t buzzer_start_tick = 0U;
    uint32_t result_count = g_camera_inspection_result_count;
    uint32_t bad_count = g_camera_inspection_bad_count;
    uint32_t now = turntable_speed_get_tick_count();
    uint16_t result = g_camera_inspection_result;

    // 每条新不良重新计时；同批次后续良品不能取消蜂鸣器脉冲。
    if (bad_count != applied_bad_count)
    {
        applied_bad_count = bad_count;
        buzzer_start_tick = now;
        g_camera_buzzer_active = 1U;
        GPIO_writePin(1U, 0U); // K1送+24V：蜂鸣器响。
    }
    // 无符号减法兼容时基回绕；即使无新报文也检查关闭期限。
    if ((g_camera_buzzer_active != 0U) &&
        ((uint32_t)(now - buzzer_start_tick) >= CAMERA_BUZZER_DURATION_TICKS))
    {
        GPIO_writePin(1U, 1U); // K1开路：须验证设备输入能可靠识别为静音。
        g_camera_buzzer_active = 0U;
    }
    if (result_count != applied_result_count)
    {
        applied_result_count = result_count;
        if (result == CAMERA_INSPECTION_GOOD)
        {
            GPIO_writePin(4U, 0U); // K4送+24V：目标为绿灯。
        }
        else if (result == CAMERA_INSPECTION_BAD)
        {
            GPIO_writePin(4U, 1U); // K4开路：目标为红灯，须验证设备下拉。
        }
    }
    // 最近结果保持到下一条有效帧；TCP重连只清解析半帧，不改变当前输出。
}

void main(void)
{
    int network_ready; // 网络初始化返回码：0 表示成功。
    uint16_t network_retry_ticks = 0U;
    int32_t encoder_speed_snapshot;
    int32_t angle_snapshot_x100;
    int32_t optical_speed_snapshot;
    uint32_t bottle_count_snapshot;
    uint16_t detected_snapshot;

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

    // 初始化相机触发队列和 K0（GPIO0）输出，上电默认关闭。
    camera_trigger_init();
    camera_result_init();
    camera_result_output_init();

    // 建立当前角度的零点。
    turntable_angle_init();

    // 启动 1 ms Timer0：编码器测速、当前角度和光电边沿检测共用此时基。
    turntable_speed_init();

    // 使能全局中断和实时调试中断，让 Timer0 开始更新测量值。
    EINT;
    ERTM;

    // W5500 使用 192.168.1.60:2000；初始化失败时主循环每约 1 秒重试。
    network_ready = photoelectric_tcp_server_init();

    while (1)
    {
        // 每轮各取一次快照，避免 TCP 模块自己反复读取正在被中断更新的变量。
        encoder_speed_snapshot = turntable_speed_get_rpm_x100();
        angle_snapshot_x100 = turntable_angle_get_degrees_x100();
        photoelectric_sensor_get_snapshot(&detected_snapshot,
                                          &optical_speed_snapshot,
                                          &bottle_count_snapshot);

        // 显示器通过 Modbus RTU 读取寄存器。4x-5/4x-6 发布累计进入数量的
        // 低 16 位和高 16 位；其余地址继续使用原有通用寄存器映射。
        rs485_modbus_port_poll(bottle_count_snapshot);
        if (rs485_modbus_port_take_count_reset_request() != 0U)
        {
            // 0x-83“计数清零”在 Timer0 中断内执行，安全更新 32 位累计值。
            photoelectric_sensor_request_count_reset();
        }

        if (network_ready == 0)
        {
            // 每约 1 秒发送一行
            // "enc_rpm=...,angle=...,photo=..."；光电变化也立即发送完整状态行。
            if (photoelectric_tcp_server_poll(encoder_speed_snapshot,
                                              angle_snapshot_x100,
                                              detected_snapshot) != 0)
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

        // 本轮TCP接收完成后应用最新相机结果；不改变K0拍照输出。
        camera_result_output_poll();

        // 1 ms 轮询可及时清空 16 字节 SCI FIFO；测量精度仍由 Timer0 中断保证。
        DEVICE_DELAY_US(1000U);
    }
}
