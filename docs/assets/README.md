# Screenshot provenance

These PNGs show the project UI. They are documentation illustrations, not proof of four physical Bluetooth connections or physical finger accuracy.

| Asset | Source and meaning |
| --- | --- |
| `native-values-three.png` | Actual JC framebuffer diagnostic capture. Numeric readings are explicitly labelled **EXAMPLE** by the firmware; they were not received from three meters. |
| `native-meter-tabs.png` | Actual JC framebuffer menu capture showing four directly selectable setup tabs. |
| `native-screen-controls.png` | Actual JC framebuffer menu capture showing dim and orientation controls. |
| `native-calibration.png` | JC first-boot calibration-screen capture from development. |
| `web-values-four.png` | Current production browser UI rendered against the automated four-meter fixture. All measurements are synthetic demonstration data. |
| `web-overlay-four.png` | Current production browser UI, expanded shared chart, using the same synthetic fixture. |
| `web-phone.png` | Current production browser UI at phone size, using synthetic demonstration data. |
| `web-calculations.png` | Current production browser calculator, using fixture voltage/current values. |

Native captures were retained from development; the corresponding view design is unchanged by the later touch-decoder correction. A framebuffer capture does not independently measure physical panel visibility or touch accuracy. No real Wi-Fi join QR codes, passwords, measurement sessions or private flash backups are included.

Regenerate browser examples with `METER_TEST_SCREENSHOT_DIR` set to an output directory, then run `node test/web_meter/visual_layout_browser.cjs`. The extra `docs-calculations` capture is produced by that test. Keep demo-data captions next to any reused image.
