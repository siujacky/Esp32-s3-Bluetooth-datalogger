# Waveshare 3.49-inch board support

Add Meter Workbench builds for both ESP32-S3-Touch-LCD-3.49 revisions (including
the corresponding B/enclosure variants). Preserve the existing meter, web and
recording functions. The user additionally requires use of the RTC and IMU.

## Approach

Use the existing Arduino 2/IDF 4 PlatformIO toolchain and mapping compiler.
Add a dedicated AXS15231B backend: the JC3248 initialization and touch axes are
different despite the shared controller name. Separate V1/V2 mappings avoid
guessing the revision. Default readings use 640x172 landscape; setup and menus
use 172x640 portrait references, with all four user orientations supported.

Both mappings include one-bit SDMMC (CLK41/CMD39/D0 40) and the auxiliary I2C
bus SDA47/SCL48. TCA9554 at 0x20 holds battery power on using P6. V1 has reset21
and backlight8. V2 has reset on TCA P5, backlight42 and enable on TCA P1.

PCF85063 at 0x51 seeds UTC timestamps at boot only with a valid calendar and
clear oscillator-stop flag. Accepted NTP/browser clock updates are queued for
RTC write/readback on the main loop; they never perform I2C on a network thread.
QMI8658 at 0x6b supplies acceleration and angular velocity. An optional saved
auto-rotation toggle uses stable gravity and pauses during contact/calibration;
motion-induced rotations must not write flash. Diagnostics expose failures,
RTC validity, last readings and the suggested orientation.

## Verification and delivery

Host tests must cover invalid RTC/calendar values, leap days, touch corners in
four rotations, malformed contacts and motion stability. Compile both firmware
targets and regress existing targets. Deliver matching app/factory binaries,
checksums, provenance and instructions. SDK/host proof is distinct from physical
LCD, touch, RTC retention, IMU mounting-axis and SD-card qualification. Do not
flash or publish as part of this request.

## Sources

- [Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49)
- [V1 examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49/tree/def6edd0b6e1925ed09702eed01a2f181afdf8c1/Examples/Arduino)
- [V2 examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/tree/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/Arduino/examples)
