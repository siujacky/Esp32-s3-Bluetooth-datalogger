#include "ut_meter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
namespace ut_meter {
const char *unit(const Reading &r) {return r.hasMetadata || r.unitLabel[0]?r.unitLabel:unit(r.mode);}
const char *modeName(const Reading &r) {return r.hasMetadata || r.modeLabel[0]?r.modeLabel:modeName(r.mode,r.channel);}
bool isFresh(bool connected, bool hasReading, uint32_t now, uint32_t receivedAt) {
    return connected && hasReading && uint32_t(now-receivedAt)<=STALE_MS;
}
// Protocol and model scaling ported from olegv142/dmmpy, adapters/ut61xp.py
// (public domain / Unlicense). Units match that adapter: e.g. nF and mA.
bool decode(const uint8_t *f, size_t size, Model model, Reading &out) {
    if (!f || size != FRAME_SIZE || f[0] != 0xab || f[1] != 0xcd || f[2] != 16)
        return false;
    unsigned sum=0;
    for(size_t i=0;i<17;i++) sum+=f[i];
    if(sum != unsigned(f[17])*256+f[18] || f[4]<'0' || f[4]>'9') return false;
    Reading r;
    r.mode=f[3]; r.range=f[4]-'0';
    memcpy(r.flags,f+14,3);
    r.channel=(r.mode==25 && (r.flags[2]&8)) ? 1 : 0;
    size_t n=0;
    for(size_t i=5;i<12;i++) if(f[i]!=' ') r.text[n++]=char(f[i]);
    r.text[n]=0;
    bool numeric=n>0, digit=false;
    for(size_t i=0;i<n;i++) {
        char c=r.text[i];
        if(c>='0' && c<='9')digit=true;
        else if(c!='.' && !(i==0 && (c=='+' || c=='-')))numeric=false;
    }
    char *end=nullptr;
    double value=strtod(r.text,&end);
    r.numeric=numeric && digit && end==r.text+n && isfinite(value) && *unit(r.mode);
    if(r.numeric) {
        int offset=-1;
        if(model==Model::UT60BT) {
            if(r.mode==0 || r.mode==2 || r.mode==6 || r.mode==14 || r.mode==15)offset=2;
            if(r.mode==0 || r.mode==2)value*=.001;
        } else {
            if(r.mode==6)offset=2;
            if(r.mode==9)offset=1;
        }
        if(offset>=0)value*=pow(10.,3*((r.range+offset)/3));
        r.value=value;
    } else r.value=NAN;
    out=r;
    return true;
}
bool Decoder::push(uint8_t byte, Model model, Reading &out) {
    buffer_[count_++]=byte;
    while(count_) {
        bool prefix=buffer_[0]==0xab && (count_<2 || buffer_[1]==0xcd)
            && (count_<3 || buffer_[2]==16);
        if(prefix && count_<FRAME_SIZE)return false;
        if(prefix && decode(buffer_,count_,model,out)) {count_=0;return true;}
        --count_;
        memmove(buffer_,buffer_+1,count_);
    }
    return false;
}
const char *modeName(uint8_t mode, uint8_t channel) {
    switch(mode) {
    case 0: case 1: return "AC voltage";
    case 2: case 3: return "DC voltage";
    case 4: return "Frequency"; case 5: return "Duty cycle";
    case 6: return "Resistance"; case 7: return "Continuity";
    case 8: return "Diode"; case 9: return "Capacitance";
    case 10: case 11: return "Temperature";
    case 12: case 14: case 16: return "DC current";
    case 13: case 15: case 17: return "AC current";
    case 18: return "Transistor"; case 20: return "NCV";
    case 21: return "AC LoZ"; case 24: return "AC LPF";
    case 25: return channel ? "AC voltage" : "DC voltage";
    default: return "Unknown mode";
    }
}
const char *unit(uint8_t mode) {
    switch(mode) {
    case 0: case 2: case 8: case 21: case 24: case 25: return "V";
    case 1: case 3: return "mV";
    case 4: return "Hz"; case 5: return "%";
    case 6: case 7: return "Ohm"; case 9: return "nF";
    case 10: return "C"; case 11: return "F";
    case 12: case 13: return "uA";
    case 14: case 15: return "mA";
    case 16: case 17: return "A";
    case 18: return "hFE"; case 20: return "NCV";
    default: return "";
    }
}
}
