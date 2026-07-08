# f28384d_pipeline_sorter

TMS320F28384D 管道分拣系统 — CPU1 首阶段固件

## 硬件

| 项目 | 规格 |
|------|------|
| DSP | TMS320F28384D, 176pin, CPU1 |
| 晶振 | 20 MHz (PCB1 Y1) |
| 编码器 | EI40A6-P6DR-1000, eQEP2 (A/B/Z) |
| 以太网 | W5500, SPIA |
| 调试器 | XDS100v1/v2 |
| PCB | 自定义板，非 TI controlCARD |

## 引脚映射

| 外设 | 引脚 | 备注 |
|------|------|------|
| eQEP2 A/B/Z | GPIO78 / 79 / 81 | SysConfig |
| 传感器 | GPIO26 + XINT1 | 下降沿中断 |
| K0-K19 输出 | GPIO0-15, 22-25 | 20 路 |
| W5500 SPIA | SIMO=GPIO16, SOMI=GPIO17, CLK=GPIO18 | 50 Mbps |
| W5500 CS/RST | GPIO19 / 94 | |
| SCI2 (RS485) | TX=GPIO54, RX=GPIO55 | 115200 8N1 |
| SCI3 (debug) | TX=GPIO72, RX=GPIO73 | 115200 8N1 |
| ADC | ADCA ADCINA0/1, ADCB ADCINB0/1 | 12-bit |

## ⚠️ 已知 PCB 问题

**SCI3-TX 错接到 GPIO74。** F28384D 数据手册 (SPRSP14E) 第 78 页确认 GPIO74 无 SCIC_TX 功能。代码使用 GPIO72。如需用硬件 SCI3，需 PCB 飞线 GPIO72 → RS232 TX。

SCIC_TX 可用引脚：GPIO 12, 38, 56, 63, 72, 89, 106, 140。

## 时钟

| 时钟 | 频率 | 来源 |
|------|------|------|
| SYSCLK | 200 MHz | PLL: 20M × 40 / 2 / 2 / 1 |
| LSPCLK | 50 MHz | SYSCLK / 4 |
| AUXCLK | 125 MHz | AUXPLL: 20M × 50 / 2 / 4 / 1 |
| EPWMCLK | 200 MHz | |

## 环境

- CCS 20.2.0 (`D:\ccs 20\ccs`)，symlink `D:\ccs20`
- SysConfig 1.28.0
- C2000Ware 26.01.00.00
- 编译器 TI C2000 22.6.2.LTS

## 构建

CPU1_RAM 配置编译链接：0 errors, 0 warnings。

## 参考

- 数据手册: `tms320f28384d.pdf` (TI SPRSP14E)
- 原理图: PCB1.pdf (6 页), PCB2.pdf, PCB3.pdf
- 检测 demo: `9_bottle_checking_cpu1/main.c`
