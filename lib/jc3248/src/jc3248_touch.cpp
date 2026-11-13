#include "jc3248_touch.h"
namespace jc3248 {
bool transformTouch(uint16_t x,uint16_t y,unsigned from,unsigned to,unsigned width,unsigned height,uint16_t &outX,uint16_t &outY){
    if(from>3||to>3||!width||!height||width>4096||height>4096||x>=((from&1)?height:width)||y>=((from&1)?width:height))return false;
    unsigned rawX=x,rawY=y;
    switch(from){case 1:rawX=width-1-y;rawY=x;break;case 2:rawX=width-1-x;rawY=height-1-y;break;case 3:rawX=y;rawY=height-1-x;break;default:break;}
    unsigned nextX=rawX,nextY=rawY;
    switch(to){case 1:nextX=rawY;nextY=width-1-rawX;break;case 2:nextX=width-1-rawX;nextY=height-1-rawY;break;case 3:nextX=height-1-rawY;nextY=rawX;break;default:break;}
    outX=uint16_t(nextX);outY=uint16_t(nextY);return true;
}
bool decodeTouch(const uint8_t *data,size_t size,unsigned rotation,uint16_t &x,uint16_t &y,bool &down,unsigned width,unsigned height){
    if(!data||size!=8||rotation>3||!width||!height||width>4096||height>4096)return false;
    // Both uniform markers were captured on this JC3248 after finger-up.
    // A missing release leaves the gesture held and loses the next edge start.
    // Match the entire known marker, retaining rejection of corrupt/mixed data.
    bool idle=data[0]==0x23||data[0]==0x63;
    for(size_t i=1;idle&&i<size;++i)if(data[i]!=data[0])idle=false;
    if(idle){down=false;return true;}
    if(data[1]>1)return false;
    if(!data[1]){down=false;return true;}
    const unsigned event=data[2]>>6;
    if(event==1||event==3){down=false;return true;}
    const uint16_t rawX=((data[2]&15)<<8)|data[3],rawY=((data[4]&15)<<8)|data[5];
    if(rawX>=width||rawY>=height)return false;
    uint16_t nextX=rawX,nextY=rawY;
    switch(rotation){case 1:nextX=rawY;nextY=width-1-rawX;break;
    case 2:nextX=width-1-rawX;nextY=height-1-rawY;break;
    case 3:nextX=height-1-rawY;nextY=rawX;break;default:break;}
    x=nextX;y=nextY;down=true;return true;
}
}
