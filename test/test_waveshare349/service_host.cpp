#include "meter_board_devices.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <waveshare349.h>
#include <cassert>
#include <cstdio>
uint32_t fakeMillis=0;FakeSerial Serial;FakeWire Wire1;
bool fakeImuOk=true;float fakeAx=0,fakeAy=1,fakeAz=0;
std::map<std::string,bool> fakeSaved;
std::map<std::string,uint8_t> fakeSavedBytes;
bool fakeOffsetWriteFails=false,fakeOffsetReadbackFails=false;unsigned fakeOffsetWrites=0;
int main(int argc,char **argv){
    const char *mode=argc>1?argv[1]:"valid";
    if(!strcmp(mode,"offset-restart"))fakeSavedBytes["imu-offset"]=3;
    if(!strcmp(mode,"offset-invalid"))fakeSavedBytes["imu-offset"]=9;
    if(!strcmp(mode,"offset-unreadable")){fakeSavedBytes["imu-offset"]=1;fakeOffsetReadbackFails=true;}
    const uint64_t utc=1709210096000ULL;
    assert(waveshare349::encodeRtc(utc,&Wire1.registers[0x51][4],7));
    Wire1.registers[0x20][1]=0x80;Wire1.registers[0x20][3]=0xff;
    Wire1.registers[0x6b][0x38]=0x40; // AY = 16384 = 1g at +/-2g.
    if(!strcmp(mode,"stopped"))Wire1.registers[0x51][4]|=0x80;
    const bool writeFailure=!strcmp(mode,"rtc-write-fail");
    if(!strcmp(mode,"12-hour")||writeFailure){Wire1.registers[0x51][0]=2;Wire1.registers[0x51][6]=0x21;}
    Wire1.rejectCalendarWrite=writeFailure;
    if(!strcmp(mode,"missing-imu"))fakeImuOk=false;
    if(!strcmp(mode,"missing-rtc"))Wire1.missing[0x51]=true;
    if(!strcmp(mode,"missing-expander"))Wire1.missing[0x20]=true;
    const bool boot=meter_board::begin();
    if(!strcmp(mode,"missing-expander")){assert(!boot);return 0;}
    assert(boot&&Wire1.sda==47&&Wire1.scl==48);
    const unsigned initialOffset=!strcmp(mode,"offset-restart")?3:0;
    assert(meter_board::imuRotationOffset()==initialOffset);
    assert(meter_board::imuRotationOffsetSaved()==(!strcmp(mode,"offset-restart")));
    fakeOffsetReadbackFails=false;
    assert(!meter_board::setImuRotationOffset(4)&&!meter_board::setImuRotationOffset(999));
    assert(fakeOffsetWrites==0);
    assert(Wire1.registers[0x20][1]&0x40);assert(Wire1.registers[0x20][1]&0x80); // Preserve unrelated audio bit.
    assert(!(Wire1.registers[0x20][3]&0x40));assert(!meter_board::autoRotation());
    if(!strcmp(mode,"stopped")||!strcmp(mode,"missing-rtc")||!strcmp(mode,"12-hour")||writeFailure)assert(!meter_board::rtcUnixMs());else assert(meter_board::rtcUnixMs()==utc);
    const unsigned before=Wire1.writes;
    const bool postInitFailure=!strcmp(mode,"imu-nack")||!strcmp(mode,"imu-short");
    if(!strcmp(mode,"imu-nack"))Wire1.missing[0x6b]=true;
    if(!strcmp(mode,"imu-short"))Wire1.shortRead[0x6b]=true;
    meter_board::syncRtc(utc+60000);assert(Wire1.writes==before); // Queue only; no web-thread I2C.
    fakeMillis=250;meter_board::tick(false);
    JsonDocument status;meter_board::writeStatus(status.to<JsonObject>());
    assert(status["supported"].as<bool>());
    if(postInitFailure){assert(!status["imu"]["sampleValid"].as<bool>());assert(status["imu"]["errors"].as<unsigned>()>0);}
    const std::string rtcError=status["rtc"]["error"].as<std::string>();
    const std::string imuError=status["imu"]["error"].as<std::string>();
    assert(rtcError.size()==strlen(rtcError.c_str())&&imuError.size()==strlen(imuError.c_str()));
    if(!strcmp(mode,"missing-rtc")||writeFailure)assert(!status["rtc"]["valid"].as<bool>());
    else {assert(status["rtc"]["valid"].as<bool>()&&status["rtc"]["writes"].as<unsigned>()==1);assert(meter_board::rtcUnixMs()==utc+60000);assert(!(Wire1.registers[0x51][0]&0x22));}
    meter_board::setAutoRotation(true);fakeMillis=300;meter_board::tick(false);
    assert(meter_board::autoRotation()&&fakeSaved["auto-rotate"]);unsigned rotation=99;assert(!meter_board::takeRotation(rotation));
    for(unsigned n=0;n<15;++n){fakeMillis+=50;meter_board::tick(true);}
    if(fakeImuOk&&!postInitFailure){assert(meter_board::takeRotation(rotation)&&rotation==initialOffset);}
    else assert(!meter_board::takeRotation(rotation));
    fakeAx=1;fakeAy=0;meter_board::tick(false);
    Wire1.registers[0x6b][0x38]=0;Wire1.registers[0x6b][0x36]=0x40;
    for(unsigned n=0;n<15;++n){fakeMillis+=50;meter_board::tick(false);}assert(!meter_board::takeRotation(rotation));
    meter_board::setAutoRotation(false);meter_board::tick(false);assert(!meter_board::autoRotation());
    if(fakeImuOk&&!postInitFailure){
        Wire1.registers[0x6b][0x36]=0x80;Wire1.registers[0x6b][0x3c]=0xc0;
        fakeMillis+=50;meter_board::tick(false);meter_board::writeStatus(status.to<JsonObject>());
        assert(status["imu"]["accelerationG"][0].as<float>()==-2.0f);
        assert(status["imu"]["angularVelocityDps"][0].as<float>()==-256.0f);
    }
    if(!strcmp(mode,"valid")||!strncmp(mode,"offset-",7)){
        fakeOffsetWriteFails=!strcmp(mode,"offset-write-fail");fakeOffsetReadbackFails=!strcmp(mode,"offset-readback-fail");
        const bool shouldSave=!fakeOffsetWriteFails&&!fakeOffsetReadbackFails;
        // Manual mode remains off while alignment is changed and saved.
        assert(meter_board::setImuRotationOffset(1));assert(fakeOffsetWrites==0);
        fakeMillis+=50;meter_board::tick(false);assert(!meter_board::autoRotation());
        assert(meter_board::imuRotationOffset()==1&&meter_board::imuRotationOffsetSaved()==shouldSave);
        meter_board::setAutoRotation(true);fakeMillis+=50;meter_board::tick(false);
        for(unsigned offset=0;offset<4;++offset){
            assert(meter_board::setImuRotationOffset(offset));
            fakeMillis+=50;meter_board::tick(false);
            assert(meter_board::autoRotation()&&meter_board::imuRotationOffset()==offset);
            assert(meter_board::imuRotationOffsetSaved()==shouldSave);
            const unsigned writes=fakeOffsetWrites;
            for(unsigned direction=0;direction<4;++direction){
                for(unsigned i=0;i<12;++i)Wire1.registers[0x6b][0x35+i]=0;
                const unsigned reg=direction%2?0x36:0x38;
                Wire1.registers[0x6b][reg]=direction<2?0x40:0xc0;
                for(unsigned n=0;n<15;++n){fakeMillis+=50;meter_board::tick(true);}
                assert(meter_board::takeRotation(rotation)&&rotation==(direction+offset)%4);
                assert(fakeOffsetWrites==writes); // Motion never saves settings.
            }
            meter_board::writeStatus(status.to<JsonObject>());
            assert(status["imu"]["rotationOffsetDegrees"].as<unsigned>()==offset*90);
            assert(status["imu"]["rotationOffsetSaved"].as<bool>()==shouldSave);
        }
        // A changed alignment re-evaluates the same stationary gravity.
        assert(meter_board::setImuRotationOffset(1));
        for(unsigned n=0;n<15;++n){fakeMillis+=50;meter_board::tick(true);}
        assert(meter_board::takeRotation(rotation)&&rotation==0);
    }
    meter_board::writeStatus(status.to<JsonObject>());
    if(writeFailure)assert(!status["rtc"]["valid"].as<bool>()); // Periodic read must not trust the failed calendar.
    assert(status["imu"]["ready"].as<bool>()==fakeImuOk);
    printf("board service %s: power, RTC queue/readback, IMU and motion suppression passed\n",mode);
}
