#include "meter_ntp.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/ip_addr.h>
#include <cstdio>
#include <sys/time.h>

namespace meter_ntp {
namespace {
enum class CommandKind:uint8_t { Configure,Sync };
struct Command { CommandKind kind=CommandKind::Sync;Config config; };
enum class EventKind:uint8_t { Accepted,InvalidTime,SetTimeFailed };
struct Event { uint64_t unixMs=0,uptimeMs=0;uint32_t generation=0;EventKind kind=EventKind::Accepted; };
portMUX_TYPE stateLock=portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t commands=nullptr,events=nullptr,anchors=nullptr;
Snapshot state;
TimeState clockState;
SyncCallback applicationCallback=nullptr;
bool started=false,stationUp=false;
unsigned pendingCommands=0,pendingConfigurations=0;
uint32_t acceptingGeneration=0;
char activeServer[serverBytes]={}; // lwIP retains this pointer until stop.

uint64_t uptime(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
template<size_t N>void copy(char (&out)[N],const char *in){snprintf(out,N,"%s",in?in:"");}
uint32_t ipNumber(const IPAddress &ip){return (uint32_t(ip[0])<<24)|(uint32_t(ip[1])<<16)|(uint32_t(ip[2])<<8)|ip[3];}
void publishError(const char *saving,const char *sync){
    portENTER_CRITICAL(&stateLock);
    if(*saving&&*sync)snprintf(state.error,sizeof(state.error),"%.90s; %.96s",saving,sync);
    else copy(state.error,*saving?saving:sync);
    portEXIT_CRITICAL(&stateLock);
}
bool enqueue(const Command &command){
    if(!commands||!started)return false;
    portENTER_CRITICAL(&stateLock);++pendingCommands;if(command.kind==CommandKind::Configure)++pendingConfigurations;portEXIT_CRITICAL(&stateLock);
    if(xQueueSend(commands,&command,0)==pdTRUE)return true;
    portENTER_CRITICAL(&stateLock);--pendingCommands;if(command.kind==CommandKind::Configure)--pendingConfigurations;portEXIT_CRITICAL(&stateLock);
    return false;
}
void stopService(){
    portENTER_CRITICAL(&stateLock);acceptingGeneration=0;portEXIT_CRITICAL(&stateLock);
    // Thread-safe wrappers may wait for lwIP; only this worker calls them.
    if(esp_sntp_enabled())esp_sntp_stop();
    xQueueReset(events);
    portENTER_CRITICAL(&stateLock);state.active=false;state.syncing=false;portEXIT_CRITICAL(&stateLock);
}
bool startService(const Config &config,uint32_t generation){
    stopService();
    copy(activeServer,config.server);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_sync_interval(3600000);
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
#if LWIP_DHCP_GET_NTP_SRV
    esp_sntp_servermode_dhcp(false);
#endif
    const ip_addr_t empty={};
    for(unsigned i=0;i<SNTP_MAX_SERVERS;++i){esp_sntp_setservername(i,nullptr);esp_sntp_setserver(i,&empty);}
    uint32_t numeric=0;ip_addr_t address={};
    if(parseIpv4(activeServer,numeric)&&ipaddr_aton(activeServer,&address))esp_sntp_setserver(0,&address);
    else esp_sntp_setservername(0,activeServer);
    portENTER_CRITICAL(&stateLock);acceptingGeneration=generation;state.active=true;state.syncing=true;portEXIT_CRITICAL(&stateLock);
    esp_sntp_init();
    if(esp_sntp_enabled())return true;
    stopService();return false;
}
void worker(void *){
    Preferences preferences;
    Config config;
    bool opened=preferences.begin("meter-ntp",false),durable=false;
    char saveError[128]={},syncError[128]={};
    if(!opened)copy(saveError,"NTP preferences unavailable; defaults are in RAM");
    else if(preferences.isKey("config")){
        uint8_t bytes[configBytes];
        durable=preferences.getBytesLength("config")==sizeof(bytes)&&preferences.getBytes("config",bytes,sizeof(bytes))==sizeof(bytes)&&decodeConfig(bytes,sizeof(bytes),config);
        if(!durable)copy(saveError,"Saved NTP configuration corrupt/unreadable; defaults are in RAM");
    }
    portENTER_CRITICAL(&stateLock);state.enabled=config.enabled;copy(state.server,config.server);state.saved=durable;state.ready=true;state.waitingForNetwork=config.enabled;portEXIT_CRITICAL(&stateLock);
    publishError(saveError,syncError);
    bool active=false,restart=false,attemptPending=false;
    uint32_t generation=0;
    uint64_t attemptStarted=0,retryNotBefore=0;
    for(;;){
        Command command;
        for(unsigned processed=0;processed<4&&xQueueReceive(commands,&command,0)==pdTRUE;++processed){
            if(command.kind==CommandKind::Configure){
                stopService();active=false;attemptPending=false;restart=true;
                config=command.config;saveError[0]=0;syncError[0]=0;
                portENTER_CRITICAL(&stateLock);state.enabled=config.enabled;copy(state.server,config.server);state.saved=false;portEXIT_CRITICAL(&stateLock);
                uint8_t bytes[configBytes],check[configBytes];
                if(!opened)opened=preferences.begin("meter-ntp",false);
                durable=opened&&encodeConfig(config,bytes,sizeof(bytes))&&preferences.putBytes("config",bytes,sizeof(bytes))==sizeof(bytes)&&preferences.getBytesLength("config")==sizeof(check)&&preferences.getBytes("config",check,sizeof(check))==sizeof(check)&&!memcmp(bytes,check,sizeof(check));
                if(!durable)copy(saveError,"Saving NTP settings failed readback; changes are in RAM");
                portENTER_CRITICAL(&stateLock);state.saved=durable;portEXIT_CRITICAL(&stateLock);
            }else if(config.enabled){restart=true;syncError[0]=0;}
            else copy(syncError,"NTP is disabled; enable it before requesting sync");
            portENTER_CRITICAL(&stateLock);--pendingCommands;if(command.kind==CommandKind::Configure)--pendingConfigurations;portEXIT_CRITICAL(&stateLock);
        }
        bool connected;
        portENTER_CRITICAL(&stateLock);connected=stationUp;portEXIT_CRITICAL(&stateLock);
        const bool localPeer=!connected&&localApServer(config.server,ipNumber(WiFi.softAPIP()),ipNumber(WiFi.softAPSubnetMask()));
        const bool canRun=config.enabled&&(connected||localPeer);
        portENTER_CRITICAL(&stateLock);state.waitingForNetwork=config.enabled&&!canRun;portEXIT_CRITICAL(&stateLock);
        if(!canRun){
            if(active){stopService();active=false;attemptPending=false;}
            retryNotBefore=0;
        }else if(restart||(!active&&uptime()>=retryNotBefore)){
            // Zero is reserved for rejecting callbacks while stopped.
            if(++generation==0)++generation;
            active=startService(config,generation);restart=false;attemptPending=active;attemptStarted=uptime();syncError[0]=0;
            if(!active){retryNotBefore=attemptStarted+15000;copy(syncError,"NTP service could not start; retrying in 15 s");}
        }
        Event event;
        if(xQueueReceive(events,&event,0)==pdTRUE&&active&&event.generation==generation){
            if(event.kind==EventKind::Accepted){
                portENTER_CRITICAL(&stateLock);
                clockState.accept(int64_t(event.unixMs/1000),int32_t((event.unixMs%1000)*1000),event.uptimeMs);
                state.synced=clockState.synced;state.lastSyncUnixMs=clockState.lastSyncUnixMs;state.lastSyncUptimeMs=clockState.lastSyncUptimeMs;copy(state.lastSyncServer,activeServer);state.syncing=false;
                portEXIT_CRITICAL(&stateLock);
                xQueueOverwrite(anchors,&event);attemptPending=false;syncError[0]=0;
            }else{
                copy(syncError,event.kind==EventKind::InvalidTime?"NTP returned an invalid time outside 2020..2100":"NTP time could not be applied to the system clock");
                portENTER_CRITICAL(&stateLock);state.syncing=false;portEXIT_CRITICAL(&stateLock);attemptPending=false;
            }
        }
        if(active&&attemptPending&&uptime()-attemptStarted>=30000){
            copy(syncError,"No valid NTP response within 30 s; retrying automatically");attemptPending=false;
            portENTER_CRITICAL(&stateLock);state.syncing=false;portEXIT_CRITICAL(&stateLock);
        }
        publishError(saveError,syncError);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
}

bool begin(SyncCallback callback){
    if(started)return true;
    applicationCallback=callback;
    commands=xQueueCreate(4,sizeof(Command));events=xQueueCreate(1,sizeof(Event));anchors=xQueueCreate(1,sizeof(Event));
    if(!commands||!events||!anchors){
        if(commands)vQueueDelete(commands);
        if(events)vQueueDelete(events);
        if(anchors)vQueueDelete(anchors);
        commands=events=anchors=nullptr;publishError("NTP queue allocation failed","");return false;
    }
    started=true;
    if(xTaskCreate(worker,"meter-ntp",6144,nullptr,1,nullptr)!=pdPASS){
        started=false;vQueueDelete(commands);vQueueDelete(events);vQueueDelete(anchors);commands=events=anchors=nullptr;publishError("NTP worker could not start","");return false;
    }
    return true;
}
void tick(bool stationConnected){
    portENTER_CRITICAL(&stateLock);stationUp=stationConnected;portEXIT_CRITICAL(&stateLock);
    Event event;
    if(anchors&&applicationCallback&&xQueueReceive(anchors,&event,0)==pdTRUE){
        const uint64_t now=uptime();
        if(now>=event.uptimeMs&&now-event.uptimeMs<=maxUnixMs-event.unixMs)applicationCallback(event.unixMs+(now-event.uptimeMs));
    }
}
bool configure(bool enabled,const char *server,String &error){
    if(!validServer(server)){error="Use a DNS hostname or unicast IPv4 address without a URL, port or spaces";return false;}
    Command command;command.kind=CommandKind::Configure;command.config.enabled=enabled;copy(command.config.server,server);
    if(!enqueue(command)){error="NTP settings queue unavailable or busy";return false;}
    error="";return true;
}
bool requestSync(){return enqueue(Command{});}
Snapshot snapshot(){
    const uint64_t now=uptime();
    portENTER_CRITICAL(&stateLock);Snapshot out=state;out.pending=pendingCommands!=0;out.saved=out.saved&&pendingConfigurations==0;out.currentUnixMs=clockState.current(now);portEXIT_CRITICAL(&stateLock);
    return out;
}

// Called on the lwIP task by the documented weak sntp_sync_time override.
// No allocation, flash, network or application callback runs here.
void receiveTime(timeval *value){
    uint32_t generation;
    portENTER_CRITICAL(&stateLock);generation=acceptingGeneration;portEXIT_CRITICAL(&stateLock);
    if(!events||!generation)return;
    Event event;event.generation=generation;event.uptimeMs=uptime();
    TimeState checked;
    if(!value||value->tv_usec<0||value->tv_usec>=1000000||!checked.accept(int64_t(value->tv_sec),int32_t(value->tv_usec),event.uptimeMs))event.kind=EventKind::InvalidTime;
    else if(settimeofday(value,nullptr)!=0)event.kind=EventKind::SetTimeFailed;
    else {event.unixMs=checked.lastSyncUnixMs;esp_sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);}
    xQueueOverwrite(events,&event);
}
}

// IDF 4.4 documents this hook specifically for replacing the default clock
// update. Validating a notification alone would run after settimeofday().
extern "C" void sntp_sync_time(struct timeval *value){meter_ntp::receiveTime(value);}
