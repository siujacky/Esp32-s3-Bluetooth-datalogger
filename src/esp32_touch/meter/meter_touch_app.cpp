#include "meter_touch_app.h"
#include <meter_ui_config.h>
#include "../panel.h"
#include "meter_ble.h"
#include "meter_web.h"
#include "meter_calibration.h"
#include "meter_display.h"
#include "meter_value_settings.h"
#include "meter_value_view.h"
#include "meter_design.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ut_touch.h>
#include <wifi_qr.h>
#include <meter_calc.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>
#include <utility>
#include <meter_selection.h>
#include "meter_ntp.h"
#include "meter_board_devices.h"

namespace meter_touch {
namespace {
constexpr uint16_t BG=meter_design::modern?meter_design::BG:0x0841,FG=meter_design::modern?meter_design::FG:0xffff,
    MUTED=meter_design::modern?meter_design::MUTED:0xa554,GREEN=meter_design::modern?meter_design::ACCENT:0x5f2c,
    WARN=meter_design::modern?meter_design::WARN:0xfd20,BUTTON=meter_design::modern?meter_design::SURFACE:0x2127;
constexpr unsigned REQUEST_LIMIT=8192,REPLY_LIMIT=98304,QUEUE_DEPTH=3,ROW_LIMIT=96;
enum class Page {Home,Quick,Network,Recording,Card,File,Rules,Rule,Calculations,CalcSettings,Scope,Values,Events,Diagnostics,Help,Web,Text,Qr,Confirm,Keyboard,Connections,Time,Screen};
enum Action {NONE=0,NETWORK,RECORDING,CARD,RULES,CALCULATIONS,EVENTS,DIAGNOSTICS,HELP,WEB,
    WIFI_SCAN,WIFI_SSID,WIFI_PASSWORD,WIFI_JOIN,WIFI_DISCONNECT,WIFI_QR,RECORD_LABEL,RECORD_START,RECORD_STOP,RECORD_MARKER,RECORD_MARK,
    CARD_REFRESH,CARD_REMOUNT,CARD_FORMAT,FILE_QR,NEW_RULE,SAVE_RULE,TEST_RULE,DELETE_RULE,
    CALC_SETTINGS,CALC_START,CALC_PAUSE,CALC_RESET,CALC_WEB,REFRESH,CLOCK_EDIT,CLOCK_SET,SHOW_WEB,SHOW_SESSIONS,SHOW_SUMMARIES,
    CONFIRM_INPUT,CONFIRM_EXECUTE,KEY_LEFT,KEY_RIGHT,KEY_SPACE,KEY_LF,KEY_PREVIOUS,KEY_NEXT,KEY_BACKSPACE,KEY_DONE,
    SCOPE_MENU,SCOPE_METER,SCOPE_CHANNEL,SCOPE_WINDOW,SCOPE_CLEAR,SCOPE_SHOW,README_QR,RULE_USE_UNIT,FILE_PREVIOUS,FILE_NEXT,TOUCH_CALIBRATE,DISPLAY_COUNT,CHART_LAYOUT,AUTO_DIM,
    VALUES_MENU,FORMAT_SLOT,FORMAT_CHANNEL,FORMAT_DIGITS,FORMAT_DECIMALS,FORMAT_PADDING,FORMAT_SAVE,VALUE_SHOW,VALUE_LAYOUT,BT_MENU,BT_SLOT,BT_DEVICE,BT_PROFILE,BT_SCAN,BT_CONNECT,BT_DISCONNECT,NTP_MENU,NTP_ENABLED,NTP_SERVER,NTP_SAVE,NTP_SYNC,SCREEN_ROTATION,ALL_FUNCTIONS,IMU_ROTATION,IMU_ALIGNMENT};
class JsonAllocator:public ArduinoJson::Allocator {
public:
    void *allocate(size_t n) override {void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);return p?p:heap_caps_malloc(n,MALLOC_CAP_8BIT);}
    void deallocate(void *p) override {heap_caps_free(p);}
    void *reallocate(void *p,size_t n) override {if(!n){heap_caps_free(p);return nullptr;}if(!p)return allocate(n);void *next=heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);return next?next:heap_caps_realloc(p,n,MALLOC_CAP_8BIT);}
};
JsonAllocator touchAllocator;
struct Document:JsonDocument {Document():JsonDocument(&touchAllocator){}};
struct Request {uint32_t id=0;String method,path,body;};
struct Response {uint32_t id=0;int code=0;String message,payload;};
struct Cache {
    String values[5],message,response;
    uint32_t versions[5]={},job=0,statusAt=0;int code=0;bool online=false;
};
Cache sharedCache;
SemaphoreHandle_t cacheMutex=nullptr;
QueueHandle_t requests=nullptr;
QueueHandle_t responses=nullptr;
std::atomic<bool> active{false},wantScan{false},wantRules{false},wantFiles{false},wantEvents{false},refresh{false};
std::atomic<bool> apiValidated{false};
std::atomic<uint32_t> apiStatusAt{0};
uint32_t nextJob=0,lastJob=0,versions[5]={},statusAt=0,pendingSave=0,pendingFormatJob=0;
uint32_t editorGeneration=0,pendingSaveGeneration=0;
bool started=false,online=false;
unsigned outstanding=0;
Document statusDoc,scanDoc,rulesDoc,filesDoc,eventsDoc;
String notice="Open a function",wifiSsid,wifiPassword,recordLabel,markerLabel="Marker",clockText;
String fields[13],calcFields[8],formatConfirmation,pendingFormat,selectedFile,selectedFileBytes;
uint32_t ruleId=0;
Page page=Page::Home,previousPage=Page::Home,qrBack=Page::Home,textBack=Page::Home,confirmBack=Page::Home;
Page menuBack=Page::Home;bool valueMenu=false,liveMenu=false;
int scroll=0,previousScroll=0,confirmKind=0;
String pageText,qrText,qrCaption,qrHint;
String *editTarget=nullptr,editText,editCaption;
unsigned editMaximum=0,editCursor=0,keyPage=0;
bool editSecret=false;
struct Row {
    String title,detail;int action;bool enabled,info;uint16_t color;
    bool numeric=false;double value=0;meter_ui::NumberFormat format;
    Row(const String &t,const String &d,int a,bool e,bool i,uint16_t c):title(t),detail(d),action(a),enabled(e),info(i),color(c){}
};
std::vector<Row> rows;
unsigned formatSlot=0,formatChannel=0;meter_ui::NumberFormat editedFormat;bool formatDirty=false;
meter_ui::NumberFormat formatDrafts[meter_ble::MAX_CONNECTIONS][8];
bool formatDraftDirty[meter_ble::MAX_CONNECTIONS][8]={},formatLoaded=false;
struct Option {String label;int value;Option(const String &l,int v):label(l),value(v){}};
std::vector<Option> options;std::vector<meter_ble::Candidate> dropdownDevices;
meter_ui::SelectionState dropdown;meter_ui::SelectionLayout dropdownLayout;
String dropdownTitle;int dropdownAction=NONE,dropdownReturn=-1;unsigned dropdownSlot=0;
meter_ble::Candidate selectedDevices[meter_ble::MAX_CONNECTIONS];bool haveSelectedDevice[meter_ble::MAX_CONNECTIONS]={};
String selectedProfiles[meter_ble::MAX_CONNECTIONS];bool ntpEnabled=true,ntpDirty=false;String ntpServer="pool.ntp.org";
void closeDropdown();void openChoice(int target);void chooseOption(unsigned visibleIndex);

const meter_ui::Map configuredUi=meter_ui::configuredMap();
meter_ui::Map ui=configuredUi;
struct Layout {
    int left=0,width=0,top=0,bottom=0,step=1,headerBottom=0;
    meter_ui::Transform tx{meter_ui::view(ui.advanced),ui.advanced.reference_width,ui.advanced.reference_height};
    meter_ui::Rect back{};MeterTabs tabs{{0,0,0,0}};
    Layout()=default;
    void resolve(int w,int h){
        ui=nativeMenuMap(configuredUi,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,meter_display::rotation());
        tx=meter_ui::Transform(meter_ui::view(ui.advanced),w,h);
        const auto area=tx.rect(ui.advanced.viewport);left=area.x;width=area.w;top=area.y;bottom=area.y+area.h;step=tx.yLength(ui.advanced.row.step);
        headerBottom=top;back=tx.rect(ui.advanced.back);const meter_ui::Rect largerBack{back.x,back.y,std::max(44,back.w),std::max(44,back.h)};
        if(largerBack.y+largerBack.h<=top&&tx.safe(largerBack))back=largerBack;
        tabs=MeterTabs({left,top,width,bottom-top});
        if(page==Page::Connections||page==Page::Values)top+=tabs.height;else tabs=MeterTabs({0,0,0,0});
    }
    meter_ui::Rect viewport()const{return {left,top,width,bottom-top};}
    meter_ui::RowLayout rowLayout()const{return {viewport(),step,tx.yLength(ui.advanced.row.gap)};}
    bool homeGrid()const{return meter_design::modern&&page==Page::Home&&!ui.advanced.safe_circle&&ui.advanced.home_grid.height>=80;}
    meter_ui::GridLayout gridLayout()const{
        const auto &spec=ui.advanced.home_grid;
        return {viewport(),spec.columns,tx.xLength(spec.column_gap),tx.yLength(spec.step),tx.yLength(spec.height)};
    }
    int contentHeight(int count)const{return homeGrid()?gridLayout().contentHeight(count):rowLayout().contentHeight(count);}
    meter_ui::Rect rect(int index,int offset)const{return homeGrid()?gridLayout().rect(index,offset):rowLayout().rect(index,offset);}
    meter_ui::Rect visible(int index,int offset)const{return homeGrid()?gridLayout().visible(index,offset):rowLayout().visible(index,offset);}
    int indexAt(int x,int y,int offset,int count)const{return homeGrid()?gridLayout().indexAt(x,y,offset,count):rowLayout().indexAt(x,y,offset,count);}
    NativeKeyboard keyboard()const{return {viewport(),meter_ui::scaledKeyboard(ui.advanced.keyboard,tx)};}
};
Layout layout;
ut_meter::TouchGesture gesture;
meter_calc::Battery battery;
meter_calc::Config calcConfig;
bool calcDirty=false;
bool scopeRequested=false,clearScopeRequested=false,valuesRequested=false;
bool redraw=true,dropBaseline=false;
uint32_t previousDrops[2]={},lastPacket[meter_ble::MAX_CONNECTIONS]={},packetEpoch[meter_ble::MAX_CONNECTIONS]={};
uint64_t packetSource[meter_ble::MAX_CONNECTIONS]={};
bool pendingLoss[meter_ble::MAX_CONNECTIONS]={};
unsigned scopeSeconds=60;
unsigned filePage=0;
int scopeChannels[meter_ble::MAX_CONNECTIONS]={-1,-1,-1,-1};
unsigned voltageMeter=0,voltageChannel=0,currentMeter=1,currentChannel=0;
uint64_t bootSource=0;
struct StoredReading {double value=0;char unit[12]={},mode[40]={},address[18]={},profile[32]={};uint32_t flags=0,at=0,modeKey=0,channelPacket=0;uint64_t time=0,seq=0,source=0;bool numeric=false,have=false;};
StoredReading (*readings)[8]=nullptr;

String number(double value,unsigned digits=6){if(!std::isfinite(value))return "--";char b[40];snprintf(b,sizeof(b),"%.*g",int(digits),value);return b;}
String asText(JsonVariantConst value,const char *fallback=""){return value.is<const char*>()?String(value.as<const char*>()):String(fallback);}
String json(JsonDocument &d){String out;serializeJson(d,out);return out;}
bool validStatus(JsonDocument &d){return d.is<JsonObject>()&&d["device"].is<JsonObject>()&&d["device"]["id"].is<const char*>()&&d["device"]["uptimeMs"].is<uint64_t>()&&d["recording"].is<JsonObject>()&&d["recording"]["active"].is<bool>()&&d["storage"].is<JsonObject>()&&d["storage"]["supported"].is<bool>()&&d["storage"]["mounted"].is<bool>()&&d["network"].is<JsonObject>()&&d["network"]["apIp"].is<const char*>()&&d["meters"].is<JsonArray>();}
bool statusFresh(){return online&&apiValidated.load()&&uint32_t(millis()-statusAt)<4000;}
void message(const String &value){notice=value;}
void publish(unsigned index,const String &value){xSemaphoreTake(cacheMutex,portMAX_DELAY);sharedCache.values[index]=value;++sharedCache.versions[index];if(index==0){sharedCache.statusAt=millis();sharedCache.online=true;}xSemaphoreGive(cacheMutex);}
class LimitedOutput:public Stream {
public:
    String value;bool overflow=false;
    int available() override{return 0;}int read() override{return -1;}int peek() override{return -1;}void flush() override{}
    size_t write(uint8_t byte) override{return write(&byte,1);}
    size_t write(const uint8_t *bytes,size_t count) override {
        if(count>REPLY_LIMIT-value.length()){overflow=true;return 0;}
        return value.concat(reinterpret_cast<const char*>(bytes),count)?count:0;
    }
};
bool requestHttp(const String &method,const String &path,const String &payload,String &reply,int &code){
    const auto network=meter_web::network();
    if(!network.apIp.length()){reply="Device AP is not ready";code=0;return false;}
    WiFiClient client;HTTPClient http;
    http.setConnectTimeout(1000);http.setTimeout(1600);http.setReuse(false);http.useHTTP10(true);
    if(!http.begin(client,"http://"+network.apIp+path)){reply="Local API connection could not start";code=0;return false;}
    http.addHeader("Accept","application/json");
    if(method=="POST")http.addHeader("Content-Type","application/json");
    code=method=="GET"?http.GET():http.sendRequest(method.c_str(),payload);
    if(code<=0){reply=HTTPClient::errorToString(code);http.end();return false;}
    if(http.getSize()>int(REPLY_LIMIT)){reply="Local API response exceeds native UI limit";http.end();return false;}
    LimitedOutput output;const int copied=http.writeToStream(&output);http.end();
    if(copied<0||output.overflow){reply="Local API response was interrupted or too large";return false;}
    reply=output.value;return code>=200&&code<300;
}
void fetch(unsigned index,const String &path){
    String reply;int code=0;
    if(requestHttp("GET",path,"",reply,code)){
        if(index==0){Document candidate;auto parseError=deserializeJson(candidate,reply,DeserializationOption::NestingLimit(10));if(parseError||!validStatus(candidate)){unsigned fields=0;const bool checks[]={candidate["device"].is<JsonObject>(),candidate["device"]["id"].is<const char*>(),candidate["device"]["uptimeMs"].is<uint64_t>(),candidate["recording"]["active"].is<bool>(),candidate["storage"]["supported"].is<bool>(),candidate["storage"]["mounted"].is<bool>(),candidate["network"]["apIp"].is<const char*>(),candidate["meters"].is<JsonArray>()};for(unsigned i=0;i<8;++i)if(checks[i])fields|=1u<<i;reply=String("Invalid device status: ")+parseError.c_str()+" fields="+String(fields,HEX)+" bytes="+String(reply.length());}else {apiStatusAt.store(millis());apiValidated.store(true);publish(index,reply);return;}}
        else {publish(index,reply);return;}
    }
    if(index==0){apiValidated.store(false);xSemaphoreTake(cacheMutex,portMAX_DELAY);sharedCache.online=false;sharedCache.message="API: "+reply.substring(0,120);xSemaphoreGive(cacheMutex);}
}
void worker(void*){
    uint32_t statusDue=0,scanDue=0,rulesDue=0,filesDue=0,eventsDue=0;uint64_t eventCursor=0;
    String eventBoot;Document events;events["events"].to<JsonArray>();
    for(;;){
        Request *job=nullptr;
        if(xQueueReceive(requests,&job,pdMS_TO_TICKS(40))==pdTRUE&&job){
            String reply;int code=0;const bool success=requestHttp(job->method,job->path,job->body,reply,code);
            Document d;String result;
            if(!deserializeJson(d,reply))result=asText(d["error"]);
            if(!result.length())result=success?"Request accepted; checking status":reply.substring(0,120);
            xSemaphoreTake(cacheMutex,portMAX_DELAY);sharedCache.job=job->id;sharedCache.code=success?code:-abs(code);sharedCache.response=reply;sharedCache.message=result;xSemaphoreGive(cacheMutex);
            auto *response=new(std::nothrow) Response;
            if(response){response->id=job->id;response->code=success?code:-abs(code);response->message=result;response->payload=reply.substring(0,2048);if(xQueueSend(responses,&response,0)!=pdTRUE)delete response;}
            delete job;statusDue=scanDue=rulesDue=filesDue=eventsDue=0;
        }
        if(!active.load())continue;
        if(refresh.exchange(false))statusDue=scanDue=rulesDue=filesDue=eventsDue=0;
        const uint32_t now=millis();
        if(int32_t(now-statusDue)>=0){fetch(0,"/api/status?compact=1");statusDue=millis()+900;}
        if(wantScan.load()&&int32_t(now-scanDue)>=0){fetch(1,"/api/wifi/scan");scanDue=millis()+1200;}
        if(wantRules.load()&&int32_t(now-rulesDue)>=0){fetch(2,"/api/rules");rulesDue=millis()+2500;}
        if(wantFiles.load()&&int32_t(now-filesDue)>=0){fetch(3,"/api/storage/files");filesDue=millis()+2500;}
        if(wantEvents.load()&&int32_t(now-eventsDue)>=0){
            char cursor[24];snprintf(cursor,sizeof(cursor),"%llu",static_cast<unsigned long long>(eventCursor));
            String reply;int code=0;
            if(requestHttp("GET",String("/api/frames?after=")+cursor+"&limit=64","",reply,code)){
                Document feed;
                if(!deserializeJson(feed,reply)){
                    String boot=asText(feed["bootId"]);
                    if(eventBoot.length()&&eventBoot!=boot){events.clear();events["events"].to<JsonArray>();eventCursor=0;}
                    eventBoot=boot;auto list=events["events"].as<JsonArray>();
                    for(JsonObjectConst frame:feed["frames"].as<JsonArrayConst>())if(asText(frame["kind"])=="event"){
                        if(list.size()>=24)list.remove(0);
                        list.add(frame);
                    }
                    events["gap"]=feed["gap"]|false;events["nextSeq"]=feed["nextSeq"];
                    eventCursor=feed["nextSeq"]|uint64_t(0);publish(4,json(events));
                }
            }
            eventsDue=millis()+700;
        }
    }
}
uint32_t queue(const char *method,const String &path,const String &payload=""){
    if(!started){message("Native API worker is unavailable");return 0;}
    if(!apiValidated.load()||uint32_t(millis()-apiStatusAt.load())>=4000){refresh.store(true);message("Wait for valid current device status before sending an action");return 0;}
    if(outstanding>=QUEUE_DEPTH+1){message("Wait for previous action results before sending another");return 0;}
    if(payload.length()>REQUEST_LIMIT){message("Request exceeds 8192 bytes; shorten the fields");return 0;}
    auto *job=new(std::nothrow) Request;
    if(!job){message("Not enough memory to queue action");return 0;}
    job->id=++nextJob;job->method=method;job->path=path;job->body=payload;
    const uint32_t id=job->id;
    if(job->body.length()!=payload.length()||xQueueSend(requests,&job,0)!=pdTRUE){delete job;message("Action queue is full; nothing was sent");return 0;}
    ++outstanding;message("Action queued; wait for result");return id;
}
void applyResponse(uint32_t job,int code,const String &result,const String &messageText){
    if(job<=lastJob)return;
    lastJob=job;if(outstanding)--outstanding;redraw=true;
    message((code>0?String("OK: "):String("FAILED: "))+messageText);
    if(job==pendingSave){if(code>0&&pendingSaveGeneration==editorGeneration){Document reply;if(!deserializeJson(reply,result))ruleId=reply["id"]|ruleId;}pendingSave=0;}
    if(job==pendingFormatJob&&code<=-400&&code>=-599){pendingFormat="";pendingFormatJob=0;}
}
void setPage(Page next){
    page=next;scroll=0;gesture=ut_meter::TouchGesture();
    const bool sideways=meter_display::rotation()%2;layout.resolve(sideways?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH,sideways?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT);
    wantScan.store(next==Page::Network);wantRules.store(next==Page::Rules||next==Page::Rule);
    wantFiles.store(next==Page::Card||next==Page::File);wantEvents.store(next==Page::Events);
    refresh.store(true);
}
void goBack(){
    if(dropdown.isOpen()){closeDropdown();return;}
    if(page==Page::Keyboard){setPage(previousPage);scroll=previousScroll;editTarget=nullptr;return;}
    if(liveMenu&&page==menuBack){active.store(false);liveMenu=false;return;}
    if(page==Page::Quick){active.store(false);valuesRequested=true;return;}
    if(page==Page::Home){if(valueMenu){menuBack=Page::Quick;setPage(Page::Quick);}else active.store(false);return;}
    if(page==Page::Qr){setPage(qrBack);return;}
    if(page==Page::Text){setPage(textBack);return;}
    if(page==Page::Confirm){formatConfirmation="";setPage(confirmBack);return;}
    if(page==Page::Rule){setPage(Page::Rules);return;}
    if(page==Page::File){setPage(Page::Card);return;}
    if(page==Page::CalcSettings){setPage(Page::Calculations);return;}
    setPage(menuBack);
}
void rememberFormatDraft(){if(formatLoaded){formatDrafts[formatSlot][formatChannel]=editedFormat;formatDraftDirty[formatSlot][formatChannel]=formatDirty;}}
void selectFormatContext(unsigned slot,unsigned channel){
    if(slot>=meter_ble::MAX_CONNECTIONS||channel>=8)return;
    rememberFormatDraft();formatSlot=slot;formatChannel=channel;
    formatDirty=formatDraftDirty[slot][channel];editedFormat=formatDirty?formatDrafts[slot][channel]:meter_values::snapshot().formats[slot][channel];formatLoaded=true;
}
void showValueSettings(){
    const unsigned slot=meter_ble::activeSlot();selectFormatContext(slot,meter_ble::displaySnapshot(slot,scopeChannel(slot)).channel);
    setPage(Page::Values);message("Select a meter tab; preview then Save number format");
}
void editor(String &value,const String &caption,unsigned maximum,bool secret=false){
    previousPage=page;previousScroll=scroll;editTarget=&value;editText=value;editCaption=caption;editMaximum=maximum;editSecret=secret;editCursor=editText.length();keyPage=0;setPage(Page::Keyboard);
}
void insert(char character){
    if(!canInsert(editText.length(),editMaximum)){message("Field is at its length limit");return;}
    String next=editText.substring(0,editCursor)+String(character)+editText.substring(editCursor);
    if(next.length()!=editText.length()+1){message("Text allocation failed; value unchanged");return;}
    editText=next;++editCursor;
}
void add(const String &title,const String &detail="",int action=NONE,bool enabled=true,uint16_t color=FG){
    if(rows.size()<ROW_LIMIT)rows.push_back({title,detail,action,enabled,false,color});
}
String displayText(const String &value);
void info(const String &value,uint16_t color=MUTED){
    const auto &spec=ui.advanced.row;const int size=std::max(1,layout.tx.font(spec.info_size)),padding=layout.tx.xLength(spec.padding),titleY=layout.tx.yLength(spec.title_y);
    const unsigned columns=std::max(1,(layout.width-2*padding)/(6*size)),lines=std::max(1,(layout.rect(0,0).h-titleY-layout.tx.yLength(4))/(8*size));
    const String shown=displayText(value);unsigned offset=0;
    while(offset<shown.length()&&rows.size()<ROW_LIMIT){
        const unsigned start=offset;
        for(unsigned line=0;line<lines&&offset<shown.length();++line){unsigned count=std::min<unsigned>(columns,shown.length()-offset);const int newline=shown.indexOf('\n',offset);if(newline>=int(offset)&&newline<int(offset+count))count=newline-offset;offset+=count;if(offset<shown.length()&&shown[offset]=='\n')++offset;}
        rows.push_back({shown.substring(start,offset),"",NONE,false,true,color});
    }
}
bool finiteInput(const String &text,double &result){
    String value=text;value.trim();if(!value.length())return false;
    char *end=nullptr;result=strtod(value.c_str(),&end);return end&&*end==0&&std::isfinite(result);
}
bool calcSettings(){
    double pf=1,capacity=0,soc=-1;
    if(calcFields[0].length()&&(!finiteInput(calcFields[0],pf)||pf<0||pf>1)){message("Power factor must be 0..1 or empty");return false;}
    if(calcFields[1].length()!=0||calcFields[2].length()!=0){
        if(!finiteInput(calcFields[1],capacity)||capacity<=0||!finiteInput(calcFields[2],soc)||soc<0||soc>100){message("Enter positive capacity and SOC 0..100, or leave both empty");return false;}
    }
    calcConfig.powerFactorSet=calcFields[0].length()>0;calcConfig.powerFactor=pf;calcConfig.capacityAh=capacity;calcConfig.initialSocPercent=soc;return true;
}
void calcChanged(){battery.pause();if(battery.summary().samples)calcDirty=true;message(calcDirty?"Settings changed. Reset the test before Start":"Calculation settings changed");}
meter_calc::Reading calcReading(unsigned meter,unsigned channel,uint32_t *dropped=nullptr,bool integration=false){
    if(!readings||meter>=meter_ble::MAX_CONNECTIONS||channel>=8)return {};
    const auto &stored=readings[meter][channel];meter_calc::Reading out;
    out.value=stored.value;out.unit=stored.unit;out.mode=stored.mode;out.numeric=stored.numeric;
    const auto connection=meter_ble::connection(meter);if(dropped)*dropped=connection.dropped;
    out.ageMs=millis()-stored.at;out.connected=connection.state==meter_ble::State::Connected;
    const bool cursorMatches=stored.channelPacket>0&&stored.seq<=connection.packets&&(integration?stored.channelPacket<=connection.channelPackets[channel]:stored.channelPacket==connection.channelPackets[channel]);
    out.fresh=stored.have&&connection.hasReading[channel]&&cursorMatches&&out.connected&&out.ageMs<3000&&!strcmp(stored.address,connection.peer.address)&&!strcmp(stored.profile,connection.profile)&&connection.readings[channel].modeKey==stored.modeKey;out.flags=stored.flags;
    out.timeMs=stored.time;out.seq=stored.channelPacket;out.source=stored.source;return out;
}
void updateBattery(){
    if(!battery.summary().running)return;
    uint32_t drops[2]={};auto voltage=calcReading(voltageMeter,voltageChannel,&drops[0],true),current=calcReading(currentMeter,currentChannel,&drops[1],true);
    const bool gap=pendingLoss[voltageMeter]||pendingLoss[currentMeter]||(dropBaseline&&(drops[0]>previousDrops[0]||drops[1]>previousDrops[1]));
    previousDrops[0]=drops[0];previousDrops[1]=drops[1];dropBaseline=true;pendingLoss[voltageMeter]=pendingLoss[currentMeter]=false;
    battery.update(voltage,current,calcConfig,uint64_t(esp_timer_get_time()/1000),gap);
}
String sourceLabel(unsigned meter,unsigned channel){if(!readings||meter>=meter_ble::MAX_CONNECTIONS||channel>=8)return "Source unavailable";const auto &r=readings[meter][channel];return String("M")+String(meter+1)+" / CH "+String(channel+1)+(r.have?"  "+number(r.value)+" "+r.unit:"  waiting");}
void newRule(){
    ++editorGeneration;pendingSave=0;
    ruleId=0;fields[0]="on";fields[1]="0";fields[2]="0";fields[3]="V";fields[4]="above";fields[5]="0";fields[6]="0";fields[7]="1";fields[8]="POST";fields[9]="http://";fields[10]="";fields[11]="{}";fields[12]="";setPage(Page::Rule);
}
void loadRule(JsonObjectConst rule){
    ++editorGeneration;pendingSave=0;
    ruleId=rule["id"]|uint32_t(0);fields[0]=(rule["enabled"]|false)?"on":"off";
    fields[1]=String(rule["meter"]|0);fields[2]=String(rule["channel"]|0);fields[3]=asText(rule["unit"]);
    fields[4]=asText(rule["operator"],"above");fields[5]=number(rule["threshold"]|0.0,12);fields[6]=number(rule["hysteresis"]|0.0,12);
    fields[7]=number((rule["cooldownMs"]|1000)/1000.0,10);fields[8]=asText(rule["method"],"POST");fields[9]=asText(rule["url"]);
    fields[10]=asText(rule["body"]);fields[11]="";serializeJson(rule["headers"],fields[11]);if(fields[11]=="null")fields[11]="{}";fields[12]=asText(rule["caPem"]);setPage(Page::Rule);
}
void saveRule(){
    if(pendingSave){message("Wait for this Save result before saving again");return;}
    double threshold,hysteresis,cooldown;
    if(!finiteInput(fields[5],threshold)||!finiteInput(fields[6],hysteresis)||hysteresis<0||!finiteInput(fields[7],cooldown)||cooldown<0||cooldown>86400){message("Check threshold, nonnegative hysteresis and cooldown 0..86400s");return;}
    Document headers;if(deserializeJson(headers,fields[11])||!headers.is<JsonObject>()||headers.size()>8){message("Headers must be a JSON object with up to 8 string values");return;}
    for(JsonPairConst p:headers.as<JsonObjectConst>())if(!p.value().is<const char*>()){message("Every header value must be a string");return;}
    Document d;if(ruleId)d["id"]=ruleId;d["enabled"]=fields[0]=="on";d["meter"]=fields[1].toInt();d["channel"]=fields[2].toInt();
    d["unit"]=fields[3];d["operator"]=fields[4];d["threshold"]=threshold;d["hysteresis"]=hysteresis;d["cooldownMs"]=uint32_t(std::llround(cooldown*1000));
    d["method"]=fields[8];d["url"]=fields[9];d["body"]=fields[10];d["headers"]=headers.as<JsonObjectConst>();d["caPem"]=fields[12];
    if(d.overflowed()){message("Not enough memory for trigger fields");return;}
    pendingSave=queue("POST","/api/rules",json(d));if(pendingSave)pendingSaveGeneration=editorGeneration;
}
bool cardBusy(){JsonObjectConst card=statusDoc["storage"].as<JsonObjectConst>();return (statusDoc["recording"]["active"]|false)||(card["recording"]|false)||(card["formatting"]|false)||(card["remounting"]|false)||(card["queueDepth"]|0)>0||(card["bufferedRecords"]|0)>0||(card["downloads"]|0)>0;}
bool formatOutstanding(){
    if(!pendingFormat.length())return false;
    JsonObjectConst card=statusDoc["storage"].as<JsonObjectConst>();const String result=asText(card["formatResult"]);
    return asText(card["formatOperationId"])!=pendingFormat||(result!="success"&&result!="failed");
}
void recording(const char *action,const String &label){Document d;d["action"]=action;d["label"]=label;queue("POST","/api/recording",json(d));}
String encodeUrl(const String &input){const char *hex="0123456789ABCDEF";String out;for(unsigned i=0;i<input.length();++i){uint8_t c=input[i];if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
void showQr(const String &value,const String &caption,const String &hint){qrBack=page;qrText=value;qrCaption=caption;qrHint=hint;setPage(Page::Qr);}
void showText(const String &value){textBack=page;pageText=value;setPage(Page::Text);}
void closeDropdown(){dropdown.cancel();options.clear();dropdownDevices.clear();gesture=ut_meter::TouchGesture();redraw=true;const int back=dropdownReturn;dropdownReturn=-1;if(back>=0){active.store(false);if(back==1)valuesRequested=true;else if(back==2)scopeRequested=true;}}
void openChoice(int target){
 options.clear();dropdownDevices.clear();dropdownAction=target;dropdownTitle="Select";dropdownSlot=meter_ble::activeSlot();int current=-1;
 auto option=[&](const String &label,int value){options.emplace_back(label,value);};
 auto meters=[&](unsigned value){current=int(value);for(unsigned i=0;i<meter_ble::MAX_CONNECTIONS;++i){auto m=meter_ble::connection(i);option(String("Meter ")+String(i+1)+(m.peer.name[0]?String(" / ")+m.peer.name:String(" / ")+m.profile),i);}};
 auto channels=[&](unsigned value){current=int(value);for(unsigned i=0;i<8;++i)option(String("Channel ")+String(i+1),i);};
 switch(target){
 case SCOPE_METER:case BT_SLOT:dropdownTitle="Meter";meters(meter_ble::activeSlot());break;
 case FORMAT_SLOT:dropdownTitle="Format meter";meters(formatSlot);break;
 case DISPLAY_COUNT:dropdownTitle="Meters shown";current=meter_display::count()-1;for(unsigned i=1;i<=4;++i)option(String(i)+" meters",i);break;
 case SCREEN_ROTATION:
  dropdownTitle="Screen rotation";current=meter_display::rotation();
#ifdef UT_METER_LCD349
  current=meter_board::autoRotation()?0:int(meter_display::rotation())+1;
  option("Automatic (IMU)",4);
  for(unsigned i=0;i<4;++i)option(String(i*90)+" degrees (manual)",i);
#else
  for(unsigned i=0;i<4;++i)option(String(i*90)+" degrees",i);
#endif
  break;
#ifdef UT_METER_LCD349
 case IMU_ALIGNMENT:
  dropdownTitle="IMU alignment";current=int(meter_board::imuRotationOffset());
  option("0 degrees (none)",0);option("90 degrees clockwise",1);
  option("180 degrees",2);option("90 degrees counterclockwise",3);break;
#endif
 case VALUE_LAYOUT:{dropdownTitle="Value layout";auto prefs=meter_values::snapshot();current=int(prefs.presets[meter_display::count()-1]);const char *names[]={"Solo / 1 meter","Equal / 2 or 3 meters","1 large + 1 small","1 large + 2 small","2 large + 1 small","Grid / 4 meters"};for(int i=0;i<6;++i)option(names[i],i);break;}
 case SCOPE_CHANNEL:dropdownTitle="Display channel";current=scopeChannels[meter_ble::activeSlot()]+1;option("Auto / current channel",-1);for(unsigned i=0;i<8;++i)option(String("Channel ")+String(i+1),i);break;
 case FORMAT_CHANNEL:dropdownTitle="Format channel";channels(formatChannel);break;
 case FORMAT_DIGITS:dropdownTitle="Total digits";current=editedFormat.digits-3;for(int i=3;i<=8;++i)option(String(i),i);break;
 case FORMAT_DECIMALS:dropdownTitle="Decimal places";current=editedFormat.decimals;for(unsigned i=0;i<=std::min(6u,unsigned(editedFormat.digits-1));++i)option(String(i),i);break;
 case FORMAT_PADDING:dropdownTitle="Leading positions";current=editedFormat.padding==meter_ui::Padding::Blank?0:1;option("Spaces",0);option("Zeros",1);break;
 case CHART_LAYOUT:dropdownTitle="Chart layout";current=meter_display::overlay()?0:1;option("Overlay / same chart",1);option("Separate charts",0);break;
 case SCOPE_WINDOW:{dropdownTitle="Time window";const unsigned seconds[]={5,10,30,60,120,300};for(unsigned i=0;i<6;++i){option(String(seconds[i])+" seconds",seconds[i]);if(seconds[i]==scopeSeconds)current=i;}break;}
 case BT_PROFILE:dropdownTitle="Protocol profile";option("Auto detect",-1);current=0;for(size_t i=0;i<dmm::profileCount();++i){const auto *profile=dmm::profileAt(i);option(String(profile->name)+(profile->experimental?" [experimental]":""),i);if(selectedProfiles[dropdownSlot]==profile->id)current=i+1;}break;
 case BT_DEVICE:{dropdownTitle="Bluetooth device";auto v=meter_ble::view();for(unsigned i=0;i<v.count;++i){dropdownDevices.push_back(v.meters[i]);option(String(v.meters[i].name)+" / "+v.meters[i].address,i);if(haveSelectedDevice[dropdownSlot]&&!strcmp(selectedDevices[dropdownSlot].address,v.meters[i].address)&&selectedDevices[dropdownSlot].addressType==v.meters[i].addressType)current=i;}break;}
 case NTP_ENABLED:dropdownTitle="NTP automatic time";current=ntpEnabled?1:0;option("Disabled",0);option("Enabled",1);break;
 case NTP_SERVER:{dropdownTitle="NTP server";const char *servers[]={"pool.ntp.org","time.cloudflare.com","time.google.com"};current=3;for(int i=0;i<3;++i){option(servers[i],i);if(ntpServer==servers[i])current=i;}option("Custom server...",3);break;}
 default:
  if(target==4001){dropdownTitle="Trigger meter";meters(fields[1].toInt());}
  else if(target==4002){dropdownTitle="Trigger channel";channels(fields[2].toInt());}
  else if(target==4000){dropdownTitle="Trigger enabled";current=fields[0]=="on"?1:0;option("Off",0);option("On",1);}
  else if(target==4004){dropdownTitle="Threshold condition";current=fields[4]=="below"?1:0;option("Above",0);option("Below",1);}
  else if(target==4008){dropdownTitle="API method";current=fields[8]=="GET"?1:0;option("POST",0);option("GET",1);}
  else if(target==5000){dropdownTitle="Voltage meter";meters(voltageMeter);}
  else if(target==5001){dropdownTitle="Voltage channel";channels(voltageChannel);}
  else if(target==5002){dropdownTitle="Current meter";meters(currentMeter);}
  else if(target==5003){dropdownTitle="Current channel";channels(currentChannel);}
  else if(target==5004){dropdownTitle="Calculation mode";current=calcConfig.ac?1:0;option("DC",0);option("AC",1);}
  else if(target==5005){dropdownTitle="Discharge direction";current=calcConfig.negativeDischarge?1:0;option("Positive current",0);option("Negative current",1);}
  break;
 }
 if(options.empty()){message("No choices available; scan for Bluetooth devices first");return;}
 const bool round=layout.tx.reference.safe_circle;const meter_ui::Rect area=round?layout.viewport():meter_ui::Rect{8,8,layout.tx.width-16,layout.tx.height-16};
 const bool visual=target==VALUE_LAYOUT||target==CHART_LAYOUT;dropdownLayout=swipeSelectionLayout(area,options.size(),visual);if(!dropdownLayout.valid||!dropdown.begin(options.size(),current,dropdownLayout.visible,dropdownLayout.rowHeight,dropdownLayout.columns)){message("Selection list cannot fit the screen");return;}gesture=ut_meter::TouchGesture();redraw=true;
}
void chooseOption(unsigned visibleIndex){
 int index=-1;if(!dropdown.chooseVisible(visibleIndex,index)||index<0||unsigned(index)>=options.size())return;
 const int value=options[index].value,target=dropdownAction;meter_ble::Candidate candidate;if(target==BT_DEVICE&&value>=0&&unsigned(value)<dropdownDevices.size())candidate=dropdownDevices[value];
 const int returnView=dropdownReturn;dropdownReturn=-1;closeDropdown();dropdownReturn=returnView;
 if(target==SCOPE_METER||target==BT_SLOT)meter_ble::focus(value);
 else if(target==DISPLAY_COUNT)meter_display::requestCount(value);
 else if(target==SCREEN_ROTATION){
  if(meter_calibration::active())message("Finish touch calibration before rotating");
#ifdef UT_METER_LCD349
  else if(value==4){meter_board::setAutoRotation(true);message("Automatic IMU rotation requested; hold upright and steady");}
#endif
  else if(!meter_display::requestRotation(unsigned(value)))message("Screen rotation request unavailable");
  else message("Manual rotation requested; check saved status");
 }
#ifdef UT_METER_LCD349
 else if(target==IMU_ALIGNMENT){
  if(!meter_board::setImuRotationOffset(unsigned(value)))message("IMU alignment unavailable");
  else message("IMU alignment requested; check saved status");
 }
#endif
 else if(target==VALUE_LAYOUT){if(meter_values::requestNativeLayout(meter_ui::ValuePreset(value))){dropdownReturn=1;}else message("Layout request unavailable");}
 else if(target==SCOPE_CHANNEL){scopeChannels[meter_ble::activeSlot()]=value;if(value>=0)meter_ble::selectChannel(meter_ble::activeSlot(),value);}
 else if(target==FORMAT_SLOT||target==FORMAT_CHANNEL){selectFormatContext(target==FORMAT_SLOT?unsigned(value):formatSlot,target==FORMAT_CHANNEL?unsigned(value):formatChannel);message(formatDirty?"Unsaved preview restored for this meter/channel":"Number format selected; changes need Save");}
 else if(target==FORMAT_DIGITS){editedFormat.digits=value;if(editedFormat.decimals>=editedFormat.digits)editedFormat.decimals=editedFormat.digits-1;formatDirty=true;}
 else if(target==FORMAT_DECIMALS){editedFormat.decimals=value;formatDirty=true;}
 else if(target==FORMAT_PADDING){editedFormat.padding=value?meter_ui::Padding::Zero:meter_ui::Padding::Blank;formatDirty=true;}
 else if(target==CHART_LAYOUT)meter_display::requestOverlay(value!=0);
 else if(target==SCOPE_WINDOW)scopeSeconds=value;
 else if(target==BT_DEVICE){selectedDevices[dropdownSlot]=candidate;haveSelectedDevice[dropdownSlot]=true;message("Selected device; press CONNECT to start readings");}
 else if(target==BT_PROFILE)selectedProfiles[dropdownSlot]=value<0?String():String(dmm::profileAt(value)->id);
 else if(target==NTP_ENABLED){ntpEnabled=value!=0;ntpDirty=true;}
 else if(target==NTP_SERVER){const char *servers[]={"pool.ntp.org","time.cloudflare.com","time.google.com"};ntpDirty=true;if(value<3)ntpServer=servers[value];else editor(ntpServer,"NTP hostname or IPv4 address",meter_ntp::serverBytes-1);}
 else if(target==4000)fields[0]=value?"on":"off";
 else if(target==4001)fields[1]=String(value);
 else if(target==4002)fields[2]=String(value);
 else if(target==4004)fields[4]=value?"below":"above";
 else if(target==4008)fields[8]=value?"GET":"POST";
 else if(target>=5000&&target<=5005){const bool changed=target==5000?voltageMeter!=unsigned(value):target==5001?voltageChannel!=unsigned(value):target==5002?currentMeter!=unsigned(value):target==5003?currentChannel!=unsigned(value):target==5004?calcConfig.ac!=(value!=0):calcConfig.negativeDischarge!=(value!=0);if(changed){if(target==5000)voltageMeter=value;else if(target==5001)voltageChannel=value;else if(target==5002)currentMeter=value;else if(target==5003)currentChannel=value;else if(target==5004)calcConfig.ac=value!=0;else calcConfig.negativeDischarge=value!=0;calcChanged();}}
 if(dropdownReturn>=0)closeDropdown();redraw=true;
}
void execute(int action){
    if(action==DISPLAY_COUNT||action==SCREEN_ROTATION||action==IMU_ALIGNMENT||action==VALUE_LAYOUT||action==SCOPE_METER||action==SCOPE_CHANNEL||action==SCOPE_WINDOW||action==CHART_LAYOUT||action==FORMAT_SLOT||action==FORMAT_CHANNEL||action==FORMAT_DIGITS||action==FORMAT_DECIMALS||action==FORMAT_PADDING||action==BT_SLOT||action==BT_DEVICE||action==BT_PROFILE||action==NTP_ENABLED||action==NTP_SERVER||action==4000||action==4001||action==4002||action==4004||action==4008||(action>=5000&&action<=5005)){openChoice(action);return;}

    if(action>=6000&&action<6006){const auto preset=meter_ui::ValuePreset(action-6000);if(meter_values::requestNativeLayout(preset)){valuesRequested=true;active.store(false);}else message("Value layout request unavailable");return;}
    if(action>=1000&&action<1100){unsigned index=action-1000;auto list=scanDoc["networks"].as<JsonArrayConst>();if(index<list.size()){auto n=list[index];if(!(n["joinable"]|false)){message("This Wi-Fi security type is unsupported");return;}wifiSsid=asText(n["ssid"]);wifiPassword="";message("Selected "+wifiSsid+"; enter password, then Join");}return;}
    if(action>=2000&&action<2200){auto list=filesDoc["files"].as<JsonArrayConst>();unsigned index=action-2000;if(index<list.size()){selectedFile=asText(list[index]["name"]);selectedFileBytes=String(list[index]["bytes"].as<unsigned long long>());setPage(Page::File);}return;}
    if(action>=3000&&action<3010){auto list=rulesDoc["rules"].as<JsonArrayConst>();unsigned index=action-3000;if(index<list.size())loadRule(list[index]);return;}
    if(action>=4000&&action<4013){unsigned index=action-4000;
        if(index==0)fields[0]=fields[0]=="on"?"off":"on";
        else if(index==1)fields[1]=String((fields[1].toInt()+1)%meter_ble::MAX_CONNECTIONS);
        else if(index==2)fields[2]=String((fields[2].toInt()+1)%8);
        else if(index==4)fields[4]=fields[4]=="above"?"below":"above";
        else if(index==8)fields[8]=fields[8]=="GET"?"POST":"GET";
        else {static const char *names[]={"Enabled","Meter","Channel","Exact unit","Crossing","Threshold","Hysteresis","Cooldown seconds","HTTP method","URL","POST body","Headers JSON","CA PEM certificate"};const unsigned max[]={3,1,1,11,5,32,32,32,4,512,1024,3072,4096};editor(fields[index],names[index],max[index]);}
        return;
    }
    if(action>=5000&&action<5009){unsigned index=action-5000;calcChanged();
        if(index==0)voltageMeter=(voltageMeter+1)%meter_ble::MAX_CONNECTIONS;
        else if(index==1)voltageChannel=(voltageChannel+1)%8;
        else if(index==2)currentMeter=(currentMeter+1)%meter_ble::MAX_CONNECTIONS;
        else if(index==3)currentChannel=(currentChannel+1)%8;
        else if(index==4)calcConfig.ac=!calcConfig.ac;
        else if(index==5)calcConfig.negativeDischarge=!calcConfig.negativeDischarge;
        else {const char *names[]={"Power factor (blank allowed)","Rated capacity Ah (blank allowed)","Initial SOC % (blank allowed)"};editor(calcFields[index-6],names[index-6],32);}
        return;
    }
    switch(action){
    case ALL_FUNCTIONS:menuBack=Page::Home;setPage(Page::Home);break;
    case BT_MENU:setPage(Page::Connections);break;
    case BT_SCAN:if(!meter_ble::scan())message("Scan request unavailable");break;
    case BT_CONNECT:{const unsigned slot=meter_ble::activeSlot();if(!haveSelectedDevice[slot]){message("Choose a device first");break;}auto v=meter_ble::view();const auto &picked=selectedDevices[slot];bool found=false;for(unsigned i=0;i<v.count;++i)if(!strcmp(v.meters[i].address,picked.address)&&v.meters[i].addressType==picked.addressType){found=true;const String profile=selectedProfiles[slot].length()?selectedProfiles[slot]:String(picked.profile);if(!profile.length())message("Choose a protocol profile for this device");else if(!meter_ble::connect(i,slot,profile.c_str(),picked.address,picked.addressType))message("Disconnect this slot before replacing its meter");else message("Connection requested; saved status is separate");break;}if(!found)message("Scan changed; select the device again");break;}
    case BT_DISCONNECT:if(!meter_ble::disconnect(meter_ble::activeSlot()))message("Disconnect request unavailable");break;
    case NTP_MENU:{auto n=meter_ntp::snapshot();ntpEnabled=n.enabled;ntpServer=n.server;ntpDirty=false;setPage(Page::Time);break;}
    case NTP_SAVE:{Document d;d["enabled"]=ntpEnabled;d["server"]=ntpServer;queue("POST","/api/ntp",json(d));break;}
    case NTP_SYNC:queue("POST","/api/ntp","{\"action\":\"sync\"}");break;
    case NETWORK:setPage(Page::Network);break;case RECORDING:setPage(Page::Recording);break;case CARD:setPage(Page::Card);break;
    case RULES:setPage(Page::Rules);break;case CALCULATIONS:setPage(Page::Calculations);break;case EVENTS:setPage(Page::Events);break;
    case DIAGNOSTICS:setPage(Page::Diagnostics);break;case HELP:setPage(Page::Help);break;case WEB:setPage(Page::Web);break;
    case SCOPE_MENU:setPage(Page::Scope);break;
    case VALUES_MENU:showValueSettings();break;
    case VALUE_SHOW:valuesRequested=true;active.store(false);break;
    case FORMAT_SLOT:selectFormatContext((formatSlot+1)%meter_ble::MAX_CONNECTIONS,formatChannel);break;
    case FORMAT_CHANNEL:selectFormatContext(formatSlot,(formatChannel+1)%8);break;
    case FORMAT_DIGITS:editedFormat.digits=editedFormat.digits==8?3:editedFormat.digits+1;if(editedFormat.decimals>=editedFormat.digits)editedFormat.decimals=editedFormat.digits-1;formatDirty=true;break;
    case FORMAT_DECIMALS:editedFormat.decimals=(editedFormat.decimals+1)%(std::min(6u,unsigned(editedFormat.digits-1))+1);formatDirty=true;break;
    case FORMAT_PADDING:editedFormat.padding=editedFormat.padding==meter_ui::Padding::Blank?meter_ui::Padding::Zero:meter_ui::Padding::Blank;formatDirty=true;break;
    case FORMAT_SAVE:if(meter_values::requestFormat(formatSlot,formatChannel,editedFormat)){formatDirty=false;rememberFormatDraft();message("Number format requested; check saved status");}else message("Number format save unavailable");break;
    case AUTO_DIM:meter_display::requestAutoDim(!meter_display::autoDim());message("Auto dim setting updated");break;
    case IMU_ROTATION:meter_board::setAutoRotation(!meter_board::autoRotation());message("IMU rotation setting requested");break;
    case SCOPE_METER:{unsigned slot=(meter_ble::activeSlot()+1)%meter_ble::MAX_CONNECTIONS;meter_ble::focus(slot);if(scopeChannels[slot]>=0)meter_ble::selectChannel(slot,scopeChannels[slot]);break;}
    case SCOPE_CHANNEL:{const auto view=meter_ble::view();int channel=scopeChannels[view.activeSlot]>=0?scopeChannels[view.activeSlot]:view.reading.channel;scopeChannels[view.activeSlot]=(channel+1)%8;meter_ble::selectChannel(view.activeSlot,scopeChannels[view.activeSlot]);break;}
    case SCOPE_WINDOW:{const unsigned values[]={5,10,30,60,120,300};unsigned i=0;while(i<6&&values[i]!=scopeSeconds)++i;scopeSeconds=values[(i+1)%6];break;}
    case DISPLAY_COUNT:meter_display::requestCount(meter_display::count()%meter_ble::MAX_CONNECTIONS+1);message("Display count requested; applies to values and charts");break;
    case CHART_LAYOUT:meter_display::requestOverlay(!meter_display::overlay());message("Chart layout requested; applies to native scope");break;
    case SCOPE_CLEAR:clearScopeRequested=true;message("Current native scope trace cleared");break;
    case SCOPE_SHOW:scopeRequested=true;active.store(false);break;
    case WIFI_SCAN:queue("POST","/api/wifi/scan","{}");break;
    case WIFI_SSID:editor(wifiSsid,"Wi-Fi network name",32);break;case WIFI_PASSWORD:editor(wifiPassword,"Wi-Fi password (blank=open)",63,true);break;
    case WIFI_JOIN:{Document d;d["ssid"]=wifiSsid;d["password"]=wifiPassword;if(!wifiSsid.length())message("Select or enter a network name first");else if(queue("POST","/api/wifi",json(d)))wifiPassword="";break;}
    case WIFI_DISCONNECT:queue("POST","/api/wifi","{\"ssid\":\"\",\"password\":\"\"}");break;
    case WIFI_QR:{auto n=meter_web::network();showQr(meter_wifi::payload(n.apSsid.c_str(),n.apPassword.c_str()).c_str(),"Join device Wi-Fi",n.apSsid);break;}
    case RECORD_LABEL:editor(recordLabel,"Next recording label",96);break;case RECORD_MARKER:editor(markerLabel,"Marker label",96);break;
    case RECORD_START:recording("start",recordLabel);break;case RECORD_STOP:recording("stop","");break;case RECORD_MARK:recording("mark",markerLabel);break;
    case CARD_REFRESH:case REFRESH:refresh.store(true);message("Refresh requested");break;
    case CARD_REMOUNT:queue("POST","/api/storage/remount","{}");break;
    case CARD_FORMAT:confirmKind=1;confirmBack=page;formatConfirmation="";setPage(Page::Confirm);break;
    case FILE_QR:showQr("http://"+meter_web::network().apIp+"/api/storage/download?name="+encodeUrl(selectedFile),"Download TF CSV",selectedFile);break;
    case NEW_RULE:newRule();break;case SAVE_RULE:saveRule();break;
    case RULE_USE_UNIT:{const auto connection=meter_ble::connection(fields[1].toInt());const unsigned channel=fields[2].toInt();if(channel<8&&connection.hasReading[channel]){fields[3]=connection.readings[channel].unit;message("Copied exact live unit bytes for this meter/channel");}else message("No reading on this meter/channel; choose a live channel first");break;}
    case FILE_PREVIOUS:if(filePage)--filePage;break;
    case FILE_NEXT:if((filePage+1)*48<filesDoc["files"].size())++filePage;break;
    case TEST_RULE:{Document d;d["id"]=ruleId;queue("POST","/api/rules/test",json(d));break;}
    case DELETE_RULE:confirmKind=2;confirmBack=page;setPage(Page::Confirm);break;
    case CALC_SETTINGS:setPage(Page::CalcSettings);break;
    case CALC_START:{if(calcDirty){message("Reset before starting with changed settings");break;}if(!calcSettings())break;auto voltage=calcReading(voltageMeter,voltageChannel,&previousDrops[0]),current=calcReading(currentMeter,currentChannel,&previousDrops[1]);auto r=meter_calc::calculate(voltage,current,calcConfig);if(!r.valid||calcConfig.ac){message(calcConfig.ac?"Battery integration requires DC":String(r.reason));break;}dropBaseline=true;pendingLoss[voltageMeter]=pendingLoss[currentMeter]=false;battery.start();message("Battery test running; totals are in device RAM");break;}
    case CALC_PAUSE:battery.pause();message("Battery test paused. Resume explicitly with Start");break;
    case CALC_RESET:battery.reset();calcDirty=false;message("Native battery totals reset");break;
    case CALC_WEB:case SHOW_SUMMARIES:showQr("http://"+meter_web::network().apIp+"/","Open web Calculations","Browser summaries/settings are separate. Open the Calculations tab.");break;
    case SHOW_SESSIONS:showQr("http://"+meter_web::network().apIp+"/","Open web Sessions","Open Sessions to save/export/delete browser-local recordings.");break;
    case SHOW_WEB:showQr("http://"+meter_web::network().apIp+"/","Open web dashboard","Join device Wi-Fi or use its LAN. All browser functions remain available.");break;
    case README_QR:showQr("http://"+meter_web::network().apIp+"/readme.html","Full offline README","Open the full device instructions in your browser.");break;
    case CLOCK_EDIT:editor(clockText,"Unix time in milliseconds",13);break;
    case CLOCK_SET:{double value;if(!finiteInput(clockText,value)||value<1577836800000.0||value>4102444800000.0||floor(value)!=value){message("Unix milliseconds must represent a date in 2020..2100");break;}Document d;d["unixMs"]=uint64_t(value);queue("POST","/api/time",json(d));break;}
    case TOUCH_CALIBRATE:meter_calibration::request();break;
    case CONFIRM_INPUT:editor(formatConfirmation,"Type FORMAT TF CARD",14);break;
    case CONFIRM_EXECUTE:
        if(confirmKind==1){if(!statusFresh()||!(statusDoc["storage"]["supported"]|false)||formatConfirmation!="FORMAT TF CARD"||cardBusy()||formatOutstanding()){message("Need fresh supported TF status, idle card and exact confirmation");break;}char token[48];snprintf(token,sizeof(token),"touch_%08lx_%08lx_%08lx",static_cast<unsigned long>(millis()),static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));Document d;d["confirm"]="FORMAT TF CARD";d["operationId"]=token;pendingFormatJob=queue("POST","/api/storage/format",json(d));if(pendingFormatJob){pendingFormat=token;formatConfirmation="";setPage(Page::Card);message("Format requested. Waiting for matching operation result");}}
        else if(confirmKind==2){if(queue("DELETE",String("/api/rules?id=")+String(ruleId)))setPage(Page::Rules);}break;
    case KEY_LEFT:editCursor=cursorOffset(editCursor,-1,editText.length());break;case KEY_RIGHT:editCursor=cursorOffset(editCursor,1,editText.length());break;
    case KEY_SPACE:insert(' ');break;case KEY_LF:insert('\n');break;
    case KEY_PREVIOUS:{const unsigned pages=layout.keyboard().pages();keyPage=(keyPage+pages-1)%pages;break;}
    case KEY_NEXT:keyPage=(keyPage+1)%layout.keyboard().pages();break;
    case KEY_BACKSPACE:if(editCursor){editText.remove(editCursor-1,1);--editCursor;}break;
    case KEY_DONE:if(editTarget){*editTarget=editText;setPage(previousPage);scroll=previousScroll;editTarget=nullptr;message("Field updated; use Save / Join / Start to apply");}break;
    default:break;
    }
}

String rotationDetail(){
#ifdef UT_METER_LCD349
    if(meter_board::autoRotation())return String("Automatic (IMU) / ")+String(meter_display::rotation()*90)+" degrees";
    return String("Manual / ")+String(meter_display::rotation()*90)+" degrees / "+(meter_display::rotationSaved()?"saved":"RAM / waiting to save");
#else
    return String(meter_display::rotation()*90)+" degrees / "+(meter_display::rotationSaved()?"saved":"RAM / waiting to save");
#endif
}
void rotationControls(bool enabled){
    add("SCREEN ROTATION",rotationDetail(),SCREEN_ROTATION,enabled);
#ifdef UT_METER_LCD349
    const char *names[]={"0 degrees","90 clockwise","180 degrees","90 counterclockwise"};
    add("IMU ALIGNMENT",String(names[meter_board::imuRotationOffset()])+" / "+(meter_board::imuRotationOffsetSaved()?"saved":"RAM / not saved"),IMU_ALIGNMENT,enabled);
#endif
}
void buildRows(){
    rows.clear();JsonObjectConst rec=statusDoc["recording"].as<JsonObjectConst>(),net=statusDoc["network"].as<JsonObjectConst>(),card=statusDoc["storage"].as<JsonObjectConst>();
    const bool freshStatus=statusFresh();
    if(notice.startsWith("FAILED:"))info(notice,WARN);
    switch(page){
    case Page::Screen:
#ifdef UT_METER_LCD349
        add("IMU AUTO ROTATION",String(meter_board::autoRotation()?"ON":"OFF")+" / tap to switch",IMU_ROTATION);
#endif
        add("AUTO DIM",String(meter_display::autoDim()?"ON / 2 min idle to 10%":"OFF / keep screen bright")+" (tap to switch)",AUTO_DIM);
        rotationControls(!meter_calibration::busy());
        add("SHOW VALUES","Full-screen meter readings",VALUE_SHOW);
        add("SHOW FULL SCOPE","Full-screen chart",SCOPE_SHOW);
        info("Live shortcuts: top down = screen; left inward = meters; right inward = functions. Swipe up on values for layout. Back returns to the previous live view.");break;
    case Page::Quick:
        add("LAYOUT",meter_ui::valuePresetName(meter_values::snapshot().presets[meter_display::count()-1]),VALUE_LAYOUT);
        add("METERS SHOWN",String(meter_display::count())+" / 4",DISPLAY_COUNT);
        rotationControls(!meter_calibration::active());
        add("VALUES / NUMBER FORMAT","Meter tabs, digits, decimals and padding",VALUES_MENU);
        add("METERS / BLUETOOTH","M1-M4 setup, scan and explicit Connect",BT_MENU);
        add("ALL FUNCTIONS","Wi-Fi, TF files, triggers, charts and more",ALL_FUNCTIONS);
        add("RETURN TO VALUES","Live full-screen readings",VALUE_SHOW);break;
    case Page::Home:
        add("NETWORK / WI-FI","Scan, select, password and join",NETWORK);add("RECORDING","Start, stop, labels and markers",RECORDING);
        add("TF CARD / FILES","Status, downloads, remount and format",CARD);add("TRIGGERS / HTTP","Create, edit, test and delete",RULES);
        add("POWER / BATTERY","Native V/A, W, Ah, Wh, SOC and runtime",CALCULATIONS);add("EVENTS","Recent device and delivery events",EVENTS);
        add("SCOPE CONTROLS","Meter, channel, time window and clear trace",SCOPE_MENU);add("DIAGNOSTICS","Meters, memory, clock and queues",DIAGNOSTICS);add("HELP","Instructions and data boundaries",HELP);add("OPEN WEB","Browser sessions, summaries and exports",WEB);add("VALUES / FORMAT","CCTV layouts, digits and decimal places",VALUES_MENU);add("METERS / BLUETOOTH","Choose1-4 meter slots, device and protocol",BT_MENU);add("DATE / TIME / NTP","Automatic network time and sync status",NTP_MENU);break;
    case Page::Network:
        info("LAN: "+asText(net["state"],"waiting")+" "+asText(net["staSsid"])+" "+asText(net["staIp"])+"\n"+asText(net["message"]));
        add("SCAN NETWORKS",asText(scanDoc["state"])+" "+asText(scanDoc["message"]),WIFI_SCAN);
        add("NETWORK NAME",wifiSsid.length()?wifiSsid:"Tap to enter manual SSID",WIFI_SSID);add("PASSWORD",wifiPassword.length()?String(wifiPassword.length())+" characters; tap to edit":"Blank for an open network",WIFI_PASSWORD);
        add("JOIN NETWORK","Save credentials and connect; device AP stays on",WIFI_JOIN,wifiSsid.length()>0);add("DISABLE LAN","Forget station credentials; AP stays on",WIFI_DISCONNECT);add("JOIN DEVICE AP","Show Wi-Fi QR",WIFI_QR);
        {unsigned i=0;for(JsonObjectConst n:scanDoc["networks"].as<JsonArrayConst>()){add(asText(n["ssid"]),asText(n["security"])+"  "+String(n["rssi"]|0)+" dBm  CH "+String(n["channel"]|0),1000+i,n["joinable"]|false);++i;}}
        break;
    case Page::Recording:
        info(String("DEVICE RECORDING: ")+((rec["active"]|false)?"ACTIVE":"STOPPED")+"\n"+asText(rec["label"])+"\n"+asText(rec["id"]));
        add("NEXT LABEL",recordLabel.length()?recordLabel:"Tap to enter a recording label",RECORD_LABEL);
        add("START RECORDING","TF continues with browser closed when mounted",RECORD_START,freshStatus&&!(rec["active"]|false)&&!cardBusy());add("STOP RECORDING","Wait for queued TF writes to finish",RECORD_STOP,freshStatus&&(rec["active"]|false));
        add("MARKER LABEL",markerLabel,RECORD_MARKER);add("ADD MARKER","Tag current device feed / recording",RECORD_MARK,freshStatus);
        info("Browser IndexedDB is independent. Browser save counts require an open dashboard. TF files remain on the card. A device recording flag alone does not prove TF writes succeeded.");add("TF WRITE STATUS","Inspect written/dropped counts",CARD);add("BROWSER SESSIONS","Open web to save/export/delete browser data",SHOW_SESSIONS);break;
    case Page::Card:
        info(String("TF: ")+((card["supported"]|false)?((card["mounted"]|false)?"MOUNTED":"UNAVAILABLE"):"NOT SUPPORTED ON THIS BOARD")+"\n"+asText(card["error"]),card["mounted"]|false?GREEN:WARN);
        info("File: "+asText(card["activeFile"])+"\nWritten "+String(card["writtenRecords"]|0)+" / dropped "+String(card["droppedRecords"]|0)+" / queue "+String(card["queueDepth"]|0)+"\nUsed "+number(card["usedBytes"]|0.0)+" / total "+number(card["totalBytes"]|0.0)+" bytes");
        if(card["formatting"]|false)info("FORMATTING: "+asText(card["formatOperationId"]),WARN);
        if(pendingFormat.length()){String id=asText(card["formatOperationId"]);info(id==pendingFormat?"This format: "+asText(card["formatResult"])+" "+asText(card["formatMessage"]):"Waiting for this format operation. Do not submit again.",WARN);}
        else if(asText(card["formatResult"]).length())info("Last format: "+asText(card["formatResult"])+" "+asText(card["formatMessage"]));
        add("REFRESH FILES",asText(filesDoc["error"]),CARD_REFRESH);add("REMOUNT TF","Retry mount without formatting",CARD_REMOUNT,freshStatus&&(card["supported"]|false)&&!cardBusy());
        add("FORMAT TF CARD","ERASES the whole card; explicit confirmation",CARD_FORMAT,freshStatus&&(card["supported"]|false)&&!cardBusy()&&!formatOutstanding(),WARN);
        if((filesDoc["truncated"]|false)||(card["listTruncated"]|false))info("File listing is bounded/truncated. Download existing logs before removing files externally.",WARN);
        {auto files=filesDoc["files"].as<JsonArrayConst>();const unsigned pages=std::max(1U,(unsigned(files.size())+47)/48);if(filePage>=pages)filePage=pages-1;
            add(String("FILES PAGE ")+String(filePage+1)+" / "+String(pages),String(files.size())+" cached files / 48 per page");
            add("PREVIOUS FILE PAGE","All cached files remain reachable",FILE_PREVIOUS,filePage>0);add("NEXT FILE PAGE","All cached files remain reachable",FILE_NEXT,filePage+1<pages);
            for(unsigned i=filePage*48;i<files.size()&&i<(filePage+1)*48;++i){auto file=files[i];add(asText(file["name"]),number(file["bytes"]|0.0)+" bytes"+((file["active"]|false)?" / active snapshot":""),2000+i);}
            add("PREVIOUS FILE PAGE","",FILE_PREVIOUS,filePage>0);add("NEXT FILE PAGE","",FILE_NEXT,filePage+1<pages);
        }
        break;
    case Page::File:info(selectedFile+"\n"+selectedFileBytes+" bytes");add("DOWNLOAD CSV QR","Scan with a phone connected to device Wi-Fi",FILE_QR);info("The QR opens this exact file download. No file data is copied into touchscreen RAM. Active files download a fixed-length snapshot.");break;
    case Page::Rules:
        add("NEW TRIGGER","Exact meter/channel/unit; at most 8",NEW_RULE);add("REFRESH TRIGGERS","Device rules continue without a browser",REFRESH);
        {unsigned i=0;for(JsonObjectConst rule:rulesDoc["rules"].as<JsonArrayConst>()){add(String("TRIGGER ")+String(rule["id"]|0)+((rule["enabled"]|false)?" / ON":" / OFF"),String("M")+String((rule["meter"]|0)+1)+" CH"+String((rule["channel"]|0)+1)+" "+asText(rule["operator"])+" "+number(rule["threshold"]|0.0)+" "+asText(rule["unit"]),3000+i);info(asText(rule["lastResult"])+" / HTTP "+String(rule["lastCode"]|0));++i;}}
        info("Manual test sends the configured HTTP request. It needs a matching numeric reading from the last 30 seconds. A queued request is not confirmed delivery.");break;
    case Page::Rule:{
        static const char *names[]={"ENABLED","METER","CHANNEL","EXACT UNIT","CROSSING","THRESHOLD","HYSTERESIS","COOLDOWN SECONDS","METHOD","URL","POST BODY","HEADERS JSON","CA PEM CERTIFICATE"};
        info(ruleId?String("Editing device trigger ")+String(ruleId):"New trigger; Save writes device settings");
        for(unsigned i=0;i<13;++i){String detail=fields[i];if(i==1||i==2)detail=String(fields[i].toInt()+1)+" (select)";if(i==10||i==11||i==12)detail=String(fields[i].length())+" bytes; tap to edit";add(names[i],detail,4000+i);}
        add("USE LIVE READING UNIT","Copy exact unit, including ohm / micro / degrees",RULE_USE_UNIT);
        add("SAVE TRIGGER",pendingSave?"Waiting for saved trigger acknowledgement":"Backend validates URL, unit, headers and TLS",SAVE_RULE,!pendingSave);add("SEND HTTP TEST","Sends a real request using this saved trigger",TEST_RULE,ruleId>0&&!pendingSave);add("DELETE TRIGGER","Requires a second confirmation",DELETE_RULE,ruleId>0&&!pendingSave,WARN);
        info("Body/header templates support {{value}}, {{unit}}, {{meter}}, {{timestamp}}. Header values are strings in a JSON object. HTTPS validates CA and time; use Diagnostics to set the clock.");break;}
    case Page::Calculations:{
        auto result=meter_calc::calculate(calcReading(voltageMeter,voltageChannel),calcReading(currentMeter,currentChannel),calcConfig);auto totals=battery.summary();
        info("V: "+sourceLabel(voltageMeter,voltageChannel)+"\nI: "+sourceLabel(currentMeter,currentChannel));
        info(result.valid?"Voltage "+number(result.voltageV)+" V / Current "+number(result.currentA)+" A\nPower "+number(calcConfig.ac?result.voltAmps:result.watts)+(calcConfig.ac?" VA":" W")+" / real "+(result.wattsValid?number(result.watts):"--")+" W\nV/I "+(result.ohmsValid?number(result.ohms):"--")+" ohm":String("No live result: ")+result.reason,result.valid?GREEN:WARN);
        add("SOURCE / ASSUMPTIONS","Choose meters/channels, DC/AC, PF, capacity, SOC",CALC_SETTINGS);
        add(totals.running?"TEST RUNNING":"START / RESUME TEST","DC battery test; fresh compatible sources required",CALC_START,!totals.running&&!calcDirty);add("PAUSE TEST","Excludes unobserved intervals",CALC_PAUSE,totals.running);add("RESET NATIVE TEST","Clears RAM totals and continuity warnings",CALC_RESET);
        info(String(totals.running?"RUNNING":"PAUSED / IDLE")+" / "+totals.reason+"\nDischarge "+number(totals.dischargeAh,8)+" Ah / "+number(totals.dischargeWh,8)+" Wh\nCharge in "+number(totals.chargeAh,8)+" Ah / "+number(totals.chargeWh,8)+" Wh\nIntegrated "+number(totals.integratedMs/1000.0)+" s / "+String(totals.samples)+" samples / "+String(totals.gaps)+" gaps");
        info(totals.estimateValid?"Estimated remaining "+number(totals.remainingAh)+" Ah / "+number(totals.socPercent)+"%\nRuntime at present current "+(totals.runtimeValid?number(totals.runtimeHours)+" h":"--"):"SOC/runtime unavailable: enter capacity + initial SOC and reset after any unobserved interval.");
        info("Native totals/settings are in device RAM, separate from browser calculations. Reset/reboot loses native totals. V/I is a ratio, not an ohmmeter test. AC V x I is VA; watts needs PF. Estimates omit cutoff, efficiency, temperature and load changes.");add("BROWSER SUMMARIES","Open web for saved summaries and JSON exports",CALC_WEB);break;}
    case Page::CalcSettings:
        add("VOLTAGE METER",sourceLabel(voltageMeter,voltageChannel),5000);add("VOLTAGE CHANNEL",String(voltageChannel+1)+" / 8 (select)",5001);
        add("CURRENT METER",sourceLabel(currentMeter,currentChannel),5002);add("CURRENT CHANNEL",String(currentChannel+1)+" / 8 (select)",5003);
        add("ELECTRICAL MODE",calcConfig.ac?"AC / RMS (tap for DC)":"DC (tap for AC / RMS)",5004);add("DISCHARGE SIGN",calcConfig.negativeDischarge?"Negative current means discharge":"Positive current means discharge",5005);
        add("AC POWER FACTOR",calcFields[0].length()?calcFields[0]:"Optional; blank means real watts unknown",5006);add("RATED CAPACITY AH",calcFields[1].length()?calcFields[1]:"Blank for measured-only integration",5007);add("INITIAL SOC %",calcFields[2].length()?calcFields[2]:"Blank with capacity, or enter 0..100",5008);
        info("Sources/settings changes pause integration. Reset a test with samples before changing its assumptions. Choose voltage and current from the same circuit; independent meters are not synchronized.");break;
    case Page::Connections:{const unsigned slot=meter_ble::activeSlot();const auto c=meter_ble::connection(slot);
        if(!layout.tabs.columns)add("METER SLOT",String("Meter ")+String(slot+1)+" / 4",BT_SLOT);
        add("DEVICE",haveSelectedDevice[slot]?String(selectedDevices[slot].name)+" / "+selectedDevices[slot].address:"Scan and select a device",BT_DEVICE);
        add("PROTOCOL PROFILE",selectedProfiles[slot].length()?selectedProfiles[slot]:String("Auto detect"),BT_PROFILE);
        add("SCAN METERS","Refresh nearby Bluetooth meter choices",BT_SCAN);
        add("CONNECT","Explicitly connect selected device to this slot",BT_CONNECT,c.state!=meter_ble::State::Connected&&c.state!=meter_ble::State::Connecting&&c.state!=meter_ble::State::Retry);
        add("DISCONNECT","Disconnect only this meter slot",BT_DISCONNECT);
        info(String("State: ")+String(unsigned(c.state))+" / "+c.message+"\n"+c.peer.address);info(meter_ble::pairingsSaved()?"Pairings saved":"Pairings queued / RAM only",meter_ble::pairingsSaved()?MUTED:WARN);if(meter_ble::pairingMessage().length())info(meter_ble::pairingMessage(),WARN);break;}
    case Page::Time:{auto n=meter_ntp::snapshot();const bool same=ntpEnabled==n.enabled&&ntpServer==n.server;if(same&&n.saved&&!n.pending)ntpDirty=false;add("AUTOMATIC NTP",ntpEnabled?"Enabled":"Disabled",NTP_ENABLED);add("NTP SERVER",ntpServer,NTP_SERVER);add("SAVE TIME SETTINGS",!same?"Draft settings not applied":n.pending?"Waiting for saved readback":n.saved?"Settings saved":"RAM only / settings not saved",NTP_SAVE);add("SYNC NTP NOW","Request time from the configured server",NTP_SYNC);
        info(n.synced?String("NTP synchronized / ")+n.lastSyncServer:n.waitingForNetwork?String("Waiting for Wi-Fi with network access"):n.syncing?String("NTP sync in progress"):String("NTP not synchronized"));
        info(String("Clock source: ")+asText(statusDoc["clock"]["source"],"none"));const uint64_t shownUtc=n.currentUnixMs?n.currentUnixMs:(statusDoc["clock"]["unixMs"]|uint64_t(0));
        if(shownUtc){time_t seconds=time_t(shownUtc/1000);struct tm utc;gmtime_r(&seconds,&utc);char value[40];strftime(value,sizeof(value),"%Y-%m-%d %H:%M:%S UTC",&utc);info(value);}
        if(n.error[0])info(n.error,WARN);info("NTP stores UTC. Connect Wi-Fi for internet servers; an explicit local IPv4 NTP server can also be used. Browser time remains a fallback.");break;}
    case Page::Values:{
        const auto prefs=meter_values::snapshot();
        add("MAIN METER",String("Meter ")+String(meter_ble::activeSlot()+1)+" (select)",SCOPE_METER);
        add("DISPLAY CHANNEL",(meter_touch::scopeChannel(meter_ble::activeSlot())<0?String("Auto / current channel"):String(meter_touch::scopeChannel(meter_ble::activeSlot())+1))+" (select)",SCOPE_CHANNEL);
        add("METERS SHOWN",String(meter_display::count())+" / 4",DISPLAY_COUNT);
        add("VALUE LAYOUT",meter_ui::valuePresetName(prefs.presets[meter_display::count()-1]),VALUE_LAYOUT);
        rotationControls(!meter_calibration::active());
        if(!layout.tabs.columns)add("FORMAT METER",String("Meter ")+String(formatSlot+1),FORMAT_SLOT);
        add("FORMAT CHANNEL",String(formatChannel+1)+" / 8",FORMAT_CHANNEL);
        add("TOTAL DIGITS",String(editedFormat.digits)+" / 3..8 (sign/point have their own space)",FORMAT_DIGITS);
        add("DECIMAL PLACES",String(editedFormat.decimals)+" / fixed decimal position",FORMAT_DECIMALS);
        add("LEADING POSITIONS",editedFormat.padding==meter_ui::Padding::Blank?"Spaces":"Zeros",FORMAT_PADDING);
        const auto sample=meter_ble::displaySnapshot(formatSlot,formatChannel);
        add(sample.hasReading&&sample.reading.numeric?"FORMAT PREVIEW / LAST READING":"FORMAT PREVIEW / EXAMPLE","",NONE);
        rows.back().numeric=true;rows.back().value=sample.hasReading&&sample.reading.numeric?sample.reading.value:12.34;rows.back().format=editedFormat;
        add("SAVE NUMBER FORMAT",formatDirty?"Preview changes not applied":meter_values::saved()?"Device preferences saved":"RAM / waiting or saving failed",FORMAT_SAVE);
        add("SHOW VALUES","Return to live CCTV readings",VALUE_SHOW);
        if(meter_values::message().length())info(meter_values::message(),WARN);
        info("Fixed digits and decimals change only the display. Original precision remains in charts, calculations, API and logs. OVR keeps the original reading below the value. Sources stay in cyclic slot order.");break;}
    case Page::Scope:{
        add("VALUES / FORMAT","CCTV value layouts, digits and decimals",VALUES_MENU);
        rotationControls(!meter_calibration::active());
        add("AUTO DIM",String(meter_display::autoDim()?"ON / 2 min idle to 10%":"OFF / keep screen bright")+" (tap to switch)",AUTO_DIM);
        add("METERS SHOWN",String(meter_display::count())+" / 4 (select) / "+(meter_display::saved()?"saved":"RAM only"),DISPLAY_COUNT);
        add("CHART LAYOUT",String(meter_display::overlay()?"Overlay / one chart":"Separate / chart lanes")+" (tap to switch)",CHART_LAYOUT);
        const auto view=meter_ble::view();
        add("SCOPE METER",String("Meter ")+String(view.activeSlot+1)+" / 4 (select)",SCOPE_METER);
        add("SCOPE CHANNEL",String("Channel ")+String((scopeChannels[view.activeSlot]>=0?scopeChannels[view.activeSlot]:view.reading.channel)+1)+" / 8 (select)",SCOPE_CHANNEL);
        add("TIME WINDOW",String(scopeSeconds)+" seconds (select)",SCOPE_WINDOW);
        add("CLEAR TRACE","Clears current native meter/channel history",SCOPE_CLEAR);
        add("SHOW FULL SCOPE","Return to full-screen native trace",SCOPE_SHOW);
        info("The native trace holds the latest 128 observations per channel. A longer time window does not create extra samples. Browser traces and their Clear action are independent.");break;}
    case Page::Events:
        add("REFRESH EVENTS","Latest 24 device events; RAM feed is bounded",REFRESH);
        if(eventsDoc["gap"]|false)info("Device feed gap: older events may be missing.",WARN);
        {auto list=eventsDoc["events"].as<JsonArrayConst>();for(int i=int(list.size())-1;i>=0;--i){auto event=list[i];info(asText(event["event"])+" / #"+String(event["seq"].as<unsigned long long>())+" / HTTP "+String(event["code"]|0)+"\n"+asText(event["message"]));}}
        break;
    case Page::Diagnostics:
#ifdef UT_METER_LCD349
        add("IMU AUTO ROTATION",String(meter_board::autoRotation()?"ON":"OFF")+" / tap to switch",IMU_ROTATION);
        {const auto dev=statusDoc["boardDevices"].as<JsonObjectConst>();
        info(String("RTC PCF85063 UTC / ")+((dev["rtc"]["valid"]|false)?"VALID":"UNSET / ERROR")+"\n"+(dev["rtc"]["unixMs"].is<uint64_t>()?String(dev["rtc"]["unixMs"].as<uint64_t>()):String("--"))+"\n"+asText(dev["rtc"]["error"]));
        auto axis=[&](const char *group,unsigned i){const auto v=dev["imu"][group][i];return v.is<double>()?number(v.as<double>()):String("--");};
        info(String("IMU QMI8658 / ")+((dev["imu"]["sampleValid"]|false)?"LIVE":"UNAVAILABLE")+"\nAccel g: "+axis("accelerationG",0)+", "+axis("accelerationG",1)+", "+axis("accelerationG",2)+"\nGyro deg/s: "+axis("angularVelocityDps",0)+", "+axis("angularVelocityDps",1)+", "+axis("angularVelocityDps",2)+"\n"+asText(dev["imu"]["error"]));}
#endif
        rotationControls(!meter_calibration::active());
        add("AUTO DIM",String(meter_display::autoDim()?"ON / 2 min idle to 10%":"OFF / keep screen bright")+" / "+(meter_display::lightSaved()?"saved":"RAM only"),AUTO_DIM);
#ifdef UT_METER_JC3248
        add("TOUCH CALIBRATION","Four corners + centre; saved in NVS",TOUCH_CALIBRATE);
#endif
        add("REFRESH STATUS",freshStatus?"Local API current":"API status is unavailable / stale",REFRESH);
        info("Device "+asText(statusDoc["device"]["id"])+"\nBoot "+asText(statusDoc["device"]["bootId"])+"\nUptime "+number((statusDoc["device"]["uptimeMs"]|0.0)/1000)+" s\nHeap "+String(ESP.getFreeHeap())+" / PSRAM "+String(ESP.getFreePsram())+" bytes");
        info("AP "+asText(net["apIp"])+" / LAN "+asText(net["staIp"])+"\nOutbound queued "+String(statusDoc["webhook"]["queued"]|0)+" / dropped "+String(statusDoc["webhook"]["dropped"]|0)+"\nSample drops "+String(statusDoc["webhook"]["sampleQueueDropped"]|0)+"\nTLS clock "+((statusDoc["timeSynced"]|false)?String("SET"):String("NOT SET")));
        for(JsonObjectConst meter:statusDoc["meters"].as<JsonArrayConst>())info(String("M")+String((meter["slot"]|0)+1)+" "+asText(meter["name"])+" / "+asText(meter["state"])+"\n"+asText(meter["address"])+" / "+asText(meter["profile"])+"\nPackets "+String(meter["packets"]|0)+" dropped "+String(meter["dropped"]|0)+"\n"+asText(meter["message"]));
        add("UNIX MILLISECONDS",clockText.length()?clockText:"Enter UTC epoch time to set TLS clock",CLOCK_EDIT);add("SET DEVICE CLOCK","Requires a timestamp in 2020..2100",CLOCK_SET,clockText.length()>0);break;
    case Page::Help:
        info("Swipe up or down to scroll functions, forms and choices. Tap a field to edit. The keyboard has six character pages, Space, LF, cursor arrows and Backspace. Done accepts a field; Back cancels. Save/Join/Start sends the action.");
        info("On live values or charts: top edge down opens screen controls, left edge inward opens M1-M4 setup, right edge inward opens Functions. On values, swipe upward for Layout, Meters shown and screen rotation. All functions opens Wi-Fi, recording, TF files, triggers, calculations, events and diagnostics. M1-M4 tabs select a meter draft; Connect starts its Bluetooth connection explicitly.");
        info("Device recording, triggers and native battery integration can continue without a browser. Check TF write status before trusting card recording. Native calculations require fresh compatible channels and pause on gaps. Resume is explicit; gap-affected SOC estimates require Reset.");
        info("Browser IndexedDB recordings, saved calculation summaries and browser settings belong to that browser/profile and dashboard address. The MCU cannot read, export or delete them. Open Web provides the correct browser workflow; native calculations are independent.");
        info("Format erases the entire TF card and is never retried automatically. It is unavailable while recording, queued writes, downloads, remount or another format are active. A timeout is ambiguous: check the operation result before any new confirmation.");
        add("OPEN WEB DASHBOARD","Local saved browser data and complete controls",SHOW_WEB);add("FULL OFFLINE README","QR link to device instructions",README_QR);break;
    case Page::Web:
        info("These functions act on data stored in your browser. The touchscreen cannot read another browser's IndexedDB or local settings. Join the device AP with Network > Join device AP, then open a QR link below.");
        add("FULL DASHBOARD","Live meters, scope, rules, Wi-Fi and documentation",SHOW_WEB);add("BROWSER SESSIONS","Save/export CSV or JSON/delete local recordings",SHOW_SESSIONS);add("CALCULATION SUMMARIES","Save/export/delete browser summaries and settings",SHOW_SUMMARIES);info("Native battery results are independent device-RAM results. Opening the browser does not import them into browser summaries.");break;
    case Page::Text:info(pageText,FG);break;
    case Page::Confirm:
        if(confirmKind==1){info("ERASE THE ENTIRE TF CARD?\nAll files on this card will be lost. Stop recording and finish downloads first. This operation cannot be undone.",WARN);add("TYPE CONFIRMATION",formatConfirmation.length()?formatConfirmation:"Enter exactly: FORMAT TF CARD",CONFIRM_INPUT);add("ERASE / FORMAT NOW","One request only; wait for its operation result",CONFIRM_EXECUTE,freshStatus&&(card["supported"]|false)&&!cardBusy()&&!formatOutstanding()&&formatConfirmation=="FORMAT TF CARD",WARN);}
        else {info(String("Delete saved device trigger ")+String(ruleId)+"? Its outbound requests will no longer run.",WARN);add("CONFIRM DELETE","Permanently remove this trigger from device settings",CONFIRM_EXECUTE);}
        break;
    default:break;
    }
    scroll=scrollOffset(scroll,0,layout.contentHeight(int(rows.size())),layout.bottom-layout.top);
}
String displayText(const String &value){String out=value;out.replace("\xCE\xA9","ohm");out.replace("\xC2\xB5","u");out.replace("\xCE\xBC","u");out.replace("\xC2\xB0","deg");return out;}
void text(DeskPanel &panel,int x,int y,const String &value,uint8_t size=1,uint16_t color=FG){panel.setFont(nullptr);panel.setTextSize(size);panel.setTextColor(color);panel.setCursor(x,y);panel.print(displayText(value));}
void wrapped(DeskPanel &panel,int x,int y,int width,const String &value,unsigned lines,uint8_t size=1,uint16_t color=FG){
    const String shown=displayText(value);
    const unsigned columns=std::max(1,width/(6*size));unsigned offset=0;
    for(unsigned line=0;line<lines&&offset<shown.length();++line){unsigned count=std::min<unsigned>(columns,shown.length()-offset);int newline=shown.indexOf('\n',offset);if(newline>=int(offset)&&newline<int(offset+count))count=newline-offset;text(panel,x,y+line*8*size,shown.substring(offset,offset+count),size,color);offset+=count;if(offset<shown.length()&&shown[offset]=='\n')++offset;}
}
String preview(const String &value,int width,int size){
    String shown=displayText(value);shown.replace('\n',' ');
    const unsigned columns=std::max(1,width/(6*std::max(1,size)));
    return shown.length()>columns?(columns>3?shown.substring(0,columns-3)+"...":shown.substring(0,columns)):shown;
}
void box(DeskPanel &panel,meter_ui::Rect r,const String &label,bool enabled=true,uint16_t color=FG,unsigned size=0){
    if(meter_design::modern)meter_design::frame(panel,r,meter_design::RAISED,enabled?meter_design::BORDER:BG);
    else panel.fillRoundRect(r.x,r.y,r.w,r.h,5,BUTTON);
    if(!size)size=layout.tx.font(ui.advanced.keyboard.font_size);
    unsigned columns=std::max(1,r.w/(6*int(size)));String caption=label.substring(0,columns);
    text(panel,r.x+(r.w-int(caption.length())*6*size)/2,r.y+(r.h-8*size)/2,caption,size,enabled?color:MUTED);
}
void mappedText(DeskPanel &panel,meter_ui::Label label,const String &value,uint16_t color=FG){
    label=layout.tx.label(label);if(!label.size)return;
    const int width=int(value.length())*6*label.size;
    text(panel,label.x-(label.align==0?width/2:label.align>0?width:0),label.y,value,label.size,color);
}
void keyboard(DeskPanel &panel){
    const auto keys=layout.keyboard();const int width=layout.width,left=layout.left,top=layout.top;
    if(!keys.valid){wrapped(panel,left,top,width,"Keyboard cannot fit this viewport",3,1,WARN);return;}
    if(keys.captionHeight)text(panel,left,top,editCaption.substring(0,std::max(1,width/6)),1,MUTED);
    String shown=editText;if(editSecret){shown="";for(unsigned i=0;i<editText.length();++i)shown+='*';}
    const unsigned columns=std::max(1,width/6),start=editCursor>columns?editCursor-columns:0;
    shown=shown.substring(start,editCursor)+"|"+shown.substring(editCursor,std::min<unsigned>(shown.length(),editCursor+columns));
    const int captionHeight=keys.captionHeight;
    const unsigned lines=std::max(1,keys.textHeight/8);
    wrapped(panel,left,top+captionHeight,width,shown,lines,1,FG);
    const char *controls[]={"LEFT","RIGHT","SPACE","LF"},*footer[]={"PREV","NEXT","DEL","DONE"};
    const char *modernControls[]={"Left","Right","Space","LF"},*modernFooter[]={"Prev","Next","Del","Done"};
    for(int i=0;i<4;++i){
        box(panel,keys.control(i),meter_design::modern?modernControls[i]:controls[i],true,FG,keys.spec.font_size);
        if(meter_design::modern&&i==3){const auto r=keys.footer(i);const int size=std::max(1,keys.spec.font_size);meter_design::frame(panel,r,meter_design::ACCENT,meter_design::ACCENT);text(panel,r.x+(r.w-4*6*size)/2,r.y+(r.h-8*size)/2,"Done",size,BG);}
        else box(panel,keys.footer(i),meter_design::modern?modernFooter[i]:footer[i],true,FG,keys.spec.font_size);
    }
    for(unsigned index=0;index<keys.keyCount;++index){int code=keys.code(keyPage,index);String caption=code>=0?String(char(code)):"";if(code==32)caption="SP";box(panel,keys.key(index),caption,code>=0,FG,keys.spec.font_size);}
}
meter_design::Icon functionIcon(int action){
    using Icon=meter_design::Icon;
    switch(action){case NETWORK:return Icon::Wifi;case RECORDING:return Icon::Record;case CARD:return Icon::Card;case RULES:return Icon::Trigger;
    case CALCULATIONS:return Icon::Power;case EVENTS:return Icon::Events;case SCOPE_MENU:return Icon::Scope;case DIAGNOSTICS:return Icon::Tools;case VALUES_MENU:return Icon::Meter;case BT_MENU:return Icon::Meter;case NTP_MENU:return Icon::Tools;
    case HELP:return Icon::Help;case WEB:return Icon::Web;default:return Icon::Next;}
}
const char *functionCaption(int action){
    switch(action){case NETWORK:return "Wi-Fi";case RECORDING:return "Recording";case CARD:return "TF files";case RULES:return "API triggers";
    case CALCULATIONS:return "Power /\nbattery";case EVENTS:return "Events";case SCOPE_MENU:return "Scope";case DIAGNOSTICS:return "Diagnostics";case VALUES_MENU:return "Values /\nformat";case BT_MENU:return "Meters /\nBluetooth";case NTP_MENU:return "Date / time\nNTP";
    case HELP:return "Help";case WEB:return "Open web";default:return nullptr;}
}
const char *functionDetail(int action){
    switch(action){case NETWORK:return "Scan and join";case RECORDING:return "Labels and markers";case CARD:return "Downloads and card";case RULES:return "Create, edit and test";
    case CALCULATIONS:return "Live W, Ah and Wh";case EVENTS:return "Recent activity";case SCOPE_MENU:return "Trace and channels";case DIAGNOSTICS:return "Status and clock";case VALUES_MENU:return "Layout, digits, decimals";case BT_MENU:return "Slots, devices, profiles";case NTP_MENU:return "Network clock and UTC";
    case HELP:return "Instructions";case WEB:return "Dashboard links";default:return "";}
}
void modernRow(DeskPanel &panel,meter_ui::Rect r,const Row &row){
    const auto &spec=ui.advanced.row;
    const int padding=layout.tx.xLength(spec.padding),titleY=layout.tx.yLength(spec.title_y),detailY=layout.tx.yLength(spec.detail_y);
    const uint16_t color=row.enabled?row.color:MUTED;
    if(layout.homeGrid()&&!row.info){
        meter_design::frame(panel,r,meter_design::SURFACE,meter_design::BORDER);
        const int inset=layout.tx.xLength(12),tile=layout.tx.xLength(36),iconX=r.x+inset+tile/2,iconY=r.y+layout.tx.yLength(12)+tile/2;
        meter_design::frame(panel,{r.x+inset,r.y+layout.tx.yLength(12),tile,tile},meter_design::RAISED,meter_design::RAISED);
        meter_design::icon(panel,functionIcon(row.action),iconX,iconY,row.enabled?meter_design::ACCENT:MUTED,layout.tx.xLength(20));
        const char *caption=functionCaption(row.action);
        const int captionX=r.x+inset+tile+layout.tx.xLength(10);
        wrapped(panel,captionX,r.y+layout.tx.yLength(16),r.x+r.w-inset-captionX,caption?String(caption):row.title,2,layout.tx.font(spec.title_size),color);
        wrapped(panel,r.x+inset,r.y+layout.tx.yLength(66),r.w-2*inset,functionDetail(row.action),1,layout.tx.font(spec.detail_size),MUTED);
        return;
    }
    meter_design::frame(panel,r,row.info?meter_design::RAISED:meter_design::SURFACE,meter_design::BORDER);
    if(row.numeric){
        wrapped(panel,r.x+padding,r.y+titleY,r.w-2*padding,row.title,1,layout.tx.font(spec.detail_size),MUTED);
        const auto out=meter_ui::formatNumber(row.value,row.format);
        meter_value_view::number(panel,{r.x+padding,r.y+detailY,r.w-2*padding,std::max(8,r.h-detailY-4)},out,row.format,FG,36);return;
    }
    if(row.info){
        if(row.color==WARN||row.color==GREEN)panel.fillRoundRect(r.x+layout.tx.xLength(4),r.y+layout.tx.yLength(10),layout.tx.xLength(3),std::max(1,r.h-layout.tx.yLength(20)),1,row.color);
        wrapped(panel,r.x+padding,r.y+titleY,r.w-2*padding,row.title,std::max(1,(r.h-titleY-layout.tx.yLength(4))/(8*layout.tx.font(spec.info_size))),layout.tx.font(spec.info_size),row.color);
    }else {
        const int arrowWidth=row.action==NONE?0:layout.tx.xLength(20);
        wrapped(panel,r.x+padding,r.y+titleY,r.w-2*padding-arrowWidth,row.title,r.h<60&&layout.tx.width>layout.tx.height?1:2,layout.tx.font(spec.title_size),color);
        wrapped(panel,r.x+padding,r.y+detailY,r.w-2*padding,preview(row.detail,r.w-2*padding,layout.tx.font(spec.detail_size)),1,layout.tx.font(spec.detail_size),MUTED);
        if(row.action!=NONE)text(panel,r.x+r.w-padding-layout.tx.xLength(8),r.y+titleY,">",layout.tx.font(spec.detail_size),row.enabled?meter_design::ACCENT:MUTED);
    }
}
void header(DeskPanel &panel,const String &title){
    const auto label=layout.tx.label(ui.advanced.header);
    box(panel,layout.back,meter_design::modern?"Back":"BACK");
    if(!label.size)return;
    if(meter_design::modern){
        const bool showBadge=layout.width>=400;const int badgeWidth=showBadge?layout.tx.xLength(70):0,right=layout.left+layout.width,badgeX=right-badgeWidth;
        const int noticeY=layout.tx.y(ui.advanced.message.y),headingBottom=noticeY>=label.y&&noticeY<layout.headerBottom?noticeY-4:layout.headerBottom-layout.tx.yLength(6);
        meter_design::heading(panel,{label.x,label.y,std::max(1,badgeX-label.x-layout.tx.xLength(showBadge?10:0)),std::max(1,headingBottom-label.y)},title,FG);
        if(showBadge)meter_design::badge(panel,{badgeX,layout.tx.y(16),badgeWidth,layout.tx.yLength(24)},statusFresh()?"API LIVE":"OFFLINE",statusFresh()?GREEN:WARN);
    }else wrapped(panel,label.x,label.y,layout.left+layout.width-label.x,title,2,label.size);
}
void scrollIndicator(DeskPanel &panel){
    const int height=layout.bottom-layout.top,content=layout.contentHeight(int(rows.size()));
    if(height<=0||content<=height)return;
    const int thumb=std::min(height,std::max(16,int(int64_t(height)*height/content))),y=layout.top+int(int64_t(height-thumb)*scroll/(content-height));
    panel.fillRect(layout.left+layout.width-3,layout.top,2,height,meter_design::BORDER);
    panel.fillRoundRect(layout.left+layout.width-3,y,2,thumb,1,MUTED);
}
void meterTabs(DeskPanel &panel){
    if(!layout.tabs.columns)return;
    const unsigned selected=page==Page::Values?formatSlot:meter_ble::activeSlot();
    for(unsigned i=0;i<meter_ble::MAX_CONNECTIONS;++i){
        const auto r=layout.tabs.rect(i);meter_design::frame(panel,r,i==selected?meter_design::RAISED:BUTTON,i==selected?GREEN:meter_design::BORDER);
        String label=String("M")+String(i+1);if(page==Page::Values&&(i==formatSlot?formatDirty:formatDraftDirty[i][formatChannel]))label+="*";
        text(panel,r.x+(r.w-int(label.length())*12)/2,r.y+(r.h-16)/2,label,2,i==selected?GREEN:FG);
    }
}
void qr(DeskPanel &panel){
    static String cached;static uint8_t data[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];static bool ready=false;
    if(cached!=qrText){cached=qrText;uint8_t scratch[sizeof(data)];ready=qrcodegen_encodeText(qrText.c_str(),scratch,data,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,false);}
    if(!ready){wrapped(panel,layout.left,layout.top,layout.width,"QR does not fit. Open the dashboard address shown below.",3,1,WARN);}
    else {const int modules=qrcodegen_getSize(data),limit=std::min(layout.width,layout.bottom-layout.top-66),pixel=limit/(modules+8),extent=pixel*(modules+8),x=layout.left+(layout.width-extent)/2;
        if(pixel<1)text(panel,layout.left,layout.top,"QR unavailable at this size",1,WARN);
        else {panel.fillRect(x,layout.top,extent,extent,0xffff);for(int row=0;row<modules;++row)for(int col=0;col<modules;++col)if(qrcodegen_getModule(data,col,row))panel.fillRect(x+(col+4)*pixel,layout.top+(row+4)*pixel,pixel,pixel,0);}
    }
    wrapped(panel,layout.left,layout.bottom-60,layout.width,qrHint+"\n"+qrText,6,1,MUTED);
}
uint16_t previewColor(unsigned i){const uint16_t colors[]={ui.overview.slot0_color,ui.overview.slot1_color,ui.overview.slot2_color,ui.overview.slot3_color};return colors[(meter_ble::activeSlot()+i)%meter_ble::MAX_CONNECTIONS];}
void valueLayoutPreview(DeskPanel &panel,meter_ui::Rect icon,meter_ui::ValuePreset preset,unsigned count){
 const int width=ui.reading.rotation%2?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH,height=ui.reading.rotation%2?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT;
 const meter_ui::Transform tx{meter_ui::view(ui.reading),width,height};const auto area=tx.rect(ui.overview.values);
 const auto tiles=meter_ui::valueTiles(area,preset,count,tx.yLength(ui.overview.gap));if(!tiles.valid)return;
 for(unsigned i=0;i<count;++i){const auto r=tiles.tiles[i].bounds;const int x=icon.x+(r.x-area.x)*icon.w/area.w,y=icon.y+(r.y-area.y)*icon.h/area.h,w=std::max(3,r.w*icon.w/area.w-1),h=std::max(3,r.h*icon.h/area.h-1);panel.fillRoundRect(x,y,w,h,2,previewColor(i));if(w>=10&&h>=10)text(panel,x+(w-6)/2,y+(h-8)/2,String(i+1),1,BG);}
}
void chartLayoutPreview(DeskPanel &panel,meter_ui::Rect icon,bool overlay){
 const unsigned count=meter_display::count();for(unsigned i=0;i<count;++i){auto r=overlay?icon:meter_ui::scopeLane(icon,count,i,3);if(r.w<4||r.h<4)continue;panel.drawRect(r.x,r.y,r.w,r.h,MUTED);const int inset=3,span=std::max(1,r.w-2*inset),height=std::max(1,r.h-2*inset);int x=r.x+inset,y=r.y+inset+height/2;for(int p=1;p<=6;++p){const int nx=r.x+inset+p*span/6,ny=r.y+inset+((p*7+int(i)*3)%11)*height/11;panel.drawLine(x,y,nx,ny,previewColor(i));x=nx;y=ny;}}
}
void drawLayoutChoice(DeskPanel &panel,meter_ui::Rect r,unsigned index,bool current){
 meter_design::frame(panel,r,current?meter_design::RAISED:BUTTON,current?GREEN:meter_design::BORDER);
 const bool values=dropdownAction==VALUE_LAYOUT;const unsigned count=values?meter_ui::valuePresetCount(meter_ui::ValuePreset(options[index].value),meter_display::count()):meter_display::count();
 const int iconWidth=std::min(88,r.w/2-12);const meter_ui::Rect icon{r.x+12,r.y+(r.h-48)/2,iconWidth,48};
 String caption;if(values){const char *names[]={"Solo","Equal","Main + 1","Main + 2","2 big + 1","Grid 4"};caption=names[options[index].value];if(options[index].value==int(meter_ui::ValuePreset::Equal))caption+=" "+String(count);valueLayoutPreview(panel,icon,meter_ui::ValuePreset(options[index].value),count);}else{caption=options[index].value?"Together":"Separate";chartLayoutPreview(panel,icon,options[index].value!=0);}
 const int x=icon.x+icon.w+12,font=r.w>=200?2:1;text(panel,x,r.y+18,caption,font,current?GREEN:FG);text(panel,x,r.y+45,String(count)+(count==1?" meter":" meters"),1,MUTED);
 if(current){const int x=r.x+r.w-18,y=r.y+8;panel.drawLine(x,y+3,x+3,y+6,GREEN);panel.drawLine(x+3,y+6,x+10,y-1,GREEN);}
}
void drawDropdown(DeskPanel &panel){
 if(!dropdown.isOpen())return;const auto &g=dropdownLayout;meter_design::frame(panel,g.box,BUTTON,meter_design::BORDER);
 const unsigned font=g.box.w>=280?2:1;
 wrapped(panel,g.header.x+10,g.header.y+6,g.cancel.x-g.header.x-20,dropdownTitle,2,font,FG);text(panel,g.cancel.x+8,g.cancel.y+(g.cancel.h-8*font)/2,"Cancel",font,GREEN);
 for(unsigned i=0;i<g.visible;++i){const unsigned index=dropdown.first()+i;if(index>=options.size())break;const auto r=g.row(i);const bool current=int(index)==dropdown.current();if(dropdownAction==VALUE_LAYOUT||dropdownAction==CHART_LAYOUT){drawLayoutChoice(panel,r,index,current);continue;}panel.fillRect(r.x+1,r.y+1,r.w-2,r.h-2,current?meter_design::RAISED:BUTTON);if(current)text(panel,r.x+5,r.y+(r.h-8*font)/2,"*",font,GREEN);const unsigned lines=font==2?2:3;const int width=r.w-34;const bool multiline=displayText(options[index].label).length()>unsigned(width/(6*font));wrapped(panel,r.x+24,r.y+(multiline?6:(r.h-8*font)/2),width,options[index].label,lines,font,current?GREEN:FG);}
 if(g.scrollable){const int thumb=std::max(12,g.list.h*int(g.visible)/int(options.size())),span=g.list.h-thumb,y=g.list.y+span*int(dropdown.first())/std::max(1,int(options.size()-g.visible));panel.fillRect(g.list.x+g.list.w-3,g.list.y,2,g.list.h,meter_design::BORDER);panel.fillRoundRect(g.list.x+g.list.w-3,std::min(g.list.y+span,y),2,thumb,1,MUTED);}
}
void tap(uint16_t x,uint16_t y){
    if(dropdown.isOpen()){const auto &g=dropdownLayout;if(g.cancel.contains(x,y)||layout.back.contains(x,y)||!g.box.contains(x,y)){closeDropdown();return;}const int row=g.indexAt(x,y);if(row>=0)chooseOption(unsigned(row));return;}

    if(layout.back.contains(x,y)){goBack();return;}
    const int slot=layout.tabs.hit(x,y);
    if(slot>=0){
        if(page==Page::Values){selectFormatContext(unsigned(slot),formatChannel);message(formatDirty?"Unsaved preview restored for this meter/channel":"Number format meter selected");}
        else if(page==Page::Connections){meter_ble::focus(unsigned(slot));message(String("M")+String(slot+1)+" selected; Connect starts only when pressed");}
        scroll=0;gesture=ut_meter::TouchGesture();return;
    }
    if(page==Page::Keyboard){
        const auto keys=layout.keyboard();const int hit=keys.hit(x,y);
        if(hit>=0&&hit<4){const int ids[]={KEY_LEFT,KEY_RIGHT,KEY_SPACE,KEY_LF};execute(ids[hit]);}
        else if(hit>=4&&hit<20){int code=keys.code(keyPage,hit-4);if(code>=0)insert(char(code));}
        else if(hit>=20){const int ids[]={KEY_PREVIOUS,KEY_NEXT,KEY_BACKSPACE,KEY_DONE};execute(ids[hit-20]);}
        return;
    }
    if(page==Page::Qr)return;
    const int index=layout.indexAt(x,y,scroll,rows.size());
    if(index>=0&&rows[index].enabled&&rows[index].action!=NONE)execute(rows[index].action);
}
}

bool begin(){
    if(started)return true;
    if(!readings){void *readingMemory=heap_caps_calloc(meter_ble::MAX_CONNECTIONS,sizeof(*readings),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!readingMemory)readingMemory=heap_caps_calloc(meter_ble::MAX_CONNECTIONS,sizeof(*readings),MALLOC_CAP_8BIT);
    if(!readingMemory){message("No memory for calculation readings");return false;}readings=static_cast<StoredReading(*)[8]>(readingMemory);for(unsigned s=0;s<meter_ble::MAX_CONNECTIONS;++s)for(unsigned c=0;c<8;++c)new(&readings[s][c]) StoredReading;}

    if(started)return true;
    cacheMutex=xSemaphoreCreateMutex();requests=xQueueCreate(QUEUE_DEPTH,sizeof(Request*));responses=xQueueCreate(QUEUE_DEPTH+1,sizeof(Response*));
    if(!cacheMutex||!requests||!responses){message("Native API worker allocation failed");return false;}
    bootSource=(uint64_t(esp_random())<<32)|esp_random();rows.reserve(ROW_LIMIT);
    TaskHandle_t task=nullptr;
    started=xTaskCreate(worker,"meter-touch-api",8192,nullptr,1,&task)==pdPASS;
    if(!started)message("Native API worker could not start");return started;
}
void openSelector(Selector type,int returnView){
 liveMenu=false;valueMenu=false;menuBack=Page::Home;
 active.store(true);if(type==Selector::Device||type==Selector::Profile||type==Selector::Meter)setPage(Page::Connections);else setPage(Page::Scope);
 dropdownReturn=returnView;const int targets[]={SCOPE_METER,DISPLAY_COUNT,SCOPE_CHANNEL,VALUE_LAYOUT,BT_DEVICE,BT_PROFILE};openChoice(targets[unsigned(type)]);
 if(!dropdown.isOpen()&&returnView>=0)closeDropdown();
}
void openConnections(){liveMenu=false;valueMenu=false;menuBack=Page::Home;dropdown.cancel();dropdownReturn=-1;options.clear();active.store(true);setPage(Page::Connections);}
void openValues(){liveMenu=false;valueMenu=false;menuBack=Page::Home;dropdown.cancel();dropdownReturn=-1;options.clear();active.store(true);showValueSettings();}
void open(){liveMenu=false;valueMenu=false;menuBack=Page::Home;dropdown.cancel();dropdownReturn=-1;options.clear();active.store(true);setPage(Page::Home);message(started?"Choose a function; swipe to scroll":"Native API worker unavailable; calculations/help still available");}
void openValueMenu(){liveMenu=false;valueMenu=true;menuBack=Page::Quick;dropdown.cancel();dropdownReturn=-1;options.clear();active.store(true);setPage(Page::Quick);message("Swipe to scroll; Back returns to live values");}
void openEdgeMenu(ut_meter::TouchEdge edge){
    if(edge==ut_meter::TouchEdge::None)return;
    if(edge==ut_meter::TouchEdge::Left)openConnections();else open();
    if(edge==ut_meter::TouchEdge::Top)setPage(Page::Screen);
    liveMenu=true;menuBack=page;gesture=ut_meter::TouchGesture();message("Swipe to scroll; Back returns to your live view");
}
void rotationChanged(){
    // Discard only popup geometry and touch state. Fields, keyboard text,
    // per-slot drafts and queued operation IDs retain their original identity.
    dropdown.cancel();dropdownReturn=-1;options.clear();dropdownDevices.clear();dropdownLayout={};
    gesture=ut_meter::TouchGesture();scroll=previousScroll=0;
    const bool sideways=meter_display::rotation()%2;layout.resolve(sideways?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH,sideways?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT);
    keyPage%=layout.keyboard().pages();redraw=true;
}
void close(){
    rememberFormatDraft();liveMenu=false;valueMenu=false;menuBack=Page::Home;
    dropdown.cancel();dropdownReturn=-1;options.clear();
    active.store(false);gesture=ut_meter::TouchGesture();redraw=true;
    // Leaving a modal cancels its unaccepted text/confirmation. Keep queued
    // operations and their completion IDs intact so closing cannot replay them.
    editTarget=nullptr;editText="";editCaption="";editCursor=editMaximum=0;editSecret=false;
    formatConfirmation="";confirmKind=0;page=Page::Home;scroll=0;
}
bool isOpen(){return active.load();}
bool autoRotationBlocked(){return blocksAutoRotation(isOpen(),page==Page::Keyboard,page==Page::Confirm,dropdown.isOpen());}
const char *pageName(){switch(page){case Page::Home:return "Functions";case Page::Quick:return "Value controls";case Page::Screen:return "Screen controls";case Page::Network:return "Wi-Fi";case Page::Recording:return "Recording";case Page::Card:return "TF card";case Page::File:return "TF file";case Page::Rules:return "Triggers";case Page::Rule:return "Trigger form";case Page::Calculations:return "Power / battery";case Page::CalcSettings:return "Calculation setup";case Page::Scope:return "Scope controls";case Page::Values:return "Values / Format";case Page::Events:return "Events";case Page::Diagnostics:return "Diagnostics";case Page::Help:return "Help";case Page::Web:return "Open web";case Page::Text:return "Details";case Page::Qr:return "QR link";case Page::Confirm:return "Confirm";case Page::Keyboard:return "Keyboard";case Page::Connections:return "Meters / Bluetooth";case Page::Time:return "Date / Time / NTP";}return "Functions";}
void draw(DeskPanel &panel){
    redraw=false;
    const int height=panel.height(),width=panel.width();layout.resolve(width,height);
    panel.setTextWrap(false);panel.fillScreen(BG);
    const String title=page==Page::Qr?qrCaption:String(pageName());
    header(panel,title);
    if(page==Page::Keyboard)keyboard(panel);
    else if(page==Page::Qr)qr(panel);
    else {
        buildRows();
        const auto &spec=ui.advanced.row;
        const int padding=layout.tx.xLength(spec.padding),titleY=layout.tx.yLength(spec.title_y),detailY=layout.tx.yLength(spec.detail_y);
        for(unsigned i=0;i<rows.size();++i){
            const auto r=layout.rect(i,scroll);if(!layout.visible(i,scroll).h)continue;const auto &row=rows[i];
            if(meter_design::modern)modernRow(panel,r,row);
            else if(row.numeric){panel.fillRoundRect(r.x,r.y,r.w,r.h,6,BUTTON);text(panel,r.x+padding,r.y+titleY,row.title,1,MUTED);const auto out=meter_ui::formatNumber(row.value,row.format);meter_value_view::number(panel,{r.x+padding,r.y+detailY,r.w-2*padding,std::max(8,r.h-detailY-4)},out,row.format,FG,30);}
            else if(row.info)wrapped(panel,r.x+padding,r.y+titleY,r.w-2*padding,row.title,std::max(1,(r.h-titleY)/(8*layout.tx.font(spec.info_size))),layout.tx.font(spec.info_size),row.color);
            else {panel.fillRoundRect(r.x,r.y,r.w,r.h,6,BUTTON);const uint16_t color=row.enabled?row.color:MUTED;wrapped(panel,r.x+padding,r.y+titleY,r.w-2*padding,row.title,r.h<60&&width>height?1:2,layout.tx.font(spec.title_size),color);wrapped(panel,r.x+padding,r.y+detailY,r.w-2*padding,row.detail,1,layout.tx.font(spec.detail_size),MUTED);if(row.action==DISPLAY_COUNT||row.action==SCREEN_ROTATION||row.action==VALUE_LAYOUT||row.action==SCOPE_METER||row.action==SCOPE_CHANNEL||row.action==SCOPE_WINDOW||row.action==CHART_LAYOUT||row.action==FORMAT_SLOT||row.action==FORMAT_CHANNEL||row.action==FORMAT_DIGITS||row.action==FORMAT_DECIMALS||row.action==FORMAT_PADDING||row.action==BT_SLOT||row.action==BT_DEVICE||row.action==BT_PROFILE||row.action==NTP_ENABLED||row.action==NTP_SERVER||(row.action>=4000&&row.action<=4002)||(row.action>=5000&&row.action<=5005))text(panel,r.x+r.w-16,r.y+titleY,"v",1,GREEN);}
        }
        panel.fillRect(0,0,width,layout.top,BG);panel.fillRect(0,layout.bottom,width,height-layout.bottom,BG);
        header(panel,title);meterTabs(panel);scrollIndicator(panel);
    }
    mappedText(panel,ui.advanced.message,notice.substring(0,std::max(1,layout.width/(6*std::max(1,layout.tx.font(ui.advanced.message.size))))),(online||page==Page::Calculations)?MUTED:WARN);
    drawDropdown(panel);
}
void contact(uint16_t x,uint16_t y,bool down){
    const bool scrollable=dropdown.isOpen()?dropdownLayout.list.contains(x,y):page!=Page::Keyboard&&page!=Page::Qr&&y>=layout.top&&y<layout.bottom;
    auto action=gesture.update(x,y,down,scrollable,millis());
    if(action.deltaY){if(dropdown.isOpen())dropdown.scrollBy(action.deltaY);else scroll=scrollOffset(scroll,action.deltaY,layout.contentHeight(int(rows.size())),layout.bottom-layout.top);redraw=true;}
    if(action.tap){tap(action.x,action.y);redraw=true;}
}
void resetContact(){gesture=ut_meter::TouchGesture();}
void tick(){
    static uint32_t checked=0;
    if(uint32_t(millis()-checked)>=200){checked=millis();updateBattery();}
    if(!started)return;
    Response *response=nullptr;
    for(unsigned i=0;i<QUEUE_DEPTH+1&&xQueueReceive(responses,&response,0)==pdTRUE;++i)if(response){applyResponse(response->id,response->code,response->payload,response->message);delete response;response=nullptr;}
    if(!isOpen()||xSemaphoreTake(cacheMutex,0)!=pdTRUE)return;
    String changed[5],result,messageText;bool dirty[5]={};int code=0;uint32_t job=0;
    for(unsigned i=0;i<5;++i)if(versions[i]!=sharedCache.versions[i]){versions[i]=sharedCache.versions[i];changed[i]=sharedCache.values[i];dirty[i]=true;}
    online=sharedCache.online&&apiValidated.load();statusAt=sharedCache.statusAt;
    if(lastJob<sharedCache.job){job=sharedCache.job;result=sharedCache.response;messageText=sharedCache.message;code=sharedCache.code;}
    xSemaphoreGive(cacheMutex);
    JsonDocument *docs[]={&statusDoc,&scanDoc,&rulesDoc,&filesDoc,&eventsDoc};
    for(unsigned i=0;i<5;++i)if(dirty[i]){redraw=true;Document candidate;auto error=deserializeJson(candidate,changed[i],DeserializationOption::NestingLimit(10));if(error||(i==0&&!validStatus(candidate))){if(i==0){online=false;statusAt=0;apiValidated.store(false);}message("Native JSON status rejected; actions disabled until refresh");}else *docs[i]=std::move(candidate);}
    if(job)applyResponse(job,code,result,messageText);
}
void sample(const meter_ble::Event &event){
    if(!readings||event.slot>=meter_ble::MAX_CONNECTIONS||event.reading.channel>=8)return;
    auto &stored=readings[event.slot][event.reading.channel];stored.value=event.reading.value;stored.numeric=event.reading.numeric;stored.flags=event.reading.flags;stored.at=event.at;
    stored.modeKey=event.reading.modeKey;stored.channelPacket=event.channelPacket;
    snprintf(stored.unit,sizeof(stored.unit),"%s",event.reading.unit);snprintf(stored.mode,sizeof(stored.mode),"%s",event.reading.mode);stored.seq=event.packet;
    snprintf(stored.address,sizeof(stored.address),"%s",event.address);snprintf(stored.profile,sizeof(stored.profile),"%s",event.profile);
    stored.time=uint64_t(esp_timer_get_time()/1000)-uint32_t(millis()-event.at);
    uint64_t hash=1469598103934665603ULL^bootSource;auto hashByte=[&](uint8_t b){hash^=b;hash*=1099511628211ULL;};
    for(const char *s=event.address;*s;++s)hashByte(*s);for(const char *s=event.profile;*s;++s)hashByte(*s);hashByte(event.slot);
    const uint64_t identity=hash;
    if(packetSource[event.slot]==identity&&lastPacket[event.slot]){
        if(event.packet>lastPacket[event.slot]&&event.packet-lastPacket[event.slot]>1)pendingLoss[event.slot]=true;
        else if(event.packet<=lastPacket[event.slot])++packetEpoch[event.slot];
    }else packetEpoch[event.slot]=0;
    packetSource[event.slot]=identity;lastPacket[event.slot]=event.packet;
    hashByte(event.reading.channel);for(unsigned i=0;i<4;++i){hashByte(event.reading.modeKey>>(8*i));hashByte(packetEpoch[event.slot]>>(8*i));}stored.source=hash;stored.have=true;
    if((event.slot==voltageMeter&&event.reading.channel==voltageChannel)||(event.slot==currentMeter&&event.reading.channel==currentChannel))updateBattery();
}
unsigned scopeWindowSeconds(){return scopeSeconds;}
int scopeChannel(unsigned slot){return slot<meter_ble::MAX_CONNECTIONS?scopeChannels[slot]:-1;}
bool takeValuesRequest(){bool value=valuesRequested;valuesRequested=false;return value;}
bool takeScopeRequest(){bool value=scopeRequested;scopeRequested=false;return value;}
bool takeClearScopeRequest(){bool value=clearScopeRequested;clearScopeRequested=false;return value;}
bool takeDrawRequest(){bool value=redraw;redraw=false;return value;}
bool command(const String &line){
    if(line=="view functions"||line=="view advanced"||line=="ui open"){open();return true;}
    if(line=="ui geometry"){
        const bool sideways=meter_display::rotation()%2;layout.resolve(sideways?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH,sideways?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT);
        const auto viewport=layout.viewport();Serial.printf("ui_geometry canvas=%dx%d rotation=%u page=%s viewport=%d,%d,%d,%d scroll=%d footer_buttons=0\n",layout.tx.width,layout.tx.height,meter_display::rotation(),pageName(),viewport.x,viewport.y,viewport.w,viewport.h,scroll);
        Serial.printf("ui_back=%d,%d,%d,%d\n",layout.back.x,layout.back.y,layout.back.w,layout.back.h);
        if(layout.tabs.columns)for(unsigned i=0;i<meter_ble::MAX_CONNECTIONS;++i){const auto tab=layout.tabs.rect(i);Serial.printf("ui_tab=%u rect=%d,%d,%d,%d selected=%u\n",i,tab.x,tab.y,tab.w,tab.h,i==(page==Page::Values?formatSlot:meter_ble::activeSlot()));}
        if(dropdown.isOpen())Serial.printf("ui_popup=%d,%d,%d,%d list=%d,%d,%d,%d first=%u visible=%u columns=%u\n",dropdownLayout.box.x,dropdownLayout.box.y,dropdownLayout.box.w,dropdownLayout.box.h,dropdownLayout.list.x,dropdownLayout.list.y,dropdownLayout.list.w,dropdownLayout.list.h,dropdown.first(),dropdownLayout.visible,dropdownLayout.columns);
        Serial.println("ui_geometry_end");Serial.flush();return true;
    }
    if(line=="ui status"){Serial.printf("touch_app=%u page=%s scroll=%d rows=%u keyboard=%u chars=%u cursor=%u api=%u queue=%u battery_running=%u\n",isOpen(),pageName(),scroll,unsigned(rows.size()),page==Page::Keyboard,editText.length(),editCursor,online,requests?unsigned(uxQueueMessagesWaiting(requests)):0,battery.summary().running);if(!online&&cacheMutex&&xSemaphoreTake(cacheMutex,pdMS_TO_TICKS(10))==pdTRUE){String detail=sharedCache.message;xSemaphoreGive(cacheMutex);Serial.printf("ui_api_message=%s\n",detail.c_str());}if(dropdown.isOpen())Serial.printf("ui_dropdown=1 first=%u count=%u title=%s\n",dropdown.first(),unsigned(options.size()),dropdownTitle.c_str());return true;}
    if(!isOpen()&&(line=="ui back"||line=="ui done"||line.startsWith("ui tap ")||line.startsWith("ui text ")||line.startsWith("ui scroll "))){Serial.println("Native UI closed; interactive command ignored");return true;}
    if(line=="ui back"){goBack();return true;}
    if(line.startsWith("ui tap ")){unsigned x=0,y=0;if(sscanf(line.c_str(),"ui tap %u %u",&x,&y)==2)tap(x,y);return true;}
    if(line.startsWith("ui scroll ")){if(dropdown.isOpen())dropdown.scrollBy(line.substring(10).toInt());else scroll=scrollOffset(scroll,line.substring(10).toInt(),layout.contentHeight(int(rows.size())),layout.bottom-layout.top);return true;}
    if(line.startsWith("ui text ")&&page==Page::Keyboard){String value=line.substring(8);value.replace("\\n","\n");if(value.length()>editMaximum)message("Serial text exceeds field maximum");else {editText=value;editCursor=value.length();}return true;}
    if(line=="ui done"&&page==Page::Keyboard){execute(KEY_DONE);return true;}
    if(line.startsWith("ui action ")){int action=line.substring(10).toInt();if(isOpen()){if(dropdown.isOpen()){if(action>=int(dropdown.first()))chooseOption(unsigned(action-int(dropdown.first())));}else{buildRows();if(action>=0&&action<int(rows.size())&&rows[action].enabled)execute(rows[action].action);}}return true;}
    if(line=="ui rows"){if(dropdown.isOpen()){for(unsigned i=0;i<options.size();++i)Serial.printf("ui_row=%u action=%d enabled=%u label=%s\n",i,12000+options[i].value,i>=dropdown.first()&&i<dropdown.first()+dropdownLayout.visible,options[i].label.c_str());Serial.printf("ui_rows_end=%u\n",unsigned(options.size()));Serial.flush();return true;}buildRows();for(unsigned i=0;i<rows.size();++i)Serial.printf("ui_row=%u action=%d enabled=%u label=%s\n",i,rows[i].action,rows[i].enabled,rows[i].title.c_str());Serial.printf("ui_rows_end=%u\n",unsigned(rows.size()));Serial.flush();return true;}
    return false;
}
}
