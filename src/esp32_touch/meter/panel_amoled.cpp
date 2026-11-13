#ifdef UT_METER_AMOLED
#include "../panel.h"
#include <Arduino_GFX_Library.h>
#include <touch/TouchDrvCST92xx.hpp>
#include <Wire.h>
#include <meter_ui.h>

// Exact ESP32-S3-Touch-AMOLED-1.75 mapping and 6-column offset from
// Waveshare's 01_HelloWorld and 10_Touch_CST9217 examples. Not the 1.75C.
static Arduino_ESP32QSPI bus(METER_DISPLAY_BUS_CS,METER_DISPLAY_BUS_SCK,
    METER_DISPLAY_BUS_DATA0,METER_DISPLAY_BUS_DATA1,METER_DISPLAY_BUS_DATA2,METER_DISPLAY_BUS_DATA3);
// The pinned Arduino_GFX 1.6.0 constructor retains its IPS argument.
static Arduino_CO5300 display(&bus,METER_DISPLAY_RESET,0,false,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,
    METER_DISPLAY_OFFSET_X,METER_DISPLAY_OFFSET_Y,0,0);
static TouchDrvCST92xx digitizer;
static bool displayReady=false, touchReady=false;
static volatile bool pending=false;
static void IRAM_ATTR touchIRQ(){pending=true;}
bool DeskPanel::begin() {
    // Global constructors run before Arduino enables PSRAM malloc. Retry
    // the 434 kB canvas allocation after setup() starts.
    if(!buffer)buffer=static_cast<uint16_t *>(ps_malloc(uint32_t(METER_DISPLAY_WIDTH)*METER_DISPLAY_HEIGHT*2));
    if(!getBuffer()){diagnostic="Framebuffer allocation failed";return false;}
    Wire.begin(METER_TOUCH_SDA,METER_TOUCH_SCL,METER_TOUCH_FREQUENCY);Wire.setTimeOut(20);
    // Check this board's shared bus before driving its reset/display pins.
#if METER_TOUCH_REQUIRED_PMIC_ADDRESS >= 0
    Wire.beginTransmission(METER_TOUCH_REQUIRED_PMIC_ADDRESS);
    if(Wire.endTransmission()!=0) {
        diagnostic="Required PMIC missing on configured touch I2C bus";
        Serial.println("AMOLED board check failed: required PMIC missing");
        return false;
    }
#endif
    displayReady=display.begin(METER_DISPLAY_FREQUENCY);
    if(displayReady){display.fillScreen(0);display.setBrightness(128);}
    digitizer.setPins(METER_TOUCH_RESET,METER_TOUCH_IRQ);
    touchReady=digitizer.begin(Wire,METER_TOUCH_ADDRESS,METER_TOUCH_SDA,METER_TOUCH_SCL);
    diagnostic=!displayReady?"CO5300 init failed":!touchReady?"CST9217 init failed":"CO5300 and CST9217 initialized";
    if(touchReady) {
        if(METER_TOUCH_IRQ>=0){pinMode(METER_TOUCH_IRQ,INPUT_PULLUP);attachInterrupt(digitalPinToInterrupt(METER_TOUCH_IRQ),touchIRQ,FALLING);}
    }
    return displayReady && touchReady;
}
void DeskPanel::brightness(uint8_t percent) {
    if(displayReady)display.setBrightness(uint16_t(percent>100?100:percent)*255/100);
}
void DeskPanel::flush() {
    if(displayReady && getBuffer())display.draw16bitRGBBitmap(0,0,getBuffer(),METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT);
}
bool DeskPanel::touch(uint16_t &x,uint16_t &y,bool &down) {
    if(!touchReady)return false;
    noInterrupts();
    const bool ready=pending;pending=false;
    interrupts();
    if(!ready)return false;
    const auto &points=digitizer.getTouchPoints();
    if(!points.getPointCount()){down=false;return true;}
    const auto &p=points.getPoint(0);
    if(!meter_ui::mapTouch(p.x,p.y,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,getRotation(),METER_TOUCH_MIRROR_X,METER_TOUCH_MIRROR_Y,METER_TOUCH_SWAP_XY,x,y)){++touchErrors;return false;}
    down=true;return true;
}
#endif
