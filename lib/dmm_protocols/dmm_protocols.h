// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol port from antoinecellerier/dmm-tools; see NOTICE and COPYING.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dmm {
enum class Family : uint8_t {
  Uni61,
  Uni171,
  Uni181,
  Uni71,
  Owon6,
  Owon15,
  Zotek,
  Brymen,
  Eevblog
};
constexpr uint32_t HOLD = 1u << 0, REL = 1u << 1, MIN = 1u << 2, MAX = 1u << 3,
                   LOW_BATT = 1u << 4, MANUAL = 1u << 5, CAUTION = 1u << 6,
                   PEAK = 1u << 7, AUTO_RANGE = 1u << 8, DC = 1u << 9,
                   AC = 1u << 10, AVG = 1u << 11, RECORD = 1u << 12,
                   LOZ = 1u << 13, LEAD_ERROR = 1u << 14, COMP = 1u << 15,
                   PEAK_MIN = 1u << 16, PEAK_MAX = 1u << 17, LPF = 1u << 18,
                   INRUSH = 1u << 19, UNKNOWN = 1u << 20;
enum class BringUp : uint8_t { Subscribe, ReadInfo, BrymenLogin };
struct Gatt {
  const char *name, *serviceUuid, *notifyUuid, *writeUuid, *infoUuid;
  uint16_t minMtu, settleMs;
  BringUp bringUp;
  bool alwaysWithoutResponse, requireWriteResponse, stripsHeartbeat;
};
struct Profile {
  const char *id, *name;
  Family family;
  const Gatt *gatt;
  bool experimental; // Upstream protocol status; never claims local hardware
                     // validation.
  uint16_t modelCode;
};
struct Reading {
  char text[24] = {};
  double value = 0;
  bool numeric = false;
  char unit[12] = {}, mode[40] = {};
  uint32_t modeKey = 0;
  uint8_t channel = 0, range = 0;
  uint32_t flags = 0;
};
size_t profileCount();
const Profile *profileAt(size_t index);
const Profile *findProfile(const char *id);
// Returns nullptr for ambiguous/generic names, including Bluetooth DMM/BDM.
const Profile *profileForName(const char *advertisedName);
const Profile *profileForOwonInfo(const uint8_t *info, size_t size);

// Decode a complete ON-AIR packet (framing/checksum included), never a fake
// value. Return number written. No writes on malformed data. Output order is
// main, then auxiliary displays (channel >= 1). value is always expressed in
// unit, including its prefix; text retains the display's sign/decimal precision
// where available.
size_t decodePacket(const Profile &profile, const uint8_t *data, size_t size,
                    Reading *out, size_t capacity);

struct Command {
  uint8_t data[32] = {};
  size_t size = 0;
  bool withResponse = false;
  uint16_t waitMs = 0;    // delay after this command, before the next action
  bool readReply = false; // read writeUuid after waitMs (Brymen login)
};
// Sequential startup commands; false marks end. UNI-T must subscribe BEFORE
// startup. UT60BT/UT202BT step0 is GetName: wait for a checked NameReply before
// sending step1 StartStream. OWON info reads and Brymen login run BEFORE their
// subscription. mac is six BLE address bytes in conventional printed order,
// reversed internally for Brymen; nullptr uses zero. Brymen step0 sends login
// 0000: read its reply, judgeLogin(), settle500ms, then subscribe. An explicit
// refusal must abort; never retry passwords.
bool startupCommand(const Profile &profile, size_t step, Command &out,
                    const uint8_t *mac = nullptr);
// Returns false for unprompted streaming protocols. UNI61 supports an optional
// query (e.g. UT60BT), while its start command enables adapter streaming.
bool pollCommand(const Profile &profile, Command &out);
enum class LoginResult : uint8_t { Unknown, Accepted, Refused };
LoginResult judgeLogin(const uint8_t *reply, size_t size,
                       uint16_t *refusalCode = nullptr);

class Decoder {
public:
  explicit Decoder(const Profile *profile = nullptr) : profile_(profile) {}
  void setProfile(const Profile *profile) {
    profile_ = profile;
    reset();
  }
  void reset() {
    discardPartial();
    brymenLowBattery_ = false;
  }
  size_t pendingBytes() const { return size_; }
  // Discard only transport fragments/alignment, preserving validated metadata
  // such as Brymen's battery state. reset()/setProfile() clear metadata too.
  void discardPartial() {
    size_ = 0;
    aligned_ = false;
  }
  // Fixed 512-byte buffer. Consumes all input, emitting up to capacity
  // readings. If output fills, further complete packets in this call are
  // discarded; partial trailing bytes remain buffered. Use capacity >=8 for all
  // auxiliary displays in a packet. No heap allocation; one Decoder per BLE
  // client.
  size_t push(const uint8_t *data, size_t size, Reading *out, size_t capacity);

private:
  friend class AgedDecoder;
  const Profile *profile_;
  uint8_t buffer_[512] = {};
  size_t size_ = 0;
  // Offset of the first published packet in the pending bytes + push input.
  // AgedDecoder feeds one byte at a time and uses this for exact provenance.
  size_t publishedOffset_ = 0;
  bool aligned_ = false, brymenLowBattery_ = false;
};

class AgedDecoder {
public:
  explicit AgedDecoder(const Profile *profile = nullptr,
                       uint32_t maxAgeMs = 3000)
      : decoder_(profile), maxAgeMs_(maxAgeMs) {}
  void setProfile(const Profile *profile) {
    decoder_.setProfile(profile);
    count_ = 0;
  }
  void reset() {
    decoder_.reset();
    count_ = 0;
  }
  void discardPartial() {
    decoder_.discardPartial();
    count_ = 0;
  }
  size_t pendingBytes() const { return decoder_.pendingBytes(); }
  // at is the notification's capture time in wrapping uint32_t milliseconds.
  // A fragment expires when at-oldestByteAt exceeds maxAgeMs, even if more
  // fragments have dripped in since. captureAt is the oldest contributing byte
  // timestamp among RETURNED readings in this call, not the last byte's time.
  // Multiple returned packets share that conservative timestamp; feed separate
  // packets/calls if individual timestamps are required. No output leaves
  // captureAt unchanged. A trailing partial packet retains its own timestamps.
  size_t push(const uint8_t *data, size_t size, uint32_t at, Reading *out,
              size_t capacity, uint32_t &captureAt);

private:
  Decoder decoder_;
  uint32_t maxAgeMs_;
  uint32_t timestamps_[513] = {};
  size_t count_ = 0;
};

class NameReply {
public:
  void reset() {
    size_ = 0;
    name_[0] = 0;
  }
  // True only if this input completes a checksum-valid AB CD name packet.
  // Payload must be3..20 ASCII-graphic bytes; ack/measurement packets cannot
  // establish readiness. The captured UT202BT len8/canonical-len9 checksum
  // quirk is normalized only for its exact model name. Bounded25-byte storage; fragmented/noisy input
  // allowed.
  bool push(const uint8_t *data, size_t size);
  const char *name() const { return name_; }

private:
  uint8_t buffer_[25] = {};
  size_t size_ = 0;
  char name_[21] = {};
};
} // namespace dmm
