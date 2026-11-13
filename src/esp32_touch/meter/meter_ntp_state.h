#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace meter_ntp {
constexpr size_t serverBytes=254;
constexpr size_t configBytes=266;
constexpr uint64_t minUnixMs=1577836800000ULL;
constexpr uint64_t maxUnixMs=4102444800000ULL;
struct Config { bool enabled=true; char server[serverBytes]="pool.ntp.org"; };
// Network-order integer, independent of the host CPU's endianness. Reject
// shorthand/octal spellings and addresses which cannot name a remote host.
inline bool parseIpv4(const char *text,uint32_t &out) {
    if(!text)return false;
    uint32_t address=0;
    for(unsigned part=0;part<4;++part){
        unsigned value=0,count=0;
        const char *start=text;
        while(*text>='0'&&*text<='9'){
            if(++count>3)return false;
            value=value*10+unsigned(*text++-'0');
        }
        if(!count||value>255||(count>1&&*start=='0'))return false;
        address=(address<<8)|value;
        if(part<3){if(*text++!='.')return false;}else if(*text)return false;
    }
    const unsigned first=address>>24;
    if(first==0||first==127||first>=224)return false;
    out=address;return true;
}
inline bool validServer(const char *text) {
    if(!text)return false;
    size_t size=0;bool numeric=true;
    for(;size<serverBytes&&text[size];++size)
        if((text[size]<'0'||text[size]>'9')&&text[size]!='.')numeric=false;
    if(!size||size>=serverBytes)return false;
    if(numeric){uint32_t address=0;return parseIpv4(text,address);}
    size_t label=0;
    for(size_t i=0;i<size;++i){
        const char c=text[i];
        if(c=='.'){
            if(!label||text[i-1]=='-')return false;
            label=0;continue;
        }
        const bool alnum=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
        if((!alnum&&c!='-')||(!label&&c=='-')||++label>63)return false;
    }
    return label&&text[size-1]!='-';
}
inline bool localApServer(const char *server,uint32_t ap,uint32_t mask) {
    uint32_t address=0;
    if(!ap||!mask||(mask|(mask-1))!=0xffffffffU||!parseIpv4(server,address))return false;
    const uint32_t network=ap&mask,broadcast=network|~mask;
    return (address&mask)==network&&address!=network&&address!=broadcast&&address!=ap;
}
inline uint32_t configCrc(const uint8_t *bytes,size_t size) {
    uint32_t crc=0xffffffffU;
    for(size_t i=0;i<size;++i){crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&uint32_t(-int32_t(crc&1)));}
    return ~crc;
}
inline bool encodeConfig(const Config &config,uint8_t *out,size_t size) {
    if(!out||size!=configBytes||!validServer(config.server))return false;
    memset(out,0,size);out[0]='N';out[1]='T';out[2]='P';out[3]=1;
    out[4]=config.enabled?1:0;out[5]=static_cast<uint8_t>(strlen(config.server));
    memcpy(out+8,config.server,out[5]);
    const uint32_t crc=configCrc(out,size-4);
    for(unsigned i=0;i<4;++i)out[size-4+i]=static_cast<uint8_t>(crc>>(i*8));
    return true;
}
inline bool decodeConfig(const uint8_t *bytes,size_t size,Config &out) {
    if(!bytes||size!=configBytes||bytes[0]!='N'||bytes[1]!='T'||bytes[2]!='P'||bytes[3]!=1||bytes[4]>1||!bytes[5]||bytes[5]>=serverBytes||bytes[6]||bytes[7])return false;
    uint32_t stored=0;for(unsigned i=0;i<4;++i)stored|=uint32_t(bytes[size-4+i])<<(i*8);
    if(stored!=configCrc(bytes,size-4))return false;
    for(size_t i=bytes[5];i<serverBytes;++i)if(bytes[8+i])return false;
    Config next;next.enabled=bytes[4]!=0;memcpy(next.server,bytes+8,bytes[5]);next.server[bytes[5]]=0;
    if(strlen(next.server)!=bytes[5]||!validServer(next.server))return false;
    out=next;return true;
}
struct TimeState {
    bool synced=false;
    uint64_t lastSyncUnixMs=0,lastSyncUptimeMs=0;
    bool accept(int64_t seconds,int32_t microseconds,uint64_t uptimeMs) {
        if(seconds<int64_t(minUnixMs/1000)||seconds>int64_t(maxUnixMs/1000)||microseconds<0||microseconds>=1000000)return false;
        const uint64_t unixMs=uint64_t(seconds)*1000+uint32_t(microseconds)/1000;
        if(unixMs<minUnixMs||unixMs>maxUnixMs)return false;
        synced=true;lastSyncUnixMs=unixMs;lastSyncUptimeMs=uptimeMs;return true;
    }
    uint64_t current(uint64_t uptimeMs) const {
        if(!synced||uptimeMs<lastSyncUptimeMs||uptimeMs-lastSyncUptimeMs>maxUnixMs-lastSyncUnixMs)return 0;
        return lastSyncUnixMs+(uptimeMs-lastSyncUptimeMs);
    }
};
}
