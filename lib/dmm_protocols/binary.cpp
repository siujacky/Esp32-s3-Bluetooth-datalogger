// SPDX-License-Identifier: GPL-3.0-or-later
// Port of dmm-tools bm78xbt and eevblog121gw packets/tables/decode.
#include "internal.h"
namespace dmm {
namespace detail {
struct GwRange {
  uint8_t mode, range, decimals;
  const char *name, *unit;
};
#include "gw_ranges.inc"
size_t eevblog(const Profile &p, const uint8_t *data, size_t n, Reading *out,
               size_t cap) {
  if (!cap || !data || (n != 18 && n != 19))
    return 0;
  uint8_t buf[19];
  const uint8_t *b = data;
  if (n == 18) {
    buf[0] = 0xf2;
    memcpy(buf + 1, data, 18);
    b = buf;
  }
  if (b[0] != 0xf2)
    return 0;
  uint8_t x = 0;
  for (int i = 0; i < 19; i++)
    x ^= b[i];
  if (x || b[13] & 0xe0)
    return 0;
  unsigned mode = b[5] & 31, range = b[6] & 15, coupling = b[15] >> 3 & 3;
  const GwRange *row = nullptr;
  for (const auto &r : gwRanges)
    if (r.mode == mode && r.range == range) {
      row = &r;
      break;
    }
  Reading r = base(p, mode, row ? row->name : "Unknown", range);
  str(r.unit, sizeof(r.unit), row ? row->unit : "");
  if (!row)
    r.flags |= UNKNOWN;
  if (mode == 5) {
    str(r.unit, sizeof(r.unit), (b[6] & 16) && !(b[6] & 32) ? "°F" : "°C");
    str(r.mode, sizeof(r.mode), r.unit);
  }
  if ((mode == 1 || mode == 2) && coupling == 3)
    str(r.mode, sizeof(r.mode), "AC+DC V");
  if ((b[15] & 64) && coupling != 3 &&
      (mode == 2 || mode == 4 || mode == 16 || mode == 18 || mode == 20)) {
    char label[40];
    snprintf(label, sizeof(label), "LPF %s",
             mode == 2    ? "V"
             : mode == 4  ? "mV"
             : mode == 16 ? "µA"
             : mode == 18 ? "mA"
                          : "A");
    str(r.mode, sizeof(r.mode), label);
    r.flags |= LPF;
  }
  r.flags |= (b[15] & 1 ? LOW_BATT : 0) | (b[16] & 16 ? REL : 0) |
             (b[17] & 0x30 ? RECORD : 0) | (coupling == 1 ? DC : 0) |
             (coupling == 2 ? AC : 0);
  autoFlag(r, b[15] & 4);
  if ((b[17] >> 2 & 3) == 1 || (b[17] >> 2 & 3) == 2)
    r.flags |= HOLD;
  unsigned mm = b[16] & 7;
  if (b[15] & 32)
    r.flags |= PEAK | (mm == 2 ? PEAK_MIN : PEAK_MAX);
  else
    r.flags |= mm == 1   ? MAX
               : mm == 2 ? MIN
               : mm == 3 ? AVG
               : mm == 4 ? MIN | MAX | AVG
                         : 0;
  if (b[6] & 128)
    word(r, "OL");
  else
    count(r, (uint32_t(b[5] >> 6) << 16) | uint32_t(b[7]) << 8 | b[8],
          row ? row->decimals : 0, b[6] & 64);
  out[0] = r;
  size_t wrote = 1;
  unsigned code = b[9], flags = b[10];
  const char *label = nullptr, *unit = nullptr;
  if (code == 1 || code == 2) {
    label = "Voltage";
    unit = "V";
  } else if (code == 6) {
    label = "Frequency";
    unit = flags & 32 ? "kHz" : "Hz";
  } else if (code >= 16 && code <= 21) {
    label = "Current";
    unit = code <= 17 ? "µA" : "mA";
  } else if (code == 100 || code == 101 || code == 105 || code == 106) {
    label = "Internal temperature";
    unit = code >= 105 ? "°F" : "°C";
  } else if (code == 110) {
    label = "Battery";
    unit = "V";
  } else if (code == 150) {
    label = "Burden voltage";
    unit = "mV";
  } else if (code == 180) {
    label = "dBm";
    unit = "dBm";
  }
  if (label && cap > wrote) {
    // VA alternates its voltage and current operands. Keep stable channel IDs
    // instead of combining volts and amps in one graph/log series.
    const bool va = (mode >= 13 && mode <= 15) || (mode >= 22 && mode <= 24);
    unsigned channel = va ? ((code == 1 || code == 2)     ? 1
                             : (code >= 16 && code <= 21) ? 2
                                                          : 3)
                          : 1;
    Reading sub = base(p, (mode << 8) | code, label, range, channel);
    str(sub.unit, sizeof(sub.unit), unit);
    if (flags & 128)
      word(sub, "OL");
    else
      count(sub, uint32_t(b[11]) * 256 + b[12], flags & 7, flags & 64);
    out[wrote++] = sub;
  }
  return wrote;
}
static bool framed(const uint8_t *b, size_t n, uint8_t second, uint8_t type) {
  return n >= 8 && b[0] == 0xff && b[1] == second && b[2] == n &&
         b[3] == type && b[n - 2] == 0xff && b[n - 1] == 3 &&
         crc16(b + 2, n - 6) == le16(b + n - 4);
}
static const char *bmMode(unsigned main, unsigned sub, bool &dc) {
  dc = false;
  if (main == 2) {
    switch (sub) {
    case 0:
      return "LoZ AC V";
    case 1:
      dc = true;
      return "LoZ DC V";
    case 2:
      return "LoZ Ω";
    case 3:
      return "Auto V";
    }
    return "Unknown";
  }
  if (main == 3 || main == 4 || main == 5 || main == 6 || main == 7) {
    if (sub == 3 && main != 4)
      return "Line Hz";
    if (main == 6 && sub == 8)
      return "% 4-20mA";
    static const char *rows[][3] = {{"AC V", "DC V", "AC+DC V"},
                                    {"AC mV", "DC mV", "AC+DC mV"},
                                    {"AC µA", "DC µA", "AC+DC µA"},
                                    {"AC mA", "DC mA", "AC+DC mA"},
                                    {"AC A", "DC A", "AC+DC A"}};
    if (sub <= 2) {
      dc = sub == 1;
      return rows[main - 3][sub];
    }
    return "Unknown";
  }
  if (main == 0x17)
    return sub == 0 ? "VFD Hz" : sub == 1 ? "VFD AC V" : "Unknown";
  if (main == 0x0c) {
    static const char *names[] = {"T1", "T2", "T1-T2"};
    return sub < 3 ? names[sub] : "Unknown";
  }
  if (main == 0x22)
    return sub == 0 ? "EF-L" : sub == 1 ? "EF-H" : "Unknown";
  if (sub)
    return "Unknown";
  switch (main) {
  case 0x0d:
    return "Ω";
  case 0x0e:
    return "Capacitance";
  case 0x0f:
    return "Continuity";
  case 0x10:
    return "Diode";
  case 0x11:
    return "nS";
  case 0x12:
    return "Duty %";
  case 0x13:
    return "Logic Hz";
  case 0x23:
    return "Line Hz";
  default:
    return "Unknown";
  }
}
size_t brymen(const Profile &p, const uint8_t *data, size_t n, Reading *out,
              size_t cap) {
  if (!cap || !data)
    return 0;
  const uint8_t *b = nullptr, *info = nullptr;
  for (size_t at = 0; at + 32 <= n; at++)
    if (framed(data + at, 32, 2, 5)) {
      b = data + at;
      if (at >= 24 && framed(data + at - 24, 24, 1, 4))
        info = data + at - 24;
      break;
    }
  if (!b)
    return 0;
  unsigned a = b[14], c = b[15], main = b[18], sub = b[20];
  bool dc = false;
  const char *mode = bmMode(main, sub, dc);
  Reading r = base(p, main << 8 | sub, mode);
  if (dc)
    r.flags |= DC;
  if (main == 2)
    r.flags |= LOZ;
  if (!strcmp(mode, "Unknown"))
    r.flags |= UNKNOWN;
  r.flags |= (a & (32 | 8) ? HOLD : 0) | (a & 64 ? REL : 0) |
             (c & 16 ? RECORD : 0) | (info && (info[12] & 2) ? LOW_BATT : 0);
  autoFlag(r, a & 16);
  bool crest = a & 128;
  r.flags |= crest
                 ? (PEAK | (c & 8 ? PEAK_MAX : 0) | (c & 4 ? PEAK_MIN : 0))
                 : ((c & 8 ? MAX : 0) | (c & 4 ? MIN : 0) | (c & 2 ? AVG : 0));
  const char *unit = "";
  switch (b[26]) {
  case 2:
    unit = "V";
    break;
  case 3:
    unit = "A";
    break;
  case 4:
    unit = "Ω";
    break;
  case 5:
    unit = "S";
    break;
  case 6:
    unit = "F";
    break;
  case 8:
    unit = "Hz";
    break;
  case 10:
  case 0x4f:
    unit = "%";
    break;
  case 20:
    unit = "°C";
    break;
  case 21:
    unit = "°F";
    break;
  default:
    r.flags |= UNKNOWN;
  }
  int power = int8_t(b[25]);
  static const char *prefixes[] = {"n", "µ", "m", "", "k", "M", "G"};
  if (power >= -9 && power <= 9 && power % 3 == 0)
    snprintf(r.unit, sizeof(r.unit), "%s%s", prefixes[power / 3 + 3], unit);
  else
    r.flags |= UNKNOWN;
  int32_t countSigned = int32_t(le24(b + 21) << 8) >> 8;
  uint32_t magnitude =
      countSigned < 0 ? uint32_t(-countSigned) : uint32_t(countSigned);
  if (c & 32)
    word(r, "OL");
  else if (a & 4) {
    unsigned code = countSigned;
    const char *text = "?";
    switch (code) {
    case 0:
      text = "OL";
      break;
    case 1:
      text = "Auto";
      break;
    case 2:
      text = "InEr";
      r.flags |= LEAD_ERROR;
      break;
    case 3:
      text = "-";
      break;
    case 4:
      text = "- -";
      break;
    case 5:
      text = "- - -";
      break;
    case 6:
      text = "- - - -";
      break;
    case 7:
      text = "- - - - -";
      break;
    case 8:
      text = "diSC";
      break;
    case 9:
      text = "CALi";
      break;
    case 10:
      text = "EF-H";
      break;
    case 11:
      text = "EF-L";
      break;
    case 12:
      text = "rS-3";
      break;
    case 13:
      text = "SoC";
      break;
    case 14:
      text = "SoH";
      break;
    case 15:
      text = "bAd";
      break;
    }
    word(r, text);
  } else {
    unsigned dp = b[24] && b[24] <= b[27] ? b[27] - b[24] : 0;
    if (dp > 9)
      dp = 9;
    count(r, magnitude, dp, (c & 64) || countSigned < 0);
  }
  out[0] = r;
  return 1;
}
} // namespace detail
} // namespace dmm
