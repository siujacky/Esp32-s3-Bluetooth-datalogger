# Meter threshold rules

Arduino-independent threshold evaluation and request validation used by the
ESP32 meter web backend. `test/test_meter_rules` runs under native Unity.

- Meter slot, channel and exact unit must match. Invalid and nonnumeric values
  leave the rule's state alone.
- Above/below comparisons are strict. The first qualifying reading can fire.
- A rule rearms at or beyond the threshold minus/plus hysteresis.
- A crossing during cooldown is consumed; it requires another departure and
  reentry to fire. Keeping the value beyond the threshold never floods calls.
- Header names and values reject control characters and reserved HTTP framing
  headers. URL validation admits HTTP/HTTPS destinations without userinfo,
  fragments or control characters.
- Template expansion is a single pass over the user-supplied template. Inserted
  values are never recursively interpreted as more template instructions.
- Feed cursors normalize expired or future cursors and explicitly report gaps.

The embedded adapter bounds the frame ring, JSON bodies, rule count and outbound
queue; it sends requests on a worker task, without automatic retries or redirects.
HTTPS validates the installed IDF root bundle or the configured CA PEM and needs
browser time synchronization after boot. AP-subnet destinations work without a
station uplink. Queue failures and completed deliveries are visible as sequenced
events; stale jobs fail after 30 seconds. No runtime transport or physical radio
claim is implied by native tests.
