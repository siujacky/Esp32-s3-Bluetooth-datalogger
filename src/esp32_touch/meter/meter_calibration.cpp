#include "meter_calibration.h"
#include "../panel.h"
#include "meter_design.h"
#ifdef UT_METER_JC3248
#include <Preferences.h>
#include <jc3248_wizard.h>
#include <atomic>
#include <cstring>
namespace meter_calibration {
namespace {
constexpr unsigned kRotation=METER_UI_READING_ROTATION;
constexpr unsigned kWidth=(kRotation&1)?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH;
constexpr unsigned kHeight=(kRotation&1)?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT;
Preferences preferences;
jc3248::CalibrationWizard wizard;
jc3248::Calibration previous;
bool prefsOpen=false,saved=false,previousValid=false,resetContact=false,dirty=false;
std::atomic<bool> opened{false};
bool saveScreen=false;uint32_t savedAt=0;
int currentSlot=-1,previousSlot=-1;bool selectionUncertain=false;
std::atomic<bool> requested{false};
String saveError;
void text(DeskPanel &p,int y,const String &value,uint8_t size=2,uint16_t color=0xffff){while(size>1&&int(value.length())*6*size>p.width()-16)--size;p.setFont(nullptr);p.setTextSize(size);p.setTextColor(color);p.setCursor((p.width()-int(value.length())*6*size)/2,int(int32_t(y)*p.height()/320));p.print(value);}
void start(DeskPanel &p){
    previous=p.touchCalibration;previousValid=p.useTouchCalibration;previousSlot=currentSlot;
    p.useTouchCalibration=false;wizard.configure(kWidth,kHeight);opened=true;saveScreen=false;saveError="";resetContact=true;dirty=true;
    Serial.println("touch_calibration active=1 targets=4 centre_check=1; hold each cross until green, then lift");
}
bool loadRecord(const char *key,jc3248::Calibration &model){
    uint8_t bytes[jc3248::CALIBRATION_BYTES];
    return prefsOpen&&preferences.isKey(key)&&preferences.getBytesLength(key)==sizeof(bytes)&&
        preferences.getBytes(key,bytes,sizeof(bytes))==sizeof(bytes)&&jc3248::decodeCalibration(bytes,sizeof(bytes),model,kWidth,kHeight);
}
bool restoreSelection(int slot){
    if(!prefsOpen)return false;
    if(slot>=0)return preferences.putUChar("active",uint8_t(slot))==1&&preferences.getUChar("active",255)==slot;
    if(preferences.isKey("active")&&!preferences.remove("active"))return false;
    return !preferences.isKey("active");
}
bool store(DeskPanel &p){
    uint8_t encoded[jc3248::CALIBRATION_BYTES],readback[jc3248::CALIBRATION_BYTES];jc3248::Calibration verified;
    if(!prefsOpen)prefsOpen=preferences.begin(METER_TOUCH_CALIBRATION_NAMESPACE,false);
    const int next=currentSlot==0?1:0;const char *key=next==0?"cal0":"cal1";
    // Write and verify the inactive bank before atomically changing selection.
    // A failed candidate/readback never overwrites the prior durable record.
    if(!prefsOpen||!jc3248::encodeCalibration(wizard.calibration(),encoded,sizeof(encoded),kWidth,kHeight)||
       preferences.putBytes(key,encoded,sizeof(encoded))!=sizeof(encoded)||preferences.getBytesLength(key)!=sizeof(encoded)||
       preferences.getBytes(key,readback,sizeof(readback))!=sizeof(readback)||memcmp(encoded,readback,sizeof(encoded))||
       !jc3248::decodeCalibration(readback,sizeof(readback),verified,kWidth,kHeight))return false;
    if(preferences.putUChar("active",uint8_t(next))!=1||preferences.getUChar("active",255)!=next){selectionUncertain=!restoreSelection(currentSlot);return false;}
    currentSlot=next;selectionUncertain=false;p.touchCalibration=verified;p.useTouchCalibration=true;saved=true;
    Serial.println("touch_calibration saved=1 readback=verified bytes=64");return true;
}
}
void begin(DeskPanel &p){
    prefsOpen=preferences.begin(METER_TOUCH_CALIBRATION_NAMESPACE,false);saved=false;
    jc3248::Calibration model;bool loaded=false;currentSlot=-1;
    if(prefsOpen&&preferences.isKey("active")){const int slot=preferences.getUChar("active",255);if(slot<=1&&loadRecord(slot==0?"cal0":"cal1",model)){currentSlot=slot;loaded=true;}}
    else if(loadRecord("cal",model))loaded=true; // earlier single-record version
    if(loaded){
        p.touchCalibration=model;p.useTouchCalibration=true;saved=true;Serial.printf("touch_calibration loaded=1 crc=valid active=0 bank=%d\n",currentSlot);
    }else start(p);
}
bool active(){return opened.load();}
bool busy(){return requested.load()||opened.load();}
void request(){requested.store(true);}
bool takeContactReset(){bool result=resetContact;resetContact=false;return result;}
bool tick(DeskPanel &p){
    if(requested.exchange(false)){
        if(selectionUncertain&&!restoreSelection(previousSlot)){saveError="NVS state unconfirmed. Retry the centre.";dirty=true;}
        else {selectionUncertain=false;if(opened&&!saveScreen){p.touchCalibration=previous;p.useTouchCalibration=previousValid;currentSlot=previousSlot;}start(p);}
    }
    if(opened&&saveScreen&&uint32_t(millis()-savedAt)>=1300){opened=false;saveScreen=false;resetContact=true;dirty=true;}
    const bool changed=dirty;dirty=false;return changed;
}
void contact(DeskPanel &p,bool down){
    if(!opened||saveScreen)return;
    if(wizard.update(p.rawTouchX(),p.rawTouchY(),down,millis()))dirty=true;
    if(!wizard.ready())return;
    if(store(p)){saveScreen=true;savedAt=millis();saveError="";dirty=true;}
    else {saveError=selectionUncertain?"Save unconfirmed. Tap centre to retry.":"NVS save failed. Tap centre to retry.";wizard.retryVerification();dirty=true;Serial.println("touch_calibration saved=0 error=NVS_save_or_readback_failed");}
}
void draw(DeskPanel &p){
    p.setRotation(kRotation);p.fillScreen(meter_design::BG);p.setTextWrap(false);
    if(saveScreen){text(p,110,"CALIBRATION SAVED",3,0x5f2c);text(p,155,"Verified in NVS");text(p,188,"Restart keeps this mapping",1,0xa554);return;}
    const jc3248::Point target=wizard.target();const int x=int(target.x+.5),y=int(target.y+.5);
    const uint16_t color=wizard.captureReady()?meter_design::ACCENT:meter_design::FG;
    if(meter_design::modern){
        for(unsigned i=0;i<5;++i)p.fillCircle(p.width()/2+(int(i)-2)*24,p.height()*7/8,4,i<wizard.step()?meter_design::ACCENT:meter_design::BORDER);
    }
    p.drawCircle(x,y,19,color);p.drawCircle(x,y,20,color);p.drawFastHLine(x-26,y,53,color);p.drawFastVLine(x,y-26,53,color);p.fillCircle(x,y,3,color);
    if(wizard.step()<4){text(p,100,"TOUCH CALIBRATION");text(p,132,"Corner "+String(wizard.step()+1)+" of 4");text(p,166,wizard.captureReady()?"GREEN - LIFT YOUR FINGER":"Hold the cross until green",1,color);text(p,188,"Then lift to accept this point",1,0xa554);}
    else {text(p,64,"VERIFY CENTRE");text(p,91,"Hold centre until green, then lift",1);text(p,218,"This confirms and saves the mapping",1,0xa554);}
    const String error=saveError.length()?saveError:String(wizard.error());if(error.length())text(p,244,error,1,0xfd20);
    text(p,305,"Four corners + centre / saved in device NVS",1,0xa554);
}
void printStatus(){
    Serial.printf("touch_calibration active=%u saved=%u step=%u samples=%u ready=%u nvs=%u bank=%d\n",unsigned(opened.load()),saved,wizard.step()+1,wizard.samples(),wizard.ready(),prefsOpen,currentSlot);
    jc3248::Calibration model;
    if(saved&&loadRecord(currentSlot==0?"cal0":currentSlot==1?"cal1":"cal",model))Serial.printf("touch_model x=%.12g,%.12g,%.12g y=%.12g,%.12g,%.12g source=NVS crc=valid\n",model.x[0],model.x[1],model.x[2],model.y[0],model.y[1],model.y[2]);
}
bool command(const String &line,DeskPanel &p){
    if(line=="touch calibrate"){request();return true;}
    if(line=="touch calibration"){printStatus();return true;}
    if(line=="touch cancel"){
        if(saveScreen){Serial.println("touch_calibration cancel_refused=already_saved");return true;}
        if(selectionUncertain&&!restoreSelection(previousSlot)){Serial.println("touch_calibration cancel_refused=NVS_selection_unconfirmed");return true;}
        selectionUncertain=false;
        if(opened&&previousValid){p.touchCalibration=previous;p.useTouchCalibration=true;opened=false;saveScreen=false;dirty=true;resetContact=true;Serial.println("touch_calibration cancelled=1 previous_mapping_retained=1");}
        else Serial.println("touch_calibration cancel_refused=no_previous_mapping");
        return true;
    }
    return false;
}
}
#else
namespace meter_calibration {
void begin(DeskPanel&){} bool active(){return false;} bool busy(){return false;} void request(){} bool tick(DeskPanel&){return false;}
bool takeContactReset(){return false;} void contact(DeskPanel&,bool){} void draw(DeskPanel&){} void printStatus(){}
bool command(const String&,DeskPanel&){return false;}
}
#endif
