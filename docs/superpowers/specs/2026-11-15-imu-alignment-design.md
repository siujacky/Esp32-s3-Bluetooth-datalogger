# Adjustable IMU alignment

The user confirmed automatic rotation works but is offset by 90 degrees and
requested a UI setting. Add IMU ALIGNMENT beside SCREEN ROTATION throughout
the existing touchscreen controls on LCD 3.49 boards. Offer no correction,
90 degrees clockwise, 180 degrees, and 90 degrees counterclockwise.

Store a validated quarter-turn offset in meter-sensors NVS, independently of
the automatic-mode toggle and manual display orientation. Queue all writes
to the main loop, read back saved values, and expose the offset/save result
in boardDevices.imu. A changed offset restarts gravity stability detection,
so a stationary upright device adopts the correction without a new gesture.
Keep contact/modal/calibration gates and flash-free motion updates.

Use the existing native choice-list layout and Preferences pattern. Default
to zero correction to preserve prior installations. No new web controls.
