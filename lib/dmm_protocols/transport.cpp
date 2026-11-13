// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal.h"
namespace dmm {
size_t AgedDecoder::push(const uint8_t *data, size_t n, uint32_t at,
                         Reading *out, size_t cap, uint32_t &captureAt) {
  if ((!data && n) || (!out && cap))
    return 0;
  // Check against the oldest byte, never the most recent drip fragment. The
  // uint32 subtraction intentionally handles the millis() wrap boundary.
  if (count_ && uint32_t(at - timestamps_[0]) > maxAgeMs_)
    discardPartial();
  size_t wrote = 0;
  bool stamped = false;
  for (size_t i = 0; i < n; i++) {
    timestamps_[count_++] = at;
    Reading local[8];
    size_t decoded = decoder_.push(data + i, 1, local, 8);
    if (decoded && wrote < cap) {
      const uint32_t firstAt = timestamps_[decoder_.publishedOffset_];
      if (!stamped || uint32_t(at - firstAt) > uint32_t(at - captureAt))
        captureAt = firstAt;
      stamped = true;
      for (size_t k = 0; k < decoded && wrote < cap; k++)
        out[wrote++] = local[k];
    }
    const size_t pending = decoder_.pendingBytes();
    // Decoder keeps a suffix after noise, complete packets and metadata frames.
    // Retain exactly the matching timestamp suffix, including its original age.
    if (pending)
      memmove(timestamps_, timestamps_ + count_ - pending,
              pending * sizeof(uint32_t));
    count_ = pending;
  }
  return wrote;
}

// One physical UT202BT revision sends len=8 with the seven-byte UT202BT
// name, but computes its checksum with the correct len=9. Normalize only
// this exact model prefix; every other packet retains strict wire framing.
static size_t namePacketLength(const uint8_t *data, size_t available) {
  static const uint8_t ut202Prefix[] = {0xab, 0xcd, 8, 'U', 'T', '2', '0', '2', 'B', 'T'};
  if (available >= sizeof(ut202Prefix) && !memcmp(data, ut202Prefix, sizeof(ut202Prefix)))
    return 9;
  return available >= 3 ? data[2] : 0;
}
bool NameReply::push(const uint8_t *data, size_t n) {
  if (!data && n)
    return false;
  bool received = false;
  for (size_t i = 0; i < n; i++) {
    if (size_ == sizeof(buffer_))
      memmove(buffer_, buffer_ + 1, --size_);
    buffer_[size_++] = data[i];
    // Search past an incomplete false header too, so bounded noise cannot
    // postpone a later complete valid reply until its fictitious length fills.
    for (size_t at = 0; at + 8 <= size_; at++) {
      if (buffer_[at] != 0xab || buffer_[at + 1] != 0xcd)
        continue;
      size_t length = namePacketLength(buffer_ + at, size_ - at);
      if (length < 5 || length > 22 || at + length + 3 > size_)
        continue;
      const size_t payload = length - 2, end = at + length + 3;
      unsigned sum = length - buffer_[at + 2];
      for (size_t j = at; j < end - 2; j++)
        sum += buffer_[j];
      if (sum != unsigned(buffer_[end - 2]) * 256 + buffer_[end - 1])
        continue;
      bool graphic = true;
      for (size_t j = at + 3; j < end - 2; j++)
        if (buffer_[j] < 0x21 || buffer_[j] > 0x7e)
          graphic = false;
      if (!graphic)
        continue;
      memcpy(name_, buffer_ + at + 3, payload);
      name_[payload] = 0;
      received = true;
      size_ -= end;
      memmove(buffer_, buffer_ + end, size_);
      break;
    }
    // Drop impossible prefixes; retain a plausible incomplete header for a
    // later notification. Complete non-name frames never establish readiness.
    while (size_) {
      bool prefix = buffer_[0] == 0xab && (size_ < 2 || buffer_[1] == 0xcd);
      if (prefix && size_ >= 3) {
        const size_t length = namePacketLength(buffer_, size_);
        prefix = length >= 5 && length <= 22 && size_ < length + 3;
      }
      if (prefix)
        break;
      --size_;
      memmove(buffer_, buffer_ + 1, size_);
    }
  }
  return received;
}
} // namespace dmm
