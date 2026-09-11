# Output LED Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn K0 through K19 output indicators on continuously in an isolated CPU1 test mode.

**Architecture:** Add one compile-time test switch and one local initialization function to the existing CPU1 main file. When enabled, the program initializes the 20 output GPIOs, drives them low, and remains in a test loop before any Ethernet or encoder application starts.

**Tech Stack:** TMS320F28384D CPU1, C2000 DriverLib, CCS20, C2000Ware 26.01.

## Global Constraints

- Modify only the user's original `f28384d_pipeline_sorter` project.
- Do not modify encoder3 GPIO28, GPIO29, or GPIO31.
- Use GPIO0-15 and GPIO22-25 as active-low open-drain outputs.
- Build `CPU1_FLASH`; `CPU1_RAM` is already known not to fit.

---

### Task 1: Add the isolated output LED test

**Files:**
- Modify: `empty_driverlib_main.c`
- Modify: `CLAUDE_HANDOFF.md`

**Interfaces:**
- Consumes: C2000 DriverLib GPIO APIs and `Board_init()`.
- Produces: `BoardOutputLedTestInit(void)` and compile-time `BOARD_OUTPUT_LED_TEST_ONLY`.

- [x] Add a compile-time test switch set to `1U`.
- [x] Add a constant list containing GPIO0-15 and GPIO22-25.
- [x] Add `BoardOutputLedTestInit()` to configure every listed pin as GPIO, open-drain output, then write low.
- [x] Call the test function after `Board_init()` and remain in a loop while test mode is enabled.
- [x] Build `CPU1_FLASH` and confirm the output file is generated with zero errors.
- [x] Record the exact behavior and restoration method in the handoff document.
