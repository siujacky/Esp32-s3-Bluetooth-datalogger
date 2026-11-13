# Source attribution

The firmware contains GPL-3.0-or-later protocol adaptations from [dmm-tools](https://github.com/antoinecellerier/dmm-tools), pinned to revision `2d4dc507dbd6fa33b05b0241b368572f0ca15745`. The detailed adaptation notice and full license are in [lib/dmm_protocols/NOTICE](lib/dmm_protocols/NOTICE) and [COPYING](lib/dmm_protocols/COPYING). Corresponding source accompanies firmware releases.

The numeral renderer derives its font subset from Roboto Mono Medium under the SIL Open Font License 1.1. See [font notice](lib/meter_numerals/NOTICE.md) and [OFL text](lib/meter_numerals/assets/OFL.txt). The vendored stb_truetype header retains its MIT/public-domain license options.

The QR generator retains its upstream MIT license and pinned-source attribution in [lib/meter_wifi/NOTICE](lib/meter_wifi/NOTICE) and its source headers.

PlatformIO installs the explicitly versioned SDK and Arduino libraries listed in `platformio.ini`. Those projects retain their own license notices. This repository does not replace those notices or claim ownership of upstream work.

Hardware mappings were informed by the cited board-vendor demos and schematics. They describe the named boards; they are not a certification of another board sharing the same controller.
