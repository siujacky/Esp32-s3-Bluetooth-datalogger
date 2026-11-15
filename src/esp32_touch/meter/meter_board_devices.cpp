#include "meter_board_devices.h"
#ifdef UT_METER_LCD349
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <SensorQMI8658.hpp>
#include <waveshare349.h>
#include <esp_timer.h>
#include <atomic>
#include <cstring>

namespace meter_board {
namespace {
struct State {
    bool bus=false,expander=false,rtc=false,rtcValid=false,imu=false,sampleValid=false,autoRotate=false,saved=false;
    uint64_t rtcMs=0,rtcAt=0;uint32_t sampleAt=0,rtcWrites=0,rtcErrors=0,imuErrors=0;
    float ax=0,ay=0,az=0,gx=0,gy=0,gz=0;
    unsigned suggestion=4;char rtcError[96]={},imuError[96]={};
};
State state;
portMUX_TYPE stateLock=portMUX_INITIALIZER_UNLOCKED;
bool started=false;uint8_t expanderOutput=0,expanderConfig=0xff;
uint8_t pinMask(int pin){return pin>=0&&pin<8?uint8_t(1u<<unsigned(pin)):0;}
Preferences preferences;SensorQMI8658 imu;waveshare349::MotionRotation motion;
std::atomic<unsigned> setting{2},suggested{4};
uint64_t pendingRtc=0,pendingAt=0;
uint64_t uptime(){return uint64_t(esp_timer_get_time()/1000);}
State snapshot(){portENTER_CRITICAL(&stateLock);const State result=state;portEXIT_CRITICAL(&stateLock);return result;}
bool read(uint8_t address,uint8_t reg,uint8_t *bytes,size_t n){
    Wire1.beginTransmission(address);Wire1.write(reg);
    if(Wire1.endTransmission(false)!=0)return false;
    if(Wire1.requestFrom(address,uint8_t(n))!=n){while(Wire1.available())Wire1.read();return false;}
    return Wire1.readBytes(bytes,n)==n;
}
bool write(uint8_t address,uint8_t reg,const uint8_t *bytes,size_t n){
    Wire1.beginTransmission(address);Wire1.write(reg);Wire1.write(bytes,n);return Wire1.endTransmission()==0;
}
bool write(uint8_t address,uint8_t reg,uint8_t value){return write(address,reg,&value,1);}
bool expanderPin(int pin,bool high){
    if(pin<0)return true;
    const uint8_t next=high?expanderOutput|(1<<pin):expanderOutput&~(1<<pin);
    if(!write(METER_DEVICES_EXPANDER_ADDRESS,1,next))return false;
    expanderOutput=next;return true;
}
void rtcError(const char *message){
    portENTER_CRITICAL(&stateLock);state.rtcValid=false;++state.rtcErrors;
    strlcpy(state.rtcError,message,sizeof(state.rtcError));portEXIT_CRITICAL(&stateLock);
}
bool readRtc(){
    uint8_t control=0,bytes[7];uint64_t utc=0;
    if(!read(METER_DEVICES_RTC_ADDRESS,0,&control,1)||!read(METER_DEVICES_RTC_ADDRESS,4,bytes,7)){
        rtcError("RTC I2C read failed");return false;
    }
    if((control&0x22)||!waveshare349::decodeRtc(bytes,7,utc)){
        rtcError("RTC stopped, 12-hour mode or invalid date; set UTC from NTP/browser");return false;
    }
    const uint64_t now=uptime();
    portENTER_CRITICAL(&stateLock);state.rtcValid=true;state.rtcMs=utc;state.rtcAt=now;state.rtcError[0]=0;portEXIT_CRITICAL(&stateLock);return true;
}
void writeRtc(uint64_t utc){
    uint8_t bytes[7],control=0;
    if(!waveshare349::encodeRtc(utc,bytes,7)){rtcError("RTC time outside 2020..2099");return;}
    if(!read(METER_DEVICES_RTC_ADDRESS,0,&control,1)){rtcError("RTC control read failed");return;}
    // Freeze the counters, clear OS through the new seconds, then restart.
    // Keep the old hour format until the complete new calendar has arrived.
    // A failed/partial calendar write stays stopped, so it cannot be trusted
    // by a subsequent poll or reboot. The next accepted UTC update can retry.
    const bool stopped=write(METER_DEVICES_RTC_ADDRESS,0,uint8_t(control|0x20));
    const bool written=stopped&&write(METER_DEVICES_RTC_ADDRESS,4,bytes,7);
    if(!written){rtcError("RTC calendar write failed; set UTC again to restart clock");return;}
    // Clear 12_24: the calendar codec writes 24-hour BCD, not AM/PM encoding.
    const bool running=write(METER_DEVICES_RTC_ADDRESS,0,uint8_t(control&~0x22));
    if(!written||!running||!readRtc()){rtcError("RTC UTC write/readback failed");return;}
    const uint64_t actual=snapshot().rtcMs;
    if(actual<utc/1000*1000||actual>utc/1000*1000+1000){rtcError("RTC UTC readback mismatch");return;}
    portENTER_CRITICAL(&stateLock);++state.rtcWrites;portEXIT_CRITICAL(&stateLock);
}
}
bool begin(){
    if(started)return snapshot().expander;
    started=true;State initial;
    initial.bus=Wire1.begin(METER_DEVICES_SDA,METER_DEVICES_SCL,METER_DEVICES_FREQUENCY);Wire1.setTimeOut(20);
    if(initial.bus&&read(METER_DEVICES_EXPANDER_ADDRESS,1,&expanderOutput,1)&&read(METER_DEVICES_EXPANDER_ADDRESS,3,&expanderConfig,1)){
        uint8_t outputs=uint8_t(1<<METER_DEVICES_POWER_ENABLE);
        outputs|=pinMask(METER_DEVICES_LCD_RESET)|pinMask(METER_DEVICES_BACKLIGHT_ENABLE);
        expanderOutput|=1<<METER_DEVICES_POWER_ENABLE;
        expanderOutput|=pinMask(METER_DEVICES_LCD_RESET);
        expanderOutput&=~pinMask(METER_DEVICES_BACKLIGHT_ENABLE);
        expanderConfig&=~outputs;
        initial.expander=write(METER_DEVICES_EXPANDER_ADDRESS,1,expanderOutput)&&write(METER_DEVICES_EXPANDER_ADDRESS,3,expanderConfig);
    }
    const bool opened=preferences.begin("meter-sensors",false);
    initial.autoRotate=opened&&preferences.getBool("auto-rotate",false);
    initial.saved=opened;
    uint8_t probe=0;initial.rtc=initial.bus&&read(METER_DEVICES_RTC_ADDRESS,0,&probe,1);
    // Preserve the shared Wire1 pin/frequency setup; the driver handles WHO_AM_I
    // and bounded reset before enabling both calibrated measurement ranges.
    initial.imu=initial.bus&&imu.begin(Wire1,METER_DEVICES_IMU_ADDRESS)&&
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_2G,SensorQMI8658::ACC_ODR_125Hz)&&
        imu.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,SensorQMI8658::GYR_ODR_112_1Hz)&&
        imu.enableAccelerometer()&&imu.enableGyroscope();
    if(!initial.imu)strlcpy(initial.imuError,"QMI8658 identification/configuration failed",sizeof(initial.imuError));
    portENTER_CRITICAL(&stateLock);state=initial;portEXIT_CRITICAL(&stateLock);
    if(initial.rtc)readRtc();else rtcError("PCF85063 missing on auxiliary I2C bus");
    Serial.printf("board_devices expander=%u rtc=%u rtc_valid=%u imu=%u auto_rotation=%u\n",initial.expander,initial.rtc,snapshot().rtcValid,initial.imu,initial.autoRotate);
    return initial.expander;
}
bool lcdReset(bool high){return expanderPin(METER_DEVICES_LCD_RESET,high);}
bool backlightEnable(bool on){return expanderPin(METER_DEVICES_BACKLIGHT_ENABLE,on);}
uint64_t rtcUnixMs(){const State s=snapshot();return s.rtcValid?s.rtcMs+(uptime()-s.rtcAt):0;}
void syncRtc(uint64_t utc){portENTER_CRITICAL(&stateLock);pendingRtc=utc;pendingAt=uptime();portEXIT_CRITICAL(&stateLock);}
bool autoRotation(){return snapshot().autoRotate;}
void setAutoRotation(bool enabled){setting.store(enabled?1:0);}
bool takeRotation(unsigned &rotation){const unsigned value=suggested.exchange(4);if(value>3)return false;rotation=value;return true;}
void tick(bool allowMotion){
    if(!started)return;
    uint64_t utc,at;portENTER_CRITICAL(&stateLock);utc=pendingRtc;at=pendingAt;pendingRtc=0;portEXIT_CRITICAL(&stateLock);
    if(utc)writeRtc(utc+uptime()-at);
    const unsigned requested=setting.exchange(2);
    if(requested<2){
        const bool ok=preferences.putBool("auto-rotate",requested!=0)==1&&preferences.getBool("auto-rotate",requested==0)==(requested!=0);
        portENTER_CRITICAL(&stateLock);state.autoRotate=requested!=0;state.saved=ok;portEXIT_CRITICAL(&stateLock);
        motion.reset();suggested.store(4);
    }
    static uint32_t lastRtc=0,lastImu=0;const uint32_t now=millis();
    if(uint32_t(now-lastRtc)>=1000){lastRtc=now;if(snapshot().rtc)readRtc();}
    if(!allowMotion){motion.reset();suggested.store(4);}
    if(uint32_t(now-lastImu)<50||!snapshot().imu)return;
    lastImu=now;uint8_t sample[12];float axes[6]={};
    // SensorLib 0.5.1's legacy raw readers accept errors other than -1. Use
    // checked I2C for samples: reject NACK and short bursts before publishing.
    // CTRL1 address auto-increment was enabled by SensorLib initialization.
    const bool valid=read(METER_DEVICES_IMU_ADDRESS,0x35,sample,sizeof(sample));
    if(valid)for(unsigned i=0;i<6;++i){
        const unsigned bits=unsigned(sample[2*i])|(unsigned(sample[2*i+1])<<8);
        const int value=bits<32768?int(bits):int(bits)-65536;
        axes[i]=value*(i<3?2.0f:512.0f)/32768.0f;
    }
    const float ax=axes[0],ay=axes[1],az=axes[2],gx=axes[3],gy=axes[4],gz=axes[5];
    unsigned rotation=4;
    if(valid&&allowMotion&&autoRotation()&&std::fmax(std::fabs(gx),std::fmax(std::fabs(gy),std::fabs(gz)))<25){
        if(motion.update(ax,ay,az,now,rotation))suggested.store(rotation);
    }else motion.reset();
    portENTER_CRITICAL(&stateLock);state.sampleValid=valid;
    if(valid){state.ax=ax;state.ay=ay;state.az=az;state.gx=gx;state.gy=gy;state.gz=gz;state.sampleAt=now;state.imuError[0]=0;}
    else{++state.imuErrors;strlcpy(state.imuError,"IMU sample read failed",sizeof(state.imuError));}
    if(rotation<4)state.suggestion=rotation;
    portEXIT_CRITICAL(&stateLock);
}
void writeStatus(JsonObject out){
    const State s=snapshot();out["supported"]=true;out["auxI2cReady"]=s.bus;out["expanderReady"]=s.expander;
    auto rtc=out["rtc"].to<JsonObject>();rtc["chip"]="PCF85063";rtc["present"]=s.rtc;rtc["valid"]=s.rtcValid;rtc["utc"]=true;
    if(s.rtcValid)rtc["unixMs"]=s.rtcMs+(uptime()-s.rtcAt);else rtc["unixMs"]=nullptr;
    rtc["writes"]=s.rtcWrites;rtc["errors"]=s.rtcErrors;rtc["error"]=JsonString(s.rtcError);
    auto sensor=out["imu"].to<JsonObject>();sensor["chip"]="QMI8658";sensor["ready"]=s.imu;sensor["sampleValid"]=s.sampleValid;
    sensor["sampleUptimeMs"]=s.sampleAt;sensor["errors"]=s.imuErrors;sensor["error"]=JsonString(s.imuError);
    if(s.sampleValid){auto a=sensor["accelerationG"].to<JsonArray>();a.add(s.ax);a.add(s.ay);a.add(s.az);auto g=sensor["angularVelocityDps"].to<JsonArray>();g.add(s.gx);g.add(s.gy);g.add(s.gz);}
    sensor["autoRotation"]=s.autoRotate;sensor["settingSaved"]=s.saved;
    if(s.suggestion<4)sensor["suggestedRotation"]=s.suggestion;else sensor["suggestedRotation"]=nullptr;
}
}
#else
namespace meter_board {
bool begin(){return true;}bool lcdReset(bool){return true;}bool backlightEnable(bool){return true;}
uint64_t rtcUnixMs(){return 0;}void syncRtc(uint64_t){}void tick(bool){}
bool takeRotation(unsigned &){return false;}bool autoRotation(){return false;}void setAutoRotation(bool){}
void writeStatus(JsonObject out){out["supported"]=false;}
}
#endif
