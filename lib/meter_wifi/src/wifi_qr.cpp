#include "wifi_qr.h"
#include <string.h>
namespace meter_wifi {
static std::string escape(const std::string &s) {
    std::string out;
    for(char c:s){if(c=='\\'||c==';'||c==','||c==':'||c=='"')out+='\\';out+=c;}
    return out;
}
std::string payload(const std::string &ssid,const std::string &password) {
    return "WIFI:T:WPA;S:"+escape(ssid)+";P:"+escape(password)+";;";
}
bool encode(const std::string &ssid,const std::string &password,Qr &out) {
    memset(out.data,0,sizeof(out.data));
    if(ssid.empty()||ssid.size()>32||password.size()<8||password.size()>63||
       ssid.find('\0')!=std::string::npos||password.find('\0')!=std::string::npos)return false;
    uint8_t scratch[sizeof(out.data)];
    const std::string text=payload(ssid,password);
    return qrcodegen_encodeText(text.c_str(),scratch,out.data,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,false);
}
}
