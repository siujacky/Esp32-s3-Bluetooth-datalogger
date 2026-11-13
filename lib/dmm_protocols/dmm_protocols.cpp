// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal.h"
namespace dmm {
static const Gatt issc = {"ISSC UART",
                          "49535343-fe7d-4ae5-8fa9-9fafd205e455",
                          "49535343-1e4d-4bd9-ba61-23c647249616",
                          "49535343-8841-43f4-a8d4-ecbe34729bb3",
                          nullptr,
                          23,
                          0,
                          BringUp::Subscribe,
                          true,
                          false,
                          true};
static const Gatt fff0 = {"FFF0",
                          "0000fff0-0000-1000-8000-00805f9b34fb",
                          "0000fff4-0000-1000-8000-00805f9b34fb",
                          "0000fff4-0000-1000-8000-00805f9b34fb",
                          nullptr,
                          23,
                          0,
                          BringUp::Subscribe,
                          false,
                          false,
                          false};
static const Gatt owonGatt = {"OWON",
                              "0000fff0-0000-1000-8000-00805f9b34fb",
                              "0000fff4-0000-1000-8000-00805f9b34fb",
                              "0000fff3-0000-1000-8000-00805f9b34fb",
                              "0000fff2-0000-1000-8000-00805f9b34fb",
                              23,
                              0,
                              BringUp::ReadInfo,
                              false,
                              false,
                              false};
static const Gatt brymenGatt = {"Brymen BM78xBT",
                                "0003cdd0-0000-1000-8000-00805f9b0131",
                                "0003cdd5-0000-1000-8000-00805f9b0131",
                                "0003cdd4-0000-1000-8000-00805f9b0131",
                                nullptr,
                                155,
                                500,
                                BringUp::BrymenLogin,
                                false,
                                true,
                                false};
static const Gatt gwGatt = {"EEVblog 121GW",
                            "0bd51666-e7cb-469b-8e4d-2742f1ba77cc",
                            "e7add780-b042-4876-aae1-112855353cc1",
                            "e7add780-b042-4876-aae1-112855353cc1",
                            nullptr,
                            23,
                            0,
                            BringUp::Subscribe,
                            false,
                            false,
                            false};
#define P(id, name, family, gatt, experimental, code)                          \
  { id, name, Family::family, &gatt, experimental, code }
static const Profile profiles[] = {
    P("ut60bt", "UNI-T UT60BT", Uni61, issc, true, 60),
    P("ut202bt", "UNI-T UT202BT", Uni61, issc, true, 202),
    P("ut61eplus", "UNI-T UT61E+", Uni61, issc, false, 61),
    P("ut61b+", "UNI-T UT61B+", Uni61, issc, false, 62),
    P("ut61d+", "UNI-T UT61D+", Uni61, issc, true, 63),
    P("ut161e", "UNI-T UT161E", Uni61, issc, false, 61),
    P("ut161b", "UNI-T UT161B", Uni61, issc, true, 62),
    P("ut161d", "UNI-T UT161D", Uni61, issc, true, 63),
    P("ut171a", "UNI-T UT171A", Uni171, issc, true, 171),
    P("ut171b", "UNI-T UT171B", Uni171, issc, true, 171),
    P("ut171c", "UNI-T UT171C", Uni171, issc, true, 171),
    P("ut181a", "UNI-T UT181A", Uni181, issc, true, 181),
    P("ut71ab", "UNI-T UT71A/B", Uni71, issc, true, 71),
    P("ut71cde", "UNI-T UT71C/D/E", Uni71, issc, true, 72),
    P("ow18b", "OWON OW18B/OW16B", Owon6, owonGatt, true, 18),
    P("ow18e", "OWON OW18E", Owon6, owonGatt, true, 20),
    P("b33", "OWON B33", Owon6, owonGatt, true, 33),
    P("b35t+", "OWON B35T+", Owon6, owonGatt, true, 35),
    P("b41t+", "OWON B41T+", Owon6, owonGatt, true, 41),
    P("cm2100b", "OWON CM2100B", Owon6, owonGatt, true, 21),
    P("cms101", "OWON CMS101 / UNI-T UT217B", Owon15, owonGatt, true, 101),
    P("cms061", "OWON CMS061 / UNI-T UT217A", Owon15, owonGatt, false, 61),
    P("ow65b", "OWON OW65B", Owon15, owonGatt, true, 65),
    P("ow67b", "OWON OW67B", Owon15, owonGatt, true, 67),
    P("ow69b", "OWON OW69B", Owon15, owonGatt, true, 69),
    P("vc871", "Voltcraft VC871", Owon15, owonGatt, true, 87),
    P("vc891", "Voltcraft VC891", Owon15, owonGatt, true, 89),
    P("vc915", "Voltcraft VC915", Owon15, owonGatt, true, 91),
    P("vc925", "Voltcraft VC925 PV", Owon15, owonGatt, true, 92),
    P("zt5b", "ZOTEK ZT-5B / ANENG V05B", Zotek, fff0, false, 2),
    P("zt5bq", "ZOTEK ZT-5BQ / ST207", Zotek, fff0, true, 1),
    P("zt300ab", "ZOTEK ZT-300AB / AN9002", Zotek, fff0, true, 3),
    P("zt5566se", "ZOTEK ZT-5566SE / AN999S", Zotek, fff0, true, 4),
    P("bm78xbt", "Brymen BM788BT/BM787BT", Brymen, brymenGatt, true, 788),
    P("eevblog121gw", "EEVblog 121GW", Eevblog, gwGatt, true, 121)};
#undef P
size_t profileCount() { return sizeof(profiles) / sizeof(profiles[0]); }
const Profile *profileAt(size_t i) {
  return i < profileCount() ? profiles + i : nullptr;
}
const Profile *findProfile(const char *id) {
  if (!id)
    return nullptr;
  struct Alias {
    const char *id, *to;
  };
  static const Alias aliases[] = {
      {"ut61e+", "ut61eplus"},   {"ut61e-plus", "ut61eplus"},
      {"ut61bplus", "ut61b+"},   {"ut61dplus", "ut61d+"},
      {"ut171", "ut171c"},       {"ut181", "ut181a"},
      {"ut71a", "ut71ab"},       {"ut71b", "ut71ab"},
      {"ut71c", "ut71cde"},      {"ut71d", "ut71cde"},
      {"ut71e", "ut71cde"},      {"ow16b", "ow18b"},
      {"ut217a", "cms061"},      {"ut217b", "cms101"},
      {"bm788bt", "bm78xbt"},    {"bm787bt", "bm78xbt"},
      {"121gw", "eevblog121gw"}, {"vc925pv", "vc925"}};
  for (const auto &a : aliases)
    if (!strcmp(id, a.id))
      id = a.to;
  for (const auto &p : profiles)
    if (!strcmp(id, p.id))
      return &p;
  return nullptr;
}
const Profile *profileForName(const char *name) {
  if (!name)
    return nullptr;
  if (!strcmp(name, "121GW"))
    return findProfile("121gw");
  if (!strcmp(name, "BM78xBT"))
    return findProfile("bm78xbt");
  if (!strcmp(name, "UT60BT"))
    return findProfile("ut60bt");
  if (!strcmp(name, "UT202BT"))
    return findProfile("ut202bt");
  return nullptr;
}
const Profile *profileForOwonInfo(const uint8_t *info, size_t n) {
  if (!info || !n)
    return nullptr;
  for (const auto &p : profiles)
    if ((p.family == Family::Owon6 || p.family == Family::Owon15) &&
        p.modelCode == info[0])
      return &p;
  return nullptr;
}
namespace detail {
uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xffff;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int j = 0; j < 8; j++)
      c = c & 1 ? (c >> 1) ^ 0xa001 : c >> 1;
  }
  return c;
}
void seal(uint8_t *p, size_t n) {
  uint16_t c = crc16(p + 2, n - 6);
  p[n - 4] = uint8_t(c);
  p[n - 3] = uint8_t(c >> 8);
  p[n - 2] = 0xff;
  p[n - 1] = 3;
}
} // namespace detail
static void beCommand(uint8_t op, Command &c) {
  c = Command();
  c.size = 6;
  c.data[0] = 0xab;
  c.data[1] = 0xcd;
  c.data[2] = 3;
  c.data[3] = op;
  unsigned sum = 379 + op;
  c.data[4] = sum >> 8;
  c.data[5] = sum;
}
static void leCommand(uint8_t op, uint8_t arg, bool hasArg, Command &c) {
  c = Command();
  c.size = hasArg ? 8 : 7;
  c.data[0] = 0xab;
  c.data[1] = 0xcd;
  c.data[2] = hasArg ? 4 : 3;
  c.data[4] = op;
  if (hasArg)
    c.data[5] = arg;
  unsigned sum = c.data[2] + op + (hasArg ? arg : 0);
  c.data[c.size - 2] = sum;
  c.data[c.size - 1] = sum >> 8;
}
bool startupCommand(const Profile &p, size_t step, Command &out,
                    const uint8_t *mac) {
  out = Command();
  if (p.family == Family::Uni61) {
    if (p.modelCode == 60 || p.modelCode == 202) {
      if (step == 0) {
        beCommand(0x5f, out);
        out.waitMs = 150;
        return true;
      }
      if (step == 1) {
        beCommand(0x5d, out);
        return true;
      }
    } else if (step == 0) {
      beCommand(0x5d, out);
      return true;
    }
  } else if (p.family == Family::Uni181 && step == 0) {
    leCommand(5, 1, true, out);
    return true;
  } else if (p.family == Family::Uni171 && step == 0) {
    leCommand(10, 1, true, out);
    return true;
  } else if (p.family == Family::Brymen && step == 0) {
    out.size = 32;
    out.withResponse = true;
    out.readReply = true;
    out.data[0] = 0xff;
    out.data[1] = 1;
    out.data[2] = 32;
    out.data[3] = 1;
    out.data[4] = 1;
    if (mac)
      for (int i = 0; i < 6; i++)
        out.data[5 + i] = mac[5 - i];
    out.data[11] = 0x51;
    out.data[12] = 1;
    out.data[13] = 1;
    detail::seal(out.data, 32);
    return true;
  }
  return false;
}
bool pollCommand(const Profile &p, Command &out) {
  out = Command();
  if (p.family == Family::Uni61) {
    beCommand(0x5e, out);
    return true;
  }
  return false;
}
LoginResult judgeLogin(const uint8_t *p, size_t n, uint16_t *code) {
  if (code)
    *code = 0;
  if (!p || n < 32 || memcmp(p, "\xff\x01\x20\x02", 4) || p[30] != 0xff ||
      p[31] != 3 || detail::crc16(p + 2, 26) != detail::le16(p + 28))
    return LoginResult::Unknown;
  if (detail::le16(p + 11) == 0x151)
    return LoginResult::Accepted;
  if (detail::le16(p + 11) == 0x8001 && detail::le16(p + 14) == 0x151) {
    if (code)
      *code = detail::le16(p + 16);
    return LoginResult::Refused;
  }
  return LoginResult::Unknown;
}
size_t decodePacket(const Profile &p, const uint8_t *data, size_t n,
                    Reading *out, size_t cap) {
  if (!data || !out || !cap)
    return 0;
  using namespace detail;
  switch (p.family) {
  case Family::Uni61:
  case Family::Uni171:
  case Family::Uni181: {
    if (n < 6 || data[0] != 0xab || data[1] != 0xcd)
      return 0;
    bool be = p.family == Family::Uni61;
    size_t offset = be ? 3 : 4;
    size_t length = be ? data[2] : le16(data + 2);
    if (length < 2 || length + offset != n)
      return 0;
    unsigned sum = 0;
    for (size_t i = be ? 0 : 2; i < n - 2; i++)
      sum += data[i];
    unsigned received =
        be ? unsigned(data[n - 2]) * 256 + data[n - 1] : le16(data + n - 2);
    if ((sum & 0xffff) != received)
      return 0;
    if (p.family == Family::Uni61)
      return uni61(p, data + offset, n - offset - 2, out, cap);
    if (p.family == Family::Uni171)
      return uni171(p, data + offset, n - offset - 2, out, cap);
    return uni181(p, data + offset, n - offset - 2, out, cap);
  }
  case Family::Uni71:
    return uni71(p, data, n, out, cap);
  case Family::Owon6:
  case Family::Owon15:
    return owon(p, data, n, out, cap);
  case Family::Zotek:
    return zotek(p, data, n, out, cap);
  case Family::Brymen:
    return brymen(p, data, n, out, cap);
  case Family::Eevblog:
    return eevblog(p, data, n, out, cap);
  }
  return 0;
}
size_t Decoder::push(const uint8_t *data, size_t n, Reading *out, size_t cap) {
  if (!profile_ || (!data && n) || (!out && cap))
    return 0;
  size_t produced = 0;
  size_t dropped = 0;
  bool published = false;
  publishedOffset_ = 0;
  for (size_t i = 0; i < n; i++) {
    if (size_ == sizeof(buffer_)) {
      memmove(buffer_, buffer_ + 1, --size_);
      ++dropped;
    }
    buffer_[size_++] = data[i];
    for (;;) {
      if (!size_)
        break;
      size_t wanted = 0;
      bool prefix = true;
      Family f = profile_->family;
      if (f == Family::Uni61 || f == Family::Uni171 || f == Family::Uni181) {
        prefix = buffer_[0] == 0xab && (size_ < 2 || buffer_[1] == 0xcd);
        if (prefix && size_ >= 3 && f == Family::Uni61)
          wanted = buffer_[2] + 3;
        if (prefix && size_ >= 4 && f != Family::Uni61)
          wanted = detail::le16(buffer_ + 2) + 4;
        if (wanted && (wanted < 6 || wanted > sizeof(buffer_)))
          prefix = false;
        // This decoder publishes the family's 14-byte measurements only.
        // Do not let a corrupt length or a name/command reply hold a later
        // complete reading hostage. The adapter's heartbeat is the exception.
        if (f == Family::Uni61 && wanted && wanted != 19 && wanted != 9)
          prefix = false;
      } else if (f == Family::Zotek) {
        prefix = buffer_[0] == 0x1b && (size_ < 2 || buffer_[1] == 0x84);
        if (prefix && size_ >= 3) {
          uint8_t type = buffer_[2] ^ 0x73;
          wanted = type <= 2 && type ? 10 : type == 3 ? 11 : type == 4 ? 19 : 0;
          if (!wanted)
            prefix = false;
        }
      } else if (f == Family::Brymen) {
        prefix = buffer_[0] == 0xff &&
                 (size_ < 2 || buffer_[1] == 1 || buffer_[1] == 2);
        if (prefix && size_ >= 4) {
          wanted = buffer_[2];
          prefix = (wanted == 24 && buffer_[3] == 4) ||
                   (wanted == 32 && buffer_[3] == 5);
        }
      } else if (f == Family::Owon6) {
        wanted = 6;
        prefix = (size_ < 2 || (buffer_[1] & 0xfc) == 0xf0) &&
                 (aligned_ || size_ < 4 || buffer_[3] == 0);
      } else if (f == Family::Owon15) {
        wanted = 15;
        prefix = (size_ < 3 || buffer_[2] == 0xf0) &&
                 (aligned_ || ((size_ < 2 || !(buffer_[1] & 0xe0)) &&
                               (size_ < 12 || buffer_[11] != 0xf0)));
      } else if (f == Family::Uni71)
        wanted = 11;
      else if (f == Family::Eevblog)
        wanted = buffer_[0] == 0xf2 ? 19 : 18;
      if (prefix && (!wanted || size_ < wanted))
        break;
      if (prefix) {
        Reading local[8];
        size_t count = decodePacket(*profile_, buffer_, wanted, local, 8);
        // An unaligned checksum-free/XOR-framed stream needs the same
        // plausibility gates as upstream. Once aligned, unfamiliar fields still
        // reach the caller with UNKNOWN rather than silently inventing a known
        // scale.
        if (!aligned_ && count && f == Family::Zotek &&
            (local[0].flags & UNKNOWN))
          count = 0;
        if (!aligned_ && count && f == Family::Eevblog) {
          const unsigned shift = wanted == 18 ? 1 : 0;
          if ((local[0].flags & UNKNOWN) || (buffer_[5 - shift] & 0x20) ||
              (buffer_[10 - shift] & 0x08) || (buffer_[13 - shift] & 0xe0) ||
              (buffer_[14 - shift] & 0xc0) || (buffer_[17 - shift] & 0x80))
            count = 0;
        }
        bool bmInfo =
            f == Family::Brymen && wanted == 24 && buffer_[0] == 0xff &&
            detail::crc16(buffer_ + 2, 18) == detail::le16(buffer_ + 20) &&
            buffer_[22] == 0xff && buffer_[23] == 3;
        if (bmInfo)
          brymenLowBattery_ = buffer_[12] & 2;
        if (f == Family::Brymen && brymenLowBattery_)
          for (size_t k = 0; k < count; k++)
            local[k].flags |= LOW_BATT;
        bool ignored =
            bmInfo ||
            (profile_->gatt->stripsHeartbeat && wanted == 9 &&
             !memcmp(buffer_, "\xab\xcd\x06\xaa\xaa\x6e\x67\x03\xa7", 9));
        if (count || ignored) {
          if (count && !published) {
            publishedOffset_ = dropped;
            published = true;
          }
          aligned_ = true;
          for (size_t k = 0; k < count; k++)
            if (produced < cap)
              out[produced++] = local[k];
          size_ -= wanted;
          memmove(buffer_, buffer_ + wanted, size_);
          dropped += wanted;
          continue;
        }
      }
      aligned_ = false;
      --size_;
      memmove(buffer_, buffer_ + 1, size_);
      ++dropped;
    }
  }
  return produced;
}
} // namespace dmm
