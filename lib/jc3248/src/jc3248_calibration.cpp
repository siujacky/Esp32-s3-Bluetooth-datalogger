#include "jc3248_calibration.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace jc3248 {
namespace {
constexpr double MIN_SPAN = 32.0;
static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559,
              "Calibration records require IEEE-754 binary64 doubles");

bool finitePoint(const Point &p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// Independent axis normalization keeps the four-point solve well-scaled and
// makes polygon tests independent of controller orientation and resolution.
bool normalizeCorners(const Point points[4], Point normalized[4],
                      Point &center, Point &span) {
    if (!points) return false;
    Point lo=points[0],hi=points[0];
    for (unsigned i=0;i<4;++i) {
        if (!finitePoint(points[i]) || std::fabs(points[i].x)>65535.0 ||
            std::fabs(points[i].y)>65535.0) return false;
        if (points[i].x<lo.x) lo.x=points[i].x;
        if (points[i].x>hi.x) hi.x=points[i].x;
        if (points[i].y<lo.y) lo.y=points[i].y;
        if (points[i].y>hi.y) hi.y=points[i].y;
    }
    span={hi.x-lo.x,hi.y-lo.y};
    if (span.x<MIN_SPAN || span.y<MIN_SPAN) return false;
    center={lo.x+span.x/2.0,lo.y+span.y/2.0};
    for (unsigned i=0;i<4;++i) {
        normalized[i]={(points[i].x-center.x)/span.x,
                       (points[i].y-center.y)/span.y};
    }
    double orientation=0;
    for (unsigned i=0;i<4;++i) {
        const Point &a=normalized[i],&b=normalized[(i+1)%4],&c=normalized[(i+2)%4];
        const double cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if (std::fabs(cross)<1e-5) return false;
        if (i && cross*orientation<=0) return false;
        orientation=cross;
    }
    return true;
}

uint32_t checksum(const uint8_t *data,size_t size) {
    uint32_t crc=0xffffffffu;
    for (size_t i=0;i<size;++i) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;++bit) {
            crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
        }
    }
    return crc^0xffffffffu;
}
void putLittleEndian(uint8_t *data,uint64_t value,unsigned bytes) {
    for (unsigned i=0;i<bytes;++i) data[i]=static_cast<uint8_t>(value>>(8*i));
}
uint64_t readLittleEndian(const uint8_t *data,unsigned bytes) {
    uint64_t value=0;
    for (unsigned i=0;i<bytes;++i) value|=static_cast<uint64_t>(data[i])<<(8*i);
    return value;
}
} // namespace

bool validCalibration(const Calibration &c) {
    for (unsigned i=0;i<3;++i) {
        if (!std::isfinite(c.x[i]) || !std::isfinite(c.y[i])) return false;
    }
    // Controller coordinates are small. Bound gain, offset and conditioning so
    // a corrupt or numerically singular mapping cannot become an active record.
    if (std::fabs(c.x[2])>8192.0 || std::fabs(c.y[2])>8192.0) return false;
    const double a=c.x[0],b=c.x[1],d=c.y[0],e=c.y[1];
    if (std::fabs(a)>16 || std::fabs(b)>16 ||
        std::fabs(d)>16 || std::fabs(e)>16) return false;
    const double trace=a*a+b*b+d*d+e*e;
    const double determinant=a*e-b*d;
    const double discriminant=std::hypot(a*a+d*d-b*b-e*e,2*(a*b+d*e));
    const double maximum=std::sqrt((trace+discriminant)/2.0);
    if (maximum==0 || maximum>16.0) return false;
    const double minimum=std::fabs(determinant)/maximum;
    return minimum>=1.0/64.0 && maximum/minimum<=128.0;
}

bool mapCalibration(const Calibration &c,double rawX,double rawY,Point &out) {
    if (!validCalibration(c) || !std::isfinite(rawX) || !std::isfinite(rawY)) return false;
    const Point next={c.x[0]*rawX+c.x[1]*rawY+c.x[2],
                      c.y[0]*rawX+c.y[1]*rawY+c.y[2]};
    if (!finitePoint(next)) return false;
    out=next;
    return true;
}

bool fitCalibration(const Point raw[4],const Point target[4],Calibration &out,double &rms,unsigned width,unsigned height) {
    if(width<128||height<128||width>2048||height>2048)return false;
    Point normalized[4],targetNormalized[4],center,span,targetCenter,targetSpan;
    if (!normalizeCorners(raw,normalized,center,span) ||
        !normalizeCorners(target,targetNormalized,targetCenter,targetSpan)) return false;
    for (unsigned i=0;i<4;++i) {
        if (target[i].x<0 || target[i].x>width-1 || target[i].y<0 || target[i].y>height-1) return false;
    }
    // Center both inputs and targets, reducing the affine solve to a symmetric
    // 2x2 covariance system. The intercept is then recovered from the means.
    Point mean={0,0},targetMean={0,0};
    for (unsigned i=0;i<4;++i) {
        mean.x+=normalized[i].x/4;mean.y+=normalized[i].y/4;
        targetMean.x+=target[i].x/4;targetMean.y+=target[i].y/4;
    }
    double xx=0,xy=0,yy=0,xTargetX=0,yTargetX=0,xTargetY=0,yTargetY=0;
    for (unsigned i=0;i<4;++i) {
        const double x=normalized[i].x-mean.x,y=normalized[i].y-mean.y;
        const double tx=target[i].x-targetMean.x,ty=target[i].y-targetMean.y;
        xx+=x*x;xy+=x*y;yy+=y*y;
        xTargetX+=x*tx;yTargetX+=y*tx;
        xTargetY+=x*ty;yTargetY+=y*ty;
    }
    const double determinant=xx*yy-xy*xy;
    if (determinant<=1e-5*xx*yy) return false;
    const double ax=(yy*xTargetX-xy*yTargetX)/determinant;
    const double bx=(xx*yTargetX-xy*xTargetX)/determinant;
    const double ay=(yy*xTargetY-xy*yTargetY)/determinant;
    const double by=(xx*yTargetY-xy*xTargetY)/determinant;
    Calibration next;
    next.x[0]=ax/span.x;next.x[1]=bx/span.y;
    next.y[0]=ay/span.x;next.y[1]=by/span.y;
    next.x[2]=targetMean.x-ax*mean.x-bx*mean.y-next.x[0]*center.x-next.x[1]*center.y;
    next.y[2]=targetMean.y-ay*mean.x-by*mean.y-next.y[0]*center.x-next.y[1]*center.y;
    if (!validCalibration(next)) return false;
    double squared=0,maximum=0;
    for (unsigned i=0;i<4;++i) {
        Point mapped;
        if (!mapCalibration(next,raw[i].x,raw[i].y,mapped)) return false;
        const double error=std::hypot(mapped.x-target[i].x,mapped.y-target[i].y);
        squared+=error*error;
        if (error>maximum) maximum=error;
    }
    const double nextRms=std::sqrt(squared/4.0);
    if (!std::isfinite(nextRms) || nextRms>12.0 || maximum>20.0) return false;
    out=next;rms=nextRms;
    return true;
}

bool encodeCalibration(const Calibration &c,uint8_t *out,size_t size,unsigned width,unsigned height) {
    if (!out || size<CALIBRATION_BYTES || width<128 || height<128 || width>2048 || height>2048 || !validCalibration(c)) return false;
    uint8_t next[CALIBRATION_BYTES]={0x4a,0x43,0x43,0x31};
    putLittleEndian(next+4,1,2);
    putLittleEndian(next+6,width,2);
    putLittleEndian(next+8,height,2);
    for (unsigned i=0;i<6;++i) {
        const double value=i<3?c.x[i]:c.y[i-3];
        uint64_t bits;
        std::memcpy(&bits,&value,sizeof bits);
        putLittleEndian(next+12+8*i,bits,8);
    }
    putLittleEndian(next+60,checksum(next,60),4);
    std::memcpy(out,next,sizeof next);
    return true;
}

bool decodeCalibration(const uint8_t *data,size_t size,Calibration &out,unsigned width,unsigned height) {
    if (!data || size!=CALIBRATION_BYTES || width<128 || height<128 || width>2048 || height>2048 || std::memcmp(data,"JCC1",4)!=0 ||
        readLittleEndian(data+4,2)!=1 || readLittleEndian(data+6,2)!=width ||
        readLittleEndian(data+8,2)!=height || readLittleEndian(data+10,2)!=0 ||
        readLittleEndian(data+60,4)!=checksum(data,60)) return false;
    Calibration next;
    for (unsigned i=0;i<6;++i) {
        const uint64_t bits=readLittleEndian(data+12+8*i,8);
        double &value=i<3?next.x[i]:next.y[i-3];
        std::memcpy(&value,&bits,sizeof value);
    }
    if (!validCalibration(next)) return false;
    out=next;
    return true;
}
} // namespace jc3248
