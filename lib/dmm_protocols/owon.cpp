// SPDX-License-Identifier: GPL-3.0-or-later
// Port of dmm-tools owon/{decode,decode15,frame,frame15}.rs.
#include "internal.h"
namespace dmm {
namespace detail {
static bool cms(const Profile &p) {
  return p.modelCode == 61 || p.modelCode == 101;
}
static bool hasNcv(const Profile &p) {
  return cms(p) || p.modelCode == 18 || p.modelCode == 20 || p.modelCode == 21;
}
static const char *owNames[] = {
    "DC V",        "AC V",         "DC A",    "AC A",     "Ω",
    "Capacitance", "Hz",           "Duty %",  "°C",       "°F",
    "Diode",       "Continuity",   "hFE",     "NCV",      "Power W",
    "Power VA",    "Power factor", "4-20mA",  "Power Ah", "Time",
    "Power Wh",    "Power V",      "Power A", "AC+DC V",  "Motor",
    "Solar",       "Angle",        "Compass", "HV DC V",  "HV AC V"};
static const char *owBase[] = {"V",  "V",    "A",  "A", "Ω",  "F", "Hz", "%",
                               "°C", "°F",   "V",  "Ω", "",   "",  "W",  "VA",
                               "",   "%",    "Ah", "s", "Wh", "V", "A",  "V",
                               "",   "W/m²", "°",  "°", "V",  "V"};
static bool prefixed(unsigned f) {
  return f < 7 || f == 10 || f == 11 || f == 14 || f == 15 || f == 18 ||
         f == 20 || f == 21 || f == 22 || f == 23 || f >= 25;
}
static void owUnit(Reading &r, unsigned f, unsigned prefix, const Profile &p) {
  if (f >= 30) {
    r.flags |= UNKNOWN;
    return;
  }
  if (cms(p) && (f == 14 || f == 15)) {
    str(r.unit, sizeof(r.unit), f == 14 ? "V" : "A");
    return;
  }
  if (f == 13 && !hasNcv(p)) {
    r.flags |= UNKNOWN;
    return;
  }
  static const char *pres[] = {"p", "n", "µ", "m", "", "k", "M", "G"};
  snprintf(r.unit, sizeof(r.unit), "%s%s", prefixed(f) ? pres[prefix & 7] : "",
           owBase[f]);
}
static void owValue(Reading &r, unsigned f, unsigned dp, uint32_t v, bool wide,
                    const Profile &p) {
  bool negative = v & (wide ? 0x800000 : 0x8000);
  uint32_t mag = v & (wide ? 0x7ffff : 0x7fff);
  unsigned state = wide ? (v >> 20) & 7 : 0;
  if (state == 1 || dp == 7) {
    word(r, "OL");
    return;
  }
  if (state == 2 || dp == 6) {
    word(r, "UL");
    return;
  }
  if (state == 3) {
    word(r, "HI");
    return;
  }
  if (state == 4) {
    word(r, "LO");
    return;
  }
  if (state >= 6 || (!wide && mag == 0x6fff)) {
    word(r, "----");
    return;
  }
  if (f == 13 && hasNcv(p)) {
    if (!negative && mag <= 4 && dp <= (wide ? 1u : 0u)) {
      static const char *s[] = {"EF", "-", "--", "---", "----"};
      word(r, s[mag]);
      return;
    }
    r.flags |= UNKNOWN;
  }
  if (wide && f == 24 && dp == 0 && !negative && mag <= 3) {
    static const char *s[] = {"- - -", "- - -", "1-2-3", "3-2-1"};
    word(r, s[mag]);
    return;
  }
  count(r, mag, dp, negative);
}
static uint32_t owFlags(uint32_t status) {
  return (status & 1 ? HOLD : 0) | (status & 2 ? REL : 0) |
         (status & 8 ? LOW_BATT : 0) | (status & 16 ? MIN : 0) |
         (status & 32 ? MAX : 0);
}
size_t owon(const Profile &p, const uint8_t *b, size_t n, Reading *out,
            size_t cap) {
  bool wide = p.family == Family::Owon15;
  if (!cap || n != (wide ? 15u : 6u))
    return 0;
  if (wide ? (b[2] != 0xf0 || b[14] == 0xff) : ((b[1] & 0xfc) != 0xf0))
    return 0;
  uint32_t gear = wide ? le24(b) : le16(b),
           status = wide ? le24(b + 12) : le16(b + 2);
  unsigned f = gear >> 6 & (wide ? 31 : 15), dp = gear & 7,
           prefix = gear >> 3 & 7;
  Reading r = base(p, f, f < 30 ? owNames[f] : "Unknown", gear & 63);
  owUnit(r, f, prefix, p);
  r.flags |= owFlags(status);
  autoFlag(r, status & 4);
  if (f == 0 || f == 2 || f == 28)
    r.flags |= DC;
  else if (f == 1 || f == 3 || f == 29)
    r.flags |= AC;
  if (wide)
    r.flags |= (status & 64 ? AVG : 0) | (status & 256 ? LOZ : 0) |
               (status & 512 ? LPF : 0) | (status & 1024 ? PEAK : 0) |
               (status & 65536 ? LEAD_ERROR : 0) |
               (status & 131072 ? INRUSH : 0);
  bool scope = wide && cms(p) && (status & (1u << 18));
  if (scope) {
    str(r.mode, sizeof(r.mode), "Scope");
    word(r, "----");
  } else {
    owValue(r, f, dp, wide ? le24(b + 3) : le16(b + 4), wide, p);
    if (wide) {
      const char *power = nullptr;
      if (!cms(p) &&
          (f == 14 || f == 15 || f == 18 || f == 20 || f == 21 || f == 22)) {
        power = status & 0x8000   ? "USB"
                : status & 0x4000 ? "DC"
                : status & 0x2000 ? "AC"
                                  : nullptr;
      }
      char label[40];
      if (status & 131072)
        str(r.mode, sizeof(r.mode), "Inrush");
      else if (status & (1u << 18))
        str(r.mode, sizeof(r.mode), "Scope");
      else if (power) {
        snprintf(label, sizeof(label), "%s power %s", power, owBase[f]);
        str(r.mode, sizeof(r.mode), label);
      } else if (f == 24 &&
                 (!strcmp(r.text, "1-2-3") || !strcmp(r.text, "3-2-1"))) {
        snprintf(label, sizeof(label), "Motor %s", r.text);
        str(r.mode, sizeof(r.mode), label);
      }
      if (status & 1024) {
        snprintf(label, sizeof(label), "Peak %s", r.mode);
        str(r.mode, sizeof(r.mode), label);
      }
      if (status & 512) {
        snprintf(label, sizeof(label), "LPF %s", r.mode);
        str(r.mode, sizeof(r.mode), label);
      }
    }
  }
  out[0] = r;
  if (!wide || !(gear & 4096) || scope || cap < 2)
    return 1;
  uint32_t subgear = le24(b + 6);
  unsigned subf = subgear >> 6 & 31;
  bool follows = p.modelCode == 87 && (status & (2 | 16 | 32));
  if (follows)
    subf = f;
  const char *label = subf < 30 ? owNames[subf] : "Unknown";
  if (follows)
    label = status & 2 ? "Reference" : "Live";
  else if (cms(p) && subf == f && (status & 32))
    label = "Max";
  else if (cms(p) && subf == f && (status & 16))
    label = "Min";
  else if (f == 23 && subf <= 1)
    label = subf == 0 ? "DC" : "AC";
  else if (subf == 7)
    label = "Duty";
  else if (subf == 14 || subf == 15 || subf == 18 || subf == 20 || subf == 21 ||
           subf == 22)
    label = owBase[subf];
  Reading sub = base(p, subf, label, subgear & 63, 1);
  owUnit(sub, subf, subgear >> 3 & 7, p);
  owValue(sub, subf, subgear & 7, le24(b + 9), true, p);
  out[1] = sub;
  return 2;
}
} // namespace detail
} // namespace dmm
