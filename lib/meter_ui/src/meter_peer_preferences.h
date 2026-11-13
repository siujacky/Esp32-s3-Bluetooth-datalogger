#pragma once
#include "meter_record_crc.h"
#include <meter_limits.h>
#include <cstring>
namespace meter_ui {
struct PeerPreference {char address[18]={},profile[32]={};uint8_t type=0,enabled=0;};
static_assert(sizeof(PeerPreference)==52,"legacy pairing layout changed");
struct PeerPreferences {PeerPreference peers[ut_meter::METER_SLOTS];};
constexpr unsigned peerRecordBytes=216,legacyPeerRecordBytes=160;
inline bool peerAddressValid(const char *s){if(std::strlen(s)!=17)return false;for(unsigned i=0;i<17;++i){if(i%3==2){if(s[i]!=':')return false;}else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;}return true;}
inline bool validPeerPreferences(const PeerPreferences &p){for(const auto &v:p.peers){if(v.enabled>1||v.type>3||!std::memchr(v.address,0,sizeof(v.address))||!std::memchr(v.profile,0,sizeof(v.profile)))return false;if(v.enabled&&(!peerAddressValid(v.address)||!v.profile[0]))return false;for(const char *s=v.profile;*s;++s)if(*s<33||*s>126)return false;}return true;}
inline bool encodePeerPreferences(const PeerPreferences &p,uint8_t *out,unsigned size){if(!out||size!=peerRecordBytes||!validPeerPreferences(p))return false;out[0]='M';out[1]='P';out[2]=2;out[3]=4;unsigned at=4;for(const auto &v:p.peers){memcpy(out+at,v.address,18);at+=18;memcpy(out+at,v.profile,32);at+=32;out[at++]=v.type;out[at++]=v.enabled;}const auto crc=recordCrc(out,at);for(unsigned i=0;i<4;++i)out[at++]=crc>>(8*i);return true;}
inline bool decodePeerPreferences(const uint8_t *data,unsigned size,PeerPreferences &out,bool *legacy=nullptr){
 if(!data||size<4)return false;bool old=size==legacyPeerRecordBytes&&data[0]==0x31&&data[1]==0x52&&data[2]==0x54&&data[3]==0x4d;
 if(!old){if(size!=peerRecordBytes||data[0]!='M'||data[1]!='P'||data[2]!=2||data[3]!=4)return false;uint32_t stored=0;for(unsigned i=0;i<4;++i)stored|=uint32_t(data[size-4+i])<<(8*i);if(stored!=recordCrc(data,size-4))return false;}
 PeerPreferences next;unsigned at=4;for(unsigned i=0;i<(old?3u:4u);++i){auto &v=next.peers[i];memcpy(v.address,data+at,18);at+=18;memcpy(v.profile,data+at,32);at+=32;v.type=data[at++];v.enabled=data[at++];}if(!validPeerPreferences(next))return false;out=next;if(legacy)*legacy=old;return true;
}
}
