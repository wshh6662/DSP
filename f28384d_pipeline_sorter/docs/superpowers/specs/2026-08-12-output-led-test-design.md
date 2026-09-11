# Output LED Test Design

## Goal

Use the original CPU1 project to turn output indicators LED1 through LED20 on continuously, so the user can check the board-to-board output signal path.

## Scope

- Test K0 through K19 only.
- Use GPIO0 through GPIO15 and GPIO22 through GPIO25.
- Configure the outputs as open-drain and drive them low because the board output indicators are active-low.
- Leave encoder3 GPIO28, GPIO29, and GPIO31 untouched.
- Do not start CM Ethernet, W5500, TCP, or encoder processing while LED test mode is enabled.
- Preserve the existing application behind a compile-time switch so it can be restored without deleting code.

## Expected Result

After programming and running the CPU1 Flash image, LED1 through LED20 remain continuously illuminated. Input, encoder, power, and Ethernet LEDs are outside this software-controlled test.

## Verification

- Build the `CPU1_FLASH` configuration with zero errors.
- Program `CPU1_FLASH\f28384d_pipeline_sorter.out` into `C28xx_CPU1`.
- Confirm LED1 through LED20 are all on continuously.
