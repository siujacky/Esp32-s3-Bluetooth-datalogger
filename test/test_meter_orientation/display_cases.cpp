#ifdef METER_ORIENTATION_HOST_ADAPTER
#include "meter_display.h"
#include <Preferences.h>
#include <cassert>
#include <climits>
#include <cstdio>
int main(){
    using namespace meter_display;using namespace fake_preferences;
    bytes()["count"]=3;bytes()["overlay"]=0;bytes()["auto-dim"]=0;
    begin();assert(openedNamespace()=="meter-view");assert(rotation()==METER_UI_READING_ROTATION&&!rotationSaved());
    assert(count()==3&&!overlay()&&!autoDim());assert(!requestRotation(4)&&!requestRotation(UINT_MAX));assert(!tick());assert(writes()==0);
    assert(requestRotation(0));assert(rotation()==METER_UI_READING_ROTATION);assert(tick());assert(rotation()==0&&rotationSaved());assert(bytes()["rotation"]==0);
    assert(bytes()["count"]==3&&bytes()["overlay"]==0&&bytes()["auto-dim"]==0);
    begin();assert(rotation()==0&&rotationSaved());
    const unsigned beforeDeferred=writes();assert(requestRotation(3));assert(!tick(false));assert(rotation()==0&&rotationSaved()&&writes()==beforeDeferred);assert(tick(true));assert(rotation()==3&&rotationSaved());assert(requestRotation(0));assert(tick());
    writeOK()=false;assert(requestRotation(2));assert(tick());assert(rotation()==2&&!rotationSaved());assert(bytes()["rotation"]==0);
    writeOK()=true;readbackOK()=false;assert(requestRotation(2));assert(tick());assert(rotation()==2&&!rotationSaved());
    readbackOK()=true;assert(requestRotation(2));assert(tick());assert(rotationSaved());
    const unsigned durableWrites=writes();assert(requestRotation(2));assert(!tick());assert(writes()==durableWrites);
    bytes()["rotation"]=255;begin();assert(rotation()==METER_UI_READING_ROTATION&&!rotationSaved());
    beginOK()=false;begin();assert(!rotationSaved());assert(requestRotation(3));assert(tick());assert(rotation()==3&&!rotationSaved());
    beginOK()=true;assert(requestRotation(3));assert(tick());assert(rotationSaved());begin();assert(rotation()==3&&rotationSaved());
    bytes().erase("rotation");begin();assert(rotation()==METER_UI_READING_ROTATION&&!rotationSaved());
    assert(requestRotation(METER_UI_READING_ROTATION));assert(tick());assert(rotationSaved()&&bytes()["rotation"]==METER_UI_READING_ROTATION);
    puts("meter_display rotation: namespace/default/strict inputs/restart/write failure/readback failure/retry passed");
}
#endif
