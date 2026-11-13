#pragma once
#include <Arduino.h>
class DeskPanel;
namespace meter_calibration {
void begin(DeskPanel &panel);
bool active();
bool busy();
void request();
bool tick(DeskPanel &panel);
bool takeContactReset();
void contact(DeskPanel &panel,bool down);
void draw(DeskPanel &panel);
void printStatus();
bool command(const String &line,DeskPanel &panel);
}
