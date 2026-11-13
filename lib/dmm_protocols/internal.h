// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dmm_protocols.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace dmm {
namespace detail {
inline void str(char *out, size_t n, const char *v) {
  if (n)
    snprintf(out, n, "%s", v ? v : "");
}
inline uint16_t le16(const uint8_t *p) {
  return uint16_t(p[0]) | uint16_t(p[1]) << 8;
}
inline uint32_t le24(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16;
}
inline float f32(const uint8_t *p) {
  uint32_t bits = le24(p) | uint32_t(p[3]) << 24;
  float f;
  memcpy(&f, &bits, 4);
  return f;
}
inline void word(Reading &r, const char *text) {
  r.numeric = false;
  r.value = NAN;
  str(r.text, sizeof(r.text), text);
}
inline void number(Reading &r, double value, unsigned decimals) {
  if (!isfinite(value)) {
    word(r, "?");
    r.flags |= UNKNOWN;
    return;
  }
  r.numeric = true;
  r.value = value;
  int written = snprintf(r.text, sizeof(r.text), "%.*f",
                         int(decimals > 9 ? 9 : decimals), value);
  if (written >= int(sizeof(r.text)))
    snprintf(r.text, sizeof(r.text), "%.9g", value);
}
inline void count(Reading &r, uint32_t mag, unsigned decimals, bool negative) {
  double v = double(mag) / pow(10., decimals);
  number(r, negative ? -v : v, decimals);
}
inline void ascii(Reading &r, const uint8_t *p, size_t n) {
  size_t j = 0;
  for (size_t i = 0; i < n && j + 1 < sizeof(r.text); ++i)
    if (p[i] != ' ' && p[i])
      r.text[j++] = char(p[i]);
  r.text[j] = 0;
  char *end = nullptr;
  r.value = strtod(r.text, &end);
  bool digit = false;
  for (size_t i = 0; i < j; i++)
    if (r.text[i] >= '0' && r.text[i] <= '9')
      digit = true;
  r.numeric = digit && end == r.text + j && isfinite(r.value);
  if (!r.numeric)
    r.value = NAN;
}
inline Reading base(const Profile &p, unsigned raw, const char *mode,
                    unsigned range = 0, unsigned channel = 0) {
  Reading r;
  r.value = NAN;
  str(r.mode, sizeof(r.mode), mode);
  r.modeKey = (uint32_t(p.family) + 1u) << 24 | uint32_t(raw) << 8 | channel;
  r.range = uint8_t(range);
  r.channel = uint8_t(channel);
  return r;
}
inline void autoFlag(Reading &r, bool automatic) {
  r.flags |= automatic ? AUTO_RANGE : MANUAL;
}
uint16_t crc16(const uint8_t *, size_t);
void seal(uint8_t *, size_t);
size_t uni61(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t uni171(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t uni181(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t uni71(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t owon(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t zotek(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t brymen(const Profile &, const uint8_t *, size_t, Reading *, size_t);
size_t eevblog(const Profile &, const uint8_t *, size_t, Reading *, size_t);
} // namespace detail
} // namespace dmm
