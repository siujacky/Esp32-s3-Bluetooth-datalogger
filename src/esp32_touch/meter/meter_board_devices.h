#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

namespace meter_board {
// Auxiliary I2C is owned exclusively by Arduino's main loop. Other tasks may
// queue time/settings or copy diagnostics; they never access a sensor directly.
bool begin();
bool lcdReset(bool high);
bool backlightEnable(bool on);
uint64_t rtcUnixMs();
void syncRtc(uint64_t unixMs);
void tick(bool allowMotion);
bool takeRotation(unsigned &rotation);
bool autoRotation();
void setAutoRotation(bool enabled);
void writeStatus(JsonObject out);
}
