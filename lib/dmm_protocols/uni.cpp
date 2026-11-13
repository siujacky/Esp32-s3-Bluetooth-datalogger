// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from dmm-tools ut61eplus, ut171, ut181a and ut80x protocols.
#include "internal.h"
namespace dmm {
namespace detail {
struct UniRange {
  uint16_t model;
  uint8_t mode, range;
  const char *unit;
};
#include "uni_ranges.inc"
static const char *uniNames[] = {
    "AC V",    "AC mV", "DC V",       "DC mV",       "Hz",
    "Duty %",  "Ω",     "Continuity", "Diode",       "Capacitance",
    "°C",      "°F",    "DC µA",      "AC µA",       "DC mA",
    "AC mA",   "DC A",  "AC A",       "hFE",         "Live",
    "NCV",     "LoZ V", "Clamp AC A", "Clamp DC A",  "LPF V",
    "AC+DC V", "LPF A", "AC+DC A",    "Clamp LPF A", "Clamp AC+DC A",
    "Inrush"};
size_t uni61(const Profile &p, const uint8_t *b, size_t n, Reading *out,
             size_t cap) {
  if (n != 14 || !cap)
    return 0;
  unsigned mode = b[0] & 0x7f, range = b[1] & 15;
  bool secondary = b[0] & 0x80;
  if (secondary && p.modelCode != 202)
    return 0;
  if (mode >= 31)
    return 0;
  Reading r = base(p, mode, uniNames[mode], range, secondary ? 1 : 0);
  const char *unit = "";
  for (const auto &row : uniRanges)
    if (row.model == p.modelCode && row.mode == mode && row.range == range) {
      unit = row.unit;
      break;
    }
  str(r.unit, sizeof(r.unit), unit);
  ascii(r, b + 2, 7);
  if (mode == 20) {
    r.numeric = false;
    r.value = NAN;
  }
  if (!*unit && mode != 20)
    r.flags |= UNKNOWN;
  if (!secondary) {
    uint8_t a = b[11] & 15, c = b[12] & 15, d = b[13] & 15;
    r.flags |= (a & 1 ? REL : 0) | (a & 2 ? HOLD : 0) | (a & 4 ? MIN : 0) |
               (a & 8 ? MAX : 0) | (c & 1 ? CAUTION : 0) |
               (c & 2 ? LOW_BATT : 0) | (d & 4 ? PEAK | PEAK_MAX : 0) |
               (d & 2 ? PEAK | PEAK_MIN : 0);
    autoFlag(r, !(c & 4));
    if (mode == 25 || mode == 27 || mode == 29) {
      r.channel = d & 8 ? 1 : 0;
      r.flags |= d & 8 ? AC : DC;
      r.modeKey = (r.modeKey & ~255u) | r.channel;
    } else if (mode == 2 || mode == 3 || mode == 12 || mode == 14 ||
               mode == 16 || mode == 23)
      r.flags |= DC;
    else if (mode == 0 || mode == 1 || mode == 13 || mode == 15 || mode == 17 ||
             mode == 21 || mode == 22 || mode == 24 || mode == 26 || mode == 28)
      r.flags |= AC;
  }
  out[0] = r;
  return 1;
}
static void floatDisplay(Reading &r, float v, uint8_t display,
                         bool main = true) {
  unsigned decimals = display >> 4, state = display & 15;
  if (state == 1 || state == 2) {
    word(r, state == 2 ? "-OL" : "OL");
    return;
  }
  if (state == 3 || (!main && state >= 4 && state <= 7)) {
    const char *dash[] = {"----", "----.-", "---.--", "--.---", "-.----"};
    word(r, dash[decimals <= 4 ? decimals : 0]);
    return;
  }
  if (state >= 4 && state <= 7) {
    const char *words[] = {"LEAD", "DISC", "Lo", "Hi"};
    word(r, words[state - 4]);
    if (state == 4)
      r.flags |= LEAD_ERROR;
    return;
  }
  number(r, v, decimals <= 4 ? decimals : 4);
  if (state > 7 || decimals > 4)
    r.flags |= UNKNOWN;
}
size_t uni171(const Profile &p, const uint8_t *b, size_t n, Reading *out,
              size_t cap) {
  if (n < 4 || b[0] != 2 || !cap)
    return 0;
  unsigned m = b[3], a = b[1], c = b[2];
  static const char *names[] = {
      "Unknown",    "LoZ V~",   "V DC",  "V AC",    "V AC+DC",  "mV DC",
      "mV AC",      "mV AC+DC", "°C",    "°F",      "Ω",        "Capacitance",
      "Continuity", "Diode",    "nS",    "Hz",      "Unknown",  "µA DC",
      "µA AC",      "µA AC+DC", "mA DC", "mA AC",   "mA AC+DC", "A DC",
      "A AC",       "A AC+DC",  "NCV",   "600A DC", "600A AC",  "Square wave",
      "VFC",        "% 4-20mA"};
  static const char *units[] = {
      "V",  "V",  "V",  "mV", "mV", "mV", "µA", "µA", "µA",  "mA",  "mA",
      "mA", "A",  "A",  "A",  "Ω",  "kΩ", "MΩ", "Hz", "kHz", "MHz", "%",
      "nF", "µF", "mF", "°C", "°F", "V",  "Ω",  "nS", "",    "ms"};
  Reading r = base(p, m, m < 32 ? names[m] : "Unknown", n > 4 ? b[4] : 0);
  if (a & 64) {
    char mode[40];
    snprintf(mode, sizeof(mode), "Peak %s", r.mode);
    str(r.mode, sizeof(r.mode), mode);
    r.flags |= PEAK;
  }
  r.flags |= (a & 128 ? HOLD : 0) | (a & 16 ? REL : 0) | (a & 2 ? RECORD : 0) |
             (a & 4 ? LOW_BATT : 0) | (c & 2 ? CAUTION : 0);
  autoFlag(r, c & 1);
  unsigned agg = c & 0x60;
  r.flags |= agg == 0x40 ? MAX : agg == 0x20 ? AVG : agg == 0x60 ? MIN : 0;
  if (m == 26) {
    word(r, "?");
    r.flags |= UNKNOWN;
    out[0] = r;
    return 1;
  }
  if (m == 29) {
    if (n < 18)
      return 0;
    str(r.unit, sizeof(r.unit), "Hz");
    number(r, f32(b + 5), 1);
    out[0] = r;
    size_t wrote = 1;
    if (cap > wrote) {
      Reading a1 = base(p, m, "Duty", r.range, 1);
      str(a1.unit, sizeof(a1.unit), "%");
      number(a1, f32(b + 9), 1);
      out[wrote++] = a1;
    }
    if (cap > wrote) {
      Reading a2 = base(p, m, "Pulse Width", r.range, 2);
      str(a2.unit, sizeof(a2.unit), "ms");
      number(a2, f32(b + 13), b[17] > 4 ? 4 : b[17]);
      out[wrote++] = a2;
    }
    return wrote;
  }
  if (n < 11)
    return 0;
  str(r.unit, sizeof(r.unit), b[10] < 32 ? units[b[10]] : "");
  floatDisplay(r, f32(b + 5), b[9]);
  out[0] = r;
  if ((a & 1) && n >= 17 && cap > 1) {
    const unsigned u = b[16];
    const char *label = u < 15               ? (u % 3 == 0   ? "DC"
                                                : u % 3 == 1 ? "AC"
                                                             : "Aux")
                        : u >= 18 && u <= 20 ? "Frequency"
                        : u == 21            ? "Duty"
                        : u == 31            ? "Period"
                                             : "Aux";
    Reading sub =
        base(p, (m << 8) | u, a & 16 ? "Reference" : label, r.range, 1);
    str(sub.unit, sizeof(sub.unit), b[16] < 32 ? units[b[16]] : "");
    floatDisplay(sub, f32(b + 11), b[15], false);
    out[1] = sub;
    return 2;
  }
  return 1;
}
static void uni181Mode(unsigned m, char *out, size_t n) {
  unsigned a = m >> 12, c = m >> 8 & 15, v = m >> 4 & 15;
  if (m == 0x5212) {
    str(out, n, "Continuity (open)");
    return;
  }
  if (m == 0x6112) {
    str(out, n, "Diode Alarm");
    return;
  }
  const char *baseName = "Unknown";
  switch (a) {
  case 1:
    baseName = "V AC";
    break;
  case 2:
    baseName = "mV AC";
    break;
  case 3:
    baseName = "V DC";
    break;
  case 4:
    baseName = c == 1 ? "mV DC" : c == 2 ? "°C" : c == 3 ? "°F" : "Unknown";
    break;
  case 5:
    baseName = c == 1 ? "Ω" : c == 2 ? "Continuity" : c == 3 ? "nS" : "Unknown";
    break;
  case 6:
    baseName = c == 1 ? "Diode" : c == 2 ? "Capacitance" : "Unknown";
    break;
  case 7:
    baseName = c == 1   ? "Hz"
               : c == 2 ? "Duty %"
               : c == 3 ? "Pulse Width"
                        : "Unknown";
    break;
  case 8:
    baseName = c == 1 ? "µA DC" : "µA AC";
    break;
  case 9:
    baseName = c == 1 ? "mA DC" : "mA AC";
    break;
  case 10:
    baseName = c == 1 ? "A DC" : "A AC";
    break;
  }
  const char *suffix = "";
  if (a == 4 && (c == 2 || c == 3)) {
    suffix = v == 2 ? " T2" : v == 3 ? " T1-T2" : v == 4 ? " T2-T1" : "";
  } else if (v == 2) {
    if (a == 1 || a == 2 || (a >= 8 && a <= 10 && c == 2))
      suffix = " Hz";
    else if (a == 3 || (a >= 8 && a <= 10 && c == 1))
      suffix = " AC+DC";
    else if (a == 4 && c == 1)
      suffix = " Peak";
  } else if (v == 3)
    suffix = " Peak";
  else if (v == 4)
    suffix = a == 1 ? " LPF" : a == 2 ? " AC+DC" : "";
  else if (v == 5)
    suffix = " dBV";
  else if (v == 6)
    suffix = " dBm";
  snprintf(out, n, "%s%s%s", baseName, suffix, (m & 15) == 2 ? " REL" : "");
}
static void unit181(Reading &r, const uint8_t *b) {
  char wire[9] = {};
  size_t z = 0;
  for (; z < 8 && b[z]; z++)
    wire[z] = char(b[z]);
  char unit[16] = {};
  size_t k = 0;
  for (size_t i = 0; i < z && k + 3 < sizeof(unit); i++) {
    const char *replace = uint8_t(wire[i]) == 0xb0   ? "°"
                          : wire[i] == '~'           ? "Ω"
                          : i == 0 && wire[i] == 'u' ? "µ"
                                                     : nullptr;
    if (replace) {
      size_t len = strlen(replace);
      memcpy(unit + k, replace, len);
      k += len;
    } else
      unit[k++] = wire[i];
  }
  unit[k] = 0;
  const char *suffix[] = {"ac+dc", "DC", "AC"};
  for (const char *s : suffix) {
    size_t len = strlen(s);
    if (k >= len && !strcmp(unit + k - len, s)) {
      unit[k - len] = 0;
      break;
    }
  }
  str(r.unit, sizeof(r.unit), unit);
}
static void value181(Reading &r, const uint8_t *b, bool full) {
  float v = f32(b);
  uint8_t precision = b[4];
  if (full)
    unit181(r, b + 5);
  if ((precision & 3) == 3 && v == 0)
    word(r, "----");
  else if (precision & 3)
    word(r, precision & 2 ? "-OL" : "OL");
  else
    number(r, v, precision >> 4);
}
size_t uni181(const Profile &p, const uint8_t *b, size_t n, Reading *out,
              size_t cap) {
  if (n < 19 || b[0] != 2 || !cap)
    return 0;
  unsigned m = le16(b + 3), a = b[1], c = b[2], fmt = a >> 4 & 7;
  size_t need = fmt == 1 ? 45 : fmt == 2 ? 46 : fmt == 4 ? 32 : 19;
  if (n < need)
    return 0;
  Reading r = base(p, m, "", b[5]);
  uni181Mode(m, r.mode, sizeof(r.mode));
  r.flags =
      (a & 128 ? HOLD : 0) | (c & 2 ? CAUTION : 0) | (c & 8 ? LEAD_ERROR : 0) |
      (c & 16 ? COMP : 0) | (c & 32 ? RECORD : 0) | (fmt == 1 ? REL : 0) |
      (fmt == 2 ? MIN | MAX : 0) | (fmt == 4 ? PEAK | PEAK_MIN | PEAK_MAX : 0);
  autoFlag(r, c & 1);
  value181(r, b + 6, fmt != 2);
  if (fmt == 2)
    unit181(r, b + 38);
  out[0] = r;
  size_t wrote = 1;
  auto add = [&](size_t at, const char *label, bool full) {
    if (at + (full ? 13 : 5) > n || wrote >= cap)
      return;
    Reading sub = base(p, m, label, r.range, wrote);
    str(sub.unit, sizeof(sub.unit), r.unit);
    value181(sub, b + at, full);
    out[wrote++] = sub;
  };
  if (fmt == 1) {
    add(19, "Reference", true);
    add(32, "Absolute", true);
  } else if (fmt == 2) {
    add(11, "Max", false);
    add(20, "Average", false);
    add(29, "Min", false);
  } else if (fmt == 4)
    add(19, "Peak Min", true);
  else {
    size_t at = 19;
    unsigned n3 = m >> 12, n2 = m >> 8 & 15, n1 = m >> 4 & 15;
    const char *l1 = "Aux1", *l2 = "Aux2";
    if (n3 == 4 && (n2 == 2 || n2 == 3)) {
      l1 = n1 == 1 ? "T2" : "T1";
      if (n1 >= 3)
        l2 = "T2";
    } else if ((n3 == 1 || n3 == 2) && n1 == 2) {
      l1 = "Frequency";
      l2 = "Period";
    } else if ((n3 == 3 && n1 == 2) || (n3 == 2 && n1 == 4) ||
               (n3 >= 8 && n3 <= 10 && n2 == 1 && n1 == 2)) {
      l1 = "AC";
      l2 = "DC";
    } else if (n3 == 1 && n1 >= 5) {
      l1 = "Voltage";
      if (n1 == 6)
        l2 = "Impedance";
    }
    if (a & 2) {
      add(at, l1, true);
      at += 13;
    }
    if (a & 4) {
      add(at, l2, true);
      at += 13;
    }
    if (a & 8)
      at += 12;
    if ((c & 16) && at + 7 <= n) {
      unsigned dp = b[at + 2] & 15;
      Reading limit = base(p, m, "High limit", r.range, wrote);
      str(limit.unit, sizeof(limit.unit), r.unit);
      number(limit, f32(b + at + 3), dp);
      if (wrote < cap)
        out[wrote++] = limit;
      if (b[at] <= 1 && at + 11 <= n) {
        limit = base(p, m, "Low limit", r.range, wrote);
        str(limit.unit, sizeof(limit.unit), r.unit);
        number(limit, f32(b + at + 7), dp);
        if (wrote < cap)
          out[wrote++] = limit;
      }
    }
  }
  return wrote;
}
size_t uni71(const Profile &p, const uint8_t *b, size_t n, Reading *out,
             size_t cap) {
  if (n != 11 || !cap || (b[9] & 127) != 13 || (b[10] & 127) != 10)
    return 0;
  uint8_t d[9];
  for (int i = 0; i < 9; i++) {
    if ((b[i] & 0x70) != 0x30)
      return 0;
    d[i] = b[i] & 15;
  }
  unsigned mode = d[6], range = d[5], coupling = d[7], dp = 3;
  const char *name = "Unknown", *unit = "";
  bool known = true;
  switch (mode) {
  case 1:
  case 2:
    name = "V";
    unit = "V";
    if (range < 1 || range > 4)
      known = false;
    else
      dp = range - 1;
    break;
  case 3:
    name = "mV";
    unit = "mV";
    dp = 2;
    break;
  case 4:
    name = "Ω";
    if (range == 1) {
      unit = "Ω";
      dp = 2;
    } else if (range >= 2 && range <= 4) {
      unit = "kΩ";
      dp = range - 2;
    } else if (range == 5 || range == 6) {
      unit = "MΩ";
      dp = range - 5;
    } else
      known = false;
    break;
  case 5:
    name = "Capacitance";
    if (range == 1 || range == 2) {
      unit = "nF";
      dp = range;
    } else if (range >= 3 && range <= 5) {
      unit = "µF";
      dp = range - 3;
    } else if (range == 6 || range == 7) {
      unit = "mF";
      dp = range - 6;
    } else
      known = false;
    break;
  case 6:
    name = "Temperature";
    unit = "°C";
    dp = 3;
    break;
  case 7:
    name = "µA";
    unit = "µA";
    dp = range + 2;
    known = range <= 1;
    break;
  case 8:
    name = "mA";
    unit = "mA";
    dp = range + 1;
    known = range <= 1;
    break;
  case 9:
    name = "A";
    unit = "A";
    dp = 1;
    break;
  case 10:
    name = "Continuity";
    unit = "Ω";
    dp = 2;
    break;
  case 11:
    name = "Diode";
    unit = "V";
    dp = 0;
    break;
  case 12:
    name = "Frequency";
    if (range <= 1) {
      unit = "Hz";
      dp = range + 1;
    } else if (range <= 4) {
      unit = "kHz";
      dp = range - 2;
    } else if (range <= 7) {
      unit = "MHz";
      dp = range - 5;
    } else
      known = false;
    break;
  case 13:
    name = "Temperature";
    unit = "°F";
    dp = 3;
    break;
  case 14:
    name = "Power";
    unit = "W";
    dp = 3;
    break;
  case 15:
    name = "mA%";
    unit = "%";
    dp = 2;
    break;
  default:
    known = false;
  }
  bool negative = d[8] & 4;
  if (mode == 12 && negative) {
    name = "Duty %";
    unit = "%";
    dp = 2;
    negative = false;
  }
  Reading r = base(p, mode, name, range);
  if (mode <= 3 || (mode >= 7 && mode <= 9)) {
    const char *pre = coupling == 1 ? "AC" : coupling == 3 ? "AC+DC" : "DC";
    snprintf(r.mode, sizeof(r.mode), "%s %s", pre, name);
    r.flags |= coupling == 1 ? AC : DC;
  }
  str(r.unit, sizeof(r.unit), known ? unit : "");
  autoFlag(r, (d[8] & 3) == 1);
  if (!known)
    r.flags |= UNKNOWN;
  if (d[0] == 10) {
    word(r, d[1] == 10 ? (negative ? "-0L" : "0L") : d[1] == 12 ? "LO" : "?");
  } else if (d[3] == 11) {
    number(r, 0, 0);
  } else {
    char text[24] = {};
    size_t k = 0;
    if (negative)
      text[k++] = '-';
    for (unsigned i = 0; i < 5; i++) {
      if (d[i] <= 9)
        text[k++] = char('0' + d[i]);
      else if (!(i == 4 && d[i] == 10))
        return 0;
      if (i == dp && i < 4)
        text[k++] = '.';
    }
    ascii(r, reinterpret_cast<const uint8_t *>(text), k);
  }
  out[0] = r;
  return 1;
}
} // namespace detail
} // namespace dmm
