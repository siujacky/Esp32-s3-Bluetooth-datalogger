// SPDX-License-Identifier: GPL-3.0-or-later
#include <dmm_protocols.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
using namespace dmm;
void setUp() {}
void tearDown() {}
static void nearValue(double tolerance, double expected, double actual,
                      const char *message = "numeric value") {
  if (!isfinite(actual) || fabs(expected - actual) > tolerance) {
    printf("Expected %.17g got %.17g tolerance %.3g\n", expected, actual,
           tolerance);
    TEST_FAIL_MESSAGE(message);
  }
}
struct Golden {
  const char *name, *profile;
  uint8_t payload[100];
  size_t size;
  bool numeric;
  double value;
  const char *unit, *mode;
  uint8_t channel;
  uint32_t flags;
};
#include "goldens.inc"
#include "vectors.inc"
static size_t frame(const Profile &p, const uint8_t *payload, size_t size,
                    uint8_t *out) {
  bool be = p.family == Family::Uni61;
  size_t offset = be ? 3 : 4, n = offset + size + 2;
  out[0] = 0xab;
  out[1] = 0xcd;
  out[2] = size + 2;
  if (!be)
    out[3] = 0;
  memcpy(out + offset, payload, size);
  unsigned sum = 0;
  for (size_t i = be ? 0 : 2; i < n - 2; i++)
    sum += out[i];
  out[n - 2] = be ? sum >> 8 : sum;
  out[n - 1] = be ? sum : sum >> 8;
  return n;
}
static size_t packet(const char *id, const uint8_t *b, size_t n, Reading *r) {
  const Profile *p = findProfile(id);
  TEST_ASSERT_NOT_NULL(p);
  return decodePacket(*p, b, n, r, 8);
}
void test_registry_and_real_ut60_capture() {
  const Profile *p = findProfile("ut60bt");
  TEST_ASSERT_NOT_NULL(p);
  const uint8_t f[] = {0xab, 0xcd, 0x10, 0x02, 0x30, 0x20, 0x2d,
                       0x20, 0x20, 0x31, 0x2e, 0x37, 0,    0,
                       0,    0,    0,    0x02, 0xdd};
  Reading r[8];
  TEST_ASSERT_EQUAL(1, decodePacket(*p, f, sizeof(f), r, 8));
  TEST_ASSERT_TRUE(r[0].numeric);
  TEST_ASSERT_EQUAL_STRING("-1.7", r[0].text);
  TEST_ASSERT_EQUAL_STRING("mV", r[0].unit);
  TEST_ASSERT_FLOAT_WITHIN(.0001, -1.7, r[0].value);
}
void test_all_upstream_real_golden_fixtures() {
  for (const auto &g : goldens) {
    const Profile *p = findProfile(g.profile);
    uint8_t b[120];
    size_t n = frame(*p, g.payload, g.size, b);
    Reading r[8];
    size_t count = decodePacket(*p, b, n, r, 8);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, count, g.name);
    TEST_ASSERT_EQUAL_MESSAGE(g.numeric, r[0].numeric, g.name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.unit, r[0].unit, g.name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.mode, r[0].mode, g.name);
    TEST_ASSERT_EQUAL_MESSAGE(g.channel, r[0].channel, g.name);
    if (g.numeric)
      nearValue(fmax(1e-9, fabs(g.value) * 1e-6), g.value, r[0].value, g.name);
    uint32_t mask = HOLD | REL | MIN | MAX | LOW_BATT | CAUTION | AUTO_RANGE |
                    AVG | RECORD | LOZ | LEAD_ERROR | COMP | PEAK_MIN |
                    PEAK_MAX;
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(g.flags & mask, r[0].flags & mask, g.name);
    // Every possible boundary exercises actual buffering and excludes partial
    // publication.
    for (size_t split = 0; split < n; split++) {
      Decoder d(p);
      TEST_ASSERT_EQUAL_MESSAGE(0, d.push(b, split, r, 8), g.name);
      size_t got = d.push(b + split, n - split, r, 8);
      TEST_ASSERT_GREATER_THAN_MESSAGE(0, got, g.name);
      TEST_ASSERT_EQUAL_MESSAGE(g.numeric, r[0].numeric, g.name);
    }
    b[n - 1] ^= 1;
    TEST_ASSERT_EQUAL_MESSAGE(0, decodePacket(*p, b, n, r, 8), g.name);
  }
}
void test_owon_6byte_worked_and_15byte_captured_vectors() {
  Reading r[8];
  const uint8_t six[] = {0x19, 0xf0, 4, 0, 0xbd, 9};
  TEST_ASSERT_EQUAL(1, packet("b35t+", six, 6, r));
  TEST_ASSERT_EQUAL_STRING("249.3", r[0].text);
  TEST_ASSERT_EQUAL_STRING("mV", r[0].unit);
  const char *text[] = {
      "UL",     "-1.0054", "OL",      "24.9",   "0.0",    "0",      "0.5409",
      "540.74", "57.1",    "369.7",   "0.5507", "0.0694", "50.003", "52.42",
      "22.6",   "78.3",    "-1592.6", "167.2",  "-1.680", "0.98",   "0.2893",
      "0.078",  "473.8",   "-0.001",  "0.00",   "0",      "0.4",    "0.000"};
  const char *units[] = {"MF", "V",  "mV", "°C", "W",   "Wh", "V",
                         "mV", "mV", "kΩ", "V",  "nF",  "Hz", "%",
                         "°C", "°F", "µA", "µA", "mA",  "mA", "A",
                         "A",  "W",  "",   "V",  "mAh", "V",  "A"};
  TEST_ASSERT_EQUAL(28, sizeof(owVectors) / 15);
  for (unsigned i = 0; i < 28; i++) {
    TEST_ASSERT_GREATER_THAN(0, packet("vc871", owVectors[i], 15, r));
    TEST_ASSERT_EQUAL_STRING(text[i], r[0].text);
    TEST_ASSERT_EQUAL_STRING(units[i], r[0].unit);
    Decoder d(findProfile("vc871"));
    for (unsigned j = 0; j < 14; j++)
      TEST_ASSERT_EQUAL(0, d.push(owVectors[i] + j, 1, r, 8));
    TEST_ASSERT_GREATER_THAN(0, d.push(owVectors[i] + 14, 1, r, 8));
  }
}
void test_121gw_examples_and_body_only_packets() {
  Reading r[8];
  const double values[] = {1.2345, 87.654, NAN, -12.345, 0, NAN};
  const char *units[] = {"V", "Hz", "MΩ", "mV", "%", "MΩ"};
  for (unsigned i = 0; i < 6; i++) {
    TEST_ASSERT_GREATER_THAN(0, packet("121gw", gwVectors[i], 19, r));
    TEST_ASSERT_EQUAL_STRING(units[i], r[0].unit);
    if (isnan(values[i]))
      TEST_ASSERT_FALSE(r[0].numeric);
    else
      nearValue(.000001, values[i], r[0].value);
    TEST_ASSERT_GREATER_THAN(0, packet("121gw", gwVectors[i] + 1, 18, r));
    for (size_t split = 0; split < 18; split++) {
      Decoder d(findProfile("121gw"));
      TEST_ASSERT_EQUAL(0, d.push(gwVectors[i] + 1, split, r, 8));
      TEST_ASSERT_GREATER_THAN(
          0, d.push(gwVectors[i] + 1 + split, 18 - split, r, 8));
    }
  }
}
void test_zotek_all_four_upstream_worked_examples() {
  const uint8_t z[][19] = {
      {0x1b, 0x84, 0x70, 0x41, 0x08, 0x5c, 0x7d, 0x7f, 0x66, 0xfa, 0x3a},
      {0x1b, 0x84, 0x72, 0xf3, 0x2f, 0x2e, 0xe9, 0xd6, 0x66, 0xaa},
      {0x1b, 0x84, 0x71, 0x15, 0x3c, 0x2b, 0xd9, 0xfa, 0x66, 0xa9},
      {0x1b, 0x84, 0x77, 0x51, 0xf2, 0x2a, 0xc9, 0x9a, 0xa1, 0x6d, 0x75, 0x5f,
       0x5f, 0xa6, 0x33, 0x14, 0x20, 0x1a, 0xaa}};
  const size_t lengths[] = {11, 10, 10, 19};
  const double values[] = {-12.34, 230.5, 4.700, 1.2345};
  const char *units[] = {"V", "V", "kΩ", "V"};
  Reading r[8];
  for (unsigned i = 0; i < 4; i++) {
    TEST_ASSERT_GREATER_THAN(0, packet("zt5b", z[i], lengths[i], r));
    TEST_ASSERT_EQUAL_STRING(units[i], r[0].unit);
    nearValue(.00001, values[i], r[0].value);
    for (size_t split = 0; split < lengths[i]; split++) {
      Decoder d(findProfile("zt5b"));
      TEST_ASSERT_EQUAL(0, d.push(z[i], split, r, 8));
      TEST_ASSERT_GREATER_THAN(0,
                               d.push(z[i] + split, lengths[i] - split, r, 8));
    }
  }
}
void test_brymen_worked_crc_and_login() {
  const uint8_t b[] = {0xff, 2,    0x20, 5, 1,    0,    0,    1,
                       0xfa, 0x78, 0x85, 3, 0x3a, 0x35, 0x10, 0x40,
                       0,    1,    3,    0, 1,    0xc7, 0xcf, 0xff,
                       1,    0,    2,    5, 0x76, 0xc3, 0xff, 3};
  Reading r[8];
  TEST_ASSERT_EQUAL(1, packet("bm78xbt", b, 32, r));
  TEST_ASSERT_EQUAL_STRING("DC V", r[0].mode);
  TEST_ASSERT_EQUAL_STRING("-1.2345", r[0].text);
  TEST_ASSERT_EQUAL_STRING("V", r[0].unit);

  Command c;
  const uint8_t mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  TEST_ASSERT_TRUE(startupCommand(*findProfile("bm78xbt"), 0, c, mac));
  const uint8_t expected[] = {0xff, 1,    0x20, 1,    1,    0x66, 0x55, 0x44,
                              0x33, 0x22, 0x11, 0x51, 1,    1,    0,    0,
                              0,    0,    0,    0,    0,    0,    0,    0,
                              0,    0,    0,    0,    0x45, 0x43, 0xff, 3};
  TEST_ASSERT_EQUAL_MEMORY(expected, c.data, 32);
  TEST_ASSERT_TRUE(c.withResponse && c.readReply);
  TEST_ASSERT_EQUAL(155, findProfile("bm78xbt")->gatt->minMtu);
  TEST_ASSERT_EQUAL(500, findProfile("bm78xbt")->gatt->settleMs);
  for (size_t split = 0; split < 32; split++) {
    Decoder d(findProfile("bm78xbt"));
    TEST_ASSERT_EQUAL(0, d.push(b, split, r, 8));
    TEST_ASSERT_EQUAL(1, d.push(b + split, 32 - split, r, 8));
  }
  uint8_t bad[32];
  memcpy(bad, b, 32);
  bad[22] ^= 1;
  TEST_ASSERT_EQUAL(0, packet("bm78xbt", bad, 32, r));
  TEST_ASSERT_EQUAL(int(LoginResult::Unknown), int(judgeLogin(c.data, 32)));
}
void test_ut171_float_payload_and_ut71_parity() {
  const uint8_t payload[] = {2, 0, 1, 2, 1, 0x19, 4, 0x9e, 0x3f, 0x40, 0};
  uint8_t f[40];
  Reading r[8];
  size_t n = frame(*findProfile("ut171c"), payload, sizeof(payload), f);
  TEST_ASSERT_EQUAL(1, packet("ut171c", f, n, r));
  TEST_ASSERT_EQUAL_STRING("1.2345", r[0].text);
  TEST_ASSERT_EQUAL_STRING("V", r[0].unit);
  const uint8_t dual[] = {2,    1, 1, 3, 1,    0x19, 4,    0x9e, 0x3f,
                          0x40, 1, 0, 0, 0x48, 0x42, 0x20, 18};
  n = frame(*findProfile("ut171c"), dual, sizeof(dual), f);
  TEST_ASSERT_EQUAL(2, packet("ut171c", f, n, r));
  TEST_ASSERT_EQUAL_STRING("Frequency", r[1].mode);
  TEST_ASSERT_EQUAL_STRING("Hz", r[1].unit);
  TEST_ASSERT_EQUAL_STRING("50.00", r[1].text);
  const uint8_t ut71[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x31,
                          0x31, 0x32, 0x31, 0x0d, 0x8a};
  TEST_ASSERT_EQUAL(1, packet("ut71ab", ut71, 11, r));
  TEST_ASSERT_EQUAL_STRING("1.2345", r[0].text);
  TEST_ASSERT_EQUAL_STRING("V", r[0].unit);
}
void test_transport_registry_and_startup() {
  TEST_ASSERT_GREATER_OR_EQUAL(35, profileCount());
  TEST_ASSERT_NULL(findProfile("b35"));
  TEST_ASSERT_NULL(findProfile("b35t"));
  TEST_ASSERT_NULL(profileForName("BDM"));
  TEST_ASSERT_NULL(profileForName("Bluetooth DMM"));
  const uint8_t info[] = {92, 1, 2, 3};
  TEST_ASSERT_EQUAL_PTR(findProfile("vc925"), profileForOwonInfo(info, 4));
  const uint8_t bad[] = {200};
  TEST_ASSERT_NULL(profileForOwonInfo(bad, 1));
  TEST_ASSERT_EQUAL_STRING("0000fff3-0000-1000-8000-00805f9b34fb",
                           findProfile("cms061")->gatt->writeUuid);
  Command c;
  TEST_ASSERT_TRUE(startupCommand(*findProfile("ut171c"), 0, c));
  const uint8_t start[] = {0xab, 0xcd, 4, 0, 10, 1, 15, 0};
  TEST_ASSERT_EQUAL_MEMORY(start, c.data, 8);
  TEST_ASSERT_FALSE(pollCommand(*findProfile("zt5b"), c));
  TEST_ASSERT_TRUE(pollCommand(*findProfile("ut60bt"), c));
  const uint8_t poll[] = {0xab, 0xcd, 3, 0x5e, 1, 0xd9};
  TEST_ASSERT_EQUAL_MEMORY(poll, c.data, 6);
}

static void sealBm(uint8_t *p, size_t n) {
  uint16_t c = 0xffff;
  for (size_t i = 2; i < n - 4; i++) {
    c ^= p[i];
    for (int j = 0; j < 8; j++)
      c = c & 1 ? (c >> 1) ^ 0xa001 : c >> 1;
  }
  p[n - 4] = c;
  p[n - 3] = c >> 8;
  p[n - 2] = 0xff;
  p[n - 1] = 3;
}
void test_brymen_information_battery_and_login_refusal() {
  uint8_t info[] = {0xff, 1, 0x18, 4, 1, 2, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
                    2,    0, 0,    0, 4, 0, 0,    1,    0xfc, 0x94, 0xff, 3};
  sealBm(info, sizeof(info));
  const uint8_t b[] = {0xff, 2,    0x20, 5, 1,    0,    0,    1,
                       0xfa, 0x78, 0x85, 3, 0x3a, 0x35, 0x10, 0x40,
                       0,    1,    3,    0, 1,    0xc7, 0xcf, 0xff,
                       1,    0,    2,    5, 0x76, 0xc3, 0xff, 3};
  Reading r[8];
  Decoder d(findProfile("bm78xbt"));
  TEST_ASSERT_EQUAL(0, d.push(info, sizeof(info), r, 8));
  TEST_ASSERT_EQUAL(1, d.push(b, sizeof(b), r, 8));
  TEST_ASSERT_TRUE(r[0].flags & LOW_BATT);
  Command c;
  startupCommand(*findProfile("bm78xbt"), 0, c);
  c.data[3] = 2;
  sealBm(c.data, 32);
  TEST_ASSERT_EQUAL(int(LoginResult::Accepted), int(judgeLogin(c.data, 32)));
  c.data[11] = 1;
  c.data[12] = 0x80;
  c.data[14] = 0x51;
  c.data[15] = 1;
  c.data[16] = 3;
  sealBm(c.data, 32);
  uint16_t refusal = 0;
  TEST_ASSERT_EQUAL(int(LoginResult::Refused),
                    int(judgeLogin(c.data, 32, &refusal)));
  TEST_ASSERT_EQUAL(3, refusal);
  c.data[22] ^= 1;
  TEST_ASSERT_EQUAL(int(LoginResult::Unknown), int(judgeLogin(c.data, 32)));
}

struct ZCase {
  uint8_t type, ann[19];
  const char *mode, *unit;
  uint32_t flags;
};
#include "zotek_annunciators.inc"
void test_all_upstream_zotek_annunciator_cases() {
  const uint8_t key[] = {0x41, 0x21, 0x73, 0x55, 0xa2, 0xc1, 0x32,
                         0x71, 0x66, 0xaa, 0x3b, 0xd0, 0xe2, 0xa8,
                         0x33, 0x14, 0x20, 0x1a, 0xaa, 0xbb};
  for (const auto &c : zCases) {
    uint8_t b[19] = {0x5a, 0xa5, c.type};
    size_t n = c.type == 4 ? 19 : c.type == 3 ? 11 : 10;
    if (c.type == 4) {
      b[9] = 0xc7;
      b[10] = 0x4e;
      b[11] = 0x8f;
      b[12] = 0xbd;
      b[13] = 0x0c;
    } else {
      b[4] = 0xaa;
      b[5] = 0x9d;
      b[6] = 0x4f;
      b[7] = 0x0e;
    }
    for (size_t i = 3; i < n; i++)
      b[i] |= c.ann[i];
    for (size_t i = 0; i < n; i++)
      b[i] ^= key[i];
    Reading r[8];
    TEST_ASSERT_GREATER_THAN(0, packet("zt5b", b, n, r));
    TEST_ASSERT_EQUAL_STRING(c.mode, r[0].mode);
    TEST_ASSERT_EQUAL_STRING(c.unit, r[0].unit);
    const uint32_t mask =
        HOLD | REL | MIN | MAX | LOW_BATT | CAUTION | AUTO_RANGE | DC;
    TEST_ASSERT_EQUAL_HEX32(c.flags, r[0].flags & mask);
  }
}
void test_cms_captured_scope_ncv_and_maximum() {
  const uint8_t scope[] = {0xda, 3, 0xf0, 0xc3, 0x20, 0x10, 0xe1, 1,
                           0xf0, 0, 0,    0,    0,    0,    4};
  Reading r[8];
  TEST_ASSERT_EQUAL(1, packet("cms061", scope, 15, r));
  TEST_ASSERT_EQUAL_STRING("Scope", r[0].mode);
  TEST_ASSERT_EQUAL_STRING("A", r[0].unit);
  TEST_ASSERT_FALSE(r[0].numeric);
  for (unsigned level = 0; level <= 4; level++) {
    uint8_t ncv[] = {0x61, 3, 0xf0, 0, 0, 0, 0xa3, 1, 0xf0, 0, 0, 0, 4, 0, 0};
    ncv[3] = level;
    TEST_ASSERT_EQUAL(1, packet("cms061", ncv, 15, r));
    TEST_ASSERT_EQUAL_STRING("NCV", r[0].mode);
    TEST_ASSERT_FALSE(r[0].numeric);
  }
  const uint8_t maximum[] = {0x22, 0x10, 0xf0, 0x53, 0xf,  0x80, 0x22, 0x10,
                             0xf0, 0x4f, 0xf,  0x80, 0x20, 0,    0};
  TEST_ASSERT_EQUAL(2, packet("cms061", maximum, 15, r));
  TEST_ASSERT_EQUAL_STRING("-39.23", r[0].text);
  TEST_ASSERT_EQUAL_STRING("Max", r[1].mode);
  TEST_ASSERT_EQUAL_STRING("-39.19", r[1].text);
}
void test_model_range_scaling_and_secondary_channels() {
  struct C {
    const char *id;
    uint8_t mode, range;
    const char *unit;
  };
  const C cases[] = {{"ut60bt", 2, 0, "mV"},    {"ut60bt", 2, 1, "V"},
                     {"ut60bt", 14, 0, "mA"},   {"ut60bt", 14, 1, "A"},
                     {"ut61eplus", 6, 1, "kΩ"}, {"ut61b+", 9, 2, "µF"},
                     {"ut61d+", 10, 1, "°C"},   {"ut161b", 12, 1, "µA"},
                     {"ut161d", 16, 1, "A"},    {"ut202bt", 24, 2, "V"},
                     {"ut202bt", 24, 0, ""}};
  for (const auto &c : cases) {
    uint8_t payload[14] = {c.mode, uint8_t(0x30 | c.range),
                           ' ',    ' ',
                           '1',    '.',
                           '2',    '3',
                           '4',    0,
                           0,      0x30,
                           0x30,   0x30};
    uint8_t b[30];
    const Profile *p = findProfile(c.id);
    size_t n = frame(*p, payload, 14, b);
    Reading r[8];
    TEST_ASSERT_EQUAL(1, decodePacket(*p, b, n, r, 8));
    TEST_ASSERT_EQUAL_STRING(c.unit, r[0].unit);
    nearValue(.000001, 1.234, r[0].value);
  }
  uint8_t secondary[] = {0x84, 0x30, ' ', '1', '2', '3', '.',
                         '4',  '5',  0,   0,   0,   0,   0};
  uint8_t b[30];
  size_t n = frame(*findProfile("ut202bt"), secondary, 14, b);
  Reading r[8];
  TEST_ASSERT_EQUAL(1, packet("ut202bt", b, n, r));
  TEST_ASSERT_EQUAL(1, r[0].channel);
  TEST_ASSERT_EQUAL_STRING("Hz", r[0].unit);
  Command c;
  TEST_ASSERT_TRUE(startupCommand(*findProfile("ut202bt"), 0, c));
  TEST_ASSERT_EQUAL(0x5f, c.data[3]);
}

void test_stream_resynchronizes_bad_length_and_checksums_without_backlog() {
  const Profile *p = findProfile("ut60bt");
  const uint8_t payload[] = {2,   0x30, ' ', ' ', '1',  '.',  '2',
                             '3', '4',  0,   0,   0x30, 0x30, 0x30};
  uint8_t b[30];
  size_t n = frame(*p, payload, 14, b);
  Reading r[8];
  Decoder d(p);
  const uint8_t garbage[] = {0, 0xab, 0xcd, 0xff, 0x55, 0xaa};
  TEST_ASSERT_EQUAL(0, d.push(garbage, sizeof(garbage), r, 8));
  TEST_ASSERT_EQUAL(1, d.push(b, n, r, 8));
  uint8_t bad[30];
  memcpy(bad, b, n);
  bad[n - 1] ^= 1;
  TEST_ASSERT_EQUAL(0, d.push(bad, n, r, 8));
  TEST_ASSERT_EQUAL(1, d.push(b, n, r, 8));
  uint8_t two[60];
  memcpy(two, b, n);
  memcpy(two + n, b, n);
  TEST_ASSERT_EQUAL(1, d.push(two, n * 2, r, 1));
  TEST_ASSERT_EQUAL(0, d.push(nullptr, 0, r, 8));
  TEST_ASSERT_EQUAL(0, d.push(b, 8, r, 8));
  d.reset();
  TEST_ASSERT_EQUAL(0, d.push(b + 8, n - 8, r, 8));
  TEST_ASSERT_EQUAL(1, d.push(b, n, r, 8));
}
void test_121gw_va_operands_have_stable_distinct_channels() {
  uint8_t b[19];
  memcpy(b, gwVectors[0], 19);
  b[5] = 13;
  b[6] = 0;
  b[9] = 16;
  b[10] = 2;
  b[11] = 4;
  b[12] = 0xd2;
  b[18] = 0;
  for (unsigned i = 0; i < 18; i++)
    b[18] ^= b[i];
  Reading r[8];
  TEST_ASSERT_EQUAL(2, packet("121gw", b, 19, r));
  TEST_ASSERT_EQUAL(2, r[1].channel);
  TEST_ASSERT_EQUAL_STRING("µA", r[1].unit);
  TEST_ASSERT_EQUAL_STRING("12.34", r[1].text);
  uint32_t currentKey = r[1].modeKey;
  b[9] = 1;
  b[10] = 3;
  b[18] = 0;
  for (unsigned i = 0; i < 18; i++)
    b[18] ^= b[i];
  TEST_ASSERT_EQUAL(2, packet("121gw", b, 19, r));
  TEST_ASSERT_EQUAL(1, r[1].channel);
  TEST_ASSERT_EQUAL_STRING("V", r[1].unit);
  TEST_ASSERT_EQUAL_STRING("1.234", r[1].text);
  TEST_ASSERT_NOT_EQUAL(currentKey, r[1].modeKey);
}
// Two consecutive real UT202BT notifications captured on 2026-11-12.
void test_physical_ut202_main_and_auxiliary_readings(){
 const uint8_t mainFrame[]={0xab,0xcd,0x10,0x18,0x32,0x20,0x20,0x20,0x20,0x30,0x2e,0x30,0x00,0x00,0x30,0x34,0x38,0x03,0x7c};
 const uint8_t auxiliaryFrame[]={0xab,0xcd,0x10,0x84,0x30,0x20,0x20,0x20,0x20,0x20,0x30,0x20,0x00,0x00,0x30,0x34,0x38,0x03,0xc8};
 Reading r[8];TEST_ASSERT_EQUAL(1,packet("ut202bt",mainFrame,sizeof(mainFrame),r));
 TEST_ASSERT_TRUE(r[0].numeric);nearValue(.0001,0,r[0].value);TEST_ASSERT_EQUAL_STRING("0.0",r[0].text);TEST_ASSERT_EQUAL_STRING("V",r[0].unit);TEST_ASSERT_EQUAL_STRING("LPF V",r[0].mode);TEST_ASSERT_EQUAL(0,r[0].channel);
 TEST_ASSERT_EQUAL(1,packet("ut202bt",auxiliaryFrame,sizeof(auxiliaryFrame),r));TEST_ASSERT_TRUE(r[0].numeric);nearValue(.0001,0,r[0].value);TEST_ASSERT_EQUAL_STRING("Hz",r[0].unit);TEST_ASSERT_EQUAL(1,r[0].channel);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_121gw_va_operands_have_stable_distinct_channels);
  RUN_TEST(test_stream_resynchronizes_bad_length_and_checksums_without_backlog);
  RUN_TEST(test_all_upstream_zotek_annunciator_cases);
  RUN_TEST(test_cms_captured_scope_ncv_and_maximum);
  RUN_TEST(test_model_range_scaling_and_secondary_channels);
  RUN_TEST(test_brymen_information_battery_and_login_refusal);
  RUN_TEST(test_registry_and_real_ut60_capture);
  RUN_TEST(test_all_upstream_real_golden_fixtures);
  RUN_TEST(test_owon_6byte_worked_and_15byte_captured_vectors);
  RUN_TEST(test_121gw_examples_and_body_only_packets);
  RUN_TEST(test_zotek_all_four_upstream_worked_examples);
  RUN_TEST(test_brymen_worked_crc_and_login);
  RUN_TEST(test_ut171_float_payload_and_ut71_parity);
  RUN_TEST(test_transport_registry_and_startup);
  RUN_TEST(test_physical_ut202_main_and_auxiliary_readings);
  return UNITY_END();
}
