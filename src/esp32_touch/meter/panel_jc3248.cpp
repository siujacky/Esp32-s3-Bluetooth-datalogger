#ifdef UT_METER_JC3248
#include "../panel.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <jc3248_touch.h>
#include <cmath>

// F1ATB JC3248W535-Demo bd5d87ee / Guition board YAML 737d0c14.
// Never use the demo's nominal touch reset12/IRQ11: those are TF pins.
static Arduino_ESP32QSPI bus(METER_DISPLAY_BUS_CS,METER_DISPLAY_BUS_SCK,
    METER_DISPLAY_BUS_DATA0,METER_DISPLAY_BUS_DATA1,METER_DISPLAY_BUS_DATA2,METER_DISPLAY_BUS_DATA3);
static Arduino_AXS15231B display(&bus,METER_DISPLAY_RESET,0,false,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,
    METER_DISPLAY_OFFSET_X,METER_DISPLAY_OFFSET_Y,0,0);
static bool displayReady=false,touchReady=false;
static uint32_t polls=0,ioErrors=0,decodeErrors=0,contacts=0;
static uint8_t lastPacket[8]={},lastTx=0,lastCount=0;
static uint16_t rawX=0,rawY=0,logicalX=0,logicalY=0;
static bool lastDown=false,lastDecoded=false;
uint16_t DeskPanel::rawTouchX()const{return rawX;}
uint16_t DeskPanel::rawTouchY()const{return rawY;}
void DeskPanel::printTouchStatus(){
    Serial.printf("touch_diag polls=%lu io_errors=%lu decode_errors=%lu contacts=%lu tx=%u bytes=%u raw=%u,%u logical=%u,%u down=%u decoded=%u packet=",
        (unsigned long)polls,(unsigned long)ioErrors,(unsigned long)decodeErrors,(unsigned long)contacts,
        lastTx,lastCount,rawX,rawY,logicalX,logicalY,lastDown,lastDecoded);
    for(auto byte:lastPacket)Serial.printf("%02X",byte);Serial.println();
}
bool DeskPanel::begin(){
    if(!buffer)buffer=static_cast<uint16_t *>(ps_malloc(uint32_t(METER_DISPLAY_WIDTH)*METER_DISPLAY_HEIGHT*2));
    if(!buffer){diagnostic="JC3248 framebuffer needs PSRAM";return false;}
    Wire.begin(METER_TOUCH_SDA,METER_TOUCH_SCL,METER_TOUCH_FREQUENCY);Wire.setTimeOut(20);
    if(METER_TOUCH_RESET>=0){pinMode(METER_TOUCH_RESET,OUTPUT);digitalWrite(METER_TOUCH_RESET,LOW);delay(20);digitalWrite(METER_TOUCH_RESET,HIGH);delay(120);}
    if(METER_TOUCH_IRQ>=0)pinMode(METER_TOUCH_IRQ,INPUT_PULLUP); // Touch remains explicitly polled.
    Wire.beginTransmission(METER_TOUCH_ADDRESS);touchReady=Wire.endTransmission()==0;
    if(!touchReady){diagnostic="AXS15231B touch missing on configured I2C bus";return false;}
    if(METER_DISPLAY_BACKLIGHT>=0){pinMode(METER_DISPLAY_BACKLIGHT,OUTPUT);digitalWrite(METER_DISPLAY_BACKLIGHT,LOW);}
    displayReady=display.begin(METER_DISPLAY_FREQUENCY);
    if(!displayReady){diagnostic="AXS15231B QSPI init failed";return false;}
    display.fillScreen(0);
    if(METER_DISPLAY_BACKLIGHT>=0){ledcSetup(0,5000,8);ledcAttachPin(METER_DISPLAY_BACKLIGHT,0);brightness(55);}
    diagnostic="AXS15231B mapped QSPI / polled touch initialized";return true;
}
void DeskPanel::flush(){if(displayReady&&buffer)display.draw16bitRGBBitmap(0,0,buffer,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT);}
void DeskPanel::brightness(uint8_t percent){if(displayReady&&METER_DISPLAY_BACKLIGHT>=0)ledcWrite(0,uint16_t(percent>100?100:percent)*255/100);}
bool DeskPanel::touch(uint16_t &x,uint16_t &y,bool &down){
    if(!touchReady)return false;
    static uint32_t lastRead=0;const uint32_t now=millis();if(now-lastRead<20)return false;lastRead=now;
    static const uint8_t command[]={0xb5,0xab,0xa5,0x5a,0,0,0,8,0,0,0};
    ++polls;
    uint8_t bytes[8];Wire.beginTransmission(METER_TOUCH_ADDRESS);Wire.write(command,sizeof(command));
    lastTx=Wire.endTransmission();lastCount=0;lastDecoded=false;
    if(lastTx!=0){++touchErrors;++ioErrors;return false;}
    lastCount=Wire.requestFrom(uint8_t(METER_TOUCH_ADDRESS),uint8_t(sizeof(bytes)));
    if(lastCount!=sizeof(bytes)){while(Wire.available())Wire.read();++touchErrors;++ioErrors;return false;}
    for(auto &byte:bytes)byte=Wire.read();
    memcpy(lastPacket,bytes,sizeof(bytes));rawX=((bytes[2]&15)<<8)|bytes[3];rawY=((bytes[4]&15)<<8)|bytes[5];
    if(!jc3248::decodeTouch(bytes,sizeof(bytes),getRotation(),x,y,down,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT)){++touchErrors;++decodeErrors;return false;}
    if(down&&useTouchCalibration){
        jc3248::Point mapped{};
        constexpr unsigned rotation=METER_UI_READING_ROTATION;
        constexpr int width=(rotation&1)?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH;
        constexpr int height=(rotation&1)?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT;
        if(!jc3248::mapCalibration(touchCalibration,rawX,rawY,mapped)||mapped.x<-64||mapped.x>width+63||mapped.y<-64||mapped.y>height+63){++touchErrors;++decodeErrors;return false;}
        const uint16_t calibratedX=uint16_t(constrain(int(std::lround(mapped.x)),0,width-1));
        const uint16_t calibratedY=uint16_t(constrain(int(std::lround(mapped.y)),0,height-1));
        if(!jc3248::transformTouch(calibratedX,calibratedY,rotation,getRotation(),METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,x,y)){++touchErrors;++decodeErrors;return false;}
    }
    lastDecoded=true;logicalX=x;logicalY=y;lastDown=down;if(down)++contacts;
    return true;
}
#endif
