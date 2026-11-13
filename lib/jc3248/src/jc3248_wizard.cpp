#include "jc3248_wizard.h"
#include <algorithm>
#include <cmath>
namespace jc3248 {
bool CalibrationWizard::configure(unsigned width,unsigned height,unsigned inset){
    if(width<128||height<128||width>2048||height>2048||inset<8||inset>std::min(width,height)/4)return false;
    width_=width;height_=height;inset_=inset;reset();return true;
}
void CalibrationWizard::reset(){const unsigned width=width_,height=height_,inset=inset_;*this=CalibrationWizard();width_=width;height_=height;inset_=inset;}
void CalibrationWizard::clearCapture(){count_=next_=0;held_=false;}
bool CalibrationWizard::captureReady()const{return held_&&count_>=8&&uint32_t(lastSample_-started_)>=160;}
Point CalibrationWizard::target()const{
    const double left=inset_,right=width_-1-inset_,top=inset_,bottom=height_-1-inset_;
    const Point targets[]={{left,top},{right,top},{right,bottom},{left,bottom},{(width_-1)/2.0,(height_-1)/2.0}};
    return targets[step_<5?step_:4];
}
bool CalibrationWizard::update(double x,double y,bool down,uint32_t now){
    if(ready_)return false;
    if(waitRelease_){if(!down){waitRelease_=false;clearCapture();return true;}return false;}
    if(down){
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>4095||y<0||y>4095){clearCapture();error_="Invalid touch data. Try again.";return true;}
        if(!held_||std::fabs(x-anchorX_)>12||std::fabs(y-anchorY_)>12){clearCapture();held_=true;started_=now;anchorX_=x;anchorY_=y;}
        xs_[next_]=x;ys_[next_]=y;next_=(next_+1)%16;if(count_<16)++count_;lastSample_=now;error_="";return true;
    }
    if(!held_)return false;
    const bool stable=captureReady()&&uint32_t(now-lastSample_)<=250;
    if(!stable){clearCapture();error_="Hold until green, then lift.";return true;}
    double xs[16],ys[16];for(unsigned i=0;i<count_;++i){xs[i]=xs_[i];ys[i]=ys_[i];}
    std::sort(xs,xs+count_);std::sort(ys,ys+count_);
    const Point point={count_%2?xs[count_/2]:(xs[count_/2-1]+xs[count_/2])/2,
                       count_%2?ys[count_/2]:(ys[count_/2-1]+ys[count_/2])/2};
    clearCapture();error_="";
    if(step_<4){
        corners_[step_++]=point;
        if(step_==4){
            const Point targets[]={{double(inset_),double(inset_)},{double(width_-1-inset_),double(inset_)},{double(width_-1-inset_),double(height_-1-inset_)},{double(inset_),double(height_-1-inset_)}};double rms=0;
            if(!fitCalibration(corners_,targets,candidate_,rms,width_,height_)){reset();waitRelease_=false;error_="Corners did not match. Repeat all four.";}
        }
        return true;
    }
    Point mapped{};
    const Point centre=target();
    if(mapCalibration(candidate_,point.x,point.y,mapped)&&std::hypot(mapped.x-centre.x,mapped.y-centre.y)<=24){ready_=true;return true;}
    if(++misses_>=3){reset();waitRelease_=false;error_="Centre mismatch. Repeat the four corners.";}
    else error_="Centre missed. Tap the centre cross again.";
    return true;
}
void CalibrationWizard::retryVerification(){
    if(!validCalibration(candidate_)){reset();return;}
    step_=4;ready_=false;misses_=0;waitRelease_=true;error_="";clearCapture();
}
}
