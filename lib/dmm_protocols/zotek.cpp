// SPDX-License-Identifier: GPL-3.0-or-later
// Port of dmm-tools zotek/{frame,glyph,layout}.rs.
#include "internal.h"
namespace dmm {
namespace detail {
struct ZBit {
  uint8_t type, byte, mask, kind, arg, units;
};
#include "zotek_bits.inc"
static char glyph(uint8_t byte) {
  static const uint8_t codes[] = {
      0xeb, 0x0a, 0xad, 0x8f, 0x4e, 0xc7, 0xe7, 0x8a, 0xef, 0xcf, 0xee,
      0xe5, 0xe4, 0x61, 0x27, 0x23, 0x65, 0x04, 0x67, 0xe1, 0x2f, 0};
  static const char chars[] = "0123456789AEFLout-bCd ";
  byte &= 0xef;
  for (unsigned i = 0; i < sizeof(codes); i++)
    if (codes[i] == byte)
      return chars[i];
  return '?';
}
static bool zrow(Reading &r, const uint8_t *b, unsigned type,
                 bool secondary = false) {
  char cells[5];
  bool dp[5] = {}, negative = false;
  unsigned n = type == 4 && !secondary ? 5 : 4;
  if (type != 4) {
    negative = b[3] & 16;
    for (unsigned i = 0; i < 4; i++) {
      cells[i] = glyph((b[3 + i] & 0xf0) | (b[4 + i] & 15));
      dp[i] = i > 0 && (b[3 + i] & 16);
    }
  } else if (secondary) {
    negative = b[8] & 16;
    for (unsigned i = 0; i < 4; i++) {
      cells[i] = glyph(b[8 - i]);
      dp[i] = i > 0 && (b[8 - i] & 16);
    }
  } else {
    negative = b[13] & 128;
    cells[0] = (b[13] & 12) == 12 ? '1' : ' ';
    for (unsigned i = 1; i < 5; i++) {
      cells[i] = glyph(b[13 - i]);
      dp[i] = b[13 - i] & 16;
    }
  }
  bool blank = true, numeric = true;
  for (unsigned i = 0; i < n; i++) {
    if (cells[i] != ' ')
      blank = false;
    if (cells[i] != ' ' && (cells[i] < '0' || cells[i] > '9'))
      numeric = false;
  }
  if (blank)
    return false;
  uint8_t text[24] = {};
  size_t k = 0;
  bool started = false, point = false;
  if (negative)
    text[k++] = '-';
  for (unsigned i = 0; i < n; i++) {
    if (dp[i] && !point) {
      text[k++] = '.';
      point = true;
      started = true;
    }
    char c = cells[i];
    if (numeric && c == ' ' && started)
      c = '0';
    if (c != ' ')
      text[k++] = c;
    started |= c >= '0' && c <= '9';
  }
  ascii(r, text, k);
  if (!numeric)
    r.numeric = false;
  if (!r.numeric)
    r.value = NAN;
  if (strchr(r.text, '?'))
    r.flags |= UNKNOWN;
  return true;
}
size_t zotek(const Profile &p, const uint8_t *data, size_t n, Reading *out,
             size_t cap) {
  if (!data || !cap || n < 3 || n > 19 || data[0] != 0x1b || data[1] != 0x84)
    return 0;
  static const uint8_t key[] = {0x41, 0x21, 0x73, 0x55, 0xa2, 0xc1, 0x32,
                                0x71, 0x66, 0xaa, 0x3b, 0xd0, 0xe2, 0xa8,
                                0x33, 0x14, 0x20, 0x1a, 0xaa, 0xbb};
  uint8_t b[19];
  for (size_t i = 0; i < n; i++)
    b[i] = data[i] ^ key[i];
  unsigned type = b[2], expected = type == 1 || type == 2 ? 10
                                   : type == 3            ? 11
                                   : type == 4            ? 19
                                                          : 0;
  if (n != expected)
    return 0;
  bool lit[19] = {};
  unsigned units = 0;
  for (const auto &bit : zbits)
    if (bit.type == type && (b[bit.byte] & bit.mask)) {
      if (bit.kind == 0)
        units |= 1u << bit.arg;
      else if (bit.kind >= 2)
        lit[bit.kind] = true;
    }
  static const unsigned priority[] = {6, 7, 5, 4, 3, 2, 1, 0};
  int unit = -1;
  for (unsigned u : priority)
    if (units & (1u << u)) {
      unit = u;
      break;
    }
  int prefix = -1;
  for (const auto &bit : zbits)
    if (bit.type == type && bit.kind == 1 && (b[bit.byte] & bit.mask) &&
        unit >= 0 && (bit.units & (1u << unit))) {
      if (prefix < 0)
        prefix = bit.arg;
    }
  unsigned f = 0;
  if (lit[13])
    f = 15;
  else if (unit == 6)
    f = 12;
  else if (unit == 7)
    f = 13;
  else if (lit[4])
    f = 8;
  else if (lit[5])
    f = 7;
  else if (unit == 5)
    f = 11;
  else if (unit == 4)
    f = 10;
  else if (unit == 3)
    f = 9;
  else if (unit == 2)
    f = 6;
  else if (unit == 1)
    f = (type == 3 || type == 4) && prefix == 2   ? 4
        : (type == 3 || type == 4) && prefix == 1 ? 5
                                                  : 3;
  else if (unit == 0)
    f = (type == 3 || type == 4) && prefix == 2 ? 2 : 1;
  Reading r = base(p, 0, "Unknown");
  if (!zrow(r, b, type))
    return 0;
  if (f == 0 &&
      (!strcmp(r.text, "EF") || strspn(r.text, "-") == strlen(r.text)))
    f = 14;
  static const char *names[] = {
      "Auto",  "V",           "mV", "A",      "mA", "µA", "Ω",   "Continuity",
      "Diode", "Capacitance", "Hz", "Duty %", "°C", "°F", "NCV", "Inrush"};
  unsigned coupling = (lit[3] ? 16 : 0) | (lit[2] ? 32 : 0);
  bool coupled = f >= 1 && f <= 5;
  unsigned raw = f | (coupled ? coupling : 0) | (lit[12] ? 64 : 0);
  r.modeKey = (uint32_t(p.family) + 1u) << 24 | raw << 8;
  const char *pre = coupled ? (coupling == 16   ? "DC "
                               : coupling == 32 ? "AC "
                               : coupling == 48 ? "AC+DC "
                                                : "")
                            : "";
  snprintf(r.mode, sizeof(r.mode), "%s%s%s", pre,
           f == 0 && strcmp(r.text, "Auto") ? "Unknown" : names[f],
           lit[12] && f != 14 ? " peak" : "");
  static const char *unitNames[] = {"V", "A", "Ω", "F", "Hz", "%", "°C", "°F"};
  static const char *prefixNames[] = {"n", "µ", "m", "k", "M"};
  if (unit >= 0 && f != 14)
    snprintf(r.unit, sizeof(r.unit), "%s%s",
             prefix >= 0 ? prefixNames[prefix] : "", unitNames[unit]);
  r.flags |= (lit[6] ? HOLD : 0) | (lit[7] ? REL : 0) | (lit[9] ? MIN : 0) |
             (lit[10] ? MAX : 0) | (lit[11] ? LOW_BATT : 0) |
             (lit[12] ? PEAK : 0) | (lit[13] ? INRUSH : 0) |
             (lit[14] ? CAUTION : 0) | (coupled && lit[3] ? DC : 0) |
             (coupled && lit[2] ? AC : 0);
  autoFlag(r, lit[8]);
  out[0] = r;
  if (type == 4 && cap > 1 && (lit[15] || lit[16])) {
    Reading sub = base(p, raw, lit[16] ? "Frequency" : "Duty", 0, 1);
    str(sub.unit, sizeof(sub.unit), lit[16] ? (lit[17] ? "kHz" : "Hz") : "%");
    if (zrow(sub, b, type, true)) {
      out[1] = sub;
      return 2;
    }
  }
  return 1;
}
} // namespace detail
} // namespace dmm
