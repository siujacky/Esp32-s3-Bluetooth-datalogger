#pragma once
#include "meter_value_format.h"
#include "meter_value_layout.h"
#include "meter_record_crc.h"
namespace meter_ui {
struct ValuePreferences {ValuePreset presets[4]={ValuePreset::Solo,ValuePreset::LargeSmall,ValuePreset::LargeSmallSmall,ValuePreset::Grid};NumberFormat formats[4][8]{};};
constexpr unsigned valueRecordBytes=108,legacyValueRecordBytes=83;
inline uint32_t valuePreferencesCrc(const uint8_t *data,unsigned size){return recordCrc(data,size);}
inline bool validValuePreferences(const ValuePreferences &p){for(unsigned i=0;i<4;++i)if(!validValuePreset(p.presets[i],i+1))return false;for(const auto &slot:p.formats)for(const auto &f:slot)if(!validNumberFormat(f))return false;return true;}
inline bool encodeValuePreferences(const ValuePreferences &p,uint8_t *out,unsigned size){
    if(!out||size!=valueRecordBytes||!validValuePreferences(p))return false;
    out[0]='M';out[1]='V';out[2]=2;out[3]=0;unsigned at=4;
    for(auto preset:p.presets)out[at++]=uint8_t(preset);
    for(const auto &slot:p.formats)for(const auto &f:slot){out[at++]=f.digits;out[at++]=f.decimals;out[at++]=uint8_t(f.padding);}
    const uint32_t crc=valuePreferencesCrc(out,at);for(unsigned i=0;i<4;++i)out[at++]=uint8_t(crc>>(i*8));return true;
}
inline bool decodeValuePreferences(const uint8_t *data,unsigned size,ValuePreferences &out){
    if(!data||size<4||data[0]!='M'||data[1]!='V'||data[3]!=0)return false;
    const bool legacy=size==legacyValueRecordBytes&&data[2]==1;
    if(!legacy&&(size!=valueRecordBytes||data[2]!=2))return false;
    uint32_t stored=0;for(unsigned i=0;i<4;++i)stored|=uint32_t(data[size-4+i])<<(8*i);
    if(stored!=valuePreferencesCrc(data,size-4))return false;
    ValuePreferences next;unsigned at=4,slots=legacy?3:4;
    for(unsigned i=0;i<slots;++i)next.presets[i]=ValuePreset(data[at++]);
    for(unsigned s=0;s<slots;++s)for(auto &f:next.formats[s]){f.digits=data[at++];f.decimals=data[at++];f.padding=Padding(data[at++]);}
    if(!validValuePreferences(next))return false;
    out=next;return true;
}
}
