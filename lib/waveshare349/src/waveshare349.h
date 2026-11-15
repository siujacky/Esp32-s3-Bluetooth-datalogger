#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cmath>

// Portable PCF85063 UTC, AXS15231B axis and gravity policy. Bus I/O lives in
// meter_board_devices.cpp/panel_lcd349.cpp, so these rules are host-testable.
namespace waveshare349 {
inline unsigned daysInMonth(unsigned y,unsigned m){
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return m>=1&&m<=12?days[m-1]+unsigned(m==2&&y%4==0&&(y%100!=0||y%400==0)):0;
}
inline bool bcd(uint8_t v,unsigned max,unsigned &out){
    if((v&15)>9||(v>>4)>9)return false;
    out=(v>>4)*10+(v&15);return out<=max;
}
inline uint8_t bcd(unsigned v){return uint8_t((v/10<<4)|(v%10));}
inline bool decodeRtc(const uint8_t *p,size_t n,uint64_t &unixMs){
    if(!p||n!=7)return false; // Seconds OS flag must be clear; no bit masking.
    unsigned s,m,h,d,w,mo,y;
    if(!bcd(p[0],59,s)||!bcd(p[1],59,m)||!bcd(p[2],23,h)||!bcd(p[3],31,d)||
       !bcd(p[4],6,w)||!bcd(p[5],12,mo)||!bcd(p[6],99,y)||y<20||!d||!mo||d>daysInMonth(2000+y,mo))return false;
    unsigned days=0;
    for(unsigned year=1970;year<2000+y;++year)days+=365+unsigned(daysInMonth(year,2)==29);
    for(unsigned month=1;month<mo;++month)days+=daysInMonth(2000+y,month);
    days+=d-1;
    unixMs=(uint64_t(days)*86400+h*3600+m*60+s)*1000;return true;
}
inline bool encodeRtc(uint64_t unixMs,uint8_t *p,size_t n){
    if(!p||n!=7||unixMs<1577836800000ULL||unixMs>=4102444800000ULL)return false;
    const uint64_t seconds=unixMs/1000;unsigned days=unsigned(seconds/86400),remain=days,year=1970,month=1;
    for(;;){const unsigned length=365+unsigned(daysInMonth(year,2)==29);if(remain<length)break;remain-=length;++year;}
    while(remain>=daysInMonth(year,month)){remain-=daysInMonth(year,month);++month;}
    const uint8_t result[]={bcd(unsigned(seconds%60)),bcd(unsigned(seconds/60%60)),bcd(unsigned(seconds/3600%24)),
        bcd(remain+1),bcd((days+4)%7),bcd(month),bcd(year-2000)};
    for(unsigned i=0;i<7;++i)p[i]=result[i];
    return true;
}
inline bool decodeTouch(const uint8_t *p,size_t n,unsigned rotation,uint16_t &x,uint16_t &y,bool &down){
    if(!p||n!=32||rotation>3)return false;
    // Captured on the physical Waveshare Rev1.1/V2 when no finger is down.
    // Match the whole marker; a mixed/corrupt response is still rejected.
    bool idle=p[0]==0xf9;
    for(size_t i=1;idle&&i<n;++i)if(p[i]!=0xf9)idle=false;
    if(idle){down=false;return true;}
    if(p[1]>1)return false;
    if(!p[1]){down=false;return true;}
    const unsigned event=p[2]>>6;if(event==1){down=false;return true;}if(event==3)return false;
    const unsigned rawX=((p[2]&15)<<8)|p[3],rawY=((p[4]&15)<<8)|p[5];
    if(rawX>=640||rawY>=172)return false;
    // Vendor portrait axes are (rawY, 639-rawX), then Adafruit GFX rotation.
    const unsigned nx=rawY,ny=639-rawX;
    switch(rotation){
    case 1:x=uint16_t(ny);y=uint16_t(171-nx);break;
    case 2:x=uint16_t(171-nx);y=uint16_t(639-ny);break;
    case 3:x=uint16_t(639-ny);y=uint16_t(nx);break;
    default:x=uint16_t(nx);y=uint16_t(ny);break;
    }
    down=true;return true;
}
class MotionRotation {
    unsigned candidate_=4,emitted_=4;uint32_t since_=0;
public:
    void reset(){candidate_=emitted_=4;since_=0;}
    bool update(float x,float y,float z,uint32_t now,unsigned &rotation){
        const float ax=std::fabs(x),ay=std::fabs(y),az=std::fabs(z),norm=x*x+y*y+z*z;
        if(!std::isfinite(norm)||norm<0.64f||norm>1.44f||az>0.55f||std::fmax(ax,ay)<0.75f||std::fabs(ax-ay)<0.3f){candidate_=4;return false;}
        const unsigned next=ay>ax?(y>0?0:2):(x>0?1:3);
        if(next!=candidate_){candidate_=next;since_=now;return false;}
        if(uint32_t(now-since_)<500||next==emitted_)return false;
        rotation=emitted_=next;return true;
    }
};
}
