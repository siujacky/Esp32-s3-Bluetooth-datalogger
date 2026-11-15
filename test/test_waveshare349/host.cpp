#include <waveshare349.h>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace waveshare349;
int main(){
    uint8_t rtc[7]={0x56,0x34,0x12,0x29,0x04,0x02,0x24};uint64_t utc=0;
    assert(decodeRtc(rtc,7,utc)&&utc==1709210096000ULL);
    uint8_t encoded[7]={};assert(encodeRtc(utc,encoded,7));assert(!memcmp(rtc,encoded,7));
    for(unsigned year=2020;year<2100;++year){
        rtc[6]=uint8_t(((year-2000)/10<<4)|(year%10));
        assert(decodeRtc(rtc,7,utc)==(year%4==0));
    }
    rtc[6]=0x24;
    for(unsigned i=0;i<7;++i){uint8_t bad[7];memcpy(bad,rtc,7);bad[i]=0xff;assert(!decodeRtc(bad,7,utc));}
    rtc[0]|=0x80;assert(!decodeRtc(rtc,7,utc));rtc[0]&=0x7f;
    assert(!encodeRtc(0,encoded,7));assert(!decodeRtc(rtc,6,utc));
    const uint16_t expected[4][4][2]={{{0,639},{0,0},{171,0},{171,639}},{{639,171},{0,171},{0,0},{639,0}},{{171,0},{171,639},{0,639},{0,0}},{{0,0},{639,0},{639,171},{0,171}}};
    const unsigned raw[4][2]={{0,0},{639,0},{639,171},{0,171}};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        uint8_t p[32]={0,1,uint8_t(raw[c][0]>>8),uint8_t(raw[c][0]),uint8_t(raw[c][1]>>8),uint8_t(raw[c][1])};
        uint16_t x=9,y=10;bool down=false;assert(decodeTouch(p,32,r,x,y,down)&&down);
        assert(x==expected[r][c][0]&&y==expected[r][c][1]);
    }
    uint16_t x=9,y=10;bool down=true;uint8_t p[32]={};assert(decodeTouch(p,32,0,x,y,down)&&!down&&x==9&&y==10);
    p[1]=1;p[2]=3;assert(!decodeTouch(p,32,0,x,y,down)&&x==9&&y==10&&!down);
    p[2]=0;p[1]=5;assert(!decodeTouch(p,32,0,x,y,down));assert(!decodeTouch(p,8,0,x,y,down));
    // Real Rev1.1/V2 idle capture: all 32 bytes are F9. It is a release,
    // including after a valid contact, and must preserve the last position.
    memset(p,0xf9,sizeof(p));down=true;x=99;y=100;
    assert(decodeTouch(p,32,0,x,y,down)&&!down&&x==99&&y==100);
    p[31]=0;down=true;assert(!decodeTouch(p,32,0,x,y,down)&&down&&x==99&&y==100);
    MotionRotation motion;unsigned r=9;
    assert(!motion.update(0,1,0,0,r));assert(!motion.update(0,1,0,499,r));assert(motion.update(0,1,0,500,r)&&r==0);
    assert(!motion.update(0,0,1,600,r));assert(!motion.update(1,0,0,700,r));assert(motion.update(1,0,0,1200,r)&&r==1);
    motion.reset();assert(!motion.update(1,1,0,1300,r));assert(!motion.update(4,0,0,1400,r));
    assert(!motion.update(0,-1,0,1500,r));assert(!motion.update(0,-1,0,1700,r));assert(motion.update(0,-1,0,2000,r)&&r==2);
    puts("waveshare349: RTC UTC/calendar/OS, touch axes/corners/errors and stable motion passed");
}
