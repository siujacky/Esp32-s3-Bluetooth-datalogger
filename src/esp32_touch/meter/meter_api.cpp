#include "meter_api.h"
#include "meter_ble.h"
#include "meter_calibration.h"
#include "meter_display.h"
#include "meter_value_settings.h"
#include <math.h>
#include <esp_timer.h>
namespace meter_api {
static const char *stateName(meter_ble::State s) {
    switch(s){case meter_ble::State::Idle:return "disconnected";case meter_ble::State::Scanning:return "scanning";
    case meter_ble::State::Connecting:return "connecting";case meter_ble::State::Connected:return "connected";
    case meter_ble::State::Retry:return "reconnecting";default:return "error";}
}
static const char *familyName(dmm::Family f) {
    switch(f){case dmm::Family::Uni61:return "UNI-T 61/161/60/202";case dmm::Family::Uni171:return "UNI-T 171";
    case dmm::Family::Uni181:return "UNI-T 181";case dmm::Family::Uni71:return "UNI-T 71";
    case dmm::Family::Owon6:case dmm::Family::Owon15:return "OWON";case dmm::Family::Zotek:return "ZOTEK/ANENG";
    case dmm::Family::Brymen:return "Brymen";default:return "EEVblog 121GW";}
}
class Adapter : public meter_web::Provider {
public:
    String stateJson() override {
        const unsigned screenRotation=meter_display::rotation();JsonDocument d;d["activeSlot"]=meter_ble::activeSlot();d["pairings"]["saved"]=meter_ble::pairingsSaved();d["pairings"]["message"]=meter_ble::pairingMessage();d["maxMeters"]=meter_ble::MAX_CONNECTIONS;
        d["display"]["rotation"]=screenRotation;d["display"]["rotationDegrees"]=screenRotation*90;d["display"]["rotationSaved"]=meter_display::rotationSaved();d["display"]["width"]=(screenRotation&1)?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH;d["display"]["height"]=(screenRotation&1)?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT;d["display"]["count"]=meter_display::count();d["display"]["saved"]=meter_display::saved();
        d["display"]["layout"]=meter_display::overlay()?"overlay":"separate";
        d["display"]["autoDim"]=meter_display::autoDim();d["display"]["dimmed"]=meter_display::brightness()==10;
        d["display"]["brightnessPercent"]=meter_display::brightness();d["display"]["dimAfterMs"]=120000;
        d["display"]["autoDimSaved"]=meter_display::lightSaved();
        const auto values=meter_values::snapshot();auto valueView=d["display"]["values"].to<JsonObject>();
        valueView["layout"]=meter_ui::valuePresetName(values.presets[meter_display::count()-1]);valueView["saved"]=meter_values::saved();valueView["message"]=meter_values::message();
        auto formats=valueView["formats"].to<JsonArray>();for(unsigned slot=0;slot<meter_ble::MAX_CONNECTIONS;++slot)for(unsigned channel=0;channel<8;++channel){const auto &f=values.formats[slot][channel];auto item=formats.add<JsonObject>();item["slot"]=slot;item["channel"]=channel;item["digits"]=f.digits;item["decimals"]=f.decimals;item["padding"]=f.padding==meter_ui::Padding::Zero?"zero":"blank";}
#ifdef UT_METER_JC3248
        d["capabilities"]["touchCalibration"]=true;
#endif
        JsonArray meters=d["meters"].to<JsonArray>();
        for(unsigned i=0;i<meter_ble::MAX_CONNECTIONS;++i) {
            auto slot=meter_ble::connection(i);auto m=meters.add<JsonObject>();
            m["slot"]=i;m["id"]=String("m")+String(i+1);m["name"]=String(slot.peer.name[0]?slot.peer.name:slot.profile);
            m["address"]=String(slot.peer.address);m["profile"]=String(slot.profile);m["state"]=stateName(slot.state);
            m["packets"]=slot.packets;m["bytes"]=slot.bytes;m["dropped"]=slot.dropped;m["message"]=String(slot.message);
            m["fresh"]=false;JsonArray readings=m["readings"].to<JsonArray>();
            for(unsigned channel=0;channel<meter_ble::MAX_CHANNELS;++channel)if(slot.hasReading[channel]) {
                const auto &r=slot.readings[channel];auto out=readings.add<JsonObject>();
                bool numeric=r.numeric && isfinite(r.value),fresh=ut_meter::isFresh(slot.state==meter_ble::State::Connected,true,millis(),slot.readingAt[channel]);
                out["channel"]=channel;out["numeric"]=numeric;if(numeric)out["value"]=r.value;else out["value"]=nullptr;
                out["text"]=String(r.text);out["unit"]=String(r.unit);out["mode"]=String(r.mode);out["modeKey"]=r.modeKey;out["flags"]=r.flags;
                out["ageMs"]=uint32_t(millis()-slot.readingAt[channel]);out["fresh"]=fresh;
                const uint64_t nowMs=uint64_t(esp_timer_get_time()/1000);
                const uint32_t age=uint32_t(millis()-slot.readingAt[channel]);
                out["uptimeMs"]=nowMs>=age?nowMs-age:0;out["packet"]=slot.channelPackets[channel];
                if(fresh)m["fresh"]=true;
            }
        }
        auto snapshot=meter_ble::view();auto discovery=d["discovery"].to<JsonObject>();discovery["scanning"]=snapshot.scanning;
        auto devices=discovery["devices"].to<JsonArray>();
        for(unsigned i=0;i<snapshot.count;++i) {
            auto out=devices.add<JsonObject>();const auto &c=snapshot.meters[i];out["index"]=i;out["name"]=c.name;
            out["address"]=c.address;out["addressType"]=c.addressType;out["rssi"]=c.rssi;out["profile"]=c.profile;
        }
        String result;serializeJson(d,result);return result;
    }
    String profilesJson() override {
        JsonDocument d;auto array=d.to<JsonArray>();
        for(size_t i=0;i<dmm::profileCount();++i){const auto &p=*dmm::profileAt(i);auto out=array.add<JsonObject>();out["id"]=p.id;out["name"]=p.name;out["family"]=familyName(p.family);out["experimental"]=p.experimental;}
        String result;serializeJson(d,result);return result;
    }
    bool control(JsonVariantConst body,String &error) override {
        if(!body["action"].is<const char*>()){error="A string action is required";return false;}
        const JsonString actionText=body["action"].as<JsonString>();if(actionText.size()!=strlen(actionText.c_str())){error="Action contains an embedded NUL";return false;}
        const char *action=body["action"]|"";int slot=body["slot"]|int(meter_ble::activeSlot());
        if(!strcmp(action,"value-layout")){
            if(!body["preset"].is<const char*>()){error="A value preset is required";return false;}
            const JsonString text=body["preset"].as<JsonString>();meter_ui::ValuePreset preset;
            if(!meter_ui::parseValuePreset(text.c_str(),text.size(),preset)||!meter_ui::validValuePreset(preset,meter_display::count())){error="Value preset must match the current meter count";return false;}
            if(!meter_values::requestLayout(preset)){error="Value settings queue is full";return false;}return true;
        }
        if(!strcmp(action,"value-format")){
            const char *keys[]={"slot","channel","digits","decimals"};for(auto key:keys)if(body[key].is<bool>()||!body[key].is<unsigned>()){error="Slot, channel, digits and decimals must be integers";return false;}
            const unsigned s=body["slot"],channel=body["channel"],digits=body["digits"],decimals=body["decimals"];
            if(s>=meter_ble::MAX_CONNECTIONS||channel>=8||digits<3||digits>8||decimals>6||decimals>=digits||!body["padding"].is<const char*>()){error="Check slot/channel, 3..8 digits and decimals below digits";return false;}
            const JsonString pad=body["padding"].as<JsonString>();meter_ui::Padding padding=meter_ui::Padding::Invalid;
            if(pad.size()==5&&!memcmp(pad.c_str(),"blank",5))padding=meter_ui::Padding::Blank;
            if(pad.size()==4&&!memcmp(pad.c_str(),"zero",4))padding=meter_ui::Padding::Zero;
            if(padding==meter_ui::Padding::Invalid){error="Padding must be blank or zero";return false;}
            if(!meter_values::requestFormat(s,channel,{uint8_t(digits),uint8_t(decimals),padding})){error="Value settings queue is full";return false;}return true;
        }
        if(!strcmp(action,"screen-rotation")){
            if(body["rotation"].is<bool>()||!body["rotation"].is<unsigned>()||body["rotation"].as<unsigned>()>3){error="Rotation must be 0, 1, 2 or 3";return false;}
            if(meter_calibration::busy()){error="Finish touch calibration before rotating";return false;}
            if(!meter_display::requestRotation(body["rotation"].as<unsigned>())){error="Screen rotation request unavailable";return false;}return true;
        }
        if(!strcmp(action,"auto-dim")){
            if(!body["enabled"].is<bool>()){error="Auto dim enabled must be true or false";return false;}
            meter_display::requestAutoDim(body["enabled"].as<bool>());return true;
        }
        if(!strcmp(action,"chart-layout")){
            if(!body["layout"].is<const char*>()){error="Chart layout must be overlay or separate";return false;}
            const JsonString value=body["layout"].as<JsonString>();
            const bool isOverlay=value.size()==7&&!memcmp(value.c_str(),"overlay",7);
            const bool isSeparate=value.size()==8&&!memcmp(value.c_str(),"separate",8);
            if(!isOverlay&&!isSeparate){error="Chart layout must be overlay or separate";return false;}
            meter_display::requestOverlay(isOverlay);return true;
        }
        if(!strcmp(action,"display-count")){
            if(body["count"].is<bool>()||!body["count"].is<unsigned>()||!meter_display::requestCount(body["count"].as<unsigned>())){error="Display count must be 1, 2, 3 or 4";return false;}
            return true;
        }
#ifdef UT_METER_JC3248
        if(!strcmp(action,"touch-calibrate")){meter_calibration::request();return true;}
#endif
        if(slot<0 || slot>=int(meter_ble::MAX_CONNECTIONS)){error="Invalid meter slot";return false;}
        bool ok=false;
        if(!strcmp(action,"scan"))ok=meter_ble::scan();
        else if(!strcmp(action,"focus")) {
            ok=meter_ble::focus(slot);
            if(ok && !body["channel"].isNull()) {
                int channel=body["channel"]|-1;ok=channel>=0 && meter_ble::selectChannel(slot,channel);
            }
        }
        else if(!strcmp(action,"disconnect"))ok=meter_ble::disconnect(slot);
        else if(!strcmp(action,"profile"))ok=meter_ble::setProfile(slot,body["profile"]|"");
        else if(!strcmp(action,"connect")) {
            int index=body["deviceIndex"]|-1;
            if(index<0){error="Scan and select a meter first";return false;}
            const char *address=body["address"]|"";
            int addressType=-1;if(body["addressType"].isNull()==false){if(body["addressType"].is<bool>()||!body["addressType"].is<unsigned>()||body["addressType"].as<unsigned>()>3){error="Invalid Bluetooth address type";return false;}addressType=body["addressType"].as<int>();}
            ok=meter_ble::connect(index,slot,body["profile"]|"auto",address,addressType);
        } else {error="Unknown meter action";return false;}
        if(!ok)error="Request unavailable: choose a supported profile or disconnect before changing it";
        return ok;
    }
};
meter_web::Provider &provider(){static Adapter adapter;return adapter;}
}
