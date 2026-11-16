# IMU Alignment Implementation Plan

> For agentic workers: Use superpowers:executing-plans for inline execution.

**Goal:** Let the user correct automatic screen rotation on the touchscreen.
**Architecture:** Queue validated offsets to the board service; persist and
read back once per setting, then add the offset to gravity-based rotations.
**Tech Stack:** Arduino/ESP32-S3, Preferences, existing native menus and host C++ tests.
**Spec:** [IMU alignment design](../specs/2026-11-15-imu-alignment-design.md).

## Constraints and review focus

Preserve manual rotation, automatic-mode state, modal/contact gates, legacy
boards and original-flash backup. Reject invalid offsets, handle failed NVS
writes/readback, reload saved offsets, wrap all four gravity directions, and
re-evaluate unchanged gravity after an offset change without per-turn writes.

## Implementation

- [x] Extend the board-service host harness with offset persistence, invalid
  input, four offsets/directions, same-gravity changes and save-failure cases;
  run it and observe failure before implementing.
- [x] Add queued offset APIs/status in meter_board_devices and the native
  IMU ALIGNMENT chooser beside rotation controls. Run board and menu tests.
- [x] Update help, freeze inputs, build all five targets, and review the diff.
- [x] Verify exact USB identity/backup, app-only flash V2, exercise all four
  UI choices with live IMU, restore zero offset for user choice, and verify
  persistence after restart.

Package/publish verified source and images from the resulting clean commit.
