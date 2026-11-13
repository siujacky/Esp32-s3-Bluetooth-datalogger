# Bluetooth meter compatibility

See the [user guide](USER_GUIDE.md) for connection/setup and the
[porting manual](PORTING.md) for display-board support. Meter protocol support
and display-board qualification are separate.

The ESP32-S3 meter firmware uses `lib/dmm_protocols`, a fixed-buffer C++ port of [dmm-tools](https://github.com/antoinecellerier/dmm-tools) at revision `2d4dc507dbd6fa33b05b0241b368572f0ca15745` (retrieved 2026-11-09). The model list and upstream status below come from its [supported-devices document at that revision](https://github.com/antoinecellerier/dmm-tools/blob/2d4dc507dbd6fa33b05b0241b368572f0ca15745/docs/supported-devices.md).

## What is verified here

The protocol library has portable packet/transport tests, including physical
UT60BT and UT202BT captures. Earlier hardware checks exercised UT60BT + UT202BT
simultaneous streaming, native/web readings and automatic recovery after a
restart. The UT202BT check covered main LPF V and auxiliary Hz frames; other
modes remain experimental. This is specific historical evidence, not physical
verification of every listed model, mode, adapter or radio combination.

The application supports 1–4 meter slots and four-link NimBLE configuration.
Four simultaneous real BLE links remain unverified. The current publication
does not add fresh physical meter frames establishing a new RF test; example
screen/browser fixtures establish UI behavior only. Pairing, notifications,
reconnect and Wi-Fi/TF coexistence need physical evidence for each claimed setup.

The host suites include 125 upstream real UT61E+/UT61B+/UT181A golden captures,
28 captured OWON VC871 vectors, CMS061/UT217A scope/NCV/Max frames, independent
ZOTEK annunciator/worked cases, 121GW worked/community packets, Brymen CRC and
password login examples, UT171/UT71 layouts, and local UT60BT/UT202BT captures.
Golden captures are checked at every notification split and with checksum
corruption. Transport checks cover oldest-byte fragment expiry, drip feeds,
timestamp wrap, metadata retention, checked name replies and the UT202BT
length-byte quirk. Startup, range/unit, reset, bad-length recovery and output
capacity also have checks. Worked protocol examples remain distinct from
physical captures.

From the repository root, run the two portable suites through PlatformIO's
native target (host toolchain required):

```sh
pio test -e native -f test_dmm_protocols -f test_dmm_transport
```

Or use the standalone PowerShell runners with `g++` and Unity source available.
The explicit build paths keep output inside this checkout:

```powershell
powershell -NoProfile -File test/test_dmm_protocols/run_host.ps1 -BuildDirectory .build/dmm-protocols -UnityDirectory .pio/libdeps/native/Unity/src
powershell -NoProfile -File test/test_dmm_transport/run_host.ps1 -BuildDirectory .build/dmm-transport -UnityDirectory .pio/libdeps/native/Unity/src
```

The standalone runners compile the portable library with C++11 and
`-Wall -Wextra -Werror`; they do not invoke an ESP32 SDK or flash hardware.
`-UnityDirectory` must point to a Unity `src` directory containing `unity.c`;
the shown path is populated by the native PlatformIO tests. Checked-in fixtures
mean an upstream checkout and Python are not needed to run these suites.

## Profiles and upstream status

`experimental` in the registry follows the upstream model/protocol status. It does not assert BLE validation: UT61B+ and UT161E are verified upstream as meters, while their Bluetooth adapter paths remain experimental. UT181A remains tagged experimental here because upstream only partly verifies it, over USB. Every profile below is implemented and selectable; models sharing a layout retain explicit choices where source tables differ.

| Profile ID | Models and aliases | Link / packet family | Upstream status / remaining boundary |
|---|---|---|---|
| `ut60bt` | UNI-T UT60BT | Built-in BLE / UT61 family | Experimental upstream; earlier local physical streaming/captures |
| `ut202bt` | UNI-T UT202BT | Built-in BLE / UT61 family; secondary frames | Experimental upstream; earlier local LPF V/auxiliary Hz streaming/captures |
| `ut61eplus` | UT61E+ (`ut61e+`, `ut61e-plus`) | UT-D07B BLE adapter / UT61 family | Meter and adapter verified upstream |
| `ut61b+` | UT61B+ (`ut61bplus`) | UT-D07B / UT61 family | Meter verified; BLE experimental |
| `ut61d+` | UT61D+ (`ut61dplus`) | UT-D07B / UT61 family | Experimental |
| `ut161e` | UT161E | UT-D07B / UT61E+ tables | Meter verified; BLE experimental |
| `ut161b` | UT161B | UT-D07B / UT61B+ tables | Experimental |
| `ut161d` | UT161D | UT-D07B / UT61D+ tables | Experimental |
| `ut171a`, `ut171b`, `ut171c` | UT171A/B/C (`ut171` → C) | UT-D07B or untested UT-D07A / float frames | Experimental |
| `ut181a` | UT181A (`ut181`) | UT-D07B or untested UT-D07A / normal, REL, Min/Max, Peak, COMP | Partly verified upstream via USB; BLE experimental |
| `ut71ab` | UT71A/B (`ut71a`, `ut71b`) | Untested UT-D07A / parity-masked 11-byte stream | Experimental; explicit selection required |
| `ut71cde` | UT71C/D/E (`ut71c`, `ut71d`, `ut71e`) | Untested UT-D07A / parity-masked 11-byte stream | Experimental; explicit selection required |
| `ow18b` | OW18B / OW16B (`ow16b`) | Built-in BLE / OWON 6-byte | Experimental |
| `ow18e` | OW18E | Built-in BLE / OWON 6-byte | Experimental |
| `b33` | OWON B33 | Built-in BLE / OWON 6-byte | Experimental |
| `b35t+` | OWON B35T+ | Built-in BLE / OWON 6-byte | Experimental; distinct from older B35/B35T |
| `b41t+` | OWON B41T+ | Built-in BLE / OWON 6-byte | Experimental |
| `cm2100b` | OWON CM2100B | Built-in BLE / OWON 6-byte | Experimental |
| `cms061` | CMS061 / UNI-T UT217A (`ut217a`) | Built-in BLE / OWON 15-byte | Verified upstream; scope sends no waveform reading |
| `cms101` | CMS101 / UNI-T UT217B (`ut217b`) | Built-in BLE / OWON 15-byte | Experimental; scope carries stale meter fields, suppressed |
| `ow65b` | OWON OW65B | Built-in BLE / OWON 15-byte | Experimental |
| `ow67b` | OWON OW67B | Built-in BLE / OWON 15-byte | Experimental |
| `ow69b` | OWON OW69B | Built-in BLE / OWON 15-byte | Experimental |
| `vc871` | Voltcraft VC871 | Built-in BLE / OWON 15-byte | Experimental; own REL/Min/Max sub-display behavior |
| `vc891` | Voltcraft VC891 | Built-in BLE / OWON 15-byte | Experimental |
| `vc915` | Voltcraft VC915 | Built-in BLE / OWON 15-byte | Experimental |
| `vc925` | Voltcraft VC925 PV (`vc925pv`) | Built-in BLE / OWON 15-byte | Experimental |
| `zt5b` | ZOTEK/ZOYI ZT-5B / ANENG V05B | Built-in BLE / scrambled type 2 | Verified upstream |
| `zt5bq` | ZT-5BQ / ST207 | Built-in BLE / scrambled type 1 | Experimental |
| `zt300ab` | ZT-300AB / ANENG AN9002 / BSIDE | Built-in BLE / scrambled type 3 | Experimental |
| `zt5566se` | ZT-5566SE / ANENG AN999S | Built-in BLE / scrambled type 4, secondary display | Experimental; app-derived meter BLE behavior |
| `bm78xbt` | Brymen BM788BT / BM787BT (`bm788bt`, `bm787bt`) | Built-in BLE / CRC16 packets and login | Experimental; models indistinguishable on wire |
| `eevblog121gw` | EEVblog 121GW (`121gw`) | Built-in BLE / binary XOR packet | Experimental; old ASCII firmware requires update |

Older OWON **B35 and B35T are not supported** by upstream. Their Bluetooth Classic transport/older packet format is not represented as B35T+ support. The ESP32-S3 BLE client does not provide Bluetooth Classic SPP. USB and serial-only meters, including the serial transport of Voltcraft VC920/940/960, are outside this BLE library.

## GATT and initialization

UUID directions below are from the host's perspective: RX is meter-to-host notification/indication; TX is host-to-meter write. Generic shared service names do not establish a meter model. `profileForName()` only recognizes exact unambiguous names (`UT60BT`, `UT202BT`, `BM78xBT`, `121GW`); `BDM`, `Bluetooth DMM`, and UT-D07 adapters require model selection or OWON information detection.

| Layout | Service UUID | RX / notify UUID | TX / write UUID | Bring-up |
|---|---|---|---|---|
| UNI-T ISSC | `49535343-fe7d-4ae5-8fa9-9fafd205e455` | `49535343-1e4d-4bd9-ba61-23c647249616` | `49535343-8841-43f4-a8d4-ecbe34729bb3` | Always unacknowledged writes; strip adapter heartbeat |
| ZOTEK | `0000fff0-0000-1000-8000-00805f9b34fb` | `0000fff4-0000-1000-8000-00805f9b34fb` | Same FFF4 characteristic | Subscribe; meter streams; use characteristic's supported write type |
| OWON / Voltcraft | `0000fff0-0000-1000-8000-00805f9b34fb` | `0000fff4-0000-1000-8000-00805f9b34fb` | `0000fff3-0000-1000-8000-00805f9b34fb` | Read `0000fff2-0000-1000-8000-00805f9b34fb` before subscribing; byte 0 selects model |
| Brymen | `0003cdd0-0000-1000-8000-00805f9b0131` | `0003cdd5-0000-1000-8000-00805f9b0131` | `0003cdd4-0000-1000-8000-00805f9b0131` | Acknowledged 32-byte password login; read reply on TX; 500 ms settle; subscribe; ATT MTU at least 155 |
| 121GW | `0bd51666-e7cb-469b-8e4d-2742f1ba77cc` | `e7add780-b042-4876-aae1-112855353cc1` | Same characteristic | Subscribe to indications/notifications; meter streams |

`startupCommand(profile, step, command, mac)` enumerates commands until it returns false. `Command.size`, `.withResponse`, `.readReply`, and `.waitMs` describe each write. `Gatt.settleMs` follows startup. The MAC argument is conventional printed order (most significant octet first); Brymen wire order is reversed inside the library. On ESP32 the address is available, so the optional desktop GetBLEAddress fallback is unnecessary.

- UT60BT and UT202BT: subscribe first, send Get Name `AB CD 03 5F 01 DA`, wait up to six seconds for a checked `NameReply`, then send Start Stream `AB CD 03 5D 01 D8`. An acknowledgement or measurement is not a name reply. Other UT61+/UT161 profiles send Start Stream. Optional `pollCommand` returns Get Measurement `AB CD 03 5E 01 D9`; the firmware uses it only after the stream has been silent for at least 500 ms.
- UT171: connect/stream `AB CD 04 00 0A 01 0F 00`.
- UT181A: monitor `AB CD 04 00 05 01 0A 00`.
- UT71, OWON, ZOTEK and 121GW: no poll/start command; subscribe to their stream.
- Brymen: login uses only the documented default password **0000**. `judgeLogin(reply,size,&code)` returns Accepted, Refused, or Unknown after framing and CRC validation. An explicit Refused result must stop automatic login; Unknown is not proof of refusal. Password changes and password guessing are not implemented. Login writes must remain whole and acknowledged, with sufficient negotiated MTU.

OWON FFF2 model codes: 18 OW18B/OW16B; 20 OW18E; 33 B33; 35 B35T+; 41 B41T+; 21 CM2100B; 101 CMS101; 61 CMS061; 65/67/69 OW65B/OW67B/OW69B; 87/89 VC871/VC891; 91 VC915; 92 VC925 PV. An unknown code does not invent a known model. The upstream software sends no OWON challenge, and neither does this port.

## Reading and channel contract

`decodePacket(profile, bytes, size, readings, capacity)` accepts a whole on-air frame, including checksum/header where the protocol has them. ZOTEK expects scrambled on-air bytes. 121GW accepts 19 bytes including F2, or its observed 18-byte body. Malformed checksummed frames publish no readings. OWON and ZOTEK have no packet checksum; their source-specific markers/glyphs gate stream resynchronization.

`Decoder` adds a bounded 512-byte buffer, reassembles fragmented notifications, strips complete UNI-T adapter heartbeats (including fragmented heartbeats), skips corrupt packets, and separates multiple packets in one call. Create one instance per connection. `reset()` clears partial bytes, alignment and Brymen cached battery metadata. There is no heap allocation or meter-data generator in the product library.

The firmware wraps each decoder with `AgedDecoder`: partial packets expire after three seconds measured from their oldest byte, even if newer fragments keep arriving. Published timestamps preserve that oldest contributing byte. `discardPartial()` drops incomplete bytes while preserving valid Brymen metadata.

`Reading.value` is always in `Reading.unit`, including its metric prefix, while `text` retains the signed display digits and decimal precision where the packet supplies them. Examples: UT60BT range 0 `-1.7` has value -1.7 and unit mV; UT60BT mA mode range 1 `1.234` has value 1.234 and unit A; resistance can be `1.234 kΩ`. Convert using the explicit prefix before combining unlike units or comparing to older base-unit readings. Unknown source scales set `UNKNOWN` and leave the unit empty. Non-numeric displays (OL, UL, EF, dashes, lead warnings, scope stale fields) have `numeric=false` and `value=NaN`; they must not enter graphs as zeros.

`modeKey` is a stable numeric identity, independent of localized labels: family in the top byte, raw function in bits 8-23, and channel in the low byte. Some auxiliary functions combine main/sub codes in the raw function. `range` keeps the raw range/prefix/point byte. Translated flags include HOLD, REL, MIN, MAX, LOW_BATT, MANUAL/AUTO_RANGE, CAUTION, PEAK/PEAK_MIN/PEAK_MAX, AVG, RECORD, LOZ, LEAD_ERROR, COMP, LPF and INRUSH where source packets define them. CAUTION is only set from a documented hazard annunciator.

Channel 0 is the main display. Auxiliary displays use channels 1 and above. UT61E+ AC+DC V alternates DC channel 0 and AC channel 1 packets; UT202BT secondary frames emit channel 1 without fabricating a main reading. UT171 square-wave mode emits Hz, duty and pulse width. UT181A normal/REL/MinMax/Peak/COMP emits its present values with independent labels and units. OWON 15-byte and ZOTEK type 4 expose actual sub-displays; CMS scope stale numeric fields are suppressed. 121GW VA voltage and current operands use stable channels 1 and 2 while other auxiliaries use channel 3; absent operands are not synthesized. Brymen packets expose the single meter reading and battery state from a preceding valid information packet.

Use output capacity at least 8. If output capacity is exhausted, complete packets later in the same `push` call are consumed and discarded; any trailing incomplete packet stays buffered. This bounds memory and avoids a stale output backlog. A caller needing a complete log must provide capacity for every reading in the delivered chunk or feed one notification/packet at a time.

The port is a live-reading decoder and connection registry. USB bridge configuration, Bluetooth Classic, remote front-panel keys, internal logged-memory download, oscilloscope waveform acquisition, and old 121GW ASCII conversion are outside its API. These limitations do not change the explicit model selection and packet decoding available above.

## Licensing and regeneration

The port and upstream-derived fixtures are GPL-3.0-or-later. Preserve `lib/dmm_protocols/NOTICE` and `COPYING`, and provide corresponding source with firmware distributions as the license requires. Generated range and annunciator maps are derived from the pinned upstream files; they can be recreated with `generate_tables.py <upstream-checkout>`. Real golden/example fixtures can be regenerated with `test/test_dmm_protocols/generate_fixtures.py <upstream-checkout>` (Python and PyYAML required only for regeneration). No upstream simulator is included in the product library.

## UT202BT hardware update — 2026-11-12

The [physical capture](../test/fixtures/ut202bt/physical-2026-11-12.json)
contains the 12-byte GetName reply `AB CD 08 55 54 32 30 32 42 54 03 54`.
This unit's wire length byte is 8, although its checksum is computed using the
corrected value 9. The [name-reply parser](../lib/dmm_protocols/transport.cpp)
normalizes only the exact UT202BT prefix when that corrected checksum passes;
it retains checksum and model checks for other traffic. Regression tests check
every fragmentation split, noise, corrected-length input and corrupt replies.

The earlier physical run exercised UT60BT and UT202BT streaming together,
UT202BT main LPF V/auxiliary Hz packets, native/web display and reconnection
after restart. It does not qualify other UT202BT modes or four real meters
together, and is not a fresh physical-meter test of the current release.
