#pragma once
#include <meter_value_preferences.h>
#include "meter_ble.h"
class DeskPanel;
namespace meter_value_view {
meter_ble::DisplaySnapshot exampleReading(unsigned slot,uint32_t now);
meter_ui::FormattedNumber formatted(const meter_ble::DisplaySnapshot&,const meter_ui::NumberFormat&);
bool number(DeskPanel&,meter_ui::Rect,const meter_ui::FormattedNumber&,const meter_ui::NumberFormat&,uint16_t,unsigned maxEm=140);
bool textNumber(DeskPanel&,meter_ui::Rect,const char*,unsigned capacity,uint16_t,unsigned maxEm=140);
void draw(DeskPanel&,meter_ui::Rect,meter_ui::ValuePreset,unsigned focus,unsigned count,uint32_t now,int gap,bool examples=false);
}
