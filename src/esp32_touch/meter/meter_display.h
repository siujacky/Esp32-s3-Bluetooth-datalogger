#pragma once
#include <cstdint>
namespace meter_display {
void begin();
unsigned count();
bool requestCount(unsigned value);
unsigned rotation();
bool requestRotation(unsigned value);
bool rotationSaved();
bool overlay();
void requestOverlay(bool value);
bool autoDim();
void requestAutoDim(bool value);
bool lightSaved();
unsigned brightness();
bool touch(uint32_t now);
unsigned lightTick(uint32_t now,bool keepAwake);
bool tick(bool allowRotation=true);
bool saved();
}
