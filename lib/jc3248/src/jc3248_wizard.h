#pragma once
#include "jc3248_calibration.h"
#include <stdint.h>
namespace jc3248 {
class CalibrationWizard {
public:
    bool configure(unsigned width,unsigned height,unsigned inset=32);
    void reset();
    bool update(double rawX,double rawY,bool down,uint32_t now);
    unsigned step()const{return step_;}
    unsigned samples()const{return count_;}
    bool captureReady()const;
    bool ready()const{return ready_;}
    Point target()const;
    const Calibration &calibration()const{return candidate_;}
    const char *error()const{return error_;}
    void retryVerification();
private:
    unsigned width_=480,height_=320,inset_=32;
    void clearCapture();
    unsigned step_=0,count_=0,next_=0,misses_=0;
    bool waitRelease_=true,held_=false,ready_=false;
    uint32_t started_=0,lastSample_=0;
    double xs_[16]={},ys_[16]={},anchorX_=0,anchorY_=0;
    Point corners_[4]={};Calibration candidate_;
    const char *error_="";
};
}
