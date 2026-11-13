#pragma once
#include <Adafruit_GFX.h>
#ifdef UT_METER_JC3248
#include <jc3248_calibration.h>
#endif
class DeskPanel : public GFXcanvas16 {
public:
#ifdef METER_BOARD_CONFIGURED
    // Meter dimensions come from its validated per-environment profile. Allocate
    // after Arduino initializes PSRAM, including larger mapped LCD variants.
    DeskPanel():GFXcanvas16(METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT) {if(buffer){free(buffer);buffer=nullptr;}}
#elif defined(UT_METER_AMOLED)
    DeskPanel():GFXcanvas16(466,466) {}
#elif defined(UT_METER_JC3248)
    // GFX 1.11.11 allocates in its constructor. Release any startup allocation;
    // begin() creates this larger framebuffer in initialized PSRAM instead.
    DeskPanel():GFXcanvas16(320,480) {if(buffer){free(buffer);buffer=nullptr;}}
#else
    DeskPanel():GFXcanvas16(172,320) {}
#endif
    bool begin();
    void flush();
    bool touch(uint16_t &x,uint16_t &y, bool &down);
    void brightness(uint8_t percent);
    uint32_t touchErrors=0;
#ifdef UT_METER_JC3248
    void printTouchStatus();
    uint16_t rawTouchX()const;
    uint16_t rawTouchY()const;
    jc3248::Calibration touchCalibration;
    bool useTouchCalibration=false;
#endif
    const char *diagnostic="See panel status";
};
