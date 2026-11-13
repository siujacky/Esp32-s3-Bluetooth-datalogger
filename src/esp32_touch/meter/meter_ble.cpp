#include "meter_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <new>
#include <string.h>
#include <meter_peer_preferences.h>

namespace meter_ble {
enum class Action : uint8_t { Scan,Connect,Disconnect,Profile };
struct Command {Action action;unsigned slot;Candidate target;char profile[32]={};};
struct Chunk {uint32_t generation,at;uint16_t size;uint8_t bytes[512];};
using Saved=meter_ui::PeerPreferences;
struct Context {
    unsigned slot;
    QueueHandle_t commands=nullptr,chunks=nullptr;
    NimBLEClient *client=nullptr;
    dmm::AgedDecoder *decoder=nullptr;
    bool ready=false;
};
static Context contexts[MAX_CONNECTIONS];
static Connection *links=nullptr;
static Candidate discovered[MAX_METERS];
static unsigned discoveredCount=0,focused=0;
static unsigned preferredChannel[MAX_CONNECTIONS]={};
static bool scanning=false,explicitProfile[MAX_CONNECTIONS]={},slotMessage[MAX_CONNECTIONS]={};
static uint8_t pending[MAX_CONNECTIONS]={};
static bool stopPending[MAX_CONNECTIONS]={},stopRequested[MAX_CONNECTIONS]={};
static char discoveryMessage[80]="Enable meter Bluetooth; tap Scan";
static QueueHandle_t commands=nullptr,events=nullptr;
static SemaphoreHandle_t connectMutex=nullptr;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static Saved saved{};
static bool peerDurable=false;static char peerError[96]={};
static NimBLEScan *radio=nullptr;

template<size_t N> static void copy(char (&out)[N],const char *text) {strncpy(out,text?text:"",N-1);out[N-1]=0;}
static void state(unsigned slot,State s,const char *message) {
    if(!links)return;
    portENTER_CRITICAL(&mux);links[slot].state=s;copy(links[slot].message,message);slotMessage[slot]=true;portEXIT_CRITICAL(&mux);
}
// Rejected control requests must not disguise an existing live connection.
static void notice(unsigned slot,const char *message) {
    if(!links)return;
    portENTER_CRITICAL(&mux);copy(links[slot].message,message);slotMessage[slot]=true;portEXIT_CRITICAL(&mux);
}
static void finishCommand(unsigned slot,Action action) {
    portENTER_CRITICAL(&mux);
    if(pending[slot])--pending[slot];
    if(action==Action::Disconnect){stopPending[slot]=false;stopRequested[slot]=false;}
    portEXIT_CRITICAL(&mux);
}
static bool stopping(unsigned slot) {
    portENTER_CRITICAL(&mux);bool result=stopRequested[slot];portEXIT_CRITICAL(&mux);return result;
}
static unsigned profileIndex(const char *id) {
    for(unsigned i=0;i<dmm::profileCount();++i)if(!strcmp(dmm::profileAt(i)->id,id))return i;
    return 0;
}
ut_meter::Reading displayReading(const dmm::Reading &r) {
    ut_meter::Reading out;out.value=r.value;out.numeric=r.numeric;out.modeKey=r.modeKey;out.hasMetadata=true;
    out.mode=uint8_t(r.modeKey>>8);out.channel=r.channel;out.range=r.range;
    copy(out.text,r.text);copy(out.unitLabel,r.unit);copy(out.modeLabel,r.mode);
    if(r.flags&dmm::HOLD)out.flags[0]|=2;if(r.flags&dmm::REL)out.flags[0]|=1;
    if(r.flags&dmm::MIN)out.flags[0]|=4;if(r.flags&dmm::MAX)out.flags[0]|=8;
    if(r.flags&dmm::CAUTION)out.flags[1]|=1;if(r.flags&dmm::LOW_BATT)out.flags[1]|=2;
    if(r.flags&dmm::MANUAL)out.flags[1]|=4;
    if(r.flags&dmm::PEAK_MIN)out.flags[2]|=2;if(r.flags&dmm::PEAK_MAX)out.flags[2]|=4;
    return out;
}
View view() {
    View out;if(!links){out.state=State::Error;copy(out.message,"No memory for connection state");return out;}
    portENTER_CRITICAL(&mux);
    const auto &slot=links[focused];out.activeSlot=focused;out.count=discoveredCount;
    memcpy(out.meters,discovered,sizeof(discovered));out.state=slot.state;out.scanning=scanning;
    if(out.state==State::Idle && scanning)out.state=State::Scanning;
    copy(out.profile,slot.profile);copy(out.peer,slot.peer.address);
    out.model=!strcmp(slot.profile,"ut60bt")?ut_meter::Model::UT60BT:ut_meter::Model::UT61Plus;
    copy(out.message,scanning?"Looking for meters (5s)":slot.state==State::Idle && !slotMessage[focused]?discoveryMessage:slot.message);
    const unsigned channel=displayChannel(slot.hasReading,slot.readingAt,preferredChannel[focused],-1,millis());
    out.hasReading=slot.hasReading[channel];out.readingAt=slot.readingAt[channel];
    if(out.hasReading)out.reading=displayReading(slot.readings[channel]);
    out.packets=slot.packets;out.bytes=slot.bytes;out.dropped=slot.dropped;
    portEXIT_CRITICAL(&mux);
    out.profileIndex=profileIndex(out.profile);
    return out;
}
Connection connection(unsigned slot) {
    Connection out;if(slot>=MAX_CONNECTIONS||!links){out.state=State::Error;return out;}
    portENTER_CRITICAL(&mux);out=links[slot];portEXIT_CRITICAL(&mux);return out;
}
DisplaySnapshot displaySnapshot(unsigned slot,int requestedChannel,uint32_t now){
    DisplaySnapshot out;if(slot>=MAX_CONNECTIONS||!links){out.state=State::Error;return out;}
    portENTER_CRITICAL(&mux);
    const auto &link=links[slot];out.state=link.state;
    copy(out.peer,link.peer.address);copy(out.profile,link.profile);copy(out.name,link.peer.name);
    out.channel=displayChannel(link.hasReading,link.readingAt,preferredChannel[slot],requestedChannel,now);
    out.hasReading=link.hasReading[out.channel];out.readingAt=link.readingAt[out.channel];
    if(out.hasReading)out.reading=displayReading(link.readings[out.channel]);
    else out.reading.channel=out.channel;
    portEXIT_CRITICAL(&mux);
    return out;
}
DisplaySnapshot displaySnapshot(unsigned slot,int requestedChannel){return displaySnapshot(slot,requestedChannel,millis());}
unsigned activeSlot(){portENTER_CRITICAL(&mux);unsigned out=focused;portEXIT_CRITICAL(&mux);return out;}
bool focus(unsigned slot) {
    if(slot>=MAX_CONNECTIONS)return false;
    portENTER_CRITICAL(&mux);focused=slot;portEXIT_CRITICAL(&mux);return true;
}
bool selectChannel(unsigned slot,unsigned channel) {
    if(slot>=MAX_CONNECTIONS || channel>=MAX_CHANNELS)return false;
    portENTER_CRITICAL(&mux);preferredChannel[slot]=channel;portEXIT_CRITICAL(&mux);return true;
}
bool poll(Event &out){return events && xQueueReceive(events,&out,0)==pdTRUE;}
static bool send(const Command &cmd){return commands && xQueueSend(commands,&cmd,0)==pdTRUE;}
// Called under mux, including from connect's atomic discovery validation/copy.
static bool reserve(const Command &cmd) {
    const unsigned slot=cmd.slot;if(!links)return false;
    slotMessage[slot]=true;
    if(!contexts[slot].ready){copy(links[slot].message,"Meter worker is unavailable");return false;}
    // One disconnect may follow an in-flight request so a name wait can stop.
    if(pending[slot] && (cmd.action!=Action::Disconnect || stopPending[slot])) {
        copy(links[slot].message,"Meter is busy; wait for the current request");return false;
    }
    const State s=links[slot].state;
    if(cmd.action!=Action::Disconnect && (s==State::Connected || s==State::Connecting || s==State::Retry)) {
        copy(links[slot].message,"Disconnect before changing the meter or profile");return false;
    }
    ++pending[slot];if(cmd.action==Action::Disconnect)stopPending[slot]=true;
    copy(links[slot].message,"Meter request queued");
    return true;
}
static bool sendReserved(const Command &cmd) {
    if(send(cmd))return true;
    notice(cmd.slot,"Meter request queue is full; try again");finishCommand(cmd.slot,cmd.action);return false;
}
static bool submit(const Command &cmd) {
    portENTER_CRITICAL(&mux);bool accepted=reserve(cmd);portEXIT_CRITICAL(&mux);
    return accepted && sendReserved(cmd);
}
bool scan(){Command c{};c.action=Action::Scan;return send(c);}
bool connect(unsigned index){return connect(index,activeSlot(),nullptr);}
bool connect(unsigned index,unsigned slot,const char *profile,const char *expectedAddress,int expectedAddressType) {
    if(slot>=MAX_CONNECTIONS||!links)return false;
    Command c{};c.action=Action::Connect;c.slot=slot;
    portENTER_CRITICAL(&mux);
    bool valid=index<discoveredCount;
    if(!valid){copy(links[slot].message,"Discovery changed; scan and select a meter");slotMessage[slot]=true;}
    if(valid && expectedAddress && *expectedAddress && strcasecmp(expectedAddress,discovered[index].address)) {
        copy(links[slot].message,"Discovery changed; select the meter again");slotMessage[slot]=true;valid=false;
    }
    if(valid&&expectedAddressType>=0&&discovered[index].addressType!=unsigned(expectedAddressType)){copy(links[slot].message,"Discovery changed; select the meter again");slotMessage[slot]=true;valid=false;}
    if(valid) {
        c.target=discovered[index];
        if(profile && strcmp(profile,"auto"))copy(c.profile,profile);
        else if(explicitProfile[slot])copy(c.profile,links[slot].profile);
        else copy(c.profile,c.target.profile);
        const auto *selected=dmm::findProfile(c.profile);
        valid=selected!=nullptr;
        if(valid){copy(c.profile,selected->id);valid=reserve(c);}
        else {copy(links[slot].message,"Choose a supported profile before connecting");slotMessage[slot]=true;}
    }
    portEXIT_CRITICAL(&mux);
    return valid && sendReserved(c);
}
bool disconnect(){return disconnect(activeSlot());}
bool disconnect(unsigned slot){if(slot>=MAX_CONNECTIONS)return false;Command c{};c.action=Action::Disconnect;c.slot=slot;return submit(c);}
bool setProfile(unsigned slot,const char *profile) {
    const auto *selected=dmm::findProfile(profile);
    if(slot>=MAX_CONNECTIONS || !selected)return false;
    Command c{};c.action=Action::Profile;c.slot=slot;copy(c.profile,selected->id);return submit(c);
}
bool setModel(ut_meter::Model model){return setProfile(activeSlot(),model==ut_meter::Model::UT60BT?"ut60bt":"ut61eplus");}
bool pairingsSaved(){portENTER_CRITICAL(&mux);bool ok=peerDurable;for(unsigned i=0;i<MAX_CONNECTIONS;++i)ok=ok&&!pending[i];portEXIT_CRITICAL(&mux);return ok;}
String pairingMessage(){char message[96];portENTER_CRITICAL(&mux);copy(message,peerError);portEXIT_CRITICAL(&mux);return String(message);}
static bool persist() {
    portENTER_CRITICAL(&mux);peerDurable=false;portEXIT_CRITICAL(&mux);
    uint8_t bytes[meter_ui::peerRecordBytes],check[meter_ui::peerRecordBytes];Preferences p;
    const bool opened=p.begin("meter-peers",false);
    const bool ok=opened&&meter_ui::encodePeerPreferences(saved,bytes,sizeof(bytes))&&p.putBytes("peers4",bytes,sizeof(bytes))==sizeof(bytes)&&p.getBytes("peers4",check,sizeof(check))==sizeof(check)&&!memcmp(bytes,check,sizeof(check));
    if(opened)p.end();portENTER_CRITICAL(&mux);peerDurable=ok;copy(peerError,ok?"":"Pairing save failed; current assignments are in RAM");portEXIT_CRITICAL(&mux);return ok;
}

class Scanner : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *device) override {
        auto name=device->getName();const auto *profile=dmm::profileForName(name.c_str());
        bool recognized=profile!=nullptr;
        if(!recognized)for(size_t i=0;i<dmm::profileCount();++i) {
            const auto *p=dmm::profileAt(i);
            if(device->isAdvertisingService(NimBLEUUID(p->gatt->serviceUuid))){recognized=true;break;}
        }
        if(!recognized && name.find("UT-D07")==std::string::npos && name.find("DMM")==std::string::npos)return;
        Candidate c;copy(c.name,name.empty()?"Bluetooth meter":name.c_str());
        copy(c.address,device->getAddress().toString().c_str());c.addressType=device->getAddressType();c.rssi=device->getRSSI();
        if(profile)copy(c.profile,profile->id);
        portENTER_CRITICAL(&mux);
        unsigned i=0;while(i<discoveredCount && strcmp(discovered[i].address,c.address))++i;
        if(i<MAX_METERS){discovered[i]=c;if(i==discoveredCount)++discoveredCount;}
        portEXIT_CRITICAL(&mux);
    }
};
static Scanner scanner;
static void scanDone(NimBLEScanResults) {
    portENTER_CRITICAL(&mux);scanning=false;
    copy(discoveryMessage,discoveredCount?"Choose a meter; tap Connect":"No meters: enable Bluetooth; close phone app");
    portEXIT_CRITICAL(&mux);
}
static void slotWorker(void *arg) {
    auto &ctx=*static_cast<Context *>(arg);unsigned slot=ctx.slot;
    NimBLEClient *client=ctx.client;NimBLERemoteCharacteristic *tx=nullptr;
    auto &decoder=*ctx.decoder;const dmm::Profile *profile=dmm::findProfile("ut60bt");
    Candidate target;bool wanted=false,linked=false;uint32_t generation=0,retryAt=0,pollAt=0,lastPacket=0;
    auto close=[&]() {
        ++generation;linked=false;tx=nullptr;decoder.reset();
        if(client->isConnected()) {
            client->disconnect();
            for(unsigned n=0;n<100 && client->isConnected();++n)vTaskDelay(pdMS_TO_TICKS(10));
        }
        xQueueReset(ctx.chunks);
    };
    auto write=[&](const dmm::Command &cmd) {
        if(!cmd.size)return true;if(!tx || !client->isConnected())return false;
        bool response=cmd.withResponse || profile->gatt->requireWriteResponse;
        if(profile->gatt->alwaysWithoutResponse)response=false;
        else if(!response && !tx->canWriteNoResponse())response=true;
        if(response && !tx->canWrite())return false;
        return tx->writeValue(cmd.data,cmd.size,response);
    };
    auto receive=[&](const Chunk &chunk) {
        if(chunk.generation!=generation)return;
        if(uint32_t(millis()-chunk.at)>ut_meter::STALE_MS){decoder.discardPartial();return;}
        portENTER_CRITICAL(&mux);links[slot].bytes+=chunk.size;portEXIT_CRITICAL(&mux);
        dmm::Reading readings[MAX_CHANNELS];uint32_t capturedAt=chunk.at;
        size_t count=decoder.push(chunk.bytes,chunk.size,chunk.at,readings,MAX_CHANNELS,capturedAt);
        for(size_t i=0;i<count;++i) {
            auto &r=readings[i];if(r.channel>=MAX_CHANNELS)continue;lastPacket=chunk.at;
            Event event;event.slot=slot;event.reading=r;event.at=capturedAt;
            copy(event.profile,profile->id);copy(event.address,target.address);
            portENTER_CRITICAL(&mux);
            auto &s=links[slot];s.readings[r.channel]=r;s.hasReading[r.channel]=true;s.readingAt[r.channel]=capturedAt;
            event.channelPacket=++s.channelPackets[r.channel];event.packet=++s.packets;copy(s.message,"Receiving readings");
            portEXIT_CRITICAL(&mux);
            if(xQueueSend(events,&event,0)!=pdTRUE){portENTER_CRITICAL(&mux);++links[slot].dropped;portEXIT_CRITICAL(&mux);}
        }
    };
    for(;;) {
        Command cmd;bool completingCommand=false;
        if(xQueueReceive(ctx.commands,&cmd,pdMS_TO_TICKS(15))==pdTRUE) {
            completingCommand=true;
            if(cmd.action==Action::Disconnect) {wanted=false;close();state(slot,State::Idle,"Disconnected");}
            else if(cmd.action==Action::Connect) {
                state(slot,State::Connecting,"Connecting to selected meter");
                close();target=cmd.target;profile=dmm::findProfile(cmd.profile);wanted=profile!=nullptr;retryAt=millis();
                portENTER_CRITICAL(&mux);links[slot].peer=target;copy(links[slot].profile,cmd.profile);
                memset(links[slot].hasReading,0,sizeof(links[slot].hasReading));portEXIT_CRITICAL(&mux);
            } else if(cmd.action==Action::Profile) {
                if(wanted || linked || client->isConnected())notice(slot,"Disconnect before changing the profile");
                else {
                    profile=dmm::findProfile(cmd.profile);decoder.setProfile(profile);
                    portENTER_CRITICAL(&mux);copy(links[slot].profile,profile->id);explicitProfile[slot]=true;
                    memset(links[slot].hasReading,0,sizeof(links[slot].hasReading));portEXIT_CRITICAL(&mux);
                    state(slot,State::Idle,"Profile selected; choose a meter");
                }
            }
        }
        uint32_t now=millis();
        if(linked && !client->isConnected()){close();retryAt=now+5000;state(slot,State::Retry,"Link lost; reconnecting selected meter");}
        if(wanted && !linked && int32_t(now-retryAt)>=0) {
            state(slot,State::Connecting,"Connecting to selected meter");++generation;
            // The controller can initiate only one connection at a time. A
            // concurrent NimBLE 1.4.x connect cancels the previous attempt.
            bool acquired=false;
            while(!stopping(slot) && !(acquired=xSemaphoreTake(connectMutex,pdMS_TO_TICKS(25))==pdTRUE)) {}
            bool ok=false;
            if(acquired) {
                if(!stopping(slot))ok=client->connect(NimBLEAddress(std::string(target.address),target.addressType));
                xSemaphoreGive(connectMutex);
            }
            if(stopping(slot))ok=false;
            NimBLERemoteCharacteristic *rx=nullptr;bool loginRefused=false,nameTimedOut=false;
            if(ok) {
                const auto *g=profile->gatt;auto *service=client->getService(NimBLEUUID(g->serviceUuid));
                if(service && g->bringUp==dmm::BringUp::ReadInfo && g->infoUuid) {
                    auto *info=service->getCharacteristic(NimBLEUUID(g->infoUuid));
                    if(info && info->canRead()) {
                        auto bytes=info->readValue();
                        if(const auto *detected=dmm::profileForOwonInfo(reinterpret_cast<const uint8_t *>(bytes.data()),bytes.size())) {
                            profile=detected;g=profile->gatt;
                            portENTER_CRITICAL(&mux);copy(links[slot].profile,profile->id);portEXIT_CRITICAL(&mux);
                        }
                    }
                }
                if(service) {
                    rx=service->getCharacteristic(NimBLEUUID(g->notifyUuid));
                    if(g->writeUuid && *g->writeUuid)tx=service->getCharacteristic(NimBLEUUID(g->writeUuid));
                }
                ok=rx && (rx->canNotify() || rx->canIndicate()) && client->getMTU()>=g->minMtu;
                decoder.setProfile(profile);const uint32_t session=generation;
                auto subscribe=[&]() {
                    return rx->subscribe(rx->canNotify(),[slot,session](NimBLERemoteCharacteristic *,uint8_t *data,size_t len,bool) {
                        Chunk c{};c.generation=session;c.at=millis();
                        if(len>sizeof(c.bytes)) {
                            portENTER_CRITICAL(&mux);++links[slot].dropped;portEXIT_CRITICAL(&mux);return;
                        }
                        c.size=len;memcpy(c.bytes,data,len);
                        if(xQueueSend(contexts[slot].chunks,&c,0)!=pdTRUE) {
                            portENTER_CRITICAL(&mux);++links[slot].dropped;portEXIT_CRITICAL(&mux);
                        }
                    },true);
                };
                const bool loginBeforeSubscribe=g->bringUp==dmm::BringUp::BrymenLogin;
                // UNI-T protocol commands need an already subscribed UART.
                // Brymen's password login is the transport exception.
                if(ok && !loginBeforeSubscribe)ok=subscribe();
                uint8_t mac[6]={};
                sscanf(target.address,"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",mac,mac+1,mac+2,mac+3,mac+4,mac+5);
                for(size_t step=0;ok && step<8;++step) {
                    if(stopping(slot)){ok=false;break;}
                    dmm::Command startup;if(!dmm::startupCommand(*profile,step,startup,mac))break;
                    const uint32_t sentAt=millis();ok=write(startup);
                    const bool needsName=profile->family==dmm::Family::Uni61 &&
                        (profile->modelCode==60 || profile->modelCode==202) && step==0;
                    if(ok && needsName) {
                        dmm::NameReply reply;bool answered=false;
                        while(!stopping(slot) && client->isConnected() && uint32_t(millis()-sentAt)<6000) {
                            Chunk chunk;
                            if(xQueueReceive(ctx.chunks,&chunk,pdMS_TO_TICKS(25))!=pdTRUE)continue;
                            if(chunk.generation!=generation || uint32_t(millis()-chunk.at)>ut_meter::STALE_MS)continue;
                            answered=reply.push(chunk.bytes,chunk.size);
                            receive(chunk);
                            if(answered)break;
                        }
                        if(!answered){ok=false;nameTimedOut=!stopping(slot);}
                    }
                    uint32_t waited=millis()-sentAt;
                    if(ok && waited<startup.waitMs)vTaskDelay(pdMS_TO_TICKS(startup.waitMs-waited));
                    if(ok && startup.readReply && tx && tx->canRead()) {
                        auto answer=tx->readValue();
                        auto result=dmm::judgeLogin(reinterpret_cast<const uint8_t *>(answer.data()),answer.size());
                        if(result==dmm::LoginResult::Refused){ok=false;loginRefused=true;}
                    }
                }
                if(ok && g->settleMs)vTaskDelay(pdMS_TO_TICKS(g->settleMs));
                if(ok && loginBeforeSubscribe)ok=subscribe();
            }
            if(stopping(slot))ok=false;
            if(ok && client->isConnected()) {
                linked=true;lastPacket=millis();pollAt=lastPacket;state(slot,State::Connected,"Waiting for meter readings");
            } else {
                close();retryAt=millis()+5000;
                if(loginRefused){wanted=false;state(slot,State::Error,"Meter refused login; automatic login stopped");}
                else state(slot,State::Retry,stopping(slot)?"Disconnect requested; stopping meter":nameTimedOut?"No valid meter name reply; reconnecting":"Connect failed; enable Bluetooth; close phone app");
            }
        }
        Chunk chunk;
        for(unsigned n=0;n<8 && xQueueReceive(ctx.chunks,&chunk,0)==pdTRUE;++n) {
            if(linked)receive(chunk);
        }
        now=millis();
        // Drain streamed data before deciding whether a fallback query is needed.
        if(linked && !stopping(slot) && client->isConnected() && uint32_t(now-lastPacket)>=500 && int32_t(now-pollAt)>=0) {
            dmm::Command poll;
            if(dmm::pollCommand(*profile,poll) && !write(poll)) {
                close();retryAt=millis()+5000;state(slot,State::Retry,"Query failed; reconnecting");
            }
            pollAt=millis()+500;
        }
        if(linked && uint32_t(millis()-lastPacket)>12000){close();retryAt=millis()+5000;state(slot,State::Retry,"No valid readings; reconnecting");}
        if(completingCommand)finishCommand(slot,cmd.action);
    }
}
static void manager(void *) {
    for(;;) {
        Command cmd;if(xQueueReceive(commands,&cmd,portMAX_DELAY)!=pdTRUE)continue;
        if(cmd.action==Action::Scan) {
            if(radio->isScanning())radio->stop();radio->clearResults();
            portENTER_CRITICAL(&mux);discoveredCount=0;scanning=true;copy(discoveryMessage,"Looking for meters (5s)");portEXIT_CRITICAL(&mux);
            if(!radio->start(5,scanDone,false))scanDone(NimBLEScanResults());
        } else if(cmd.slot<MAX_CONNECTIONS) {
            unsigned slot=cmd.slot;
            if(cmd.action==Action::Connect) {
                bool duplicate=false;
                for(unsigned i=0;i<MAX_CONNECTIONS;++i)
                    if(i!=slot && saved.peers[i].enabled && !strcmp(saved.peers[i].address,cmd.target.address))duplicate=true;
                if(duplicate) {
                    notice(slot,"That meter is already assigned to another slot");finishCommand(slot,cmd.action);continue;
                }
                if(radio->isScanning())radio->stop();
            }
            if(!contexts[slot].ready || xQueueSend(contexts[slot].commands,&cmd,0)!=pdTRUE) {
                notice(slot,"Meter command queue is full; try again");finishCommand(slot,cmd.action);continue;
            }
            // Durable desired assignments reflect accepted worker commands only.
            if(cmd.action==Action::Connect) {
                copy(saved.peers[slot].address,cmd.target.address);copy(saved.peers[slot].profile,cmd.profile);
                saved.peers[slot].type=cmd.target.addressType;saved.peers[slot].enabled=1;persist();
            } else if(cmd.action==Action::Disconnect) {
                portENTER_CRITICAL(&mux);if(stopPending[slot])stopRequested[slot]=true;portEXIT_CRITICAL(&mux);
                saved.peers[slot].enabled=0;persist();
            }
        }
    }
}
bool begin() {
    void *linkMemory=psramFound()?ps_malloc(sizeof(Connection)*MAX_CONNECTIONS):nullptr;if(!linkMemory)linkMemory=malloc(sizeof(Connection)*MAX_CONNECTIONS);if(!linkMemory)return false;
    links=static_cast<Connection*>(linkMemory);for(unsigned i=0;i<MAX_CONNECTIONS;++i)new(&links[i]) Connection;
    commands=xQueueCreate(12,sizeof(Command));events=xQueueCreate(64,sizeof(Event));connectMutex=xSemaphoreCreateMutex();
    if(!commands || !events || !connectMutex) {
        for(unsigned i=0;i<MAX_CONNECTIONS;++i)state(i,State::Error,"No memory for meter control queues");
        return false;
    }
    NimBLEDevice::init("Bluetooth Meter Logger");NimBLEDevice::setMTU(247);
    radio=NimBLEDevice::getScan();radio->setAdvertisedDeviceCallbacks(&scanner,true);
    radio->setActiveScan(true);radio->setInterval(80);radio->setWindow(60);radio->setMaxResults(0);
    Preferences prefs;bool loaded=true,migrate=false;
    if(prefs.begin("meter-peers",false)) {
        const bool modern=prefs.isKey("peers4");const char *key=modern?"peers4":"peers";
        if(prefs.isKey(key)){
            const auto size=prefs.getBytesLength(key);uint8_t record[meter_ui::peerRecordBytes];bool legacy=false;
            loaded=(size==meter_ui::peerRecordBytes||(!modern&&size==meter_ui::legacyPeerRecordBytes))&&prefs.getBytes(key,record,size)==size&&meter_ui::decodePeerPreferences(record,size,saved,&legacy);
            migrate=loaded&&!modern;
        }
        prefs.end();
    }else loaded=false;
    peerDurable=loaded;if(!loaded)copy(peerError,"Saved pairings unavailable/corrupt; automatic connections disabled");
    if(migrate)persist(); // Preserve the old three-slot key for rollback.
    bool allReady=true;
    for(unsigned i=0;i<MAX_CONNECTIONS;++i) {
        auto &ctx=contexts[i];ctx.slot=i;
        void *memory=psramFound()?ps_malloc(sizeof(dmm::AgedDecoder)):nullptr;
        if(!memory)memory=malloc(sizeof(dmm::AgedDecoder));
        if(!memory){allReady=false;state(i,State::Error,"No memory for meter decoder");continue;}
        ctx.decoder=new(memory)dmm::AgedDecoder(nullptr,ut_meter::STALE_MS);
        ctx.commands=xQueueCreate(4,sizeof(Command));ctx.chunks=xQueueCreate(8,sizeof(Chunk));
        ctx.client=NimBLEDevice::createClient();
        if(!ctx.commands || !ctx.chunks || !ctx.client) {
            allReady=false;state(i,State::Error,"No memory for meter worker");continue;
        }
        ctx.client->setConnectTimeout(5);
        if(xTaskCreate(slotWorker,"meter-slot",8192,&ctx,1,nullptr)!=pdPASS) {
            allReady=false;state(i,State::Error,"Could not start meter worker");continue;
        }
        ctx.ready=true;
    }
    const Saved restore=saved;
    if(xTaskCreate(manager,"meter-manager",4096,nullptr,1,nullptr)!=pdPASS) {
        for(unsigned i=0;i<MAX_CONNECTIONS;++i){contexts[i].ready=false;state(i,State::Error,"Could not start meter manager");}
        return false;
    }
    for(unsigned i=0;i<MAX_CONNECTIONS;++i)if(restore.peers[i].enabled && contexts[i].ready) {
        auto p=restore.peers[i];p.address[17]=0;p.profile[31]=0;
        const auto *profile=dmm::findProfile(p.profile);
        if(strlen(p.address)!=17 || !profile)continue;
        Command c{};c.action=Action::Connect;c.slot=i;copy(c.profile,profile->id);copy(c.target.profile,profile->id);
        copy(c.target.address,p.address);copy(c.target.name,profile->id);c.target.addressType=p.type;submit(c);
    }
    return allReady;
}
}
