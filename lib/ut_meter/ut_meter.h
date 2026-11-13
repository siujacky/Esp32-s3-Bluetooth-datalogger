#pragma once
#include "meter_limits.h"
#include <stddef.h>
#include <stdint.h>

namespace ut_meter {
enum class Model : uint8_t { UT61Plus, UT60BT };
constexpr size_t FRAME_SIZE = 19;
constexpr uint32_t STALE_MS = 3000;
constexpr uint8_t QUERY[] = {0xab, 0xcd, 0x03, 0x5e, 0x01, 0xd9};
struct Reading {
    double value = 0;
    char text[24] = {};
    char unitLabel[12] = {}, modeLabel[40] = {};
    uint32_t modeKey = 0;
    bool hasMetadata = false;
    uint8_t mode = 0, range = 0, channel = 0;
    uint8_t flags[3] = {};
    bool numeric = false;
};
bool decode(const uint8_t *frame, size_t size, Model model, Reading &out);
const char *modeName(uint8_t mode, uint8_t channel = 0);
const char *unit(uint8_t mode);
const char *unit(const Reading &reading);
const char *modeName(const Reading &reading);
bool isFresh(bool connected, bool hasReading, uint32_t now, uint32_t receivedAt);
class Decoder {
public:
    bool push(uint8_t byte, Model model, Reading &out);
    void reset() { count_ = 0; }
private:
    uint8_t buffer_[FRAME_SIZE] = {};
    size_t count_ = 0;
};
}
