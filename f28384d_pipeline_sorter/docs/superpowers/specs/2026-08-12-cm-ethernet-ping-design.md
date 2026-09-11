# CM Ethernet Ping Test Design

## Goal

Use the board's native `CM ETH` connector, without W5500/TCP, and make the PC successfully ping the static CM address `192.168.5.21`.

## Project Boundary

- Keep `f28384d_pipeline_sorter` as the existing CPU1 application.
- Add `f28384d_pipeline_sorter_cm_native_ping` beside it as the Cortex-M4 CM application.
- Do not modify `f28384d_pipeline_sorter_backup_20260722`.
- Keep the existing W5500 and eQEP code available, but the CM test uses only the physical `CM ETH` connector.

## CPU1 Responsibilities

CPU1 owns the system-level setup that CM cannot perform by itself:

1. Configure the Ethernet clock from SYSPLL with divide-by-two.
2. Configure the custom PCB MII pin mux from the board schematic, not the TI controlCARD pin map.
3. Release the external PHY power/reset control signals used by this board.
4. Boot the CM core from RAM for the first debugger test, or Flash for a later standalone test.

The CPU1 helper will use plain Chinese comments explaining why each pin and boot step exists.

## CM Responsibilities

The CM program initializes only what is needed for an ICMP test:

1. Initialize the CM core and Ethernet MAC.
2. Start SysTick for the LwIP time base.
3. Use MII mode with the custom-board DP83822 PHY path.
4. Configure static networking:
   - MAC: `02:00:00:00:05:21`
   - IP: `192.168.5.21`
   - Mask: `255.255.255.0`
   - Gateway: `0.0.0.0` for the direct-cable test
5. Run the LwIP/DMA service loop. LwIP answers ICMP Echo automatically; no TCP socket and no NetAssist are needed.

The application and project configuration are maintained locally. TI's official DriverLib and LwIP sources are dependencies; no board-vendor demo application is copied into this test.

## Verification

1. Build CPU1 and CM projects without errors.
2. Connect only the board's `CM ETH` cable to the PC Ethernet adapter.
3. Set the PC adapter to `192.168.5.30/24`, gateway blank.
4. Load/run both cores in CCS.
5. Run `ping 192.168.5.21` on the PC.
6. Success means replies arrive from `192.168.5.21`; failure is diagnosed first by link LEDs, PHY state, then MII/LwIP state.
