#include "meter_display.h"
#include <meter_display_count.h>
#include <meter_orientation.h>
#include <meter_idle_dim.h>
#include <Preferences.h>
#include <Arduino.h>
namespace meter_display {
namespace {Preferences preferences;bool opened=false;meter_ui::DisplayCount state;meter_ui::OverlaySetting layout,dimSetting;meter_ui::OrientationSetting orientation{METER_UI_READING_ROTATION};meter_ui::IdleDimmer dimmer;std::atomic<unsigned> light{55};}
void begin(){opened=preferences.begin("meter-view",false);state.load(opened?preferences.getUChar("count",1):1,opened);layout.load(opened?preferences.getUChar("overlay",1):1,opened);dimSetting.load(opened?preferences.getUChar("auto-dim",1):1,opened);orientation.load(opened?preferences.getUChar("rotation",255):255,opened&&preferences.isKey("rotation"));dimmer.configure(autoDim(),millis());}
unsigned count(){return state.count();}
bool saved(){return state.saved()&&layout.saved();}
bool requestCount(unsigned value){return state.request(value);}
unsigned rotation(){return orientation.rotation();}
bool requestRotation(unsigned value){return orientation.request(value);}
bool rotationSaved(){return orientation.saved();}
bool overlay(){return layout.overlay();}
void requestOverlay(bool value){layout.request(value);}
bool autoDim(){return dimSetting.overlay();}
void requestAutoDim(bool value){dimSetting.request(value);}
bool lightSaved(){return dimSetting.saved();}
unsigned brightness(){return light.load();}
bool touch(uint32_t now){return dimmer.contact(now);}
unsigned lightTick(uint32_t now,bool keepAwake){const unsigned value=dimmer.tick(now,keepAwake);light.store(value);return value;}
bool tick(bool allowRotation){
    bool changed=false;unsigned next=0;
    if(allowRotation&&orientation.consume(next)){
        if(!opened)opened=preferences.begin("meter-view",false);
        const bool ok=opened&&preferences.putUChar("rotation",next)==1&&preferences.getUChar("rotation",255)==next;
        orientation.markSaved(ok);changed=true;Serial.printf("display_rotation=%u saved=%u\n",next,ok);
    }
    if(state.consume(next)){
        if(!opened)opened=preferences.begin("meter-view",false);
        const bool ok=opened&&preferences.putUChar("count",next)==1&&preferences.getUChar("count",0)==next;
        state.markSaved(ok);changed=true;Serial.printf("display_count=%u saved=%u\n",next,ok);
    }
    bool nextOverlay=true;
    if(layout.consume(nextOverlay)){
        if(!opened)opened=preferences.begin("meter-view",false);
        const unsigned stored=nextOverlay?1:0;
        const bool ok=opened&&preferences.putUChar("overlay",stored)==1&&preferences.getUChar("overlay",2)==stored;
        layout.markSaved(ok);changed=true;Serial.printf("chart_layout=%s saved=%u\n",nextOverlay?"overlay":"separate",ok);
    }
    bool nextDim=true;
    if(dimSetting.consume(nextDim)){
        if(!opened)opened=preferences.begin("meter-view",false);
        const unsigned stored=nextDim?1:0;
        const bool ok=opened&&preferences.putUChar("auto-dim",stored)==1&&preferences.getUChar("auto-dim",2)==stored;
        dimSetting.markSaved(ok);dimmer.configure(nextDim,millis());changed=true;
        Serial.printf("auto_dim=%u saved=%u\n",stored,ok);
    }
    return changed;
}
}
