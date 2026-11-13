#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <limits>
namespace meter_ui {
enum class Padding:uint8_t {Blank,Zero,Invalid};
struct NumberFormat {
    uint8_t digits,decimals;Padding padding;
    NumberFormat(uint8_t d=6,uint8_t p=2,Padding pad=Padding::Blank):digits(d),decimals(p),padding(pad){}
};
enum class NumberState:uint8_t {Numeric,Overflow,Unavailable,Invalid};
struct FormattedNumber {NumberState state=NumberState::Invalid;char cells[12]="ERR";uint8_t decimalIndex=255;};
inline bool validNumberFormat(const NumberFormat &f){return f.digits>=3&&f.digits<=8&&f.decimals<=6&&f.decimals<f.digits&&(f.padding==Padding::Blank||f.padding==Padding::Zero);}
inline unsigned numberCellCount(const NumberFormat &f){return unsigned(f.digits)+1+(f.decimals?1:0);}
inline FormattedNumber formatNumber(double value,const NumberFormat &f,bool available=true){
    FormattedNumber out;
    if(!validNumberFormat(f))return out;
    if(!available){out.state=NumberState::Unavailable;std::strcpy(out.cells,"--");return out;}
    if(!std::isfinite(value))return out;
    double scale=1,limit=1;
    for(unsigned i=0;i<f.decimals;++i)scale*=10;
    for(unsigned i=0;i<f.digits;++i)limit*=10;
    const double scaled=std::fabs(value)*scale;
    if(!std::isfinite(scaled)||scaled>limit){out.state=NumberState::Overflow;std::strcpy(out.cells,"OVR");return out;}
    // Stabilize decimal half cases represented immediately below a binary
    // floating boundary. Shared JS vectors pin the same four-ULP tolerance.
    const double rounded=std::floor(scaled+.5+4*std::numeric_limits<double>::epsilon()*scaled);
    if(rounded>=limit){out.state=NumberState::Overflow;std::strcpy(out.cells,"OVR");return out;}
    char digits[12];std::snprintf(digits,sizeof(digits),"%0*lu",int(f.digits),static_cast<unsigned long>(rounded));
    const unsigned integer=f.digits-f.decimals;
    out.cells[0]=' ';unsigned at=1;
    for(unsigned i=0;i<f.digits;++i){
        if(f.decimals&&i==integer){out.decimalIndex=uint8_t(at);out.cells[at++]='.';}
        out.cells[at++]=digits[i];
    }
    out.cells[at]=0;
    if(f.padding==Padding::Blank)for(unsigned i=1;i<integer&&out.cells[i]=='0';++i)out.cells[i]=' ';
    if(value<0&&rounded>0){unsigned sign=0;if(f.padding==Padding::Blank){unsigned first=1;while(out.cells[first]==' ')++first;sign=first-1;}out.cells[sign]='-';}
    out.state=NumberState::Numeric;return out;
}
}
