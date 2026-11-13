#pragma once
#include <meter_value_preferences.h>
#include <Arduino.h>
namespace meter_values {
void begin();
meter_ui::ValuePreferences snapshot();
bool saved();
String message();
bool requestLayout(meter_ui::ValuePreset preset);
bool requestNativeLayout(meter_ui::ValuePreset preset);
bool requestFormat(unsigned slot,unsigned channel,const meter_ui::NumberFormat &format);
bool tick();
}
