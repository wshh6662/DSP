# CM Ethernet Ping Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a buildable CPU1 + CM test that responds to `ping 192.168.5.21` through the physical CM Ethernet connector.

**Architecture:** The existing C28x CPU1 project performs board-level Ethernet pin, PHY, clock, and CM boot setup. A separate Cortex-M4 project runs the Ethernet MAC and LwIP ICMP responder because CPU1 and CM require different compilers and binaries.

**Tech Stack:** CCS 20, C2000Ware 26.01.00.00, TI C2000 compiler, TI ARM compiler, F28384D driverlib, LwIP 2.1.2.

## Global Constraints

- Use the custom PCB MII mapping confirmed from the board schematic.
- Do not use the TI controlCARD Ethernet mapping.
- Do not modify the backup project.
- Preserve existing W5500/eQEP behavior outside the CM test path.
- Add plain Chinese comments to all handwritten initialization and test code.
- Static CM IP is `192.168.5.21`; PC IP is `192.168.5.30`.

---

### Task 1: CPU1 CM Ethernet Board Setup

**Files:**
- Modify: `empty_driverlib_main.c`

**Interfaces:**
- Produces: `CM_EthernetBootInit(void)` called once after `Board_init()`.
- Consumes: C2000Ware `SysCtl_*`, `GPIO_*`, and `Device_bootCM()` APIs.

- [x] Add the custom PCB MII pin assignments to the existing CPU1 project.
- [x] Release the PHY control pins with readable Chinese comments.
- [x] Boot CM from Flash in the CPU1_FLASH test image.
- [x] Compile CPU1 and confirm no errors.

### Task 2: CM Ping Companion Project

**Files:**
- Create: sibling project `f28384d_pipeline_sorter_cm_native_ping`
- Modify: `.project`, `.ccsproject`, `.cproject`, `main.c`, `device/lwipopts.h`

**Interfaces:**
- Consumes: the CPU1 MII/PHY/clock setup from Task 1.
- Produces: a CM image with static address `192.168.5.21` that services ICMP Echo.

- [x] Create a new local CM project; do not copy the board-vendor application.
- [x] Configure TI ARM compiler 20.2.7.LTS and C2000Ware 26.01.00.00 dependencies.
- [x] Add handwritten minimal Ethernet/LwIP ICMP service code with plain Chinese comments.
- [x] Confirm ICMP is enabled and TCP application code is absent.
- [x] Compile CM Flash and confirm no errors.

### Task 3: Integration and Handoff

**Files:**
- Modify: `常见错误记录.md` only if build/configuration errors occur.
- Modify: `C:\Users\qazws\Desktop\ccs DSP project\CLAUDE_HANDOFF.md`

**Interfaces:**
- Produces: exact CCS load/run and PC ping steps.

- [x] Record actual build results and compiler discovery fixes.
- [x] Document CPU1/CM Flash order and `ping 192.168.5.21` procedure.
- [x] Check workspace changes and leave the backup project untouched.
