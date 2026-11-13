#include "meter_value_settings.h"
#include "meter_display.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>
namespace meter_values {namespace {
struct Request {uint8_t kind=0,slot=0,channel=0,count=1;meter_ui::ValuePreset preset=meter_ui::ValuePreset::Solo;meter_ui::NumberFormat format;bool changeCount=false;};
Preferences preferences;bool opened=false;
meter_ui::ValuePreferences current;
QueueHandle_t queue=nullptr;SemaphoreHandle_t mutex=nullptr;
std::atomic<bool> durable{false};String lastError;
bool enqueue(const Request &r){return queue&&xQueueSend(queue,&r,0)==pdTRUE;}
void error(const String &s){if(mutex){xSemaphoreTake(mutex,portMAX_DELAY);lastError=s;xSemaphoreGive(mutex);}}
}
void begin(){
    current=meter_ui::ValuePreferences{};
    mutex=xSemaphoreCreateMutex();queue=xQueueCreate(8,sizeof(Request));opened=preferences.begin("meter-values",false);
    bool ok=opened;
    const char *key=opened&&preferences.isKey("config4")?"config4":"config";
    if(opened&&preferences.isKey(key)){
        uint8_t record[meter_ui::valueRecordBytes];const auto size=preferences.getBytesLength(key);
        ok=(size==meter_ui::valueRecordBytes||(strcmp(key,"config4")!=0&&size==meter_ui::legacyValueRecordBytes))&&preferences.getBytes(key,record,size)==size&&meter_ui::decodeValuePreferences(record,size,current);
        if(ok&&strcmp(key,"config4")){
            uint8_t encoded[meter_ui::valueRecordBytes],check[meter_ui::valueRecordBytes];
            ok=meter_ui::encodeValuePreferences(current,encoded,sizeof(encoded))&&preferences.putBytes("config4",encoded,sizeof(encoded))==sizeof(encoded)&&preferences.getBytes("config4",check,sizeof(check))==sizeof(check)&&!memcmp(encoded,check,sizeof(check));
        }
    }
    durable.store(ok&&mutex&&queue);if(!ok)lastError="Value preferences unavailable/corrupt; defaults are in RAM";
}
meter_ui::ValuePreferences snapshot(){if(!mutex)return {};xSemaphoreTake(mutex,portMAX_DELAY);auto copy=current;xSemaphoreGive(mutex);return copy;}
bool saved(){return durable.load()&&queue&&uxQueueMessagesWaiting(queue)==0;}
String message(){if(!mutex)return "Value settings unavailable";xSemaphoreTake(mutex,portMAX_DELAY);String copy=lastError;xSemaphoreGive(mutex);return copy;}
bool requestLayout(meter_ui::ValuePreset preset){
    const unsigned count=meter_display::count();if(!meter_ui::validValuePreset(preset,count))return false;
    Request r;r.kind=0;r.preset=preset;r.count=count;return enqueue(r);
}
bool requestNativeLayout(meter_ui::ValuePreset preset){
    const unsigned count=meter_ui::valuePresetCount(preset,meter_display::count());if(!meter_ui::validValuePreset(preset,count))return false;
    Request r;r.kind=0;r.preset=preset;r.count=count;r.changeCount=true;return enqueue(r);
}
bool requestFormat(unsigned slot,unsigned channel,const meter_ui::NumberFormat &format){
    if(slot>=ut_meter::METER_SLOTS||channel>=8||!meter_ui::validNumberFormat(format))return false;
    Request r;r.kind=1;r.slot=slot;r.channel=channel;r.format=format;return enqueue(r);
}
bool tick(){
    if(!queue||!mutex)return false;
    Request r;bool changed=false;
    for(unsigned i=0;i<8&&xQueueReceive(queue,&r,0)==pdTRUE;++i){
        if(r.kind==0&&r.changeCount){meter_display::requestCount(r.count);meter_display::tick();}
        if(r.kind==0&&meter_display::count()!=r.count){error("Count changed before the value layout could apply");continue;}
        xSemaphoreTake(mutex,portMAX_DELAY);
        durable.store(false); // Invalidate acknowledgement before publishing changed RAM state.
        if(r.kind==0)current.presets[r.count-1]=r.preset;else current.formats[r.slot][r.channel]=r.format;
        lastError="";xSemaphoreGive(mutex);changed=true;
    }
    if(!changed)return false;
    uint8_t record[meter_ui::valueRecordBytes],check[meter_ui::valueRecordBytes];const auto prefs=snapshot();
    if(!opened)opened=preferences.begin("meter-values",false);
    const bool ok=opened&&meter_ui::encodeValuePreferences(prefs,record,sizeof(record))&&preferences.putBytes("config4",record,sizeof(record))==sizeof(record)&&preferences.getBytes("config4",check,sizeof(check))==sizeof(check)&&!memcmp(record,check,sizeof(check));
    durable.store(ok);if(!ok)error("Saving value preferences failed; changes are in RAM");
    Serial.printf("values_saved=%u value_layout=%s\n",ok,meter_ui::valuePresetName(prefs.presets[meter_display::count()-1]));return true;
}
}
