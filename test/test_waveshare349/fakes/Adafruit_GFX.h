#pragma once
#include "Arduino.h"
class GFXcanvas16 {
protected:
    uint16_t *buffer;const unsigned w,h;
public:
    GFXcanvas16(unsigned width,unsigned height):buffer(static_cast<uint16_t *>(malloc(width*height*2))),w(width),h(height){}
    virtual ~GFXcanvas16(){free(buffer);}
    uint16_t *getBuffer(){return buffer;}
    unsigned getRotation(){return 0;}
    void fillScreen(uint16_t color){std::fill(buffer,buffer+w*h,color);}
};
