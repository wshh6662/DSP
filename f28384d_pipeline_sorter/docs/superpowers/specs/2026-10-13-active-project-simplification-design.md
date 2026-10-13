# Active DSP project simplification design

Date: 2026-10-13
Baseline: `camera-tcp` commit `c36e139`

## Goal and scope

Simplify the currently running `f28384d_pipeline_sorter` project while preserving all of its active behavior. Deliver an importable CCS project as `project_DSP.zip` on the desktop after verification. Do not change board wiring, GPIO assignments, protocol bytes, timing constants, target core, or the required TI/WIZnet drivers.

The active behavior includes encoder 3 speed/angle measurement; IN0 photoelectric sensing and bottle count; K0 camera trigger; W5500 TCP server on port 2000, telemetry and `re` reset; parsing `tcmc1,` and `tcmc2,`; K4 latest-result lamp output; K1 500 ms alarm on each new bad result; and RS485 Modbus RTU/HMI behavior. Preserve current Watch diagnostics that are useful for validating these paths.

## Approach

1. Classify source files by reachability from `main()`, CCS build configuration, and current tests. Do not infer inactivity merely from a filename or old handoff document.
2. Remove the retired Socket1/2001 camera server, old encoder text payload, unused legacy Modbus include, and their exclusive tests/build references only after confirming no active call site. Keep `modbus_tcp_server.c/.h`: the active RS485 path uses its parser/storage.
3. Simplify duplicated or pass-through code inside active modules only where the interface and observable behavior remain unchanged. Avoid a single-file rewrite and avoid consolidating hardware timing with TCP polling.
4. Keep source and test changes small enough to compare against the baseline. Do not modify vendor driver files except to preserve existing local fixes.

## Verification and acceptance

- Record baseline test and RAM/Flash build results before changes, then repeat them after each logical removal/refactor. Keep tests for every active behavior.
- Confirm target remains TMS320F28384D CPU1, 176pin, TI CGT C2000 22.6.2.LTS, SysConfig 1.28.0, C2000Ware 26.01.00.00.STS.
- Verify complete and split TCP result frames, bad-to-good batches, alarm 499/500 ms boundary and retrigger, encoder/angle reset, camera trigger timing, and RS485/Modbus behavior with the available host tests. Compare build artifacts without treating compilation as proof of hardware operation.
- Physical acceptance requires loading the new `.out` and checking IN0 bottle count, encoder direction/speed, K0 capture timing, `tcmc1,`/`tcmc2,` responses, K4 lamp, K1 500 ms alarm, and RS485/HMI. If the board is unavailable, report this as unverified; do not claim the whole system works on hardware.
- Package only the CCS-importable project sources, project configuration, linker files, local drivers and active tests. Exclude CPU1_RAM/CPU1_FLASH outputs, editor caches, temp files and unrelated workspace content. Verify the ZIP contents and integrity before delivery.

## Safety and rollback

Preserve the user's existing uncommitted generated `CPU1_RAM/.clangd/compile_commands.json` and unrelated root error record. Use the baseline Git commit for comparison; do not reset the worktree or delete outside the named project. Changes to inactive historical modules are limited to this project's archive/removal and do not alter physical wiring or the running board until the user loads a new binary.
