#include "panel.h"
#if !defined(UT_METER_AMOLED) && !defined(UT_METER_JC3248)
#include <SPI.h>
#include <Wire.h>
#ifdef METER_BOARD_CONFIGURED
#include <meter_ui.h>
#endif
// Pins, register sequence and mirrored touch X from Waveshare's official
// ESP32-S3-Touch-LCD-1.47 demo, 03_lvgl_arduino_v8 (downloaded 2026-10-10).
#ifdef METER_BOARD_CONFIGURED
static constexpr int DC=METER_DISPLAY_BUS_DC,CS=METER_DISPLAY_BUS_CS,CLK=METER_DISPLAY_BUS_SCK,
    LCD_MOSI=METER_DISPLAY_BUS_DATA0,RST=METER_DISPLAY_RESET,BL=METER_DISPLAY_BACKLIGHT;
static constexpr int LCD_WIDTH=METER_DISPLAY_WIDTH,LCD_HEIGHT=METER_DISPLAY_HEIGHT,
    OFFSET_X=METER_DISPLAY_OFFSET_X,OFFSET_Y=METER_DISPLAY_OFFSET_Y;
static constexpr int TOUCH_SDA=METER_TOUCH_SDA,TOUCH_SCL=METER_TOUCH_SCL,
    TOUCH_RESET=METER_TOUCH_RESET,TOUCH_IRQ=METER_TOUCH_IRQ,TOUCH_ADDRESS=METER_TOUCH_ADDRESS;
static constexpr uint32_t LCD_FREQUENCY=METER_DISPLAY_FREQUENCY,TOUCH_FREQUENCY=METER_TOUCH_FREQUENCY;
static constexpr bool TOUCH_MIRROR_X=METER_TOUCH_MIRROR_X,TOUCH_MIRROR_Y=METER_TOUCH_MIRROR_Y,TOUCH_SWAP_XY=METER_TOUCH_SWAP_XY;
#else
static constexpr int DC=45,CS=21,CLK=38,LCD_MOSI=39,RST=40,BL=46;
static constexpr int LCD_WIDTH=172,LCD_HEIGHT=320,OFFSET_X=34,OFFSET_Y=0;
static constexpr int TOUCH_SDA=42,TOUCH_SCL=41,TOUCH_RESET=47,TOUCH_IRQ=48,TOUCH_ADDRESS=0x63;
static constexpr uint32_t LCD_FREQUENCY=40000000,TOUCH_FREQUENCY=400000;
static constexpr bool TOUCH_MIRROR_X=true,TOUCH_MIRROR_Y=false,TOUCH_SWAP_XY=false;
#endif
static volatile bool irq=false;
static void IRAM_ATTR touchIRQ(){irq=true;}
static void command(uint8_t c,const uint8_t *data=nullptr,size_t n=0){
    digitalWrite(DC,LOW); SPI.write(c); digitalWrite(DC,HIGH);
    if(n) SPI.writeBytes(data,n);
}
static void addressWindow(){
    const uint16_t right=OFFSET_X+LCD_WIDTH-1,bottom=OFFSET_Y+LCD_HEIGHT-1;
    const uint8_t x[]={uint8_t(OFFSET_X>>8),uint8_t(OFFSET_X),uint8_t(right>>8),uint8_t(right)},
        y[]={uint8_t(OFFSET_Y>>8),uint8_t(OFFSET_Y),uint8_t(bottom>>8),uint8_t(bottom)};
    command(0x2A,x,4);command(0x2B,y,4);
}
// Each entry is command, payload length, payload bytes; 0 terminates.
static const uint8_t panelInit[]={
    0xDF,2,0x98,0x53, 0xB2,1,0x23,
    0xB7,4,0x00,0x47,0x00,0x6F,
    0xBB,6,0x1C,0x1A,0x55,0x73,0x63,0xF0,
    0xC0,2,0x44,0xA4, 0xC1,1,0x16,
    0xC3,8,0x7D,0x07,0x14,0x06,0xCF,0x71,0x72,0x77,
    0xC4,12,0,0,0xA0,0x79,0x0B,0x0A,0x16,0x79,0x0B,0x0A,0x16,0x82,
    0xC8,32,0x3F,0x32,0x29,0x29,0x27,0x2B,0x27,0x28,0x28,0x26,0x25,0x17,0x12,0x0D,4,0,
      0x3F,0x32,0x29,0x29,0x27,0x2B,0x27,0x28,0x28,0x26,0x25,0x17,0x12,0x0D,4,0,
    0xD0,5,4,6,0x6B,0x0F,0, 0xD7,2,0,0x30, 0xE6,1,0x14, 0xDE,1,1,
    0xB7,5,3,0x13,0xEF,0x35,0x35, 0xC1,3,0x14,0x15,0xC0,
    0xC2,2,6,0x3A, 0xC4,2,0x72,0x12, 0xBE,1,0, 0xDE,1,2,
    0xE5,3,0,2,0, 0xE5,3,1,2,0, 0xDE,1,0, 0x35,1,0, 0x3A,1,5,
    0xDE,1,2, 0xE5,3,0,2,0, 0xDE,1,0, 0x36,1,0, 0
};
bool DeskPanel::begin(){
#ifdef METER_BOARD_CONFIGURED
    if(!buffer)buffer=static_cast<uint16_t *>(ps_malloc(uint32_t(LCD_WIDTH)*LCD_HEIGHT*2));
#endif
    if(!getBuffer()) return false;
    pinMode(DC,OUTPUT); pinMode(CS,OUTPUT);digitalWrite(CS,HIGH);
    if(RST>=0){pinMode(RST,OUTPUT);digitalWrite(RST,LOW);delay(20);digitalWrite(RST,HIGH);delay(120);}
    SPI.begin(CLK,-1,LCD_MOSI,CS); SPI.beginTransaction(SPISettings(LCD_FREQUENCY,MSBFIRST,SPI_MODE0));
    digitalWrite(CS,LOW); command(0x11); delay(120);
    const uint8_t *p=panelInit;
    while(*p){uint8_t c=*p++, n=*p++; command(c,p,n); p+=n;}
    addressWindow();
    command(0x21); delay(10); command(0x29);
    digitalWrite(CS,HIGH); SPI.endTransaction();
    if(BL>=0){ledcSetup(0,5000,8);ledcAttachPin(BL,0);brightness(75);}
    Wire.begin(TOUCH_SDA,TOUCH_SCL,TOUCH_FREQUENCY); Wire.setTimeOut(20);
    if(TOUCH_RESET>=0){pinMode(TOUCH_RESET,OUTPUT);digitalWrite(TOUCH_RESET,LOW);delay(200);digitalWrite(TOUCH_RESET,HIGH);delay(300);}
    if(TOUCH_IRQ>=0){pinMode(TOUCH_IRQ,INPUT_PULLUP);attachInterrupt(TOUCH_IRQ,touchIRQ,FALLING);}
    Wire.beginTransmission(TOUCH_ADDRESS); return Wire.endTransmission()==0;
}
void DeskPanel::brightness(uint8_t percent){if(BL>=0)ledcWrite(0,uint16_t(percent>100?100:percent)*255/100);}
void DeskPanel::flush(){
    if(!getBuffer())return;
    SPI.beginTransaction(SPISettings(LCD_FREQUENCY,MSBFIRST,SPI_MODE0)); digitalWrite(CS,LOW);
    addressWindow();command(0x2C);
    SPI.writePixels(getBuffer(),uint32_t(LCD_WIDTH)*LCD_HEIGHT*2);
    digitalWrite(CS,HIGH); SPI.endTransaction();
}
bool DeskPanel::touch(uint16_t &x,uint16_t &y,bool &down){
    // Poll contact state as well as interrupts. Silence from a stationary
    // finger is not a release; polling also recovers a missed release IRQ.
    static uint32_t lastRead=0;
    if(!irq && millis()-lastRead<40) return false;
    lastRead=millis();
    irq=false;
    uint8_t b[14];
    Wire.beginTransmission(TOUCH_ADDRESS); Wire.write(1);
    if(Wire.endTransmission()!=0 || Wire.requestFrom(TOUCH_ADDRESS,14)!=14){touchErrors++;return false;}
    Wire.readBytes(b,14);
    if(b[1]>2){touchErrors++;return false;}
    // Validate into locals, then publish. Writing `down` and `y` before the
    // range check meant a glitched frame returned false having already
    // claimed a press at a corrupted coordinate, with `x` left untouched.
    if(b[1]==0){down=false;return true;}
    uint16_t rawX=((b[2]&15)<<8)|b[3], rawY=((b[4]&15)<<8)|b[5];
#ifdef METER_BOARD_CONFIGURED
    if(!meter_ui::mapTouch(rawX,rawY,LCD_WIDTH,LCD_HEIGHT,getRotation(),TOUCH_MIRROR_X,TOUCH_MIRROR_Y,TOUCH_SWAP_XY,x,y)){++touchErrors;return false;}
#else
    if(TOUCH_SWAP_XY){uint16_t temporary=rawX;rawX=rawY;rawY=temporary;}
    if(rawX>=LCD_WIDTH || rawY>=LCD_HEIGHT){touchErrors++;return false;}
    x=TOUCH_MIRROR_X?LCD_WIDTH-1-rawX:rawX;
    y=TOUCH_MIRROR_Y?LCD_HEIGHT-1-rawY:rawY;
#endif
    down=true;return true;
}
#endif
