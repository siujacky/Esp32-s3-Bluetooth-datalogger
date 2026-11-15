# Waveshare ESP32-S3-Touch-LCD-3.49

Meter Workbench includes dedicated builds for both revisions, with display,
touch, TF logging, PCF85063 RTC and QMI8658 accelerometer/gyroscope support.
The matching B and EN enclosure/battery variants use their PCB revision's build.
These are new builds and are not part of release v0.1.0.
Download the matching package from [release v0.2.0](https://github.com/siujacky/Esp32-s3-Bluetooth-datalogger/releases/tag/v0.2.0)
and follow its included `INSTALL.md`.

## Select the revision

Waveshare identifies V2 by **Rev1.1** on the PCB or **V2** on the case QC label.
Do not select a revision by the ESP32 USB identifier. The reset and backlight
wiring differ. See [Waveshare's revision notice](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49).

| Connection | V1 | V2 / Rev1.1 |
|---|---|---|
| PlatformIO environment | `ut_meter_lcd349_v1` | `ut_meter_lcd349_v2` |
| Profile | [lcd349_v1.json](../boards/meter/lcd349_v1.json) | [lcd349_v2.json](../boards/meter/lcd349_v2.json) |
| LCD reset | GPIO21 | TCA9554 P5 |
| Backlight PWM | GPIO8 | GPIO42 |
| Backlight enable | Direct PWM | TCA9554 P1 plus PWM |
| Battery power hold | TCA9554 P6 | TCA9554 P6 |
| QSPI CS / CLK / D0 / D1 / D2 / D3 | 9 / 10 / 11 / 12 / 13 / 14 | Same |
| Touch SDA / SCL / address | 17 / 18 / `0x3b` | Same; interrupts are polled |
| Auxiliary SDA / SCL | 47 / 48 | Same |
| TCA9554 / RTC / IMU addresses | `0x20` / `0x51` / `0x6b` | Same |
| SDMMC CLK / CMD / D0 | 41 / 39 / 40 | Same |

The native panel is **172×640**, with RGB565 QSPI mode 3 at 40 MHz. The default
reading orientation is 640×172. Setup/functions have portrait reference layouts;
the runtime orientation adapter supports all four screen rotations. The display
driver uses the Waveshare panel's short OTP initialization and its touch axes;
the JC3248 driver is not interchangeable.

## Build and install

From the source checkout, build only the environment matching your PCB:

```powershell
python -m pip install platformio
pio run -e ut_meter_lcd349_v2
```

For V1 substitute `ut_meter_lcd349_v1`. Outputs are under
`.pio/build/ENVIRONMENT/`. A local board package includes `app.bin`, complete
`factory.bin`, bootloader/partition files, generated mapping, provenance,
SHA-256 checksums and `INSTALL.md` with the exact flash commands.

For a fresh compatible board, follow the package's factory-install procedure
after preserving its original firmware. For an app-only upgrade, confirm app0
at `0x10000` is the active boot-selected slot and that it has enough space.
Do not use an application binary alone as a full factory install. This port's
build process does not flash any connected board.

## RTC UTC time

At boot, a valid PCF85063 calendar seeds the system clock and recording
timestamps before Wi-Fi is available. The driver rejects oscillator-stop,
stopped-clock, retained 12-hour mode, invalid BCD and impossible dates. A full
UTC update selects 24-hour mode explicitly. An unset RTC leaves the normal
unsynchronized-clock behavior until you set UTC using NTP or the browser.

Accepted NTP and browser time updates are queued to the main loop, written to
the RTC and checked by reading it back. NTP remains the preferred clock source.
`GET /api/status` exposes `clock.source` as `rtc`, `ntp`, `browser` or `none`.
`boardDevices.rtc` includes validity, UTC milliseconds, successful writes,
error count and error text. NTP's own status still describes NTP synchronization
during this boot; RTC restoration does not claim a network sync occurred.

The RTC retains time while its power source remains available. Complete loss of
RTC power can invalidate the calendar; the driver does not invent a time or use
the firmware compilation date. Physical power-retention testing is pending.

## IMU and automatic rotation

The QMI8658 runs its accelerometer at ±2 g and gyroscope at ±512 degrees/s.
The service samples at 20 Hz and exposes acceleration in g and angular velocity
in degrees/s under `GET /api/status` → `boardDevices.imu`. Open **Functions →
Diagnostics** to see RTC and live IMU values on the screen.

**Top edge down → Screen controls → IMU auto rotation** toggles automatic
rotation. It defaults off and saves the toggle in NVS. Stable gravity must
persist for 500 ms; flat, diagonal, accelerated or rapidly turning samples are
ignored. Rotation pauses during contact, calibration and open forms. Motion
changes only the live orientation and never writes flash for each turn.
Selecting a manual screen rotation disables automatic rotation and saves your
manual choice. The assumed sensor-to-panel axes need confirmation on a real
board; keep manual rotation selected if the physical orientation differs.

## TF recording

TF uses a separate one-bit SDMMC peripheral at 20 MHz. The existing recording,
file listing/download, explicit remount and guarded format flows are retained.
Normal mounting never formats a card. Explicit format allocates and verifies
the card's physical FatFs drive, rejects a conflicting logical mapping, and
does not assume drive zero belongs to the card.

## Evidence and physical acceptance

Host checks cover RTC leap dates, bad calendars, oscillator-stop, queued clock
writes/readback, absent peripherals, touch corners in four rotations, malformed
contacts, gravity stability, contact suppression and rotation without NVS wear.
Mapped layouts and keyboard reachability are checked for both revisions.
SDMMC worker simulations exercise logging, downloads, format guards, drive
ownership, failure reporting and replay protection.

After preparing the SDK dependencies with a board build, run the dedicated
host checks from the source root:

```powershell
python tools/esp32/test_waveshare349.py
python tools/esp32/test_sdmmc_storage.py
python test/test_meter_orientation/run_host.py
python test/test_meter_ui/run_mapped_tests.py
```

SDK compilation and simulated I/O do not prove physical display/touch operation,
IMU mounting axes, RTC retention/drift or TF electrical reliability. Validate the
matching PCB using visible test colors, corner/edge touches, UTC set/reboot with
Wi-Fi off, RTC power-loss detection, six IMU orientations and recording/download
on a spare card. No firmware is flashed or public release published by this port.

Implementation sources: Waveshare's pinned [V1 Arduino examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49/tree/def6edd0b6e1925ed09702eed01a2f181afdf8c1/Examples/Arduino)
and [V2 Arduino examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2/tree/1c157e6e8e68b89fd4dc400f46bf1724cb64a57e/Arduino/examples).
