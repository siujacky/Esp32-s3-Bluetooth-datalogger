// SPDX-License-Identifier: GPL-3.0-or-later
#include <dmm_protocols.h>
#include <string.h>
#include <unity.h>
using namespace dmm;
void setUp() {}
void tearDown() {}
// Captured physical UT60BT notification from the existing project evidence.
static const uint8_t capture[] = {0xab, 0xcd, 0x10, 0x02, 0x30, 0x20, 0x2d,
                                  0x20, 0x20, 0x31, 0x2e, 0x37, 0,    0,
                                  0,    0,    0,    0x02, 0xdd};
void test_split_physical_capture_is_stamped_from_oldest_fragment() {
  for (size_t split = 1; split < sizeof(capture); split++) {
    AgedDecoder d(findProfile("ut60bt"));
    Reading r[8];
    uint32_t at = 42;
    TEST_ASSERT_EQUAL(0, d.push(capture, split, 100, r, 8, at));
    TEST_ASSERT_EQUAL(42, at);
    TEST_ASSERT_EQUAL(split, d.pendingBytes());
    TEST_ASSERT_EQUAL(
        1, d.push(capture + split, sizeof(capture) - split, 2100, r, 8, at));
    TEST_ASSERT_EQUAL(100, at);
    TEST_ASSERT_EQUAL_STRING("-1.7", r[0].text);
    TEST_ASSERT_EQUAL_STRING("mV", r[0].unit);
    TEST_ASSERT_EQUAL(0, d.pendingBytes());
  }
}
void test_old_prefix_and_late_tail_are_rejected_then_full_packet_recovers() {
  AgedDecoder d(findProfile("ut60bt"));
  Reading r[8];
  uint32_t at = 42;
  TEST_ASSERT_EQUAL(0, d.push(capture, 7, 100, r, 8, at));
  TEST_ASSERT_EQUAL(0,
                    d.push(capture + 7, sizeof(capture) - 7, 7100, r, 8, at));
  TEST_ASSERT_EQUAL(42, at);
  TEST_ASSERT_EQUAL(0, d.pendingBytes());
  TEST_ASSERT_EQUAL(1, d.push(capture, sizeof(capture), 7100, r, 8, at));
  TEST_ASSERT_EQUAL(7100, at);
  TEST_ASSERT_EQUAL_STRING("-1.7", r[0].text);
}
void test_drip_fragments_do_not_refresh_oldest_byte_age() {
  AgedDecoder d(findProfile("ut60bt"));
  Reading r[8];
  uint32_t at = 42;
  for (size_t i = 0; i < sizeof(capture); i++)
    TEST_ASSERT_EQUAL(
        0, d.push(capture + i, 1, 100 + uint32_t(i) * 1000, r, 8, at));
  TEST_ASSERT_EQUAL(42, at);
  TEST_ASSERT_EQUAL(1, d.push(capture, sizeof(capture), 20100, r, 8, at));
  TEST_ASSERT_EQUAL(20100, at);
}
void test_capture_timestamps_and_expiry_handle_clock_wrap() {
  Reading r[8];
  uint32_t at = 42;
  AgedDecoder fresh(findProfile("ut60bt"));
  TEST_ASSERT_EQUAL(0, fresh.push(capture, 7, 0xfffffff0u, r, 8, at));
  TEST_ASSERT_EQUAL(
      1, fresh.push(capture + 7, sizeof(capture) - 7, 2000, r, 8, at));
  TEST_ASSERT_EQUAL_HEX32(0xfffffff0u, at);
  AgedDecoder expired(findProfile("ut60bt"));
  TEST_ASSERT_EQUAL(0, expired.push(capture, 7, 0xfffffff0u, r, 8, at));
  TEST_ASSERT_EQUAL(
      0, expired.push(capture + 7, sizeof(capture) - 7, 4096, r, 8, at));
  TEST_ASSERT_EQUAL(1, expired.push(capture, sizeof(capture), 4097, r, 8, at));
  TEST_ASSERT_EQUAL(4097, at);
}
void test_age_boundary_configuration_and_empty_tick() {
  Reading r[8];
  uint32_t at = 42;
  AgedDecoder equal(findProfile("ut60bt"), 250);
  TEST_ASSERT_EQUAL(0, equal.push(capture, 7, 100, r, 8, at));
  TEST_ASSERT_EQUAL(
      1, equal.push(capture + 7, sizeof(capture) - 7, 350, r, 8, at));
  TEST_ASSERT_EQUAL(100, at);
  AgedDecoder over(findProfile("ut60bt"), 250);
  TEST_ASSERT_EQUAL(0, over.push(capture, 7, 100, r, 8, at));
  TEST_ASSERT_EQUAL(0,
                    over.push(capture + 7, sizeof(capture) - 7, 351, r, 8, at));
  AgedDecoder tick(findProfile("ut60bt"));
  TEST_ASSERT_EQUAL(0, tick.push(capture, 7, 100, r, 8, at));
  TEST_ASSERT_EQUAL(0, tick.push(nullptr, 0, 7100, r, 8, at));
  TEST_ASSERT_EQUAL(0, tick.pendingBytes());
}
void test_multiple_packets_stamp_oldest_and_suffix_keeps_own_age() {
  for (size_t capacity = 1; capacity <= 8; capacity += 7) {
    AgedDecoder d(findProfile("ut60bt"));
    Reading r[8];
    uint32_t at = 42;
    TEST_ASSERT_EQUAL(0, d.push(capture, 7, 100, r, 8, at));
    uint8_t chunk[50];
    size_t used = 0;
    memcpy(chunk + used, capture + 7, sizeof(capture) - 7);
    used += sizeof(capture) - 7;
    memcpy(chunk + used, capture, sizeof(capture));
    used += sizeof(capture);
    memcpy(chunk + used, capture, 5);
    used += 5;
    TEST_ASSERT_EQUAL(capacity == 1 ? 1 : 2,
                      d.push(chunk, used, 2100, r, capacity, at));
    TEST_ASSERT_EQUAL(100, at);
    TEST_ASSERT_EQUAL(5, d.pendingBytes());
    TEST_ASSERT_EQUAL(1,
                      d.push(capture + 5, sizeof(capture) - 5, 4000, r, 8, at));
    TEST_ASSERT_EQUAL(2100, at);
    TEST_ASSERT_EQUAL(0, d.pendingBytes());
  }
}
void test_noise_timestamps_and_dropped_outputs_do_not_contaminate_packet_stamp() {
  AgedDecoder d(findProfile("ut60bt"));
  Reading r[8];
  uint32_t at = 42;
  const uint8_t noise[] = {0, 0xab};
  TEST_ASSERT_EQUAL(0, d.push(noise, sizeof(noise), 100, r, 8, at));
  TEST_ASSERT_EQUAL(1, d.push(capture, sizeof(capture), 500, r, 8, at));
  TEST_ASSERT_EQUAL(500, at);
  TEST_ASSERT_EQUAL(0, d.push(capture, sizeof(capture), 1000, nullptr, 0, at));
  TEST_ASSERT_EQUAL(500, at);
  TEST_ASSERT_EQUAL(1, d.push(capture, sizeof(capture), 2000, r, 8, at));
  TEST_ASSERT_EQUAL(2000, at);
}
static void sealBm(uint8_t *p, size_t n) {
  uint16_t crc = 0xffff;
  for (size_t i = 2; i < n - 4; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++)
      crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
  }
  p[n - 4] = crc;
  p[n - 3] = crc >> 8;
  p[n - 2] = 0xff;
  p[n - 1] = 3;
}
void test_discard_partial_preserves_brymen_metadata_but_reset_clears_it() {
  uint8_t info[] = {0xff, 1, 0x18, 4, 1, 2, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
                    2,    0, 0,    0, 4, 0, 0,    1,    0xfc, 0x94, 0xff, 3};
  sealBm(info, sizeof(info));
  const uint8_t reading[] = {0xff, 2,    0x20, 5, 1,    0,    0,    1,
                             0xfa, 0x78, 0x85, 3, 0x3a, 0x35, 0x10, 0x40,
                             0,    1,    3,    0, 1,    0xc7, 0xcf, 0xff,
                             1,    0,    2,    5, 0x76, 0xc3, 0xff, 3};
  Reading r[8];
  Decoder direct(findProfile("bm78xbt"));
  TEST_ASSERT_EQUAL(0, direct.push(info, sizeof(info), r, 8));
  TEST_ASSERT_EQUAL(0, direct.push(reading, 7, r, 8));
  TEST_ASSERT_EQUAL(7, direct.pendingBytes());
  direct.discardPartial();
  TEST_ASSERT_EQUAL(0, direct.pendingBytes());
  TEST_ASSERT_EQUAL(1, direct.push(reading, sizeof(reading), r, 8));
  TEST_ASSERT_TRUE(r[0].flags & LOW_BATT);
  direct.reset();
  TEST_ASSERT_EQUAL(1, direct.push(reading, sizeof(reading), r, 8));
  TEST_ASSERT_FALSE(r[0].flags & LOW_BATT);
  AgedDecoder aged(findProfile("bm78xbt"));
  uint32_t at = 42;
  TEST_ASSERT_EQUAL(0, aged.push(info, sizeof(info), 100, r, 8, at));
  TEST_ASSERT_EQUAL(0, aged.push(reading, 7, 200, r, 8, at));
  TEST_ASSERT_EQUAL(
      0, aged.push(reading + 7, sizeof(reading) - 7, 7100, r, 8, at));
  TEST_ASSERT_EQUAL(1, aged.push(reading, sizeof(reading), 7200, r, 8, at));
  TEST_ASSERT_TRUE(r[0].flags & LOW_BATT);
  TEST_ASSERT_EQUAL(7200, at);
  aged.setProfile(findProfile("bm78xbt"));
  TEST_ASSERT_EQUAL(1, aged.push(reading, sizeof(reading), 7300, r, 8, at));
  TEST_ASSERT_FALSE(r[0].flags & LOW_BATT);
}
static size_t nameFrame(const uint8_t *payload, size_t size, uint8_t *out) {
  out[0] = 0xab;
  out[1] = 0xcd;
  out[2] = size + 2;
  memcpy(out + 3, payload, size);
  unsigned sum = 0;
  for (size_t i = 0; i < size + 3; i++)
    sum += out[i];
  out[size + 3] = sum >> 8;
  out[size + 4] = sum;
  return size + 5;
}
void test_name_reply_accepts_all_fragment_splits_and_reset() {
  uint8_t b[30];
  size_t n = nameFrame(reinterpret_cast<const uint8_t *>("UT60BT"), 6, b);
  for (size_t split = 0; split < n; split++) {
    NameReply reply;
    TEST_ASSERT_EQUAL_STRING("", reply.name());
    TEST_ASSERT_FALSE(reply.push(b, split));
    TEST_ASSERT_TRUE(reply.push(b + split, n - split));
    TEST_ASSERT_EQUAL_STRING("UT60BT", reply.name());
    reply.reset();
    TEST_ASSERT_EQUAL_STRING("", reply.name());
    TEST_ASSERT_EQUAL(split == 0, reply.push(b + split, n - split));
  }
}
void test_name_reply_rejects_ack_measurements_commands_and_bad_checksum() {
  const uint8_t ack[] = {0xab, 0xcd, 4, 0xff, 0, 2, 0x7b};
  const uint8_t command[] = {0xab, 0xcd, 3, 0x5f, 1, 0xda};
  NameReply reply;
  TEST_ASSERT_FALSE(reply.push(ack, sizeof(ack)));
  TEST_ASSERT_FALSE(reply.push(capture, sizeof(capture)));
  TEST_ASSERT_FALSE(reply.push(command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("", reply.name());
  uint8_t b[30];
  size_t n = nameFrame(reinterpret_cast<const uint8_t *>("UT61E+"), 6, b);
  b[n - 1] ^= 1;
  TEST_ASSERT_FALSE(reply.push(b, n));
  TEST_ASSERT_EQUAL_STRING("", reply.name());
  b[n - 1] ^= 1;
  TEST_ASSERT_TRUE(reply.push(b, n));
  TEST_ASSERT_EQUAL_STRING("UT61E+", reply.name());
}
void test_name_reply_graphic_ascii_and_length_limits() {
  const char *names[] = {"ABC", "ABCDEFGHIJKLMNOPQRST"};
  for (const char *name : names) {
    uint8_t b[30];
    size_t n =
        nameFrame(reinterpret_cast<const uint8_t *>(name), strlen(name), b);
    NameReply reply;
    for (size_t i = 0; i + 1 < n; i++)
      TEST_ASSERT_FALSE(reply.push(b + i, 1));
    TEST_ASSERT_TRUE(reply.push(b + n - 1, 1));
    TEST_ASSERT_EQUAL_STRING(name, reply.name());
  }
  const char *bad[] = {"AB", "ABCDEFGHIJKLMNOPQRSTU", "UT 60BT", "UT\x7f"};
  for (const char *name : bad) {
    uint8_t b[40];
    size_t n =
        nameFrame(reinterpret_cast<const uint8_t *>(name), strlen(name), b);
    NameReply reply;
    TEST_ASSERT_FALSE(reply.push(b, n));
    TEST_ASSERT_EQUAL_STRING("", reply.name());
  }
  const uint8_t nul[] = {'U', 0, 'T'};
  uint8_t b[20];
  size_t n = nameFrame(nul, sizeof(nul), b);
  NameReply reply;
  TEST_ASSERT_FALSE(reply.push(b, n));
}
void test_name_reply_resynchronizes_noise_and_false_incomplete_headers() {
  NameReply reply;
  uint8_t b[30];
  size_t n = nameFrame(reinterpret_cast<const uint8_t *>("UT202BT"), 7, b);
  const uint8_t noise[] = {0x55, 0xab, 0xcd, 22, 0x42};
  TEST_ASSERT_FALSE(reply.push(noise, sizeof(noise)));
  TEST_ASSERT_TRUE(reply.push(b, n));
  TEST_ASSERT_EQUAL_STRING("UT202BT", reply.name());
  reply.reset();
  for (unsigned i = 0; i < 4096; i++) {
    const uint8_t byte = i & 1 ? 0xab : 0x55;
    TEST_ASSERT_FALSE(reply.push(&byte, 1));
  }
  TEST_ASSERT_TRUE(reply.push(b, n));
  TEST_ASSERT_EQUAL_STRING("UT202BT", reply.name());
}
// Captured from the physical UT202BT 40:C1:F6:16:B2:D1 on 2026-11-12.
// Its length byte is one short; checksum uses the corrected nine-byte length.
static const uint8_t ut202NameCapture[]={0xab,0xcd,0x08,0x55,0x54,0x32,0x30,0x32,0x42,0x54,0x03,0x54};
void test_physical_ut202_name_length_quirk_at_every_split(){
 for(size_t split=0;split<sizeof(ut202NameCapture);++split){
  NameReply reply;TEST_ASSERT_FALSE(reply.push(ut202NameCapture,split));
  TEST_ASSERT_TRUE(reply.push(ut202NameCapture+split,sizeof(ut202NameCapture)-split));
  TEST_ASSERT_EQUAL_STRING("UT202BT",reply.name());
 }
 NameReply reply;for(size_t i=0;i+1<sizeof(ut202NameCapture);++i)TEST_ASSERT_FALSE(reply.push(ut202NameCapture+i,1));
 TEST_ASSERT_TRUE(reply.push(ut202NameCapture+sizeof(ut202NameCapture)-1,1));
 uint8_t noisy[32]={0x55,0xab,0xcd,22,0x42};memcpy(noisy+5,ut202NameCapture,sizeof(ut202NameCapture));
 reply.reset();TEST_ASSERT_TRUE(reply.push(noisy,5+sizeof(ut202NameCapture)));TEST_ASSERT_EQUAL_STRING("UT202BT",reply.name());
}
void test_ut202_quirk_still_requires_exact_model_and_corrected_checksum(){
 uint8_t b[sizeof(ut202NameCapture)];memcpy(b,ut202NameCapture,sizeof(b));b[11]^=1;
 NameReply reply;TEST_ASSERT_FALSE(reply.push(b,sizeof(b)));TEST_ASSERT_EQUAL_STRING("",reply.name());
 memcpy(b,ut202NameCapture,sizeof(b));b[11]=0x53;reply.reset();TEST_ASSERT_FALSE(reply.push(b,sizeof(b)));
 memcpy(b,ut202NameCapture,sizeof(b));b[8]='C';b[11]++;reply.reset();TEST_ASSERT_FALSE(reply.push(b,sizeof(b)));
 memcpy(b,ut202NameCapture,sizeof(b));b[2]=9;reply.reset();TEST_ASSERT_TRUE(reply.push(b,sizeof(b)));TEST_ASSERT_EQUAL_STRING("UT202BT",reply.name());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_split_physical_capture_is_stamped_from_oldest_fragment);
  RUN_TEST(
      test_old_prefix_and_late_tail_are_rejected_then_full_packet_recovers);
  RUN_TEST(test_drip_fragments_do_not_refresh_oldest_byte_age);
  RUN_TEST(test_capture_timestamps_and_expiry_handle_clock_wrap);
  RUN_TEST(test_age_boundary_configuration_and_empty_tick);
  RUN_TEST(test_multiple_packets_stamp_oldest_and_suffix_keeps_own_age);
  RUN_TEST(
      test_noise_timestamps_and_dropped_outputs_do_not_contaminate_packet_stamp);
  RUN_TEST(test_discard_partial_preserves_brymen_metadata_but_reset_clears_it);
  RUN_TEST(test_name_reply_accepts_all_fragment_splits_and_reset);
  RUN_TEST(test_name_reply_rejects_ack_measurements_commands_and_bad_checksum);
  RUN_TEST(test_name_reply_graphic_ascii_and_length_limits);
  RUN_TEST(test_name_reply_resynchronizes_noise_and_false_incomplete_headers);
  RUN_TEST(test_physical_ut202_name_length_quirk_at_every_split);
  RUN_TEST(test_ut202_quirk_still_requires_exact_model_and_corrected_checksum);
  return UNITY_END();
}
