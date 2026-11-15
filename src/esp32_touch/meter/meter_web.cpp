#include "meter_web.h"
#include <meter_limits.h>
#include "meter_ntp.h"
#include "meter_ntp_json.h"
#include "meter_board_devices.h"
#include <limits>
#include "meter_http_body.h"
#include <wifi_qr.h>
#include "meter_web_assets.h"
#include "meter_readme_assets.h"
#include "meter_storage.h"
#include <meter_rules.h>
#include <meter_log_format.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <new>
#include <vector>

// Arduino 2.x ships the full IDF trust bundle in libmbedtls.a. An explicit
// reference pulls the binary object out of that archive. No insecure fallback.
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
extern const uint8_t meter_ca_bundle[] asm("_binary_x509_crt_bundle_start");
#endif

namespace meter_web {
namespace {
constexpr unsigned kMaxRules=8, kMaxBody=8192, kFrameLimit=128;
constexpr unsigned kDeliveryDepth=8, kSampleDepth=64;
class JsonAllocator:public ArduinoJson::Allocator {
public:
    void *allocate(size_t size) override {
        void *p=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        return p ? p : heap_caps_malloc(size,MALLOC_CAP_8BIT);
    }
    void deallocate(void *ptr) override {heap_caps_free(ptr);}
    void *reallocate(void *ptr,size_t size) override {
        if(!size){heap_caps_free(ptr);return nullptr;}
        if(!ptr)return allocate(size);
        void *p=heap_caps_realloc(ptr,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        return p ? p : heap_caps_realloc(ptr,size,MALLOC_CAP_8BIT);
    }
};
JsonAllocator g_jsonAllocator;
struct PsramDocument:JsonDocument {PsramDocument():JsonDocument(&g_jsonAllocator){}};
struct Frame {
    uint64_t seq=0,timestampMs=0;
    Sample sample;
    char sessionId[40]={},event[20]={},message[112]={};
    uint32_t ruleId=0;
    int code=0;
    bool success=false,reading=false;
};
struct Rule {
    uint32_t id=0,revision=0;
    meter_rules::Config config;
    meter_rules::State state;
    String url,method,body,caPem;
    std::vector<std::pair<String,String>> headers;
    String lastResult="idle";
    int lastCode=0;
    uint32_t fireCount=0,deliveryCount=0;
    uint64_t lastDeliveryMs=0;
};
struct Job { Rule rule; Sample sample; uint64_t timestampMs=0; bool manual=false; };
struct WifiCommand { char ssid[33]={},password[65]={}; };
Provider *g_provider=nullptr;
WebServer g_server(80);
#if METER_STORAGE_ENABLED
WebServer g_fileServer(81);
#endif
Preferences g_preferences;
Frame *g_frames=nullptr;
unsigned g_capacity=0;
uint64_t g_latest=0;
uint32_t g_sampleDrops=0,g_deliveryDrops=0,g_ruleCounter=0,g_recordCounter=0;
bool g_recording=false,g_timeSynced=false;
uint8_t g_clockSource=0;SemaphoreHandle_t g_clockMutex=nullptr;
std::atomic<bool> g_started{false};
int64_t g_epochOffset=0;
char g_recordId[40]={},g_recordLabel[97]={};
String g_deviceId,g_bootId;
NetworkInfo g_network;
struct WifiFound {String ssid;int rssi=0,channel=0;wifi_auth_mode_t auth=WIFI_AUTH_OPEN;};
std::vector<WifiFound> g_wifiFound;
String g_scanState="idle",g_scanMessage="Scan for nearby 2.4 GHz networks";
bool g_scanRequested=false;
uint64_t g_scanFinishedAt=0;
std::atomic<bool> g_wifiScanDone{false};
std::atomic<uint32_t> g_wifiScanStatus{0};
SemaphoreHandle_t g_netMutex=nullptr,g_ruleMutex=nullptr;
QueueHandle_t g_samples=nullptr,g_jobs=nullptr,g_wifiCommands=nullptr;
portMUX_TYPE g_feedLock=portMUX_INITIALIZER_UNLOCKED;
std::vector<Rule> g_rules;
Sample (*g_lastSamples)[8]=nullptr;
bool g_haveLast[ut_meter::METER_SLOTS][8]={};
bool g_prefsOpen=false;
String g_requestBody;
bool g_requestBodyFailed=false;
bool g_requestBodyReady=false;

uint64_t nowMs() { return static_cast<uint64_t>(esp_timer_get_time()/1000); }
template<size_t N> void copyText(char (&target)[N],const char *value) {
    strlcpy(target,value ? value : "",N);
}
bool applyClock(uint64_t unixMs,uint8_t source){
    const uint64_t maximum=(sizeof(time_t)<=4?uint64_t(std::numeric_limits<time_t>::max())*1000ULL+999ULL:uint64_t(4102444800000ULL));
    if(unixMs<1577836800000ULL||unixMs>maximum||!g_clockMutex)return false;
    xSemaphoreTake(g_clockMutex,portMAX_DELAY);
    portENTER_CRITICAL(&g_feedLock);const bool keepNtp=g_clockSource==2&&source!=2;portEXIT_CRITICAL(&g_feedLock);
    bool ok=keepNtp;
    if(!keepNtp){timeval value={static_cast<time_t>(unixMs/1000),static_cast<suseconds_t>((unixMs%1000)*1000)};ok=settimeofday(&value,nullptr)==0;if(ok){const int64_t offset=int64_t(unixMs)-int64_t(nowMs());portENTER_CRITICAL(&g_feedLock);g_epochOffset=offset;g_timeSynced=true;g_clockSource=source;portEXIT_CRITICAL(&g_feedLock);}}
    if(ok&&!keepNtp&&source!=3)meter_board::syncRtc(unixMs);
    xSemaphoreGive(g_clockMutex);return ok;
}
void ntpTimeAccepted(uint64_t unixMs){applyClock(unixMs,2);}
void ntpJson(JsonObject out){meter_ntp::writeSnapshotJson(out,meter_ntp::snapshot());}
uint64_t timestamp(uint64_t uptime) {
    portENTER_CRITICAL(&g_feedLock);
    const bool synced=g_timeSynced;
    const int64_t offset=g_epochOffset;
    portEXIT_CRITICAL(&g_feedLock);
    return synced && static_cast<int64_t>(uptime)+offset>0 ? static_cast<uint64_t>(static_cast<int64_t>(uptime)+offset) : 0;
}
void appendFrameLocked(Frame &frame) {
    frame.seq=++g_latest;
    if (g_recording) copyText(frame.sessionId,g_recordId);
    g_frames[(frame.seq-1)%g_capacity]=frame;
    if(frame.sessionId[0]){
        meter_storage::Record record;record.seq=frame.seq;record.timestampMs=frame.timestampMs;record.sample=frame.sample;
        copyText(record.sessionId,frame.sessionId);copyText(record.label,g_recordLabel);
        copyText(record.bootId,g_bootId.c_str());copyText(record.deviceId,g_deviceId.c_str());
        copyText(record.event,frame.event);copyText(record.message,frame.message);
        record.ruleId=frame.ruleId;record.code=frame.code;record.success=frame.success;record.reading=frame.reading;
        record.stopSession=!frame.reading&&!strcmp(frame.event,"recording")&&!strcmp(frame.message,"Recording stopped");
        // A fixed-size, zero-wait queue copy preserves sequence order. The
        // storage worker owns all card formatting and filesystem operations.
        meter_storage::append(record);
    }
}
void appendFrame(Frame &frame) {portENTER_CRITICAL(&g_feedLock);appendFrameLocked(frame);portEXIT_CRITICAL(&g_feedLock);}
void event(const char *type,const char *message,uint32_t rule=0,bool success=false,int code=0,const Sample *sample=nullptr) {
    if (!g_frames) return;
    Frame f;
    if (sample) f.sample=*sample;
    f.sample.uptimeMs=nowMs();
    f.timestampMs=timestamp(f.sample.uptimeMs);
    copyText(f.event,type);copyText(f.message,message);
    f.ruleId=rule;f.success=success;f.code=code;
    appendFrame(f);
}
class ReplyWriter {
public:
    size_t write(uint8_t byte){buffer_[used_++]=byte;if(used_==sizeof(buffer_))flush();return 1;}
    size_t write(const uint8_t *bytes,size_t count){for(size_t i=0;i<count;i++)write(bytes[i]);return count;}
    void flush(){if(used_){g_server.sendContent(reinterpret_cast<const char*>(buffer_),used_);used_=0;}}
private:
    uint8_t buffer_[1024];size_t used_=0;
};
void jsonReply(JsonDocument &doc,int code=200) {
    if(doc.overflowed()){g_server.send(503,"application/json","{\"ok\":false,\"error\":\"JSON memory exhausted\"}");return;}
    g_server.sendHeader("Cache-Control","no-store");
    g_server.sendHeader("X-Content-Type-Options","nosniff");
    g_server.setContentLength(measureJson(doc));g_server.send(code,"application/json","");
    ReplyWriter writer;serializeJson(doc,writer);writer.flush();
}
void fail(int status,const char *message) {
    PsramDocument d;d["ok"]=false;d["error"]=message;jsonReply(d,status);
}
void ok() { PsramDocument d;d["ok"]=true;jsonReply(d); }
bool sameOrigin() {
    // Clients without Origin (CLI/local integrations) are allowed. Browsers
    // supplying Origin must agree with both Host and a local interface address.
    String host=g_server.hostHeader();
    NetworkInfo n=network();
    String localAp=n.apIp,localSta=n.staIp;
    auto localHost=[&](const String &ip) { return ip.length()&&(host==ip||host==ip+":80"); };
    if (!localHost(localAp)&&!localHost(localSta)) { fail(403,"Host must be a device interface address");return false; }
    if (g_server.hasHeader("Origin")) {
        const String origin=g_server.header("Origin");
        if (origin!="http://"+host) { fail(403,"Foreign Origin rejected");return false; }
    }
    return true;
}
bool body(JsonDocument &doc) {
    if (!sameOrigin()) return false;
    String type=g_server.header("Content-Type");
    if (!type.startsWith("application/json")) { fail(415,"Use application/json");return false; }
    if (g_server.clientContentLength()>static_cast<int>(kMaxBody)||g_requestBody.length()>kMaxBody) { fail(413,"JSON request too large");return false; }
    DeserializationError error=deserializeJson(doc,g_requestBody,DeserializationOption::NestingLimit(8));
    if (error||!doc.is<JsonObject>()) { fail(400,"Invalid JSON object");return false; }
    return true;
}
void ruleJson(JsonObject out,const Rule &r,bool includeConfig,bool includeResults=true) {
    out["id"]=r.id;out["enabled"]=r.config.enabled;out["meter"]=r.config.meter;out["channel"]=r.config.channel;
    out["unit"]=r.config.unit;out["operator"]=r.config.op==meter_rules::Operator::Above ? "above" : "below";
    out["threshold"]=r.config.threshold;out["hysteresis"]=r.config.hysteresis;out["cooldownMs"]=r.config.cooldownMs;
    if (includeConfig) {
        out["url"]=r.url;out["method"]=r.method;out["body"]=r.body;out["caPem"]=r.caPem;
        JsonObject h=out["headers"].to<JsonObject>();
        for (const auto &entry:r.headers) h[entry.first]=entry.second;
    }
    if(includeResults) {
        out["armed"]=r.state.armed;out["lastResult"]=r.lastResult;out["lastCode"]=r.lastCode;
        out["lastDeliveryMs"]=r.lastDeliveryMs;out["fireCount"]=r.fireCount;out["deliveryCount"]=r.deliveryCount;
    }
}
bool number(JsonVariantConst v,double &out) {
    if (!v.is<double>()&&!v.is<int64_t>()&&!v.is<uint64_t>()) return false;
    out=v.as<double>();return std::isfinite(out);
}
bool integer(JsonVariantConst v,uint64_t max,uint64_t &out) {
    if (!v.is<uint64_t>()) return false;
    out=v.as<uint64_t>();return out<=max;
}
bool stringValue(JsonVariantConst v,size_t max,String &out,bool empty=true) {
    if(!v.is<const char*>())return false;
    JsonString value=v.as<JsonString>();
    if(value.size()>max||(!empty&&!value.size())||memchr(value.c_str(),0,value.size()))return false;
    out=value.c_str();return true;
}
bool parseRule(JsonVariantConst object,Rule &r,String &error) {
    uint64_t id=0,meter=0,channel=0,cooldown=1000;
    if (!object["id"].isNull()&&!integer(object["id"],UINT32_MAX,id)) { error="Invalid rule id";return false; }
    if (!integer(object["meter"],ut_meter::METER_SLOTS-1,meter)||!integer(object["channel"],7,channel)) { error="meter 0..3 and channel 0..7 required";return false; }
    if (!object["enabled"].isNull()&&!object["enabled"].is<bool>()) { error="enabled must be boolean";return false; }
    r.id=id;r.config.enabled=object["enabled"]|true;r.config.meter=meter;r.config.channel=channel;
    String unit;
    if (!stringValue(object["unit"],11,unit,false)) { error="unit must contain 1..11 bytes";return false; }
    for (unsigned i=0;i<unit.length();i++) if (static_cast<uint8_t>(unit[i])<32||unit[i]==127) { error="Invalid unit";return false; }
    r.config.unit=unit.c_str();
    String op=object["operator"]|"";
    if (op!="above"&&op!="below") { error="operator must be above or below";return false; }
    r.config.op=op=="above" ? meter_rules::Operator::Above : meter_rules::Operator::Below;
    if (!number(object["threshold"],r.config.threshold)) { error="Finite threshold required";return false; }
    if (!object["hysteresis"].isNull()&&!number(object["hysteresis"],r.config.hysteresis)) { error="Invalid hysteresis";return false; }
    if (r.config.hysteresis<0||!std::isfinite(r.config.threshold+r.config.hysteresis)||!std::isfinite(r.config.threshold-r.config.hysteresis)) { error="Invalid hysteresis range";return false; }
    if (!object["cooldownMs"].isNull()&&!integer(object["cooldownMs"],86400000,cooldown)) { error="cooldownMs must be 0..86400000";return false; }
    r.config.cooldownMs=cooldown;
    if(!stringValue(object["url"],512,r.url,false)){error="URL string required (max 512 bytes)";return false;}
    r.method="GET";
    if(!object["method"].isNull()&&!stringValue(object["method"],4,r.method,false)){error="Invalid method string";return false;}
    if (!meter_rules::validUrl(r.url.c_str())) { error="Use a valid http:// or https:// URL without userinfo or fragments (max 512 bytes)";return false; }
    if (r.method!="GET"&&r.method!="POST") { error="method must be GET or POST";return false; }
    if((!object["body"].isNull()&&!stringValue(object["body"],1024,r.body))||(!object["caPem"].isNull()&&!stringValue(object["caPem"],4096,r.caPem))) { error="Body or CA must be a bounded string";return false; }
    if (r.caPem.length()&&(!r.caPem.startsWith("-----BEGIN CERTIFICATE-----")||r.caPem.indexOf("-----END CERTIFICATE-----")<0)) { error="caPem must contain PEM certificates";return false; }
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    if (r.url.startsWith("https://")&&!r.caPem.length()) { error="HTTPS requires caPem on this build";return false; }
#endif
    JsonVariantConst h=object["headers"];
    if (!h.isNull()&&!h.is<JsonObjectConst>()) { error="headers must be an object";return false; }
    if (h.size()>8) { error="At most 8 headers";return false; }
    for (JsonPairConst item:h.as<JsonObjectConst>()) {
        if (!item.value().is<const char*>()) { error="Header values must be strings";return false; }
        String name=item.key().c_str(),value;
        if(item.key().size()!=name.length()||!stringValue(item.value(),256,value)){error="Invalid header string";return false;}
        if (!meter_rules::validHeader(name.c_str(),value.c_str())) { error="Invalid or reserved header";return false; }
        r.headers.emplace_back(name,value);
    }
    return true;
}
bool saveRules(const std::vector<Rule> &rules) {
    if(!g_prefsOpen)return false;
    PsramDocument d;JsonArray a=d.to<JsonArray>();
    // Persist configuration only: volatile delivery/cooldown state should not
    // consume the shared 20KB NVS partition.
    for (const Rule &r:rules) ruleJson(a.add<JsonObject>(),r,true,false);
    if(d.overflowed())return false;
    size_t expected=measureJson(d);
    String out;
    if(serializeJson(d,out)!=expected||out.length()!=expected)return false;
    return g_preferences.putString("rules",out)==out.length();
}
void loadRules(const String &stored) {
    if (!stored.length()||stored.length()>60000) return;
    PsramDocument d;
    if (deserializeJson(d,stored)||!d.is<JsonArray>()) return;
    for (JsonVariantConst v:d.as<JsonArrayConst>()) {
        Rule r;String error;
        if (g_rules.size()<kMaxRules&&parseRule(v,r,error)&&r.id) {
            bool duplicate=false;for (const Rule &existing:g_rules) if (existing.id==r.id) duplicate=true;
            if (duplicate) continue;
            r.revision=1;g_ruleCounter=std::max(g_ruleCounter,r.id);g_rules.push_back(r);
        }
    }
}
void appendReadingJson(JsonObject out,const Frame &f) {
    const Sample &s=f.sample;
    out["seq"]=f.seq;out["kind"]=f.reading ? "reading" : "event";
    out["meter"]=s.meter;out["channel"]=s.channel;out["uptimeMs"]=s.uptimeMs;out["sessionId"]=String(f.sessionId);
    if (f.timestampMs) out["timestampMs"]=f.timestampMs;else out["timestampMs"]=nullptr;
    if (f.reading) {
        out["address"]=String(s.address);out["profile"]=String(s.profile);out["packet"]=s.packet;
        const bool numeric=s.numeric&&std::isfinite(s.value);
        out["numeric"]=numeric;if(numeric)out["value"]=s.value;else out["value"]=nullptr;
        out["text"]=String(s.text);out["unit"]=String(s.unit);out["mode"]=String(s.mode);out["modeKey"]=s.modeKey;out["flags"]=s.flags;
    } else {
        out["event"]=String(f.event);out["message"]=String(f.message);out["ruleId"]=f.ruleId;out["success"]=f.success;out["code"]=f.code;
    }
}
bool parseUnsigned(const String &s,uint64_t &value) {
    if (!s.length()||s.length()>20) return false;
    value=0;
    for (unsigned i=0;i<s.length();i++) {
        if (s[i]<'0'||s[i]>'9') return false;
        uint64_t digit=s[i]-'0';if(value>(UINT64_MAX-digit)/10)return false;
        value=value*10+digit;
    }
    return true;
}
void routeStatus() {
    if(!sameOrigin())return;
    PsramDocument d;
    if (deserializeJson(d,g_provider->stateJson())||!d.is<JsonObject>()) { d.clear();d.to<JsonObject>();d["providerError"]=true; }
    JsonObject device=d["device"].to<JsonObject>();device["id"]=g_deviceId;device["bootId"]=g_bootId;device["uptimeMs"]=nowMs();device["freeHeap"]=ESP.getFreeHeap();device["minFreeHeap"]=ESP.getMinFreeHeap();device["largestInternalBlock"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);device["freePsram"]=ESP.getFreePsram();
    JsonObject mapping=device["mapping"].to<JsonObject>();
    mapping["id"]=METER_ID;mapping["sha256"]=METER_MAPPING_SHA256;
    mapping["displayBackend"]=METER_DISPLAY_BACKEND;mapping["touchDriver"]=METER_TOUCH_DRIVER;
    mapping["nativeWidth"]=METER_DISPLAY_WIDTH;mapping["nativeHeight"]=METER_DISPLAY_HEIGHT;
    mapping["readingRotation"]=METER_UI_READING_ROTATION;mapping["setupRotation"]=METER_UI_SETUP_ROTATION;mapping["advancedRotation"]=METER_UI_ADVANCED_ROTATION;
    NetworkInfo n=network();JsonObject net=d["network"].to<JsonObject>();
    net["apSsid"]=n.apSsid;net["apIp"]=n.apIp;net["staIp"]=n.staIp;net["connected"]=n.connected;
    net["staSsid"]=n.staSsid;net["state"]=n.staState;net["message"]=n.message;
    bool active,synced;char id[40],label[97];uint64_t latest;uint32_t drops,deliveryDrops;
    portENTER_CRITICAL(&g_feedLock);
    active=g_recording;synced=g_timeSynced;copyText(id,g_recordId);copyText(label,g_recordLabel);latest=g_latest;drops=g_sampleDrops;deliveryDrops=g_deliveryDrops;
    portEXIT_CRITICAL(&g_feedLock);
    JsonObject rec=d["recording"].to<JsonObject>();rec["active"]=active;rec["id"]=String(id);rec["label"]=String(label);
    rec["storage"]="browser-indexeddb";rec["ramCapacity"]=g_capacity;
#if METER_STORAGE_ENABLED
    rec["storage"]="browser-indexeddb+tf-card";
#endif
    PsramDocument card;if(!deserializeJson(card,meter_storage::statusJson()))d["storage"]=card.as<JsonVariantConst>();
    uint8_t clockSource;portENTER_CRITICAL(&g_feedLock);clockSource=g_clockSource;portEXIT_CRITICAL(&g_feedLock);
    auto clock=d["clock"].to<JsonObject>();clock["source"]=clockSource==2?"ntp":clockSource==1?"browser":clockSource==3?"rtc":"none";clock["synced"]=synced;const auto utc=timestamp(nowMs());if(utc)clock["unixMs"]=utc;else clock["unixMs"]=nullptr;
    meter_board::writeStatus(d["boardDevices"].to<JsonObject>());
    ntpJson(d["ntp"].to<JsonObject>());
    if(g_server.arg("compact")=="1"){for(JsonObject meter:d["meters"].as<JsonArray>())meter.remove("readings");d["display"]["values"].remove("formats");}
    d["latestSeq"]=latest;d["oldestSeq"]=meter_rules::feedCursor(latest,g_capacity,latest).oldest;d["timeSynced"]=synced;
    JsonObject hooks=d["webhook"].to<JsonObject>();hooks["queued"]=uxQueueMessagesWaiting(g_jobs);hooks["dropped"]=deliveryDrops;hooks["sampleQueueDropped"]=drops;
    JsonArray rules=d["rules"].to<JsonArray>();
    xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
    for (const Rule &r:g_rules) ruleJson(rules.add<JsonObject>(),r,false);
    xSemaphoreGive(g_ruleMutex);
    jsonReply(d);
}
void routeFrames() {
    if(!sameOrigin())return;
    uint64_t after=0,limit=64;
    if ((g_server.hasArg("after")&&!parseUnsigned(g_server.arg("after"),after))||(g_server.hasArg("limit")&&!parseUnsigned(g_server.arg("limit"),limit))||!limit||limit>kFrameLimit) { fail(400,"after must be unsigned; limit must be 1..128");return; }
    uint64_t latest;
    portENTER_CRITICAL(&g_feedLock);latest=g_latest;portEXIT_CRITICAL(&g_feedLock);
    meter_rules::FeedCursor window=meter_rules::feedCursor(latest,g_capacity,after);
    bool gap=window.gap;uint64_t cursor=window.next;
    PsramDocument d;d["bootId"]=g_bootId;d["oldestSeq"]=window.oldest;d["latestSeq"]=latest;
    JsonArray frames=d["frames"].to<JsonArray>();
    for (unsigned count=0;cursor<latest&&count<limit;count++) {
        Frame f;uint64_t wanted=cursor+1;
        portENTER_CRITICAL(&g_feedLock);f=g_frames[(wanted-1)%g_capacity];portEXIT_CRITICAL(&g_feedLock);
        if (f.seq!=wanted) { gap=true;break; }
        appendReadingJson(frames.add<JsonObject>(),f);cursor=wanted;
    }
    d["nextSeq"]=cursor;d["gap"]=gap;jsonReply(d);
}
void routeRecording() {
    PsramDocument d;if (!body(d)) return;
    String action=d["action"]|"",label=d["label"]|"";
    if (!d["label"].isNull()&&!stringValue(d["label"],96,label)) { fail(400,"Label must be a string of at most 96 bytes");return; }
    if (action=="start") {
        PsramDocument card;
        if(!deserializeJson(card,meter_storage::statusJson())&&((card["remounting"]|false)||(card["formatting"]|false))){fail(409,"TF card maintenance is running; wait before starting a recording");return;}
        if((card["supported"]|false)&&((card["recording"]|false)||(card["queueDepth"]|0)>0||(card["bufferedRecords"]|0)>0)){fail(409,"TF card is finishing the previous recording; wait before starting another");return;}
        String id=g_bootId+"-"+String(g_recordCounter+1);
        Frame start;start.sample.uptimeMs=nowMs();start.timestampMs=timestamp(start.sample.uptimeMs);
        copyText(start.event,"recording");copyText(start.message,"Recording started");
        portENTER_CRITICAL(&g_feedLock);
        if (g_recording) { portEXIT_CRITICAL(&g_feedLock);fail(409,"Recording already active");return; }
        ++g_recordCounter;copyText(g_recordId,id.c_str());copyText(g_recordLabel,label.c_str());g_recording=true;
        appendFrameLocked(start);
        portEXIT_CRITICAL(&g_feedLock);
    } else if (action=="stop") {
        Frame stop;stop.sample.uptimeMs=nowMs();stop.timestampMs=timestamp(stop.sample.uptimeMs);
        copyText(stop.event,"recording");copyText(stop.message,"Recording stopped");
        portENTER_CRITICAL(&g_feedLock);
        if(g_recording){appendFrameLocked(stop);g_recording=false;}
        portEXIT_CRITICAL(&g_feedLock);
    } else if (action=="mark") event("mark",label.c_str());
    else { fail(400,"action must be start, stop or mark");return; }
    ok();
}
void routeTime() {
    PsramDocument d;if(!body(d))return;uint64_t unixMs;
    const uint64_t maximum=(sizeof(time_t)<=4?uint64_t(std::numeric_limits<time_t>::max())*1000ULL+999ULL:uint64_t(4102444800000ULL));
    if(!integer(d["unixMs"],maximum,unixMs)||unixMs<1577836800000ULL){fail(400,"unixMs is outside the supported system-clock range");return;}
    if(!applyClock(unixMs,1)){fail(500,"Unable to set synchronized clock");return;}
    uint8_t source;portENTER_CRITICAL(&g_feedLock);source=g_clockSource;portEXIT_CRITICAL(&g_feedLock);PsramDocument out;out["ok"]=true;out["source"]=source==2?"ntp":"browser";jsonReply(out);
}
void routeNtp(){
    PsramDocument d;if(!body(d))return;
    if(!d["action"].isUnbound()){
        if(!d["action"].is<const char*>()||d["action"].as<JsonString>().size()!=4||memcmp(d["action"].as<const char*>(),"sync",4)||!d["enabled"].isUnbound()||!d["server"].isUnbound()){fail(400,"Use action sync without settings");return;}
        if(!meter_ntp::requestSync()){fail(503,"NTP request queue unavailable");return;}
    }else{
        if(!d["enabled"].is<bool>()||!d["server"].is<const char*>()){fail(400,"enabled boolean and server string required");return;}
        const JsonString server=d["server"].as<JsonString>();if(server.size()!=strlen(server.c_str())||!meter_ntp::validServer(server.c_str())){fail(400,"Use an NTP hostname or unicast IPv4 address");return;}
        String error;if(!meter_ntp::configure(d["enabled"].as<bool>(),server.c_str(),error)){fail(503,error.c_str());return;}
    }
    PsramDocument out;out["ok"]=true;out["accepted"]=true;jsonReply(out,202);
}
void routeWifi() {
    PsramDocument d;if (!body(d)) return;
    String ssid,password;
    if (!stringValue(d["ssid"],32,ssid)||!stringValue(d["password"],63,password)) { fail(400,"ssid and password bounded strings required");return; }
    if (ssid.length()>32||password.length()>63||(password.length()>0&&password.length()<8)) { fail(400,"SSID max 32 bytes; WPA password empty or 8..63 bytes");return; }
    WifiCommand c;copyText(c.ssid,ssid.c_str());copyText(c.password,password.c_str());
    // One blob makes SSID/password a single atomic NVS update. Empty strings
    // are unambiguous because putBytes returns the full fixed record length.
    if(!g_prefsOpen||g_preferences.putBytes("wifi",&c,sizeof(c))!=sizeof(c)) {
        event("configuration","WiFi save failed; station settings unchanged");
        fail(507,"WiFi settings were not saved; station settings unchanged (NVS unavailable/full)");return;
    }
    if(xQueueOverwrite(g_wifiCommands,&c)!=pdTRUE){fail(503,"WiFi saved; unable to queue station reconfiguration");return;}
    PsramDocument result;result["ok"]=true;result["joining"]=ssid.length()>0;jsonReply(result,202);
}
const char *wifiSecurity(wifi_auth_mode_t auth) {
    switch(auth){case WIFI_AUTH_OPEN:return "Open";case WIFI_AUTH_WEP:return "WEP";
    case WIFI_AUTH_WPA_PSK:return "WPA";case WIFI_AUTH_WPA2_PSK:return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:return "WPA/WPA2";case WIFI_AUTH_WPA3_PSK:return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:return "WPA2/WPA3";case WIFI_AUTH_WPA2_ENTERPRISE:case WIFI_AUTH_WPA3_ENT_192:return "Enterprise";
    default:return "Unsupported";}
}
bool wifiJoinable(wifi_auth_mode_t auth) {
    return auth==WIFI_AUTH_OPEN||auth==WIFI_AUTH_WPA2_PSK||auth==WIFI_AUTH_WPA_WPA2_PSK||auth==WIFI_AUTH_WPA3_PSK||auth==WIFI_AUTH_WPA2_WPA3_PSK;
}
void routeWifiScanGet() {
    if(!sameOrigin())return;
    String state,message;uint64_t finished;std::vector<WifiFound> found;
    xSemaphoreTake(g_netMutex,portMAX_DELAY);state=g_scanState;message=g_scanMessage;finished=g_scanFinishedAt;found=g_wifiFound;xSemaphoreGive(g_netMutex);
    PsramDocument d;d["state"]=state;d["message"]=message;d["finishedAt"]=finished;d["limit"]=32;
    auto networks=d["networks"].to<JsonArray>();
    for(const auto &network:found){auto item=networks.add<JsonObject>();item["ssid"]=network.ssid;item["rssi"]=network.rssi;item["channel"]=network.channel;item["security"]=wifiSecurity(network.auth);item["joinable"]=wifiJoinable(network.auth);}
    jsonReply(d);
}
void routeWifiScanPost() {
    PsramDocument bodyDoc;if(!body(bodyDoc))return;
    xSemaphoreTake(g_netMutex,portMAX_DELAY);
    bool joining=g_network.staState=="joining";
    if(!joining && g_scanState!="queued" && g_scanState!="scanning"){
        g_scanRequested=true;g_scanState="queued";g_scanMessage="Wi-Fi scan queued";
    }
    xSemaphoreGive(g_netMutex);
    if(joining){fail(409,"Wait for the Wi-Fi join to finish before scanning");return;}
    PsramDocument result;result["ok"]=true;result["state"]="queued";jsonReply(result,202);
}
void routeRulesGet() {
    if(!sameOrigin())return;
    PsramDocument d;JsonArray a=d["rules"].to<JsonArray>();
    xSemaphoreTake(g_ruleMutex,portMAX_DELAY);for(const Rule &r:g_rules)ruleJson(a.add<JsonObject>(),r,true);xSemaphoreGive(g_ruleMutex);
    jsonReply(d);
}
void routeRulesPost() {
    PsramDocument d;if (!body(d)) return;
    Rule r;String error;
    if (!parseRule(d.as<JsonVariantConst>(),r,error)) { fail(400,error.c_str());return; }
    xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
    auto found=g_rules.end();for(auto it=g_rules.begin();it!=g_rules.end();++it)if(it->id==r.id)found=it;
    if(found==g_rules.end()&&g_rules.size()>=kMaxRules) { xSemaphoreGive(g_ruleMutex);fail(409,"At most 8 rules");return; }
    if(r.id&&found==g_rules.end()) { xSemaphoreGive(g_ruleMutex);fail(404,"Rule not found");return; }
    // Hold the rule lock until persistence completes: other rules' live
    // cooldown/rearm state cannot move while the staged vector is saved.
    const bool adding=!r.id;
    std::vector<Rule> staged=g_rules;
    if(adding) {r.id=g_ruleCounter+1;r.revision=1;staged.push_back(r);}
    else {r.revision=found->revision+1;staged[static_cast<size_t>(found-g_rules.begin())]=r;}
    bool saved=meter_rules::commitIfSaved(g_rules,staged,saveRules);
    if(saved&&adding)g_ruleCounter=r.id;
    xSemaphoreGive(g_ruleMutex);
    if(!saved) { event("configuration","Rule save failed; active rules unchanged",r.id,false);fail(507,"Rule was not saved; active rules unchanged (NVS unavailable/full)");return; }
    PsramDocument result;result["ok"]=true;result["id"]=r.id;jsonReply(result);
}
void routeRulesDelete() {
    if(!sameOrigin())return;
    uint64_t id;
    if(!parseUnsigned(g_server.arg("id"),id)||!id||id>UINT32_MAX) {fail(400,"Valid id required");return;}
    xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
    auto found=g_rules.end();for(auto it=g_rules.begin();it!=g_rules.end();++it)if(it->id==id)found=it;
    if(found==g_rules.end()){xSemaphoreGive(g_ruleMutex);fail(404,"Rule not found");return;}
    std::vector<Rule> staged=g_rules;
    staged.erase(staged.begin()+(found-g_rules.begin()));
    bool saved=meter_rules::commitIfSaved(g_rules,staged,saveRules);
    xSemaphoreGive(g_ruleMutex);
    if(!saved){event("configuration","Rule deletion save failed; active rules unchanged",id,false);fail(507,"Rule was not deleted; active rules unchanged (NVS unavailable/full)");return;}
    ok();
}
bool queueJob(const Rule &rule,const Sample &sample,bool manual) {
    Job *job=new(std::nothrow) Job;
    if (!job) { event("delivery","Out of memory; request not sent",rule.id,false,0,&sample);return false; }
    job->rule=rule;job->sample=sample;job->timestampMs=timestamp(sample.uptimeMs);job->manual=manual;
    if(xQueueSend(g_jobs,&job,0)!=pdTRUE) {
        delete job;
        portENTER_CRITICAL(&g_feedLock);++g_deliveryDrops;portEXIT_CRITICAL(&g_feedLock);
        event("delivery","Outbound queue full; request not sent",rule.id,false,0,&sample);return false;
    }
    event("trigger",manual ? "Manual test queued" : "Threshold crossed; request queued",rule.id,true,0,&sample);
    return true;
}
void routeRulesTest() {
    PsramDocument d;if(!body(d))return;
    uint64_t id;
    if(!integer(d["id"],UINT32_MAX,id)||!id){fail(400,"Valid id required");return;}
    Rule r;Sample s;bool found=false,have=false;
    xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
    for(const Rule &candidate:g_rules)if(candidate.id==id){r=candidate;found=true;break;}
    if(found){have=g_haveLast[r.config.meter][r.config.channel];s=g_lastSamples[r.config.meter][r.config.channel];}
    xSemaphoreGive(g_ruleMutex);
    if(!found){fail(404,"Rule not found");return;}
    if(!have||!s.numeric||!std::isfinite(s.value)||r.config.unit!=s.unit||nowMs()-s.uptimeMs>30000){fail(409,"Manual test needs a matching numeric reading from the last 30 seconds");return;}
    if(!queueJob(r,s,true)){fail(503,"Request not queued");return;}ok();
}
// Stream JSON through a bounded buffer instead of WebServer's default plain
// body parser, which allocates the advertised Content-Length before our route.
class JsonHandler:public RequestHandler {
public:
    JsonHandler(const char *path,WebServer::THandlerFunction callback):path_(path),callback_(callback) {}
    bool canHandle(HTTPMethod method,String uri) override {
        const bool matched=method==HTTP_POST&&uri==path_;
        // A multipart request can bypass raw() in the pinned parser. Clear the
        // previous request at handler selection, before choosing its body path.
        if(matched){g_requestBody="";g_requestBodyFailed=false;g_requestBodyReady=false;}
        return matched;
    }
    bool canRaw(String) override {return true;}
    bool handle(WebServer &,HTTPMethod,String) override {
        if(g_requestBodyFailed)return true;
        if(!g_requestBodyReady){fail(415,"A raw application/json body is required");return true;}
        callback_();return true;
    }
    void raw(WebServer &server,String,HTTPRaw &raw) override {
        if(raw.status==RAW_START) {
            g_requestBody="";g_requestBodyFailed=false;g_requestBodyReady=false;
            // The pinned WebServer raw parser requests HTTP_RAW_BUFLEN even
            // for the final short chunk. Consume the exact declared length
            // here and advance its byte counter so it skips that blocking read.
            const int length=server.clientContentLength();
            raw.totalSize=size_t(length);
            auto reject=[&](int code,const char *message){g_requestBodyFailed=true;fail(code,message);server.client().stop();};
            if(server.clientContentLength()<0||server.clientContentLength()>static_cast<int>(kMaxBody)) {
                reject(413,"JSON request too large");return;
            }
            if(!sameOrigin()){g_requestBodyFailed=true;server.client().stop();return;}
            String type=server.header("Content-Type");
            if(!type.startsWith("application/json")){reject(415,"Use application/json");return;}
            if(length&&!g_requestBody.reserve(length)){reject(503,"JSON memory exhausted");return;}
            auto client=server.client();size_t received=0;
            const auto result=meter_http::readBody(client,size_t(length),kMaxBody,5000,[](){return millis();},[](){vTaskDelay(pdMS_TO_TICKS(2));},[](const uint8_t *bytes,size_t count){return bool(g_requestBody.concat(reinterpret_cast<const char*>(bytes),count));},received);
            if(result!=meter_http::BodyResult::Complete)reject(result==meter_http::BodyResult::NoMemory?503:408,result==meter_http::BodyResult::NoMemory?"JSON memory exhausted":"Incomplete JSON request body");
            else g_requestBodyReady=true;
        }
    }
private:
    String path_;
    WebServer::THandlerFunction callback_;
};
void jsonRoute(const char *path,WebServer::THandlerFunction fn) {g_server.addHandler(new JsonHandler(path,fn));}
void routeWifiQr() {
    if(!sameOrigin())return;
    const auto n=network();meter_wifi::Qr qr;
    if(!meter_wifi::encode(n.apSsid.c_str(),n.apPassword.c_str(),qr)){fail(503,"Wi-Fi QR not ready");return;}
    const unsigned side=qr.size()+8;
    String svg;svg.reserve(10000);
    svg="<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "+String(side)+" "+String(side)+"\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/><path fill=\"black\" d=\"";
    for(unsigned y=0;y<qr.size();++y)for(unsigned x=0;x<qr.size();) {
        if(!qr.dark(x,y)){++x;continue;}
        unsigned start=x;while(x<qr.size()&&qr.dark(x,y))++x;
        unsigned width=x-start;
        svg+="M"+String(start+4)+" "+String(y+4)+"h"+String(width)+"v1h-"+String(width)+"z";
    }
    svg+="\"/></svg>";
    g_server.sendHeader("Cache-Control","no-store");g_server.sendHeader("X-Content-Type-Options","nosniff");
    g_server.send(200,"image/svg+xml",svg);
}
// File transfers have a separate listener/worker so large downloads never
// occupy the control server's handleClient loop or starve browser feed polls.
#if METER_STORAGE_ENABLED
void serveCardDownload(){
    auto reject=[&](int code,const char *message){PsramDocument d;d["ok"]=false;d["error"]=message;String json;serializeJson(d,json);g_fileServer.send(code,"application/json",json);};
    const auto net=network();const String host=g_fileServer.hostHeader();String matched;
    for(const String &ip:{net.apIp,net.staIp})if(ip.length()&&(host==ip||host==ip+":81")){matched=ip;break;}
    if(!matched.length()){reject(403,"Host must be a device interface address");return;}
    if(g_fileServer.hasHeader("Origin")){
        String origin=g_fileServer.header("Origin");
        if(origin!="http://"+matched&&origin!="http://"+matched+":80"&&origin!="http://"+matched+":81"){reject(403,"Foreign Origin rejected");return;}
    }
    uint64_t size=0;String error;
    const uint32_t token=meter_storage::openDownload(g_fileServer.arg("name"),size,error);
    if(!token){reject(409,error.c_str());return;}
    if(size>=SIZE_MAX){meter_storage::closeDownload(token);reject(413,"Log is too large for this HTTP server");return;}
    g_fileServer.sendHeader("Cache-Control","no-store");g_fileServer.sendHeader("X-Content-Type-Options","nosniff");
    g_fileServer.sendHeader("Content-Disposition","attachment; filename=\""+g_fileServer.arg("name")+"\"");
    g_fileServer.setContentLength(size_t(size));g_fileServer.send(200,"text/csv; charset=utf-8","");
    uint8_t buffer[1024];uint64_t left=size;auto client=g_fileServer.client();uint32_t busySince=0;
    while(left&&client.connected()){
        size_t count=meter_storage::readDownload(token,buffer,size_t(std::min<uint64_t>(left,sizeof(buffer))),error);
        if(!count){
            if(error.indexOf("busy")>=0){if(!busySince)busySince=millis();if(uint32_t(millis()-busySince)<2000){vTaskDelay(pdMS_TO_TICKS(10));continue;}}
            break;
        }
        busySince=0;
        size_t sent=0;
        while(sent<count&&client.connected()){size_t n=client.write(buffer+sent,count-sent);if(!n)break;sent+=n;}
        left-=sent;if(sent!=count)break;vTaskDelay(pdMS_TO_TICKS(1));
    }
    meter_storage::closeDownload(token);
    // A short read/write terminates the response. Do not append JSON to a CSV
    // body or pretend a partial snapshot reached the advertised length.
    if(left)client.stop();
}
void fileTask(void*){
    while(!g_started)vTaskDelay(pdMS_TO_TICKS(20));
    const char *headers[]={"Origin"};g_fileServer.collectHeaders(headers,1);
    g_fileServer.on("/api/storage/download",HTTP_GET,serveCardDownload);g_fileServer.begin();
    for(;;){g_fileServer.handleClient();vTaskDelay(pdMS_TO_TICKS(2));}
}
#endif
void routeCardDownload(){
    if(!sameOrigin())return;
    const String name=g_server.arg("name");
    if(!meter_log_format::validLogName(name.c_str())){fail(400,"Invalid TF log filename");return;}
    PsramDocument card;
    if(deserializeJson(card,meter_storage::statusJson())||!(card["supported"]|false)){fail(409,"TF downloads are not available on this board");return;}
    String url="http://"+g_server.client().localIP().toString()+":81/api/storage/download?name="+String(meter_rules::urlEncode(name.c_str()).c_str());
    g_server.sendHeader("Cache-Control","no-store");g_server.sendHeader("Location",url);g_server.send(302,"text/plain","TF log download");
}
void routes() {
    const char *headers[]={"Origin","Content-Type"};g_server.collectHeaders(headers,2);
    g_server.on("/",HTTP_GET,[]{g_server.sendHeader("Cache-Control","no-cache");g_server.sendHeader("X-Content-Type-Options","nosniff");g_server.send_P(200,"text/html; charset=utf-8",METER_WEB_HTML);});
    g_server.on("/readme.html",HTTP_GET,[]{g_server.sendHeader("Cache-Control","no-cache");g_server.sendHeader("X-Content-Type-Options","nosniff");g_server.send_P(200,"text/html; charset=utf-8",METER_README_HTML);});
    g_server.on("/api/status",HTTP_GET,routeStatus);
    g_server.on("/api/board-map",HTTP_GET,[]{if(!sameOrigin())return;g_server.sendHeader("Cache-Control","no-store");g_server.sendHeader("X-Content-Type-Options","nosniff");g_server.sendHeader("Content-Disposition","attachment; filename=board-map.json");g_server.send_P(200,"application/json",METER_BOARD_MAP_JSON);});
    g_server.on("/api/wifi-qr.svg",HTTP_GET,routeWifiQr);
    g_server.on("/api/storage/status",HTTP_GET,[]{if(!sameOrigin())return;g_server.sendHeader("Cache-Control","no-store");g_server.send(200,"application/json",meter_storage::statusJson());});
    g_server.on("/api/storage/files",HTTP_GET,[]{if(!sameOrigin())return;g_server.sendHeader("Cache-Control","no-store");g_server.send(200,"application/json",meter_storage::filesJson());});
    g_server.on("/api/storage/download",HTTP_GET,routeCardDownload);
    jsonRoute("/api/storage/remount",[]{PsramDocument d;if(!body(d))return;bool recording;portENTER_CRITICAL(&g_feedLock);recording=g_recording;portEXIT_CRITICAL(&g_feedLock);if(recording){fail(409,"Stop recording before remounting the TF card");return;}String error;if(!meter_storage::requestRemount(error)){fail(409,error.c_str());return;}PsramDocument out;out["ok"]=true;out["queued"]=true;jsonReply(out,202);});
    jsonRoute("/api/storage/format",[]{
        PsramDocument d;if(!body(d))return;
        if(!d["confirm"].is<const char*>()||strcmp(d["confirm"].as<const char*>(),"FORMAT TF CARD")){fail(400,"Explicit confirmation FORMAT TF CARD is required; this erases all TF-card data");return;}
        if(!d["operationId"].is<const char*>()){fail(400,"A unique format operationId is required");return;}
        bool recording;portENTER_CRITICAL(&g_feedLock);recording=g_recording;portEXIT_CRITICAL(&g_feedLock);
        if(recording){fail(409,"Stop recording before formatting the TF card");return;}
        String error;if(!meter_storage::requestFormat(d["operationId"].as<String>(),error)){fail(409,error.c_str());return;}
        PsramDocument out;out["ok"]=true;out["queued"]=true;out["operationId"]=d["operationId"];jsonReply(out,202);
    });
    g_server.on("/api/profiles",HTTP_GET,[]{g_server.sendHeader("Cache-Control","no-store");g_server.send(200,"application/json",g_provider->profilesJson());});
    g_server.on("/api/frames",HTTP_GET,routeFrames);
    jsonRoute("/api/control",[]{PsramDocument d;if(!body(d))return;String error;if(!g_provider->control(d.as<JsonVariantConst>(),error))fail(400,error.c_str());else ok();});
    jsonRoute("/api/recording",routeRecording);
    jsonRoute("/api/time",routeTime);
    jsonRoute("/api/ntp",routeNtp);
    g_server.on("/api/ntp",HTTP_GET,[]{if(!sameOrigin())return;PsramDocument d;ntpJson(d.to<JsonObject>());jsonReply(d);});
    jsonRoute("/api/wifi",routeWifi);
    g_server.on("/api/wifi/scan",HTTP_GET,routeWifiScanGet);
    jsonRoute("/api/wifi/scan",routeWifiScanPost);
    g_server.on("/api/rules",HTTP_GET,routeRulesGet);
    jsonRoute("/api/rules",routeRulesPost);
    g_server.on("/api/rules",HTTP_DELETE,routeRulesDelete);
    jsonRoute("/api/rules/test",routeRulesTest);
    g_server.onNotFound([]{fail(404,"Not found");});
    g_server.begin();
}
void webTask(void*) {
    while(!g_started)vTaskDelay(pdMS_TO_TICKS(20));
    routes();
    for(;;){g_server.handleClient();vTaskDelay(pdMS_TO_TICKS(2));}
}
void networkTask(void*) {
    // Arduino has populated its scan buffer before it invokes this callback.
    // A stop request alone is not permission to delete that shared buffer.
    WiFi.onEvent([](arduino_event_id_t,arduino_event_info_t info){g_wifiScanStatus=info.wifi_scan_done.status;g_wifiScanDone=true;},ARDUINO_EVENT_WIFI_SCAN_DONE);
    WiFi.persistent(false);WiFi.mode(WIFI_AP_STA);
    meter_ntp::begin(ntpTimeAccepted);
    // IDF 4.4 requires modem sleep for Wi-Fi/Bluetooth coexistence on S3.
    // WIFI_PS_NONE aborts in pm_set_sleep_type once both radios are enabled.
    WiFi.setSleep(WIFI_PS_MIN_MODEM);WiFi.setAutoReconnect(true);
    NetworkInfo n=network();
    bool ap=WiFi.softAP(n.apSsid.c_str(),n.apPassword.c_str(),1,false,4);
    if(!ap)event("network","Access point failed to start");
    String desiredSsid;uint64_t joinStartedAt=0,scanStartedAt=0;
    bool joinedOnce=false,scanning=false,scanStopping=false,scanCancelled=false,restoreReconnect=true,hasJoin=false;
    WifiCommand pendingJoin;
    auto resumeReconnect=[&](){WiFi.setAutoReconnect(restoreReconnect);if(restoreReconnect&&desiredSsid.length()&&WiFi.status()!=WL_CONNECTED)WiFi.reconnect();};
    if(g_prefsOpen) {
        WifiCommand saved;
        bool valid=false;
        if(g_preferences.isKey("wifi")) {
            valid=g_preferences.getBytesLength("wifi")==sizeof(saved)&&g_preferences.getBytes("wifi",&saved,sizeof(saved))==sizeof(saved)&&saved.ssid[32]==0&&saved.password[64]==0;
            if(!valid)event("configuration","Saved WiFi credentials could not be read; station not started");
        } else {
            // Read older two-key settings until the next successful single-blob save.
            String ssid=g_preferences.getString("ssid",""),password=g_preferences.getString("staPass","");
            if(ssid.length()<=32&&password.length()<=63){copyText(saved.ssid,ssid.c_str());copyText(saved.password,password.c_str());valid=true;}
        }
        if(valid&&saved.ssid[0]){desiredSsid=saved.ssid;joinStartedAt=nowMs();WiFi.begin(saved.ssid,saved.password);}
    }
    g_started=true;
    bool wasConnected=false;
    for(;;) {
        WifiCommand c;
        if(xQueueReceive(g_wifiCommands,&c,pdMS_TO_TICKS(250))==pdTRUE) {
            pendingJoin=c;hasJoin=true;
            if(scanning&&!scanStopping){esp_wifi_scan_stop();scanStopping=true;scanCancelled=true;}
            xSemaphoreTake(g_netMutex,portMAX_DELAY);
            if(g_scanRequested){g_scanRequested=false;g_scanState="cancelled";g_scanMessage="Scan cancelled to join network";}
            if(scanning)g_scanMessage="Finishing Wi-Fi scan before joining network";
            xSemaphoreGive(g_netMutex);
        }
        bool connected=WiFi.status()==WL_CONNECTED;
        if(connected)joinedOnce=true;
        xSemaphoreTake(g_netMutex,portMAX_DELAY);
        g_network.apIp=WiFi.softAPIP().toString();g_network.connected=connected;
        g_network.staIp=connected ? WiFi.localIP().toString() : "";
        g_network.staSsid=desiredSsid;
        g_network.staState=connected?"connected":desiredSsid.length()?(nowMs()-joinStartedAt<20000?"joining":joinedOnce?"reconnecting":"failed"):"disconnected";
        g_network.message=connected?"Connected to "+desiredSsid:g_network.staState=="joining"?"Joining "+desiredSsid:g_network.staState=="failed"?"Could not join; check password, signal and 2.4 GHz support":g_network.staState=="reconnecting"?"Connection lost; reconnecting":"Choose a network to join";
        bool requested=g_scanRequested&&!scanning&&!hasJoin;g_scanRequested=false;
        if(requested){g_scanState="scanning";g_scanMessage="Scanning nearby 2.4 GHz networks...";}
        xSemaphoreGive(g_netMutex);
        if(requested){
            restoreReconnect=WiFi.getAutoReconnect();WiFi.setAutoReconnect(false);
            // A failed saved join otherwise keeps re-entering the driver's
            // connecting state and prevents an explicit scan from starting.
            if(WiFi.status()!=WL_CONNECTED)WiFi.disconnect(false,false);
            g_wifiScanDone=false;g_wifiScanStatus=0;scanStartedAt=nowMs();scanStopping=false;scanCancelled=false;
            int result=WiFi.scanNetworks(true,true,false,120);
            scanning=result==WIFI_SCAN_RUNNING||result>=0;
            if(!scanning){resumeReconnect();xSemaphoreTake(g_netMutex,portMAX_DELAY);g_scanState="failed";g_scanMessage="Scan could not start; please retry";xSemaphoreGive(g_netMutex);}
        }
        if(scanning){
            if(!scanStopping&&nowMs()-scanStartedAt>10000){esp_wifi_scan_stop();scanStopping=true;}
            if(g_wifiScanDone){
                int count=WiFi.scanComplete();
                std::vector<WifiFound> found;
                if(count>=0&&!scanCancelled&&g_wifiScanStatus==0)for(int i=0;i<count;++i){
                    WifiFound item;item.ssid=WiFi.SSID(i);item.rssi=WiFi.RSSI(i);item.channel=WiFi.channel(i);item.auth=WiFi.encryptionType(i);
                    if(!item.ssid.length()||item.ssid==n.apSsid)continue;
                    auto same=std::find_if(found.begin(),found.end(),[&](const WifiFound &old){return old.ssid==item.ssid&&old.auth==item.auth;});
                    if(same!=found.end()){if(item.rssi>same->rssi)*same=item;}
                    else if(found.size()<32)found.push_back(item);
                    else if(item.rssi>found.back().rssi)found.back()=item;
                    std::sort(found.begin(),found.end(),[](const WifiFound &a,const WifiFound &b){return a.rssi>b.rssi;});
                }
                WiFi.scanDelete();scanning=false;
                xSemaphoreTake(g_netMutex,portMAX_DELAY);g_wifiFound.swap(found);g_scanFinishedAt=nowMs();
                bool success=count>=0&&g_wifiScanStatus==0&&!scanStopping;
                g_scanState=scanCancelled?"cancelled":success?"complete":"failed";
                g_scanMessage=scanCancelled?"Scan cancelled to join network":success?String(g_wifiFound.size())+" networks found; select one to join":"Wi-Fi scan failed; please retry";xSemaphoreGive(g_netMutex);
                if(!hasJoin)resumeReconnect();
            }
        }
        if(hasJoin&&!scanning){
            WiFi.setAutoReconnect(true);WiFi.disconnect(false,false);
            desiredSsid=pendingJoin.ssid;joinStartedAt=nowMs();joinedOnce=false;
            if(pendingJoin.ssid[0])WiFi.begin(pendingJoin.ssid,pendingJoin.password);
            event("network",pendingJoin.ssid[0] ? "Station connection requested; AP stays available" : "Station disabled; AP stays available");
            memset(pendingJoin.password,0,sizeof(pendingJoin.password));hasJoin=false;
        }
        if(connected!=wasConnected){event("network",connected ? "Station connected" : "Station disconnected");wasConnected=connected;}
    }
}
void ruleTask(void*) {
    Sample s;
    for(;;) {
        if(xQueueReceive(g_samples,&s,portMAX_DELAY)!=pdTRUE)continue;
        std::vector<Rule> fired;
        xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
        if(s.meter<ut_meter::METER_SLOTS&&s.channel<8){g_lastSamples[s.meter][s.channel]=s;g_haveLast[s.meter][s.channel]=true;}
        meter_rules::Reading reading;reading.meter=s.meter;reading.channel=s.channel;reading.unit=s.unit;reading.value=s.value;reading.numeric=s.numeric;reading.uptimeMs=s.uptimeMs;
        for(Rule &r:g_rules)if(meter_rules::evaluate(r.config,r.state,reading)){++r.fireCount;r.lastResult="queued";fired.push_back(r);}
        xSemaphoreGive(g_ruleMutex);
        for(const Rule &r:fired)if(!queueJob(r,s,false)) {
            xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
            for(Rule &live:g_rules)if(live.id==r.id&&live.revision==r.revision)live.lastResult="queue failed";
            xSemaphoreGive(g_ruleMutex);
        }
    }
}
String valueString(double value) { char text[32];snprintf(text,sizeof(text),"%.12g",value);return text; }
String uintString(uint64_t value) { char text[24];snprintf(text,sizeof(text),"%llu",static_cast<unsigned long long>(value));return text; }
void deliver(Job &job,int &code,String &result) {
    // Destinations on the device AP subnet are valid even without a station
    // uplink. Let the socket's route/connect result decide reachability.
    if(nowMs()<job.sample.uptimeMs||nowMs()-job.sample.uptimeMs>30000){result="Queued measurement expired after 30 seconds";return;}
    const Rule &r=job.rule;bool https=r.url.startsWith("https://");
    if(https&&!timestamp(nowMs())){result="HTTPS requires a synchronized NTP or browser clock";return;}
    WiFiClient plain;WiFiClientSecure secure;
    if(https) {
        if(r.caPem.length())secure.setCACert(r.caPem.c_str());
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        else secure.setCACertBundle(meter_ca_bundle);
#else
        else {result="HTTPS trust certificate is missing";return;}
#endif
        secure.setHandshakeTimeout(8);
    }
    HTTPClient http;http.setConnectTimeout(4000);http.setTimeout(5000);http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    String value=valueString(job.sample.value),unit=job.sample.unit,meter=String(job.sample.meter),time=job.timestampMs ? uintString(job.timestampMs) : "";
    String url=r.url;
    auto encoded=[](const String &v){return String(meter_rules::urlEncode(v.c_str()).c_str());};
    if(r.method=="GET")url+=(url.indexOf('?')>=0 ? "&" : "?")+String("value=")+encoded(value)+"&unit="+encoded(unit)+"&meter="+encoded(meter)+"&timestamp="+encoded(time);
    if(!(https ? http.begin(secure,url) : http.begin(plain,url))){result="Unable to initialize HTTP request";return;}
    http.addHeader("X-Meter-Delivery",g_bootId+"-"+String(r.id)+"-"+uintString(job.sample.uptimeMs));
    bool hasContentType=false;
    for(const auto &entry:r.headers) {
        String v=meter_rules::render(entry.second.c_str(),value.c_str(),unit.c_str(),meter.c_str(),time.c_str()).c_str();
        if(!meter_rules::validHeader(entry.first.c_str(),v.c_str())){http.end();result="Expanded header is invalid";return;}
        if(entry.first.equalsIgnoreCase("Content-Type"))hasContentType=true;
        http.addHeader(entry.first,v);
    }
    if(r.method=="GET")code=http.GET();
    else {
        String payload;
        if(r.body.length())payload=meter_rules::render(r.body.c_str(),value.c_str(),unit.c_str(),meter.c_str(),time.c_str()).c_str();
        else {PsramDocument d;d["value"]=job.sample.value;d["unit"]=unit;d["meter"]=job.sample.meter;if(job.timestampMs)d["timestamp"]=job.timestampMs;else d["timestamp"]=nullptr;d["manualTest"]=job.manual;serializeJson(d,payload);}
        if(!hasContentType)http.addHeader("Content-Type","application/json");
        code=http.POST(payload);
    }
    // Intentionally no retry and no redirect: a POST can succeed upstream even
    // if its response is lost. Response bodies are not retained or exposed.
    result=code>=200&&code<300 ? "Delivered" : code>0 ? "HTTP response was not 2xx" : "Network/TLS request failed";
    http.end();
}
void deliveryTask(void*) {
    for(;;) {
        Job *job=nullptr;if(xQueueReceive(g_jobs,&job,portMAX_DELAY)!=pdTRUE||!job)continue;
        bool exists=false;
        xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
        for(const Rule &r:g_rules)if(r.id==job->rule.id&&r.revision==job->rule.revision&&(r.config.enabled||job->manual))exists=true;
        xSemaphoreGive(g_ruleMutex);
        int code=0;String result="Cancelled after rule changed or was removed";
        if(exists)deliver(*job,code,result);
        bool success=code>=200&&code<300;
        xSemaphoreTake(g_ruleMutex,portMAX_DELAY);
        for(Rule &r:g_rules)if(r.id==job->rule.id&&r.revision==job->rule.revision){r.lastResult=result;r.lastCode=code;r.lastDeliveryMs=nowMs();++r.deliveryCount;}
        xSemaphoreGive(g_ruleMutex);
        event("delivery",result.c_str(),job->rule.id,success,code,&job->sample);
        delete job;
    }
}
}

bool begin(Provider &provider) {
    if(g_provider)return true;
    meter_storage::begin();
    g_lastSamples=static_cast<Sample(*)[8]>(heap_caps_calloc(ut_meter::METER_SLOTS,sizeof(*g_lastSamples),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!g_lastSamples)g_lastSamples=static_cast<Sample(*)[8]>(heap_caps_calloc(ut_meter::METER_SLOTS,sizeof(*g_lastSamples),MALLOC_CAP_8BIT));
    if(!g_lastSamples)return false;
    for(unsigned s=0;s<ut_meter::METER_SLOTS;++s)for(unsigned c=0;c<8;++c)new(&g_lastSamples[s][c]) Sample;
    g_capacity=2048;
    g_frames=static_cast<Frame*>(heap_caps_calloc(g_capacity,sizeof(Frame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!g_frames){g_capacity=256;g_frames=static_cast<Frame*>(heap_caps_calloc(g_capacity,sizeof(Frame),MALLOC_CAP_8BIT));}
    g_netMutex=xSemaphoreCreateMutex();g_ruleMutex=xSemaphoreCreateMutex();g_clockMutex=xSemaphoreCreateMutex();
    g_samples=xQueueCreate(kSampleDepth,sizeof(Sample));g_jobs=xQueueCreate(kDeliveryDepth,sizeof(Job*));g_wifiCommands=xQueueCreate(1,sizeof(WifiCommand));
    if(!g_frames||!g_netMutex||!g_ruleMutex||!g_clockMutex||!g_samples||!g_jobs||!g_wifiCommands)return false;
    const uint64_t rtcTime=meter_board::rtcUnixMs();if(rtcTime)applyClock(rtcTime,3);
    for(unsigned i=0;i<g_capacity;i++)new(&g_frames[i]) Frame;
    char id[17],boot[17];snprintf(id,sizeof(id),"%012llX",static_cast<unsigned long long>(ESP.getEfuseMac()&0xFFFFFFFFFFFFULL));
    snprintf(boot,sizeof(boot),"%08lX%08lX",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));
    g_deviceId=id;g_bootId=boot;
    g_prefsOpen=g_preferences.begin("meter-web",false);
    g_network.apSsid="Meter-"+g_deviceId.substring(6);
    g_network.apPassword=g_prefsOpen ? g_preferences.getString("apPass","") : "";
    if(g_network.apPassword.length()!=16) {
        const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
        String password;for(unsigned i=0;i<16;i++)password+=alphabet[esp_random()%(sizeof(alphabet)-1)];
        if(!g_prefsOpen||g_preferences.putString("apPass",password)!=password.length()) {
            Serial.println("[meter-web] AP password save failed (NVS unavailable/full); web startup aborted");
            event("configuration","AP password could not be saved; web startup aborted");
            return false;
        }
        g_network.apPassword=password;
    }
    g_network.apIp="192.168.4.1";
    if(g_prefsOpen)loadRules(g_preferences.getString("rules",""));
    g_provider=&provider;
    TaskHandle_t net=nullptr,web=nullptr,rules=nullptr,http=nullptr,files=nullptr;
    bool created=xTaskCreate(networkTask,"meter-net",4096,nullptr,1,&net)==pdPASS;
    created=created&&xTaskCreate(webTask,"meter-web",12288,nullptr,1,&web)==pdPASS;
    created=created&&xTaskCreate(ruleTask,"meter-rules",6144,nullptr,1,&rules)==pdPASS;
    created=created&&xTaskCreate(deliveryTask,"meter-http",10240,nullptr,1,&http)==pdPASS;
#if METER_STORAGE_ENABLED
    created=created&&xTaskCreate(fileTask,"meter-files",8192,nullptr,1,&files)==pdPASS;
#endif
    if(!created){if(net)vTaskDelete(net);if(web)vTaskDelete(web);if(rules)vTaskDelete(rules);if(http)vTaskDelete(http);if(files)vTaskDelete(files);g_provider=nullptr;return false;}
    event("device","Web dashboard ready; RAM feed is temporary, browser IndexedDB stores sessions");
    return true;
}
void tick() { meter_ntp::tick(WiFi.status()==WL_CONNECTED); }
void publish(const Sample &sample) {
    if(!g_provider||!g_frames)return;
    Frame f;f.reading=true;f.sample=sample;f.timestampMs=timestamp(sample.uptimeMs);
    appendFrame(f);
    if(xQueueSend(g_samples,&sample,0)!=pdTRUE){portENTER_CRITICAL(&g_feedLock);++g_sampleDrops;portEXIT_CRITICAL(&g_feedLock);}
}
void printDiagnostics() {
    wifi_config_t stationConfig{};const int stationResult=esp_wifi_get_config(WIFI_IF_STA,&stationConfig);
    Serial.printf("web_sta_config error=%d ssid=%.*s configured_ssid=%s\n",stationResult,int(strnlen(reinterpret_cast<const char*>(stationConfig.sta.ssid),sizeof(stationConfig.sta.ssid))),stationConfig.sta.ssid,network().staSsid.c_str());
    wifi_config_t apConfig{};const int apResult=esp_wifi_get_config(WIFI_IF_AP,&apConfig);
    Serial.printf("web_ap_config error=%d ssid=%.*s hidden=%u channel=%u station_status=%d\n",apResult,int(apConfig.ap.ssid_len),apConfig.ap.ssid,apConfig.ap.ssid_hidden,apConfig.ap.channel,int(WiFi.status()));
    wifi_mode_t mode=WIFI_MODE_NULL;uint8_t channel=0;wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
    const int modeResult=esp_wifi_get_mode(&mode),channelResult=esp_wifi_get_channel(&channel,&secondary);
    wifi_sta_list_t stations{};const int clientsResult=esp_wifi_ap_get_sta_list(&stations);
    uint8_t mac[6]={};const int macResult=esp_wifi_get_mac(WIFI_IF_AP,mac);
    Serial.printf("web_diag started=%u mode=%d mode_error=%d channel=%u channel_error=%d ap_clients=%d clients_error=%d mac_error=%d ap_mac=%02x:%02x:%02x:%02x:%02x:%02x actual_ap=%s heap=%u largest=%u\n",unsigned(g_started.load()),int(mode),modeResult,channel,channelResult,stations.num,clientsResult,macResult,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],WiFi.softAPIP().toString().c_str(),ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
}
NetworkInfo network() {
    if(!g_netMutex)return {};
    xSemaphoreTake(g_netMutex,portMAX_DELAY);NetworkInfo result=g_network;xSemaphoreGive(g_netMutex);return result;
}
}
