#pragma once
#include <string>
#include "qrcodegen.h"

namespace meter_wifi {
struct Qr {
    uint8_t data[qrcodegen_BUFFER_LEN_FOR_VERSION(10)]={};
    unsigned size() const {return data[0]?qrcodegen_getSize(data):0;}
    bool dark(unsigned x,unsigned y) const {return qrcodegen_getModule(data,x,y);}
};
std::string payload(const std::string &ssid,const std::string &password);
bool encode(const std::string &ssid,const std::string &password,Qr &out);
}
