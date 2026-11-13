#pragma once
#include <ut_meter.h>
#include <dmm_protocols.h>

class String;
namespace meter_ble {
constexpr unsigned MAX_METERS=16,MAX_CONNECTIONS=ut_meter::METER_SLOTS,MAX_CHANNELS=ut_meter::METER_CHANNELS;
struct Candidate {
    char name[32] = {}, address[18] = {};
    uint8_t addressType=0;
    int rssi=0;
    char profile[32]={};
};
enum class State : uint8_t { Idle, Scanning, Connecting, Connected, Retry, Error };
// One selected channel only: safe to hold while rendering several slots on the
// limited Arduino loop stack. Explicit selections never fall back to a channel
// with a different unit when their own sample is absent or old.
struct DisplaySnapshot {
    State state=State::Idle;
    char peer[18]={},profile[32]="ut60bt",name[32]={};
    ut_meter::Reading reading;
    bool hasReading=false;
    uint32_t readingAt=0;
    unsigned channel=0;
};
inline unsigned displayChannel(const bool *has,const uint32_t *at,unsigned preferred,int requested,uint32_t now){
    if(requested>=0 && unsigned(requested)<MAX_CHANNELS)return unsigned(requested);
    unsigned channel=preferred<MAX_CHANNELS?preferred:0;
    if(!has[channel] || uint32_t(now-at[channel])>ut_meter::STALE_MS){
        for(unsigned c=0;c<MAX_CHANNELS;++c)if(has[c] && uint32_t(now-at[c])<=ut_meter::STALE_MS){channel=c;break;}
        if(!has[channel])for(unsigned c=0;c<MAX_CHANNELS;++c)if(has[c]){channel=c;break;}
    }
    return channel;
}
struct Connection {
    State state=State::Idle;
    Candidate peer;
    char profile[32]="ut60bt",message[80]="Choose a meter; tap Connect";
    dmm::Reading readings[MAX_CHANNELS];
    bool hasReading[MAX_CHANNELS]={};
    uint32_t readingAt[MAX_CHANNELS]={},channelPackets[MAX_CHANNELS]={};
    uint32_t packets=0,bytes=0,dropped=0;
};
struct Event {
    uint8_t slot=0;
    dmm::Reading reading;
    uint32_t packet=0,channelPacket=0,at=0;
    char address[18]={},profile[32]={};
};
struct View {
    State state=State::Idle;
    ut_meter::Model model=ut_meter::Model::UT60BT;
    Candidate meters[MAX_METERS];
    unsigned count=0;
    char message[64]="Enable meter Bluetooth; tap Scan";
    char peer[18]={};
    ut_meter::Reading reading;
    bool hasReading=false;
    uint32_t readingAt=0, packets=0, bytes=0, dropped=0;
    unsigned activeSlot=0,profileIndex=0;
    char profile[32]="ut60bt";
    bool scanning=false;
};
bool begin();
bool pairingsSaved();
String pairingMessage();
View view();
bool scan();
bool connect(unsigned index);
bool disconnect();
bool setModel(ut_meter::Model model);
Connection connection(unsigned slot);
DisplaySnapshot displaySnapshot(unsigned slot,int requestedChannel=-1);
DisplaySnapshot displaySnapshot(unsigned slot,int requestedChannel,uint32_t now);
bool poll(Event &event);
bool connect(unsigned index,unsigned slot,const char *profile,const char *expectedAddress=nullptr,int expectedAddressType=-1);
bool disconnect(unsigned slot);
bool setProfile(unsigned slot,const char *profile);
bool focus(unsigned slot);
unsigned activeSlot();
bool selectChannel(unsigned slot,unsigned channel);
ut_meter::Reading displayReading(const dmm::Reading &reading);
}
