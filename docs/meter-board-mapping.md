# Board mapping reference

Board profiles in [`boards/meter`](../boards/meter) select existing display and
touch implementations and configure pins, dimensions, offsets, orientation,
optional TF storage, theme and native UI geometry. Read the
[porting manual](PORTING.md) for the electrical worksheet, exact canonical pins,
driver/platform additions and staged physical acceptance checks. Normal use is
covered by the [user guide](USER_GUIDE.md).

## Supported platform and selections

The supplied build platform is ESP32-S3 N16R8: 16 MB QIO flash, OPI PSRAM,
`qio_opi` memory and `default_16MB.csv` partitions. JSON cannot change the MCU,
flash/PSRAM interface, partitions, library implementation or panel init sequence.

| Named environment | Canonical profile | Display / touch | Native size; compiled reading/setup/advanced rotations |
| --- | --- | --- | --- |
| `ut_meter_jc3248w535` (default) | `jc3248w535.json` | AXS15231B QSPI / AXS15231B | 320×480; 1/1/1 |
| `ut_meter_amoled175` | `amoled175.json` | CO5300 QSPI / CST92xx (CST9217) | 466×466; 0/0/0 |
| `ut_meter_lcd147` | `lcd147.json` | JD9853 SPI / AXS5106L | 172×320; 1/0/0 |
| `ut_meter_mapped` | Required `METER_BOARD_PROFILE` path | One implemented pair above | Validated custom profile |

The AMOLED target is Waveshare ESP32-S3-Touch-AMOLED-1.75, **not 1.75C**. The
LCD is Waveshare ESP32-S3-Touch-LCD-1.47. Profiles have no PCB revision field;
confirm the actual revision and schematic rather than assuming pins from the
ESP32 generation or display size.

JC touch is SDA4/SCL8 at `0x3B`, with reset/IRQ `-1`. TF is independent SPI3:
CS10/MOSI11/SCK12/MISO13. Do not use demo touch reset12/IRQ11, which overlap
those TF pins. The full pin and source-revision tables are in
[PORTING.md](PORTING.md#canonical-hardware-profiles).

## Validate and build a profile

Save this **illustrative, not hardware-qualified** example as
`boards/meter/my-board.json` in UTF-8 without a BOM. It inherits all canonical JC
pins, geometry and storage, changing only UI appearance and ID. It introduces
no suggested GPIO wiring. Confirm that the inherited physical map matches the
actual board before use.

```json
{
  "extends": "jc3248w535.json",
  "id": "example-jc-ui",
  "theme": {"accent": 2016, "radius": 8},
  "ui": {"reading": {"profile": {"size": 1}}}
}
```

From the repository root, PowerShell:

```powershell
python tools/esp32/meter_board_config.py boards/meter/my-board.json .pio/map-check
$env:METER_BOARD_PROFILE = 'boards/meter/my-board.json'
try {
    pio run -e ut_meter_mapped
    if ($LASTEXITCODE -ne 0) { throw 'Mapped firmware build failed' }
} finally {
    Remove-Item Env:\METER_BOARD_PROFILE -ErrorAction SilentlyContinue
}
```

POSIX shell:

```sh
python3 tools/esp32/meter_board_config.py boards/meter/my-board.json .pio/map-check
METER_BOARD_PROFILE=boards/meter/my-board.json pio run -e ut_meter_mapped
```

The compiler uses Python 3.10+ and its standard library. The build hook validates
again, force-includes a per-environment generated header and includes its
fingerprint in compiler signatures. Inherited changes also rebuild affected
objects. Named environments always select their canonical profiles, even when
the environment variable is set. Missing/invalid mapped selection fails closed.

Edit JSON and rebuild; do not edit `meter_board_config.h`. A compiled mapping is
not a runtime file, automatic board detector or new controller driver.

## Inheritance and validation

`extends` searches relative to the selected file, then the canonical mapping
directory. Nested objects merge; the final resolved object must be complete.
Absolute parents, `..`, backslashes in parents, symlink escapes, cycles, depth
beyond 16 files, duplicate JSON keys, unknown keys and wrong types are rejected.
Use `/` in relative paths. Addresses/colors are decimal integers in JSON, which
does not accept hexadecimal literals or comments.

| Object | Fields and meaning |
| --- | --- |
| `schema_version`, `id` | Version `1`; lowercase 1–63-character identifier, letters/digits/`_`/`-`, first character letter/digit. |
| `theme` | `style` 0 legacy / 1 workbench; `background`, `surface`, `raised`, `text`, `muted`, `accent`, `warning`, `border` RGB565 values 0–65535; `radius` 0–24. |
| `display` | `backend`, native `width`/`height`, `bus` (`cs`, `sck`, `data0`–`data3`, `dc`), `reset`, `backlight`, `frequency`, `offset_x`/`offset_y`. |
| `touch` | Matching `driver`, `sda`/`scl`, 7-bit `address`, `reset`/`irq`, `frequency`, `mirror_x`/`mirror_y`/`swap_xy`, `required_pmic_address`. |
| `storage` | `backend` `none` or `sd_spi`; `spi_host: 3`, `cs`/`mosi`/`sck`/`miso`, `frequency`. Disabled storage has all four pins `-1`. |
| `ui.reading` | View fields, large value, profile/status/flags/unit/slot labels, scope plot and labels. |
| `ui.setup` | View fields, fixed header, scrolling viewport, row/grid geometry, information and network QR/text. |
| `ui.advanced` | View fields, viewport, Back and compatibility Up/Down rectangles, row/card geometry, header/message and keyboard. Native swipe menus do not show an Up/Down footer. |
| `ui.overview` | `values`, `scope`, `count_button`, `show_count_button`, `gap`, `slot0_color` through `slot3_color`; uses the reading reference canvas. |

View fields are `rotation` 0–3, `reference_width`, `reference_height`,
`safe_circle`, and `safe_margin`. Rotations 1/3 swap native width/height. JC's
320×480 native buffer therefore gives a 480×320 canvas at rotation 1.
Coordinates scale from each view's reference canvas. Runtime Screen rotation
0/90/180/270 reflows that baseline map; it is a separate saved preference.

Rectangles are `x,y,w,h`; labels are `x,y,size,align` with -1/0/1 alignment.
Optional label `size: 0` hides it; required fonts must be positive. Rows separate
height and step so their gaps remain inert. Setup supports `columns` and
`column_gap`; Advanced has `home_grid`. Shared resolved layouts own both drawing
and touch hits, including clipped rows. Keyboards choose a fitting grid while
keeping configured minimum target dimensions; native short-height layouts have
an eight-character fallback. Popup choices scroll without an arrow footer.

`safe_circle: true` keeps required controls inside the inscribed circle minus
the margin. The validator checks reference/scaled geometry, divisors, text
overlap, keyboard fit, count-button bounds and controller/resource constraints.
Older version-1 profiles receive explicit compatible theme/grid/overview
defaults in the resolved export.

Values/charts support **1–4 meters**. Four colors, Grid, four separate panes and
four overlaid traces are implemented. The Python overview check retains a
legacy three-lane minimum; use the four-meter native/overlay/browser tests and
physical inspection to establish complete readability. A count button must not
overlap mapped value/scope viewports. Compact/round targets expose count through
screen controls rather than a permanent mapped button.

GPIO checks exclude native USB19/20, nonexistent22–25 and this N16R8 platform's
flash/PSRAM26–37, and reject duplicate active pins. They do not establish voltage,
power, boot-strap or board-specific peripheral compatibility. Optional `-1`
pins follow driver rules; CST92xx needs IRQ and CO5300 uses internal brightness.
AXS mirror/swap flags must be false because its rotation/calibration path owns
that transform. Controller dimension/offset limits are listed in
[the porting manual](PORTING.md#existing-controller-limits).

## Calibration and identity

AXS raw coordinates map through affine calibration into the compiled reading
baseline, then transform into runtime rotation 0–3. Other supplied drivers use
native-axis normalization followed by GFX rotation. Application handlers consume
already transformed coordinates. The JC release decoder accepts uniform
eight-byte `0x23` and `0x63` idle markers, zero contact and explicit release;
corrupt/mixed frames are not releases.

AXS uses a 64-byte dimension/version/CRC-checked calibration record. Save writes
and verifies the inactive NVS bank before selecting it. The physical-map
fingerprint (display, touch, compiled reading rotation) chooses the calibration
namespace; exact canonical JC hardware retains `meter-touch`. UI-only changes
retain compatible calibration. Verify saved/readback state and actual restart;
do not erase unrelated NVS as a porting step.

Serial `status` and `GET /api/status` under `device.mapping` report profile ID,
SHA-256, dimensions and configured rotations. `GET /api/board-map` exports the
resolved mapping without credentials. SHA-256 covers the complete UTF-8 JSON
with sorted keys, no spaces and ASCII escapes, including UI overrides. It is
separate from the physical calibration namespace.

## Checks and evidence

```sh
python tools/esp32/test_meter_board_config.py
python tools/esp32/test_meter_ui_validation.py
python test/test_meter_ui/run_mapped_tests.py
python test/test_meter_overlay/run_mapped_tests.py
python test/test_meter_orientation/run_host.py
python test/test_meter_native_swipe/run_host.py
pio test -e native
pio run -e ut_meter_jc3248w535 -e ut_meter_lcd147 -e ut_meter_amoled175
```

Use `python3` where needed. Geometry runners need host `g++`. They check the
canonical profiles; explicitly validate/build a custom inherited profile and
add resolved-layout test cases for a contribution. The 800×480 fixture proves
layout scaling, not controller or physical 800×480 support. These commands do
not upload firmware or test a physical screen.

JC has physical display/touch evidence. The other display targets' source/build
and geometry coverage does not establish physical visibility/touch. Earlier
UT60BT + UT202BT simultaneous streaming was exercised; four simultaneous real
RF links and new physical meter frames for the current release remain pending.
Use [the port acceptance checklist](PORTING.md#port-acceptance-and-contribution)
for visible screen, real touch/release, NVS restart and BLE/Wi-Fi/optional TF
read/write/download checks that preserve existing data.
