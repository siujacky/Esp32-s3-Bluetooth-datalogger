#pragma once
#include <meter_ui.h>
#include <cstddef>
#include <cstdint>
namespace meter_numerals {
struct Surface {void *context;uint16_t(*read)(void*,int,int);void(*write)(void*,int,int,uint16_t);};
struct Metrics {bool valid=false;meter_ui::Rect area{};unsigned capacity=0;int fontPixels=0,cellWidth=0,x=0,baseline=0,top=0,bottom=0,leftInset=0;};
struct Stats {size_t bytes=0,peakBytes=0;unsigned misses=0,failures=0;};
Metrics metrics(meter_ui::Rect,unsigned capacity,unsigned maxEm=140);
bool paint(const Surface&,const Metrics&,const char*,uint16_t color);
uint16_t blend565(uint16_t background,uint16_t foreground,uint8_t alpha);
void clear();
void allocationLimit(size_t bytes);
Stats stats();
const char *fontSha();
}
